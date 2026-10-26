#include "transport/UdpTransport.h"

#include <QNetworkDatagram>

namespace cihaz {

UdpTransport::UdpTransport(QHostAddress bindAddress, quint16 bindPort, QObject* parent)
    : ByteTransport(parent), bindAddr_(std::move(bindAddress)), bindPort_(bindPort), socket_(this) {
    connect(&socket_, &QUdpSocket::readyRead, this, [this] {
        while (socket_.hasPendingDatagrams()) {
            const QNetworkDatagram d = socket_.receiveDatagram();
            if (remotePort_ == 0 || remote_.isNull()) {
                remote_ = d.senderAddress();
                remotePort_ = static_cast<quint16>(d.senderPort());
            }
            emit bytesReceived(d.data());
        }
    });
}

void UdpTransport::setRemote(const QHostAddress& addr, quint16 port) {
    remote_ = addr;
    remotePort_ = port;
}

bool UdpTransport::open() {
    if (!socket_.bind(bindAddr_, bindPort_)) {
        emit errorOccurred(QStringLiteral("udp %1 bağlanamadı: %2").arg(bindPort_).arg(socket_.errorString()));
        return false;
    }
    emit opened();
    return true;
}

void UdpTransport::close() {
    if (socket_.state() == QAbstractSocket::BoundState) {
        socket_.close();
        emit closed();
    }
}

bool UdpTransport::isOpen() const { return socket_.state() == QAbstractSocket::BoundState; }

qint64 UdpTransport::write(const QByteArray& data) {
    if (!isOpen() || remotePort_ == 0) return -1;
    return socket_.writeDatagram(data, remote_, remotePort_);
}

QString UdpTransport::description() const {
    return QStringLiteral("udp %1:%2").arg(bindAddr_.toString()).arg(bindPort_);
}

}  // namespace cihaz
