#pragma once
// Kanal başına sabit kapasiteli halka tampon; edinim iş parçacığı yazar,
// arayüz iş parçacığı grafik için okur.
//
// Grafik saniyede ~30 kez okunur ama bir kanalda 30 saniyelik pencerede
// 15 000 nokta olabilir. window(), noktaları ekran genişliği kadar zaman
// kovasına böler ve her kovanın en küçük ve en büyük değerini döndürür
// (min/max seyreltme). Böylece kısa süreli bir akım tepesi, düz örnekleme
// yapılsaydı kaybolacakken grafikte görünür kalır.
//
// Tek bir kilit kullanılır: yazıcı örnekleri toplu ekler (appendBatch), okuyucu
// kopyayı kilidin içinde seyreltilmiş halde alır; kilit süresi kısa kalır.

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <vector>

namespace cihaz {

struct Point {
    double t;  // oturum başından beri saniye
    double v;
};

struct Sample {
    std::uint16_t channel;
    Point p;
};

class SampleStore {
public:
    SampleStore(std::size_t channels, std::size_t capacityPerChannel);

    void append(std::uint16_t channel, double t, double v);
    void appendBatch(const std::vector<Sample>& samples);
    void clear();

    // [t0, t1] aralığındaki noktalar; maxPoints'ten fazlaysa min/max seyreltilmiş.
    std::vector<Point> window(std::uint16_t channel, double t0, double t1,
                              std::size_t maxPoints) const;
    std::optional<Point> latest(std::uint16_t channel) const;
    std::size_t size(std::uint16_t channel) const;
    std::uint64_t totalAppended() const;
    std::size_t channelCount() const { return rings_.size(); }

private:
    struct Ring {
        std::vector<Point> data;
        std::size_t head = 0;  // bir sonraki yazma konumu
        std::size_t count = 0;
        const Point& at(std::size_t i) const;  // 0 = en eski
    };
    void push(Ring& r, Point p);

    mutable std::mutex mutex_;
    std::vector<Ring> rings_;
    std::size_t capacity_;
    std::uint64_t total_ = 0;
};

// Tek başına test edilebilsin diye ayrı: sıralı noktaları min/max ile seyreltir.
std::vector<Point> decimateMinMax(const std::vector<Point>& pts, std::size_t maxPoints);

}  // namespace cihaz
