#include "EventLogModel.h"

namespace {

int classify(const QString& text) {
    const QString t = text.toLower();
    if (t.contains(QStringLiteral("fault")) || t.contains(QStringLiteral("başarısız")) ||
        t.contains(QStringLiteral("açılamadı")) || t.contains(QStringLiteral("istisna")))
        return 2;
    if (t.contains(QStringLiteral("zaman aşımı")) || t.contains(QStringLiteral("kapandı")) ||
        t.contains(QStringLiteral("crc")) || t.contains(QStringLiteral("hata")))
        return 1;
    return 0;
}

}  // namespace

void EventLogModel::add(double t, const QString& text) {
    beginInsertRows({}, 0, 0);
    entries_.push_front({t, text, classify(text)});
    endInsertRows();
    if (entries_.size() > kMax) {
        const int last = static_cast<int>(entries_.size()) - 1;
        beginRemoveRows({}, last, last);
        entries_.pop_back();
        endRemoveRows();
    }
}

void EventLogModel::clear() {
    beginResetModel();
    entries_.clear();
    endResetModel();
}

int EventLogModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(entries_.size());
}

QVariant EventLogModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid()) return {};
    const Entry& e = entries_[static_cast<std::size_t>(index.row())];
    switch (role) {
    case TimeRole: return QString::number(e.t, 'f', 2);
    case TextRole:
    case Qt::DisplayRole: return e.text;
    case LevelRole: return e.level;
    default: return {};
    }
}

QHash<int, QByteArray> EventLogModel::roleNames() const {
    return {{TimeRole, "time"}, {TextRole, "text"}, {LevelRole, "level"}};
}
