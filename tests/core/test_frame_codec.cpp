#include <gtest/gtest.h>

#include <random>

#include "cihaz/frame_codec.hpp"

using namespace cihaz;

namespace {

struct Collector {
    std::vector<Frame> frames;
    FrameParser parser{[this](const Frame& f) { frames.push_back(f); }};
    void feed(const Bytes& b) { parser.feed(b.data(), b.size()); }
};

Bytes payloadOf(std::initializer_list<int> v) {
    Bytes b;
    for (int x : v) b.push_back(static_cast<std::uint8_t>(x));
    return b;
}

void append(Bytes& dst, const Bytes& src) { dst.insert(dst.end(), src.begin(), src.end()); }

}  // namespace

TEST(FrameCodec, KodlaCozGidisDonus) {
    Collector c;
    c.feed(encodeFrame(0x01, 7, payloadOf({1, 2, 3, 4})));
    ASSERT_EQ(c.frames.size(), 1U);
    EXPECT_EQ(c.frames[0].msgId, 0x01);
    EXPECT_EQ(c.frames[0].seq, 7);
    EXPECT_EQ(c.frames[0].payload, payloadOf({1, 2, 3, 4}));
    EXPECT_EQ(c.parser.stats().frames, 1U);
    EXPECT_EQ(c.parser.buffered(), 0U);
}

TEST(FrameCodec, BilinenCerceveBaytlari) {
    // CRC-16/CCITT-FALSE(02 10 00 41 42) elle hesaplanıp sabitlendi
    const Bytes f = encodeFrame(0x10, 0, payloadOf({'A', 'B'}));
    ASSERT_EQ(f.size(), 9U);
    EXPECT_EQ(f[0], 0xA5);
    EXPECT_EQ(f[1], 0x5A);
    EXPECT_EQ(f[2], 2);
    EXPECT_EQ(f[3], 0x10);
    EXPECT_EQ(f[4], 0);
    EXPECT_EQ(f[5], 'A');
    EXPECT_EQ(f[6], 'B');
}

TEST(FrameCodec, BaytBaytBeslemeAyniSonucuVerir) {
    Bytes stream;
    for (int i = 0; i < 20; ++i) append(stream, encodeFrame(0x01, static_cast<std::uint8_t>(i), payloadOf({i, i + 1})));
    Collector c;
    for (auto b : stream) c.parser.feed(&b, 1);
    ASSERT_EQ(c.frames.size(), 20U);
    EXPECT_EQ(c.frames[19].seq, 19);
    EXPECT_EQ(c.parser.stats().seqGaps, 0U);
}

TEST(FrameCodec, OndekiGurultuAtlanirVeSayilir) {
    Bytes stream = payloadOf({0x00, 0xFF, 0xA5, 0x13, 0x5A});
    append(stream, encodeFrame(0x02, 1, payloadOf({9})));
    Collector c;
    c.feed(stream);
    ASSERT_EQ(c.frames.size(), 1U);
    EXPECT_EQ(c.parser.stats().droppedBytes, 5U);
}

TEST(FrameCodec, CrcHatasindanSonrakiCerceveKaybolmaz) {
    Bytes bad = encodeFrame(0x01, 1, payloadOf({1, 2, 3}));
    bad[6] ^= 0x40;  // yükü boz
    Bytes stream = bad;
    append(stream, encodeFrame(0x01, 2, payloadOf({4, 5, 6})));
    Collector c;
    c.feed(stream);
    ASSERT_EQ(c.frames.size(), 1U);
    EXPECT_EQ(c.frames[0].seq, 2);
    EXPECT_EQ(c.parser.stats().crcErrors, 1U);
}

TEST(FrameCodec, BozukCerceveninIcindekiGecerliCerceveBulunur) {
    // Bozuk bir başlığın "uzunluk" alanı sonraki gerçek çerçeveyi yutmaya çalışır;
    // ayrıştırıcı tek bayt kaydırıp yeniden aramalı.
    Bytes stream = payloadOf({0xA5, 0x5A, 30, 0x01, 0x00});
    append(stream, encodeFrame(0x02, 5, payloadOf({1, 2, 3, 4, 5, 6, 7, 8})));
    append(stream, Bytes(40, 0x00));  // sahte çerçevenin CRC'sini yakalayacak kadar dolgu
    Collector c;
    c.feed(stream);
    ASSERT_EQ(c.frames.size(), 1U);
    EXPECT_EQ(c.frames[0].seq, 5);
    EXPECT_GE(c.parser.stats().crcErrors, 1U);
}

TEST(FrameCodec, AsiriUzunlukReddedilir) {
    Bytes stream = payloadOf({0xA5, 0x5A, static_cast<int>(kMaxPayload + 1), 0x01, 0x00});
    append(stream, encodeFrame(0x01, 0, payloadOf({1})));
    Collector c;
    c.feed(stream);
    EXPECT_EQ(c.frames.size(), 1U);
    EXPECT_EQ(c.parser.stats().lengthErrors, 1U);
}

TEST(FrameCodec, SiraAtlamasiKayipCerceveOlarakSayilir) {
    Collector c;
    c.feed(encodeFrame(0x01, 250, {}));
    c.feed(encodeFrame(0x01, 251, {}));
    c.feed(encodeFrame(0x01, 255, {}));  // 252..254 kayıp
    c.feed(encodeFrame(0x01, 0, {}));    // 255 -> 0 sarması kayıp değil
    EXPECT_EQ(c.parser.stats().seqGaps, 3U);
}

TEST(FrameCodec, YarimCerceveBeklenir) {
    const Bytes f = encodeFrame(0x01, 0, payloadOf({1, 2, 3}));
    Collector c;
    c.parser.feed(f.data(), f.size() - 1);
    EXPECT_TRUE(c.frames.empty());
    EXPECT_EQ(c.parser.buffered(), f.size() - 1);
    c.parser.feed(f.data() + f.size() - 1, 1);
    EXPECT_EQ(c.frames.size(), 1U);
}

TEST(FrameCodec, CokUzunYukKodlanamaz) {
    EXPECT_THROW(encodeFrame(0x01, 0, Bytes(kMaxPayload + 1, 0)), std::invalid_argument);
}

// Rastgele tek bayt bozulmaları: yalnızca bozulan çerçeveler kaybolmalı,
// komşuları değil. Tohum sabit, sonuç tekrarlanabilir.
TEST(FrameCodec, RastgeleBozulmadaKomsuCerceverKorunur) {
    std::mt19937 rng(1234);
    std::uniform_int_distribution<int> byteDist(0, 255);
    std::uniform_int_distribution<int> lenDist(0, 32);
    const int n = 2000;
    int corrupted = 0;
    Bytes stream;
    for (int i = 0; i < n; ++i) {
        Bytes payload(static_cast<std::size_t>(lenDist(rng)));
        for (auto& b : payload) b = static_cast<std::uint8_t>(byteDist(rng));
        Bytes f = encodeFrame(0x01, static_cast<std::uint8_t>(i), payload);
        if (i % 17 == 3) {
            std::uniform_int_distribution<std::size_t> pos(2, f.size() - 1);
            f[pos(rng)] ^= static_cast<std::uint8_t>(1 + byteDist(rng) % 255);
            ++corrupted;
        }
        append(stream, f);
    }
    append(stream, Bytes(kMaxPayload + kFrameOverhead, 0));  // sonda bekleyen sahte çerçeveyi boşalt

    Collector c;
    std::uniform_int_distribution<std::size_t> chunk(1, 64);
    for (std::size_t i = 0; i < stream.size();) {
        const std::size_t k = std::min(chunk(rng), stream.size() - i);
        c.parser.feed(stream.data() + i, k);
        i += k;
    }
    EXPECT_EQ(c.frames.size(), static_cast<std::size_t>(n - corrupted));
    EXPECT_EQ(c.parser.stats().seqGaps, static_cast<std::uint64_t>(corrupted));
}
