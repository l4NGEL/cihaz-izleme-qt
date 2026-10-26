#include <gtest/gtest.h>

#include <random>

#include "cihaz/can_signal.hpp"

using namespace cihaz::can;

TEST(CanSignal, IntelIsaretliDeger) {
    const Signal s{0, 16, ByteOrder::Intel, true, 1.0, 0.0};
    Payload d{};
    d[0] = 0x18;  // -1000 = 0xFC18
    d[1] = 0xFC;
    EXPECT_DOUBLE_EQ(decode(s, d), -1000.0);
}

TEST(CanSignal, IntelBaytSiniriniGecenSinyal) {
    const Signal s{4, 12, ByteOrder::Intel, false, 1.0, 0.0};
    Payload d{};
    encode(s, 0xABC, d);
    EXPECT_EQ(d[0], 0xC0);
    EXPECT_EQ(d[1], 0xAB);
    EXPECT_EQ(extractRaw(s, d), 0xABCU);
}

TEST(CanSignal, MotorolaBilinenYerlesim) {
    // DBC: start 23, uzunluk 16, büyük uçlu -> bayt 2 yüksek, bayt 3 düşük
    const Signal s{23, 16, ByteOrder::Motorola, false, 0.01, 0.0};
    Payload d{};
    encode(s, 123.45, d);
    EXPECT_EQ(d[2], 0x30);
    EXPECT_EQ(d[3], 0x39);
    EXPECT_NEAR(decode(s, d), 123.45, 1e-9);
}

TEST(CanSignal, MotorolaBaytIcindeKisaSinyal) {
    // start 5, uzunluk 3 -> bayt 0'ın 5..3 bitleri
    const Signal s{5, 3, ByteOrder::Motorola, false, 1.0, 0.0};
    Payload d{};
    insertRaw(s, 0b101, d);
    EXPECT_EQ(d[0], 0b00101000);
    EXPECT_EQ(extractRaw(s, d), 0b101U);
}

TEST(CanSignal, OlcekOfsetVeKirpma) {
    const Signal s{48, 8, ByteOrder::Intel, false, 1.0, -40.0};
    Payload d{};
    encode(s, 25.0, d);
    EXPECT_EQ(d[6], 65);
    encode(s, 1000.0, d);  // aralık dışı -> 255 ham
    EXPECT_DOUBLE_EQ(decode(s, d), 215.0);
    encode(s, -100.0, d);  // -> 0 ham
    EXPECT_DOUBLE_EQ(decode(s, d), -40.0);
}

TEST(CanSignal, KomsuSinyallerBirbirineDokunmaz) {
    const Signal a{0, 16, ByteOrder::Intel, true, 1.0, 0.0};
    const Signal b{16, 16, ByteOrder::Intel, true, 0.01, 0.0};
    Payload d{};
    d.fill(0xFF);
    encode(a, 1234, d);
    encode(b, -5.5, d);
    EXPECT_DOUBLE_EQ(decode(a, d), 1234.0);
    EXPECT_NEAR(decode(b, d), -5.5, 1e-9);
    for (std::size_t i = 4; i < 8; ++i) EXPECT_EQ(d[i], 0xFF);
}

TEST(CanSignal, GecersizTanimReddedilir) {
    Payload d{};
    EXPECT_THROW(extractRaw(Signal{60, 8, ByteOrder::Intel}, d), std::invalid_argument);
    EXPECT_THROW(extractRaw(Signal{0, 0, ByteOrder::Intel}, d), std::invalid_argument);
    EXPECT_THROW(extractRaw(Signal{56, 16, ByteOrder::Motorola}, d), std::invalid_argument);
}

// Her iki bayt sırası için rastgele sinyal tanımlarında yaz-oku gidiş dönüşü
TEST(CanSignal, RastgeleGidisDonus) {
    std::mt19937 rng(42);
    for (int iter = 0; iter < 2000; ++iter) {
        Signal s;
        s.order = (iter % 2) ? ByteOrder::Motorola : ByteOrder::Intel;
        s.length = static_cast<std::uint8_t>(1 + rng() % 32);
        s.isSigned = rng() % 2;
        if (s.order == ByteOrder::Intel) {
            s.startBit = static_cast<std::uint8_t>(rng() % (65 - s.length));
        } else {
            // Motorola: MSB konumu, sinyalin son baytı 7'yi aşmayacak şekilde
            const unsigned msbByte = static_cast<unsigned>(rng() % (8 - (s.length + 7) / 8 + 1));
            s.startBit = static_cast<std::uint8_t>(msbByte * 8 + 7);
        }
        const std::uint64_t mask = (std::uint64_t{1} << s.length) - 1;
        const std::uint64_t raw = rng() & mask;
        Payload d{};
        insertRaw(s, raw, d);
        ASSERT_EQ(extractRaw(s, d), raw) << "iter " << iter;
    }
}
