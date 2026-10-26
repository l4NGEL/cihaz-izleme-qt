#pragma once
// Arayüz iş parçacığındaki ön yüz: edinim iş parçacığını kurar, DeviceSession'ı
// oraya taşır ve komutları kuyruklu çağrıyla iletir.
//
// Arayüz kodu bu sınıftan başka hiçbir şeye dokunmaz; hangi iş parçacığında
// ne çalıştığı burada saklıdır. stop() oturumu kendi iş parçacığında kapatır
// (soketler yaratıldıkları iş parçacığında yok edilmeli), sonra iş parçacığını
// bekler.

#include <QObject>
#include <QThread>
#include <memory>

#include "session/SessionTypes.h"

namespace cihaz {

class DeviceSession;

class Acquisition : public QObject {
    Q_OBJECT
public:
    explicit Acquisition(QObject* parent = nullptr);
    ~Acquisition() override;

    std::shared_ptr<LiveData> live() const { return live_; }
    bool running() const { return session_ != nullptr; }

public slots:
    void start(const cihaz::SessionConfig& config);
    void stop();

    void writeSetpoint(int rpm);
    void setEnabled(bool on);
    void resetFault();
    void startLogging(const QString& path);
    void stopLogging();
    void addNote(const QString& text);

signals:
    void started(bool ok, const QString& error);
    void stopped();
    void statsUpdated(const cihaz::SessionStats& stats);
    void eventLogged(double t, const QString& text);
    void replayFinished();

private:
    template <class F>
    void post(F&& f);

    std::shared_ptr<LiveData> live_;
    QThread* thread_ = nullptr;
    DeviceSession* session_ = nullptr;
};

}  // namespace cihaz
