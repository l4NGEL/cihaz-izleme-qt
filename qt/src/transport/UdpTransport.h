#pragma once

#include <QHostAddress>
#include <QUdpSocket>

#include "transport/ByteTransport.h"

namespace cihaz {

// Yerel porta bağlanıp datagram dinler. Uzak adres verilmemişse yazmalar,
// son datagramın geldiği adrese gider (cihaz ilk paketi atınca yanıt verilebilir).
class UdpTransport : public ByteTransport {
    Q_OBJECT
public:
    UdpTransport(QHostAddress bindAddress, quint16 bindPort, QObject* parent = nullptr);

    void setRemote(const QHostAddress& addr, quint16 port);

    bool open() override;
    void close() override;
    bool isOpen() const override;
    qint64 write(const QByteArray& data) override;
    QString description() const override;
    quint16 localPort() const { return socket_.localPort(); }

private:
    QHostAddress bindAddr_;
    quint16 bindPort_;
    QHostAddress remote_;
    quint16 remotePort_ = 0;
    QUdpSocket socket_;
};

}  // namespace cihaz
