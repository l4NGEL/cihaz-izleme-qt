#pragma once

#include <QSerialPort>
#include <QTimer>

#include "transport/ByteTransport.h"

namespace cihaz {

struct SerialSettings {
    QString port;  // COM3, /dev/ttyUSB0, /dev/pts/4
    qint32 baud = 115200;
    QSerialPort::DataBits dataBits = QSerialPort::Data8;
    QSerialPort::Parity parity = QSerialPort::NoParity;
    QSerialPort::StopBits stopBits = QSerialPort::OneStop;
    QSerialPort::FlowControl flow = QSerialPort::NoFlowControl;
};

class SerialTransport : public ByteTransport {
    Q_OBJECT
public:
    explicit SerialTransport(SerialSettings settings, QObject* parent = nullptr);

    bool open() override;
    void close() override;
    bool isOpen() const override { return port_.isOpen(); }
    qint64 write(const QByteArray& data) override;
    QString description() const override;

    const SerialSettings& settings() const { return s_; }

private:
    void onError(QSerialPort::SerialPortError e);

    SerialSettings s_;
    QSerialPort port_;
    QTimer reconnect_;
    bool userClosed_ = false;
    bool opening_ = false;  // açılış hatası open() içinde tek sefer bildirilir
};

}  // namespace cihaz
