#include "ChannelModel.h"

#include <cmath>

using namespace cihaz;

ChannelModel::ChannelModel(std::shared_ptr<LiveData> live, QObject* parent)
    : QAbstractListModel(parent), live_(std::move(live)), rows_(allChannels().size()), timer_(this) {
    connect(&timer_, &QTimer::timeout, this, &ChannelModel::refresh);
    timer_.start(100);
}

int ChannelModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(rows_.size());
}

QVariant ChannelModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid()) return {};
    const auto i = static_cast<std::size_t>(index.row());
    const ChannelInfo& info = allChannels()[i];
    const Row& r = rows_[i];
    switch (role) {
    case KeyRole: return QString::fromUtf8(info.key);
    case LabelRole:
    case Qt::DisplayRole: return QString::fromUtf8(info.label);
    case UnitRole: return QString::fromUtf8(info.unit);
    case SourceRole: return QString::fromUtf8(sourceName(info.source));
    case ValueRole: return r.has ? QVariant(r.value) : QVariant();
    case TextRole: {
        if (!r.has) return QStringLiteral("—");
        if (info.id == MbFault || info.id == CanFault) {
            const int code = static_cast<int>(r.value);
            return QStringLiteral("%1 (%2)").arg(code).arg(QString::fromUtf8(faultName(code)));
        }
        const int decimals = std::abs(r.value) >= 100 ? 0 : (std::abs(r.value) >= 10 ? 1 : 2);
        return QString::number(r.value, 'f', decimals);
    }
    case FreshRole: return r.fresh;
    case IdRole: return static_cast<int>(info.id);
    default: return {};
    }
}

QHash<int, QByteArray> ChannelModel::roleNames() const {
    return {{KeyRole, "key"},     {LabelRole, "label"}, {UnitRole, "unit"},   {SourceRole, "source"},
            {ValueRole, "value"}, {TextRole, "text"},   {FreshRole, "fresh"}, {IdRole, "channelId"}};
}

int ChannelModel::indexOfKey(const QString& key) const {
    const auto& all = allChannels();
    for (std::size_t i = 0; i < all.size(); ++i)
        if (key == QLatin1String(all[i].key)) return static_cast<int>(i);
    return -1;
}

void ChannelModel::reset() {
    beginResetModel();
    for (auto& r : rows_) r = Row{};
    endResetModel();
}

void ChannelModel::refresh() {
    const double now = live_->timeSec.load(std::memory_order_relaxed);
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        const auto p = live_->store.latest(static_cast<std::uint16_t>(i));
        Row& r = rows_[i];
        const bool has = p.has_value();
        // Son 1 sn içinde güncellenmeyen kanal "bayat" gösterilir (hat koptu mu?)
        const bool fresh = has && now - p->t < 1.0;
        const bool valueChanged = has && (!r.has || p->v != r.value);
        if (!valueChanged && fresh == r.fresh && has == r.has) continue;
        r.has = has;
        r.fresh = fresh;
        if (has) {
            r.value = p->v;
            r.t = p->t;
        }
        const QModelIndex idx = index(static_cast<int>(i));
        emit dataChanged(idx, idx, {ValueRole, TextRole, FreshRole});
    }
}
