#include "ground/calibration/CalibrationService.h"
#include "ground/main/GroundMainWindowImplementation.h"
#include "LogContext.h"
#ifdef VAPORVIEW_HAS_OSGEARTH
#include "map3d/Map3DWindow.h"
#endif

void MainWindow::onRemoteBasicTelemetryUpdated(const VaporView::TelemetryBasic& telemetry)
{
    if (state_->session_replay_) return;
    syncExternalSensorRoute();
    const bool realRelay = isFpgaRelayPath() && !state_->fpga_session_.remoteStatus.value("simulation").toBool();
    const auto now = std::chrono::steady_clock::now();
    auto hasFlag = [&telemetry](quint32 flag) {
        return (telemetry.validity_flags & flag) != 0;
    };

    const VaporView::Ground::RemoteEpsilonTelemetry remoteEpsilon =
        VaporView::Ground::decodeRemoteEpsilonTelemetry(telemetry, now);
    state_->current_epsilon_ = remoteEpsilon.data;
    state_->current_remote_epsilon_validity_flags_ = telemetry.validity_flags;
    if (state_->epsilon_device_session_)
    {
        state_->epsilon_device_session_->setRemoteAvailable(
            remoteEpsilon.available,
            state_->is_english_ ? QStringLiteral("Remote Sky")
                                : QStringLiteral("天空端远程"));
    }
    if (hasFlag(VaporView::BasicHasLidar) && std::isfinite(telemetry.lidar_distance_m) && telemetry.lidar_distance_m >= 0)
    {
        state_->lidar_from_raw_fpga_ = false;
        state_->current_lidar_ = {};
        state_->current_lidar_.valid = true;
        state_->current_lidar_.timestamp = now;
        state_->current_lidar_.distance_m = telemetry.lidar_distance_m;
        if (hasFlag(VaporView::BasicHasLegacyLidarStrength))
        {
            state_->current_lidar_.legacy_signal_strength = telemetry.legacy_lidar_signal_strength;
        }
    }
    else if (!realRelay || !state_->lidar_from_raw_fpga_)
    {
        state_->current_lidar_ = {};
        state_->current_lidar_.error_message = remoteNoDataText(state_->is_english_).toStdString();
    }

    if (!realRelay && hasFlag(VaporView::BasicHasTemperature) && hasFlag(VaporView::BasicHasHumidity)
        && std::isfinite(telemetry.temperature_c) && std::isfinite(telemetry.humidity_percent))
    {
        state_->hc2a_from_raw_fpga_ = false;
        state_->current_hc2a_ = {};
        state_->current_hc2a_.valid = true;
        state_->current_hc2a_.timestamp = now;
        state_->current_hc2a_.temperature = telemetry.temperature_c;
        state_->current_hc2a_.humidity = telemetry.humidity_percent;
    }
    else if (!realRelay || !state_->hc2a_from_raw_fpga_)
    {
        state_->current_hc2a_ = {};
        state_->current_hc2a_.error_message = remoteNoDataText(state_->is_english_).toStdString();
    }

    if (hasFlag(VaporView::BasicHasPressure) && std::isfinite(telemetry.pressure_hpa))
    {
        state_->ptb_from_raw_fpga_ = false;
        state_->current_ptb_ = {};
        state_->current_ptb_.valid = true;
        state_->current_ptb_.timestamp = now;
        state_->current_ptb_.pressure_hpa = telemetry.pressure_hpa;
    }
    else if (!realRelay || !state_->ptb_from_raw_fpga_)
    {
        state_->current_ptb_ = {};
        state_->current_ptb_.error_message = remoteNoDataText(state_->is_english_).toStdString();
    }
    // RemoteSkyController updates device availability from Basic flags before this slot.
    // Restore raw owners' actual receipt ages for the home page, including an absent HC2A.
    if (realRelay && isRemoteSkyMode()) {
        auto *remote = state_->remote_sky_controller_.get();
        const auto wallNow = QDateTime::currentMSecsSinceEpoch();
        auto publishReceipt = [&](VaporView::SkyDeviceId id, const auto& sample) {
            if (sample.valid) {
                const auto age = std::chrono::duration_cast<std::chrono::milliseconds>(now - sample.timestamp).count();
                remote->noteDeviceData(id, wallNow - age);
            } else {
                remote->clearDeviceData(id);
            }
        };
        publishReceipt(VaporView::SkyDeviceId::Hc2a, state_->current_hc2a_);
        if (state_->ptb_from_raw_fpga_) publishReceipt(VaporView::SkyDeviceId::Ptb, state_->current_ptb_);
        if (state_->lidar_from_raw_fpga_) publishReceipt(VaporView::SkyDeviceId::Lidar, state_->current_lidar_);
    }
#ifdef VAPORVIEW_HAS_OSGEARTH
    if (remoteEpsilon.available)
    {
        if (remoteEpsilon.hasPosition)
        {
            maybeForwardMap3DSample(state_->current_epsilon_, telemetry.host_time_us);
        }
        else
        {
            noteMap3DSampleDrop(QStringLiteral("Remote"),
                                QStringLiteral("missing BasicHasPosition"),
                                telemetry.host_time_us);
        }
    }
#endif

    state_->calibration_service_->noteSensor(QStringLiteral("remote_environment"),
        {{"host_received_utc_us",QString::number(VaporView::Session::harmonicReceptionTimeUs())},
         {"source_host_time_us",QString::number(telemetry.host_time_us)},
         {"sensor_sample_utc",QJsonValue(QJsonValue::Null)},{"alignment","host_receipt_uncertain"},
         {"temperature_valid",!realRelay && hasFlag(VaporView::BasicHasTemperature)},{"pressure_valid",hasFlag(VaporView::BasicHasPressure)},
         {"temperature_c",!realRelay && hasFlag(VaporView::BasicHasTemperature)?QJsonValue(telemetry.temperature_c):QJsonValue(QJsonValue::Null)},
         {"pressure_hpa",hasFlag(VaporView::BasicHasPressure)?QJsonValue(telemetry.pressure_hpa):QJsonValue(QJsonValue::Null)}});

    // The existing 100 ms display timer renders the latest snapshot. Keep
    // recording and navigation forwarding above on every packet.
}

#ifdef VAPORVIEW_HAS_OSGEARTH
namespace {
using SteadyClock = std::chrono::steady_clock;
constexpr qint64 environmentTtlMs = 3000;
constexpr qint64 positionTtlMs = 2000;
constexpr qint64 waterTtlMs = 2000;

qint64 receptionAgeMs(SteadyClock::time_point received, SteadyClock::time_point now)
{
    if (received == SteadyClock::time_point{} || received > now) return -1;
    return std::chrono::duration_cast<std::chrono::milliseconds>(now - received).count();
}

bool fresh(qint64 age, qint64 ttl) { return age >= 0 && age <= ttl; }
}

void MainWindow::readMap3DEnvironment(VaporView::Map3D::Map3DLiveTelemetrySnapshot& s) const
{
    const auto now = SteadyClock::now();
    const auto& hc = state_->current_hc2a_;
    const auto& ptb = state_->current_ptb_;
    const auto& lidar = state_->current_lidar_;
    auto source = [this](QString sensor, bool raw) {
        if (state_->session_replay_) return sensor + QStringLiteral(" / Session");
        if (raw) return sensor + QStringLiteral(" / FPGA");
        return isRemoteSkyMode() ? sensor + QStringLiteral(" / Basic") : sensor;
    };
    if (hc.valid) {
        s.temperatureSource = s.humiditySource = source(QStringLiteral("HC2A"), state_->hc2a_from_raw_fpga_);
        s.temperatureAgeMs = s.humidityAgeMs = receptionAgeMs(hc.timestamp, now);
        s.temperatureStale = s.humidityStale = !fresh(s.temperatureAgeMs, environmentTtlMs);
        s.hasTemperature = std::isfinite(hc.temperature);
        s.hasHumidity = std::isfinite(hc.humidity);
        s.temperatureC = hc.temperature;
        s.humidityRh = hc.humidity;
    }
    if (ptb.valid && std::isfinite(ptb.pressure_hpa)) {
        s.pressureSource = source(QStringLiteral("PTB210"), state_->ptb_from_raw_fpga_);
        s.pressureAgeMs = receptionAgeMs(ptb.timestamp, now);
        s.pressureStale = !fresh(s.pressureAgeMs, environmentTtlMs);
        s.hasPressure = true;
        s.pressureHpa = ptb.pressure_hpa;
    }
    if (lidar.valid && std::isfinite(lidar.distance_m) && lidar.distance_m >= 0) {
        s.lidarAgeMs = receptionAgeMs(lidar.timestamp, now);
        s.lidarStale = !fresh(s.lidarAgeMs, positionTtlMs);
        s.hasLidar = true;
        s.lidarDistanceM = lidar.distance_m;
    }
}

void MainWindow::readMap3DWater(VaporView::Map3D::Map3DLiveTelemetrySnapshot& s)
{
    const auto water = state_->calibration_service_->liveSnapshot();
    s.calibrationSelected = water.calibrationSelected;
    s.calibrationValid = water.calibrationValid;
    s.calibrationName = water.calibrationName;
    s.calibrationSha256 = water.calibrationSha256;
    s.waterUnit = water.unit;
    s.calibrationWarning = water.warning;
    if (!water.calibrationSelected || !water.calibrationValid) return;
    if (!state_->map3d_calibration_epoch_ || state_->map3d_calibration_sha_ != water.calibrationSha256 ||
        state_->map3d_water_unit_ != water.unit) {
        ++state_->map3d_calibration_epoch_;
        state_->map3d_calibration_sha_ = water.calibrationSha256;
        state_->map3d_water_unit_ = water.unit;
    }
    s.calibrationEpoch = state_->map3d_calibration_epoch_;
    const auto copy = [](const auto& reading, bool& has, double& value, qint64& age,
                         bool& stale, qint64& hostTime, QString& status) {
        age = reading.ageMs;
        stale = !fresh(age, waterTtlMs);
        has = reading.valid && std::isfinite(reading.value);
        if (has) value = reading.value;
        hostTime = static_cast<qint64>(reading.hostTimeUs / 1000);
        status = reading.status;
    };
    copy(water.inSitu1368, s.hasInSituWater, s.inSituWater, s.inSituAgeMs, s.inSituStale, s.inSituHostTimeMs, s.inSituStatus);
    copy(water.column816, s.hasColumnWater, s.columnWater, s.columnAgeMs, s.columnStale, s.columnHostTimeMs, s.columnStatus);
}

VaporView::Map3D::Map3DLiveTelemetrySnapshot MainWindow::map3DLiveTelemetrySnapshot()
{
    syncExternalSensorRoute();
    VaporView::Map3D::Map3DLiveTelemetrySnapshot s;
    s.snapshotHostTimeMs = QDateTime::currentMSecsSinceEpoch();
    const bool remote = isRemoteSkyMode();
    const bool relay = isFpgaRelayPath();
    s.sourceConnected = state_->session_replay_ || isUiTestMode() || (remote ? state_->remote_sky_controller_->isOpen()
        : state_->is_connected_ || state_->fpga_ready_);
    const auto& nav = state_->current_epsilon_;
    const bool localFpga = !remote && relay && !state_->session_replay_;
    const bool hasPositionPacket = state_->session_replay_ || (localFpga ? state_->fpga_position_valid_ :
        !remote || (state_->current_remote_epsilon_validity_flags_ & VaporView::BasicHasPosition));
    const auto now = SteadyClock::now();
    if (nav.valid) {
        s.fixQualityText = QString::fromStdString(nav.gnss_fix_text);
        if (s.fixQualityText.isEmpty()) s.fixQualityText = QString::number(nav.gnss_fix_code);
        VaporView::Geo::NavSample llh;
        llh.latDeg = nav.latitude_deg;
        llh.lonDeg = nav.longitude_deg;
        llh.heightM = nav.height_m;
        if (hasPositionPacket && llh.hasLlh()) {
            s.hasPosition = true;
            s.latDeg = llh.latDeg;
            s.lonDeg = llh.lonDeg;
            s.heightM = llh.heightM;
            s.navAgeMs = receptionAgeMs(localFpga ? state_->fpga_position_time_ : nav.timestamp, now);
            s.navStale = !fresh(s.navAgeMs, positionTtlMs);
        }
    }
    readMap3DEnvironment(s);
    if (!state_->session_replay_) readMap3DWater(s);

    const auto& tec = state_->current_temperature_controller_;
    const auto& ai8 = state_->map3d_ai8_;
    QStringList thermal;
    const bool en = state_->is_english_;
    const auto connection = [en](bool online) {
        return online ? (en ? QStringLiteral("online") : QStringLiteral("在线")) :
                        (en ? QStringLiteral("offline") : QStringLiteral("离线"));
    };
    if (relay && !state_->session_replay_) {
        for (int ch = 0; ch < 2; ++ch) {
            const auto port = homeTec103Port(ch == 0 ? VaporView::SkyDeviceId::Tec103InSitu : VaporView::SkyDeviceId::Tec103Remote);
            QString text = ch == 0 ? (en ? QStringLiteral("TEC103 in-situ") : QStringLiteral("TEC103 原位"))
                                   : (en ? QStringLiteral("TEC103 remote") : QStringLiteral("TEC103 遥测"));
            const bool connected = port.value("connected").toBool();
            text += QStringLiteral(" ") + connection(connected);
            const double temp = tec.channels[ch].measured_temperature_c;
            if (connected && tec.valid && std::isfinite(temp)) text += QStringLiteral(" %1 °C").arg(temp, 0, 'f', 1);
            if (port.value("error").toInt()) {
                text += en ? QStringLiteral(" fault") : QStringLiteral(" 故障");
                s.thermalFault = true;
            }
            thermal << text;
        }
        const auto portTime = remote ? state_->fpga_session_.statusTime : state_->map3d_tec_ports_time_;
        s.thermalAgeMs = receptionAgeMs(tec.timestamp != SteadyClock::time_point{} ? tec.timestamp : portTime, now);
        s.thermalFault |= tec.error_code != 0;
    }
    const qint64 ai8Age = receptionAgeMs(state_->map3d_ai8_time_, now);
    const bool ai8Online = (ai8Age >= 0 && (ai8.valid || ai8.mainStatusValid || ai8.alarmStatusValid)) ||
        (relay && !state_->session_replay_ && homeFpgaDeviceState(VaporView::SkyDeviceId::Ai8TemperatureController) == VaporView::DeviceState::Connected);
    QString ai8Text = QStringLiteral("AI-8288 ") + connection(ai8Online);
    if (ai8Age >= 0) {
        s.thermalAgeMs = std::max(s.thermalAgeMs, ai8Age); // A fresh endpoint cannot refresh its stale sibling.
        if (ai8.valid) {
            for (int ch = 0; ch < int(ai8.measuredC.size()); ++ch) {
                if (std::isfinite(ai8.measuredC[ch]))
                    ai8Text += QStringLiteral(" CH%1 %2 °C").arg(ch + 1).arg(ai8.measuredC[ch], 0, 'f', 1);
            }
        }
        if ((ai8.mainStatusValid && (ai8.mainStatusRaw & 0x100)) || (ai8.alarmStatusValid &&
            std::any_of(ai8.alarmStatusRegisters.cbegin(), ai8.alarmStatusRegisters.cend(), [](auto reg) { return reg != 0; }))) {
            s.thermalFault = true;
            ai8Text += en ? QStringLiteral(" alarm/fault") : QStringLiteral(" 告警/故障");
        }
    }
    thermal << ai8Text;
    s.thermalSummary = thermal.join(QStringLiteral(" · "));
    s.hasThermal = s.thermalAgeMs >= 0 || s.thermalFault;
    s.thermalStale = !fresh(s.thermalAgeMs, environmentTtlMs);
    return s;
}

void MainWindow::maybeForwardMap3DSample(const VaporView::EpsilonData& data, quint64 recordTimestampUs, MapSampleSource source)
{
    auto *controller = state_->map3d_controller_.get();
    if (!controller || !controller->window() || !controller->window()->isVisible()) return;
    VaporView::Geo::TrajectoryHeatValues heat;
    const bool replay = source == MapSampleSource::SessionReplay;
    const auto positionTime = !replay && !isRemoteSkyMode() && isFpgaRelayPath() && state_->fpga_position_valid_
        ? state_->fpga_position_time_ : data.timestamp;
    if (data.valid && data.gnss_fix_code > 0 && fresh(receptionAgeMs(positionTime, SteadyClock::now()), positionTtlMs)) {
        VaporView::Map3D::Map3DLiveTelemetrySnapshot s;
        readMap3DEnvironment(s);
        if (s.hasTemperature && !s.temperatureStale) heat.temperatureC = s.temperatureC;
        if (s.hasHumidity && !s.humidityStale) heat.humidityRh = s.humidityRh;
        if (s.hasPressure && !s.pressureStale) heat.pressureHpa = s.pressureHpa;
        if (s.hasLidar && !s.lidarStale) heat.lidarDistanceM = s.lidarDistanceM;
        if (!replay && !state_->session_replay_) {
            readMap3DWater(s);
            heat.calibrationEpoch = s.calibrationEpoch;
            if (s.hasInSituWater && !s.inSituStale) heat.inSituWaterConcentration = s.inSituWater;
            if (s.hasColumnWater && !s.columnStale) heat.columnWaterConcentration = s.columnWater;
        }
    }
    controller->forwardLiveRenderSample(data, recordTimestampUs, heat);
}

void MainWindow::noteMap3DSampleDrop(const QString& source, const QString& reason, quint64 recordTimestampUs)
{
    if (state_->map3d_controller_)
    {
        state_->map3d_controller_->noteDrop(source, reason, recordTimestampUs);
    }
}

#ifdef VAPORVIEW_MAIN_WINDOW_TESTING
int MainWindow::testPendingMap3DSampleCount() const
{
    return state_->map3d_controller_ ? state_->map3d_controller_->pendingSampleCount() : 0;
}

qint64 MainWindow::testLatestPendingMap3DRecordTimestampUs() const
{
    return state_->map3d_controller_ ? state_->map3d_controller_->latestPendingRecordTimestampUs() : -1;
}

bool MainWindow::testMap3DFlushTimerActive() const
{
    return state_->map3d_controller_ && state_->map3d_controller_->flushTimerActive();
}

QString MainWindow::testLastMap3DDropReason() const
{
    return state_->map3d_controller_ ? state_->map3d_controller_->lastDropReason() : QString();
}

void MainWindow::testMaybeForwardMap3DSampleForMap3D(const VaporView::EpsilonData& epsilonData,
                                                     quint64 recordTimestampUs)
{
    maybeForwardMap3DSample(epsilonData, recordTimestampUs);
}

void MainWindow::testOnRemoteBasicTelemetryUpdatedForMap3D(const VaporView::TelemetryBasic& telemetry)
{
    onRemoteBasicTelemetryUpdated(telemetry);
}
#endif
#endif

void MainWindow::onRemoteWaveformUpdated(const VaporView::DownsampledWaveform& waveform)
{
    if (!state_->remote_wave_stream_requested_)
    {
        return;
    }
    if (state_->tcp_wave_panel_)
    {
        if (waveform.channel_id == 1)
        {
            state_->tcp_wave_panel_->injectRemoteRawSignalFrame(waveform.host_time_us, waveform.samples);
        }
        else if (waveform.channel_id == 4)
        {
            state_->tcp_wave_panel_->injectRemoteSecondHarmonicFrame(waveform.host_time_us, waveform.samples);
        }
    }
}

void MainWindow::onRemoteWaveformFeatureUpdated(const VaporView::WaveformFeature& feature)
{
    if (!state_->remote_wave_stream_requested_)
    {
        return;
    }
    if (state_->tcp_wave_panel_)
    {
        state_->tcp_wave_panel_->injectRemoteWaveformFeature(feature);
    }
}

void MainWindow::onRemoteTelemetryStatusUpdated(const VaporView::TelemetryStatus& status)
{
    if (!state_->remote_sky_online_)
    {
        state_->remote_sky_online_ = true;
        publishGroundLog(VaporView::LogLevel::Info,
                         QStringLiteral("telemetry.connection"),
                         QStringLiteral("remote_sky_handshake_confirmed"),
                         QStringLiteral("天空端握手成功。"),
                         {{QStringLiteral("ui_visibility"), QStringLiteral("details")}});
    }
    bool deviceStatesChanged = status.devices.size() != state_->remote_status_.devices.size();
    if (!deviceStatesChanged) {
        for (int i = 0; i < status.devices.size(); ++i)
            if (status.devices[i].device_id != state_->remote_status_.devices[i].device_id
                || status.devices[i].state != state_->remote_status_.devices[i].state) {
                deviceStatesChanged = true; break;
            }
    }
    const auto previousDevices = state_->remote_status_.devices;
    state_->remote_status_ = status;
    maybeConnectRemoteDevices();
    const quint64 rawTotal =
        status.raw_navigation_record_count +
        status.raw_pressure_record_count +
        status.raw_temperature_humidity_record_count +
        status.raw_distance_record_count +
        status.raw_waveform_record_count +
        status.raw_laser_temperature_controller_record_count +
        status.raw_system_temperature_controller_record_count;
    if (!status.session_name.isEmpty() ||
        status.telemetry_record_count > 0 ||
        status.waveform_feature_record_count > 0 ||
        status.waveform_snapshot_record_count > 0 ||
        rawTotal > 0)
    {
        state_->last_remote_recording_status_ = status;
        state_->has_last_remote_recording_status_ = true;
    }
    if (state_->tcp_wave_panel_)
    {
        state_->tcp_wave_panel_->setRemoteFeatureRateHz(status.feature_rate_hz);
    }
    if(isRemoteSkyMode() && state_->remote_recording_state_!=0 && status.recording_state==0 && !isUiTestMode())state_->calibration_service_->finish();
    if(isRemoteSkyMode())state_->calibration_service_->setExternalSessionOpen(status.recording_state!=0);
    if (isRemoteSkyMode()) {
        // Link loss resets the transport's recording state. Only a Sky status can
        // close the profile session; its name also detects a new one while offline.
        const bool differentSession = !status.session_name.isEmpty() && !state_->remote_profile_session_name_.isEmpty()
            && status.session_name != state_->remote_profile_session_name_;
        if (status.recording_state == 1 && (!state_->remote_profile_session_open_ || differentSession))
            state_->tcp_wave_panel_->clearWaterProfiles();
        state_->remote_profile_session_open_ = status.recording_state != 0;
        if (!state_->remote_profile_session_open_ || !status.session_name.isEmpty())
            state_->remote_profile_session_name_ = status.session_name;
    }
    state_->remote_recording_state_ = status.recording_state;
    for (const VaporView::DeviceStatusItem& item : status.devices)
    {
        const auto previous = std::find_if(previousDevices.cbegin(), previousDevices.cend(),
            [&](const auto& old) { return old.device_id == item.device_id; });
        const bool changed = previous == previousDevices.cend() || previous->state != item.state;
        if (changed) updateRemoteDeviceButtonText(item.device_id, item.state);
        if (changed && item.state != VaporView::DeviceState::Connected)
        {
            if (item.device_id == VaporView::SkyDeviceId::Epsilon)
            {
                state_->current_epsilon_ = VaporView::EpsilonData();
                state_->current_remote_epsilon_validity_flags_ = 0;
                if (state_->epsilon_device_session_)
                {
                    state_->epsilon_device_session_->setRemoteAvailable(false);
                }
            }
            else if (item.device_id == VaporView::SkyDeviceId::Ptb)
            {
                state_->current_ptb_ = VaporView::PtbData();
                state_->current_ptb_.error_message = remoteDisconnectedText(state_->is_english_).toStdString();
            }
            else if (item.device_id == VaporView::SkyDeviceId::Hc2a)
            {
                state_->current_hc2a_ = VaporView::Hc2aData();
                state_->current_hc2a_.error_message = remoteDisconnectedText(state_->is_english_).toStdString();
            }
            else if (item.device_id == VaporView::SkyDeviceId::Lidar)
            {
                state_->current_lidar_ = VaporView::LidarData();
                state_->current_lidar_.error_message = remoteDisconnectedText(state_->is_english_).toStdString();
            }
            else if (item.device_id == VaporView::SkyDeviceId::TemperatureController)
            {
                if (state_->rd105_device_session_)
                {
                    state_->rd105_device_session_->setRemoteAvailable(false);
                }
                invalidateTemperatureControllerDataUi();
            }
            else if (item.device_id == VaporView::SkyDeviceId::Ai8TemperatureController &&
                     (state_->ai8_temperature_controller_panel_ ||
                      state_->ai8_temperature_overview_panel_))
            {
                const bool fpgaConnected = isFpgaRelayPath() &&
                    homeFpgaDeviceState(VaporView::SkyDeviceId::Ai8TemperatureController) == VaporView::DeviceState::Connected;
                const bool keepFaultMetadata = item.state == VaporView::DeviceState::Error &&
                    state_->remote_ai8_fault_metadata_ && state_->remote_sky_controller_ &&
                    state_->remote_sky_controller_->deviceDataFresh(item.device_id,
                        QDateTime::currentMSecsSinceEpoch(), 3000);
                if (!isFpgaRelayPath() && state_->ai8_device_session_ && !(keepFaultMetadata && !state_->remote_ai8_system_fault_))
                {
                    state_->ai8_device_session_->setRemoteAvailable(false);
                }
                if (keepFaultMetadata)
                    continue;
#ifdef VAPORVIEW_HAS_OSGEARTH
                state_->map3d_ai8_time_ = {};
#endif
                state_->remote_ai8_fault_metadata_ = false;
                state_->remote_ai8_system_fault_ = false;
                if (state_->ai8_temperature_controller_panel_)
                {
                    if (!(item.state == VaporView::DeviceState::Error && fpgaConnected))
                        state_->ai8_temperature_controller_panel_->setBackendConnected(false);
                    state_->ai8_temperature_controller_panel_->applyLiveData({});
                }
                if (state_->ai8_temperature_overview_panel_)
                {
                    state_->ai8_temperature_overview_panel_->applyLiveData({});
                }
                updateAi8TemperatureTitleStatus();
            }
        }
    }
    if (state_->remote_wave_stream_auto_start_ &&
        !state_->remote_wave_stream_requested_ &&
        !state_->remote_wave_stream_enable_pending_ &&
        state_->remote_sky_controller_ && state_->remote_sky_controller_->isOpen() &&
        state_->remote_sky_controller_->deviceState(VaporView::SkyDeviceId::WaveTcp) == VaporView::DeviceState::Connected)
    {
        state_->remote_wave_stream_enable_pending_ = true;
        state_->remote_sky_controller_->sendCommand(VaporView::CommandId::EnableWaveformStreaming);
    }
    // The display timer renders telemetry; status packets only update changed actions.
    updateRecordingStatusLabel();
    updateTemperatureTitleButtonsState();
    if (deviceStatesChanged) {
        if (isFpgaRelayPath()) scheduleFpgaDeviceConfigUi();
        else updateDeviceConfigState();
    }
}

void MainWindow::onRemoteTemperatureControllerStatusUpdated(const VaporView::TemperatureControllerData& controllerData)
{
    state_->recording_service_->recordTemperatureControllerStatus(
        VaporView::Ground::Session::GroundRecordingService::currentTimestampUs(),controllerData);
    state_->current_temperature_controller_ = controllerData;
    state_->current_temperature_controller_.timestamp = std::chrono::steady_clock::now();
    if (state_->rd105_device_session_)
    {
        state_->rd105_device_session_->setRemoteAvailable(
            controllerData.valid,
            state_->is_english_ ? QStringLiteral("Remote Sky")
                                : QStringLiteral("天空端远程"));
    }
    if (state_->device_panel_coordinator_)
    {
        state_->device_panel_coordinator_->updateTemperatureData(state_->current_temperature_controller_);
    }
}

void MainWindow::onRemoteAi8TemperatureControllerStatusUpdated(
    const VaporView::Ai8TemperatureControllerProtocol::LiveData& liveData)
{
#ifdef VAPORVIEW_HAS_OSGEARTH
    if (!state_->session_replay_) {
        state_->map3d_ai8_ = liveData;
        state_->map3d_ai8_time_ = std::chrono::steady_clock::now();
    }
#endif
    const bool hasChannelOrStatusMetadata = liveData.mainStatusValid || liveData.alarmStatusValid ||
        liveData.detailedChannel != 0 ||
        std::any_of(liveData.controlStates.cbegin(), liveData.controlStates.cend(), [](auto state) {
            return state != VaporView::Ai8TemperatureControllerProtocol::ChannelControlState::Unknown;
        });
    const bool hasSystemFault = liveData.mainStatusValid && (liveData.mainStatusRaw & 0x100) != 0;
    state_->remote_ai8_fault_metadata_ = !liveData.valid && hasChannelOrStatusMetadata;
    state_->remote_ai8_system_fault_ = hasSystemFault;
    if (!state_->ai8_temperature_controller_panel_ && !state_->ai8_temperature_overview_panel_)
    {
        return;
    }
    const bool relay = isFpgaRelayPath();
    const QString detail = relay
        ? (state_->is_english_ ? QStringLiteral("FPGA relay") : QStringLiteral("FPGA 中转"))
        : (state_->is_english_ ? QStringLiteral("Remote Sky") : QStringLiteral("天空端远程"));
    if (state_->ai8_temperature_controller_panel_)
    {
        if (!relay ||
            homeFpgaDeviceState(VaporView::SkyDeviceId::Ai8TemperatureController) == VaporView::DeviceState::Connected ||
            liveData.valid || hasChannelOrStatusMetadata)
            state_->ai8_temperature_controller_panel_->setBackendConnected(true, detail);
    }
    if (!relay && state_->ai8_device_session_)
    {
        const bool ai8Communicating = liveData.valid || (hasChannelOrStatusMetadata && !hasSystemFault);
        const bool freshRemote = state_->remote_sky_controller_ && state_->remote_sky_controller_->isOpen() &&
            state_->remote_sky_controller_->deviceDataFresh(
                VaporView::SkyDeviceId::Ai8TemperatureController, QDateTime::currentMSecsSinceEpoch(), 3000);
        state_->ai8_device_session_->setRemoteAvailable(
            remoteDeviceDataValid(VaporView::SkyDeviceId::Ai8TemperatureController, 3000) ||
                (ai8Communicating && freshRemote),
            detail);
    }
    if (state_->ai8_temperature_controller_panel_)
    {
        state_->ai8_temperature_controller_panel_->applyLiveData(liveData);
    }
    if (state_->ai8_temperature_overview_panel_)
    {
        state_->ai8_temperature_overview_panel_->applyLiveData(liveData);
    }
    updateAi8TemperatureTitleStatus();
}

void MainWindow::onRemoteCommandAckReceived(const VaporView::CommandAck& ack)
{
    const bool ok = ack.result == 0;
    const QString commandName = VaporView::commandIdName(ack.command_id);
    const QString errorText = VaporView::commandErrorCodeText(ack.error_code, state_->is_english_);
    const bool noError = ack.error_code == VaporView::CommandErrorCode::Ok;
    if (!isTemperatureCommand(ack.command_id) &&
        ack.command_id != VaporView::CommandId::DeviceOperation)
    {
        const VaporView::ScopedLogContext context({ack.correlation_id, ack.session_id});
        QVariantMap fields{{QStringLiteral("command"), commandName},
                           {QStringLiteral("command_id"), static_cast<quint16>(ack.command_id)},
                           {QStringLiteral("command_seq"), ack.command_seq},
                           {QStringLiteral("ack_result"), ack.result},
                           {QStringLiteral("command_error_code"), commandErrorCodeIdentifier(ack.error_code)}};
        if (ok && noError)
        {
            fields.insert(QStringLiteral("ui_visibility"), QStringLiteral("hidden"));
            publishGroundLog(VaporView::LogLevel::Debug,
                             QStringLiteral("telemetry.command"),
                             QStringLiteral("remote_command_ack_received"),
                             QStringLiteral("远程命令 ACK 已收到。"),
                             fields);
        }
        else
        {
            fields.insert(QStringLiteral("error_code"),
                          noError ? QStringLiteral("REMOTE_COMMAND_FAILED")
                                  : commandErrorCodeIdentifier(ack.error_code));
            fields.insert(QStringLiteral("ui_dedupe_key"),
                          QStringLiteral("remote_command:%1:failed").arg(commandName));
            publishGroundLog(VaporView::LogLevel::Error,
                             QStringLiteral("telemetry.command"),
                             QStringLiteral("remote_command_failed"),
                             QStringLiteral("远程命令执行失败。"),
                             fields);
        }
    }

    if (ack.command_id == VaporView::CommandId::GetSkyConfig &&
        ack.command_seq == state_->remote_config_session_.readSequence &&
        !(ok && noError))
    {
        state_->remote_config_session_.reading = false;
        state_->remote_connect_devices_pending_ = false;
        clearPendingRemoteWaveTcpConnection();
        setRemoteSkyConfigStatus(state_->is_english_
            ? QStringLiteral("Remote Sky config read was rejected: %1").arg(errorText)
            : QStringLiteral("读取天空端配置被拒绝：%1").arg(errorText),
            true);
        updateRemoteSkyConfigControlsState();
    }
    else if (ack.command_id == VaporView::CommandId::SetSkyConfig &&
             ack.command_seq == state_->remote_config_session_.applySequence)
    {
        if (ok && noError)
        {
            setRemoteSkyConfigStatus(state_->is_english_
                ? QStringLiteral("Remote Sky config ACK received; waiting for apply result...")
                : QStringLiteral("天空端配置 ACK 已收到，等待应用结果..."));
        }
        else
        {
            handleRemoteSkyConfigApplyResultReceived({{QStringLiteral("success"), false}, {QStringLiteral("error"), state_->is_english_
                ? QStringLiteral("Remote Sky config apply was rejected: %1").arg(errorText)
                : QStringLiteral("天空端配置应用被拒绝：%1").arg(errorText)}}, false);
        }
        updateRemoteSkyConfigControlsState();
    }
    else if (ack.command_id == VaporView::CommandId::SaveSkyConfig &&
             ack.command_seq == state_->remote_config_session_.saveSequence)
    {
        state_->remote_config_session_.saving = false;
        setRemoteSkyConfigStatus(ok && noError
            ? (state_->is_english_ ? QStringLiteral("Remote Sky config saved on sky.") : QStringLiteral("天空端配置已保存。"))
            : (state_->is_english_ ? QStringLiteral("Remote Sky config save failed: %1").arg(errorText)
                                  : QStringLiteral("天空端配置保存失败：%1").arg(errorText)),
            !(ok && noError));
        updateRemoteSkyConfigControlsState();
    }

    if (ack.command_id == VaporView::CommandId::AutoDetectSerialPorts &&
        ack.command_seq == state_->remote_serial_detection_seq_ &&
        !(ok && noError))
    {
        state_->port_detection_in_progress_ = false;
        state_->remote_serial_detection_seq_ = 0;
        setRemoteSkyConfigStatus(state_->is_english_
            ? QStringLiteral("Remote serial-port detection was rejected: %1").arg(errorText)
            : QStringLiteral("远程串口自动识别被拒绝：%1").arg(errorText), true);
        updateConnectionStatus(state_->is_connected_);
    }
    else if (ack.command_id == VaporView::CommandId::CancelSerialPortDetection &&
             ack.command_seq == state_->remote_serial_detection_cancel_seq_ &&
             !(ok && noError))
    {
        state_->remote_serial_detection_cancel_seq_ = 0;
        const VaporView::ScopedLogContext context({ack.correlation_id, ack.session_id});
        publishGroundLog(VaporView::LogLevel::Warning,
                         QStringLiteral("device.connection"),
                         QStringLiteral("remote_serial_port_detection_cancel_failed"),
                         QStringLiteral("天空端取消串口自动识别失败。"),
                         {{QStringLiteral("error_code"), commandErrorCodeIdentifier(ack.error_code)},
                          {QStringLiteral("ui_visibility"), QStringLiteral("attention")} });
    }

    if (ack.command_id == VaporView::CommandId::EnableWaveformStreaming)
    {
        state_->remote_wave_stream_enable_pending_ = false;
        state_->remote_wave_stream_requested_ = ok;
        updateRemoteDeviceButtonText(VaporView::SkyDeviceId::WaveTcp,
                                     state_->remote_sky_controller_->deviceState(VaporView::SkyDeviceId::WaveTcp));
    }
    else if (ack.command_id == VaporView::CommandId::DisableWaveformStreaming)
    {
        state_->remote_wave_stream_enable_pending_ = false;
        state_->remote_wave_stream_requested_ = false;
        updateRemoteDeviceButtonText(VaporView::SkyDeviceId::WaveTcp,
                                     state_->remote_sky_controller_->deviceState(VaporView::SkyDeviceId::WaveTcp));
    }

    if (ack.command_id == VaporView::CommandId::SetPeakSearchRange)
    {
        const auto it = state_->remote_peak_search_commands_.find(ack.command_seq);
        if (it != state_->remote_peak_search_commands_.end())
        {
            const VaporView::PeakSearchRange range = it.value();
            state_->remote_peak_search_commands_.erase(it);
            if (state_->tcp_wave_panel_)
            {
                if (ok)
                {
                    state_->tcp_wave_panel_->applyRemotePeakSearchRange(range.start_index, range.end_index);
                    const VaporView::ScopedLogContext context({ack.correlation_id, ack.session_id});
                    publishGroundLog(VaporView::LogLevel::Info,
                                     QStringLiteral("telemetry.command"),
                                     QStringLiteral("peak_search_range_applied"),
                                     QStringLiteral("峰值搜索区间已生效，旧远程峰值趋势已清空。"),
                                     {{QStringLiteral("command"), commandName},
                                      {QStringLiteral("command_id"), static_cast<quint16>(ack.command_id)},
                                      {QStringLiteral("command_seq"), ack.command_seq},
                                      {QStringLiteral("range_start_index"), range.start_index},
                                      {QStringLiteral("range_end_index"), range.end_index},
                                      {QStringLiteral("ui_visibility"), QStringLiteral("details")}});
                }
                else
                {
                    state_->tcp_wave_panel_->rejectRemotePeakSearchRange(errorText);
                }
            }
        }
    }
}

void MainWindow::onRemoteLinkOpenChanged(bool open)
{
    if (isRemoteSkyMode())
    {
        const bool wasConnecting = state_->connection_attempt_in_progress_;
        state_->connection_attempt_in_progress_ = false;
        if (!open)
        {
            markRemoteSkyLinkClosed();
            if (wasConnecting) {
                publishGroundLog(VaporView::LogLevel::Error,
                    QStringLiteral("telemetry.connection"), QStringLiteral("remote_sky_connection_open_failed"),
                    QStringLiteral("打开天空端数传链路失败。"),
                    {{QStringLiteral("error_code"), QStringLiteral("TELEMETRY_LINK_OPEN_FAILED")},
                     {QStringLiteral("endpoint"), state_->remote_sky_controller_->telemetryService()->endpointDescription()},
                     {QStringLiteral("transport"), QStringLiteral("tcp")},
                     {QStringLiteral("ui_message"), QStringLiteral("无法连接天空端 %1：%2")
                         .arg(state_->remote_sky_controller_->telemetryService()->endpointDescription(),
                              state_->remote_sky_controller_->telemetryService()->lastLinkError())},
                     {QStringLiteral("ui_dedupe_key"), QStringLiteral("remote_sky:connection_open_failed")}});
            }
        }
        else
        {
            if (wasConnecting) {
                state_->remote_sky_controller_->sendCommand(VaporView::CommandId::DisableWaveformStreaming);
                state_->remote_sky_controller_->sendCommand(VaporView::CommandId::RequestStatus);
                publishGroundLog(VaporView::LogLevel::Info,
                    QStringLiteral("telemetry.connection"), QStringLiteral("remote_sky_connection_opened"),
                    QStringLiteral("数传链路已打开，正在等待天空端握手。"),
                    {{QStringLiteral("endpoint"), state_->remote_sky_controller_->telemetryService()->endpointDescription()},
                     {QStringLiteral("transport"), QStringLiteral("tcp")},
                     {QStringLiteral("ui_visibility"), QStringLiteral("details")}});
            }
            requestRemoteSkyConfigIfAvailable(false);
        }
        updateConnectionStatus(open);
    }
}

void MainWindow::onClearLogClicked()
{
    state_->pending_ui_log_records_.clear();
    state_->pending_ui_log_records_dropped_ = 0;
    if (state_->log_model_)
    {
        state_->log_model_->clearEntries();
    }
    state_->has_inline_progress_log_ = false;
    clearLogUnreadState();
    VaporView::LogService::withCurrentInstance([&](VaporView::LogService& logService) {
        logService.publish(VaporView::LogLevel::Info,
                           QStringLiteral("Ground"),
                           QStringLiteral("ui.log"),
                           QStringLiteral("日志面板显示已清空。"),
                           {{QStringLiteral("event"), QStringLiteral("ui_log_view_cleared")},
                            {QStringLiteral("ui_visibility"), QStringLiteral("hidden")}});
    });
}
