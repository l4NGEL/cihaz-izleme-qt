#pragma once
// UART telemetri çerçevesi ve akış ayrıştırıcısı.
//
//   +------+------+-----+-------+-----+-----------------+-----------+
//   | 0xA5 | 0x5A | LEN | MSGID | SEQ | YÜK (LEN bayt)  | CRC16 (LE)|
//   +------+------+-----+-------+-----+-----------------+-----------+
//   CRC-16/CCITT-FALSE, LEN..YÜK sonu aralığı üzerinden.
//
// Seri hat bayt bayt gelir; bir read() çağrısı yarım çerçeve, birden fazla
// çerçeve ya da hat gürültüsü içerebilir. Ayrıştırıcı bu yüzden durum tutar:
// başlangıç işaretini arar, uzunluk ve CRC tutmazsa yalnızca bir bayt atlayıp
// yeniden arar. Böylece bozuk bir çerçevenin içine gömülü geçerli bir çerçeve
// kaybolmaz.

#include <cstddef>
#include <cstdint>
#include <functional>

#include "cihaz/bytes.hpp"

namespace cihaz {

constexpr std::uint8_t kSof0 = 0xA5;
constexpr std::uint8_t kSof1 = 0x5A;
constexpr std::size_t kFrameHeader = 5;    // SOF0 SOF1 LEN MSGID SEQ
constexpr std::size_t kFrameOverhead = 7;  // başlık + CRC
constexpr std::size_t kMaxPayload = 64;

struct Frame {
    std::uint8_t msgId = 0;
    std::uint8_t seq = 0;
    Bytes payload;
};

Bytes encodeFrame(std::uint8_t msgId, std::uint8_t seq, const std::uint8_t* payload,
                  std::size_t len);
inline Bytes encodeFrame(std::uint8_t msgId, std::uint8_t seq, const Bytes& payload) {
    return encodeFrame(msgId, seq, payload.data(), payload.size());
}

struct ParserStats {
    std::uint64_t frames = 0;         // CRC'si tutan çerçeveler
    std::uint64_t crcErrors = 0;
    std::uint64_t lengthErrors = 0;   // LEN > kMaxPayload
    std::uint64_t droppedBytes = 0;   // senkron ararken atlanan baytlar
    std::uint64_t seqGaps = 0;        // SEQ sıçramalarından kestirilen kayıp çerçeve
};

class FrameParser {
public:
    using Handler = std::function<void(const Frame&)>;

    explicit FrameParser(Handler onFrame) : onFrame_(std::move(onFrame)) {}

    void feed(const std::uint8_t* data, std::size_t len);
    void reset();

    const ParserStats& stats() const { return stats_; }
    std::size_t buffered() const { return buf_.size() - head_; }

private:
    void process();
    void skip(std::size_t n, bool countAsDropped);

    Handler onFrame_;
    Bytes buf_;
    std::size_t head_ = 0;
    ParserStats stats_;
    bool haveSeq_ = false;
    std::uint8_t lastSeq_ = 0;
};

}  // namespace cihaz
