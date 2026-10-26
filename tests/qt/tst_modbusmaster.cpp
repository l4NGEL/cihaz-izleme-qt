// ModbusMaster durum makinesi: sorgu, zaman aşımı + yeniden deneme, bozuk yanıt,
// istisna, yazma önceliği, başıboş bayt ve Modbus TCP işlem kimliği.

#include <QSignalSpy>
#include <QTest>

#include "FakeTransport.h"
#include "cihaz/crc.hpp"
#include "modbus/ModbusMaster.h"

using namespace cihaz;
using modbus::Request;

namespace {

QByteArray qb(const Bytes& b) { return QByteArray(reinterpret_cast<const char*>(b.data()), static_cast<qsizetype>(b.size())); }

QByteArray rtuReply(Bytes adu) {
    putU16le(adu, crc16Modbus(adu.data(), adu.size()));
    return qb(adu);
}

// İsteğe göre doğru RTU yanıtı üreten küçük istasyon: register n = 100 + n
QByteArray goodSlave(const QByteArray& req) {
    const auto* p = reinterpret_cast<const std::uint8_t*>(req.constData());
    const std::uint8_t fc = p[1];
    if (fc == 0x03 || fc == 0x04) {
        const std::uint16_t addr = readU16be(p + 2), n = readU16be(p + 4);
        Bytes adu{p[0], fc, static_cast<std::uint8_t>(2 * n)};
        for (std::uint16_t i = 0; i < n; ++i) putU16be(adu, static_cast<std::uint16_t>(100 + addr + i));
        return rtuReply(adu);
    }
    return req;  // yazma: yankı
}

}  // namespace

class TestModbusMaster : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        qRegisterMetaType<modbus::Request>();
        qRegisterMetaType<modbus::Response>();
    }

    void tekIstekYanit() {
        FakeTransport t;
        t.responder = goodSlave;
        ModbusMaster m(&t, ModbusMaster::Framing::Rtu);
        QSignalSpy done(&m, &ModbusMaster::finished);
        const auto id = m.submit(Request::readInput(1, 0, 3));
        QVERIFY(done.wait(1000));
        QCOMPARE(done.first().at(0).toULongLong(), id);
        const auto resp = done.first().at(2).value<modbus::Response>();
        QCOMPARE(resp.registers, (std::vector<std::uint16_t>{100, 101, 102}));
        QCOMPARE(m.stats().responses, 1ULL);
        QCOMPARE(t.written.first(), qb(modbus::encodeRtu(Request::readInput(1, 0, 3))));
    }

    void parcaliYanitBirlestirilir() {
        FakeTransport t;
        ModbusMaster m(&t, ModbusMaster::Framing::Rtu);
        QSignalSpy done(&m, &ModbusMaster::finished);
        m.submit(Request::readHolding(1, 0, 2));
        const QByteArray full = goodSlave(t.written.first());
        for (char c : full) t.inject(QByteArray(1, c));  // bayt bayt
        QCOMPARE(done.count(), 1);
    }

    void zamanAsimindaYenidenDenerSonraBasarisiz() {
        FakeTransport t;  // hiç yanıt yok
        ModbusMaster m(&t, ModbusMaster::Framing::Rtu);
        m.setTimeout(30);
        m.setRetries(2);
        QSignalSpy failed(&m, &ModbusMaster::failed);
        m.submit(Request::readInput(1, 0, 1));
        QVERIFY(failed.wait(2000));
        QCOMPARE(t.written.size(), 3);  // ilk deneme + 2 yeniden
        QCOMPARE(m.stats().timeouts, 3ULL);
        QCOMPARE(m.stats().retries, 2ULL);
        QCOMPARE(m.stats().failures, 1ULL);
        QVERIFY(failed.first().at(2).toString().contains(QStringLiteral("zaman aşımı")));
    }

    void bozukYanittanSonraYenidenDenemeBasarili() {
        FakeTransport t;
        int calls = 0;
        t.responder = [&](const QByteArray& req) {
            QByteArray r = goodSlave(req);
            if (calls++ == 0) r[r.size() - 1] = static_cast<char>(r.at(r.size() - 1) ^ 0x55);  // ilk yanıtın CRC'si bozuk
            return r;
        };
        ModbusMaster m(&t, ModbusMaster::Framing::Rtu);
        QSignalSpy done(&m, &ModbusMaster::finished);
        m.submit(Request::readInput(1, 0, 2));
        QVERIFY(done.wait(1000));
        QCOMPARE(m.stats().invalidFrames, 1ULL);
        QCOMPARE(m.stats().retries, 1ULL);
        QCOMPARE(m.stats().responses, 1ULL);
    }

    void istisnaYanitiBasariSayilirAmaIsaretlenir() {
        FakeTransport t;
        t.responder = [](const QByteArray&) { return rtuReply({0x01, 0x86, 0x03}); };
        ModbusMaster m(&t, ModbusMaster::Framing::Rtu);
        QSignalSpy done(&m, &ModbusMaster::finished);
        m.submit(Request::writeSingle(1, 0, 9999));
        QVERIFY(done.wait(1000));
        const auto resp = done.first().at(2).value<modbus::Response>();
        QCOMPARE(resp.exception, modbus::Exception::IllegalDataValue);
        QCOMPARE(m.stats().exceptions, 1ULL);
        QCOMPARE(m.stats().retries, 0ULL);  // istisna yeniden denenmez
    }

    void yazmaSorgununOnuneGecer() {
        FakeTransport t;
        t.openState = false;  // kuyruk birikirken hat kapalı
        ModbusMaster m(&t, ModbusMaster::Framing::Rtu);
        m.submit(Request::readInput(1, 0, 1), false);
        m.submit(Request::readInput(1, 1, 1), false);
        m.submit(Request::writeSingle(1, 0, 1500), true);
        QCOMPARE(m.queued(), 3U);
        t.responder = goodSlave;
        t.open();  // açılınca kuyruk akar
        QTRY_COMPARE(t.written.size(), 3);
        QCOMPARE(static_cast<std::uint8_t>(t.written.at(0).at(1)), std::uint8_t{0x06});
    }

    void periyodikSorguBirikmez() {
        FakeTransport t;  // yanıt yok: her sorgu zaman aşımını bekler
        ModbusMaster m(&t, ModbusMaster::Framing::Rtu);
        m.setTimeout(100);
        m.setRetries(0);
        m.addPoll(Request::readInput(1, 0, 1), 10);  // zaman aşımından çok daha sık
        QTest::qWait(350);
        // Aynı sorgu kuyrukta birikmez: en fazla bir bekleyen + bir uçuşta
        QVERIFY(m.queued() <= 1);
        QVERIFY(t.written.size() <= 5);
        m.clearPolls();
    }

    void basibosBaytlarAtilir() {
        FakeTransport t;
        ModbusMaster m(&t, ModbusMaster::Framing::Rtu);
        t.inject(QByteArray("\x01\x04\x02", 3));  // istek yokken gelen eski yanıt parçası
        QCOMPARE(m.stats().strayBytes, 3ULL);
        t.responder = goodSlave;
        QSignalSpy done(&m, &ModbusMaster::finished);
        m.submit(Request::readInput(1, 0, 1));
        QVERIFY(done.wait(1000));
    }

    void modbusTcpIslemKimligi() {
        FakeTransport t;
        t.responder = [](const QByteArray& req) {
            const auto* p = reinterpret_cast<const std::uint8_t*>(req.constData());
            Bytes r{p[0], p[1], 0, 0, 0, 5, p[6], 0x03, 0x02, 0x00, 0x2A};
            return qb(r);
        };
        ModbusMaster m(&t, ModbusMaster::Framing::Tcp);
        QSignalSpy done(&m, &ModbusMaster::finished);
        m.submit(Request::readHolding(1, 0, 1));
        m.submit(Request::readHolding(1, 0, 1));
        QTRY_COMPARE(done.count(), 2);
        // Her istek yeni işlem kimliği taşır
        QVERIFY(t.written.at(0).left(2) != t.written.at(1).left(2));
        QCOMPARE(done.at(1).at(2).value<modbus::Response>().registers.at(0), std::uint16_t{42});
    }
};

QTEST_GUILESS_MAIN(TestModbusMaster)
#include "tst_modbusmaster.moc"
