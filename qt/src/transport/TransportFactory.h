#pragma once
// Bağlantı tanımı metninden uygun taşıma nesnesini üretir (fabrika).
//
//   serial:COM3?baud=115200&parity=even&stop=1&data=8
//   serial:/dev/ttyUSB0?baud=19200
//   tcp:192.168.1.50:5020
//   udp:14550                      (tüm arayüzlerde dinle)
//   udp:0.0.0.0:14550?remote=192.168.1.10:14551
//
// Aynı metin biçimi komut satırında, ayarlar dosyasında ve arayüzde kullanılır.

#include <memory>
#include <optional>

#include "transport/ByteTransport.h"
#include "transport/SerialTransport.h"

namespace cihaz {

struct TransportSpec {
    enum class Kind { Serial, Tcp, Udp } kind = Kind::Serial;
    SerialSettings serial;
    QString host;
    quint16 port = 0;
    QString remoteHost;
    quint16 remotePort = 0;
};

class TransportFactory {
public:
    static std::optional<TransportSpec> parse(const QString& spec, QString* error = nullptr);
    static std::unique_ptr<ByteTransport> create(const TransportSpec& spec);
    static std::unique_ptr<ByteTransport> create(const QString& spec, QString* error = nullptr);
};

}  // namespace cihaz
