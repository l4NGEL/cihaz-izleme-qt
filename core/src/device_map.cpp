#include "cihaz/device_map.hpp"

namespace cihaz {
namespace {

const std::vector<ChannelInfo> kChannels = {
    {UartSpeed, "uart.hiz", "Hız (UART)", "rpm", Source::Uart},
    {UartCurrent, "uart.akim", "Akım (UART)", "A", Source::Uart},
    {UartBusVoltage, "uart.gerilim", "DC bara (UART)", "V", Source::Uart},
    {UartTemperature, "uart.sicaklik", "Sıcaklık (UART)", "°C", Source::Uart},
    {UartPosition, "uart.pozisyon", "Pozisyon (UART)", "°", Source::Uart},
    {MbSpeed, "mb.hiz", "Hız (Modbus)", "rpm", Source::Modbus},
    {MbCurrent, "mb.akim", "Akım (Modbus)", "A", Source::Modbus},
    {MbTemperature, "mb.sicaklik", "Sıcaklık (Modbus)", "°C", Source::Modbus},
    {MbBusVoltage, "mb.gerilim", "DC bara (Modbus)", "V", Source::Modbus},
    {MbFault, "mb.ariza", "Arıza kodu (Modbus)", "", Source::Modbus},
    {MbSetpoint, "mb.hedef", "Hedef hız (Modbus)", "rpm", Source::Modbus},
    {CanSpeed, "can.hiz", "Hız (CAN)", "rpm", Source::Can},
    {CanCurrent, "can.akim", "Akım (CAN)", "A", Source::Can},
    {CanBusVoltage, "can.gerilim", "DC bara (CAN)", "V", Source::Can},
    {CanTemperature, "can.sicaklik", "Sıcaklık (CAN)", "°C", Source::Can},
    {CanFault, "can.ariza", "Arıza kodu (CAN)", "", Source::Can},
    {CanPosition, "can.pozisyon", "Pozisyon (CAN)", "°", Source::Can},
};

}  // namespace

const std::vector<ChannelInfo>& allChannels() { return kChannels; }

const ChannelInfo& channelInfo(Channel c) { return kChannels.at(static_cast<std::size_t>(c)); }

const char* sourceName(Source s) {
    switch (s) {
    case Source::Uart: return "UART";
    case Source::Modbus: return "Modbus";
    case Source::Can: return "CAN";
    }
    return "?";
}

bool decodeTelemetry(const Frame& f, std::vector<ChannelValue>& out, std::string* eventText) {
    const std::uint8_t* p = f.payload.data();
    switch (f.msgId) {
    case msg::MotorStatus:
        if (f.payload.size() != 20) return false;
        out.push_back({UartSpeed, readF32le(p + 4)});
        out.push_back({UartCurrent, readF32le(p + 8)});
        out.push_back({UartBusVoltage, readF32le(p + 12)});
        out.push_back({UartTemperature, static_cast<std::int16_t>(readU16le(p + 16)) / 10.0});
        return true;
    case msg::Encoder: {
        if (f.payload.size() != 8) return false;
        const auto counts = static_cast<std::int32_t>(readU32le(p + 4));
        double deg = static_cast<double>(counts % static_cast<std::int32_t>(kEncoderCountsPerRev)) *
                     360.0 / kEncoderCountsPerRev;
        if (deg < 0) deg += 360.0;
        out.push_back({UartPosition, deg});
        return true;
    }
    case msg::Event:
        if (eventText) eventText->assign(f.payload.begin(), f.payload.end());
        return true;
    default:
        return false;
    }
}

void decodeModbusInputs(const std::vector<std::uint16_t>& r, std::vector<ChannelValue>& out) {
    if (r.size() < 5) return;
    out.push_back({MbSpeed, static_cast<double>(static_cast<std::int16_t>(r[0]))});
    out.push_back({MbCurrent, static_cast<std::int16_t>(r[1]) / 100.0});
    out.push_back({MbTemperature, static_cast<std::int16_t>(r[2]) / 10.0});
    out.push_back({MbBusVoltage, r[3] / 10.0});
    out.push_back({MbFault, static_cast<double>(r[4])});
}

namespace sig {
using can::ByteOrder;
const can::Signal Speed{0, 16, ByteOrder::Intel, true, 1.0, 0.0};
const can::Signal Current{16, 16, ByteOrder::Intel, true, 0.01, 0.0};
const can::Signal BusVoltage{32, 16, ByteOrder::Intel, false, 0.1, 0.0};
const can::Signal Temperature{48, 8, ByteOrder::Intel, false, 1.0, -40.0};
const can::Signal Fault{7, 8, ByteOrder::Motorola, false, 1.0, 0.0};
const can::Signal State{15, 8, ByteOrder::Motorola, false, 1.0, 0.0};
const can::Signal Position{23, 16, ByteOrder::Motorola, false, 0.01, 0.0};
}  // namespace sig

bool decodeCan(std::uint32_t id, const can::Payload& d, std::uint8_t dlc,
               std::vector<ChannelValue>& out) {
    switch (id) {
    case canid::Status1:
        if (dlc < 7) return false;
        out.push_back({CanSpeed, can::decode(sig::Speed, d)});
        out.push_back({CanCurrent, can::decode(sig::Current, d)});
        out.push_back({CanBusVoltage, can::decode(sig::BusVoltage, d)});
        out.push_back({CanTemperature, can::decode(sig::Temperature, d)});
        return true;
    case canid::Status2:
        if (dlc < 4) return false;
        out.push_back({CanFault, can::decode(sig::Fault, d)});
        out.push_back({CanPosition, can::decode(sig::Position, d)});
        return true;
    case canid::Heartbeat:
        return dlc >= 1;
    default:
        return false;
    }
}

const char* faultName(int code) {
    switch (code) {
    case 0: return "yok";
    case 1: return "aşırı sıcaklık";
    case 2: return "aşırı akım";
    case 3: return "düşük bara gerilimi";
    case 4: return "enkoder hatası";
    default: return "bilinmeyen";
    }
}

}  // namespace cihaz
