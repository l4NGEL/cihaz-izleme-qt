#pragma once
// Üç hattın ham verisini zaman damgalı kanal örneklerine çeviren tek nokta.
//
// Canlı oturum, tekrar oynatma ve CSV dışa aktarma aynı sınıfı kullanır; bu
// sayede kayıttan üretilen grafik ile sahada görülen grafik aynı koddan çıkar.
// Sınıf iş parçacığı güvenli değildir; her oturum kendi örneğini kullanır.

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "cihaz/device_map.hpp"
#include "cihaz/frame_codec.hpp"
#include "cihaz/log_file.hpp"
#include "cihaz/modbus.hpp"
#include "cihaz/sample_store.hpp"

namespace cihaz {

struct DecoderStats {
    ParserStats uart;
    std::uint64_t unknownMessages = 0;  // CRC'si doğru ama tanınmayan/kısa UART mesajı
    std::uint64_t modbusBlocks = 0;
    std::uint64_t canFrames = 0;
    std::uint64_t canUnknown = 0;
    std::uint64_t events = 0;
};

struct TimedEvent {
    double t;
    std::string text;
};

class StreamDecoder {
public:
    StreamDecoder();
    // Ayrıştırıcı geri çağrısı this'i yakalar; kopyalanırsa eski nesneye yazardı.
    StreamDecoder(const StreamDecoder&) = delete;
    StreamDecoder& operator=(const StreamDecoder&) = delete;

    void uart(const std::uint8_t* data, std::size_t len, double t);
    void modbusRead(modbus::Function fn, std::uint16_t address,
                    const std::vector<std::uint16_t>& regs, double t);
    void canFrame(std::uint32_t id, const can::Payload& data, std::uint8_t dlc, double t);
    void event(std::string text, double t);

    // Kayıt dosyasındaki bir kaydı türüne göre ilgili girişe yönlendirir.
    void record(const logf::Record& r);

    std::vector<Sample> takeSamples();
    std::vector<TimedEvent> takeEvents();
    bool hasPending() const { return !samples_.empty() || !events_.empty(); }

    const DecoderStats& stats() const;
    void reset();

private:
    void push(const std::vector<ChannelValue>& values, double t);

    FrameParser parser_;
    double uartT_ = 0;
    std::vector<ChannelValue> scratch_;
    std::vector<Sample> samples_;
    std::vector<TimedEvent> events_;
    mutable DecoderStats stats_;
};

}  // namespace cihaz
