#include <gtest/gtest.h>

#include <cstring>

#include "cihaz/crc.hpp"

using namespace cihaz;

namespace {
const char* kCheck = "123456789";
const auto* kCheckBytes = reinterpret_cast<const std::uint8_t*>(kCheck);
}  // namespace

TEST(Crc, ModbusKontrolDegeri) { EXPECT_EQ(crc16Modbus(kCheckBytes, 9), 0x4B37); }

TEST(Crc, CcittFalseKontrolDegeri) { EXPECT_EQ(crc16Ccitt(kCheckBytes, 9), 0x29B1); }

TEST(Crc, ParcaliHesapTekSeferlikleAyni) {
    const std::uint16_t a = crc16Modbus(kCheckBytes, 4);
    EXPECT_EQ(crc16Modbus(kCheckBytes + 4, 5, a), 0x4B37);
    const std::uint16_t b = crc16Ccitt(kCheckBytes, 3);
    EXPECT_EQ(crc16Ccitt(kCheckBytes + 3, 6, b), 0x29B1);
}

TEST(Crc, BosVeriBaslangicDegeriniDondurur) {
    EXPECT_EQ(crc16Modbus(nullptr, 0), 0xFFFF);
    EXPECT_EQ(crc16Ccitt(nullptr, 0), 0xFFFF);
}
