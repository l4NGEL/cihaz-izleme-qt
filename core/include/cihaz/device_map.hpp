#pragma once
// İzlenen cihazın arayüz sözleşmesi: MSD-200 motor sürücüsü (kurgusal).
//
// Sürücü aynı büyüklükleri üç ayrı hattan yayınlar; sahada hangisinin bağlı
// olduğu kuruluma göre değişir:
//   UART  : 500 Hz ikili telemetri (frame_codec çerçevesi)
//   RS-485: Modbus RTU istasyonu, ana bilgisayar sorgular (holding/input register)
//   CAN   : 100 Hz durum mesajları + 1 Hz heartbeat
// Bu dosya, her hattın ham verisini ortak kanal listesine çeviren tek yerdir;
// arayüz, grafik ve log çözümleme kanallarla çalışır, protokolü bilmez.

#include <cstdint>
#include <string>
#include <vector>

#include "cihaz/can_signal.hpp"
#include "cihaz/frame_codec.hpp"

namespace cihaz {

enum class Source : std::uint8_t { Uart = 1, Modbus = 2, Can = 3 };

enum Channel : std::uint16_t {
    // UART telemetrisi
    UartSpeed = 0,
    UartCurrent,
    UartBusVoltage,
    UartTemperature,
    UartPosition,
    // Modbus input register'ları
    MbSpeed,
    MbCurrent,
    MbTemperature,
    MbBusVoltage,
    MbFault,
    MbSetpoint,  // holding register 0'ın okunan değeri
    // CAN
    CanSpeed,
    CanCurrent,
    CanBusVoltage,
    CanTemperature,
    CanFault,
    CanPosition,
    ChannelCount
};

struct ChannelInfo {
    Channel id;
    const char* key;    // makine adı (CSV başlığı, QML)
    const char* label;  // arayüz etiketi
    const char* unit;
    Source source;
};

const ChannelInfo& channelInfo(Channel c);
const std::vector<ChannelInfo>& allChannels();
const char* sourceName(Source s);

struct ChannelValue {
    Channel channel;
    double value;
};

// ---- UART telemetri mesajları
namespace msg {
constexpr std::uint8_t MotorStatus = 0x01;  // u32 cihazZamanıUs, f32 hız, f32 akım, f32 gerilim, i16 sıcaklık*10, u16 durum
constexpr std::uint8_t Encoder = 0x02;      // u32 cihazZamanıUs, i32 enkoder sayımı (4096/tur)
constexpr std::uint8_t Event = 0x10;        // ASCII metin
}  // namespace msg

constexpr double kEncoderCountsPerRev = 4096.0;

// Bilinmeyen ya da kısa mesajlarda false döner (çağıran sayar).
bool decodeTelemetry(const Frame& f, std::vector<ChannelValue>& out, std::string* eventText);

// ---- Modbus register haritası (istasyon adresi varsayılan 1)
namespace reg {
// Holding (okunur/yazılır)
constexpr std::uint16_t Setpoint = 0;     // i16, rpm
constexpr std::uint16_t Control = 1;      // bit0 etkin, bit1 arıza sıfırla
constexpr std::uint16_t RampRate = 2;     // u16, rpm/s
// Input (salt okunur), tek blok halinde okunur
constexpr std::uint16_t InputBase = 0;
constexpr std::uint16_t InputCount = 6;   // hız, akım*100, sıcaklık*10, gerilim*10, arıza, sürüm
constexpr std::uint16_t ControlEnable = 0x0001;
constexpr std::uint16_t ControlFaultReset = 0x0002;
}  // namespace reg

void decodeModbusInputs(const std::vector<std::uint16_t>& regs, std::vector<ChannelValue>& out);

// ---- CAN mesajları
namespace canid {
constexpr std::uint32_t Status1 = 0x181;    // Intel: hız, akım, gerilim, sıcaklık
constexpr std::uint32_t Status2 = 0x281;    // Motorola: arıza, durum, pozisyon
constexpr std::uint32_t Heartbeat = 0x701;  // 1 bayt NMT durumu
}  // namespace canid

namespace sig {
extern const can::Signal Speed;        // 0x181
extern const can::Signal Current;
extern const can::Signal BusVoltage;
extern const can::Signal Temperature;
extern const can::Signal Fault;        // 0x281
extern const can::Signal State;
extern const can::Signal Position;
}  // namespace sig

bool decodeCan(std::uint32_t id, const can::Payload& data, std::uint8_t dlc,
               std::vector<ChannelValue>& out);

// Arıza kodları (Modbus FAULT register'ı ve CAN Fault sinyali aynı kodu taşır)
const char* faultName(int code);

}  // namespace cihaz
