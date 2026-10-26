#pragma once
// Bayt akışı taşıyan hatların ortak arayüzü (seri port, TCP, UDP).
//
// Üst katmanlar (telemetri ayrıştırıcı, Modbus master) hattın türünü bilmez;
// örneğin Modbus RTU, RS-485 yerine bir seri-ethernet dönüştürücü üzerinden
// TCP ile de konuşabilir. Nesneler oluşturuldukları iş parçacığında kullanılır.

#include <QByteArray>
#include <QObject>
#include <QString>

namespace cihaz {

class ByteTransport : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    ~ByteTransport() override = default;

    virtual bool open() = 0;
    virtual void close() = 0;
    virtual bool isOpen() const = 0;
    virtual qint64 write(const QByteArray& data) = 0;
    virtual QString description() const = 0;

    // Bağlantı kopunca yeniden denensin mi (TCP ve seri port için anlamlı)
    void setAutoReconnect(bool on, int intervalMs = 1000) {
        autoReconnect_ = on;
        reconnectMs_ = intervalMs;
    }

signals:
    void bytesReceived(const QByteArray& data);
    void opened();
    void closed();
    void errorOccurred(const QString& message);

protected:
    bool autoReconnect_ = false;
    int reconnectMs_ = 1000;
};

}  // namespace cihaz
