#include "ground/main/GroundMainWindowImplementation.h"
#include "ground/main/UiTestReplayWindow.h"
#include "ground/session/SessionCsv.h"
#include "Hc2aProtocol.h"
#include "FpgaTelemetry.h"
#include "EpsilonProtocol.h"
#include "Sdfm300Protocol.h"
#ifdef VAPORVIEW_HAS_OSGEARTH
#include "map3d/Map3DWindow.h"
#endif
#include "shared/session/HarmonicWaveform.h"

namespace {
using Sample = UiTestReplayWindow::Sample;
using Stream = UiTestReplayWindow::Stream;
namespace Raw = VaporView::SessionRawDat;

double number(const Sample& s, const QString& key)
{
    return VaporView::Ground::SessionCsv::parseOptionalDouble(s.fields.value(key));
}

bool flag(const Sample& s, const QString& key)
{
    return VaporView::Ground::SessionCsv::parseBooleanCsvField(s.fields.value(key));
}

VaporView::EpsilonData navigation(const Sample& s)
{
    VaporView::EpsilonData e;
    e.latitude_deg = number(s, "nav_lat_deg");
    e.longitude_deg = number(s, "nav_lon_deg");
    e.height_m = number(s, "nav_height_m");
    e.ecef_x_m = number(s, "ecef_x_m");
    e.ecef_y_m = number(s, "ecef_y_m");
    e.ecef_z_m = number(s, "ecef_z_m");
    e.ned_n_m = number(s, "ned_n_m");
    e.ned_e_m = number(s, "ned_e_m");
    e.ned_d_m = number(s, "ned_d_m");
    e.vel_n_mps = number(s, "vel_n_mps");
    e.vel_e_mps = number(s, "vel_e_mps");
    e.vel_d_mps = number(s, "vel_d_mps");
    e.body_vel_x_mps = number(s, "body_vel_x_mps");
    e.body_vel_y_mps = number(s, "body_vel_y_mps");
    e.body_vel_z_mps = number(s, "body_vel_z_mps");
    e.body_acc_x_mps2 = number(s, "body_acc_x_mps2");
    e.body_acc_y_mps2 = number(s, "body_acc_y_mps2");
    e.body_acc_z_mps2 = number(s, "body_acc_z_mps2");
    e.roll_deg = number(s, "roll_deg");
    e.pitch_deg = number(s, "pitch_deg");
    e.yaw_deg = number(s, "yaw_deg");
    e.quat_w = number(s, "quat_w");
    e.quat_x = number(s, "quat_x");
    e.quat_y = number(s, "quat_y");
    e.quat_z = number(s, "quat_z");
    e.attitude_delta_max_deg = number(s, "attitude_delta_max_deg");
    e.attitude_delta_ahrs_euler_deg = number(s, "attitude_delta_ahrs_euler_deg");
    e.attitude_delta_ahrs_quat_deg = number(s, "attitude_delta_ahrs_quat_deg");
    e.attitude_delta_euler_quat_deg = number(s, "attitude_delta_euler_quat_deg");
    e.ang_vel_x_radps = number(s, "ang_vel_x_radps");
    e.ang_vel_y_radps = number(s, "ang_vel_y_radps");
    e.ang_vel_z_radps = number(s, "ang_vel_z_radps");
    e.imu_acc_x_mps2 = number(s, "imu_acc_x_mps2");
    e.imu_acc_y_mps2 = number(s, "imu_acc_y_mps2");
    e.imu_acc_z_mps2 = number(s, "imu_acc_z_mps2");
    e.imu_gyr_x_radps = number(s, "imu_gyr_x_radps");
    e.imu_gyr_y_radps = number(s, "imu_gyr_y_radps");
    e.imu_gyr_z_radps = number(s, "imu_gyr_z_radps");
    e.mag_x_mg = number(s, "mag_x_mg");
    e.mag_y_mg = number(s, "mag_y_mg");
    e.mag_z_mg = number(s, "mag_z_mg");
    e.hdop = number(s, "hdop");
    e.vdop = number(s, "vdop");
    e.hacc_m = number(s, "hacc_m");
    e.vacc_m = number(s, "vacc_m");
    e.lat_std_m = number(s, "lat_std_m");
    e.lon_std_m = number(s, "lon_std_m");
    e.height_std_m = number(s, "height_std_m");
    e.diff_age_s = number(s, "diff_age_s");
    e.imu_packet_rate_hz = number(s, "epsilon_imu_packet_rate_hz");
    e.ahrs_packet_rate_hz = number(s, "epsilon_ahrs_packet_rate_hz");
    e.insgps_packet_rate_hz = number(s, "epsilon_insgps_packet_rate_hz");
    e.sys_state_packet_rate_hz = number(s, "epsilon_sys_state_packet_rate_hz");
    e.raw_gnss_packet_rate_hz = number(s, "epsilon_raw_gnss_packet_rate_hz");
    e.satellite_packet_rate_hz = number(s, "epsilon_satellite_packet_rate_hz");
    e.geodetic_packet_rate_hz = number(s, "epsilon_geodetic_packet_rate_hz");
    e.ecef_packet_rate_hz = number(s, "epsilon_ecef_packet_rate_hz");
    e.valid = flag(s, "epsilon_valid");
    e.heading_valid = flag(s, "heading_valid");
    e.gnss_fix_text = s.fields.value("gnss_fix").toStdString();
    e.error_message = s.fields.value("epsilon_error_message").toStdString();
    e.device_timestamp_us = s.fields.value("epsilon_device_timestamp_us").toULongLong();
    e.utc_unix_s = s.fields.value("epsilon_utc_unix_s").toULongLong();
    e.utc_microseconds = s.fields.value("epsilon_utc_microseconds").toUInt();
    e.attitude_source_count = s.fields.value("attitude_source_count").toInt();
    e.gnss_satellites = s.fields.value("gnss_satellites").toInt();
    e.system_status_bits = s.fields.value("system_status_bits").toUShort();
    e.filter_status_bits = s.fields.value("filter_status_bits").toUShort();
    e.update_status_bits = s.fields.value("update_status_bits").toUShort();
    e.timestamp = std::chrono::steady_clock::now();
    return e;
}

void showWave(TcpWavePanel& panel, const Sample& s)
{
    if (s.header.recordType == Raw::kRecordTypeHarmonicPair) {
        VaporView::Session::HarmonicLayout layout;
        if (!VaporView::Session::parseHarmonicLayout(s.bytes, s.bytes.size(), layout)) return;
        QVector<QVector<double>> waves;
        for (int component : {1, 0, 6}) {
            const auto samples = VaporView::Session::harmonicSamples(s.bytes, component);
            waves.append(QVector<double>(samples.begin(), samples.end()));
        }
        panel.setFpgaMode(true);
        panel.setFpgaWaveform(layout.channel, waves, {"H1", "H2", "H2/H1"}, "Session", layout.originalPoints);
        return;
    }
    Raw::WaveformPayloadLayout layout;
    if (!Raw::parseWaveformPayloadLayout(s.bytes, s.bytes.size(), &layout)) return;
    const auto raw = s.bytes.mid(layout.rawSignalOffset, layout.rawSignalSize);
    const auto harmonic = s.bytes.mid(layout.harmonicOffset, layout.harmonicSize);
    auto encoding = VaporView::tcpFloatEncodingFromRawDatFlags(s.header.flags);
    if (encoding == VaporView::TcpFloatEncoding::Unknown)
        encoding = VaporView::autoDetectTcpFloatEncoding(harmonic.isEmpty() ? raw : harmonic);
    panel.setFpgaMode(false);
    panel.injectRemoteRawSignalFrame(s.timestamp, VaporView::decodeTcpFloatPayload(raw, encoding));
    panel.injectRemoteSecondHarmonicFrame(s.timestamp, VaporView::decodeTcpFloatPayload(harmonic, encoding));
}
}

void MainWindow::resetReplayDisplays(bool replay)
{
    const bool changed = state_->session_replay_ != replay;
    state_->session_replay_ = replay;
#ifdef VAPORVIEW_HAS_OSGEARTH
    if (state_->map3d_controller_ && state_->map3d_controller_->window()) {
        state_->map3d_controller_->flush();
        state_->map3d_controller_->window()->clearTrack();
    }
#endif
    if (changed) {
        if (replay) invalidateFpgaMeasurements();
        updateUiTestModeUi();
        if (replay && state_->motion_page_) state_->motion_page_->session()->setUiTestScenario(true, true);
    }
    if (replay) state_->fpga_page_->setUiTestState(false);
    state_->current_epsilon_ = {};
    state_->current_gnss_ = {};
    state_->current_imu_ = {};
    state_->current_ptb_ = {};
    state_->current_lidar_ = {};
    state_->current_hc2a_ = {};
    state_->current_temperature_controller_ = {};
    state_->replay_navigation_ = {};
    state_->tcp_wave_panel_->setUiTestMode(false);
    state_->tcp_wave_panel_->setUiTestMode(true);
    state_->tcp_wave_panel_->setFpgaMode(!replay && isFpgaRelayPath());
    state_->device_panel_coordinator_->clearTemperatureHistory();
    state_->ai8_temperature_controller_panel_->clearTemperatureHistory();
    state_->ai8_temperature_controller_panel_->setBackendConnected(false);
    state_->ai8_temperature_controller_panel_->applyLiveData({});
    state_->ai8_temperature_overview_panel_->applyLiveData({});
    state_->device_panel_coordinator_->updateAllData({}, {}, 0, {}, 0, {}, {}, {}, {});
    state_->device_panel_coordinator_->clearRates();
    updateEnvironmentStatusIcons(false, false, false);
    if (state_->combination_navigation_page_) state_->combination_navigation_page_->refreshStatus();
    if (!replay) applyUiTestSnapshot();
}

void MainWindow::openUiTestSourceWindow()
{
    auto *window = new UiTestReplayWindow(this, state_->is_english_, state_->recording_directory_);
    state_->ui_test_source_window_ = window;
    window->exitMode = [this] { if (!state_->ui_test_application_closing_) setUiTestModeEnabled(false); };
    window->resetSource = [this](bool replay) { resetReplayDisplays(replay); };
    auto environmentDirty = std::make_shared<bool>(false);
    window->refresh = [this, environmentDirty] {
        if (!std::exchange(*environmentDirty, false)) return;
        state_->device_panel_coordinator_->updateEnvironmentData(state_->current_epsilon_, state_->current_ptb_,
            state_->current_hc2a_, state_->current_lidar_);
        state_->epsilon_config_panel_->setLivePacketRates(state_->current_epsilon_);
        updateEnvironmentStatusIcons(state_->current_lidar_.valid, state_->current_ptb_.valid, state_->current_hc2a_.valid);
    };
    window->present = [this, environmentDirty](const Sample& s) {
        auto& nav = state_->replay_navigation_;
        if (s.stream == Stream::Sensors) {
            auto& e = state_->current_epsilon_ = navigation(s);
            auto& ptb = state_->current_ptb_;
            ptb.pressure_hpa = number(s, "ptb_pressure_hpa");
            ptb.valid = std::isfinite(ptb.pressure_hpa);
            auto& hc = state_->current_hc2a_;
            hc.temperature = number(s, s.fields.contains("hc2a_temperature_c") ? "hc2a_temperature_c" : "hmp_temperature_c");
            hc.humidity = number(s, s.fields.contains("hc2a_humidity_rh") ? "hc2a_humidity_rh" : "hmp_humidity_rh");
            hc.valid = std::isfinite(hc.temperature) && std::isfinite(hc.humidity);
            auto& lidar = state_->current_lidar_;
            lidar.distance_m = number(s, s.fields.contains("sdfm300_distance_m") ? "sdfm300_distance_m" : "lidar_distance_m");
            lidar.valid = flag(s, s.fields.contains("sdfm300_valid") ? "sdfm300_valid" : "lidar_valid");
            lidar.timestamp = hc.timestamp = ptb.timestamp = e.timestamp;
            *environmentDirty = true;
            nav.epsilonOnline = nav.epsilonDataFresh = nav.navigationDataAvailable = e.valid;
            nav.positionAvailable = e.valid && std::isfinite(e.latitude_deg) && std::isfinite(e.longitude_deg);
            nav.latitudeDeg = e.latitude_deg;
            nav.longitudeDeg = e.longitude_deg;
            nav.heightM = e.height_m;
            nav.attitudeAvailable = e.valid && std::isfinite(e.roll_deg) && std::isfinite(e.pitch_deg);
            nav.rollDeg = e.roll_deg;
            nav.pitchDeg = e.pitch_deg;
            nav.headingDeg = e.yaw_deg;
            nav.gnssFixText = QString::fromStdString(e.gnss_fix_text);
            nav.satelliteCount = e.gnss_satellites;
            nav.horizontalAccuracyM = e.hacc_m;
#ifdef VAPORVIEW_HAS_OSGEARTH
            if (e.valid) maybeForwardMap3DSample(e, s.timestamp);
#endif
        } else if (s.stream == Stream::NavigationStatus) {
            nav.gnssFixCode = s.fields.value("gnss_fix_code").toInt();
            nav.navigationDataAvailable = flag(s, "navigation_available");
            nav.gnssQualityAvailable = nav.navigationDataAvailable;
            nav.filterStatusAvailable = !s.fields.value("filter_status_bits").isEmpty();
            nav.filterStatusBits = s.fields.value("filter_status_bits").toUShort();
            nav.updateStatusBits = s.fields.value("update_status_bits").toUShort();
            nav.rtkServiceRunning = flag(s, "rtk_service_running");
            nav.rtcm.available = nav.differentialAvailable = flag(s, "rtcm_available");
            nav.rtcmStatusText = s.fields.value("rtcm_health");
            nav.rtcm.bytesPerSecond = number(s, "rtcm_receive_bytes_per_s");
            nav.rtcm.ageMs = s.fields.value("rtcm_age_ms", "-1").toLongLong();
            nav.rtcm.remote = !s.fields.value("sky_boot_id").isEmpty();
            nav.rtcm.lossAvailable = flag(s, "rtcm_link_loss_available");
            nav.rtcm.lossPercent = number(s, "rtcm_link_loss_percent");
            nav.rtcm.receivedBytes = s.fields.value("rtcm_received_bytes").toULongLong();
            nav.rtcm.receivedChunks = s.fields.value("rtcm_received_chunks").toULongLong();
            nav.rtcm.droppedBytes = s.fields.value("rtcm_dropped_bytes").toULongLong();
            nav.rtcm.droppedChunks = s.fields.value("rtcm_dropped_chunks").toULongLong();
            nav.rtcm.linkFramesReceived = s.fields.value("rtcm_link_frames_received").toULongLong();
            nav.rtcm.linkFramesLost = s.fields.value("rtcm_link_frames_lost").toULongLong();
            for (auto health : {VaporView::RtcmHealth::Normal, VaporView::RtcmHealth::Disabled,
                    VaporView::RtcmHealth::Waiting, VaporView::RtcmHealth::Warning, VaporView::RtcmHealth::Local,
                    VaporView::RtcmHealth::Delayed, VaporView::RtcmHealth::Interrupted,
                    VaporView::RtcmHealth::LinkDisconnected, VaporView::RtcmHealth::StatusUnavailable}) {
                if (VaporView::Session::rtcmHealthCode(health) == nav.rtcmStatusText) nav.rtcm.health = health;
            }
        } else if (s.stream == Stream::Laser) {
            auto& data = state_->current_temperature_controller_;
            data.valid = flag(s, "valid");
            data.internal_temperature_c = number(s, "internal_temperature_c");
            data.error_code = s.fields.value("error_code").toUShort();
            for (int i = 0; i < 2; ++i) {
                const auto prefix = QStringLiteral("ch%1_").arg(i + 1);
                auto& ch = data.channels[i];
                ch.target_temperature_c = number(s, prefix + "target_c");
                ch.measured_temperature_c = number(s, prefix + "measured_c");
                ch.output_current_a = number(s, prefix + "output_current_a");
                ch.output_percent = number(s, prefix + "output_percent");
                ch.output_enabled = flag(s, prefix + "enabled");
                ch.output_mode = s.fields.value(prefix + "mode").toInt();
                ch.max_output_percent = s.fields.value(prefix + "max_output_percent").toInt();
                ch.kp = s.fields.value(prefix + "kp").toInt();
                ch.ki = s.fields.value(prefix + "ki").toInt();
                ch.kd = s.fields.value(prefix + "kd").toInt();
            }
            state_->device_panel_coordinator_->updateTemperatureData(data, s.timestamp / 1e6);
        } else if (s.stream == Stream::System) {
            VaporView::Ai8TemperatureControllerProtocol::LiveData data;
            data.valid = flag(s, "valid");
            data.errorMessage = s.fields.value("error_message");
            data.controlStatesValid = flag(s, "control_states_valid");
            data.alarmStatusValid = flag(s, "alarm_status_valid");
            data.mainStatusValid = flag(s, "main_status_valid");
            data.mainStatusRaw = s.fields.value("main_status_raw").toUShort();
            for (int i = 0; i < 8; ++i) {
                data.measuredC[i] = number(s, QStringLiteral("ch%1_measured_c").arg(i + 1));
                data.controlStates[i] = static_cast<VaporView::Ai8TemperatureControllerProtocol::ChannelControlState>(
                    s.fields.value(QStringLiteral("ch%1_control_state").arg(i + 1)).toInt());
            }
            for (int i = 0; i < data.alarmStatusRegisters.size(); ++i)
                data.alarmStatusRegisters[i] = s.fields.value(QStringLiteral("alarm_status_reg%1").arg(i + 1)).toUShort();
            state_->ai8_temperature_controller_panel_->setBackendConnected(data.valid, "Session");
            state_->ai8_temperature_controller_panel_->applyLiveData(data, s.timestamp / 1e6);
            state_->ai8_temperature_overview_panel_->applyLiveData(data);
        } else if (s.stream == Stream::FpgaWave) {
            VaporView::FpgaWave::CompletedStream wave;
            if (VaporView::FpgaRemote::parseWaveformPreview(s.bytes, wave)) {
                state_->tcp_wave_panel_->setFpgaMode(true);
                state_->fpga_page_->updateWaveform(wave);
            }
        } else if (s.stream == Stream::FpgaSensor) {
            VaporView::FpgaSensor::Reading reading;
            if (VaporView::FpgaRemote::parseSensor(s.bytes, reading)) state_->fpga_page_->updateSensor(reading);
        } else if (s.stream == Stream::Raw && s.header.sourceId == Raw::kSourceWaveform) {
            showWave(*state_->tcp_wave_panel_, s);
        } else if (s.stream == Stream::Raw) {
            switch (s.header.sourceId) {
            case Raw::kSourceNavigation: {
                const auto r = VaporView::FpgaSensor::FpgaSensorDecoder::decode(s.bytes, 0x42);
                if (!r.validity.structure || !r.validity.crc || !r.epsilonMessageId) return;
                auto& e = state_->current_epsilon_;
                e.timestamp = std::chrono::steady_clock::now();
                if (!VaporView::EpsilonProtocol::decodeCorePacket(e, *r.epsilonMessageId,
                        reinterpret_cast<const uint8_t*>(r.epsilonData.constData()), r.epsilonData.size())) return;
                e.valid = true;
                break;
            }
            case Raw::kSourcePressure: {
                auto& pressure = state_->current_ptb_;
                pressure.pressure_hpa = s.bytes.trimmed().toDouble(&pressure.valid);
                break;
            }
            case Raw::kSourceDistance:
                if (s.header.recordType != VaporView::Sdfm300::kRecordType) return;
                state_->current_lidar_ = {};
                VaporView::Sdfm300::decodeFrame(reinterpret_cast<const uint8_t*>(s.bytes.constData()),
                                                s.bytes.size(), state_->current_lidar_);
                break;
            case Raw::kSourceTemperatureHumidity: {
                if (s.header.recordType != Raw::kRecordTypeTemperatureHumidityRoAsciiResponse) return;
                VaporView::Hc2aProtocol::Reading reading{};
                auto& hc = state_->current_hc2a_;
                hc.valid = VaporView::Hc2aProtocol::decode(s.bytes.toStdString(), 99, reading) && !reading.alarms;
                hc.temperature = reading.temperature;
                hc.humidity = reading.humidity;
                break;
            }
            default: return;
            }
            *environmentDirty = true;
        } else if (s.stream == Stream::LegacyWave) {
            state_->tcp_wave_panel_->setFpgaMode(false);
            state_->tcp_wave_panel_->injectRemoteSecondHarmonicFrame(s.timestamp, s.legacyWave);
        } else if (s.stream == Stream::Feature || s.stream == Stream::Peak) {
            VaporView::WaveformFeature feature;
            feature.host_time_us = s.timestamp;
            feature.peak = number(s, s.stream == Stream::Feature ? "peak" : "peak_value");
            feature.rms = number(s, "rms");
            feature.search_start_index = s.fields.value("search_start_index").toUInt();
            feature.search_end_index = s.fields.value("search_end_index").toUInt();
            feature.peak_index = number(s, "peak_index");
            feature.quality_flags = s.fields.value("quality_flags").toUInt();
            const auto channel = s.fields.value("channel_id").toUShort();
            if (channel == 0x30 || channel == 0x31) {
                state_->tcp_wave_panel_->setFpgaMode(true);
                state_->tcp_wave_panel_->setFpgaSecondHarmonicPeak(channel, s.fields.value("cycle_id").toUInt(),
                    s.timestamp, feature.quality_flags ? qQNaN() : feature.peak, false);
            } else state_->tcp_wave_panel_->injectRemoteWaveformFeature(feature);
        }
    };
    window->show();
}
