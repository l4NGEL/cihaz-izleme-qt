// Edinim iş parçacığı: uçtan uca veri yolu, arayüz iş parçacığının donmaması,
// kayıt + tekrar oynatma ve art arda başlat/durdur.
//
// Cihaz yerine test içinde bir TCP sunucusu telemetri basar ve Modbus TCP
// isteklerini yanıtlar; böylece test her platformda dış süreç gerektirmez.

#include <QElapsedTimer>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <QTimer>

#include "cihaz/device_map.hpp"
#include "cihaz/frame_codec.hpp"
#include "session/Acquisition.h"

using namespace cihaz;

namespace {

Bytes motorStatus(std::uint8_t seq, float speed) {
    Bytes p;
    putU32le(p, 0);
    putF32le(p, speed);
    putF32le(p, 2.0f);
    putF32le(p, 48.0f);
    putU16le(p, 300);
    putU16le(p, 1);
    return encodeFrame(msg::MotorStatus, seq, p);
}

// Arayüz iş parçacığında 5 ms'lik zamanlayıcının tetiklenme aralıklarını ölçer.
// Edinim aynı iş parçacığında çalışsaydı ya da arayüze örnek başına sinyal
// bassaydı bu aralık açılırdı.
struct JitterProbe {
    QTimer timer;
    QElapsedTimer clock;
    qint64 last = -1;
    qint64 worst = 0;
    JitterProbe() {
        timer.setTimerType(Qt::PreciseTimer);
        QObject::connect(&timer, &QTimer::timeout, [this] {
            const qint64 now = clock.elapsed();
            if (last >= 0) worst = std::max(worst, now - last);
            last = now;
        });
        clock.start();
        timer.start(5);
    }
};

}  // namespace

class TestSession : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        qRegisterMetaType<SessionStats>();
        qRegisterMetaType<SessionConfig>();
    }

    // Saniyede ~10 000 çerçeve (4 kat gerçek hız) basılırken: hiçbiri kaybolmamalı,
    // örnekler ortak tampona ulaşmalı, arayüz zamanlayıcısı aksamamalı.
    void yogunAkistaArayuzDonmaz() {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));

        Acquisition acq;
        SessionStats last;
        connect(&acq, &Acquisition::statsUpdated, this, [&](const SessionStats& s) { last = s; });
        QSignalSpy started(&acq, &Acquisition::started);
        SessionConfig cfg;
        cfg.telemetry = QStringLiteral("tcp:127.0.0.1:%1").arg(server.serverPort());
        acq.start(cfg);
        QVERIFY(started.wait(2000));
        QVERIFY(started.first().at(0).toBool());
        // started.wait() olay döngüsünü çevirdiği için bağlantı çoktan kabul edilmiş olabilir
        QTRY_VERIFY(server.hasPendingConnections());
        QTcpSocket* dev = server.nextPendingConnection();

        // Göndericiyi ayrı iş parçacığına koymak yerine olay döngüsünden besle:
        // her 2 ms'de 20 çerçeve
        const int totalFrames = 15000;
        int sent = 0;
        QTimer feeder;
        connect(&feeder, &QTimer::timeout, this, [&] {
            QByteArray chunk;
            for (int i = 0; i < 20 && sent < totalFrames; ++i, ++sent) {
                const Bytes f = motorStatus(static_cast<std::uint8_t>(sent), static_cast<float>(sent % 3000));
                chunk.append(reinterpret_cast<const char*>(f.data()), static_cast<qsizetype>(f.size()));
            }
            dev->write(chunk);
            if (sent >= totalFrames) feeder.stop();
        });
        JitterProbe probe;
        feeder.start(2);

        QTRY_VERIFY_WITH_TIMEOUT(last.uartFrames == static_cast<quint64>(totalFrames), 15000);
        QCOMPARE(last.crcErrors, 0ULL);
        QCOMPARE(last.seqGaps, 0ULL);
        auto live = acq.live();
        QCOMPARE(live->store.size(UartSpeed), static_cast<std::size_t>(totalFrames));
        QCOMPARE(live->store.latest(UartSpeed)->v, static_cast<double>((totalFrames - 1) % 3000));
        // CI makinelerinde zamanlayıcı kendiliğinden 10-20 ms kayabilir; ölçüt geniş tutuldu
        qInfo() << "arayüz zamanlayıcısı en kötü aralık:" << probe.worst << "ms";
        QVERIFY2(probe.worst < 100, qPrintable(QStringLiteral("arayüz %1 ms bekledi").arg(probe.worst)));
        acq.stop();
    }

    void kayitVeTekrarOynatma() {
        QTemporaryDir dir;
        const QString log = dir.filePath(QStringLiteral("test.cil"));
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));

        {
            Acquisition acq;
            SessionStats last;
            connect(&acq, &Acquisition::statsUpdated, this, [&](const SessionStats& s) { last = s; });
            SessionConfig cfg;
            cfg.telemetry = QStringLiteral("tcp:127.0.0.1:%1").arg(server.serverPort());
            cfg.logPath = log;
            acq.start(cfg);
            QTRY_VERIFY(server.hasPendingConnections());
            QTcpSocket* dev = server.nextPendingConnection();
            // 0.5 sn boyunca 100 Hz
            for (int i = 0; i < 50; ++i) {
                const Bytes f = motorStatus(static_cast<std::uint8_t>(i), static_cast<float>(i * 10));
                dev->write(reinterpret_cast<const char*>(f.data()), static_cast<qint64>(f.size()));
                QTest::qWait(10);
            }
            QTRY_COMPARE(last.uartFrames, 50ULL);
            acq.stop();
        }

        Acquisition replay;
        SessionStats last;
        connect(&replay, &Acquisition::statsUpdated, this, [&](const SessionStats& s) { last = s; });
        QSignalSpy finished(&replay, &Acquisition::replayFinished);
        SessionConfig cfg;
        cfg.replayPath = log;
        cfg.replaySpeed = 5.0;
        QElapsedTimer t;
        t.start();
        replay.start(cfg);
        QVERIFY(finished.wait(5000));
        const qint64 ms = t.elapsed();
        QTRY_COMPARE(last.uartFrames, 50ULL);
        QCOMPARE(replay.live()->store.size(UartSpeed), 50U);
        QCOMPARE(replay.live()->store.latest(UartSpeed)->v, 490.0);
        // ~0.5 sn'lik kayıt x5 hızda ~100 ms sürmeli (yükleme payı ile)
        qInfo() << "oynatma süresi:" << ms << "ms";
        QVERIFY(ms < 1500);
        replay.stop();
    }

    void hatAcilamazsaBildirilir() {
        Acquisition acq;
        QSignalSpy started(&acq, &Acquisition::started);
        SessionConfig cfg;
        cfg.telemetry = QStringLiteral("bilinmeyen:1");
        acq.start(cfg);
        QVERIFY(started.wait(2000));
        QCOMPARE(started.first().at(0).toBool(), false);
        QVERIFY(started.first().at(1).toString().contains(QStringLiteral("telemetri")));
        acq.stop();
    }

    void artArdaBaslatDurdur() {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        Acquisition acq;
        for (int i = 0; i < 10; ++i) {
            SessionConfig cfg;
            cfg.telemetry = QStringLiteral("tcp:127.0.0.1:%1").arg(server.serverPort());
            QSignalSpy started(&acq, &Acquisition::started);
            acq.start(cfg);
            QVERIFY(started.wait(2000));
            QVERIFY(acq.running());
            acq.stop();
            QVERIFY(!acq.running());
            while (server.hasPendingConnections()) delete server.nextPendingConnection();
        }
    }

    void modbusYazmaKomutuIletilir() {
        // Modbus TCP istasyonu: yazma isteklerini yankılar, okuma isteklerine sıfır döner
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        QList<QByteArray> requests;
        connect(&server, &QTcpServer::newConnection, this, [&] {
            QTcpSocket* s = server.nextPendingConnection();
            connect(s, &QTcpSocket::readyRead, s, [s, &requests] {
                QByteArray buf = s->readAll();
                while (buf.size() >= 12) {
                    const int len = 6 + ((static_cast<uchar>(buf[4]) << 8) | static_cast<uchar>(buf[5]));
                    const QByteArray req = buf.left(len);
                    buf.remove(0, len);
                    requests.append(req);
                    const auto fc = static_cast<uchar>(req[7]);
                    if (fc == 0x06) {
                        s->write(req);
                    } else {
                        const int n = (static_cast<uchar>(req[10]) << 8) | static_cast<uchar>(req[11]);
                        QByteArray r = req.left(4);
                        r.append(char(0)).append(char(3 + 2 * n)).append(req[6]).append(char(fc)).append(char(2 * n));
                        r.append(QByteArray(2 * n, 0));
                        s->write(r);
                    }
                }
            });
        });

        Acquisition acq;
        QStringList events;
        connect(&acq, &Acquisition::eventLogged, this, [&](double, const QString& e) { events << e; });
        SessionConfig cfg;
        cfg.modbus = QStringLiteral("tcp:127.0.0.1:%1").arg(server.serverPort());
        cfg.modbusTcp = true;
        acq.start(cfg);
        QTRY_VERIFY(!requests.isEmpty());
        acq.writeSetpoint(-1500);
        QTRY_VERIFY(events.join('\n').contains(QStringLiteral("register 0 = -1500")));
        bool sawWrite = false;
        for (const auto& r : requests)
            if (static_cast<uchar>(r[7]) == 0x06 && r.mid(10, 2) == QByteArray("\xFA\x24", 2)) sawWrite = true;
        QVERIFY(sawWrite);
        acq.stop();
    }
};

QTEST_GUILESS_MAIN(TestSession)
#include "tst_session.moc"
