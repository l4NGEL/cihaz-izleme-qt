#include "cihaz/csv_export.hpp"

#include <filesystem>
#include <iomanip>

#include "cihaz/stream_decoder.hpp"

namespace cihaz {
namespace {

std::string csvQuote(const std::string& s) {
    std::string q = "\"";
    for (char c : s) {
        if (c == '"') q += '"';
        q += c;
    }
    return q + "\"";
}

}  // namespace

CsvExportResult exportLogToCsv(const std::string& logPath, std::ostream& out,
                               const std::function<bool(double)>& progress) {
    CsvExportResult res;
    logf::Reader reader;
    if (!reader.open(logPath)) {
        res.error = reader.error();
        return res;
    }
    std::error_code ec;
    const auto total = std::filesystem::file_size(logPath, ec);

    StreamDecoder dec;
    out << "t_s,kaynak,kanal,deger,birim\n";
    out << std::fixed;
    std::uint64_t bytesRead = logf::kFileHeader;
    while (auto rec = reader.next()) {
        ++res.records;
        bytesRead += logf::kRecordHeader + rec->data.size();
        dec.record(*rec);
        for (const auto& s : dec.takeSamples()) {
            const auto& info = channelInfo(static_cast<Channel>(s.channel));
            out << std::setprecision(6) << s.p.t << ',' << sourceName(info.source) << ','
                << info.key << ',' << std::setprecision(4) << s.p.v << ',' << info.unit << '\n';
            ++res.rows;
        }
        for (const auto& e : dec.takeEvents()) {
            out << std::setprecision(6) << e.t << ",olay," << csvQuote(e.text) << ",,\n";
            ++res.rows;
        }
        if (progress && (res.records % 2048) == 0 && total > 0 &&
            !progress(static_cast<double>(bytesRead) / static_cast<double>(total))) {
            res.cancelled = true;
            return res;
        }
    }
    if (progress) progress(1.0);
    res.truncatedInput = reader.truncated();
    res.ok = static_cast<bool>(out);
    if (!res.ok) res.error = "CSV yazılamadı";
    return res;
}

}  // namespace cihaz
