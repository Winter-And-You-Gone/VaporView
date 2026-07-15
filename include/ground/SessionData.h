#pragma once

#include <QString>
#include <QStringList>
#include <QVector>
#include <QtGlobal>

namespace VaporView::Ground
{

struct SessionTrackPoint
{
    double latitude = 0.0;
    double longitude = 0.0;
    double height_m = 0.0;
    double cumulative_distance_m = 0.0;
    double segment_distance_m = 0.0;
    double speed_mps = 0.0;
    quint64 timestamp_us = 0;
    quint64 waveform_timestamp_us = 0;
    quint64 waveform_delta_us = 0;
    float peak_value = 0.0f;
    double temperature_c = 0.0;
    double humidity_rh = 0.0;
    double pressure_hpa = 0.0;
    int csv_row = -1;
    int waveform_frame_index = -1;
    QString gnss_fix;
    bool has_height = false;
    bool has_speed = false;
    bool has_peak_value = false;
    bool has_temperature = false;
    bool has_humidity = false;
    bool has_pressure = false;
    bool has_waveform_match = false;
};

struct SessionTrackStats
{
    int scanned_rows = 0;
    int accepted_points = 0;
    int rejected_invalid_nav = 0;
    int rejected_bad_fix = 0;
    int rejected_zero_coordinate = 0;
    int rejected_out_of_range = 0;
    int rejected_jump = 0;
    double jump_threshold_m = 20.0;
};

struct SessionSensorData
{
    QStringList headers;
    QVector<QStringList> rows;
    QVector<quint64> timestamps_us;
    QVector<double> temperature_values;
    QVector<double> humidity_values;
    QVector<double> pressure_values;
    QVector<SessionTrackPoint> track_points;
    SessionTrackStats track_stats;
};

}  // namespace VaporView::Ground

using RtkTrackPoint = VaporView::Ground::SessionTrackPoint;
using RtkTrackStats = VaporView::Ground::SessionTrackStats;
