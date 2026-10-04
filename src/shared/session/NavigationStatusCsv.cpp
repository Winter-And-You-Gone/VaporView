#include "shared/session/NavigationStatusCsv.h"

#include <QStringList>
#include <cmath>

namespace VaporView::Session
{
namespace
{
QString number(bool available, double value, int precision = 6)
{
    return available && std::isfinite(value) ? QString::number(value, 'f', precision) : QString();
}
QString counter(bool available, quint64 value)
{
    return available ? QString::number(value) : QString();
}
bool knownFix(const NavigationStatusRecord& record)
{
    return record.navigationAvailable && record.gnssFixCode >= 0 && record.gnssFixCode <= 9;
}
bool fixed(const NavigationStatusRecord& record)
{
    return knownFix(record) && (record.gnssFixCode == 6 || record.gnssFixCode == 9);
}
}

QString rtcmHealthCode(RtcmHealth health)
{
    switch (health)
    {
    case RtcmHealth::Local: return QStringLiteral("local");
    case RtcmHealth::Disabled: return QStringLiteral("disabled");
    case RtcmHealth::Waiting: return QStringLiteral("waiting");
    case RtcmHealth::Normal: return QStringLiteral("normal");
    case RtcmHealth::Warning: return QStringLiteral("warning");
    case RtcmHealth::Delayed: return QStringLiteral("delayed");
    case RtcmHealth::Interrupted: return QStringLiteral("interrupted");
    case RtcmHealth::LinkDisconnected: return QStringLiteral("link_disconnected");
    case RtcmHealth::StatusUnavailable: return QStringLiteral("unavailable");
    }
    return QStringLiteral("unavailable");
}

QString navigationStatusCsvHeader()
{
    return QStringLiteral("record_timestamp_us,epsilon_device_timestamp_us,sky_report_timestamp_us,source_mode,"
        "sky_boot_id,rtcm_stream_id,rtk_service_running,rtcm_health,rtcm_available,rtcm_age_ms,"
        "rtcm_receive_bytes_per_s,rtcm_link_loss_available,rtcm_link_loss_percent,"
        "rtcm_link_frames_received,rtcm_link_frames_lost,rtcm_received_bytes,rtcm_received_chunks,"
        "rtcm_dropped_bytes,rtcm_dropped_chunks,navigation_available,position_available,"
        "gnss_fix_code,rtk_fixed,gnss_satellites,filter_status_bits,update_status_bits,"
        "hacc_m,vacc_m,nav_lat_deg,nav_lon_deg,nav_height_m\n");
}

QString navigationStatusCsvRow(const NavigationStatusRecord& r)
{
    const bool sky = r.rtcm.remote && r.rtcm.available;
    const bool position = r.positionAvailable && std::isfinite(r.latitudeDeg) &&
        std::abs(r.latitudeDeg) <= 90 && std::isfinite(r.longitudeDeg) && std::abs(r.longitudeDeg) <= 180;
    QStringList row;
    row << QString::number(r.timestampUs) << counter(r.epsilonTimeUs != 0, r.epsilonTimeUs)
        << counter(sky, r.skyReportTimeUs) << r.sourceMode
        << counter(sky, r.skyBootId) << counter(sky, r.rtcmStreamId)
        << (r.rtkServiceKnown ? QString::number(r.rtkServiceRunning) : QString())
        << rtcmHealthCode(r.rtcm.health) << QString::number(sky)
        << (sky && r.rtcm.ageMs >= 0 ? QString::number(r.rtcm.ageMs) : QString())
        << number(sky, r.rtcm.bytesPerSecond, 3)
        << QString::number(sky && r.rtcm.lossAvailable)
        << number(sky && r.rtcm.lossAvailable, r.rtcm.lossPercent, 3)
        << counter(sky, r.rtcm.linkFramesReceived) << counter(sky, r.rtcm.linkFramesLost)
        << counter(sky, r.rtcm.receivedBytes) << counter(sky, r.rtcm.receivedChunks)
        << counter(sky, r.rtcm.droppedBytes) << counter(sky, r.rtcm.droppedChunks)
        << QString::number(r.navigationAvailable) << QString::number(position)
        << (r.navigationAvailable ? QString::number(r.gnssFixCode) : QString())
        << (knownFix(r) ? QString::number(fixed(r)) : QString())
        << (r.navigationAvailable ? QString::number(r.satellites) : QString())
        << (r.filterStatusAvailable ? QString::number(r.filterStatusBits) : QString())
        << (r.filterStatusAvailable ? QString::number(r.updateStatusBits) : QString())
        << number(r.navigationAvailable && r.horizontalAccuracyM >= 0, r.horizontalAccuracyM)
        << number(r.navigationAvailable && r.verticalAccuracyM >= 0, r.verticalAccuracyM)
        << number(position, r.latitudeDeg, 9) << number(position, r.longitudeDeg, 9)
        << number(position, r.heightM);
    return row.join(QLatin1Char(',')) + QLatin1Char('\n');
}

QVariantMap NavigationStatusChangeTracker::observe(const NavigationStatusRecord& r)
{
    const bool baseline = !initialized_ || r.sourceMode != previous_.sourceMode ||
        r.skyBootId != previous_.skyBootId || r.rtcmStreamId != previous_.rtcmStreamId;
    const bool changed = baseline || r.rtcm.health != previous_.rtcm.health ||
        r.navigationAvailable != previous_.navigationAvailable ||
        r.positionAvailable != previous_.positionAvailable ||
        (r.navigationAvailable && r.gnssFixCode != previous_.gnssFixCode) ||
        r.filterStatusAvailable != previous_.filterStatusAvailable ||
        (r.rtkServiceKnown && r.rtkServiceRunning != previous_.rtkServiceRunning);
    QString transition = QStringLiteral("unchanged");
    if (baseline) transition = QStringLiteral("baseline");
    else if (!knownFix(r)) transition = QStringLiteral("unavailable");
    else if (knownFix(previous_) && fixed(previous_) && !fixed(r)) transition = QStringLiteral("degraded");
    else if (knownFix(previous_) && !fixed(previous_) && fixed(r)) transition = QStringLiteral("recovered");
    QVariantMap fields;
    if (changed)
    {
        fields = {{QStringLiteral("record_timestamp_us"), r.timestampUs},
            {QStringLiteral("epsilon_device_timestamp_us"), r.epsilonTimeUs},
            {QStringLiteral("sky_report_timestamp_us"), r.skyReportTimeUs},
            {QStringLiteral("sky_boot_id"), r.skyBootId},
            {QStringLiteral("rtcm_stream_id"), r.rtcmStreamId},
            {QStringLiteral("source_mode"), r.sourceMode},
            {QStringLiteral("rtcm_health"), rtcmHealthCode(r.rtcm.health)},
            {QStringLiteral("rtcm_age_ms"), r.rtcm.ageMs},
            {QStringLiteral("navigation_available"), r.navigationAvailable},
            {QStringLiteral("gnss_fix_code"), r.navigationAvailable ? r.gnssFixCode : -1},
            {QStringLiteral("rtk_fix_transition"), transition},
            {QStringLiteral("baseline_reset"), baseline}};
        if (r.navigationAvailable && std::isfinite(r.horizontalAccuracyM) && r.horizontalAccuracyM >= 0)
            fields.insert(QStringLiteral("hacc_m"), r.horizontalAccuracyM);
        if (r.positionAvailable && std::isfinite(r.latitudeDeg) && std::abs(r.latitudeDeg) <= 90 &&
            std::isfinite(r.longitudeDeg) && std::abs(r.longitudeDeg) <= 180)
        {
            fields.insert(QStringLiteral("latitude_deg"), r.latitudeDeg);
            fields.insert(QStringLiteral("longitude_deg"), r.longitudeDeg);
        }
    }
    previous_ = r;
    initialized_ = true;
    return fields;
}

bool NavigationStatusSampleGate::accept(const NavigationStatusRecord& r)
{
    const bool changed = !changes_.observe(r).isEmpty();
    // Coarse one-second UI timers can fire slightly early; use UTC second buckets
    // so jitter does not silently reduce periodic recording to every two seconds.
    if (!changed && last_time_us_ != 0 && r.timestampUs / 1'000'000ULL ==
        last_time_us_ / 1'000'000ULL) return false;
    last_time_us_ = r.timestampUs;
    return true;
}
}
