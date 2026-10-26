#pragma once
// CAN veri alanından sinyal çıkarma/yerleştirme (DBC mantığı).
//
// İki bayt sırası desteklenir:
//   Intel    (little-endian): startBit, sinyalin en düşük anlamlı bitidir.
//   Motorola (big-endian)   : startBit, DBC'deki "testere dişi" numaralandırmasıyla
//                             sinyalin en anlamlı bitidir; bit bayt sınırını
//                             geçince bir sonraki baytın 7. bitine atlanır.
// Fiziksel değer = ham * scale + offset.

#include <array>
#include <cstdint>

namespace cihaz::can {

using Payload = std::array<std::uint8_t, 8>;

enum class ByteOrder { Intel, Motorola };

struct Signal {
    std::uint8_t startBit = 0;
    std::uint8_t length = 8;  // 1..64
    ByteOrder order = ByteOrder::Intel;
    bool isSigned = false;
    double scale = 1.0;
    double offset = 0.0;
};

std::uint64_t extractRaw(const Signal& s, const Payload& data);
void insertRaw(const Signal& s, std::uint64_t raw, Payload& data);

double decode(const Signal& s, const Payload& data);
// Fiziksel değeri en yakın ham değere yuvarlar ve sinyal aralığına kırpar.
void encode(const Signal& s, double physical, Payload& data);

}  // namespace cihaz::can
