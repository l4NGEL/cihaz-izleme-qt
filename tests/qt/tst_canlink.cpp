// CanLink, Qt'nin "virtualcan" eklentisiyle (süreç içi TCP veriyolu) her
// platformda sınanır; Linux'ta vcan0 varsa SocketCAN ile de.

#include <QCanBus>
#include <QSignalSpy>
#include <QTest>

#include "can/CanLink.h"
#include "cihaz/device_map.hpp"

using namespace cihaz;

class TestCanLink : public QObject {
    Q_OBJECT
private slots:
    void tanimAyristirma() {
        auto s = CanLink::parse(QStringLiteral("peakcan:usb0?bitrate=500000"));
        QVERIFY(s);
        QCOMPARE(s->plugin, QStringLiteral("peakcan"));
        QCOMPARE(s->interfaceName, QStringLiteral("usb0"));
        QCOMPARE(s->bitrate, 500000);
        QVERIFY(!CanLink::parse(QStringLiteral("vcan0")));
        QVERIFY(!CanLink::parse(QStringLiteral("socketcan:")));
        QVERIFY(!CanLink::parse(QStringLiteral("socketcan:can0?bitrate=abc")));
    }

    void olmayanEklenti() {
        CanLink link({QStringLiteral("yok_boyle_eklenti"), QStringLiteral("can0")});
        QSignalSpy err(&link, &CanLink::errorOccurred);
        QVERIFY(!link.open());
        QCOMPARE(err.count(), 1);
    }

    void sanalVeriyolundaGidisDonus() {
        if (!QCanBus::instance()->plugins().contains(QStringLiteral("virtualcan")))
            QSKIP("virtualcan eklentisi yok");
        CanLink a({QStringLiteral("virtualcan"), QStringLiteral("can0")});
        CanLink b({QStringLiteral("virtualcan"), QStringLiteral("can0")});
        QSignalSpy aOpen(&a, &CanLink::opened);
        QSignalSpy bOpen(&b, &CanLink::opened);
        QVERIFY(a.open());
        QVERIFY(b.open());
        QTRY_VERIFY(a.isOpen() && b.isOpen());

        QSignalSpy rx(&b, &CanLink::frameReceived);
        can::Payload d{};
        can::encode(sig::Speed, 1234, d);
        can::encode(sig::Current, -2.5, d);
        QVERIFY(a.send(canid::Status1, d, 8));
        QTRY_VERIFY_WITH_TIMEOUT(!rx.isEmpty(), 3000);

        const quint32 id = rx.first().at(0).toUInt();
        const QByteArray payload = rx.first().at(1).toByteArray();
        QCOMPARE(id, canid::Status1);
        QCOMPARE(payload.size(), 8);
        can::Payload got{};
        std::copy_n(payload.constData(), 8, reinterpret_cast<char*>(got.data()));
        std::vector<ChannelValue> out;
        QVERIFY(decodeCan(id, got, 8, out));
        QCOMPARE(out.at(0).value, 1234.0);
        QVERIFY(qAbs(out.at(1).value + 2.5) < 1e-9);
        a.close();
        b.close();
    }
};

QTEST_GUILESS_MAIN(TestCanLink)
#include "tst_canlink.moc"
