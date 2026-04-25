#ifndef RIGI_CODEC_MQTT_H_
#define RIGI_CODEC_MQTT_H_

#include "Rigi_Codec.hpp"
#include <string>
#include <cstdint>
#include <stdexcept>

namespace Rigitaeda {

// MQTT 3.1.1 코덱 — 완성된 패킷 단위로 emit
class Rigi_Codec_MQTT : public Rigi_Codec {
public:
    void decode(const char* data, size_t len,
                std::function<void(const char*, size_t)> emit) override
    {
        m_buf.insert(m_buf.end(), data, data + len);
        while (m_buf.size() >= 2) {
            // Remaining Length 디코딩 (최대 4바이트 가변 인코딩)
            uint32_t remaining  = 0;
            uint32_t multiplier = 1;
            size_t   pos        = 1;
            bool     complete   = false;
            while (pos < m_buf.size() && pos <= 4) {
                uint8_t b = static_cast<uint8_t>(m_buf[pos++]);
                remaining += (b & 0x7F) * multiplier;
                multiplier *= 128;
                if ((b & 0x80) == 0) { complete = true; break; }
            }
            if (!complete) break;

            size_t total = pos + remaining;
            if (m_buf.size() < total) break;

            emit(m_buf.data(), total);
            m_buf.erase(m_buf.begin(),
                        m_buf.begin() + static_cast<ptrdiff_t>(total));
        }
    }

    std::vector<char> encode(const char* data, size_t len) override
    {
        return std::vector<char>(data, data + len);
    }

private:
    std::vector<char> m_buf;
};

// ── MQTT 3.1.1 패킷 타입 및 빌더 ─────────────────────────────────────────────

namespace Rigi_MQTT {

enum PacketType : uint8_t {
    CONNECT     = 1,  CONNACK    = 2,
    PUBLISH     = 3,  PUBACK     = 4,
    PUBREC      = 5,  PUBREL     = 6,  PUBCOMP    = 7,
    SUBSCRIBE   = 8,  SUBACK     = 9,
    UNSUBSCRIBE = 10, UNSUBACK   = 11,
    PINGREQ     = 12, PINGRESP   = 13,
    DISCONNECT  = 14
};

enum ConnectReturnCode : uint8_t {
    ACCEPTED                   = 0x00,
    UNACCEPTABLE_PROTOCOL      = 0x01,
    IDENTIFIER_REJECTED        = 0x02,
    SERVER_UNAVAILABLE         = 0x03,
    BAD_CREDENTIALS            = 0x04,
    NOT_AUTHORIZED             = 0x05
};

// 코덱이 emit한 raw 바이트를 파싱한 MQTT 패킷
struct Packet {
    uint8_t              type_flags = 0;
    std::vector<uint8_t> payload;   // variable header + payload 합산

    uint8_t type()   const { return (type_flags >> 4) & 0x0F; }
    uint8_t flags()  const { return  type_flags & 0x0F; }
    bool    dup()    const { return (type_flags >> 3) & 0x01; }
    uint8_t qos()    const { return (type_flags >> 1) & 0x03; }
    bool    retain() const { return  type_flags & 0x01; }

    static Packet parse(const char* data, size_t len);

    // PUBLISH 전용 — topic, message, packet_id 추출
    std::string  topic()     const;
    std::string  message()   const;
    uint16_t     packet_id() const;

    // CONNACK 전용
    bool     session_present() const { return payload.size() >= 1 && (payload[0] & 0x01); }
    uint8_t  return_code()     const { return payload.size() >= 2 ? payload[1] : 0xFF; }
};

// ── 내부 인코딩 헬퍼 ─────────────────────────────────────────────────────────

namespace detail {
    inline void encode_remaining(std::vector<uint8_t>& out, uint32_t len)
    {
        do {
            uint8_t b = len % 128;
            len /= 128;
            if (len > 0) b |= 0x80;
            out.push_back(b);
        } while (len > 0);
    }

    inline void write_u16(std::vector<uint8_t>& out, uint16_t v)
    {
        out.push_back(static_cast<uint8_t>(v >> 8));
        out.push_back(static_cast<uint8_t>(v & 0xFF));
    }

    inline void write_str(std::vector<uint8_t>& out, const std::string& s)
    {
        write_u16(out, static_cast<uint16_t>(s.size()));
        out.insert(out.end(), s.begin(), s.end());
    }

    inline std::vector<char> make_packet(uint8_t type_flags,
                                          const std::vector<uint8_t>& payload)
    {
        std::vector<uint8_t> out;
        out.push_back(type_flags);
        encode_remaining(out, static_cast<uint32_t>(payload.size()));
        out.insert(out.end(), payload.begin(), payload.end());
        return std::vector<char>(out.begin(), out.end());
    }
} // namespace detail

// ── 패킷 빌더 ────────────────────────────────────────────────────────────────

inline std::vector<char> build_connect(
    const std::string& client_id,
    const std::string& username      = "",
    const std::string& password      = "",
    uint16_t           keepalive     = 60,
    bool               clean_session = true)
{
    std::vector<uint8_t> p;
    detail::write_str(p, "MQTT");
    p.push_back(0x04);  // Protocol Level 3.1.1
    uint8_t flags = clean_session ? 0x02 : 0x00;
    if (!username.empty()) flags |= 0x80;
    if (!password.empty()) flags |= 0x40;
    p.push_back(flags);
    detail::write_u16(p, keepalive);
    detail::write_str(p, client_id);
    if (!username.empty()) detail::write_str(p, username);
    if (!password.empty()) detail::write_str(p, password);
    return detail::make_packet(static_cast<uint8_t>(CONNECT) << 4, p);
}

inline std::vector<char> build_connack(uint8_t return_code,
                                        bool session_present = false)
{
    std::vector<uint8_t> p = {
        static_cast<uint8_t>(session_present ? 0x01 : 0x00),
        return_code
    };
    return detail::make_packet(static_cast<uint8_t>(CONNACK) << 4, p);
}

inline std::vector<char> build_publish(
    const std::string& topic,
    const std::string& message,
    uint8_t  qos       = 0,
    bool     retain    = false,
    bool     dup       = false,
    uint16_t packet_id = 0)
{
    uint8_t tf = static_cast<uint8_t>(PUBLISH) << 4;
    if (dup)    tf |= 0x08;
    tf |= static_cast<uint8_t>((qos & 0x03) << 1);
    if (retain) tf |= 0x01;

    std::vector<uint8_t> p;
    detail::write_str(p, topic);
    if (qos > 0) detail::write_u16(p, packet_id);
    p.insert(p.end(), message.begin(), message.end());
    return detail::make_packet(tf, p);
}

inline std::vector<char> build_subscribe(uint16_t packet_id,
                                          const std::string& topic,
                                          uint8_t qos = 0)
{
    std::vector<uint8_t> p;
    detail::write_u16(p, packet_id);
    detail::write_str(p, topic);
    p.push_back(qos & 0x03);
    return detail::make_packet((static_cast<uint8_t>(SUBSCRIBE) << 4) | 0x02, p);
}

inline std::vector<char> build_suback(uint16_t packet_id, uint8_t return_code)
{
    std::vector<uint8_t> p;
    detail::write_u16(p, packet_id);
    p.push_back(return_code);
    return detail::make_packet(static_cast<uint8_t>(SUBACK) << 4, p);
}

inline std::vector<char> build_puback(uint16_t packet_id)
{
    std::vector<uint8_t> p;
    detail::write_u16(p, packet_id);
    return detail::make_packet(static_cast<uint8_t>(PUBACK) << 4, p);
}

inline std::vector<char> build_unsubscribe(uint16_t packet_id,
                                             const std::string& topic)
{
    std::vector<uint8_t> p;
    detail::write_u16(p, packet_id);
    detail::write_str(p, topic);
    return detail::make_packet((static_cast<uint8_t>(UNSUBSCRIBE) << 4) | 0x02, p);
}

inline std::vector<char> build_pingreq()
{
    return detail::make_packet(static_cast<uint8_t>(PINGREQ) << 4, {});
}

inline std::vector<char> build_pingresp()
{
    return detail::make_packet(static_cast<uint8_t>(PINGRESP) << 4, {});
}

inline std::vector<char> build_disconnect()
{
    return detail::make_packet(static_cast<uint8_t>(DISCONNECT) << 4, {});
}

// ── Packet 멤버 구현 ─────────────────────────────────────────────────────────

inline Packet Packet::parse(const char* data, size_t len)
{
    Packet pkt;
    if (len < 2) throw std::runtime_error("MQTT: packet too short");
    pkt.type_flags = static_cast<uint8_t>(data[0]);

    uint32_t remaining  = 0;
    uint32_t multiplier = 1;
    size_t   pos        = 1;
    while (pos < len && pos <= 4) {
        uint8_t b = static_cast<uint8_t>(data[pos++]);
        remaining += (b & 0x7F) * multiplier;
        multiplier *= 128;
        if ((b & 0x80) == 0) break;
    }
    const auto* u = reinterpret_cast<const uint8_t*>(data);
    pkt.payload.assign(u + pos, u + pos + remaining);
    return pkt;
}

inline std::string Packet::topic() const
{
    if (payload.size() < 2) return "";
    uint16_t tlen = (static_cast<uint16_t>(payload[0]) << 8) | payload[1];
    if (payload.size() < 2u + tlen) return "";
    return std::string(reinterpret_cast<const char*>(payload.data()) + 2, tlen);
}

inline std::string Packet::message() const
{
    if (payload.size() < 2) return "";
    uint16_t tlen = (static_cast<uint16_t>(payload[0]) << 8) | payload[1];
    size_t offset = 2u + tlen + (qos() > 0 ? 2u : 0u);
    if (payload.size() <= offset) return "";
    return std::string(reinterpret_cast<const char*>(payload.data()) + offset,
                       payload.size() - offset);
}

inline uint16_t Packet::packet_id() const
{
    if (payload.size() < 2) return 0;
    if (type() == PUBLISH) {
        if (qos() == 0) return 0;
        uint16_t tlen = (static_cast<uint16_t>(payload[0]) << 8) | payload[1];
        if (payload.size() < 2u + tlen + 2) return 0;
        size_t off = 2u + tlen;
        return (static_cast<uint16_t>(payload[off]) << 8) | payload[off + 1];
    }
    return (static_cast<uint16_t>(payload[0]) << 8) | payload[1];
}

} // namespace Rigi_MQTT
} // namespace Rigitaeda

#endif
