#ifndef RIGI_CODEC_HTTP1_H_
#define RIGI_CODEC_HTTP1_H_

#include "Rigi_Codec.hpp"
#include <string>
#include <map>
#include <cctype>

namespace Rigitaeda {

// HTTP/1.1 요청 구조체
struct Rigi_HTTP_Request {
    bool        valid   = false;
    std::string method;                              // GET POST PUT DELETE ...
    std::string uri;                                 // /path?query
    std::string version;                             // HTTP/1.1
    std::map<std::string, std::string> headers;      // 소문자 키 정규화
    std::vector<char> body;

    // 헤더 조회 (대소문자 무관, 없으면 "")
    std::string header(const std::string& name) const
    {
        std::string key = http_lower(name);
        auto it = headers.find(key);
        return (it != headers.end()) ? it->second : "";
    }

    bool keep_alive() const
    {
        std::string conn = header("connection");
        if (version == "HTTP/1.0") return http_lower(conn) == "keep-alive";
        return http_lower(conn) != "close";
    }

    // 코덱이 emit한 raw 바이트를 파싱 (chunked body 자동 디청크)
    static Rigi_HTTP_Request parse(const char* data, size_t len);

private:
    static std::string http_lower(std::string s)
    {
        for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    }
};

// HTTP/1.1 응답 구조체
struct Rigi_HTTP_Response {
    int         status = 200;
    std::string reason;
    std::map<std::string, std::string> headers;
    std::vector<char> body;

    // 응답을 raw 바이트로 직렬화 (Content-Length 자동 추가)
    std::vector<char> serialize() const;

    // 간편 생성: make(200, "application/json", "{}")
    static Rigi_HTTP_Response make(int status_code,
                                    const std::string& content_type,
                                    const std::string& body_str);

    static const char* default_reason(int code);
};

// HTTP/1.1 코덱 — 완성된 HTTP 메시지(요청 또는 응답) 단위로 emit
// Content-Length 및 chunked Transfer-Encoding 모두 처리
class Rigi_Codec_HTTP1 : public Rigi_Codec {
public:
    void decode(const char* data, size_t len,
                std::function<void(const char*, size_t)> emit) override
    {
        m_buf.insert(m_buf.end(), data, data + len);
        bool progress = true;
        while (progress && !m_buf.empty()) {
            progress = false;
            if      (m_state == State::Header)       progress = step_header(emit);
            else if (m_state == State::BodyFixed)    progress = step_fixed(emit);
            else if (m_state == State::BodyChunked)  progress = step_chunked(emit);
        }
    }

    // encode: Rigi_HTTP_Response::serialize()를 사용 — pass-through
    std::vector<char> encode(const char* data, size_t len) override
    {
        return std::vector<char>(data, data + len);
    }

private:
    enum class State { Header, BodyFixed, BodyChunked };

    std::vector<char> m_buf;
    State  m_state    = State::Header;
    size_t m_hdr_len  = 0;  // 헤더 블록 크기 (\r\n\r\n 포함)
    size_t m_body_rem = 0;  // BodyFixed 모드: 남은 바이트 수

    // ── 헤더 완료 여부 확인 및 body 전략 결정 ────────────────────────────────

    bool step_header(std::function<void(const char*, size_t)>& emit)
    {
        static const char delim[4] = {'\r','\n','\r','\n'};
        auto it = std::search(m_buf.begin(), m_buf.end(), delim, delim + 4);
        if (it == m_buf.end()) return false;

        m_hdr_len = static_cast<size_t>(std::distance(m_buf.begin(), it)) + 4;

        // 헤더 블록에서 Content-Length / Transfer-Encoding 추출
        bool   chunked           = false;
        size_t content_length    = 0;
        bool   has_content_length = false;

        // 첫 줄 다음부터 헤더 파싱
        size_t pos = 0;
        bool   first = true;
        while (pos < m_hdr_len - 4) {
            size_t eol = find_crlf(pos);
            if (eol == npos || eol >= m_hdr_len - 4) break;
            if (first) { first = false; pos = eol + 2; continue; }

            std::string line(m_buf.begin() + static_cast<ptrdiff_t>(pos),
                             m_buf.begin() + static_cast<ptrdiff_t>(eol));
            size_t colon = line.find(':');
            if (colon != std::string::npos) {
                std::string name = lower(line.substr(0, colon));
                std::string val  = trim(line.substr(colon + 1));
                if (name == "content-length") {
                    try { content_length = std::stoul(val); has_content_length = true; }
                    catch (...) {}
                } else if (name == "transfer-encoding") {
                    chunked = (lower(val).find("chunked") != std::string::npos);
                }
            }
            pos = eol + 2;
        }

        // body 전략 결정
        if (chunked) {
            m_state = State::BodyChunked;
            return step_chunked(emit);
        }
        if (has_content_length && content_length > 0) {
            m_state   = State::BodyFixed;
            m_body_rem = content_length;
            return step_fixed(emit);
        }
        // body 없음 (GET / HEAD / 204 / 304 등)
        emit(m_buf.data(), m_hdr_len);
        m_buf.erase(m_buf.begin(),
                    m_buf.begin() + static_cast<ptrdiff_t>(m_hdr_len));
        m_hdr_len = 0;
        return true;
    }

    // ── Content-Length body ───────────────────────────────────────────────────

    bool step_fixed(std::function<void(const char*, size_t)>& emit)
    {
        if (m_buf.size() < m_hdr_len + m_body_rem) return false;
        size_t total = m_hdr_len + m_body_rem;
        emit(m_buf.data(), total);
        m_buf.erase(m_buf.begin(),
                    m_buf.begin() + static_cast<ptrdiff_t>(total));
        reset();
        return true;
    }

    // ── Chunked body ─────────────────────────────────────────────────────────
    // chunk-size CRLF chunk-data CRLF ... 0 CRLF CRLF 패턴을 추적하여
    // 전체 원시 메시지(헤더+청크 바디)를 한 번에 emit

    bool step_chunked(std::function<void(const char*, size_t)>& emit)
    {
        size_t pos = m_hdr_len;
        while (true) {
            size_t crlf = find_crlf(pos);
            if (crlf == npos) return false;

            // chunk-size 줄 파싱 (확장자 ';...' 무시)
            std::string size_line(m_buf.begin() + static_cast<ptrdiff_t>(pos),
                                  m_buf.begin() + static_cast<ptrdiff_t>(crlf));
            size_t semi = size_line.find(';');
            if (semi != std::string::npos) size_line = size_line.substr(0, semi);
            size_line = trim(size_line);

            size_t chunk_size = 0;
            try { chunk_size = std::stoul(size_line, nullptr, 16); }
            catch (...) { return false; }

            if (chunk_size == 0) {
                // 마지막 청크: "0\r\n" 다음에 트레일러 + 빈 줄("\r\n")
                size_t end = crlf + 2;
                size_t trailer_end = find_crlf(end);
                if (trailer_end == npos) return false;
                size_t total = trailer_end + 2;
                emit(m_buf.data(), total);
                m_buf.erase(m_buf.begin(),
                            m_buf.begin() + static_cast<ptrdiff_t>(total));
                reset();
                return true;
            }

            // 청크 데이터 + 후미 CRLF
            size_t data_start = crlf + 2;
            size_t data_end   = data_start + chunk_size;
            if (m_buf.size() < data_end + 2) return false;
            pos = data_end + 2;
        }
    }

    // ── 유틸 ─────────────────────────────────────────────────────────────────

    void reset()
    {
        m_state    = State::Header;
        m_hdr_len  = 0;
        m_body_rem = 0;
    }

    static constexpr size_t npos = static_cast<size_t>(-1);

    size_t find_crlf(size_t off) const
    {
        for (size_t i = off; i + 1 < m_buf.size(); ++i)
            if (m_buf[i] == '\r' && m_buf[i + 1] == '\n') return i;
        return npos;
    }

    static std::string lower(std::string s)
    {
        for (char& c : s)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    }

    static std::string trim(const std::string& s)
    {
        size_t a = s.find_first_not_of(" \t\r\n");
        size_t b = s.find_last_not_of(" \t\r\n");
        return (a == std::string::npos) ? "" : s.substr(a, b - a + 1);
    }
};

// ── inline 구현 ──────────────────────────────────────────────────────────────

inline Rigi_HTTP_Request Rigi_HTTP_Request::parse(const char* data, size_t len)
{
    Rigi_HTTP_Request req;
    const std::string raw(data, len);

    // 헤더/바디 분리
    size_t hdr_end = raw.find("\r\n\r\n");
    if (hdr_end == std::string::npos) return req;

    std::string hdr_block = raw.substr(0, hdr_end);

    // 첫 줄 파싱
    size_t line_end = hdr_block.find("\r\n");
    std::string first = (line_end != std::string::npos)
                        ? hdr_block.substr(0, line_end) : hdr_block;

    size_t sp1 = first.find(' ');
    size_t sp2 = first.rfind(' ');
    if (sp1 == std::string::npos || sp1 == sp2) return req;

    req.method  = first.substr(0, sp1);
    req.uri     = first.substr(sp1 + 1, sp2 - sp1 - 1);
    req.version = first.substr(sp2 + 1);

    // 헤더 파싱 (소문자 키)
    size_t pos = (line_end != std::string::npos) ? line_end + 2 : hdr_block.size();
    while (pos < hdr_block.size()) {
        size_t end   = hdr_block.find("\r\n", pos);
        std::string line = (end != std::string::npos)
                           ? hdr_block.substr(pos, end - pos)
                           : hdr_block.substr(pos);
        size_t colon = line.find(':');
        if (colon != std::string::npos) {
            std::string name = http_lower(line.substr(0, colon));
            std::string val;
            size_t vs = line.find_first_not_of(" \t", colon + 1);
            if (vs != std::string::npos) val = line.substr(vs);
            req.headers[name] = val;
        }
        pos = (end != std::string::npos) ? end + 2 : hdr_block.size();
    }

    // 바디 추출 (chunked 자동 디청크)
    size_t body_start = hdr_end + 4;
    auto te_it = req.headers.find("transfer-encoding");
    bool chunked = (te_it != req.headers.end() &&
                    http_lower(te_it->second).find("chunked") != std::string::npos);

    if (chunked) {
        size_t p = body_start;
        while (p < len) {
            size_t crlf = raw.find("\r\n", p);
            if (crlf == std::string::npos) break;
            std::string sz = raw.substr(p, crlf - p);
            size_t semi = sz.find(';');
            if (semi != std::string::npos) sz = sz.substr(0, semi);
            // trim
            size_t a = sz.find_first_not_of(" \t");
            if (a != std::string::npos) sz = sz.substr(a);

            size_t chunk_size = 0;
            try { chunk_size = std::stoul(sz, nullptr, 16); } catch (...) { break; }
            if (chunk_size == 0) break;

            p = crlf + 2;
            if (p + chunk_size > len) break;
            req.body.insert(req.body.end(), data + p, data + p + chunk_size);
            p += chunk_size + 2;
        }
    } else {
        if (body_start < len)
            req.body.assign(data + body_start, data + len);
    }

    req.valid = true;
    return req;
}

inline std::vector<char> Rigi_HTTP_Response::serialize() const
{
    std::string s = "HTTP/1.1 " + std::to_string(status) + " " + reason + "\r\n";

    bool has_cl = headers.count("Content-Length") || headers.count("content-length");
    for (const auto& h : headers)
        s += h.first + ": " + h.second + "\r\n";
    if (!has_cl && !body.empty())
        s += "Content-Length: " + std::to_string(body.size()) + "\r\n";

    s += "\r\n";

    std::vector<char> out(s.begin(), s.end());
    out.insert(out.end(), body.begin(), body.end());
    return out;
}

inline Rigi_HTTP_Response Rigi_HTTP_Response::make(
    int status_code,
    const std::string& content_type,
    const std::string& body_str)
{
    Rigi_HTTP_Response resp;
    resp.status = status_code;
    resp.reason = default_reason(status_code);
    resp.headers["Content-Type"]   = content_type;
    resp.headers["Content-Length"] = std::to_string(body_str.size());
    resp.body.assign(body_str.begin(), body_str.end());
    return resp;
}

inline const char* Rigi_HTTP_Response::default_reason(int code)
{
    switch (code) {
        case 100: return "Continue";
        case 101: return "Switching Protocols";
        case 200: return "OK";
        case 201: return "Created";
        case 204: return "No Content";
        case 206: return "Partial Content";
        case 301: return "Moved Permanently";
        case 302: return "Found";
        case 304: return "Not Modified";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 408: return "Request Timeout";
        case 409: return "Conflict";
        case 413: return "Payload Too Large";
        case 429: return "Too Many Requests";
        case 500: return "Internal Server Error";
        case 502: return "Bad Gateway";
        case 503: return "Service Unavailable";
        case 504: return "Gateway Timeout";
        default:  return "Unknown";
    }
}

} // namespace Rigitaeda

#endif
