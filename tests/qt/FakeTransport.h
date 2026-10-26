#pragma once
// Testler için bellek içi taşıma: yazılanları kaydeder, isteğe göre yanıt üretir.

#include <QByteArray>
#include <QList>
#include <QTimer>
#include <functional>

#include "transport/ByteTransport.h"

class FakeTransport : public cihaz::ByteTransport {
    Q_OBJECT
public:
    // Her yazmaya karşı çağrılır; dönen baytlar (boş değilse) gecikmeyle "gelir".
    std::function<QByteArray(const QByteArray&)> responder;
    int replyDelayMs = 1;
    QList<QByteArray> written;
    bool openState = true;

    bool open() override {
        openState = true;
        emit opened();
        return true;
    }
    void close() override {
        openState = false;
        emit closed();
    }
    bool isOpen() const override { return openState; }
    qint64 write(const QByteArray& data) override {
        written.append(data);
        if (responder) {
            const QByteArray reply = responder(data);
            if (!reply.isEmpty())
                QTimer::singleShot(replyDelayMs, this, [this, reply] { emit bytesReceived(reply); });
        }
        return data.size();
    }
    QString description() const override { return QStringLiteral("sahte"); }

    // Gelen veriyi doğrudan enjekte etmek için
    void inject(const QByteArray& data) { emit bytesReceived(data); }
};
