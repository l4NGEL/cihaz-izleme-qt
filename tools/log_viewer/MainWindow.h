#pragma once

#include <QFutureWatcher>
#include <QMainWindow>
#include <atomic>
#include <memory>

#include "cihaz/csv_export.hpp"
#include "cihaz/log_file.hpp"

class QComboBox;
class QLabel;
class QPlainTextEdit;
class QProgressDialog;
class QSortFilterProxyModel;
class QTableView;
class LogTableModel;

struct LoadResult {
    std::vector<cihaz::logf::Record> records;
    bool truncated = false;
    QString error;
    qint64 startMs = 0;
};

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    void openFile(const QString& path);
    void selectRow(int row);

private:
    void chooseFile();
    void exportCsv();
    void onLoaded();
    void showDetail();

    LogTableModel* model_;
    QSortFilterProxyModel* proxy_;
    QTableView* table_;
    QPlainTextEdit* detail_;
    QComboBox* typeFilter_;
    QLabel* status_;
    QString path_;

    QFutureWatcher<LoadResult> loadWatcher_;
    QFutureWatcher<cihaz::CsvExportResult> exportWatcher_;
    QProgressDialog* progress_ = nullptr;
    std::shared_ptr<std::atomic<bool>> cancelExport_;
};
