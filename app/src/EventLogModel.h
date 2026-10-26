#pragma once
// Olay listesi; en yeni üstte, en fazla 500 satır.

#include <QAbstractListModel>
#include <deque>

class EventLogModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Role { TimeRole = Qt::UserRole + 1, TextRole, LevelRole };
    using QAbstractListModel::QAbstractListModel;

    void add(double t, const QString& text);
    void clear();

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

private:
    struct Entry {
        double t;
        QString text;
        int level;  // 0 bilgi, 1 uyarı, 2 hata
    };
    std::deque<Entry> entries_;
    static constexpr std::size_t kMax = 500;
};
