#include "ground/session/SessionTimelineModel.h"

#include <QCoreApplication>

#include <cmath>
#include <iostream>

namespace
{

int failures = 0;

void expect(bool condition, const char *message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

VaporView::Ground::SessionTrackPoint pointAt(quint64 timestampUs)
{
    VaporView::Ground::SessionTrackPoint point;
    point.timestamp_us = timestampUs;
    return point;
}

}  // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    QVector<VaporView::Ground::SessionTrackPoint> points = {
        pointAt(1'000'000),
        pointAt(2'000'000),
    };
    VaporView::Ground::Session::SessionTimelineModel::attachWaveformPeaks(
        points,
        {900'000, 1'100'000, 1'900'000, 2'100'000},
        {10.0f, 14.0f, 20.0f, 24.0f});

    expect(points.at(0).has_waveform_match, "first point should have a waveform match");
    expect(points.at(0).waveform_frame_index == 0, "ties should keep the earlier waveform frame");
    expect(points.at(0).waveform_delta_us == 100'000, "first waveform delta should be preserved");
    expect(points.at(0).has_peak_value, "first point should expose a peak value");
    expect(std::abs(points.at(0).peak_value - 12.0f) < 0.0001f,
           "high-rate peaks should average inside the first track interval");
    expect(std::abs(points.at(1).peak_value - 22.0f) < 0.0001f,
           "high-rate peaks should average inside the second track interval");

    points[0].has_peak_value = true;
    points[0].has_waveform_match = true;
    points[0].waveform_frame_index = 9;
    VaporView::Ground::Session::SessionTimelineModel::attachWaveformPeaks(points, {}, {});
    expect(!points.at(0).has_peak_value, "empty waveform input should clear stale peak state");
    expect(!points.at(0).has_waveform_match, "empty waveform input should clear stale match state");
    expect(points.at(0).waveform_frame_index == -1, "empty waveform input should clear stale frame index");

    QVector<VaporView::Ground::SessionTrackPoint> exact = {
        pointAt(1'000'000),
        pointAt(2'000'000),
    };
    VaporView::Ground::Session::SessionTimelineModel::attachWaveformPeaks(
        exact,
        {1'000'000, 2'000'000},
        {3.0f, 4.0f});
    expect(exact.at(0).peak_value == 3.0f && exact.at(1).peak_value == 4.0f,
           "equal-rate timelines should retain per-frame peak values");

    return failures == 0 ? 0 : 1;
}
