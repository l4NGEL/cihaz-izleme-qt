#pragma once
// Modbus istemci (master) tarafı kodlama/çözme: RTU (RS-485) ve TCP (MBAP).
//
// RTU'da çerçeve sınırı aslında 3.5 karakterlik sessizlikle belirlenir; masaüstü
// işletim sisteminde USB-seri dönüştürücü gecikmeleri yüzünden bu zamanlamaya
// güvenilemez. Bu yüzden ayrıştırıcı, gönderilen isteğe bakarak beklenen yanıt
// uzunluğunu hesaplar ve çerçeveyi uzunluk + CRC ile doğrular. Zaman aşımı
// üst katmanın (ModbusMaster) işidir.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "cihaz/bytes.hpp"

namespace cihaz::modbus {

enum class Function : std::uint8_t {
    ReadHoldingRegisters = 0x03,
    ReadInputRegisters = 0x04,
    WriteSingleRegister = 0x06,
    WriteMultipleRegisters = 0x10,
};

// Standart istisna kodları (yanıtta fonksiyon kodunun 7. biti set edilir)
enum class Exception : std::uint8_t {
    None = 0x00,
    IllegalFunction = 0x01,
    IllegalDataAddress = 0x02,
    IllegalDataValue = 0x03,
    ServerDeviceFailure = 0x04,
};

constexpr std::uint16_t kMaxReadCount = 125;
constexpr std::uint16_t kMaxWriteCount = 123;

struct Request {
    std::uint8_t unit = 1;
    Function function = Function::ReadHoldingRegisters;
    std::uint16_t address = 0;
    std::uint16_t count = 1;                // okuma için register sayısı
    std::vector<std::uint16_t> values;      // yazma için değerler

    static Request readHolding(std::uint8_t unit, std::uint16_t addr, std::uint16_t count);
    static Request readInput(std::uint8_t unit, std::uint16_t addr, std::uint16_t count);
    static Request writeSingle(std::uint8_t unit, std::uint16_t addr, std::uint16_t value);
    static Request writeMultiple(std::uint8_t unit, std::uint16_t addr,
                                 std::vector<std::uint16_t> values);
};

struct Response {
    Exception exception = Exception::None;
    std::vector<std::uint16_t> registers;   // okuma yanıtı
    bool isException() const { return exception != Exception::None; }
};

Bytes encodePdu(const Request& req);
Bytes encodeRtu(const Request& req);
Bytes encodeTcp(const Request& req, std::uint16_t transactionId);

enum class ParseStatus { Incomplete, Complete, Invalid };

struct ParseResult {
    ParseStatus status = ParseStatus::Incomplete;
    Response response;
    std::size_t consumed = 0;   // Complete/Invalid: atılması gereken bayt sayısı
    std::string error;          // Invalid: insan okunur sebep
};

// data, isteğin gönderilmesinden sonra biriken baytlardır.
ParseResult parseRtuResponse(const Request& req, const std::uint8_t* data, std::size_t len);
ParseResult parseTcpResponse(const Request& req, std::uint16_t transactionId,
                             const std::uint8_t* data, std::size_t len);

const char* exceptionName(Exception e);

}  // namespace cihaz::modbus
