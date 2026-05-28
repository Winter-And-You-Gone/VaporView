#ifndef VaporView_SKY_DASHBOARD_TYPES_H_
#define VaporView_SKY_DASHBOARD_TYPES_H_

#include "TelemetryTypes.h"
#include "data_types.h"

#include <QVector>

namespace VaporView
{

struct SkyDashboardSnapshot
{
    quint64 host_time_us = 0;
    quint64 uptime_ms = 0;
    EpsilonData epsilon;
    PtbData ptb;
    HmpData hmp;
    LidarData lidar;
    WaveformFeature waveform_feature;
    QVector<float> latest_raw_waveform_preview;
    QVector<float> latest_harmonic_waveform_preview;
    QVector<float> peak_trend;
    TelemetryStatus telemetry_status;
    double epsilon_acquisition_rate_hz = 0.0;
    double ptb_acquisition_rate_hz = 0.0;
    double hmp_acquisition_rate_hz = 0.0;
    double lidar_acquisition_rate_hz = 0.0;
    double wave_tcp_acquisition_rate_hz = 0.0;
    double devices_csv_recording_rate_hz = 0.0;
    double raw_wave_recording_rate_hz = 0.0;
    double telemetry_basic_rate_hz = 0.0;
    double waveform_feature_rate_hz = 0.0;
    double waveform_downsampled_rate_hz = 0.0;
    bool epsilon_stale = true;
    bool ptb_stale = true;
    bool hmp_stale = true;
    bool lidar_stale = true;
    bool waveform_stale = true;
};

}  // namespace VaporView

#endif
