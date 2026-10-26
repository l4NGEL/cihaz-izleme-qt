#pragma once
// QtSerialBus üzerinden CAN arayüzü. Eklenti adıyla seçilir:
//   socketcan:vcan0        Linux SocketCAN (gerçek ya da sanal arayüz)
//   peakcan:usb0?bitrate=500000   Windows'ta PEAK adaptörü
//   virtualcan:can0        Qt'nin süreçler arası sanal veriyolu (her platformda, testler)

#include <QCanBusDevice>
#include <QCanBusFrame>
#include <QObject>
#include <QTimer>
#include <memory>
#include <optional>

#include "cihaz/can_signal.hpp"

namespace cihaz {

struct CanSpec {
    QString plugin;
    QString interfaceName;
    int bitrate = 0;  // 0: eklenti varsayılanı / arayüz zaten yapılandırılmış
};

class CanLink : public QObject {
    Q_OBJECT
public:
    explicit CanLink(CanSpec spec, QObject* parent = nullptr);
    ~CanLink() override;

    static std::optional<CanSpec> parse(const QString& text, QString* error = nullptr);

    bool open();
    void close();
    bool isOpen() const;
    bool send(quint32 id, const can::Payload& data, quint8 dlc);
    QString description() const;

    quint64 errorFrames() const { return errorFrames_; }

signals:
    void frameReceived(quint32 id, const QByteArray& payload, quint64 hostTimeUs);
    void opened();
    void closed();
    void errorOccurred(const QString& message);

private:
    void drain();

    CanSpec spec_;
    std::unique_ptr<QCanBusDevice> device_;
    quint64 errorFrames_ = 0;
};

}  // namespace cihaz
