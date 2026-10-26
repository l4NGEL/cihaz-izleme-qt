#include "cihaz/sample_store.hpp"

#include <algorithm>
#include <stdexcept>

namespace cihaz {

SampleStore::SampleStore(std::size_t channels, std::size_t capacityPerChannel)
    : rings_(channels), capacity_(capacityPerChannel) {
    if (capacityPerChannel == 0) throw std::invalid_argument("kapasite sıfır olamaz");
    for (auto& r : rings_) r.data.resize(capacity_);
}

const Point& SampleStore::Ring::at(std::size_t i) const {
    const std::size_t cap = data.size();
    return data[(head + cap - count + i) % cap];
}

void SampleStore::push(Ring& r, Point p) {
    r.data[r.head] = p;
    r.head = (r.head + 1) % r.data.size();
    if (r.count < r.data.size()) ++r.count;
    ++total_;
}

void SampleStore::append(std::uint16_t channel, double t, double v) {
    std::lock_guard<std::mutex> lock(mutex_);
    push(rings_.at(channel), {t, v});
}

void SampleStore::appendBatch(const std::vector<Sample>& samples) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& s : samples) push(rings_.at(s.channel), s.p);
}

void SampleStore::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& r : rings_) {
        r.head = 0;
        r.count = 0;
    }
}

std::vector<Point> SampleStore::window(std::uint16_t channel, double t0, double t1,
                                       std::size_t maxPoints) const {
    std::vector<Point> pts;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const Ring& r = rings_.at(channel);
        // Zaman damgaları artan sırada; pencerenin başını ikili aramayla bul.
        std::size_t lo = 0, hi = r.count;
        while (lo < hi) {
            const std::size_t mid = (lo + hi) / 2;
            if (r.at(mid).t < t0)
                lo = mid + 1;
            else
                hi = mid;
        }
        for (std::size_t i = lo; i < r.count && r.at(i).t <= t1; ++i) pts.push_back(r.at(i));
    }
    return decimateMinMax(pts, maxPoints);
}

std::optional<Point> SampleStore::latest(std::uint16_t channel) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const Ring& r = rings_.at(channel);
    if (r.count == 0) return std::nullopt;
    return r.at(r.count - 1);
}

std::size_t SampleStore::size(std::uint16_t channel) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return rings_.at(channel).count;
}

std::uint64_t SampleStore::totalAppended() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return total_;
}

std::vector<Point> decimateMinMax(const std::vector<Point>& pts, std::size_t maxPoints) {
    if (maxPoints < 4 || pts.size() <= maxPoints) return pts;
    const std::size_t buckets = maxPoints / 2;
    const double t0 = pts.front().t;
    const double span = pts.back().t - t0;
    std::vector<Point> out;
    out.reserve(buckets * 2);

    std::size_t i = 0;
    for (std::size_t b = 0; b < buckets && i < pts.size(); ++b) {
        const double end = (b + 1 == buckets)
                               ? pts.back().t
                               : t0 + span * static_cast<double>(b + 1) / static_cast<double>(buckets);
        std::size_t first = i;
        std::size_t iMin = i, iMax = i;
        while (i < pts.size() && (pts[i].t <= end || b + 1 == buckets)) {
            if (pts[i].v < pts[iMin].v) iMin = i;
            if (pts[i].v > pts[iMax].v) iMax = i;
            ++i;
        }
        if (i == first) continue;  // boş kova (veride boşluk)
        // Zaman sırası korunsun diye önce gelen önce eklenir
        if (iMin == iMax) {
            out.push_back(pts[iMin]);
        } else {
            out.push_back(pts[std::min(iMin, iMax)]);
            out.push_back(pts[std::max(iMin, iMax)]);
        }
    }
    return out;
}

}  // namespace cihaz
