#include "cihaz/can_signal.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace cihaz::can {
namespace {

bool getBit(const Payload& d, unsigned pos) { return (d[pos / 8] >> (pos % 8)) & 1U; }

void setBit(Payload& d, unsigned pos, bool v) {
    const auto mask = static_cast<std::uint8_t>(1U << (pos % 8));
    if (v)
        d[pos / 8] = static_cast<std::uint8_t>(d[pos / 8] | mask);
    else
        d[pos / 8] = static_cast<std::uint8_t>(d[pos / 8] & ~mask);
}

// Motorola sinyalinde bir sonraki (daha az anlamlı) bitin konumu
unsigned nextMotorola(unsigned pos) { return (pos % 8 == 0) ? pos + 15 : pos - 1; }

void check(const Signal& s) {
    if (s.length == 0 || s.length > 64) throw std::invalid_argument("sinyal uzunluğu 1..64 olmalı");
    if (s.order == ByteOrder::Intel && s.startBit + s.length > 64)
        throw std::invalid_argument("Intel sinyali 8 baytı aşıyor");
}

}  // namespace

std::uint64_t extractRaw(const Signal& s, const Payload& data) {
    check(s);
    std::uint64_t raw = 0;
    if (s.order == ByteOrder::Intel) {
        for (unsigned i = 0; i < s.length; ++i)
            if (getBit(data, s.startBit + i)) raw |= (std::uint64_t{1} << i);
    } else {
        unsigned pos = s.startBit;
        for (unsigned i = 0; i < s.length; ++i) {
            if (pos >= 64) throw std::invalid_argument("Motorola sinyali 8 baytı aşıyor");
            raw = (raw << 1) | (getBit(data, pos) ? 1U : 0U);
            pos = nextMotorola(pos);
        }
    }
    return raw;
}

void insertRaw(const Signal& s, std::uint64_t raw, Payload& data) {
    check(s);
    if (s.order == ByteOrder::Intel) {
        for (unsigned i = 0; i < s.length; ++i) setBit(data, s.startBit + i, (raw >> i) & 1U);
    } else {
        unsigned pos = s.startBit;
        for (unsigned i = 0; i < s.length; ++i) {
            if (pos >= 64) throw std::invalid_argument("Motorola sinyali 8 baytı aşıyor");
            setBit(data, pos, (raw >> (s.length - 1U - i)) & 1U);
            pos = nextMotorola(pos);
        }
    }
}

double decode(const Signal& s, const Payload& data) {
    const std::uint64_t raw = extractRaw(s, data);
    double v;
    if (s.isSigned && s.length < 64 && (raw >> (s.length - 1U)) & 1U)
        v = static_cast<double>(static_cast<std::int64_t>(raw) -
                                static_cast<std::int64_t>(std::uint64_t{1} << s.length));
    else if (s.isSigned)
        v = static_cast<double>(static_cast<std::int64_t>(raw));
    else
        v = static_cast<double>(raw);
    return v * s.scale + s.offset;
}

void encode(const Signal& s, double physical, Payload& data) {
    check(s);
    double raw = std::round((physical - s.offset) / s.scale);
    const double bits = static_cast<double>(s.length);
    const double lo = s.isSigned ? -std::pow(2.0, bits - 1) : 0.0;
    const double hi = s.isSigned ? std::pow(2.0, bits - 1) - 1 : std::pow(2.0, bits) - 1;
    raw = std::clamp(raw, lo, hi);
    std::uint64_t u;
    if (raw < 0)
        u = static_cast<std::uint64_t>(static_cast<std::int64_t>(raw));
    else
        u = static_cast<std::uint64_t>(raw);
    if (s.length < 64) u &= (std::uint64_t{1} << s.length) - 1U;
    insertRaw(s, u, data);
}

}  // namespace cihaz::can
