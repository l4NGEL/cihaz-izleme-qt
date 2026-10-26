#include "MainWindow.h"

#include <QApplication>
#include <QComboBox>
#include <QDateTime>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QMetaObject>
#include <QPlainTextEdit>
#include <QPointer>
#include <QProgressDialog>
#include <QSortFilterProxyModel>
#include <QSplitter>
#include <QStatusBar>
#include <QTableView>
#include <QToolBar>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>
#include <fstream>

#include "LogTableModel.h"

using namespace cihaz;

namespace {

// Tür süzgeci: -1 hepsi, aksi halde RecordType değeri
class TypeFilterProxy : public QSortFilterProxyModel {
public:
    using QSortFilterProxyModel::QSortFilterProxyModel;
    void setType(int t) {
        type_ = t;
        invalidateFilter();
    }

protected:
    bool filterAcceptsRow(int row, const QModelIndex& parent) const override {
        if (type_ < 0) return true;
        const QModelIndex idx = sourceModel()->index(row, 0, parent);
        return sourceModel()->data(idx, LogTableModel::TypeRole).toInt() == type_;
    }

private:
    int type_ = -1;
};

LoadResult loadFile(const QString& path) {
    LoadResult res;
    logf::Reader reader;
    if (!reader.open(path.toStdString())) {
        res.error = QString::fromStdString(reader.error());
        return res;
    }
    res.startMs = static_cast<qint64>(reader.startUnixNs() / 1000000ULL);
    while (auto r = reader.next()) res.records.push_back(std::move(*r));
    res.truncated = reader.truncated();
    return res;
}

}  // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(tr("Kayıt Görüntüleyici"));
    resize(1100, 700);

    model_ = new LogTableModel(this);
    auto* proxy = new TypeFilterProxy(this);
    proxy->setSourceModel(model_);
    proxy_ = proxy;

    table_ = new QTableView;
    table_->setModel(proxy_);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->verticalHeader()->setDefaultSectionSize(22);
    table_->verticalHeader()->hide();
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->setAlternatingRowColors(true);
    table_->setFont(QFont(QStringLiteral("monospace")));

    detail_ = new QPlainTextEdit;
    detail_->setReadOnly(true);
    detail_->setFont(QFont(QStringLiteral("monospace")));

    auto* split = new QSplitter(Qt::Vertical);
    split->addWidget(table_);
    split->addWidget(detail_);
    split->setStretchFactor(0, 4);
    split->setStretchFactor(1, 1);
    setCentralWidget(split);

    typeFilter_ = new QComboBox;
    typeFilter_->addItem(tr("Tümü"), -1);
    typeFilter_->addItem(QStringLiteral("UART"), static_cast<int>(logf::RecordType::UartBytes));
    typeFilter_->addItem(QStringLiteral("Modbus"), static_cast<int>(logf::RecordType::ModbusRead));
    typeFilter_->addItem(QStringLiteral("CAN"), static_cast<int>(logf::RecordType::CanFrame));
    typeFilter_->addItem(tr("Olay"), static_cast<int>(logf::RecordType::Event));
    connect(typeFilter_, &QComboBox::currentIndexChanged, this,
            [proxy, this](int) { proxy->setType(typeFilter_->currentData().toInt()); });

    auto* tb = addToolBar(tr("Araçlar"));
    tb->setMovable(false);
    tb->addAction(tr("Aç…"), this, &MainWindow::chooseFile);
    tb->addAction(tr("CSV'ye aktar…"), this, &MainWindow::exportCsv);
    tb->addSeparator();
    tb->addWidget(new QLabel(tr(" Tür: ")));
    tb->addWidget(typeFilter_);

    auto* file = menuBar()->addMenu(tr("&Dosya"));
    file->addAction(tr("&Aç…"), QKeySequence::Open, this, &MainWindow::chooseFile);
    file->addAction(tr("&CSV'ye aktar…"), this, &MainWindow::exportCsv);
    file->addSeparator();
    file->addAction(tr("Çı&kış"), QKeySequence::Quit, this, &QWidget::close);

    status_ = new QLabel(tr("Dosya açılmadı"));
    statusBar()->addWidget(status_, 1);

    connect(table_->selectionModel(), &QItemSelectionModel::currentRowChanged, this, &MainWindow::showDetail);
    connect(&loadWatcher_, &QFutureWatcher<LoadResult>::finished, this, &MainWindow::onLoaded);
    connect(&exportWatcher_, &QFutureWatcher<CsvExportResult>::finished, this, [this] {
        progress_->close();
        const CsvExportResult r = exportWatcher_.result();
        if (r.cancelled)
            status_->setText(tr("Dışa aktarma iptal edildi"));
        else if (!r.ok)
            QMessageBox::warning(this, tr("Dışa aktarma"), QString::fromStdString(r.error));
        else
            status_->setText(tr("%1 satır CSV'ye yazıldı").arg(r.rows));
    });
}

void MainWindow::chooseFile() {
    const QString f = QFileDialog::getOpenFileName(this, tr("Kayıt dosyası"), {}, tr("Kayıt (*.cil);;Tümü (*)"));
    if (!f.isEmpty()) openFile(f);
}

void MainWindow::openFile(const QString& path) {
    path_ = path;
    status_->setText(tr("Okunuyor: %1").arg(path));
    // Yüz MB'lık kayıt arayüzü dondurmasın: dosya iş havuzunda okunur
    loadWatcher_.setFuture(QtConcurrent::run(loadFile, path));
}

void MainWindow::onLoaded() {
    LoadResult res = loadWatcher_.result();
    if (!res.error.isEmpty()) {
        QMessageBox::warning(this, tr("Kayıt açılamadı"), res.error);
        status_->setText(tr("Dosya açılamadı"));
        return;
    }
    const auto n = res.records.size();
    model_->setRecords(std::move(res.records));
    table_->resizeColumnToContents(LogTableModel::Time);
    table_->resizeColumnToContents(LogTableModel::Type);
    setWindowTitle(tr("Kayıt Görüntüleyici — %1").arg(QFileInfo(path_).fileName()));
    status_->setText(tr("%1 kayıt · başlangıç %2%3")
                         .arg(n)
                         .arg(QDateTime::fromMSecsSinceEpoch(res.startMs).toString(Qt::ISODate))
                         .arg(res.truncated ? tr(" · son kayıt yarım") : QString()));
}

void MainWindow::selectRow(int row) {
    if (row < 0 || row >= proxy_->rowCount()) return;
    table_->selectRow(row);
    table_->scrollTo(proxy_->index(row, 0));
}

void MainWindow::showDetail() {
    const QModelIndex idx = proxy_->mapToSource(table_->currentIndex());
    if (!idx.isValid()) {
        detail_->clear();
        return;
    }
    const auto& r = model_->record(idx.row());
    QString text = QStringLiteral("t = %1 s   tür = %2   %3 bayt\n\n")
                       .arg(static_cast<double>(r.tNs) / 1e9, 0, 'f', 6)
                       .arg(LogTableModel::typeName(r.type))
                       .arg(r.data.size());
    for (std::size_t off = 0; off < r.data.size(); off += 16) {
        const auto end = std::min(r.data.size(), off + 16);
        text += QStringLiteral("%1  %2\n")
                    .arg(off, 4, 16, QLatin1Char('0'))
                    .arg(LogTableModel::hexDump(Bytes(r.data.begin() + static_cast<std::ptrdiff_t>(off),
                                                      r.data.begin() + static_cast<std::ptrdiff_t>(end))));
    }
    if (r.type != logf::RecordType::UartBytes) text += QStringLiteral("\n") + LogTableModel::summary(r);
    detail_->setPlainText(text);
}

void MainWindow::exportCsv() {
    if (path_.isEmpty()) return;
    const QString out = QFileDialog::getSaveFileName(this, tr("CSV kaydet"), path_ + QStringLiteral(".csv"),
                                                     tr("CSV (*.csv)"));
    if (out.isEmpty()) return;

    cancelExport_ = std::make_shared<std::atomic<bool>>(false);
    progress_ = new QProgressDialog(tr("CSV'ye aktarılıyor…"), tr("İptal"), 0, 1000, this);
    progress_->setWindowModality(Qt::WindowModal);
    progress_->setMinimumDuration(300);
    progress_->setAttribute(Qt::WA_DeleteOnClose);
    auto cancel = cancelExport_;
    connect(progress_, &QProgressDialog::canceled, this, [cancel] { *cancel = true; });

    QPointer<QProgressDialog> dlg = progress_;
    const QString in = path_;
    exportWatcher_.setFuture(QtConcurrent::run([in, out, cancel, dlg] {
        std::ofstream f(out.toStdString(), std::ios::binary);
        if (!f) {
            CsvExportResult r;
            r.error = "CSV dosyası açılamadı";
            return r;
        }
        return exportLogToCsv(in.toStdString(), f, [cancel, dlg](double fraction) {
            // İlerleme arayüz iş parçacığına kuyrukla gönderilir; diyalog kapanmışsa atlanır
            QMetaObject::invokeMethod(qApp, [dlg, fraction] {
                if (dlg) dlg->setValue(static_cast<int>(fraction * 1000));
            }, Qt::QueuedConnection);
            return !cancel->load();
        });
    }));
}
