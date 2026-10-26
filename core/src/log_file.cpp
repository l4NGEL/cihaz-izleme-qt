#include "cihaz/log_file.hpp"

#include <cstring>

namespace cihaz::logf {

Bytes packModbusRead(const modbus::Request& req, const std::vector<std::uint16_t>& regs) {
    Bytes b;
    b.push_back(static_cast<std::uint8_t>(req.function));
    putU16le(b, req.address);
    putU16le(b, static_cast<std::uint16_t>(regs.size()));
    for (auto r : regs) putU16le(b, r);
    return b;
}

bool unpackModbusRead(const Bytes& d, modbus::Function& fn, std::uint16_t& addr,
                      std::vector<std::uint16_t>& regs) {
    if (d.size() < 5) return false;
    fn = static_cast<modbus::Function>(d[0]);
    addr = readU16le(d.data() + 1);
    const std::uint16_t n = readU16le(d.data() + 3);
    if (d.size() != 5U + 2U * n) return false;
    regs.clear();
    for (std::size_t i = 0; i < n; ++i) regs.push_back(readU16le(d.data() + 5 + 2 * i));
    return true;
}

Bytes packCan(std::uint32_t id, std::uint8_t dlc, const can::Payload& payload) {
    Bytes b;
    putU32le(b, id);
    b.push_back(dlc);
    b.insert(b.end(), payload.begin(), payload.end());
    return b;
}

bool unpackCan(const Bytes& d, std::uint32_t& id, std::uint8_t& dlc, can::Payload& payload) {
    if (d.size() != 13) return false;
    id = readU32le(d.data());
    dlc = d[4];
    if (dlc > 8) return false;
    std::memcpy(payload.data(), d.data() + 5, 8);
    return true;
}

Writer::~Writer() { close(); }

bool Writer::open(const std::string& path, std::uint64_t startUnixNs) {
    close();
    out_.open(path, std::ios::binary | std::ios::trunc);
    if (!out_) return false;
    Bytes h(kMagic, kMagic + 8);
    putU32le(h, kVersion);
    putU64le(h, startUnixNs);
    out_.write(reinterpret_cast<const char*>(h.data()), static_cast<std::streamsize>(h.size()));
    records_ = 0;
    bytes_ = h.size();
    return static_cast<bool>(out_);
}

void Writer::close() {
    if (out_.is_open()) {
        out_.flush();
        out_.close();
    }
}

bool Writer::write(const Record& r) {
    return write(r.tNs, r.type, r.data.data(), r.data.size(), r.flags);
}

bool Writer::write(std::uint64_t tNs, RecordType type, const std::uint8_t* data, std::size_t len,
                   std::uint8_t flags) {
    if (!out_.is_open() || len > 0xFFFF) return false;
    Bytes h;
    h.reserve(kRecordHeader);
    putU64le(h, tNs);
    h.push_back(static_cast<std::uint8_t>(type));
    h.push_back(flags);
    putU16le(h, static_cast<std::uint16_t>(len));
    out_.write(reinterpret_cast<const char*>(h.data()), static_cast<std::streamsize>(h.size()));
    if (len) out_.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(len));
    ++records_;
    bytes_ += kRecordHeader + len;
    return static_cast<bool>(out_);
}

void Writer::flush() {
    if (out_.is_open()) out_.flush();
}

bool Reader::open(const std::string& path) {
    in_.open(path, std::ios::binary);
    if (!in_) {
        error_ = "dosya açılamadı";
        return false;
    }
    std::uint8_t h[kFileHeader];
    if (!in_.read(reinterpret_cast<char*>(h), kFileHeader)) {
        error_ = "başlık eksik";
        return false;
    }
    if (std::memcmp(h, kMagic, 8) != 0) {
        error_ = "kayıt dosyası değil";
        return false;
    }
    if (readU32le(h + 8) != kVersion) {
        error_ = "desteklenmeyen sürüm";
        return false;
    }
    startNs_ = readU64le(h + 12);
    return true;
}

std::optional<Record> Reader::next() {
    std::uint8_t h[kRecordHeader];
    in_.read(reinterpret_cast<char*>(h), kRecordHeader);
    const auto got = in_.gcount();
    if (got == 0) return std::nullopt;
    if (got < static_cast<std::streamsize>(kRecordHeader)) {
        truncated_ = true;
        return std::nullopt;
    }
    Record r;
    r.tNs = readU64le(h);
    r.type = static_cast<RecordType>(h[8]);
    r.flags = h[9];
    const std::uint16_t len = readU16le(h + 10);
    r.data.resize(len);
    if (len && !in_.read(reinterpret_cast<char*>(r.data.data()), len)) {
        truncated_ = true;
        return std::nullopt;
    }
    return r;
}

}  // namespace cihaz::logf
