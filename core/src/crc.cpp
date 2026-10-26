#include "cihaz/crc.hpp"

#include <array>

namespace cihaz {
namespace {

constexpr std::array<std::uint16_t, 256> makeModbusTable() {
    std::array<std::uint16_t, 256> t{};
    for (std::uint16_t i = 0; i < 256; ++i) {
        std::uint16_t c = i;
        for (int b = 0; b < 8; ++b)
            c = static_cast<std::uint16_t>((c & 1U) ? (c >> 1) ^ 0xA001U : c >> 1);
        t[i] = c;
    }
    return t;
}

constexpr std::array<std::uint16_t, 256> makeCcittTable() {
    std::array<std::uint16_t, 256> t{};
    for (std::uint16_t i = 0; i < 256; ++i) {
        std::uint16_t c = static_cast<std::uint16_t>(i << 8);
        for (int b = 0; b < 8; ++b)
            c = static_cast<std::uint16_t>((c & 0x8000U) ? (c << 1) ^ 0x1021U : c << 1);
        t[i] = c;
    }
    return t;
}

constexpr auto kModbus = makeModbusTable();
constexpr auto kCcitt = makeCcittTable();

}  // namespace

std::uint16_t crc16Modbus(const std::uint8_t* data, std::size_t len, std::uint16_t crc) noexcept {
    for (std::size_t i = 0; i < len; ++i)
        crc = static_cast<std::uint16_t>((crc >> 8) ^ kModbus[(crc ^ data[i]) & 0xFFU]);
    return crc;
}

std::uint16_t crc16Ccitt(const std::uint8_t* data, std::size_t len, std::uint16_t crc) noexcept {
    for (std::size_t i = 0; i < len; ++i)
        crc = static_cast<std::uint16_t>((crc << 8) ^ kCcitt[((crc >> 8) ^ data[i]) & 0xFFU]);
    return crc;
}

}  // namespace cihaz
