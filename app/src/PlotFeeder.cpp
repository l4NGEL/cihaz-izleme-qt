#include "PlotFeeder.h"

#include <QtCharts/QXYSeries>
#include <algorithm>
#include <cmath>
#include <limits>

namespace {

std::shared_ptr<cihaz::LiveData>& liveData() {
    static std::shared_ptr<cihaz::LiveData> d;
    return d;
}

// Eksen sınırını 1-2-5 dizisinde "yuvarlak" bir değere genişletir
double niceStep(double span) {
    if (span <= 0) return 1;
    const double mag = std::pow(10.0, std::floor(std::log10(span)));
    const double f = span / mag;
    return (f <= 1 ? 1 : f <= 2 ? 2 : f <= 5 ? 5 : 10) * mag / 5;
}

}  // namespace

PlotFeeder::PlotFeeder(QObject* parent) : QObject(parent), timer_(this) {
    connect(&timer_, &QTimer::timeout, this, &PlotFeeder::tick);
    timer_.start(33);
}

void PlotFeeder::setLiveData(std::shared_ptr<cihaz::LiveData> live) { liveData() = std::move(live); }

void PlotFeeder::addSeries(QAbstractSeries* series, int channel) {
    auto* xy = qobject_cast<QXYSeries*>(series);
    if (!xy || channel < 0 || channel >= cihaz::ChannelCount) return;
    bindings_.push_back({xy, channel});
}

void PlotFeeder::clearSeries() { bindings_.clear(); }

void PlotFeeder::setWindowSeconds(double s) {
    if (s <= 0 || s == window_) return;
    window_ = s;
    emit windowSecondsChanged();
}

void PlotFeeder::setMaxPoints(int n) {
    if (n < 4 || n == maxPoints_) return;
    maxPoints_ = n;
    emit maxPointsChanged();
}

void PlotFeeder::setActive(bool on) {
    if (on == timer_.isActive()) return;
    if (on)
        timer_.start(33);
    else
        timer_.stop();
    emit activeChanged();
}

void PlotFeeder::tick() {
    const auto& live = liveData();
    if (!live || bindings_.empty()) return;
    const double now = live->timeSec.load(std::memory_order_relaxed);
    const double t1 = std::max(now, window_);
    const double t0 = t1 - window_;

    double lo = std::numeric_limits<double>::infinity();
    double hi = -lo;
    int total = 0;
    QList<QPointF> pts;
    for (auto& b : bindings_) {
        if (!b.series) continue;
        const auto w = live->store.window(static_cast<std::uint16_t>(b.channel), t0, t1,
                                          static_cast<std::size_t>(maxPoints_));
        pts.clear();
        pts.reserve(static_cast<qsizetype>(w.size()));
        for (const auto& p : w) {
            pts.append(QPointF(p.t, p.v));
            lo = std::min(lo, p.v);
            hi = std::max(hi, p.v);
        }
        total += static_cast<int>(w.size());
        b.series->replace(pts);
    }

    double y0 = yMin_, y1 = yMax_;
    if (total > 0) {
        if (hi - lo < 1e-9) {
            lo -= 1;
            hi += 1;
        }
        const double step = niceStep(hi - lo);
        const double wantLo = std::floor(lo / step) * step;
        const double wantHi = std::ceil(hi / step) * step;
        // Genişlemeye hemen, daralmaya yalnızca belirgin fark olunca uy (eksen titremesin)
        const bool shrink = (wantHi - wantLo) < 0.6 * (yMax_ - yMin_);
        y0 = (wantLo < yMin_ || shrink) ? wantLo : yMin_;
        y1 = (wantHi > yMax_ || shrink) ? wantHi : yMax_;
    }
    if (t0 != xMin_ || t1 != xMax_ || y0 != yMin_ || y1 != yMax_ || total != points_) {
        xMin_ = t0;
        xMax_ = t1;
        yMin_ = y0;
        yMax_ = y1;
        points_ = total;
        emit rangeChanged();
    }
}
