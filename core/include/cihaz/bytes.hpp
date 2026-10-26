#pragma once
// Bayt dizisine küçük/büyük uçlu (endian) okuma-yazma yardımcıları.
// Tüm protokol kodu bunları kullanır; makinenin kendi bayt sırasına güvenmez.

#include <cstdint>
#include <cstring>
#include <vector>

namespace cihaz {

using Bytes = std::vector<std::uint8_t>;

inline std::uint16_t readU16le(const std::uint8_t* p) {
    return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
}
inline std::uint16_t readU16be(const std::uint8_t* p) {
    return static_cast<std::uint16_t>((p[0] << 8) | p[1]);
}
inline std::uint32_t readU32le(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}
inline std::uint64_t readU64le(const std::uint8_t* p) {
    return static_cast<std::uint64_t>(readU32le(p)) |
           (static_cast<std::uint64_t>(readU32le(p + 4)) << 32);
}
inline float readF32le(const std::uint8_t* p) {
    const std::uint32_t u = readU32le(p);
    float f;
    std::memcpy(&f, &u, sizeof f);
    return f;
}

inline void putU16le(Bytes& out, std::uint16_t v) {
    out.push_back(static_cast<std::uint8_t>(v & 0xFFU));
    out.push_back(static_cast<std::uint8_t>(v >> 8));
}
inline void putU16be(Bytes& out, std::uint16_t v) {
    out.push_back(static_cast<std::uint8_t>(v >> 8));
    out.push_back(static_cast<std::uint8_t>(v & 0xFFU));
}
inline void putU32le(Bytes& out, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFFU));
}
inline void putU64le(Bytes& out, std::uint64_t v) {
    putU32le(out, static_cast<std::uint32_t>(v & 0xFFFFFFFFULL));
    putU32le(out, static_cast<std::uint32_t>(v >> 32));
}
inline void putF32le(Bytes& out, float f) {
    std::uint32_t u;
    std::memcpy(&u, &f, sizeof u);
    putU32le(out, u);
}

}  // namespace cihaz
