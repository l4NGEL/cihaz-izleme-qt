#pragma once
// Edinim oturumu ile arayüz arasında taşınan değer tipleri.
// İş parçacıkları arası sinyallerde kopyalanırlar; QMetaType kaydı gerekir.

#include <QMetaType>
#include <QString>
#include <atomic>
#include <memory>

#include "cihaz/device_map.hpp"
#include "cihaz/sample_store.hpp"

namespace cihaz {

struct SessionConfig {
    QString telemetry;          // taşıma tanımı; boş = UART telemetrisi yok
    QString modbus;             // taşıma tanımı; boş = Modbus yok
    bool modbusTcp = false;     // true: Modbus TCP (MBAP), false: RTU çerçeveleme
    int modbusUnit = 1;
    int pollMs = 100;
    int modbusTimeoutMs = 250;
    QString can;                // "socketcan:vcan0", "virtualcan:can0"; boş = CAN yok
    QString logPath;            // boş değilse oturum başında kayda başlanır
    QString replayPath;         // boş değilse canlı hatlar yerine bu kayıt oynatılır
    double replaySpeed = 1.0;
    bool autoReconnect = true;
};

struct SessionStats {
    double sessionTime = 0;
    bool replay = false;
    double replayProgress = 0;

    bool telemetryOpen = false;
    bool modbusOpen = false;
    bool canOpen = false;
    bool logging = false;

    quint64 uartBytes = 0;
    quint64 uartFrames = 0;
    quint64 crcErrors = 0;
    quint64 lengthErrors = 0;
    quint64 droppedBytes = 0;
    quint64 seqGaps = 0;
    quint64 unknownMessages = 0;

    quint64 mbRequests = 0;
    quint64 mbResponses = 0;
    quint64 mbTimeouts = 0;
    quint64 mbInvalid = 0;
    quint64 mbExceptions = 0;
    quint64 mbFailures = 0;
    quint64 mbRetries = 0;
    double mbLatencyMs = 0;
    double mbMaxLatencyMs = 0;

    quint64 canFrames = 0;
    quint64 canErrorFrames = 0;

    quint64 samples = 0;
    quint64 logRecords = 0;
    quint64 logBytes = 0;
};

// Edinim iş parçacığının yazdığı, arayüzün okuduğu ortak durum.
// SampleStore kendi kilidini taşır; oturum saati atomik.
struct LiveData {
    LiveData() : store(ChannelCount, 120000) {}  // kanal başına 4 dk @ 500 Hz
    SampleStore store;
    std::atomic<double> timeSec{0.0};
};

}  // namespace cihaz

Q_DECLARE_METATYPE(cihaz::SessionConfig)
Q_DECLARE_METATYPE(cihaz::SessionStats)
