#pragma once
// Ham veri kayıt dosyası (.cil): "kara kutu" mantığı.
//
// Çözülmüş değerler değil, hattan gelen ham veri kaydedilir: UART'ın bayt
// parçaları (gürültü dahil), tamamlanmış Modbus işlemleri ve CAN çerçeveleri.
// Tekrar oynatma aynı çözücülerden geçtiği için bir saha kaydı ofiste birebir
// yeniden üretilebilir; CRC hatası, sıra atlaması gibi sorunlar da kayıtla gelir.
//
// Dosya:   "CIHZLOG1" | u32 sürüm | u64 başlangıç (Unix ns)
// Kayıt:   u64 t_ns (başlangıçtan) | u8 tür | u8 bayrak | u16 uzunluk | veri
// Tüm sayılar küçük uçlu. Program çökerse son kayıt yarım kalabilir; okuyucu
// yarım kaydı hata saymaz, "truncated" olarak bildirir.

#include <cstdint>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include "cihaz/bytes.hpp"
#include "cihaz/can_signal.hpp"
#include "cihaz/modbus.hpp"

namespace cihaz::logf {

constexpr char kMagic[8] = {'C', 'I', 'H', 'Z', 'L', 'O', 'G', '1'};
constexpr std::uint32_t kVersion = 1;
constexpr std::size_t kFileHeader = 20;
constexpr std::size_t kRecordHeader = 12;

enum class RecordType : std::uint8_t {
    UartBytes = 1,   // ham bayt parçası
    ModbusRead = 2,  // u8 fonksiyon | u16 adres | u16 sayı | sayı x u16 değer
    CanFrame = 3,    // u32 kimlik | u8 dlc | 8 bayt veri
    Event = 4,       // UTF-8 metin (bağlandı, zaman aşımı, kullanıcı notu...)
};

struct Record {
    std::uint64_t tNs = 0;
    RecordType type = RecordType::Event;
    std::uint8_t flags = 0;  // CanFrame: bit0 genişletilmiş kimlik
    Bytes data;
};

// Kayıt içeriği yardımcıları
Bytes packModbusRead(const modbus::Request& req, const std::vector<std::uint16_t>& regs);
bool unpackModbusRead(const Bytes& data, modbus::Function& fn, std::uint16_t& addr,
                      std::vector<std::uint16_t>& regs);
Bytes packCan(std::uint32_t id, std::uint8_t dlc, const can::Payload& payload);
bool unpackCan(const Bytes& data, std::uint32_t& id, std::uint8_t& dlc, can::Payload& payload);

class Writer {
public:
    Writer() = default;
    ~Writer();
    Writer(const Writer&) = delete;
    Writer& operator=(const Writer&) = delete;

    bool open(const std::string& path, std::uint64_t startUnixNs);
    void close();
    bool isOpen() const { return out_.is_open(); }

    bool write(const Record& r);
    bool write(std::uint64_t tNs, RecordType type, const std::uint8_t* data, std::size_t len,
               std::uint8_t flags = 0);
    void flush();

    std::uint64_t records() const { return records_; }
    std::uint64_t bytes() const { return bytes_; }

private:
    std::ofstream out_;
    std::uint64_t records_ = 0;
    std::uint64_t bytes_ = 0;
};

class Reader {
public:
    bool open(const std::string& path);
    std::optional<Record> next();

    std::uint64_t startUnixNs() const { return startNs_; }
    bool truncated() const { return truncated_; }
    const std::string& error() const { return error_; }

private:
    std::ifstream in_;
    std::uint64_t startNs_ = 0;
    bool truncated_ = false;
    std::string error_;
};

}  // namespace cihaz::logf
