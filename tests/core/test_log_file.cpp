#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <fstream>

#include "cihaz/log_file.hpp"

using namespace cihaz;
using namespace cihaz::logf;

namespace {

std::string tempPath(const char* name) {
    return (std::filesystem::temp_directory_path() / name).string();
}

}  // namespace

TEST(LogFile, YazOkuGidisDonus) {
    const auto path = tempPath("cihaz_test_1.cil");
    {
        Writer w;
        ASSERT_TRUE(w.open(path, 1'700'000'000'000'000'000ULL));
        const Bytes uart{0xA5, 0x5A, 0x00};
        w.write(1000, RecordType::UartBytes, uart.data(), uart.size());
        const auto req = modbus::Request::readInput(1, 0, 3);
        const Bytes mb = packModbusRead(req, {10, 20, 30});
        w.write(2000, RecordType::ModbusRead, mb.data(), mb.size());
        can::Payload p{1, 2, 3, 4, 5, 6, 7, 8};
        const Bytes c = packCan(0x181, 8, p);
        w.write(3000, RecordType::CanFrame, c.data(), c.size());
        w.write(4000, RecordType::Event, nullptr, 0);
        EXPECT_EQ(w.records(), 4U);
    }
    // Windows'ta açık dosya silinemez: okuyucu kapsamdan çıkınca dosya kapanır
    {
        Reader r;
        ASSERT_TRUE(r.open(path)) << r.error();
        EXPECT_EQ(r.startUnixNs(), 1'700'000'000'000'000'000ULL);

        auto rec = r.next();
        ASSERT_TRUE(rec);
        EXPECT_EQ(rec->tNs, 1000U);
        EXPECT_EQ(rec->type, RecordType::UartBytes);
        EXPECT_EQ(rec->data.size(), 3U);

        rec = r.next();
        ASSERT_TRUE(rec);
        modbus::Function fn;
        std::uint16_t addr;
        std::vector<std::uint16_t> regs;
        ASSERT_TRUE(unpackModbusRead(rec->data, fn, addr, regs));
        EXPECT_EQ(fn, modbus::Function::ReadInputRegisters);
        EXPECT_EQ(regs, (std::vector<std::uint16_t>{10, 20, 30}));

        rec = r.next();
        ASSERT_TRUE(rec);
        std::uint32_t id;
        std::uint8_t dlc;
        can::Payload p{};
        ASSERT_TRUE(unpackCan(rec->data, id, dlc, p));
        EXPECT_EQ(id, 0x181U);
        EXPECT_EQ(p[7], 8);

        rec = r.next();
        ASSERT_TRUE(rec);
        EXPECT_TRUE(rec->data.empty());
        EXPECT_FALSE(r.next());
        EXPECT_FALSE(r.truncated());
    }
    std::filesystem::remove(path);
}

TEST(LogFile, YarimKalanSonKayitBildirilir) {
    const auto path = tempPath("cihaz_test_2.cil");
    {
        Writer w;
        ASSERT_TRUE(w.open(path, 0));
        const Bytes b(100, 0x11);
        w.write(1, RecordType::UartBytes, b.data(), b.size());
        w.write(2, RecordType::UartBytes, b.data(), b.size());
    }
    // Çökme benzetimi: dosyanın son 30 baytını kes
    const auto size = std::filesystem::file_size(path);
    std::filesystem::resize_file(path, size - 30);

    {
        Reader r;
        ASSERT_TRUE(r.open(path));
        EXPECT_TRUE(r.next());
        EXPECT_FALSE(r.next());
        EXPECT_TRUE(r.truncated());
    }
    std::filesystem::remove(path);
}

TEST(LogFile, YanlisDosyaReddedilir) {
    const auto path = tempPath("cihaz_test_3.cil");
    {
        std::ofstream f(path, std::ios::binary);
        f << "bu bir kayit dosyasi degil";
    }
    {
        Reader r;
        EXPECT_FALSE(r.open(path));
        EXPECT_EQ(r.error(), "kayıt dosyası değil");
        Reader yok;
        EXPECT_FALSE(yok.open(tempPath("olmayan_dosya.cil")));
    }
    std::filesystem::remove(path);
}

TEST(LogFile, BozukIcerikAcilmaz) {
    Bytes kisa{1, 2};
    modbus::Function fn;
    std::uint16_t addr;
    std::vector<std::uint16_t> regs;
    EXPECT_FALSE(unpackModbusRead(kisa, fn, addr, regs));
    std::uint32_t id;
    std::uint8_t dlc;
    can::Payload p{};
    EXPECT_FALSE(unpackCan(kisa, id, dlc, p));
}
