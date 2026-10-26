#include <gtest/gtest.h>

#include "cihaz/crc.hpp"
#include "cihaz/modbus.hpp"

using namespace cihaz;
using namespace cihaz::modbus;

namespace {

Bytes withCrc(Bytes b) {
    putU16le(b, crc16Modbus(b.data(), b.size()));
    return b;
}

ParseResult parse(const Request& r, const Bytes& b) { return parseRtuResponse(r, b.data(), b.size()); }

}  // namespace

// Bilinen RTU çerçeveleri (Modbus belgelerindeki örnekler)
TEST(Modbus, OkumaIstegiBilinenCerceve) {
    EXPECT_EQ(encodeRtu(Request::readHolding(1, 0, 10)),
              (Bytes{0x01, 0x03, 0x00, 0x00, 0x00, 0x0A, 0xC5, 0xCD}));
}

TEST(Modbus, TekYazmaIstegiBilinenCerceve) {
    EXPECT_EQ(encodeRtu(Request::writeSingle(1, 1, 3)),
              (Bytes{0x01, 0x06, 0x00, 0x01, 0x00, 0x03, 0x98, 0x0B}));
}

TEST(Modbus, CokluYazmaPdu) {
    EXPECT_EQ(encodePdu(Request::writeMultiple(1, 0x10, {0x000A, 0x0102})),
              (Bytes{0x10, 0x00, 0x10, 0x00, 0x02, 0x04, 0x00, 0x0A, 0x01, 0x02}));
}

TEST(Modbus, GecersizSayiReddedilir) {
    EXPECT_THROW(encodeRtu(Request::readHolding(1, 0, 0)), std::invalid_argument);
    EXPECT_THROW(encodeRtu(Request::readHolding(1, 0, 126)), std::invalid_argument);
    EXPECT_THROW(encodeRtu(Request::writeMultiple(1, 0, {})), std::invalid_argument);
}

TEST(Modbus, OkumaYanitiParcaParcaGelir) {
    const auto req = Request::readInput(1, 0, 2);
    const Bytes resp = withCrc({0x01, 0x04, 0x04, 0x00, 0x0A, 0xFF, 0xFE});
    for (std::size_t n = 0; n < resp.size(); ++n)
        EXPECT_EQ(parseRtuResponse(req, resp.data(), n).status, ParseStatus::Incomplete) << n;
    const auto r = parse(req, resp);
    ASSERT_EQ(r.status, ParseStatus::Complete);
    EXPECT_EQ(r.consumed, resp.size());
    EXPECT_EQ(r.response.registers, (std::vector<std::uint16_t>{0x000A, 0xFFFE}));
}

TEST(Modbus, IstisnaYaniti) {
    const auto req = Request::readHolding(1, 0x100, 1);
    const auto r = parse(req, Bytes{0x01, 0x83, 0x02, 0xC0, 0xF1});
    ASSERT_EQ(r.status, ParseStatus::Complete);
    EXPECT_TRUE(r.response.isException());
    EXPECT_EQ(r.response.exception, Exception::IllegalDataAddress);
}

TEST(Modbus, CrcHatasiGecersizdir) {
    const auto req = Request::readHolding(1, 0, 1);
    Bytes resp = withCrc({0x01, 0x03, 0x02, 0x12, 0x34});
    resp.back() ^= 0x01;
    const auto r = parse(req, resp);
    EXPECT_EQ(r.status, ParseStatus::Invalid);
    EXPECT_EQ(r.error, "CRC hatası");
}

TEST(Modbus, YanlisIstasyonVeBaytSayisi) {
    const auto req = Request::readHolding(1, 0, 2);
    EXPECT_EQ(parse(req, withCrc({0x02, 0x03, 0x04, 0, 0, 0, 0})).status, ParseStatus::Invalid);
    EXPECT_EQ(parse(req, Bytes{0x01, 0x03, 0x02}).status, ParseStatus::Invalid);
    EXPECT_EQ(parse(req, Bytes{0x01, 0x04}).status, ParseStatus::Invalid);  // yanlış fonksiyon
}

TEST(Modbus, YazmaYankisiDogrulanir) {
    const auto req = Request::writeSingle(1, 0, 1500);
    EXPECT_EQ(parse(req, encodeRtu(req)).status, ParseStatus::Complete);  // yanıt = isteğin yankısı
    EXPECT_EQ(parse(req, encodeRtu(Request::writeSingle(1, 0, 1501))).status, ParseStatus::Invalid);
}

TEST(Modbus, CokluYazmaYaniti) {
    const auto req = Request::writeMultiple(1, 0, {1, 2, 3});
    EXPECT_EQ(parse(req, withCrc({0x01, 0x10, 0x00, 0x00, 0x00, 0x03})).status, ParseStatus::Complete);
    EXPECT_EQ(parse(req, withCrc({0x01, 0x10, 0x00, 0x00, 0x00, 0x02})).status, ParseStatus::Invalid);
}

TEST(Modbus, TcpIstekVeYanit) {
    const auto req = Request::readHolding(1, 0, 1);
    EXPECT_EQ(encodeTcp(req, 0x1234),
              (Bytes{0x12, 0x34, 0x00, 0x00, 0x00, 0x06, 0x01, 0x03, 0x00, 0x00, 0x00, 0x01}));
    const Bytes resp{0x12, 0x34, 0x00, 0x00, 0x00, 0x05, 0x01, 0x03, 0x02, 0x00, 0x2A};
    auto r = parseTcpResponse(req, 0x1234, resp.data(), resp.size() - 1);
    EXPECT_EQ(r.status, ParseStatus::Incomplete);
    r = parseTcpResponse(req, 0x1234, resp.data(), resp.size());
    ASSERT_EQ(r.status, ParseStatus::Complete);
    EXPECT_EQ(r.response.registers.at(0), 42);
    EXPECT_EQ(parseTcpResponse(req, 0x1235, resp.data(), resp.size()).status, ParseStatus::Invalid);
}
