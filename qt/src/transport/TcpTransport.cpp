#include "transport/TcpTransport.h"

namespace cihaz {

TcpTransport::TcpTransport(QString host, quint16 port, QObject* parent)
    : ByteTransport(parent), host_(std::move(host)), port_(port), socket_(this), reconnect_(this) {
    reconnect_.setSingleShot(true);
    connect(&reconnect_, &QTimer::timeout, this, [this] {
        if (!userClosed_ && socket_.state() == QAbstractSocket::UnconnectedState)
            socket_.connectToHost(host_, port_);
    });
    // Küçük cihaz paketleri (Modbus istekleri) Nagle yüzünden bekletilmesin
    connect(&socket_, &QTcpSocket::connected, this, [this] {
        socket_.setSocketOption(QAbstractSocket::LowDelayOption, 1);
        emit opened();
    });
    connect(&socket_, &QTcpSocket::readyRead, this, [this] { emit bytesReceived(socket_.readAll()); });
    connect(&socket_, &QTcpSocket::disconnected, this, [this] {
        emit closed();
        if (autoReconnect_ && !userClosed_) reconnect_.start(reconnectMs_);
    });
    connect(&socket_, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError e) {
        if (e == QAbstractSocket::RemoteHostClosedError) return;  // disconnected() ele alır
        emit errorOccurred(QStringLiteral("%1:%2 %3").arg(host_).arg(port_).arg(socket_.errorString()));
        if (autoReconnect_ && !userClosed_ && socket_.state() == QAbstractSocket::UnconnectedState)
            reconnect_.start(reconnectMs_);
    });
}

bool TcpTransport::open() {
    userClosed_ = false;
    socket_.connectToHost(host_, port_);
    return true;
}

void TcpTransport::close() {
    userClosed_ = true;
    reconnect_.stop();
    if (socket_.state() != QAbstractSocket::UnconnectedState) {
        socket_.abort();
        emit closed();
    }
}

bool TcpTransport::isOpen() const { return socket_.state() == QAbstractSocket::ConnectedState; }

qint64 TcpTransport::write(const QByteArray& data) {
    if (!isOpen()) return -1;
    return socket_.write(data);
}

QString TcpTransport::description() const { return QStringLiteral("tcp %1:%2").arg(host_).arg(port_); }

}  // namespace cihaz
