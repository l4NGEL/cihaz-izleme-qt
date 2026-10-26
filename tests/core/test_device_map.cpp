#include <gtest/gtest.h>

#include <set>
#include <string>

#include "cihaz/device_map.hpp"

using namespace cihaz;

TEST(DeviceMap, KanalTablosuEnumSirasinda) {
    const auto& all = allChannels();
    ASSERT_EQ(all.size(), static_cast<std::size_t>(ChannelCount));
    std::set<std::string> keys;
    for (std::size_t i = 0; i < all.size(); ++i) {
        EXPECT_EQ(static_cast<std::size_t>(all[i].id), i);
        EXPECT_TRUE(keys.insert(all[i].key).second) << "yinelenen anahtar " << all[i].key;
    }
}

TEST(DeviceMap, MotorDurumTelemetrisi) {
    Bytes p;
    putU32le(p, 123456);
    putF32le(p, 1500.5f);
    putF32le(p, 3.25f);
    putF32le(p, 48.0f);
    putU16le(p, static_cast<std::uint16_t>(-55));  // -5.5 °C
    putU16le(p, 0x0001);
    Frame f{msg::MotorStatus, 0, p};
    std::vector<ChannelValue> out;
    ASSERT_TRUE(decodeTelemetry(f, out, nullptr));
    ASSERT_EQ(out.size(), 4U);
    EXPECT_EQ(out[0].channel, UartSpeed);
    EXPECT_FLOAT_EQ(static_cast<float>(out[0].value), 1500.5f);
    EXPECT_DOUBLE_EQ(out[3].value, -5.5);
}

TEST(DeviceMap, EnkoderNegatifSayimSarar) {
    Bytes p;
    putU32le(p, 0);
    putU32le(p, static_cast<std::uint32_t>(-1024));  // -90° -> 270°
    std::vector<ChannelValue> out;
    ASSERT_TRUE(decodeTelemetry(Frame{msg::Encoder, 0, p}, out, nullptr));
    EXPECT_DOUBLE_EQ(out.at(0).value, 270.0);
}

TEST(DeviceMap, OlayMetniVeBilinmeyenMesaj) {
    std::string text;
    std::vector<ChannelValue> out;
    EXPECT_TRUE(decodeTelemetry(Frame{msg::Event, 0, Bytes{'O', 'K'}}, out, &text));
    EXPECT_EQ(text, "OK");
    EXPECT_FALSE(decodeTelemetry(Frame{0x7F, 0, {}}, out, nullptr));
    EXPECT_FALSE(decodeTelemetry(Frame{msg::MotorStatus, 0, Bytes(3)}, out, nullptr));
    EXPECT_TRUE(out.empty());
}

TEST(DeviceMap, ModbusInputBlogu) {
    std::vector<ChannelValue> out;
    decodeModbusInputs({static_cast<std::uint16_t>(-300), 1234, 653, 480, 1, 0x0102}, out);
    ASSERT_EQ(out.size(), 5U);
    EXPECT_DOUBLE_EQ(out[0].value, -300.0);
    EXPECT_DOUBLE_EQ(out[1].value, 12.34);
    EXPECT_DOUBLE_EQ(out[2].value, 65.3);
    EXPECT_DOUBLE_EQ(out[3].value, 48.0);
    EXPECT_EQ(out[4].channel, MbFault);
}

TEST(DeviceMap, CanDurumMesajlariKodlaCoz) {
    can::Payload d1{};
    can::encode(sig::Speed, -2500, d1);
    can::encode(sig::Current, 12.5, d1);
    can::encode(sig::BusVoltage, 47.9, d1);
    can::encode(sig::Temperature, 61, d1);
    std::vector<ChannelValue> out;
    ASSERT_TRUE(decodeCan(canid::Status1, d1, 8, out));
    ASSERT_EQ(out.size(), 4U);
    EXPECT_DOUBLE_EQ(out[0].value, -2500.0);
    EXPECT_NEAR(out[1].value, 12.5, 1e-9);
    EXPECT_NEAR(out[2].value, 47.9, 1e-9);
    EXPECT_DOUBLE_EQ(out[3].value, 61.0);

    can::Payload d2{};
    can::encode(sig::Fault, 2, d2);
    can::encode(sig::Position, 359.99, d2);
    out.clear();
    ASSERT_TRUE(decodeCan(canid::Status2, d2, 4, out));
    EXPECT_DOUBLE_EQ(out[0].value, 2.0);
    EXPECT_NEAR(out[1].value, 359.99, 1e-9);
    EXPECT_EQ(d2[0], 2);  // Motorola, bayt 0

    out.clear();
    EXPECT_TRUE(decodeCan(canid::Heartbeat, d2, 1, out));
    EXPECT_TRUE(out.empty());
    EXPECT_FALSE(decodeCan(canid::Status1, d1, 4, out));  // kısa DLC
    EXPECT_FALSE(decodeCan(0x123, d1, 8, out));
}

TEST(DeviceMap, ArizaAdlari) {
    EXPECT_STREQ(faultName(0), "yok");
    EXPECT_STREQ(faultName(1), "aşırı sıcaklık");
    EXPECT_STREQ(faultName(99), "bilinmeyen");
}
