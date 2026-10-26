#pragma once
// Edinim iş parçacığında yaşayan oturum: hatları açar, ham veriyi kaydeder,
// çözer ve örnekleri ortak tampona yazar.
//
// Bu nesne yalnızca kendi iş parçacığından çağrılır (Acquisition, kuyruklu
// çağrılarla erişir). Arayüze iki yolla veri gider:
//   - örnekler: LiveData::store (kilitli, arayüz kendi hızında okur)
//   - sayaçlar ve olaylar: kuyruklu sinyaller (statsUpdated 4 Hz, eventLogged)
// Böylece 500 Hz telemetri arayüz olay kuyruğuna örnek başına sinyal basmaz.

#include <QElapsedTimer>
#include <QObject>
#include <QTimer>
#include <memory>
#include <optional>

#include "cihaz/log_file.hpp"
#include "cihaz/stream_decoder.hpp"
#include "session/SessionTypes.h"

namespace cihaz {

class ByteTransport;
class CanLink;
class ModbusMaster;

class DeviceSession : public QObject {
    Q_OBJECT
public:
    explicit DeviceSession(std::shared_ptr<LiveData> live, QObject* parent = nullptr);
    ~DeviceSession() override;

    double now() const;

public slots:
    void start(const cihaz::SessionConfig& config);
    void stop();

    void writeSetpoint(int rpm);
    void setEnabled(bool on);
    void resetFault();
    bool startLogging(const QString& path);
    void stopLogging();
    void addNote(const QString& text);

signals:
    void started(bool ok, const QString& error);
    void stopped();
    void statsUpdated(const cihaz::SessionStats& stats);
    void eventLogged(double t, const QString& text);
    void replayFinished();

private:
    bool openLive(QString* error);
    bool openReplay(QString* error);
    void onUartBytes(const QByteArray& data);
    void onCanFrame(quint32 id, const QByteArray& payload);
    void onReplayTick();
    void publish();
    void logEvent(const QString& text);
    void reportLinkError(const QString& link, const QString& message);
    void writeControl(quint16 value, const QString& what);
    SessionStats snapshot() const;
    std::uint64_t nowNs() const;

    std::shared_ptr<LiveData> live_;
    SessionConfig cfg_;
    StreamDecoder decoder_;
    logf::Writer log_;
    QElapsedTimer clock_;

    std::unique_ptr<ByteTransport> telemetry_;
    std::unique_ptr<ByteTransport> modbusLink_;
    std::unique_ptr<ModbusMaster> modbus_;
    std::unique_ptr<CanLink> can_;
    QTimer statsTimer_;
    QTimer flushTimer_;
    QTimer clockTimer_;

    // Tekrar oynatma
    std::unique_ptr<logf::Reader> replay_;
    std::optional<logf::Record> replayNext_;
    QTimer replayTimer_;
    QElapsedTimer replayClock_;
    std::uint64_t replayBytes_ = 0;
    std::uint64_t replayTotal_ = 0;
    double replayTime_ = 0;

    quint64 uartBytes_ = 0;
    quint64 canFrames_ = 0;
    bool enabled_ = false;
    bool running_ = false;
    // Kopuk hatta her zaman aşımında olay basmamak için
    QElapsedTimer lastFailureReport_;
    int suppressedFailures_ = 0;
};

}  // namespace cihaz
