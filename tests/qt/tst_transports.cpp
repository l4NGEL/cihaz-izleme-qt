// Taşıma fabrikası ayrıştırması ve gerçek soket/pty üzerinde gidiş-dönüş.

#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <QUdpSocket>

#include "transport/SerialTransport.h"
#include "transport/TcpTransport.h"
#include "transport/TransportFactory.h"
#include "transport/UdpTransport.h"

#ifdef Q_OS_LINUX
#include <fcntl.h>
#include <pty.h>
#include <termios.h>
#include <unistd.h>
#endif

using namespace cihaz;

class TestTransports : public QObject {
    Q_OBJECT
private slots:
    void fabrikaAyristirma_data() {
        QTest::addColumn<QString>("spec");
        QTest::addColumn<bool>("valid");
        QTest::addColumn<int>("kind");
        QTest::newRow("seri-win") << "serial:COM3?baud=115200" << true << 0;
        QTest::newRow("seri-linux") << "serial:/dev/ttyUSB0?baud=19200&parity=even&stop=1&data=8" << true << 0;
        QTest::newRow("tcp") << "tcp:192.168.1.50:5020" << true << 1;
        QTest::newRow("udp-port") << "udp:14550" << true << 2;
        QTest::newRow("udp-uzak") << "udp:0.0.0.0:14550?remote=10.0.0.2:14551" << true << 2;
        QTest::newRow("tur-yok") << "COM3" << false << -1;
        QTest::newRow("bilinmeyen") << "usb:1" << false << -1;
        QTest::newRow("tcp-port-yok") << "tcp:localhost" << false << -1;
        QTest::newRow("tcp-port-buyuk") << "tcp:host:70000" << false << -1;
        QTest::newRow("seri-bos") << "serial:" << false << -1;
        QTest::newRow("seri-parite") << "serial:COM1?parity=mark" << false << -1;
        QTest::newRow("seri-secenek") << "serial:COM1?hiz=9600" << false << -1;
        QTest::newRow("udp-secenek") << "udp:1?foo=bar" << false << -1;
    }
    void fabrikaAyristirma() {
        QFETCH(QString, spec);
        QFETCH(bool, valid);
        QFETCH(int, kind);
        QString error;
        const auto parsed = TransportFactory::parse(spec, &error);
        QCOMPARE(parsed.has_value(), valid);
        if (valid)
            QCOMPARE(static_cast<int>(parsed->kind), kind);
        else
            QVERIFY(!error.isEmpty());
    }

    void seriSecenekleri() {
        const auto s = TransportFactory::parse(QStringLiteral("serial:COM7?baud=9600&parity=odd&stop=2&data=7&flow=rtscts"));
        QVERIFY(s);
        QCOMPARE(s->serial.port, QStringLiteral("COM7"));
        QCOMPARE(s->serial.baud, 9600);
        QCOMPARE(s->serial.parity, QSerialPort::OddParity);
        QCOMPARE(s->serial.stopBits, QSerialPort::TwoStop);
        QCOMPARE(s->serial.dataBits, QSerialPort::Data7);
        QCOMPARE(s->serial.flow, QSerialPort::HardwareControl);
    }

    void tcpGidisDonusVeYenidenBaglanma() {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        const quint16 port = server.serverPort();
        TcpTransport t(QStringLiteral("127.0.0.1"), port);
        t.setAutoReconnect(true, 50);
        QSignalSpy opened(&t, &ByteTransport::opened);
        QSignalSpy closed(&t, &ByteTransport::closed);
        QSignalSpy rx(&t, &ByteTransport::bytesReceived);
        t.open();
        QTRY_VERIFY(server.hasPendingConnections());
        QTcpSocket* peer = server.nextPendingConnection();
        QTRY_COMPARE(opened.count(), 1);

        peer->write("merhaba");
        QTRY_VERIFY(!rx.isEmpty());
        QCOMPARE(rx.first().first().toByteArray(), QByteArray("merhaba"));
        t.write("cihaz");
        QTRY_VERIFY(peer->bytesAvailable() >= 5);
        QCOMPARE(peer->readAll(), QByteArray("cihaz"));

        // Karşı taraf kapatır: taşıma kendiliğinden yeniden bağlanmalı
        peer->disconnectFromHost();
        QTRY_COMPARE(closed.count(), 1);
        QTRY_VERIFY(server.hasPendingConnections());
        QTRY_COMPARE(opened.count(), 2);
        t.close();
    }

    void udpGidisDonus() {
        UdpTransport t(QHostAddress::LocalHost, 0);
        QVERIFY(t.open());
        QVERIFY(t.localPort() != 0);
        QUdpSocket peer;
        QVERIFY(peer.bind(QHostAddress::LocalHost, 0));
        QSignalSpy rx(&t, &ByteTransport::bytesReceived);
        peer.writeDatagram("ping", QHostAddress::LocalHost, t.localPort());
        QTRY_COMPARE(rx.count(), 1);
        // Uzak adres verilmediği için yanıt son gönderene gider
        QCOMPARE(t.write("pong"), 4);
        QTRY_VERIFY(peer.hasPendingDatagrams());
        QByteArray buf(16, 0);
        const auto n = peer.readDatagram(buf.data(), buf.size());
        QCOMPARE(buf.left(n), QByteArray("pong"));
    }

    void olmayanSeriPortHataVerir() {
        SerialTransport t({QStringLiteral("/olmayan/port_xyz")});
        QSignalSpy err(&t, &ByteTransport::errorOccurred);
        QVERIFY(!t.open());
        QCOMPARE(err.count(), 1);
    }

    // Linux'ta sözde terminal çifti gerçek bir seri port gibi davranır:
    // QSerialPort termios ayarlarıyla açar, karşı uçtan bayt gelir.
    void seriPortPtyUzerinden() {
#ifndef Q_OS_LINUX
        QSKIP("pty yalnızca Linux'ta");
#else
        int master = -1, slave = -1;
        char name[128] = {};
        QVERIFY(openpty(&master, &slave, name, nullptr, nullptr) == 0);
        ::fcntl(master, F_SETFL, ::fcntl(master, F_GETFL) | O_NONBLOCK);
        SerialTransport t({QString::fromLatin1(name), 115200});
        QSignalSpy rx(&t, &ByteTransport::bytesReceived);
        QVERIFY(t.open());
        const QByteArray payload("\xA5\x5A\x01\x02\x03", 5);
        QCOMPARE(::write(master, payload.constData(), static_cast<size_t>(payload.size())), payload.size());
        QTRY_VERIFY(!rx.isEmpty());
        QByteArray got;
        for (const auto& args : rx) got += args.first().toByteArray();
        QCOMPARE(got, payload);
        QCOMPARE(t.write("ok"), 2);
        // QTRY_* ifadeyi birden çok kez değerlendirir; read() yan etkili olduğu için elle döngü
        char buf[8] = {};
        ssize_t n = -1;
        for (int i = 0; i < 100 && n <= 0; ++i) {
            QTest::qWait(10);
            n = ::read(master, buf, sizeof buf);
        }
        QCOMPARE(n, ssize_t{2});
        QCOMPARE(QByteArray(buf, 2), QByteArray("ok"));
        t.close();
        ::close(slave);
        ::close(master);
#endif
    }
};

QTEST_GUILESS_MAIN(TestTransports)
#include "tst_transports.moc"
