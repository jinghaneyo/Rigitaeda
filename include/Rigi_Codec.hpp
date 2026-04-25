#ifndef RIGI_CODEC_H_
#define RIGI_CODEC_H_

#include <vector>
#include <functional>
#include <algorithm>
#include <cstring>
#include <cstdint>

namespace Rigitaeda
{
    // 코덱 인터페이스 — 전송 계층과 응용 계층 사이의 프로토콜 플러그인
    // decode: TCP 바이트 스트림 → 완성된 메시지 단위
    // encode: 송신할 메시지 → 프레임 바이트 (구분자/길이 헤더 추가)
    class Rigi_Codec
    {
    public:
        virtual ~Rigi_Codec() = default;

        // 수신: 스트림에서 완성된 메시지를 추출할 때마다 emit() 호출
        virtual void decode(const char* data, size_t len,
                            std::function<void(const char*, size_t)> emit) = 0;

        // 송신: 메시지에 프레임 정보를 붙여 반환 (기본: 그대로 통과)
        virtual std::vector<char> encode(const char* data, size_t len)
        {
            return std::vector<char>(data, data + len);
        }
    };

    // -------------------------------------------------------------------------
    // 구분자 기반 프레이밍
    // 예: Rigi_Codec_Delimiter({'\x17', '\x17'})   → loan 스타일
    //     Rigi_Codec_Delimiter({'\r','\n','\r','\n'}) → HTTP 헤더 구분
    // -------------------------------------------------------------------------
    class Rigi_Codec_Delimiter : public Rigi_Codec
    {
    public:
        explicit Rigi_Codec_Delimiter(std::vector<char> delimiter)
            : m_delimiter(std::move(delimiter)) {}

        Rigi_Codec_Delimiter(std::initializer_list<char> delimiter)
            : m_delimiter(delimiter) {}

        void decode(const char* data, size_t len,
                    std::function<void(const char*, size_t)> emit) override
        {
            m_buffer.insert(m_buffer.end(), data, data + len);
            auto it = m_buffer.begin();
            while (true)
            {
                auto pos = std::search(it, m_buffer.end(),
                                       m_delimiter.begin(), m_delimiter.end());
                if (pos == m_buffer.end()) break;
                size_t msg_len = static_cast<size_t>(std::distance(it, pos));
                if (msg_len > 0)
                    emit(&(*it), msg_len);
                it = pos + static_cast<ptrdiff_t>(m_delimiter.size());
            }
            m_buffer.erase(m_buffer.begin(), it);
        }

        std::vector<char> encode(const char* data, size_t len) override
        {
            std::vector<char> result(data, data + len);
            result.insert(result.end(), m_delimiter.begin(), m_delimiter.end());
            return result;
        }

    private:
        std::vector<char> m_delimiter;
        std::vector<char> m_buffer;
    };

    // -------------------------------------------------------------------------
    // 길이 접두사 기반 프레이밍 (빅엔디안)
    // 예: Rigi_Codec_LengthPrefix(4) → 4바이트 헤더 + 본문
    //     head-body-tail 에서 head 가 길이를 포함하는 경우
    // -------------------------------------------------------------------------
    class Rigi_Codec_LengthPrefix : public Rigi_Codec
    {
    public:
        explicit Rigi_Codec_LengthPrefix(uint32_t header_bytes = 4)
            : m_header_bytes(header_bytes) {}

        void decode(const char* data, size_t len,
                    std::function<void(const char*, size_t)> emit) override
        {
            m_buffer.insert(m_buffer.end(), data, data + len);
            while (m_buffer.size() >= m_header_bytes)
            {
                uint32_t msg_len = 0;
                for (uint32_t i = 0; i < m_header_bytes; ++i)
                    msg_len = (msg_len << 8) | static_cast<unsigned char>(m_buffer[i]);

                if (m_buffer.size() < m_header_bytes + msg_len) break;
                emit(m_buffer.data() + m_header_bytes, msg_len);
                m_buffer.erase(m_buffer.begin(),
                               m_buffer.begin() + m_header_bytes + msg_len);
            }
        }

        std::vector<char> encode(const char* data, size_t len) override
        {
            std::vector<char> result(m_header_bytes + len);
            uint32_t n = static_cast<uint32_t>(len);
            for (int i = static_cast<int>(m_header_bytes) - 1; i >= 0; --i)
            {
                result[i] = static_cast<char>(n & 0xFF);
                n >>= 8;
            }
            std::memcpy(result.data() + m_header_bytes, data, len);
            return result;
        }

    private:
        uint32_t         m_header_bytes;
        std::vector<char> m_buffer;
    };

    // -------------------------------------------------------------------------
    // 고정 크기 프레이밍
    // 예: Rigi_Codec_FixedSize(64) → 항상 64바이트씩 처리
    // -------------------------------------------------------------------------
    class Rigi_Codec_FixedSize : public Rigi_Codec
    {
    public:
        explicit Rigi_Codec_FixedSize(size_t msg_size) : m_msg_size(msg_size) {}

        void decode(const char* data, size_t len,
                    std::function<void(const char*, size_t)> emit) override
        {
            m_buffer.insert(m_buffer.end(), data, data + len);
            while (m_buffer.size() >= m_msg_size)
            {
                emit(m_buffer.data(), m_msg_size);
                m_buffer.erase(m_buffer.begin(), m_buffer.begin() + m_msg_size);
            }
        }

    private:
        size_t            m_msg_size;
        std::vector<char> m_buffer;
    };
}

#endif
