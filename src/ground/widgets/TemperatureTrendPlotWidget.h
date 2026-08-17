#pragma once

#include <QString>
#include <QVector>
#include <QWidget>

#include <limits>
#include <utility>

class QPaintEvent;

class TemperatureTrendPlotWidget : public QWidget
{
public:
    explicit TemperatureTrendPlotWidget(QWidget *parent = nullptr);

    void setCompactMode(bool compact);
    void setEnglish(bool english);
    void setChannelIndex(int channelIndex);
    void setSamples(const QVector<double>& samples);
    void setSampleTimes(const QVector<double>& sampleTimes);
    void setTimeAxisEnabled(bool enabled);
    void setTargetTemperature(double celsius);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    static QString axisTickLabel(double value);
    static QString timeAxisTickLabel(double value);
    static std::pair<double, double> temperatureAxisRange(const QVector<double>& finiteSamples,
                                                           double targetTemperature);
    void updateSampleProperties();
    void applyPlotSizing();

    QVector<double> samples_;
    QVector<double> sample_times_;
    double target_temperature_c_ = std::numeric_limits<double>::quiet_NaN();
    int channel_index_ = 0;
    bool compact_mode_ = false;
    bool time_axis_enabled_ = false;
    bool is_english_ = false;
};
