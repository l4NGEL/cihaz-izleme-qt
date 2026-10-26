#include <QApplication>
#include <QCommandLineParser>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>
#include <QUrl>

#include "AppController.h"
#include "PlotFeeder.h"

int main(int argc, char* argv[]) {
    // QtCharts QML, Graphics View üzerine kurulu olduğu için QApplication ister
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("cihaz-izleme"));
    QApplication::setOrganizationName(QStringLiteral("cihaz-izleme"));
    QQuickStyle::setStyle(QStringLiteral("Fusion"));

    QCommandLineParser p;
    p.addHelpOption();
    const QCommandLineOption autoConnect("baglan", "açılışta kayıtlı tanımlarla bağlan");
    const QCommandLineOption replay("oynat", "açılışta kaydı oynat", "dosya");
    const QCommandLineOption shot("ekran-goruntusu", "pencere görüntüsünü kaydet ve çık (belgeler için)", "png");
    const QCommandLineOption shotDelay("bekle", "ekran görüntüsünden önce bekleme", "ms", "6000");
    p.addOptions({autoConnect, replay, shot, shotDelay});
    p.process(app);

    AppController controller;
    PlotFeeder::setLiveData(controller.live());
    qmlRegisterSingletonInstance("Cihaz", 1, 0, "App", &controller);
    qmlRegisterType<PlotFeeder>("Cihaz", 1, 0, "PlotFeeder");

    QQmlApplicationEngine engine;
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
    if (engine.rootObjects().isEmpty()) return 1;

    if (p.isSet(replay))
        controller.startReplay(QUrl::fromLocalFile(p.value(replay)), 1.0);
    else if (p.isSet(autoConnect))
        controller.connectDevice();

    if (p.isSet(shot)) {
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        const QString path = p.value(shot);
        QTimer::singleShot(p.value(shotDelay).toInt(), &app, [window, path] {
            window->grabWindow().save(path);
            QCoreApplication::quit();
        });
    }
    return QApplication::exec();
}
