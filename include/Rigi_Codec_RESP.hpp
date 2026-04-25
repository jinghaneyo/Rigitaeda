#ifndef RIGI_CODEC_RESP_H_
#define RIGI_CODEC_RESP_H_

#include "Rigi_Codec.hpp"
#include <string>
#include <stdexcept>

namespace Rigitaeda {

// Redis Serialization Protocol (RESP2) 값 타입
struct Rigi_RESP_Value {
    enum class Type { SimpleString, Error, Integer, BulkString, Array, Null };

    Type                         type    = Type::Null;
    std::string                  str;
    int64_t                      integer = 0;
    std::vector<Rigi_RESP_Value> array;

    bool is_null()   const { return type == Type::Null; }
    bool is_ok()     const { return type == Type::SimpleString && str == "OK"; }
    bool is_error()  const { return type == Type::Error; }
    bool is_int()    const { return type == Type::Integer; }
    bool is_string() const { return type == Type::SimpleString || type == Type::BulkString; }
    bool is_array()  const { return type == Type::Array; }

    // 코덱이 emit한 raw 바이트를 Rigi_RESP_Value로 파싱
    static Rigi_RESP_Value parse(const char* data, size_t len);

    // ── 송신용 인코딩 헬퍼 ──────────────────────────────────────────────────

    // Redis 커맨드: SET key value → encode_command({"SET","key","value"})
    static std::vector<char> encode_command(std::initializer_list<std::string> args);

    // +OK\r\n 형식 응답
    static std::vector<char> encode_simple(const std::string& s);

    // -ERR ...\r\n 형식 에러
    static std::vector<char> encode_error(const std::string& s);

    // :N\r\n 형식 정수
    static std::vector<char> encode_integer(int64_t n);

    // $N\r\n...\r\n 형식 벌크 문자열
    static std::vector<char> encode_bulk(const std::string& s);

    // $-1\r\n null 벌크
    static std::vector<char> encode_null_bulk();

    // *N\r\n... 형식 배열 (각 원소를 이미 인코딩된 벡터로 받음)
    static std::vector<char> encode_array(const std::vector<std::vector<char>>& items);
};

// RESP 코덱 — 완성된 RESP 메시지(커맨드/응답) 단위로 emit
class Rigi_Codec_RESP : public Rigi_Codec {
public:
    void decode(const char* data, size_t len,
                std::function<void(const char*, size_t)> emit) override
    {
        m_buf.insert(m_buf.end(), data, data + len);
        while (!m_buf.empty()) {
            size_t consumed = 0;
            if (!complete(0, consumed)) break;
            emit(m_buf.data(), consumed);
            m_buf.erase(m_buf.begin(),
                        m_buf.begin() + static_cast<ptrdiff_t>(consumed));
        }
    }

    // RESP 인코딩은 Rigi_RESP_Value 헬퍼로 처리 — pass-through
    std::vector<char> encode(const char* data, size_t len) override
    {
        return std::vector<char>(data, data + len);
    }

private:
    std::vector<char> m_buf;

    size_t find_crlf(size_t off) const
    {
        for (size_t i = off; i + 1 < m_buf.size(); ++i)
            if (m_buf[i] == '\r' && m_buf[i + 1] == '\n') return i;
        return static_cast<size_t>(-1);
    }

    // off 위치에서 시작하는 완전한 RESP 값의 바이트 수를 consumed에 설정
    bool complete(size_t off, size_t& consumed) const
    {
        if (off >= m_buf.size()) return false;
        char t = m_buf[off];
        size_t crlf = find_crlf(off + 1);
        if (crlf == static_cast<size_t>(-1)) return false;

        std::string line(m_buf.begin() + static_cast<ptrdiff_t>(off + 1),
                         m_buf.begin() + static_cast<ptrdiff_t>(crlf));
        size_t after = crlf + 2;

        switch (t) {
            case '+': case '-': case ':':
                consumed = after;
                return true;

            case '$': {
                int64_t n = 0;
                try { n = std::stoll(line); } catch (...) { return false; }
                if (n < 0) { consumed = after; return true; }  // null bulk
                if (m_buf.size() < after + static_cast<size_t>(n) + 2) return false;
                consumed = after + static_cast<size_t>(n) + 2;
                return true;
            }

            case '*': {
                int64_t count = 0;
                try { count = std::stoll(line); } catch (...) { return false; }
                if (count < 0) { consumed = after; return true; }  // null array
                size_t cur = after;
                for (int64_t i = 0; i < count; ++i) {
                    size_t elem = 0;
                    if (!complete(cur, elem)) return false;
                    cur += elem;
                }
                consumed = cur;
                return true;
            }

            default:
                return false;
        }
    }
};

// ── inline 구현 ──────────────────────────────────────────────────────────────

namespace detail {
    inline size_t resp_crlf(const char* data, size_t off, size_t len)
    {
        for (size_t i = off; i + 1 < len; ++i)
            if (data[i] == '\r' && data[i + 1] == '\n') return i;
        return static_cast<size_t>(-1);
    }

    inline Rigi_RESP_Value resp_parse_at(const char* data, size_t off,
                                          size_t len, size_t& consumed)
    {
        Rigi_RESP_Value val;
        if (off >= len) throw std::runtime_error("RESP: buffer empty");

        char t = data[off];
        size_t crlf = resp_crlf(data, off + 1, len);
        if (crlf == static_cast<size_t>(-1)) throw std::runtime_error("RESP: incomplete");

        std::string line(data + off + 1, data + crlf);
        size_t after = crlf + 2;

        switch (t) {
            case '+':
                val.type = Rigi_RESP_Value::Type::SimpleString;
                val.str  = line;
                consumed = after;
                break;
            case '-':
                val.type = Rigi_RESP_Value::Type::Error;
                val.str  = line;
                consumed = after;
                break;
            case ':':
                val.type    = Rigi_RESP_Value::Type::Integer;
                try { val.integer = std::stoll(line); } catch (...) {}
                consumed = after;
                break;
            case '$': {
                int64_t n = std::stoll(line);
                if (n < 0) { val.type = Rigi_RESP_Value::Type::Null; consumed = after; break; }
                val.type = Rigi_RESP_Value::Type::BulkString;
                val.str  = std::string(data + after, static_cast<size_t>(n));
                consumed = after + static_cast<size_t>(n) + 2;
                break;
            }
            case '*': {
                int64_t count = std::stoll(line);
                if (count < 0) { val.type = Rigi_RESP_Value::Type::Null; consumed = after; break; }
                val.type = Rigi_RESP_Value::Type::Array;
                size_t cur = after;
                for (int64_t i = 0; i < count; ++i) {
                    size_t elem = 0;
                    val.array.push_back(resp_parse_at(data, cur, len, elem));
                    cur += elem;
                }
                consumed = cur;
                break;
            }
            default:
                throw std::runtime_error("RESP: unknown type");
        }
        return val;
    }
} // namespace detail

inline Rigi_RESP_Value Rigi_RESP_Value::parse(const char* data, size_t len)
{
    size_t consumed = 0;
    return detail::resp_parse_at(data, 0, len, consumed);
}

inline std::vector<char> Rigi_RESP_Value::encode_command(
    std::initializer_list<std::string> args)
{
    std::string s = "*" + std::to_string(args.size()) + "\r\n";
    for (const auto& a : args)
        s += "$" + std::to_string(a.size()) + "\r\n" + a + "\r\n";
    return std::vector<char>(s.begin(), s.end());
}

inline std::vector<char> Rigi_RESP_Value::encode_simple(const std::string& s)
{
    std::string r = "+" + s + "\r\n";
    return std::vector<char>(r.begin(), r.end());
}

inline std::vector<char> Rigi_RESP_Value::encode_error(const std::string& s)
{
    std::string r = "-" + s + "\r\n";
    return std::vector<char>(r.begin(), r.end());
}

inline std::vector<char> Rigi_RESP_Value::encode_integer(int64_t n)
{
    std::string r = ":" + std::to_string(n) + "\r\n";
    return std::vector<char>(r.begin(), r.end());
}

inline std::vector<char> Rigi_RESP_Value::encode_bulk(const std::string& s)
{
    std::string r = "$" + std::to_string(s.size()) + "\r\n" + s + "\r\n";
    return std::vector<char>(r.begin(), r.end());
}

inline std::vector<char> Rigi_RESP_Value::encode_null_bulk()
{
    std::string r = "$-1\r\n";
    return std::vector<char>(r.begin(), r.end());
}

inline std::vector<char> Rigi_RESP_Value::encode_array(
    const std::vector<std::vector<char>>& items)
{
    std::string hdr = "*" + std::to_string(items.size()) + "\r\n";
    std::vector<char> out(hdr.begin(), hdr.end());
    for (const auto& item : items)
        out.insert(out.end(), item.begin(), item.end());
    return out;
}

} // namespace Rigitaeda

#endif
