#pragma once

#include <QString>
#include <QRectF>
#include <QVector>
#include <QWidget>

#include <limits>
#include <utility>

class QFontMetrics;
class QPaintEvent;
class QShowEvent;
class QHideEvent;
class QTimer;

class TemperatureTrendPlotWidget : public QWidget
{
public:
    explicit TemperatureTrendPlotWidget(QWidget *parent = nullptr);

    void setCornerBadgeText(const QString& text);
    void setCompactMode(bool compact);
    void setEnglish(bool english);
    void setChannelIndex(int channelIndex);
    void setSamples(const QVector<double>& samples);
    void setSampleTimes(const QVector<double>& sampleTimes);
    void setTimeAxisEnabled(bool enabled);
    void setOverviewTimeAxis(bool enabled);
    void setXAxisVisible(bool visible);
    void setTargetTemperature(double celsius);
    QRectF plotAreaRect() const;

protected:
    void paintEvent(QPaintEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    static QString axisTickLabel(double value);
    static QString timeAxisTickLabel(double value);
    static std::pair<double, double> temperatureAxisRange(const QVector<double>& finiteSamples,
                                                           double targetTemperature);
    int xAxisTickCount(qreal plotWidth, const QFontMetrics& metrics) const;
    std::pair<double, double> visibleTimeRange(double now, int intervals) const;
    std::pair<double, double> visibleTemperatureAxisRange(double now) const;
    void updateSampleProperties();
    void applyPlotSizing();
    int yAxisIntervals() const;

    QString corner_badge_text_;
    QVector<double> samples_;
    QVector<double> sample_times_;
    double target_temperature_c_ = std::numeric_limits<double>::quiet_NaN();
    int channel_index_ = 0;
    bool compact_mode_ = false;
    bool time_axis_enabled_ = false;
    bool overview_time_axis_ = false;
    bool x_axis_visible_ = true;
    bool is_english_ = false;
    QTimer *clock_timer_ = nullptr;
};
