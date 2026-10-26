#pragma once
// Kanal listesi (QML ListView için). Değerler SampleStore'dan 10 Hz'de okunur;
// yalnızca değişen roller için dataChanged yayınlanır.

#include <QAbstractListModel>
#include <QTimer>
#include <memory>
#include <vector>

#include "session/SessionTypes.h"

class ChannelModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Role { KeyRole = Qt::UserRole + 1, LabelRole, UnitRole, SourceRole, ValueRole, TextRole, FreshRole, IdRole };

    ChannelModel(std::shared_ptr<cihaz::LiveData> live, QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE int indexOfKey(const QString& key) const;
    void reset();

private:
    void refresh();

    struct Row {
        double value = 0;
        double t = -1;
        bool has = false;
        bool fresh = false;
    };
    std::shared_ptr<cihaz::LiveData> live_;
    std::vector<Row> rows_;
    QTimer timer_;
};
