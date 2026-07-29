#include "ground/map/Map3DController.h"

#include "map3d/Map3DWindow.h"
#include "geo/CoordinateTransform.h"

#include <cmath>

namespace VaporView::Ground
{

Map3DController::Map3DController(QObject *parent)
    : QObject(parent)
{
    flush_timer_.setInterval(50);
    flush_timer_.setTimerType(Qt::CoarseTimer);
    connect(&flush_timer_, &QTimer::timeout, this, &Map3DController::flush);
}

Map3DController::~Map3DController()
{
    close();
}

void Map3DController::open()
{
    if (!window_)
    {
        window_ = new VaporView::Map3D::Map3DWindow(nullptr);
        window_->setUiTestMode(ui_test_mode_);
        window_->setAttribute(Qt::WA_QuitOnClose, false);
        connect(window_, &QObject::destroyed, this, [this]() {
            window_ = nullptr;
            stopPendingSamples();
        });
    }

    window_->show();
    window_->raise();
    window_->activateWindow();
}

void Map3DController::setUiTestMode(bool enabled)
{
    ui_test_mode_ = enabled;
    if (window_)
    {
        window_->setUiTestMode(enabled);
    }
}

void Map3DController::showDiagnostics()
{
    open();
    if (window_)
    {
        window_->showMapDiagnostics();
    }
}

void Map3DController::close()
{
    stopPendingSamples();
    if (window_)
    {
        delete window_;
        window_ = nullptr;
    }
}

void Map3DController::forwardEpsilonSample(const VaporView::EpsilonData& data,
                                            quint64 recordTimestampUs)
{
    if (!window_ || !window_->isVisible())
    {
        stopPendingSamples();
        return;
    }

    if (!data.valid)
    {
        stopPendingSamples();
        noteDrop(QStringLiteral("Live"), QStringLiteral("epsilon invalid"), recordTimestampUs);
        return;
    }

    const VaporView::Geo::NavSample sample = sampleFromEpsilon(data, recordTimestampUs);
    if (!sample.hasLlh())
    {
        stopPendingSamples();
        noteDrop(QStringLiteral("Live"), QStringLiteral("missing LLH"), recordTimestampUs);
        return;
    }

    last_drop_reason_.clear();
    if (pending_samples_.empty())
    {
        pending_samples_.push_back(sample);
    }
    else
    {
        pending_samples_.back() = sample;
    }
    if (!flush_timer_.isActive())
    {
        flush_timer_.start();
    }
}

void Map3DController::noteDrop(const QString& source,
                                const QString& reason,
                                quint64 recordTimestampUs)
{
    stopPendingSamples();
    last_drop_reason_ = reason;
    if (window_ && window_->isVisible())
    {
        window_->noteLiveSampleDrop(source, reason, static_cast<qint64>(recordTimestampUs));
    }
}

void Map3DController::flush()
{
    if (!window_ || !window_->isVisible())
    {
        stopPendingSamples();
        return;
    }
    if (pending_samples_.empty())
    {
        flush_timer_.stop();
        return;
    }

    std::vector<VaporView::Geo::NavSample> samples;
    samples.swap(pending_samples_);
    window_->appendSamples(samples);
    if (pending_samples_.empty())
    {
        flush_timer_.stop();
    }
}

VaporView::Map3D::Map3DWindow *Map3DController::window() const
{
    return window_.data();
}

int Map3DController::pendingSampleCount() const
{
    return static_cast<int>(pending_samples_.size());
}

qint64 Map3DController::latestPendingRecordTimestampUs() const
{
    return pending_samples_.empty() ? -1 : pending_samples_.back().recordTimestampUs;
}

bool Map3DController::flushTimerActive() const
{
    return flush_timer_.isActive();
}

QString Map3DController::lastDropReason() const
{
    return last_drop_reason_;
}

void Map3DController::stopPendingSamples()
{
    pending_samples_.clear();
    flush_timer_.stop();
}

VaporView::Geo::NavSample Map3DController::sampleFromEpsilon(
    const VaporView::EpsilonData& data,
    quint64 recordTimestampUs) const
{
    VaporView::Geo::NavSample sample;
    sample.recordTimestampUs = static_cast<qint64>(recordTimestampUs);
    sample.deviceTimestampUs = static_cast<qint64>(data.device_timestamp_us);
    sample.latDeg = data.latitude_deg;
    sample.lonDeg = data.longitude_deg;
    sample.heightM = data.height_m;
    sample.heightReference = VaporView::Geo::HeightReference::Wgs84Ellipsoid;
    sample.ecefXM = data.ecef_x_m;
    sample.ecefYM = data.ecef_y_m;
    sample.ecefZM = data.ecef_z_m;
    if (std::isfinite(data.ned_n_m) &&
        std::isfinite(data.ned_e_m) &&
        std::isfinite(data.ned_d_m) &&
        (std::abs(data.ned_n_m) > 1e-6 ||
         std::abs(data.ned_e_m) > 1e-6 ||
         std::abs(data.ned_d_m) > 1e-6))
    {
        sample.nedNM = data.ned_n_m;
        sample.nedEM = data.ned_e_m;
        sample.nedDM = data.ned_d_m;
    }
    sample.velNMps = data.vel_n_mps;
    sample.velEMps = data.vel_e_mps;
    sample.velDMps = data.vel_d_mps;
    sample.rollDeg = data.roll_deg;
    sample.pitchDeg = data.pitch_deg;
    sample.yawDeg = data.yaw_deg;
    sample.quatW = data.quat_w;
    sample.quatX = data.quat_x;
    sample.quatY = data.quat_y;
    sample.quatZ = data.quat_z;
    sample.satellites = data.gnss_satellites;
    sample.hdop = data.hdop;
    sample.vdop = data.vdop;
    sample.diffAgeS = data.diff_age_s;

    if (data.gnss_fix_code <= 0)
    {
        sample.fixQuality = VaporView::Geo::FixQuality::Invalid;
    }
    else if (data.gnss_fix_code >= 6)
    {
        sample.fixQuality = VaporView::Geo::FixQuality::Fixed;
    }
    else if (data.gnss_fix_code == 5)
    {
        sample.fixQuality = VaporView::Geo::FixQuality::Float;
    }
    else if (data.gnss_fix_code == 2)
    {
        sample.fixQuality = VaporView::Geo::FixQuality::Dgps;
    }
    else
    {
        sample.fixQuality = VaporView::Geo::FixQuality::Single;
    }

    VaporView::Geo::resolveEcefFromLlh(sample);
    return sample;
}

}  // namespace VaporView::Ground
