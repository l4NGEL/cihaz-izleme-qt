#include <QApplication>
#include <QCommandLineParser>
#include <QTimer>

#include "MainWindow.h"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("kayit-goruntuleyici"));

    QCommandLineParser p;
    p.addHelpOption();
    p.addPositionalArgument(QStringLiteral("dosya"), QStringLiteral("açılacak .cil kaydı"));
    const QCommandLineOption shot("ekran-goruntusu", "pencere görüntüsünü kaydet ve çık (belgeler için)", "png");
    const QCommandLineOption row("satir", "ekran görüntüsünden önce seçilecek satır", "n", "0");
    p.addOptions({shot, row});
    p.process(app);

    MainWindow w;
    w.show();
    if (!p.positionalArguments().isEmpty()) w.openFile(p.positionalArguments().first());

    if (p.isSet(shot)) {
        const QString path = p.value(shot);
        const int r = p.value(row).toInt();
        QTimer::singleShot(2500, &app, [&w, path, r] {
            w.selectRow(r);
            QTimer::singleShot(300, &w, [&w, path] {
                w.grab().save(path);
                QApplication::quit();
            });
        });
    }
    return QApplication::exec();
}
