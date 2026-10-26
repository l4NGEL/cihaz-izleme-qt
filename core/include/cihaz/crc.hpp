#pragma once
// Cihaz protokollerinde kullanılan iki CRC-16 türü. İkisi de tabloyla hesaplanır;
// tablolar derleme zamanında üretilir.
//
//   CRC-16/MODBUS       poly 0xA001 (yansıtılmış 0x8005), başlangıç 0xFFFF
//   CRC-16/CCITT-FALSE  poly 0x1021, başlangıç 0xFFFF, yansıtma yok
//
// Kontrol değerleri ("123456789" için): MODBUS 0x4B37, CCITT-FALSE 0x29B1.

#include <cstddef>
#include <cstdint>

namespace cihaz {

std::uint16_t crc16Modbus(const std::uint8_t* data, std::size_t len,
                          std::uint16_t crc = 0xFFFF) noexcept;

std::uint16_t crc16Ccitt(const std::uint8_t* data, std::size_t len,
                         std::uint16_t crc = 0xFFFF) noexcept;

}  // namespace cihaz
