#include "LogTableModel.h"

#include <QBrush>
#include <QColor>
#include <QStringList>

using namespace cihaz;

void LogTableModel::setRecords(std::vector<logf::Record> records) {
    beginResetModel();
    records_ = std::move(records);
    endResetModel();
}

int LogTableModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(records_.size());
}

int LogTableModel::columnCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : ColumnCount; }

QVariant LogTableModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid()) return {};
    const auto& r = records_[static_cast<std::size_t>(index.row())];
    if (role == TypeRole) return static_cast<int>(r.type);
    if (role == Qt::ForegroundRole && r.type == logf::RecordType::Event) return QBrush(QColor(0xB4, 0x53, 0x09));
    if (role == Qt::TextAlignmentRole && (index.column() == Time || index.column() == Length))
        return QVariant::fromValue(Qt::AlignRight | Qt::AlignVCenter);
    if (role != Qt::DisplayRole) return {};
    switch (index.column()) {
    case Time: return QString::number(static_cast<double>(r.tNs) / 1e9, 'f', 6);
    case Type: return typeName(r.type);
    case Length: return static_cast<int>(r.data.size());
    case Summary: return summary(r);
    default: return {};
    }
}

QVariant LogTableModel::headerData(int section, Qt::Orientation o, int role) const {
    if (o != Qt::Horizontal || role != Qt::DisplayRole) return {};
    switch (section) {
    case Time: return tr("Zaman (s)");
    case Type: return tr("Tür");
    case Length: return tr("Bayt");
    case Summary: return tr("İçerik");
    default: return {};
    }
}

QString LogTableModel::typeName(logf::RecordType t) {
    switch (t) {
    case logf::RecordType::UartBytes: return QStringLiteral("UART");
    case logf::RecordType::ModbusRead: return QStringLiteral("Modbus");
    case logf::RecordType::CanFrame: return QStringLiteral("CAN");
    case logf::RecordType::Event: return QStringLiteral("Olay");
    }
    return QStringLiteral("?");
}

QString LogTableModel::summary(const logf::Record& r) {
    switch (r.type) {
    case logf::RecordType::UartBytes: {
        const std::size_t n = std::min<std::size_t>(r.data.size(), 24);
        QString s = hexDump(Bytes(r.data.begin(), r.data.begin() + static_cast<std::ptrdiff_t>(n)));
        if (r.data.size() > n) s += QStringLiteral(" …");
        return s;
    }
    case logf::RecordType::ModbusRead: {
        modbus::Function fn;
        std::uint16_t addr;
        std::vector<std::uint16_t> regs;
        if (!logf::unpackModbusRead(r.data, fn, addr, regs)) return QStringLiteral("(bozuk)");
        QStringList vals;
        for (auto v : regs) vals << QString::number(v);
        return QStringLiteral("FC%1 @%2: %3")
            .arg(static_cast<int>(fn), 2, 10, QLatin1Char('0'))
            .arg(addr)
            .arg(vals.join(QStringLiteral(", ")));
    }
    case logf::RecordType::CanFrame: {
        std::uint32_t id;
        std::uint8_t dlc;
        can::Payload p{};
        if (!logf::unpackCan(r.data, id, dlc, p)) return QStringLiteral("(bozuk)");
        return QStringLiteral("0x%1 [%2] %3")
            .arg(id, 3, 16, QLatin1Char('0'))
            .arg(dlc)
            .arg(hexDump(Bytes(p.begin(), p.begin() + dlc)));
    }
    case logf::RecordType::Event:
        return QString::fromUtf8(reinterpret_cast<const char*>(r.data.data()), static_cast<qsizetype>(r.data.size()));
    }
    return {};
}

QString LogTableModel::hexDump(const Bytes& data) {
    QString s;
    s.reserve(static_cast<qsizetype>(data.size()) * 3);
    for (std::size_t i = 0; i < data.size(); ++i) {
        if (i) s += QLatin1Char(' ');
        s += QStringLiteral("%1").arg(data[i], 2, 16, QLatin1Char('0')).toUpper();
    }
    return s;
}
