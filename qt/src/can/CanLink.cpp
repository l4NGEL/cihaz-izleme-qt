#include "can/CanLink.h"

#include <QCanBus>
#include <QUrlQuery>
#include <QVariant>

namespace cihaz {

CanLink::CanLink(CanSpec spec, QObject* parent) : QObject(parent), spec_(std::move(spec)) {}

CanLink::~CanLink() { close(); }

std::optional<CanSpec> CanLink::parse(const QString& text, QString* error) {
    const QString t = text.trimmed();
    const qsizetype colon = t.indexOf(':');
    if (colon <= 0 || colon == t.size() - 1) {
        if (error) *error = QStringLiteral("CAN tanımı eklenti:arayüz biçiminde olmalı");
        return std::nullopt;
    }
    CanSpec s;
    s.plugin = t.left(colon);
    QString rest = t.mid(colon + 1);
    const qsizetype qm = rest.indexOf('?');
    if (qm >= 0) {
        const QUrlQuery q(rest.mid(qm + 1));
        rest = rest.left(qm);
        for (const auto& [key, value] : q.queryItems()) {
            bool ok = false;
            if (key == QLatin1String("bitrate")) s.bitrate = value.toInt(&ok);
            if (!ok || s.bitrate <= 0) {
                if (error) *error = QStringLiteral("geçersiz CAN seçeneği: %1=%2").arg(key, value);
                return std::nullopt;
            }
        }
    }
    s.interfaceName = rest;
    return s;
}

bool CanLink::open() {
    close();
    QString err;
    device_.reset(QCanBus::instance()->createDevice(spec_.plugin, spec_.interfaceName, &err));
    if (!device_) {
        emit errorOccurred(QStringLiteral("CAN eklentisi %1: %2").arg(spec_.plugin, err));
        return false;
    }
    if (spec_.bitrate > 0)
        device_->setConfigurationParameter(QCanBusDevice::BitRateKey, QVariant(spec_.bitrate));
    // Hata çerçevelerini de iste: veriyolu kapanması (bus-off) gibi durumlar görünsün
    device_->setConfigurationParameter(QCanBusDevice::ErrorFilterKey,
                                       QVariant::fromValue(QCanBusFrame::FrameErrors(QCanBusFrame::AnyError)));
    connect(device_.get(), &QCanBusDevice::framesReceived, this, &CanLink::drain);
    connect(device_.get(), &QCanBusDevice::errorOccurred, this, [this](QCanBusDevice::CanBusError) {
        emit errorOccurred(QStringLiteral("CAN: %1").arg(device_->errorString()));
    });
    connect(device_.get(), &QCanBusDevice::stateChanged, this, [this](QCanBusDevice::CanBusDeviceState st) {
        if (st == QCanBusDevice::ConnectedState) emit opened();
        if (st == QCanBusDevice::UnconnectedState) emit closed();
    });
    if (!device_->connectDevice()) {
        emit errorOccurred(QStringLiteral("CAN %1 bağlanamadı: %2").arg(description(), device_->errorString()));
        device_.reset();
        return false;
    }
    return true;
}

void CanLink::close() {
    if (!device_) return;
    device_->disconnect(this);
    if (device_->state() != QCanBusDevice::UnconnectedState) {
        device_->disconnectDevice();
        emit closed();
    }
    device_.reset();
}

bool CanLink::isOpen() const { return device_ && device_->state() == QCanBusDevice::ConnectedState; }

bool CanLink::send(quint32 id, const can::Payload& data, quint8 dlc) {
    if (!isOpen() || dlc > 8) return false;
    QCanBusFrame f(id, QByteArray(reinterpret_cast<const char*>(data.data()), dlc));
    f.setExtendedFrameFormat(id > 0x7FF);
    return device_->writeFrame(f);
}

QString CanLink::description() const {
    return QStringLiteral("%1:%2").arg(spec_.plugin, spec_.interfaceName);
}

void CanLink::drain() {
    while (device_ && device_->framesAvailable()) {
        const QCanBusFrame f = device_->readFrame();
        if (f.frameType() == QCanBusFrame::ErrorFrame) {
            ++errorFrames_;
            emit errorOccurred(QStringLiteral("CAN hata çerçevesi: %1").arg(device_->interpretErrorFrame(f)));
            continue;
        }
        if (f.frameType() != QCanBusFrame::DataFrame) continue;
        const auto ts = f.timeStamp();
        const quint64 us = static_cast<quint64>(ts.seconds()) * 1000000ULL + static_cast<quint64>(ts.microSeconds());
        emit frameReceived(f.frameId(), f.payload(), us);
    }
}

}  // namespace cihaz
