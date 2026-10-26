#pragma once
// Kayıt dosyasını (.cil) çözülmüş, uzun biçimli CSV'ye çevirir:
//   t_s,kaynak,kanal,deger,birim
// Olaylar "olay" kaynağıyla, metin "kanal" sütununda yazılır.
// Uzun dosyalarda arayüz donmasın diye arka plan iş parçacığında çalıştırılır;
// ilerleme geri çağrısı false dönerse iş iptal edilir.

#include <cstdint>
#include <functional>
#include <ostream>
#include <string>

namespace cihaz {

struct CsvExportResult {
    bool ok = false;
    bool cancelled = false;
    bool truncatedInput = false;
    std::uint64_t records = 0;
    std::uint64_t rows = 0;
    std::string error;
};

CsvExportResult exportLogToCsv(const std::string& logPath, std::ostream& out,
                               const std::function<bool(double fraction)>& progress = {});

}  // namespace cihaz
