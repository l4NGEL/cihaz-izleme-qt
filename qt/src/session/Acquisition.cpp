#include "session/Acquisition.h"

#include "session/DeviceSession.h"

namespace cihaz {

Acquisition::Acquisition(QObject* parent) : QObject(parent), live_(std::make_shared<LiveData>()) {
    qRegisterMetaType<SessionConfig>();
    qRegisterMetaType<SessionStats>();
}

Acquisition::~Acquisition() { stop(); }

template <class F>
void Acquisition::post(F&& f) {
    if (!session_) return;
    QMetaObject::invokeMethod(session_, std::forward<F>(f), Qt::QueuedConnection);
}

void Acquisition::start(const SessionConfig& config) {
    stop();
    thread_ = new QThread(this);
    thread_->setObjectName(QStringLiteral("edinim"));
    session_ = new DeviceSession(live_);
    session_->moveToThread(thread_);

    // Oturum başka iş parçacığında: bu bağlantılar otomatik olarak kuyruklu olur
    connect(session_, &DeviceSession::started, this, &Acquisition::started);
    connect(session_, &DeviceSession::stopped, this, &Acquisition::stopped);
    connect(session_, &DeviceSession::statsUpdated, this, &Acquisition::statsUpdated);
    connect(session_, &DeviceSession::eventLogged, this, &Acquisition::eventLogged);
    connect(session_, &DeviceSession::replayFinished, this, &Acquisition::replayFinished);

    thread_->start();
    DeviceSession* s = session_;
    post([s, config] { s->start(config); });
}

void Acquisition::stop() {
    if (!session_) return;
    DeviceSession* s = session_;
    // Hatlar oturum iş parçacığında kapatılsın; bitene kadar bekle
    QMetaObject::invokeMethod(s, [s] { s->stop(); }, Qt::BlockingQueuedConnection);
    thread_->quit();
    thread_->wait();
    // İş parçacığı bitti; nesnenin olay döngüsü yok, burada silmek güvenli
    delete session_;
    session_ = nullptr;
    delete thread_;
    thread_ = nullptr;
}

void Acquisition::writeSetpoint(int rpm) {
    DeviceSession* s = session_;
    post([s, rpm] { s->writeSetpoint(rpm); });
}

void Acquisition::setEnabled(bool on) {
    DeviceSession* s = session_;
    post([s, on] { s->setEnabled(on); });
}

void Acquisition::resetFault() {
    DeviceSession* s = session_;
    post([s] { s->resetFault(); });
}

void Acquisition::startLogging(const QString& path) {
    DeviceSession* s = session_;
    post([s, path] { s->startLogging(path); });
}

void Acquisition::stopLogging() {
    DeviceSession* s = session_;
    post([s] { s->stopLogging(); });
}

void Acquisition::addNote(const QString& text) {
    DeviceSession* s = session_;
    post([s, text] { s->addNote(text); });
}

}  // namespace cihaz
