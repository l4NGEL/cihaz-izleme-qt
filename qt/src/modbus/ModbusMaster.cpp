#include "modbus/ModbusMaster.h"

#include <algorithm>

#include "transport/ByteTransport.h"

namespace cihaz {

ModbusMaster::ModbusMaster(ByteTransport* transport, Framing framing, QObject* parent)
    : QObject(parent), transport_(transport), framing_(framing), timeout_(this), gap_(this) {
    timeout_.setSingleShot(true);
    gap_.setSingleShot(true);
    connect(&timeout_, &QTimer::timeout, this, &ModbusMaster::onTimeout);
    connect(&gap_, &QTimer::timeout, this, &ModbusMaster::pump);
    connect(transport_, &ByteTransport::bytesReceived, this, &ModbusMaster::onBytes);
    // Hat yeniden açıldığında kuyrukta bekleyenler gönderilsin
    connect(transport_, &ByteTransport::opened, this, &ModbusMaster::pump);
}

int ModbusMaster::addPoll(const modbus::Request& req, int intervalMs) {
    const int index = static_cast<int>(polls_.size());
    auto* t = new QTimer(this);
    polls_.push_back({req, t, false});
    connect(t, &QTimer::timeout, this, [this, index] {
        Poll& p = polls_[static_cast<std::size_t>(index)];
        if (p.pending) return;  // hat yavaş: aynı sorguyu kuyrukta biriktirme
        p.pending = true;
        enqueue({nextId_++, p.req, retries_ + 1, index}, false);
    });
    t->start(intervalMs);
    return index;
}

void ModbusMaster::clearPolls() {
    for (auto& p : polls_) {
        p.timer->stop();
        p.timer->deleteLater();
    }
    polls_.clear();
    // Kuyruktaki sorgu işleri artık geçersiz dizin taşıyor
    queue_.erase(std::remove_if(queue_.begin(), queue_.end(), [](const Job& j) { return j.pollIndex >= 0; }),
                 queue_.end());
    if (active_) active_->pollIndex = -1;
}

quint64 ModbusMaster::submit(const modbus::Request& req, bool priority) {
    const quint64 id = nextId_++;
    enqueue({id, req, retries_ + 1, -1}, priority);
    return id;
}

void ModbusMaster::enqueue(Job job, bool front) {
    if (front)
        queue_.push_front(std::move(job));
    else
        queue_.push_back(std::move(job));
    pump();
}

void ModbusMaster::pump() {
    if (active_ || gap_.isActive() || queue_.empty() || !transport_->isOpen()) return;
    active_ = std::move(queue_.front());
    queue_.pop_front();
    sendActive();
}

void ModbusMaster::sendActive() {
    rx_.clear();
    Bytes frame;
    if (framing_ == Framing::Rtu) {
        frame = modbus::encodeRtu(active_->req);
    } else {
        ++transactionId_;
        frame = modbus::encodeTcp(active_->req, transactionId_);
    }
    --active_->attemptsLeft;
    ++stats_.requests;
    sentAt_.start();
    transport_->write(QByteArray(reinterpret_cast<const char*>(frame.data()), static_cast<qsizetype>(frame.size())));
    timeout_.start(timeoutMs_);
}

void ModbusMaster::onBytes(const QByteArray& data) {
    if (!active_) {
        stats_.strayBytes += static_cast<quint64>(data.size());
        return;
    }
    rx_.append(data);
    const auto* p = reinterpret_cast<const std::uint8_t*>(rx_.constData());
    const auto n = static_cast<std::size_t>(rx_.size());
    const modbus::ParseResult r = framing_ == Framing::Rtu
                                      ? modbus::parseRtuResponse(active_->req, p, n)
                                      : modbus::parseTcpResponse(active_->req, transactionId_, p, n);
    switch (r.status) {
    case modbus::ParseStatus::Incomplete:
        return;
    case modbus::ParseStatus::Invalid:
        ++stats_.invalidFrames;
        timeout_.stop();
        retryOrFail(QString::fromStdString(r.error));
        return;
    case modbus::ParseStatus::Complete:
        timeout_.stop();
        if (n > r.consumed) stats_.strayBytes += n - r.consumed;
        complete(r.response);
        return;
    }
}

void ModbusMaster::onTimeout() {
    if (!active_) return;
    ++stats_.timeouts;
    retryOrFail(QStringLiteral("zaman aşımı (%1 ms)").arg(timeoutMs_));
}

void ModbusMaster::retryOrFail(const QString& reason) {
    if (active_->attemptsLeft > 0) {
        ++stats_.retries;
        // Yarım kalmış yanıtın kalanı hattan düşsün diye ara bırakıp yeniden gönder
        Job job = std::move(*active_);
        active_.reset();
        queue_.push_front(std::move(job));
        gap_.start(turnaroundMs_ * 4);
        return;
    }
    ++stats_.failures;
    const Job job = std::move(*active_);
    finishActive();
    if (job.pollIndex >= 0 && static_cast<std::size_t>(job.pollIndex) < polls_.size())
        polls_[static_cast<std::size_t>(job.pollIndex)].pending = false;
    emit failed(job.id, job.req, reason);
}

void ModbusMaster::complete(const modbus::Response& resp) {
    ++stats_.responses;
    if (resp.isException()) ++stats_.exceptions;
    stats_.lastLatencyMs = static_cast<double>(sentAt_.nsecsElapsed()) / 1e6;
    stats_.maxLatencyMs = std::max(stats_.maxLatencyMs, stats_.lastLatencyMs);
    const Job job = std::move(*active_);
    finishActive();
    if (job.pollIndex >= 0 && static_cast<std::size_t>(job.pollIndex) < polls_.size())
        polls_[static_cast<std::size_t>(job.pollIndex)].pending = false;
    emit finished(job.id, job.req, resp);
}

void ModbusMaster::finishActive() {
    active_.reset();
    rx_.clear();
    gap_.start(turnaroundMs_);  // RS-485 hat yönü değişimi için kısa ara
}

}  // namespace cihaz
