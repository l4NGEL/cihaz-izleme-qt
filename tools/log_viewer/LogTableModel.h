#pragma once
// Kayıt dosyasındaki ham kayıtları tablo olarak sunan model (Qt Widgets MVC).
// Dosya arka planda okunur; model yalnızca hazır vektörü devralır.

#include <QAbstractTableModel>
#include <vector>

#include "cihaz/log_file.hpp"

class LogTableModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Column { Time, Type, Length, Summary, ColumnCount };
    static constexpr int TypeRole = Qt::UserRole + 1;

    using QAbstractTableModel::QAbstractTableModel;

    void setRecords(std::vector<cihaz::logf::Record> records);
    const cihaz::logf::Record& record(int row) const { return records_.at(static_cast<std::size_t>(row)); }

    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QVariant headerData(int section, Qt::Orientation o, int role) const override;

    static QString typeName(cihaz::logf::RecordType t);
    static QString summary(const cihaz::logf::Record& r);
    static QString hexDump(const cihaz::Bytes& data);

private:
    std::vector<cihaz::logf::Record> records_;
};
