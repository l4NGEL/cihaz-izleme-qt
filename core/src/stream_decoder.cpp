#include "cihaz/stream_decoder.hpp"

namespace cihaz {

StreamDecoder::StreamDecoder()
    : parser_([this](const Frame& f) {
          scratch_.clear();
          std::string text;
          if (!decodeTelemetry(f, scratch_, &text)) {
              ++stats_.unknownMessages;
              return;
          }
          push(scratch_, uartT_);
          if (!text.empty()) event("cihaz: " + text, uartT_);
      }) {}

void StreamDecoder::uart(const std::uint8_t* data, std::size_t len, double t) {
    // Bir okuma parçasındaki tüm çerçeveler parçanın geliş zamanını alır.
    // 500 Hz akışta bir parça birkaç çerçeve taşır; birkaç ms'lik bu belirsizlik
    // grafikte görünmez, cihaz zaman damgası gerekirse yükten okunabilir.
    uartT_ = t;
    parser_.feed(data, len);
}

void StreamDecoder::modbusRead(modbus::Function fn, std::uint16_t address,
                               const std::vector<std::uint16_t>& regs, double t) {
    ++stats_.modbusBlocks;
    scratch_.clear();
    if (fn == modbus::Function::ReadInputRegisters && address == reg::InputBase) {
        decodeModbusInputs(regs, scratch_);
    } else if (fn == modbus::Function::ReadHoldingRegisters && address == reg::Setpoint &&
               !regs.empty()) {
        scratch_.push_back({MbSetpoint, static_cast<double>(static_cast<std::int16_t>(regs[0]))});
    }
    push(scratch_, t);
}

void StreamDecoder::canFrame(std::uint32_t id, const can::Payload& data, std::uint8_t dlc,
                             double t) {
    ++stats_.canFrames;
    scratch_.clear();
    if (!decodeCan(id, data, dlc, scratch_)) {
        if (id != canid::Heartbeat) ++stats_.canUnknown;
        return;
    }
    push(scratch_, t);
}

void StreamDecoder::event(std::string text, double t) {
    ++stats_.events;
    events_.push_back({t, std::move(text)});
}

void StreamDecoder::record(const logf::Record& r) {
    const double t = static_cast<double>(r.tNs) / 1e9;
    switch (r.type) {
    case logf::RecordType::UartBytes:
        uart(r.data.data(), r.data.size(), t);
        break;
    case logf::RecordType::ModbusRead: {
        modbus::Function fn;
        std::uint16_t addr;
        std::vector<std::uint16_t> regs;
        if (logf::unpackModbusRead(r.data, fn, addr, regs)) modbusRead(fn, addr, regs, t);
        break;
    }
    case logf::RecordType::CanFrame: {
        std::uint32_t id;
        std::uint8_t dlc;
        can::Payload p{};
        if (logf::unpackCan(r.data, id, dlc, p)) canFrame(id, p, dlc, t);
        break;
    }
    case logf::RecordType::Event:
        event(std::string(r.data.begin(), r.data.end()), t);
        break;
    }
}

std::vector<Sample> StreamDecoder::takeSamples() {
    std::vector<Sample> out;
    out.swap(samples_);
    return out;
}

std::vector<TimedEvent> StreamDecoder::takeEvents() {
    std::vector<TimedEvent> out;
    out.swap(events_);
    return out;
}

const DecoderStats& StreamDecoder::stats() const {
    stats_.uart = parser_.stats();
    return stats_;
}

void StreamDecoder::reset() {
    parser_.reset();
    samples_.clear();
    events_.clear();
}

void StreamDecoder::push(const std::vector<ChannelValue>& values, double t) {
    for (const auto& v : values)
        samples_.push_back({static_cast<std::uint16_t>(v.channel), {t, v.value}});
}

}  // namespace cihaz
