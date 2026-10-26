#pragma once
// QML ChartView serilerini SampleStore'dan besler.
//
// Her grafik kendi PlotFeeder'ını taşır. Zamanlayıcı (~30 Hz) arayüz iş
// parçacığında çalışır: pencereyi min/max seyreltilmiş olarak alır ve
// QXYSeries::replace() ile tek seferde verir (nokta nokta append() her
// noktada yeniden çizim tetikler). Eksen sınırları özellik olarak sunulur.

#include <QObject>
#include <QPointer>
#include <QTimer>
#include <memory>
#include <vector>

#include "session/SessionTypes.h"

class QXYSeries;
class QAbstractSeries;

class PlotFeeder : public QObject {
    Q_OBJECT
    Q_PROPERTY(double windowSeconds READ windowSeconds WRITE setWindowSeconds NOTIFY windowSecondsChanged)
    Q_PROPERTY(int maxPoints READ maxPoints WRITE setMaxPoints NOTIFY maxPointsChanged)
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(double xMin READ xMin NOTIFY rangeChanged)
    Q_PROPERTY(double xMax READ xMax NOTIFY rangeChanged)
    Q_PROPERTY(double yMin READ yMin NOTIFY rangeChanged)
    Q_PROPERTY(double yMax READ yMax NOTIFY rangeChanged)
    Q_PROPERTY(int pointCount READ pointCount NOTIFY rangeChanged)

public:
    explicit PlotFeeder(QObject* parent = nullptr);

    // main() bir kez çağırır; QML'de oluşturulan tüm besleyiciler aynı veriyi okur
    static void setLiveData(std::shared_ptr<cihaz::LiveData> live);

    Q_INVOKABLE void addSeries(QAbstractSeries* series, int channel);
    Q_INVOKABLE void clearSeries();

    double windowSeconds() const { return window_; }
    void setWindowSeconds(double s);
    int maxPoints() const { return maxPoints_; }
    void setMaxPoints(int n);
    bool active() const { return timer_.isActive(); }
    void setActive(bool on);
    double xMin() const { return xMin_; }
    double xMax() const { return xMax_; }
    double yMin() const { return yMin_; }
    double yMax() const { return yMax_; }
    int pointCount() const { return points_; }

signals:
    void windowSecondsChanged();
    void maxPointsChanged();
    void activeChanged();
    void rangeChanged();

private:
    void tick();

    struct Binding {
        QPointer<QXYSeries> series;
        int channel;
    };
    std::vector<Binding> bindings_;
    QTimer timer_;
    double window_ = 20.0;
    int maxPoints_ = 1200;
    double xMin_ = 0, xMax_ = 20, yMin_ = 0, yMax_ = 1;
    int points_ = 0;
};
