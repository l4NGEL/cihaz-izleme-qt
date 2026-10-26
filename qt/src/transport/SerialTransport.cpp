#include "transport/SerialTransport.h"

namespace cihaz {

SerialTransport::SerialTransport(SerialSettings settings, QObject* parent)
    : ByteTransport(parent), s_(std::move(settings)), port_(this), reconnect_(this) {
    reconnect_.setSingleShot(true);
    connect(&reconnect_, &QTimer::timeout, this, [this] {
        if (!port_.isOpen() && !userClosed_) open();
    });
    connect(&port_, &QSerialPort::readyRead, this, [this] { emit bytesReceived(port_.readAll()); });
    connect(&port_, &QSerialPort::errorOccurred, this, &SerialTransport::onError);
}

bool SerialTransport::open() {
    userClosed_ = false;
    port_.setPortName(s_.port);
    port_.setBaudRate(s_.baud);
    port_.setDataBits(s_.dataBits);
    port_.setParity(s_.parity);
    port_.setStopBits(s_.stopBits);
    port_.setFlowControl(s_.flow);
    opening_ = true;
    const bool ok = port_.open(QIODevice::ReadWrite);
    opening_ = false;
    if (!ok) {
        emit errorOccurred(QStringLiteral("%1 açılamadı: %2").arg(s_.port, port_.errorString()));
        if (autoReconnect_) reconnect_.start(reconnectMs_);
        return false;
    }
    port_.clear();  // açılıştan önce sürücüde birikmiş eski baytları at
    emit opened();
    return true;
}

void SerialTransport::close() {
    userClosed_ = true;
    reconnect_.stop();
    if (port_.isOpen()) {
        port_.close();
        emit closed();
    }
}

qint64 SerialTransport::write(const QByteArray& data) {
    if (!port_.isOpen()) return -1;
    return port_.write(data);
}

QString SerialTransport::description() const {
    return QStringLiteral("seri %1 @ %2").arg(s_.port).arg(s_.baud);
}

void SerialTransport::onError(QSerialPort::SerialPortError e) {
    // Açılış hatası open() içinde bir kez bildirilir
    if (e == QSerialPort::NoError || e == QSerialPort::TimeoutError || opening_) return;
    // USB-seri dönüştürücü çekildiğinde ResourceError gelir; port kapanmış sayılır.
    if (e == QSerialPort::ResourceError || e == QSerialPort::PermissionError) {
        const QString msg = port_.errorString();
        if (port_.isOpen()) {
            port_.close();
            emit closed();
        }
        emit errorOccurred(QStringLiteral("%1: %2").arg(s_.port, msg));
        if (autoReconnect_ && !userClosed_) reconnect_.start(reconnectMs_);
        return;
    }
    emit errorOccurred(QStringLiteral("%1: %2").arg(s_.port, port_.errorString()));
}

}  // namespace cihaz
