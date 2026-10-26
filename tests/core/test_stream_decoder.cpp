#include <gtest/gtest.h>

#include <filesystem>
#include <sstream>

#include "cihaz/csv_export.hpp"
#include "cihaz/stream_decoder.hpp"

using namespace cihaz;

namespace {

Bytes motorStatusFrame(std::uint8_t seq, float speed) {
    Bytes p;
    putU32le(p, 0);
    putF32le(p, speed);
    putF32le(p, 1.5f);
    putF32le(p, 48.0f);
    putU16le(p, 250);
    putU16le(p, 0);
    return encodeFrame(msg::MotorStatus, seq, p);
}

}  // namespace

TEST(StreamDecoder, UartParcasiOrnegeDonusur) {
    StreamDecoder d;
    const Bytes f = motorStatusFrame(0, 1200.0f);
    d.uart(f.data(), 10, 0.5);  // yarım çerçeve
    EXPECT_TRUE(d.takeSamples().empty());
    d.uart(f.data() + 10, f.size() - 10, 0.6);
    const auto s = d.takeSamples();
    ASSERT_EQ(s.size(), 4U);
    EXPECT_EQ(s[0].channel, UartSpeed);
    EXPECT_DOUBLE_EQ(s[0].p.t, 0.6);  // tamamlayan parçanın zamanı
    EXPECT_DOUBLE_EQ(s[0].p.v, 1200.0);
    EXPECT_EQ(d.stats().uart.frames, 1U);
}

TEST(StreamDecoder, BilinmeyenMesajVeOlay) {
    StreamDecoder d;
    const Bytes unk = encodeFrame(0x55, 0, Bytes{1});
    const Bytes ev = encodeFrame(msg::Event, 1, Bytes{'F', 'A', 'U', 'L', 'T'});
    d.uart(unk.data(), unk.size(), 1);
    d.uart(ev.data(), ev.size(), 2);
    EXPECT_EQ(d.stats().unknownMessages, 1U);
    const auto e = d.takeEvents();
    ASSERT_EQ(e.size(), 1U);
    EXPECT_EQ(e[0].text, "cihaz: FAULT");
}

TEST(StreamDecoder, ModbusVeCan) {
    StreamDecoder d;
    d.modbusRead(modbus::Function::ReadInputRegisters, reg::InputBase, {1000, 250, 400, 480, 0, 1}, 1.0);
    d.modbusRead(modbus::Function::ReadHoldingRegisters, reg::Setpoint, {static_cast<std::uint16_t>(-500), 1, 300}, 1.0);
    can::Payload p{};
    can::encode(sig::Speed, 990, p);
    d.canFrame(canid::Status1, p, 8, 1.1);
    d.canFrame(canid::Heartbeat, p, 1, 1.2);
    d.canFrame(0x7FF, p, 8, 1.3);
    const auto s = d.takeSamples();
    ASSERT_EQ(s.size(), 5U + 1U + 4U);
    EXPECT_EQ(s[5].channel, MbSetpoint);
    EXPECT_DOUBLE_EQ(s[5].p.v, -500.0);
    EXPECT_EQ(s[6].channel, CanSpeed);
    EXPECT_EQ(d.stats().canFrames, 3U);
    EXPECT_EQ(d.stats().canUnknown, 1U);
}

TEST(CsvExport, KayittanCsv) {
    const auto path = (std::filesystem::temp_directory_path() / "cihaz_csv.cil").string();
    {
        logf::Writer w;
        ASSERT_TRUE(w.open(path, 0));
        for (int i = 0; i < 10; ++i) {
            const Bytes f = motorStatusFrame(static_cast<std::uint8_t>(i), 100.0f * static_cast<float>(i));
            w.write(static_cast<std::uint64_t>(i) * 2'000'000, logf::RecordType::UartBytes, f.data(), f.size());
        }
        const std::string ev = "baglanti \"kesildi\"";
        w.write(30'000'000, logf::RecordType::Event,
                reinterpret_cast<const std::uint8_t*>(ev.data()), ev.size());
    }
    std::ostringstream out;
    int calls = 0;
    const auto r = exportLogToCsv(path, out, [&](double) { ++calls; return true; });
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_EQ(r.records, 11U);
    EXPECT_EQ(r.rows, 41U);
    EXPECT_GE(calls, 1);
    const std::string csv = out.str();
    EXPECT_EQ(csv.rfind("t_s,kaynak,kanal,deger,birim\n", 0), 0U);
    EXPECT_NE(csv.find("0.018000,UART,uart.hiz,900.0000,rpm"), std::string::npos);
    EXPECT_NE(csv.find("olay,\"baglanti \"\"kesildi\"\"\""), std::string::npos);
    std::filesystem::remove(path);
}

TEST(CsvExport, OlmayanDosya) {
    std::ostringstream out;
    const auto r = exportLogToCsv("yok/yok.cil", out);
    EXPECT_FALSE(r.ok);
    EXPECT_FALSE(r.error.empty());
}
