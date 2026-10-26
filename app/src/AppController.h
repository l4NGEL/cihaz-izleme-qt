#pragma once
// QML'in tek giriş noktası (görünüm modeli). QML bu nesneyi "App" tekili
// olarak görür; edinim iş parçacığına doğrudan erişmez.
//
// Ayarlar QSettings'te tutulur; uygulama yeniden açıldığında son bağlantı
// tanımları gelir.

#include <QFutureWatcher>
#include <QObject>
#include <QSettings>
#include <QUrl>
#include <QVariantMap>

#include "ChannelModel.h"
#include "EventLogModel.h"
#include "cihaz/csv_export.hpp"
#include "session/Acquisition.h"

class AppController : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(bool replaying READ replaying NOTIFY runningChanged)
    Q_PROPERTY(bool recording READ recording NOTIFY statsChanged)
    Q_PROPERTY(QVariantMap stats READ stats NOTIFY statsChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
    Q_PROPERTY(QObject* channels READ channels CONSTANT)
    Q_PROPERTY(QObject* events READ events CONSTANT)
    Q_PROPERTY(QString telemetrySpec READ telemetrySpec WRITE setTelemetrySpec NOTIFY settingsChanged)
    Q_PROPERTY(QString modbusSpec READ modbusSpec WRITE setModbusSpec NOTIFY settingsChanged)
    Q_PROPERTY(bool modbusTcp READ modbusTcp WRITE setModbusTcp NOTIFY settingsChanged)
    Q_PROPERTY(QString canSpec READ canSpec WRITE setCanSpec NOTIFY settingsChanged)
    Q_PROPERTY(bool driveEnabled READ driveEnabled NOTIFY driveEnabledChanged)
    Q_PROPERTY(double exportProgress READ exportProgress NOTIFY exportProgressChanged)

public:
    explicit AppController(QObject* parent = nullptr);
    ~AppController() override;

    std::shared_ptr<cihaz::LiveData> live() const { return acq_.live(); }

    bool running() const { return running_; }
    bool replaying() const { return running_ && replaying_; }
    bool recording() const { return stats_.value(QStringLiteral("kayit")).toBool(); }
    QVariantMap stats() const { return stats_; }
    QString statusText() const { return status_; }
    QObject* channels() { return &channels_; }
    QObject* events() { return &events_; }
    bool driveEnabled() const { return driveEnabled_; }
    double exportProgress() const { return exportProgress_; }

    QString telemetrySpec() const { return telemetry_; }
    void setTelemetrySpec(const QString& s);
    QString modbusSpec() const { return modbus_; }
    void setModbusSpec(const QString& s);
    bool modbusTcp() const { return modbusTcp_; }
    void setModbusTcp(bool on);
    QString canSpec() const { return can_; }
    void setCanSpec(const QString& s);

    Q_INVOKABLE void connectDevice();
    Q_INVOKABLE void disconnectDevice();
    Q_INVOKABLE void startReplay(const QUrl& file, double speed);
    Q_INVOKABLE void toggleRecording();
    Q_INVOKABLE void setSetpoint(int rpm);
    Q_INVOKABLE void setDriveEnabled(bool on);
    Q_INVOKABLE void resetFault();
    Q_INVOKABLE void addNote(const QString& text);
    Q_INVOKABLE void exportCsv(const QUrl& logFile);
    Q_INVOKABLE QStringList serialPorts() const;
    Q_INVOKABLE QStringList canPlugins() const;
    Q_INVOKABLE QString logDirectory() const;
    Q_INVOKABLE QUrl logDirectoryUrl() const { return QUrl::fromLocalFile(logDirectory()); }
    Q_INVOKABLE int channelId(const QString& key) const { return channels_.indexOfKey(key); }

signals:
    void runningChanged();
    void statsChanged();
    void statusTextChanged();
    void settingsChanged();
    void driveEnabledChanged();
    void exportProgressChanged();

private:
    void start(const cihaz::SessionConfig& cfg, bool replay);
    void setStatus(const QString& s);
    void onStats(const cihaz::SessionStats& s);
    QString newLogPath() const;

    cihaz::Acquisition acq_;
    ChannelModel channels_;
    EventLogModel events_;
    QSettings settings_;
    QVariantMap stats_;
    QString status_;
    QString telemetry_, modbus_, can_;
    bool modbusTcp_ = false;
    bool running_ = false;
    bool replaying_ = false;
    bool driveEnabled_ = false;
    double exportProgress_ = -1;
    QFutureWatcher<cihaz::CsvExportResult> exportWatcher_;
};
