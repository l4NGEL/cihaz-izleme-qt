#include "transport/TransportFactory.h"

#include <QUrlQuery>

#include "transport/TcpTransport.h"
#include "transport/UdpTransport.h"

namespace cihaz {
namespace {

bool fail(QString* error, const QString& msg) {
    if (error) *error = msg;
    return false;
}

// "host:port" ya da yalnız "port"
bool splitHostPort(const QString& s, QString& host, quint16& port, bool hostOptional) {
    const qsizetype colon = s.lastIndexOf(':');
    QString portText = s;
    if (colon >= 0) {
        host = s.left(colon);
        portText = s.mid(colon + 1);
    } else if (!hostOptional) {
        return false;
    }
    bool ok = false;
    const uint p = portText.toUInt(&ok);
    if (!ok || p == 0 || p > 65535) return false;
    port = static_cast<quint16>(p);
    return !(colon >= 0 && host.isEmpty());
}

bool parseSerialOptions(const QUrlQuery& q, SerialSettings& s, QString* error) {
    for (const auto& [key, value] : q.queryItems()) {
        bool ok = true;
        if (key == QLatin1String("baud")) {
            s.baud = value.toInt(&ok);
            ok = ok && s.baud > 0;
        } else if (key == QLatin1String("parity")) {
            if (value == QLatin1String("none"))
                s.parity = QSerialPort::NoParity;
            else if (value == QLatin1String("even"))
                s.parity = QSerialPort::EvenParity;
            else if (value == QLatin1String("odd"))
                s.parity = QSerialPort::OddParity;
            else
                ok = false;
        } else if (key == QLatin1String("stop")) {
            if (value == QLatin1String("1"))
                s.stopBits = QSerialPort::OneStop;
            else if (value == QLatin1String("2"))
                s.stopBits = QSerialPort::TwoStop;
            else
                ok = false;
        } else if (key == QLatin1String("data")) {
            const int d = value.toInt(&ok);
            ok = ok && d >= 5 && d <= 8;
            if (ok) s.dataBits = static_cast<QSerialPort::DataBits>(d);
        } else if (key == QLatin1String("flow")) {
            if (value == QLatin1String("none"))
                s.flow = QSerialPort::NoFlowControl;
            else if (value == QLatin1String("rtscts"))
                s.flow = QSerialPort::HardwareControl;
            else
                ok = false;
        } else {
            return fail(error, QStringLiteral("bilinmeyen seri port seçeneği: %1").arg(key));
        }
        if (!ok) return fail(error, QStringLiteral("geçersiz %1 değeri: %2").arg(key, value));
    }
    return true;
}

}  // namespace

std::optional<TransportSpec> TransportFactory::parse(const QString& text, QString* error) {
    const QString spec = text.trimmed();
    const qsizetype colon = spec.indexOf(':');
    if (colon <= 0) {
        fail(error, QStringLiteral("tür belirtilmemiş (serial:, tcp:, udp:)"));
        return std::nullopt;
    }
    const QString scheme = spec.left(colon).toLower();
    QString rest = spec.mid(colon + 1);
    QUrlQuery query;
    const qsizetype qm = rest.indexOf('?');
    if (qm >= 0) {
        query = QUrlQuery(rest.mid(qm + 1));
        rest = rest.left(qm);
    }

    TransportSpec out;
    if (scheme == QLatin1String("serial")) {
        out.kind = TransportSpec::Kind::Serial;
        out.serial.port = rest;
        if (rest.isEmpty()) {
            fail(error, QStringLiteral("seri port adı boş"));
            return std::nullopt;
        }
        if (!parseSerialOptions(query, out.serial, error)) return std::nullopt;
        return out;
    }
    if (scheme == QLatin1String("tcp")) {
        out.kind = TransportSpec::Kind::Tcp;
        if (!splitHostPort(rest, out.host, out.port, false)) {
            fail(error, QStringLiteral("tcp için host:port gerekli"));
            return std::nullopt;
        }
        if (!query.isEmpty()) {
            fail(error, QStringLiteral("tcp seçenek almaz"));
            return std::nullopt;
        }
        return out;
    }
    if (scheme == QLatin1String("udp")) {
        out.kind = TransportSpec::Kind::Udp;
        out.host = QStringLiteral("0.0.0.0");
        if (!splitHostPort(rest, out.host, out.port, true)) {
            fail(error, QStringLiteral("udp için [adres:]port gerekli"));
            return std::nullopt;
        }
        for (const auto& [key, value] : query.queryItems()) {
            if (key != QLatin1String("remote") ||
                !splitHostPort(value, out.remoteHost, out.remotePort, false)) {
                fail(error, QStringLiteral("geçersiz udp seçeneği: %1=%2").arg(key, value));
                return std::nullopt;
            }
        }
        return out;
    }
    fail(error, QStringLiteral("bilinmeyen tür: %1").arg(scheme));
    return std::nullopt;
}

std::unique_ptr<ByteTransport> TransportFactory::create(const TransportSpec& spec) {
    switch (spec.kind) {
    case TransportSpec::Kind::Serial:
        return std::make_unique<SerialTransport>(spec.serial);
    case TransportSpec::Kind::Tcp:
        return std::make_unique<TcpTransport>(spec.host, spec.port);
    case TransportSpec::Kind::Udp: {
        auto udp = std::make_unique<UdpTransport>(QHostAddress(spec.host), spec.port);
        if (spec.remotePort) udp->setRemote(QHostAddress(spec.remoteHost), spec.remotePort);
        return udp;
    }
    }
    return nullptr;
}

std::unique_ptr<ByteTransport> TransportFactory::create(const QString& spec, QString* error) {
    const auto parsed = parse(spec, error);
    if (!parsed) return nullptr;
    return create(*parsed);
}

}  // namespace cihaz
