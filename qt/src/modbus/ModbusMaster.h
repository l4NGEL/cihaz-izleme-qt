#pragma once
// Tek hat üzerinde sırayla çalışan Modbus istemcisi (RTU ya da TCP çerçeveleme).
//
// RS-485 yarı çift yönlüdür: aynı anda yalnızca bir istek "uçuşta" olabilir.
// Durum makinesi:
//
//   Boşta --gönder--> YanıtBekle --tamam/istisna--> Ara(turnaround) --> Boşta
//                         |  \--geçersiz/zaman aşımı--> yeniden dene ya da başarısız
//
// Periyodik sorgular (poll) ve kullanıcı yazmaları aynı kuyruğa girer; yazmalar
// öne alınır, böylece 10 Hz sorgu trafiği bir hedef hız komutunu geciktirmez.
// Yanıt beklenmezken gelen baytlar "başıboş" sayılıp atılır (geç gelen eski
// yanıt bir sonraki isteğinkiyle karışmasın).

#include <QElapsedTimer>
#include <QObject>
#include <QTimer>
#include <deque>
#include <optional>
#include <vector>

#include "cihaz/modbus.hpp"

namespace cihaz {

class ByteTransport;

struct ModbusStats {
    quint64 requests = 0;
    quint64 responses = 0;     // istisna dahil, çerçevesi geçerli yanıtlar
    quint64 exceptions = 0;
    quint64 timeouts = 0;
    quint64 invalidFrames = 0; // CRC, yanlış adres/uzunluk
    quint64 retries = 0;
    quint64 failures = 0;      // tüm denemeleri tükenen istekler
    quint64 strayBytes = 0;
    double lastLatencyMs = 0;
    double maxLatencyMs = 0;
};

class ModbusMaster : public QObject {
    Q_OBJECT
public:
    enum class Framing { Rtu, Tcp };

    ModbusMaster(ByteTransport* transport, Framing framing, QObject* parent = nullptr);

    void setTimeout(int ms) { timeoutMs_ = ms; }
    void setRetries(int n) { retries_ = n; }
    void setTurnaround(int ms) { turnaroundMs_ = ms; }

    // Her intervalMs'de bir sıraya girer (önceki örnek hâlâ kuyruktaysa atlanır).
    int addPoll(const modbus::Request& req, int intervalMs);
    void clearPolls();
    // Tek seferlik istek; dönen kimlik finished()/failed() sinyallerinde gelir.
    quint64 submit(const modbus::Request& req, bool priority = true);

    bool busy() const { return active_.has_value(); }
    std::size_t queued() const { return queue_.size(); }
    const ModbusStats& stats() const { return stats_; }

signals:
    void finished(quint64 id, const cihaz::modbus::Request& req, const cihaz::modbus::Response& resp);
    void failed(quint64 id, const cihaz::modbus::Request& req, const QString& reason);

private:
    struct Job {
        quint64 id;
        modbus::Request req;
        int attemptsLeft;
        int pollIndex;  // -1: tek seferlik
    };
    struct Poll {
        modbus::Request req;
        QTimer* timer;
        bool pending = false;
    };

    void enqueue(Job job, bool front);
    void pump();
    void sendActive();
    void onBytes(const QByteArray& data);
    void onTimeout();
    void retryOrFail(const QString& reason);
    void complete(const modbus::Response& resp);
    void finishActive();

    ByteTransport* transport_;
    Framing framing_;
    int timeoutMs_ = 250;
    int retries_ = 2;
    int turnaroundMs_ = 5;

    std::deque<Job> queue_;
    std::optional<Job> active_;
    std::vector<Poll> polls_;
    QByteArray rx_;
    QTimer timeout_;
    QTimer gap_;
    QElapsedTimer sentAt_;
    quint64 nextId_ = 1;
    quint16 transactionId_ = 0;
    ModbusStats stats_;
};

}  // namespace cihaz

Q_DECLARE_METATYPE(cihaz::modbus::Request)
Q_DECLARE_METATYPE(cihaz::modbus::Response)
