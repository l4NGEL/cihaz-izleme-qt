#include "AppController.h"

#include <QCanBus>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QPointer>
#include <QSerialPortInfo>
#include <QStandardPaths>
#include <QUrl>
#include <QtConcurrent/QtConcurrentRun>
#include <fstream>

using namespace cihaz;

AppController::AppController(QObject* parent)
    : QObject(parent), channels_(acq_.live()), settings_(QStringLiteral("cihaz-izleme"), QStringLiteral("cihaz-izleme")) {
    telemetry_ = settings_.value(QStringLiteral("baglanti/telemetri"), QStringLiteral("tcp:127.0.0.1:5001")).toString();
    modbus_ = settings_.value(QStringLiteral("baglanti/modbus"), QStringLiteral("tcp:127.0.0.1:5020")).toString();
    modbusTcp_ = settings_.value(QStringLiteral("baglanti/modbusTcp"), true).toBool();
    can_ = settings_.value(QStringLiteral("baglanti/can")).toString();

    connect(&acq_, &Acquisition::statsUpdated, this, &AppController::onStats);
    connect(&acq_, &Acquisition::eventLogged, this, [this](double t, const QString& text) { events_.add(t, text); });
    connect(&acq_, &Acquisition::started, this, [this](bool ok, const QString& error) {
        if (!ok) {
            events_.add(0, QStringLiteral("başlatılamadı: ") + error);
            setStatus(QStringLiteral("Bağlantı kurulamadı: ") + error);
            acq_.stop();
            running_ = false;
            emit runningChanged();
            return;
        }
        setStatus(replaying_ ? QStringLiteral("Kayıt oynatılıyor") : QStringLiteral("Bağlı"));
    });
    connect(&acq_, &Acquisition::replayFinished, this, [this] { setStatus(QStringLiteral("Oynatma bitti")); });
    connect(&exportWatcher_, &QFutureWatcher<CsvExportResult>::finished, this, [this] {
        const CsvExportResult r = exportWatcher_.result();
        exportProgress_ = -1;
        emit exportProgressChanged();
        events_.add(0, r.ok ? QStringLiteral("CSV hazır: %1 satır").arg(r.rows)
                            : QStringLiteral("CSV başarısız: ") + QString::fromStdString(r.error));
    });
    setStatus(QStringLiteral("Bağlı değil"));
}

AppController::~AppController() { acq_.stop(); }

void AppController::setTelemetrySpec(const QString& s) {
    if (s == telemetry_) return;
    telemetry_ = s;
    settings_.setValue(QStringLiteral("baglanti/telemetri"), s);
    emit settingsChanged();
}

void AppController::setModbusSpec(const QString& s) {
    if (s == modbus_) return;
    modbus_ = s;
    settings_.setValue(QStringLiteral("baglanti/modbus"), s);
    emit settingsChanged();
}

void AppController::setModbusTcp(bool on) {
    if (on == modbusTcp_) return;
    modbusTcp_ = on;
    settings_.setValue(QStringLiteral("baglanti/modbusTcp"), on);
    emit settingsChanged();
}

void AppController::setCanSpec(const QString& s) {
    if (s == can_) return;
    can_ = s;
    settings_.setValue(QStringLiteral("baglanti/can"), s);
    emit settingsChanged();
}

void AppController::start(const SessionConfig& cfg, bool replay) {
    events_.clear();
    channels_.reset();
    replaying_ = replay;
    driveEnabled_ = false;
    emit driveEnabledChanged();
    running_ = true;
    emit runningChanged();
    setStatus(QStringLiteral("Bağlanıyor…"));
    acq_.start(cfg);
}

void AppController::connectDevice() {
    SessionConfig cfg;
    cfg.telemetry = telemetry_.trimmed();
    cfg.modbus = modbus_.trimmed();
    cfg.modbusTcp = modbusTcp_;
    cfg.can = can_.trimmed();
    start(cfg, false);
}

void AppController::disconnectDevice() {
    acq_.stop();
    running_ = false;
    emit runningChanged();
    setStatus(QStringLiteral("Bağlı değil"));
}

void AppController::startReplay(const QUrl& file, double speed) {
    SessionConfig cfg;
    cfg.replayPath = file.isLocalFile() ? file.toLocalFile() : file.toString();
    cfg.replaySpeed = speed;
    start(cfg, true);
}

QString AppController::logDirectory() const {
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + QStringLiteral("/cihaz-kayitlari");
    QDir().mkpath(dir);
    return dir;
}

QString AppController::newLogPath() const {
    return logDirectory() + QStringLiteral("/msd200_%1.cil")
                                .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss")));
}

void AppController::toggleRecording() {
    if (!running_ || replaying_) return;
    if (recording())
        acq_.stopLogging();
    else
        acq_.startLogging(newLogPath());
}

void AppController::setSetpoint(int rpm) { acq_.writeSetpoint(rpm); }

void AppController::setDriveEnabled(bool on) {
    driveEnabled_ = on;
    emit driveEnabledChanged();
    acq_.setEnabled(on);
}

void AppController::resetFault() { acq_.resetFault(); }

void AppController::addNote(const QString& text) {
    if (!text.trimmed().isEmpty()) acq_.addNote(text.trimmed());
}

void AppController::exportCsv(const QUrl& logFile) {
    if (exportWatcher_.isRunning()) return;
    const QString in = logFile.isLocalFile() ? logFile.toLocalFile() : logFile.toString();
    const QString out = in + QStringLiteral(".csv");
    exportProgress_ = 0;
    emit exportProgressChanged();
    QPointer<AppController> self(this);
    exportWatcher_.setFuture(QtConcurrent::run([in, out, self] {
        std::ofstream f(out.toStdString(), std::ios::binary);
        return exportLogToCsv(in.toStdString(), f, [self](double fraction) {
            QMetaObject::invokeMethod(qApp, [self, fraction] {
                if (!self) return;
                self->exportProgress_ = fraction;
                emit self->exportProgressChanged();
            }, Qt::QueuedConnection);
            return true;
        });
    }));
    events_.add(0, QStringLiteral("CSV'ye aktarılıyor: ") + out);
}

QStringList AppController::serialPorts() const {
    QStringList out;
    for (const QSerialPortInfo& p : QSerialPortInfo::availablePorts()) {
#ifdef Q_OS_WIN
        out << QStringLiteral("serial:%1?baud=115200").arg(p.portName());
#else
        out << QStringLiteral("serial:%1?baud=115200").arg(p.systemLocation());
#endif
    }
    return out;
}

QStringList AppController::canPlugins() const { return QCanBus::instance()->plugins(); }

void AppController::setStatus(const QString& s) {
    if (s == status_) return;
    status_ = s;
    emit statusTextChanged();
}

void AppController::onStats(const SessionStats& s) {
    const auto n = [](quint64 v) { return QVariant::fromValue(static_cast<double>(v)); };
    QVariantMap m;
    m[QStringLiteral("sure")] = s.sessionTime;
    m[QStringLiteral("oynatma")] = s.replay;
    m[QStringLiteral("oynatmaIlerleme")] = s.replayProgress;
    m[QStringLiteral("telemetriAcik")] = s.telemetryOpen;
    m[QStringLiteral("modbusAcik")] = s.modbusOpen;
    m[QStringLiteral("canAcik")] = s.canOpen;
    m[QStringLiteral("kayit")] = s.logging;
    m[QStringLiteral("uartBayt")] = n(s.uartBytes);
    m[QStringLiteral("uartCerceve")] = n(s.uartFrames);
    m[QStringLiteral("crcHata")] = n(s.crcErrors);
    m[QStringLiteral("uzunlukHata")] = n(s.lengthErrors);
    m[QStringLiteral("atilanBayt")] = n(s.droppedBytes);
    m[QStringLiteral("siraAtlama")] = n(s.seqGaps);
    m[QStringLiteral("mbIstek")] = n(s.mbRequests);
    m[QStringLiteral("mbYanit")] = n(s.mbResponses);
    m[QStringLiteral("mbZamanAsimi")] = n(s.mbTimeouts);
    m[QStringLiteral("mbGecersiz")] = n(s.mbInvalid);
    m[QStringLiteral("mbIstisna")] = n(s.mbExceptions);
    m[QStringLiteral("mbYenidenDeneme")] = n(s.mbRetries);
    m[QStringLiteral("mbGecikme")] = s.mbLatencyMs;
    m[QStringLiteral("mbMaxGecikme")] = s.mbMaxLatencyMs;
    m[QStringLiteral("canCerceve")] = n(s.canFrames);
    m[QStringLiteral("ornek")] = n(s.samples);
    m[QStringLiteral("kayitSayisi")] = n(s.logRecords);
    m[QStringLiteral("kayitBayt")] = n(s.logBytes);
    // Saniyelik hız: önceki örnekle fark
    const double prevT = stats_.value(QStringLiteral("sure")).toDouble();
    const double prevN = stats_.value(QStringLiteral("uartCerceve")).toDouble();
    const double dt = s.sessionTime - prevT;
    m[QStringLiteral("cerceveHizi")] = dt > 0.05 ? (static_cast<double>(s.uartFrames) - prevN) / dt
                                                 : stats_.value(QStringLiteral("cerceveHizi"), 0.0);
    stats_ = m;
    emit statsChanged();
}
