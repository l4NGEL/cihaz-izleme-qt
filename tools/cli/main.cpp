// cihaz-cli: arayüzsüz edinim, kayıt, tekrar oynatma ve CSV dışa aktarma.
//
// Sahada ekransız bir kutuda (gömülü Linux) kayıt almak ve uçtan uca testleri
// sürmek için. Arayüzün kullandığı Acquisition/DeviceSession yolunun aynısını
// kullanır; yani bu araçla geçen bir test, arayüzün veri yolunu da sınar.
//
//   cihaz-cli --telemetri serial:/dev/ttyUSB0?baud=115200 --sure 60 --kayit saha.cil
//   cihaz-cli --modbus tcp:192.168.1.50:502 --modbus-tcp --hedef 1500 --sure 5 --json
//   cihaz-cli --oynat saha.cil --hiz 10 --json
//   cihaz-cli --csv saha.cil --cikti saha.csv

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <QTimer>
#include <fstream>

#include "cihaz/csv_export.hpp"
#include "session/Acquisition.h"

using namespace cihaz;

namespace {

QJsonObject statsJson(const SessionStats& s) {
    return QJsonObject{
        {"sure_s", s.sessionTime},
        {"uart_bayt", static_cast<double>(s.uartBytes)},
        {"uart_cerceve", static_cast<double>(s.uartFrames)},
        {"crc_hata", static_cast<double>(s.crcErrors)},
        {"uzunluk_hata", static_cast<double>(s.lengthErrors)},
        {"atilan_bayt", static_cast<double>(s.droppedBytes)},
        {"sira_atlama", static_cast<double>(s.seqGaps)},
        {"mb_istek", static_cast<double>(s.mbRequests)},
        {"mb_yanit", static_cast<double>(s.mbResponses)},
        {"mb_zaman_asimi", static_cast<double>(s.mbTimeouts)},
        {"mb_gecersiz", static_cast<double>(s.mbInvalid)},
        {"mb_istisna", static_cast<double>(s.mbExceptions)},
        {"mb_yeniden_deneme", static_cast<double>(s.mbRetries)},
        {"mb_basarisiz", static_cast<double>(s.mbFailures)},
        {"mb_gecikme_ms", s.mbLatencyMs},
        {"mb_max_gecikme_ms", s.mbMaxLatencyMs},
        {"can_cerceve", static_cast<double>(s.canFrames)},
        {"ornek", static_cast<double>(s.samples)},
        {"kayit_sayisi", static_cast<double>(s.logRecords)},
        {"kayit_bayt", static_cast<double>(s.logBytes)},
    };
}

int exportCsv(const QString& logPath, const QString& outPath) {
    std::ofstream out(outPath.toStdString(), std::ios::binary);
    if (!out) {
        QTextStream(stderr) << "CSV dosyası açılamadı: " << outPath << '\n';
        return 2;
    }
    const auto r = exportLogToCsv(logPath.toStdString(), out);
    if (!r.ok) {
        QTextStream(stderr) << "dışa aktarma başarısız: " << QString::fromStdString(r.error) << '\n';
        return 1;
    }
    QTextStream(stdout) << r.records << " kayıt, " << r.rows << " satır yazıldı"
                        << (r.truncatedInput ? " (kayıt sonu yarım)" : "") << '\n';
    return 0;
}

}  // namespace

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("cihaz-cli"));

    QCommandLineParser p;
    p.setApplicationDescription(QStringLiteral("MSD-200 izleme: arayüzsüz edinim ve kayıt aracı"));
    p.addHelpOption();
    const QCommandLineOption tel({"t", "telemetri"}, "UART telemetri hattı", "tanım");
    const QCommandLineOption mb({"m", "modbus"}, "Modbus hattı", "tanım");
    const QCommandLineOption mbTcp("modbus-tcp", "Modbus TCP çerçeveleme (varsayılan RTU)");
    const QCommandLineOption unit("birim", "Modbus istasyon adresi", "n", "1");
    const QCommandLineOption poll("sorgu-ms", "Modbus sorgu aralığı", "ms", "100");
    const QCommandLineOption canOpt({"c", "can"}, "CAN arayüzü (eklenti:arayüz)", "tanım");
    const QCommandLineOption dur({"s", "sure"}, "saniye sonra dur", "sn", "5");
    const QCommandLineOption rec({"k", "kayit"}, "ham veriyi .cil dosyasına kaydet", "dosya");
    const QCommandLineOption target("hedef", "başta sürücüyü etkinleştir ve hedef hızı yaz", "rpm");
    const QCommandLineOption replay("oynat", "canlı hat yerine kaydı oynat", "dosya");
    const QCommandLineOption speed("hiz", "oynatma hızı çarpanı", "x", "1");
    const QCommandLineOption csv("csv", "kaydı CSV'ye çevir ve çık", "dosya");
    const QCommandLineOption csvOut("cikti", "CSV çıktı yolu", "dosya");
    const QCommandLineOption json("json", "bitişte sayaçları JSON yaz");
    const QCommandLineOption quiet({"q", "sessiz"}, "olayları yazma");
    p.addOptions({tel, mb, mbTcp, unit, poll, canOpt, dur, rec, target, replay, speed, csv, csvOut, json, quiet});
    p.process(app);

    if (p.isSet(csv)) {
        const QString outPath = p.isSet(csvOut) ? p.value(csvOut) : p.value(csv) + QStringLiteral(".csv");
        return exportCsv(p.value(csv), outPath);
    }

    SessionConfig cfg;
    cfg.telemetry = p.value(tel);
    cfg.modbus = p.value(mb);
    cfg.modbusTcp = p.isSet(mbTcp);
    cfg.modbusUnit = p.value(unit).toInt();
    cfg.pollMs = p.value(poll).toInt();
    cfg.can = p.value(canOpt);
    cfg.logPath = p.value(rec);
    cfg.replayPath = p.value(replay);
    cfg.replaySpeed = p.value(speed).toDouble();

    Acquisition acq;
    SessionStats last;
    QJsonArray events;
    int exitCode = 0;
    const bool printEvents = !p.isSet(quiet);

    QObject::connect(&acq, &Acquisition::statsUpdated, [&](const SessionStats& s) { last = s; });
    QObject::connect(&acq, &Acquisition::eventLogged, [&](double t, const QString& text) {
        events.append(QJsonObject{{"t", t}, {"metin", text}});
        if (printEvents) QTextStream(stderr) << QString::number(t, 'f', 3) << "  " << text << '\n';
    });
    QObject::connect(&acq, &Acquisition::started, [&](bool ok, const QString& error) {
        if (!ok) {
            QTextStream(stderr) << "başlatılamadı: " << error << '\n';
            exitCode = 2;
            app.quit();
            return;
        }
        if (p.isSet(target)) {
            // Hat açılıp ilk sorgular dönsün diye kısa bekle
            QTimer::singleShot(300, &acq, [&] {
                acq.setEnabled(true);
                acq.writeSetpoint(p.value(target).toInt());
            });
        }
    });
    QObject::connect(&acq, &Acquisition::replayFinished, &app, &QCoreApplication::quit);

    const int ms = static_cast<int>(p.value(dur).toDouble() * 1000);
    if (cfg.replayPath.isEmpty() || p.isSet(dur)) QTimer::singleShot(ms, &app, &QCoreApplication::quit);
    acq.start(cfg);
    app.exec();
    acq.stop();  // son sayaçlar stop() içinde yayınlanır, kuyrukta bekler
    QCoreApplication::processEvents();

    if (p.isSet(json)) {
        QJsonObject root = statsJson(last);
        QJsonObject latest;
        auto live = acq.live();
        for (const auto& ch : allChannels()) {
            if (auto pt = live->store.latest(static_cast<std::uint16_t>(ch.id))) latest[ch.key] = pt->v;
        }
        root["son_deger"] = latest;
        root["olay_sayisi"] = events.size();
        root["olaylar"] = events;
        QTextStream(stdout) << QJsonDocument(root).toJson(QJsonDocument::Indented);
    }
    return exitCode;
}
