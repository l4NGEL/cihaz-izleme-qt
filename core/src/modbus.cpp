#include "cihaz/modbus.hpp"

#include <stdexcept>

#include "cihaz/crc.hpp"

namespace cihaz::modbus {

Request Request::readHolding(std::uint8_t unit, std::uint16_t addr, std::uint16_t count) {
    return {unit, Function::ReadHoldingRegisters, addr, count, {}};
}
Request Request::readInput(std::uint8_t unit, std::uint16_t addr, std::uint16_t count) {
    return {unit, Function::ReadInputRegisters, addr, count, {}};
}
Request Request::writeSingle(std::uint8_t unit, std::uint16_t addr, std::uint16_t value) {
    return {unit, Function::WriteSingleRegister, addr, 1, {value}};
}
Request Request::writeMultiple(std::uint8_t unit, std::uint16_t addr,
                               std::vector<std::uint16_t> values) {
    const auto n = static_cast<std::uint16_t>(values.size());
    return {unit, Function::WriteMultipleRegisters, addr, n, std::move(values)};
}

Bytes encodePdu(const Request& req) {
    Bytes pdu;
    pdu.push_back(static_cast<std::uint8_t>(req.function));
    putU16be(pdu, req.address);
    switch (req.function) {
    case Function::ReadHoldingRegisters:
    case Function::ReadInputRegisters:
        if (req.count == 0 || req.count > kMaxReadCount)
            throw std::invalid_argument("Modbus okuma sayısı 1..125 olmalı");
        putU16be(pdu, req.count);
        break;
    case Function::WriteSingleRegister:
        if (req.values.size() != 1)
            throw std::invalid_argument("tek register yazımı bir değer ister");
        putU16be(pdu, req.values[0]);
        break;
    case Function::WriteMultipleRegisters: {
        if (req.values.empty() || req.values.size() > kMaxWriteCount)
            throw std::invalid_argument("Modbus yazma sayısı 1..123 olmalı");
        const auto n = static_cast<std::uint16_t>(req.values.size());
        putU16be(pdu, n);
        pdu.push_back(static_cast<std::uint8_t>(n * 2));
        for (auto v : req.values) putU16be(pdu, v);
        break;
    }
    }
    return pdu;
}

Bytes encodeRtu(const Request& req) {
    Bytes out;
    out.push_back(req.unit);
    const Bytes pdu = encodePdu(req);
    out.insert(out.end(), pdu.begin(), pdu.end());
    putU16le(out, crc16Modbus(out.data(), out.size()));  // Modbus CRC'si küçük uçlu gider
    return out;
}

Bytes encodeTcp(const Request& req, std::uint16_t transactionId) {
    const Bytes pdu = encodePdu(req);
    Bytes out;
    putU16be(out, transactionId);
    putU16be(out, 0);  // protokol kimliği
    putU16be(out, static_cast<std::uint16_t>(pdu.size() + 1));
    out.push_back(req.unit);
    out.insert(out.end(), pdu.begin(), pdu.end());
    return out;
}

namespace {

ParseResult invalid(std::size_t consumed, std::string why) {
    ParseResult r;
    r.status = ParseStatus::Invalid;
    r.consumed = consumed;
    r.error = std::move(why);
    return r;
}

// PDU (fonksiyon kodundan itibaren) uzunluğu; henüz belirlenemiyorsa 0.
// Uyuşmazlıkta error doldurulur.
std::size_t expectedPduLength(const Request& req, const std::uint8_t* pdu, std::size_t len,
                              std::string& error) {
    if (len < 1) return 0;
    const std::uint8_t fc = pdu[0];
    const auto want = static_cast<std::uint8_t>(req.function);
    if (fc == (want | 0x80U)) return 2;
    if (fc != want) {
        error = "beklenmeyen fonksiyon kodu " + std::to_string(fc);
        return 0;
    }
    switch (req.function) {
    case Function::ReadHoldingRegisters:
    case Function::ReadInputRegisters:
        if (len < 2) return 0;
        if (pdu[1] != req.count * 2) {
            error = "bayt sayısı " + std::to_string(pdu[1]) + ", beklenen " +
                    std::to_string(req.count * 2);
            return 0;
        }
        return 2U + pdu[1];
    case Function::WriteSingleRegister:
    case Function::WriteMultipleRegisters:
        return 5;
    }
    return 0;
}

// Uzunluğu ve bütünlüğü doğrulanmış PDU'yu yanıta çevirir.
ParseResult decodePdu(const Request& req, const std::uint8_t* pdu, std::size_t consumed) {
    ParseResult r;
    r.consumed = consumed;
    if (pdu[0] & 0x80U) {
        r.status = ParseStatus::Complete;
        r.response.exception = static_cast<Exception>(pdu[1]);
        return r;
    }
    switch (req.function) {
    case Function::ReadHoldingRegisters:
    case Function::ReadInputRegisters:
        for (std::size_t i = 0; i < req.count; ++i)
            r.response.registers.push_back(readU16be(pdu + 2 + 2 * i));
        break;
    case Function::WriteSingleRegister:
        if (readU16be(pdu + 1) != req.address || readU16be(pdu + 3) != req.values[0])
            return invalid(consumed, "yazma yankısı istekle uyuşmuyor");
        break;
    case Function::WriteMultipleRegisters:
        if (readU16be(pdu + 1) != req.address || readU16be(pdu + 3) != req.values.size())
            return invalid(consumed, "çoklu yazma yankısı istekle uyuşmuyor");
        break;
    }
    r.status = ParseStatus::Complete;
    return r;
}

}  // namespace

ParseResult parseRtuResponse(const Request& req, const std::uint8_t* data, std::size_t len) {
    if (len < 1) return {};
    if (data[0] != req.unit)
        return invalid(len, "beklenmeyen istasyon adresi " + std::to_string(data[0]));
    std::string error;
    const std::size_t pduLen = expectedPduLength(req, data + 1, len - 1, error);
    if (!error.empty()) return invalid(len, error);
    if (pduLen == 0) return {};
    const std::size_t total = 1 + pduLen + 2;
    if (len < total) return {};
    if (crc16Modbus(data, total - 2) != readU16le(data + total - 2))
        return invalid(total, "CRC hatası");
    return decodePdu(req, data + 1, total);
}

ParseResult parseTcpResponse(const Request& req, std::uint16_t transactionId,
                             const std::uint8_t* data, std::size_t len) {
    if (len < 7) return {};
    const std::uint16_t tid = readU16be(data);
    const std::uint16_t proto = readU16be(data + 2);
    const std::uint16_t mbapLen = readU16be(data + 4);
    if (proto != 0) return invalid(len, "protokol kimliği 0 değil");
    if (mbapLen < 3 || mbapLen > 254) return invalid(len, "geçersiz MBAP uzunluğu");
    const std::size_t total = 6U + mbapLen;
    if (len < total) return {};
    if (tid != transactionId)
        return invalid(total, "işlem kimliği uyuşmuyor (" + std::to_string(tid) + ")");
    if (data[6] != req.unit) return invalid(total, "beklenmeyen birim kimliği");
    std::string error;
    const std::size_t pduLen = expectedPduLength(req, data + 7, mbapLen - 1U, error);
    if (!error.empty()) return invalid(total, error);
    if (pduLen + 1 != mbapLen) return invalid(total, "MBAP uzunluğu PDU ile uyuşmuyor");
    return decodePdu(req, data + 7, total);
}

const char* exceptionName(Exception e) {
    switch (e) {
    case Exception::None: return "yok";
    case Exception::IllegalFunction: return "geçersiz fonksiyon";
    case Exception::IllegalDataAddress: return "geçersiz adres";
    case Exception::IllegalDataValue: return "geçersiz değer";
    case Exception::ServerDeviceFailure: return "cihaz arızası";
    }
    return "bilinmeyen istisna";
}

}  // namespace cihaz::modbus
