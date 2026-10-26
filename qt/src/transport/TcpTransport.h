#pragma once

#include <QTcpSocket>
#include <QTimer>

#include "transport/ByteTransport.h"

namespace cihaz {

class TcpTransport : public ByteTransport {
    Q_OBJECT
public:
    TcpTransport(QString host, quint16 port, QObject* parent = nullptr);

    // Bağlantı eşzamansız kurulur; true "deneme başladı" demektir, opened() sinyali bağlandığını.
    bool open() override;
    void close() override;
    bool isOpen() const override;
    qint64 write(const QByteArray& data) override;
    QString description() const override;

private:
    QString host_;
    quint16 port_;
    QTcpSocket socket_;
    QTimer reconnect_;
    bool userClosed_ = true;
};

}  // namespace cihaz
