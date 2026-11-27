#include "ground/map/Map3DController.h"

#include "map3d/Map3DWindow.h"
#include "geo/CoordinateTransform.h"

#include <QEvent>
#include <cmath>
#include <utility>
#include <osgEarth/Threading>

namespace VaporView::Ground
{

Map3DController::Map3DController(QObject *parent)
    : QObject(parent)
{
    telemetry_timer_.setInterval(300);
    connect(&telemetry_timer_, &QTimer::timeout, this, &Map3DController::refreshLiveTelemetry);
    flush_timer_.setInterval(50);
    flush_timer_.setTimerType(Qt::CoarseTimer);
    connect(&flush_timer_, &QTimer::timeout, this, &Map3DController::flush);
}

Map3DController::~Map3DController()
{
    close();
}

void Map3DController::shutdownBackgroundJobs()
{
    jobs::shutdown();
}

void Map3DController::open()
{
    if (!window_)
    {
        window_ = new VaporView::Map3D::Map3DWindow(nullptr);
        window_->setUiTestMode(ui_test_mode_);
        window_->setAttribute(Qt::WA_QuitOnClose, false);
        window_->setWaterProfileProviders(revision_provider_, profile_provider_);
        connect(window_, &Map3D::Map3DWindow::openWaveformPageRequested, this, &Map3DController::openWaveformPageRequested);
        window_->installEventFilter(this);
        connect(window_, &QObject::destroyed, this, [this]() {
            window_ = nullptr;
            telemetry_timer_.stop();
            stopPendingSamples();
        });
    }

    if (window_->isVisible()) refreshLiveTelemetry();
    window_->show();
    window_->raise();
    window_->activateWindow();
}

bool Map3DController::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == window_.data()) {
        if (event->type() == QEvent::Show && telemetry_provider_) {
            refreshLiveTelemetry();
            telemetry_timer_.start();
        } else if (event->type() == QEvent::Hide) {
            telemetry_timer_.stop();
            stopPendingSamples();
        }
    }
    return QObject::eventFilter(watched, event);
}

void Map3DController::refreshLiveTelemetry()
{
    if (telemetry_provider_ && window_ && window_->isVisible())
        window_->setLiveTelemetrySnapshot(telemetry_provider_());
}

void Map3DController::setLiveTelemetryProvider(std::function<Map3D::Map3DLiveTelemetrySnapshot()> provider)
{
    telemetry_provider_ = std::move(provider);
    if (telemetry_provider_ && window_ && window_->isVisible()) {
        refreshLiveTelemetry();
        telemetry_timer_.start();
    } else {
        telemetry_timer_.stop();
    }
}

void Map3DController::setWaterProfileProviders(Map3D::Map3DProfileRevisionProvider revisionProvider,
                                               Map3D::Map3DWaterProfileProvider snapshotProvider)
{
    revision_provider_ = std::move(revisionProvider);
    profile_provider_ = std::move(snapshotProvider);
    if (window_) window_->setWaterProfileProviders(revision_provider_, profile_provider_);
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
    forwardLiveRenderSample(data, recordTimestampUs, {});
}

void Map3DController::forwardLiveRenderSample(const VaporView::EpsilonData& data, quint64 recordTimestampUs,
                                            const Geo::TrajectoryHeatValues& heatValues)
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

    const Geo::TrajectoryRenderSample frozen{sample, sample.fixQuality == Geo::FixQuality::Invalid
        ? Geo::TrajectoryHeatValues{} : heatValues};
    last_drop_reason_.clear();
    if (pending_samples_.empty())
    {
        pending_samples_.push_back(frozen);
    }
    else
    {
        pending_samples_.back() = frozen;
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

    std::vector<Geo::TrajectoryRenderSample> samples;
    samples.swap(pending_samples_);
    window_->appendRenderSamples(samples);
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
    return pending_samples_.empty() ? -1 : pending_samples_.back().navigation.recordTimestampUs;
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
