#include "session/DeviceSession.h"

#include <QDateTime>
#include <QFileInfo>
#include <algorithm>

#include "can/CanLink.h"
#include "modbus/ModbusMaster.h"
#include "transport/TransportFactory.h"

namespace cihaz {

DeviceSession::DeviceSession(std::shared_ptr<LiveData> live, QObject* parent)
    : QObject(parent),
      live_(std::move(live)),
      statsTimer_(this),
      flushTimer_(this),
      clockTimer_(this),
      replayTimer_(this) {
    connect(&statsTimer_, &QTimer::timeout, this, [this] { emit statsUpdated(snapshot()); });
    connect(&flushTimer_, &QTimer::timeout, this, [this] { log_.flush(); });
    // Cihaz susarsa da grafik zaman ekseni ilerlesin
    connect(&clockTimer_, &QTimer::timeout, this, [this] {
        if (!replay_) live_->timeSec.store(now(), std::memory_order_relaxed);
    });
    connect(&replayTimer_, &QTimer::timeout, this, &DeviceSession::onReplayTick);
}

DeviceSession::~DeviceSession() { stop(); }

double DeviceSession::now() const {
    return replay_ ? replayTime_ : static_cast<double>(clock_.nsecsElapsed()) / 1e9;
}

std::uint64_t DeviceSession::nowNs() const { return static_cast<std::uint64_t>(clock_.nsecsElapsed()); }

void DeviceSession::start(const SessionConfig& config) {
    stop();
    cfg_ = config;
    decoder_.reset();
    live_->store.clear();
    live_->timeSec = 0;
    uartBytes_ = canFrames_ = 0;
    suppressedFailures_ = 0;
    lastFailureReport_.invalidate();
    clock_.start();

    QString error;
    const bool ok = cfg_.replayPath.isEmpty() ? openLive(&error) : openReplay(&error);
    if (!ok) {
        stop();
        emit started(false, error);
        return;
    }
    running_ = true;
    // Kayıt açılamaması edinimi durdurmaz; kullanıcı olay listesinde görür
    if (!cfg_.logPath.isEmpty() && !replay_) startLogging(cfg_.logPath);
    statsTimer_.start(250);
    flushTimer_.start(500);
    clockTimer_.start(20);
    emit started(true, {});
}

bool DeviceSession::openLive(QString* error) {
    if (cfg_.telemetry.isEmpty() && cfg_.modbus.isEmpty() && cfg_.can.isEmpty()) {
        *error = QStringLiteral("hiçbir hat seçilmedi");
        return false;
    }
    if (!cfg_.telemetry.isEmpty()) {
        telemetry_ = TransportFactory::create(cfg_.telemetry, error);
        if (!telemetry_) {
            *error = QStringLiteral("telemetri: ") + *error;
            return false;
        }
        telemetry_->setAutoReconnect(cfg_.autoReconnect);
        connect(telemetry_.get(), &ByteTransport::bytesReceived, this, &DeviceSession::onUartBytes);
        connect(telemetry_.get(), &ByteTransport::errorOccurred, this,
                [this](const QString& m) { reportLinkError(QStringLiteral("telemetri"), m); });
        connect(telemetry_.get(), &ByteTransport::opened, this,
                [this] { logEvent(QStringLiteral("telemetri bağlandı: ") + telemetry_->description()); });
        connect(telemetry_.get(), &ByteTransport::closed, this,
                [this] { logEvent(QStringLiteral("telemetri bağlantısı kapandı")); });
        if (!telemetry_->open() && !cfg_.autoReconnect) {
            *error = QStringLiteral("telemetri hattı açılamadı");
            return false;
        }
    }
    if (!cfg_.modbus.isEmpty()) {
        modbusLink_ = TransportFactory::create(cfg_.modbus, error);
        if (!modbusLink_) {
            *error = QStringLiteral("Modbus: ") + *error;
            return false;
        }
        modbusLink_->setAutoReconnect(cfg_.autoReconnect);
        modbus_ = std::make_unique<ModbusMaster>(
            modbusLink_.get(), cfg_.modbusTcp ? ModbusMaster::Framing::Tcp : ModbusMaster::Framing::Rtu);
        modbus_->setTimeout(cfg_.modbusTimeoutMs);
        const auto unit = static_cast<std::uint8_t>(cfg_.modbusUnit);
        modbus_->addPoll(modbus::Request::readInput(unit, reg::InputBase, reg::InputCount), cfg_.pollMs);
        modbus_->addPoll(modbus::Request::readHolding(unit, reg::Setpoint, 3), 500);

        connect(modbus_.get(), &ModbusMaster::finished, this,
                [this](quint64, const modbus::Request& req, const modbus::Response& resp) {
                    if (resp.isException()) {
                        logEvent(QStringLiteral("Modbus istisnası: %1 (adres %2)")
                                     .arg(QString::fromUtf8(modbus::exceptionName(resp.exception)))
                                     .arg(req.address));
                        return;
                    }
                    if (req.function == modbus::Function::ReadInputRegisters ||
                        req.function == modbus::Function::ReadHoldingRegisters) {
                        const double t = now();
                        if (log_.isOpen()) {
                            const Bytes b = logf::packModbusRead(req, resp.registers);
                            log_.write(nowNs(), logf::RecordType::ModbusRead, b.data(), b.size());
                        }
                        decoder_.modbusRead(req.function, req.address, resp.registers, t);
                        publish();
                    } else {
                        logEvent(QStringLiteral("Modbus yazma onaylandı: register %1 = %2")
                                     .arg(req.address)
                                     .arg(static_cast<qint16>(req.values.at(0))));
                    }
                });
        connect(modbus_.get(), &ModbusMaster::failed, this,
                [this](quint64, const modbus::Request& req, const QString& reason) {
                    const bool isWrite = req.function == modbus::Function::WriteSingleRegister ||
                                         req.function == modbus::Function::WriteMultipleRegisters;
                    if (isWrite) {  // kullanıcı komutları hiçbir zaman bastırılmaz
                        logEvent(QStringLiteral("Modbus yazma BAŞARISIZ (register %1): %2").arg(req.address).arg(reason));
                        return;
                    }
                    reportLinkError(QStringLiteral("Modbus"), reason);
                });
        connect(modbusLink_.get(), &ByteTransport::errorOccurred, this,
                [this](const QString& m) { reportLinkError(QStringLiteral("Modbus hattı"), m); });
        connect(modbusLink_.get(), &ByteTransport::opened, this,
                [this] { logEvent(QStringLiteral("Modbus hattı bağlandı: ") + modbusLink_->description()); });
        if (!modbusLink_->open() && !cfg_.autoReconnect) {
            *error = QStringLiteral("Modbus hattı açılamadı");
            return false;
        }
    }
    if (!cfg_.can.isEmpty()) {
        const auto spec = CanLink::parse(cfg_.can, error);
        if (!spec) return false;
        can_ = std::make_unique<CanLink>(*spec);
        connect(can_.get(), &CanLink::frameReceived, this,
                [this](quint32 id, const QByteArray& payload, quint64) { onCanFrame(id, payload); });
        connect(can_.get(), &CanLink::errorOccurred, this,
                [this](const QString& m) { reportLinkError(QStringLiteral("CAN"), m); });
        if (!can_->open()) {
            *error = QStringLiteral("CAN arayüzü açılamadı: ") + can_->description();
            return false;
        }
        logEvent(QStringLiteral("CAN bağlandı: ") + can_->description());
    }
    return true;
}

bool DeviceSession::openReplay(QString* error) {
    replay_ = std::make_unique<logf::Reader>();
    if (!replay_->open(cfg_.replayPath.toStdString())) {
        *error = QStringLiteral("kayıt açılamadı: ") + QString::fromStdString(replay_->error());
        replay_.reset();
        return false;
    }
    replayTotal_ = static_cast<std::uint64_t>(QFileInfo(cfg_.replayPath).size());
    replayBytes_ = logf::kFileHeader;
    replayNext_ = replay_->next();
    replayTime_ = 0;
    replayClock_.start();
    replayTimer_.start(10);
    const auto started =
        QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(replay_->startUnixNs() / 1000000ULL));
    logEvent(QStringLiteral("tekrar oynatma: %1 (kayıt zamanı %2, hız x%3)")
                 .arg(QFileInfo(cfg_.replayPath).fileName(), started.toString(Qt::ISODate))
                 .arg(cfg_.replaySpeed));
    return true;
}

void DeviceSession::stop() {
    const bool wasRunning = running_;
    running_ = false;
    statsTimer_.stop();
    flushTimer_.stop();
    clockTimer_.stop();
    replayTimer_.stop();
    if (wasRunning) emit statsUpdated(snapshot());
    // Önce Modbus (taşıma nesnesine bağlı), sonra hatlar
    modbus_.reset();
    if (modbusLink_) modbusLink_->close();
    modbusLink_.reset();
    if (telemetry_) telemetry_->close();
    telemetry_.reset();
    can_.reset();
    replay_.reset();
    replayNext_.reset();
    stopLogging();
    if (wasRunning) emit stopped();
}

void DeviceSession::onUartBytes(const QByteArray& data) {
    uartBytes_ += static_cast<quint64>(data.size());
    const auto* p = reinterpret_cast<const std::uint8_t*>(data.constData());
    if (log_.isOpen()) log_.write(nowNs(), logf::RecordType::UartBytes, p, static_cast<std::size_t>(data.size()));
    decoder_.uart(p, static_cast<std::size_t>(data.size()), now());
    publish();
}

void DeviceSession::onCanFrame(quint32 id, const QByteArray& payload) {
    ++canFrames_;
    can::Payload d{};
    const auto dlc = static_cast<std::uint8_t>(std::min<qsizetype>(payload.size(), 8));
    std::copy_n(payload.constData(), dlc, reinterpret_cast<char*>(d.data()));
    if (log_.isOpen()) {
        const Bytes b = logf::packCan(id, dlc, d);
        log_.write(nowNs(), logf::RecordType::CanFrame, b.data(), b.size());
    }
    decoder_.canFrame(id, d, dlc, now());
    publish();
}

void DeviceSession::onReplayTick() {
    const double speed = cfg_.replaySpeed > 0 ? cfg_.replaySpeed : 1.0;
    const auto limitNs = static_cast<std::uint64_t>(static_cast<double>(replayClock_.nsecsElapsed()) * speed);
    while (replayNext_ && replayNext_->tNs <= limitNs) {
        replayBytes_ += logf::kRecordHeader + replayNext_->data.size();
        if (replayNext_->type == logf::RecordType::UartBytes) uartBytes_ += replayNext_->data.size();
        if (replayNext_->type == logf::RecordType::CanFrame) ++canFrames_;
        decoder_.record(*replayNext_);
        replayNext_ = replay_->next();
    }
    replayTime_ = static_cast<double>(limitNs) / 1e9;
    live_->timeSec.store(replayTime_, std::memory_order_relaxed);
    publish();
    if (!replayNext_) {
        replayTimer_.stop();
        if (replay_->truncated()) logEvent(QStringLiteral("kayıt sonu yarım (program çökmüş olabilir)"));
        logEvent(QStringLiteral("tekrar oynatma bitti"));
        emit statsUpdated(snapshot());
        emit replayFinished();
    }
}

void DeviceSession::publish() {
    if (!decoder_.hasPending()) return;
    const auto samples = decoder_.takeSamples();
    if (!samples.empty()) {
        live_->store.appendBatch(samples);
        if (!replay_) live_->timeSec.store(samples.back().p.t, std::memory_order_relaxed);
    }
    for (auto& e : decoder_.takeEvents()) {
        // Cihaz olayları UART baytlarından çözülür; baytlar zaten kayıtta olduğu için
        // olay ayrıca yazılmaz, oynatmada yeniden çözülür.
        emit eventLogged(e.t, QString::fromStdString(e.text));
    }
}

void DeviceSession::logEvent(const QString& text) {
    const double t = now();
    if (log_.isOpen()) {
        const QByteArray utf8 = text.toUtf8();
        log_.write(nowNs(), logf::RecordType::Event, reinterpret_cast<const std::uint8_t*>(utf8.constData()),
                   static_cast<std::size_t>(utf8.size()));
    }
    emit eventLogged(t, text);
}

void DeviceSession::reportLinkError(const QString& link, const QString& message) {
    // Kopuk hatta saniyede 10 zaman aşımı olur; 2 sn'de bir özetle
    if (lastFailureReport_.isValid() && lastFailureReport_.elapsed() < 2000) {
        ++suppressedFailures_;
        return;
    }
    QString text = QStringLiteral("%1: %2").arg(link, message);
    if (suppressedFailures_ > 0) text += QStringLiteral(" (+%1 benzer hata)").arg(suppressedFailures_);
    suppressedFailures_ = 0;
    lastFailureReport_.start();
    logEvent(text);
}

void DeviceSession::writeSetpoint(int rpm) {
    if (!modbus_) {
        logEvent(QStringLiteral("hedef hız yazılamadı: Modbus hattı yok"));
        return;
    }
    const int clamped = std::clamp(rpm, -6000, 6000);
    modbus_->submit(modbus::Request::writeSingle(static_cast<std::uint8_t>(cfg_.modbusUnit), reg::Setpoint,
                                                 static_cast<std::uint16_t>(static_cast<qint16>(clamped))));
    logEvent(QStringLiteral("hedef hız komutu: %1 rpm").arg(clamped));
}

void DeviceSession::writeControl(quint16 value, const QString& what) {
    if (!modbus_) {
        logEvent(what + QStringLiteral(" gönderilemedi: Modbus hattı yok"));
        return;
    }
    modbus_->submit(modbus::Request::writeSingle(static_cast<std::uint8_t>(cfg_.modbusUnit), reg::Control, value));
    logEvent(what);
}

void DeviceSession::setEnabled(bool on) {
    enabled_ = on;
    writeControl(on ? reg::ControlEnable : 0, on ? QStringLiteral("sürücü etkinleştirme komutu")
                                                : QStringLiteral("sürücü durdurma komutu"));
}

void DeviceSession::resetFault() {
    writeControl(static_cast<quint16>((enabled_ ? reg::ControlEnable : 0) | reg::ControlFaultReset),
                 QStringLiteral("arıza sıfırlama komutu"));
}

bool DeviceSession::startLogging(const QString& path) {
    if (replay_) return false;
    const auto startNs = static_cast<std::uint64_t>(QDateTime::currentMSecsSinceEpoch()) * 1000000ULL -
                         nowNs();  // kayıt zamanları oturum saatiyle aynı kalsın
    if (!log_.open(path.toStdString(), startNs)) {
        emit eventLogged(now(), QStringLiteral("kayıt dosyası açılamadı: ") + path);
        return false;
    }
    logEvent(QStringLiteral("kayıt başladı: ") + path);
    return true;
}

void DeviceSession::stopLogging() {
    if (!log_.isOpen()) return;
    logEvent(QStringLiteral("kayıt durdu (%1 kayıt, %2 kB)").arg(log_.records()).arg(log_.bytes() / 1024));
    log_.close();
}

void DeviceSession::addNote(const QString& text) { logEvent(QStringLiteral("not: ") + text); }

SessionStats DeviceSession::snapshot() const {
    SessionStats s;
    s.sessionTime = now();
    s.replay = static_cast<bool>(replay_);
    if (replay_ && replayTotal_ > 0)
        s.replayProgress = replayNext_ ? static_cast<double>(replayBytes_) / static_cast<double>(replayTotal_) : 1.0;
    s.telemetryOpen = telemetry_ && telemetry_->isOpen();
    s.modbusOpen = modbusLink_ && modbusLink_->isOpen();
    s.canOpen = can_ && can_->isOpen();
    s.logging = log_.isOpen();

    const DecoderStats& d = decoder_.stats();
    s.uartBytes = uartBytes_;
    s.uartFrames = d.uart.frames;
    s.crcErrors = d.uart.crcErrors;
    s.lengthErrors = d.uart.lengthErrors;
    s.droppedBytes = d.uart.droppedBytes;
    s.seqGaps = d.uart.seqGaps;
    s.unknownMessages = d.unknownMessages;
    if (modbus_) {
        const ModbusStats& m = modbus_->stats();
        s.mbRequests = m.requests;
        s.mbResponses = m.responses;
        s.mbTimeouts = m.timeouts;
        s.mbInvalid = m.invalidFrames;
        s.mbExceptions = m.exceptions;
        s.mbFailures = m.failures;
        s.mbRetries = m.retries;
        s.mbLatencyMs = m.lastLatencyMs;
        s.mbMaxLatencyMs = m.maxLatencyMs;
    } else if (replay_) {
        s.mbResponses = d.modbusBlocks;
    }
    s.canFrames = canFrames_;
    s.canErrorFrames = can_ ? can_->errorFrames() : 0;
    s.samples = live_->store.totalAppended();
    s.logRecords = log_.records();
    s.logBytes = log_.bytes();
    return s;
}

}  // namespace cihaz
