#ifndef RIGI_CODEC_SYSLOG_H_
#define RIGI_CODEC_SYSLOG_H_

#include "Rigi_Codec.hpp"
#include <string>
#include <cctype>

namespace Rigitaeda {

// RFC 3164 / RFC 5424 파싱 결과
struct Rigi_Syslog_Message {
    bool        valid    = false;
    int         priority = 0;  // raw PRI
    int         facility = 0;  // 0-23
    int         severity = 0;  // 0-7
    int         version  = 0;  // 0=RFC3164, 1=RFC5424
    std::string timestamp;
    std::string hostname;
    std::string app_name;
    std::string proc_id;
    std::string msg_id;
    std::string structured_data;
    std::string message;

    static const char* severity_name(int sev)
    {
        static const char* t[] = {
            "Emergency","Alert","Critical","Error",
            "Warning","Notice","Info","Debug"
        };
        return (sev >= 0 && sev <= 7) ? t[sev] : "Unknown";
    }

    static const char* facility_name(int fac)
    {
        static const char* t[] = {
            "kernel","user","mail","system","security","syslogd",
            "printer","news","uucp","clock","security2","ftp","ntp",
            "audit","alert","clock2",
            "local0","local1","local2","local3","local4","local5","local6","local7"
        };
        return (fac >= 0 && fac <= 23) ? t[fac] : "Unknown";
    }

    // RFC 3164 / RFC 5424 자동 감지 파싱
    static Rigi_Syslog_Message parse(const char* data, size_t len);

    // RFC 3164 형식으로 인코딩 (framing 없이 메시지 바이트만 반환)
    static std::vector<char> encode(int facility, int severity,
                                    const std::string& hostname,
                                    const std::string& app_name,
                                    const std::string& message);
};

// TCP Syslog 코덱 — RFC 6587
// Auto          : 첫 바이트가 숫자면 OctetCounting, 아니면 Newline으로 자동 판별
// Newline       : '\n' 구분 (RFC 3164 전통 방식)
// OctetCounting : "LENGTH SP MESSAGE" 형식 (RFC 5425 / RFC 6587 권장)
class Rigi_Codec_Syslog : public Rigi_Codec {
public:
    enum class Framing { Auto, Newline, OctetCounting };

    explicit Rigi_Codec_Syslog(Framing framing = Framing::Auto)
        : m_framing(framing), m_octet_mode(false), m_detected(false) {}

    void decode(const char* data, size_t len,
                std::function<void(const char*, size_t)> emit) override
    {
        m_buffer.insert(m_buffer.end(), data, data + len);
        while (!m_buffer.empty()) {
            if (!m_detected) {
                if      (m_framing == Framing::OctetCounting) { m_octet_mode = true;  m_detected = true; }
                else if (m_framing == Framing::Newline)        { m_octet_mode = false; m_detected = true; }
                else {
                    m_octet_mode = (m_buffer[0] >= '1' && m_buffer[0] <= '9');
                    m_detected   = true;
                }
            }
            bool ok = m_octet_mode ? try_octet(emit) : try_newline(emit);
            if (!ok) break;
        }
    }

    std::vector<char> encode(const char* data, size_t len) override
    {
        if (m_octet_mode || m_framing == Framing::OctetCounting) {
            std::string prefix = std::to_string(len) + " ";
            std::vector<char> out(prefix.begin(), prefix.end());
            out.insert(out.end(), data, data + len);
            return out;
        }
        std::vector<char> out(data, data + len);
        if (out.empty() || out.back() != '\n')
            out.push_back('\n');
        return out;
    }

private:
    Framing           m_framing;
    bool              m_octet_mode;
    bool              m_detected;
    std::vector<char> m_buffer;

    bool try_octet(std::function<void(const char*, size_t)>& emit)
    {
        auto sp = std::find(m_buffer.begin(), m_buffer.end(), ' ');
        if (sp == m_buffer.end()) return false;

        std::string len_str(m_buffer.begin(), sp);
        size_t msg_len = 0;
        try { msg_len = std::stoul(len_str); } catch (...) { m_buffer.clear(); return false; }

        size_t offset = static_cast<size_t>(std::distance(m_buffer.begin(), sp)) + 1;
        if (m_buffer.size() < offset + msg_len) return false;

        emit(m_buffer.data() + offset, msg_len);
        m_buffer.erase(m_buffer.begin(),
                       m_buffer.begin() + static_cast<ptrdiff_t>(offset + msg_len));
        return true;
    }

    bool try_newline(std::function<void(const char*, size_t)>& emit)
    {
        auto nl = std::find(m_buffer.begin(), m_buffer.end(), '\n');
        if (nl == m_buffer.end()) return false;

        size_t len     = static_cast<size_t>(std::distance(m_buffer.begin(), nl));
        size_t emit_len = (len > 0 && m_buffer[len - 1] == '\r') ? len - 1 : len;
        if (emit_len > 0)
            emit(m_buffer.data(), emit_len);
        m_buffer.erase(m_buffer.begin(), nl + 1);
        return true;
    }
};

// ── inline 구현 ──────────────────────────────────────────────────────────────

inline Rigi_Syslog_Message Rigi_Syslog_Message::parse(const char* data, size_t len)
{
    Rigi_Syslog_Message msg;
    std::string s(data, len);
    size_t pos = 0;

    if (s.empty() || s[0] != '<') return msg;
    size_t close = s.find('>');
    if (close == std::string::npos || close < 2) return msg;

    try { msg.priority = std::stoi(s.substr(1, close - 1)); } catch (...) { return msg; }
    msg.facility = msg.priority >> 3;
    msg.severity = msg.priority & 0x07;
    pos = close + 1;

    // RFC 5424: PRI 바로 뒤가 버전 숫자 + 공백
    if (pos < s.size() && s[pos] >= '1' && s[pos] <= '9' &&
        pos + 1 < s.size() && s[pos + 1] == ' ')
    {
        msg.version = s[pos] - '0';
        pos += 2;

        auto next_field = [&]() -> std::string {
            if (pos >= s.size()) return "";
            size_t end = s.find(' ', pos);
            std::string f = (end == std::string::npos) ? s.substr(pos) : s.substr(pos, end - pos);
            pos = (end == std::string::npos) ? s.size() : end + 1;
            return (f == "-") ? "" : f;
        };

        msg.timestamp = next_field();
        msg.hostname  = next_field();
        msg.app_name  = next_field();
        msg.proc_id   = next_field();
        msg.msg_id    = next_field();

        // STRUCTURED-DATA: "-" 또는 "[...]" (내부에 공백 포함 가능)
        if (pos < s.size() && s[pos] == '[') {
            size_t sd_end = s.find("] ", pos);
            if (sd_end != std::string::npos) {
                msg.structured_data = s.substr(pos, sd_end - pos + 1);
                pos = sd_end + 2;
            }
        } else {
            next_field();  // "-" skip
        }
        msg.message = (pos < s.size()) ? s.substr(pos) : "";
    }
    else
    {
        // RFC 3164: TIMESTAMP(15자) HOST TAG: MSG
        msg.version = 0;
        if (pos + 15 <= s.size()) { msg.timestamp = s.substr(pos, 15); pos += 16; }

        size_t sp = s.find(' ', pos);
        if (sp != std::string::npos) { msg.hostname = s.substr(pos, sp - pos); pos = sp + 1; }

        // TAG는 ':' 또는 '[' 까지
        size_t colon = s.find(':', pos);
        size_t brack = s.find('[', pos);
        size_t tag_end = std::min(colon, brack);
        if (tag_end != std::string::npos) {
            msg.app_name = s.substr(pos, tag_end - pos);
            pos = (colon != std::string::npos && colon <= brack) ? colon + 2 : tag_end;
        }
        msg.message = (pos < s.size()) ? s.substr(pos) : "";
    }

    msg.valid = true;
    return msg;
}

inline std::vector<char> Rigi_Syslog_Message::encode(
    int facility, int severity,
    const std::string& hostname,
    const std::string& app_name,
    const std::string& message)
{
    int pri = ((facility & 0x1F) << 3) | (severity & 0x07);
    std::string s = "<" + std::to_string(pri) + ">"
                  + hostname + " " + app_name + ": " + message;
    return std::vector<char>(s.begin(), s.end());
}

} // namespace Rigitaeda

#endif
