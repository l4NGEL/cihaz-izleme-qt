#include <gtest/gtest.h>

#include <atomic>
#include <thread>

#include "cihaz/sample_store.hpp"

using namespace cihaz;

TEST(SampleStore, HalkaTasincaEnEskiSilinir) {
    SampleStore s(1, 4);
    for (int i = 0; i < 6; ++i) s.append(0, i, i * 10.0);
    EXPECT_EQ(s.size(0), 4U);
    const auto w = s.window(0, -1, 100, 100);
    ASSERT_EQ(w.size(), 4U);
    EXPECT_DOUBLE_EQ(w.front().t, 2.0);
    EXPECT_DOUBLE_EQ(w.back().v, 50.0);
    EXPECT_DOUBLE_EQ(s.latest(0)->v, 50.0);
    EXPECT_EQ(s.totalAppended(), 6U);
}

TEST(SampleStore, PencereSinirlari) {
    SampleStore s(2, 100);
    for (int i = 0; i < 50; ++i) s.append(1, i * 0.1, i);
    const auto w = s.window(1, 1.0, 2.0, 1000);
    ASSERT_FALSE(w.empty());
    EXPECT_NEAR(w.front().t, 1.0, 1e-9);
    EXPECT_NEAR(w.back().t, 2.0, 1e-9);
    EXPECT_TRUE(s.window(0, 0, 10, 100).empty());
    EXPECT_FALSE(s.latest(0).has_value());
}

TEST(SampleStore, TopluEklemeVeTemizleme) {
    SampleStore s(3, 10);
    s.appendBatch({{0, {0, 1}}, {2, {0, 2}}, {2, {1, 3}}});
    EXPECT_EQ(s.size(0), 1U);
    EXPECT_EQ(s.size(2), 2U);
    s.clear();
    EXPECT_EQ(s.size(2), 0U);
    EXPECT_THROW(s.append(3, 0, 0), std::out_of_range);
}

TEST(Decimate, KisaTepeKorunur) {
    std::vector<Point> pts;
    for (int i = 0; i < 10000; ++i) pts.push_back({i * 0.001, 1.0});
    pts[4321].v = 99.0;   // tek örneklik akım tepesi
    pts[7000].v = -50.0;
    const auto d = decimateMinMax(pts, 200);
    EXPECT_LE(d.size(), 200U);
    double mx = -1e9, mn = 1e9;
    for (const auto& p : d) {
        mx = std::max(mx, p.v);
        mn = std::min(mn, p.v);
    }
    EXPECT_DOUBLE_EQ(mx, 99.0);
    EXPECT_DOUBLE_EQ(mn, -50.0);
    for (std::size_t i = 1; i < d.size(); ++i) EXPECT_LE(d[i - 1].t, d[i].t);
    EXPECT_DOUBLE_EQ(d.front().t, 0.0);
}

TEST(Decimate, AzNoktaOldugunDokunulmaz) {
    const std::vector<Point> pts{{0, 1}, {1, 2}, {2, 3}};
    EXPECT_EQ(decimateMinMax(pts, 100).size(), 3U);
}

// Yazıcı ve okuyucu iş parçacıkları aynı anda çalışır; TSan altında da koşar.
TEST(SampleStore, EsZamanliYazmaOkuma) {
    SampleStore s(2, 5000);
    std::atomic<bool> done{false};
    std::thread writer([&] {
        std::vector<Sample> batch;
        for (int i = 0; i < 20000; ++i) {
            batch.push_back({static_cast<std::uint16_t>(i % 2), {i * 0.001, static_cast<double>(i)}});
            if (batch.size() == 50) {
                s.appendBatch(batch);
                batch.clear();
            }
        }
        s.appendBatch(batch);
        done = true;
    });
    std::size_t reads = 0;
    while (!done) {
        const auto w = s.window(0, 0, 1e9, 300);
        for (std::size_t i = 1; i < w.size(); ++i) ASSERT_LE(w[i - 1].t, w[i].t);
        ++reads;
    }
    writer.join();
    EXPECT_EQ(s.totalAppended(), 20000U);
    EXPECT_EQ(s.size(0), 5000U);
    EXPECT_GT(reads, 0U);
}
