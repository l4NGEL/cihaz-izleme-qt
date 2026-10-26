#include "cihaz/frame_codec.hpp"

#include <stdexcept>

#include "cihaz/crc.hpp"

namespace cihaz {

Bytes encodeFrame(std::uint8_t msgId, std::uint8_t seq, const std::uint8_t* payload,
                  std::size_t len) {
    if (len > kMaxPayload) throw std::invalid_argument("telemetri yükü çok uzun");
    Bytes out;
    out.reserve(len + kFrameOverhead);
    out.push_back(kSof0);
    out.push_back(kSof1);
    out.push_back(static_cast<std::uint8_t>(len));
    out.push_back(msgId);
    out.push_back(seq);
    out.insert(out.end(), payload, payload + len);
    putU16le(out, crc16Ccitt(out.data() + 2, len + 3));
    return out;
}

void FrameParser::feed(const std::uint8_t* data, std::size_t len) {
    buf_.insert(buf_.end(), data, data + len);
    process();
    // Tüketilen baştaki baytları ara sıra sil; her çerçevede erase O(n^2) olurdu.
    if (head_ > 4096 || head_ == buf_.size()) {
        buf_.erase(buf_.begin(), buf_.begin() + static_cast<std::ptrdiff_t>(head_));
        head_ = 0;
    }
}

void FrameParser::reset() {
    buf_.clear();
    head_ = 0;
    haveSeq_ = false;
}

void FrameParser::skip(std::size_t n, bool countAsDropped) {
    head_ += n;
    if (countAsDropped) stats_.droppedBytes += n;
}

void FrameParser::process() {
    for (;;) {
        // Başlangıç işaretini ara
        std::size_t i = head_;
        const std::size_t end = buf_.size();
        while (i + 1 < end && !(buf_[i] == kSof0 && buf_[i + 1] == kSof1)) ++i;
        if (i > head_) skip(i - head_, true);
        if (buffered() < kFrameHeader) return;

        const std::uint8_t* p = buf_.data() + head_;
        const std::size_t len = p[2];
        if (len > kMaxPayload) {
            ++stats_.lengthErrors;
            skip(1, true);
            continue;
        }
        const std::size_t total = len + kFrameOverhead;
        if (buffered() < total) return;

        const std::uint16_t want = readU16le(p + kFrameHeader + len);
        if (crc16Ccitt(p + 2, len + 3) != want) {
            ++stats_.crcErrors;
            skip(1, true);
            continue;
        }

        Frame f;
        f.msgId = p[3];
        f.seq = p[4];
        f.payload.assign(p + kFrameHeader, p + kFrameHeader + len);
        if (haveSeq_) {
            const auto gap = static_cast<std::uint8_t>(f.seq - lastSeq_ - 1U);
            stats_.seqGaps += gap;
        }
        haveSeq_ = true;
        lastSeq_ = f.seq;
        ++stats_.frames;
        skip(total, false);
        onFrame_(f);
    }
}

}  // namespace cihaz
