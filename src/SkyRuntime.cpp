#include "SkyRuntime.h"

#include "SerialTelemetryLink.h"
#include "TcpTelemetryLink.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStorageInfo>
#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>

namespace VaporView
{
namespace
{
int intervalMs(double hz)
{
    return std::max(1, static_cast<int>(1000.0 / std::max(0.001, hz)));
}

bool connectedAndFresh(const DeviceStatusItem& status, quint64 nowUs, quint64 timeoutUs)
{
    return status.state == DeviceState::Connected &&
           status.last_data_time_us > 0 &&
           nowUs >= status.last_data_time_us &&
           nowUs - status.last_data_time_us <= timeoutUs;
}

CommandAck makeAck(const CommandMessage& command, CommandErrorCode errorCode = CommandErrorCode::Ok)
{
    CommandAck ack;
    ack.command_id = command.command_id;
    ack.command_seq = command.command_seq;
    ack.result = errorCode == CommandErrorCode::Ok ? 0 : 1;
    ack.error_code = errorCode;
    return ack;
}

QString defaultConfigPath()
{
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("sky_config.json"));
}

QString locateRepositoryRoot()
{
    QDir dir(QCoreApplication::applicationDirPath());
    for (int i = 0; i < 6; ++i)
    {
        if (QFileInfo::exists(dir.filePath(QStringLiteral("CMakeLists.txt"))) &&
            QFileInfo::exists(dir.filePath(QStringLiteral("README.md"))))
        {
            return dir.path();
        }
        if (!dir.cdUp())
        {
            break;
        }
    }
    return QString();
}

QString defaultRecordingDirectory()
{
    const QString repositoryRoot = locateRepositoryRoot();
    if (!repositoryRoot.isEmpty())
    {
        return QDir(repositoryRoot).filePath(QStringLiteral("data"));
    }
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("data"));
}

DownsampledWaveform makeDownsampledWaveform(const QVector<float>& samples,
                                            quint64 hostTimeUs,
                                            quint64 epsilonTimeUs,
                                            quint16 channelId,
                                            int ratio)
{
    DownsampledWaveform waveform;
    waveform.host_time_us = hostTimeUs;
    waveform.epsilon_time_us = epsilonTimeUs;
    waveform.original_point_count = static_cast<quint32>(samples.size());
    waveform.channel_id = channelId;
    waveform.sample_format = 1;
    waveform.x_start = 0.0f;
    waveform.x_step = static_cast<float>(ratio);
    waveform.samples.reserve((samples.size() + ratio - 1) / ratio);
    for (int i = 0; i < samples.size(); i += ratio)
    {
        waveform.samples.push_back(samples.at(i));
    }
    waveform.downsampled_point_count = static_cast<quint32>(waveform.samples.size());
    return waveform;
}

}  // namespace

SkyRuntime::SkyRuntime(const SkyRuntimeOptions& options, QObject *parent)
    : QObject(parent)
    , options_(options)
    , codec_(1024u * 1024u)
    , device_manager_(this)
{
    connect(&device_manager_, &SkyDeviceManager::logMessage, this, &SkyRuntime::logMessage);
    connect(&device_manager_, &SkyDeviceManager::epsilonRawFrameReceived, this,
            [this](quint64 timestampUs, quint8 packetId, quint8 serialNumber, const QByteArray& frame) {
                session_recorder_.recordRawEpsilonFrame(timestampUs, packetId, serialNumber, frame);
            });
    connect(&device_manager_, &SkyDeviceManager::ptbRawResponseReceived, this,
            [this](quint64 timestampUs, const QByteArray& response) {
                session_recorder_.recordRawPtbResponse(timestampUs, response);
            });
    connect(&device_manager_, &SkyDeviceManager::hmpRawResponseReceived, this,
            [this](quint64 timestampUs, const QByteArray& response) {
                session_recorder_.recordRawHmpResponse(timestampUs, response);
            });
    connect(&device_manager_, &SkyDeviceManager::lidarRawFrameReceived, this,
            [this](quint64 timestampUs, quint16 protocol, const QByteArray& frame) {
                session_recorder_.recordRawLidarFrame(timestampUs, protocol, frame);
            });
    connect(&device_manager_, &SkyDeviceManager::tcpRawWaveFrameReceived, this,
            [this](quint64 timestampUs,
                   const QByteArray& rawPayload,
                   const QByteArray& harmonicPayload,
                   TcpFloatEncoding floatEncoding) {
                session_recorder_.recordRawTcpWaveFrame(timestampUs, rawPayload, harmonicPayload, floatEncoding);
            });
    connect(&device_manager_, &SkyDeviceManager::temperatureControllerDataUpdated, this,
            [this](const TemperatureControllerData&) {
                sendTemperatureControllerStatus();
            });
    connect(&basic_timer_, &QTimer::timeout, this, &SkyRuntime::sendBasicTelemetry);
    connect(&feature_timer_, &QTimer::timeout, this, &SkyRuntime::sendWaveformFeature);
    connect(&waveform_timer_, &QTimer::timeout, this, &SkyRuntime::sendDownsampledWaveform);
    connect(&heartbeat_timer_, &QTimer::timeout, this, &SkyRuntime::sendHeartbeat);
    connect(&status_timer_, &QTimer::timeout, this, &SkyRuntime::sendTelemetryStatus);
    basic_timer_.setTimerType(Qt::PreciseTimer);
    feature_timer_.setTimerType(Qt::PreciseTimer);
    waveform_timer_.setTimerType(Qt::PreciseTimer);
    heartbeat_timer_.setTimerType(Qt::PreciseTimer);
    status_timer_.setTimerType(Qt::PreciseTimer);
}

SkyRuntime::~SkyRuntime()
{
    stop();
}

bool SkyRuntime::start()
{
    if (running_)
    {
        return true;
    }

    SkyConfig config;
    QString configError;
    const QString configPath = options_.config_path.isEmpty() ? defaultConfigPath() : options_.config_path;
    if (!SkyConfig::loadFromFile(configPath, config, &configError))
    {
        emit logMessage(QStringLiteral("Failed to load sky config, using defaults: %1").arg(configError));
        config = SkyConfig::defaults();
    }
    if (!options_.wave_host.isEmpty())
    {
        config.wave_tcp.host = options_.wave_host;
        config.wave_tcp.port = options_.wave_port;
    }

    device_manager_.loadConfig(config);
    device_manager_.setSimulateData(options_.simulate_data);

    auto attachLinkSignals = [this](TelemetryLink *link) {
        connect(link, &TelemetryLink::bytesReceived, this, [this](const QByteArray& bytes) {
            onBytesReceived(bytes);
        });
        connect(link, &TelemetryLink::errorOccurred, this, &SkyRuntime::logMessage);
        connect(link, &TelemetryLink::statusMessage, this, &SkyRuntime::logMessage);
    };

    if (options_.telemetry_transport == TelemetryTransportType::Serial)
    {
        if (options_.telemetry_port.trimmed().isEmpty())
        {
            emit logMessage(QStringLiteral("Telemetry serial port is empty"));
            return false;
        }
        auto link = std::make_unique<SerialTelemetryLink>();
        attachLinkSignals(link.get());
        if (!link->open(options_.telemetry_port, options_.telemetry_baud))
        {
            emit logMessage(QStringLiteral("Failed to open telemetry port %1").arg(options_.telemetry_port));
            return false;
        }
        link_ = std::move(link);
    }
    else
    {
        auto link = std::make_unique<TcpTelemetryLink>();
        attachLinkSignals(link.get());
        if (!link->listen(options_.telemetry_host, static_cast<quint16>(options_.telemetry_tcp_port)))
        {
            emit logMessage(QStringLiteral("Failed to listen telemetry TCP endpoint %1:%2")
                                .arg(options_.telemetry_host)
                                .arg(options_.telemetry_tcp_port));
            return false;
        }
        link_ = std::move(link);
    }

    device_manager_.connectAll();
    updateTimerIntervals();
    basic_timer_.start();
    feature_timer_.start();
    waveform_timer_.start();
    heartbeat_timer_.start();
    status_timer_.start();
    running_ = true;
    started_time_us_ = currentTimestampUs();
    emit runningChanged(true);
    emit logMessage(QStringLiteral("SkyRuntime started on %1").arg(link_ ? link_->endpointDescription() : QString()));
    return true;
}

void SkyRuntime::stop()
{
    if (!running_)
    {
        return;
    }

    basic_timer_.stop();
    feature_timer_.stop();
    waveform_timer_.stop();
    heartbeat_timer_.stop();
    status_timer_.stop();

    if (session_recorder_.isRecording() || session_recorder_.isPaused())
    {
        QString error;
        if (!session_recorder_.stop(&error))
        {
            emit logMessage(QStringLiteral("Failed to save sky recording metadata while stopping: %1").arg(error));
        }
    }

    device_manager_.setSimulateData(false);
    device_manager_.disconnectAll();
    if (link_)
    {
        link_->close();
        link_.reset();
    }
    running_ = false;
    started_time_us_ = 0;
    last_sent_feature_time_us_ = 0;
    emit runningChanged(false);
    emit logMessage(QStringLiteral("SkyRuntime stopped"));
}

bool SkyRuntime::isRunning() const
{
    return running_;
}

bool SkyRuntime::connectDevice(SkyDeviceId id, CommandErrorCode *error)
{
    return device_manager_.connectDevice(id, error);
}

bool SkyRuntime::disconnectDevice(SkyDeviceId id, CommandErrorCode *error)
{
    return device_manager_.disconnectDevice(id, error);
}

bool SkyRuntime::reconnectDevice(SkyDeviceId id, CommandErrorCode *error)
{
    return device_manager_.reconnectDevice(id, error);
}

void SkyRuntime::connectAllDevices()
{
    device_manager_.connectAll();
}

void SkyRuntime::disconnectAllDevices()
{
    device_manager_.disconnectAll();
}

void SkyRuntime::reconnectAllDevices()
{
    device_manager_.reconnectAll();
}

bool SkyRuntime::startRecording(QString *error)
{
    if (session_recorder_.isRecording())
    {
        if (error) *error = QStringLiteral("recording already started");
        return false;
    }
    const QString transport = telemetryTransportName(options_.telemetry_transport);
    const QString endpoint = link_ ? link_->endpointDescription() : QString();
    const QString legacyPort = options_.telemetry_transport == TelemetryTransportType::Tcp
        ? endpoint
        : options_.telemetry_port;
    const int legacyBaud = options_.telemetry_transport == TelemetryTransportType::Tcp
        ? 0
        : options_.telemetry_baud;
    if (!session_recorder_.start(defaultRecordingDirectory(), legacyPort, legacyBaud, error, transport, endpoint))
    {
        return false;
    }
    emit logMessage(QStringLiteral("Sky recording started: %1").arg(session_recorder_.sessionDirectory()));
    return true;
}

bool SkyRuntime::pauseRecording(QString *error)
{
    if (!session_recorder_.isRecording() && !session_recorder_.isPaused())
    {
        if (error) *error = QStringLiteral("recording not started");
        return false;
    }
    session_recorder_.pause();
    return true;
}

bool SkyRuntime::stopRecording(QString *error)
{
    if (!session_recorder_.isRecording() && !session_recorder_.isPaused())
    {
        if (error) *error = QStringLiteral("recording not started");
        return false;
    }
    if (!session_recorder_.stop(error))
    {
        emit logMessage(QStringLiteral("Failed to save sky recording metadata: %1")
                            .arg(error ? *error : QString()));
        return false;
    }
    emit logMessage(QStringLiteral("Sky recording stopped"));
    return true;
}

TelemetryStatus SkyRuntime::currentStatus() const
{
    TelemetryStatus status;
    status.recording_state = session_recorder_.recordingState();
    status.session_name = session_recorder_.sessionName();
    status.disk_free_bytes = static_cast<quint64>(QStorageInfo(defaultRecordingDirectory()).bytesAvailable());
    status.telemetry_basic_rate_hz = static_cast<float>(device_manager_.config().telemetry.basic_rate_hz);
    status.waveform_rate_hz = static_cast<float>(device_manager_.config().telemetry.waveform_rate_hz);
    status.feature_rate_hz = static_cast<float>(device_manager_.config().telemetry.feature_rate_hz);
    status.heartbeat_rate_hz = static_cast<float>(device_manager_.config().telemetry.heartbeat_rate_hz);
    status.status_rate_hz = static_cast<float>(device_manager_.config().telemetry.status_rate_hz);
    status.rx_total_frames = rx_total_frames_;
    status.crc_error_count = static_cast<quint32>(codec_.crcErrorCount());
    status.current_seq = next_frame_seq_;
    status.last_frame_time_us = last_frame_time_us_;
    status.devices = device_manager_.allStatuses();
    status.wave_tcp_actual_rate_hz = static_cast<float>(device_manager_.waveTcpActualRateHz());
    status.recording_start_time_us = session_recorder_.recordingStartTimeUs();
    status.recording_elapsed_ms = session_recorder_.recordingElapsedMs();
    status.telemetry_record_count = session_recorder_.telemetryRecordCount();
    status.waveform_feature_record_count = session_recorder_.waveformFeatureRecordCount() + session_recorder_.temperatureControllerRecordCount();
    status.waveform_snapshot_record_count = session_recorder_.waveformSnapshotRecordCount();
    status.raw_epsilon_record_count = session_recorder_.rawEpsilonRecordCount();
    status.raw_ptb_record_count = session_recorder_.rawPtbRecordCount();
    status.raw_hmp_record_count = session_recorder_.rawHmpRecordCount();
    status.raw_lidar_record_count = session_recorder_.rawLidarRecordCount();
    status.raw_tcp_wave_record_count = session_recorder_.rawTcpWaveRecordCount();
    return status;
}

SkyConfig SkyRuntime::currentConfig() const
{
    return device_manager_.config();
}

SkyDashboardSnapshot SkyRuntime::dashboardSnapshot() const
{
    SkyDashboardSnapshot snapshot;
    const quint64 nowUs = currentTimestampUs();
    const SkyConfig config = device_manager_.config();
    snapshot.host_time_us = nowUs;
    snapshot.uptime_ms = started_time_us_ > 0 && nowUs >= started_time_us_ ? (nowUs - started_time_us_) / 1000ULL : 0;
    snapshot.epsilon = device_manager_.latestEpsilon();
    snapshot.ptb = device_manager_.latestPtb();
    snapshot.hmp = device_manager_.latestHmp();
    snapshot.lidar = device_manager_.latestLidar();
    snapshot.waveform_feature = device_manager_.latestWaveformFeature();
    const QVector<float> rawWaveform = device_manager_.latestRawWaveform();
    const QVector<float> harmonicWaveform = device_manager_.latestWaveform();
    snapshot.latest_raw_waveform_preview = waveformPreview(rawWaveform, 2048);
    snapshot.latest_harmonic_waveform_preview = waveformPreview(harmonicWaveform, 2048);
    snapshot.peak_trend = peak_trend_;
    snapshot.telemetry_status = currentStatus();
    snapshot.epsilon_acquisition_rate_hz = snapshot.epsilon.imu_packet_rate_hz > 0.0
                                               ? snapshot.epsilon.imu_packet_rate_hz
                                               : config.epsilon.frequency_hz;
    snapshot.ptb_acquisition_rate_hz = config.ptb.frequency_hz;
    snapshot.hmp_acquisition_rate_hz = config.hmp.frequency_hz;
    snapshot.lidar_acquisition_rate_hz = config.lidar.frequency_hz;
    snapshot.wave_tcp_acquisition_rate_hz = device_manager_.waveTcpActualRateHz();
    snapshot.devices_csv_recording_rate_hz = config.telemetry.basic_rate_hz;
    snapshot.raw_wave_recording_rate_hz = snapshot.wave_tcp_acquisition_rate_hz;
    snapshot.telemetry_basic_rate_hz = config.telemetry.basic_rate_hz;
    snapshot.waveform_feature_rate_hz = config.telemetry.feature_rate_hz;
    snapshot.waveform_downsampled_rate_hz = config.telemetry.waveform_rate_hz;
    snapshot.epsilon_stale = deviceStale(SkyDeviceId::Epsilon, nowUs, 2'000'000ULL);
    snapshot.ptb_stale = deviceStale(SkyDeviceId::Ptb, nowUs, 3'000'000ULL);
    snapshot.hmp_stale = deviceStale(SkyDeviceId::Hmp, nowUs, 3'000'000ULL);
    snapshot.lidar_stale = deviceStale(SkyDeviceId::Lidar, nowUs, 2'000'000ULL);
    snapshot.waveform_stale = deviceStale(SkyDeviceId::WaveTcp, nowUs, 3'000'000ULL);
    return snapshot;
}

QVector<DownsampledWaveform> SkyRuntime::currentDownsampledWaveforms() const
{
    QVector<DownsampledWaveform> waveforms;
    const quint64 hostTimeUs = currentTimestampUs();
    if (!connectedAndFresh(device_manager_.status(SkyDeviceId::WaveTcp), hostTimeUs, 3'000'000ULL))
    {
        return waveforms;
    }

    const QVector<float> rawSamples = device_manager_.latestRawWaveform();
    const QVector<float> harmonicSamples = device_manager_.latestWaveform();
    if (rawSamples.isEmpty() && harmonicSamples.isEmpty())
    {
        return waveforms;
    }

    const quint64 epsilonTimeUs = device_manager_.latestEpsilon().device_timestamp_us;
    const int ratio = std::max(1, device_manager_.config().wave_tcp.downsample_ratio);
    if (!rawSamples.isEmpty())
    {
        waveforms.push_back(makeDownsampledWaveform(rawSamples, hostTimeUs, epsilonTimeUs, 1, ratio));
    }
    if (!harmonicSamples.isEmpty())
    {
        waveforms.push_back(makeDownsampledWaveform(harmonicSamples, hostTimeUs, epsilonTimeUs, 4, ratio));
    }
    return waveforms;
}

void SkyRuntime::setWaveformStreamingEnabled(bool enabled)
{
    waveform_streaming_enabled_ = enabled;
}

bool SkyRuntime::waveformStreamingEnabled() const
{
    return waveform_streaming_enabled_;
}

void SkyRuntime::sendOneWaveformNow()
{
    sendDownsampledWaveformFrame(false);
}

void SkyRuntime::onBytesReceived(const QByteArray& bytes)
{
    const QVector<TelemetryFrame> frames = codec_.feedBytes(bytes);
    for (const TelemetryFrame& frame : frames)
    {
        dispatchFrame(frame);
    }
}

void SkyRuntime::sendBasicTelemetry()
{
    const quint64 nowUs = currentTimestampUs();
    const EpsilonData epsilon = device_manager_.latestEpsilon();
    const PtbData ptb = device_manager_.latestPtb();
    const HmpData hmp = device_manager_.latestHmp();
    const LidarData lidar = device_manager_.latestLidar();

    TelemetryBasic data;
    data.host_time_us = nowUs;
    data.latitude_deg = std::numeric_limits<double>::quiet_NaN();
    data.longitude_deg = std::numeric_limits<double>::quiet_NaN();
    data.height_m = std::numeric_limits<double>::quiet_NaN();
    data.ecef_x_m = std::numeric_limits<double>::quiet_NaN();
    data.ecef_y_m = std::numeric_limits<double>::quiet_NaN();
    data.ecef_z_m = std::numeric_limits<double>::quiet_NaN();
    data.lidar_height_m = std::numeric_limits<float>::quiet_NaN();
    data.temperature_c = std::numeric_limits<float>::quiet_NaN();
    data.humidity_percent = std::numeric_limits<float>::quiet_NaN();
    data.pressure_hpa = std::numeric_limits<float>::quiet_NaN();

    const DeviceStatusItem epsilonStatus = device_manager_.status(SkyDeviceId::Epsilon);
    const DeviceStatusItem lidarStatus = device_manager_.status(SkyDeviceId::Lidar);
    const DeviceStatusItem hmpStatus = device_manager_.status(SkyDeviceId::Hmp);
    const DeviceStatusItem ptbStatus = device_manager_.status(SkyDeviceId::Ptb);
    const bool hasEpsilon = epsilon.valid && connectedAndFresh(epsilonStatus, nowUs, 2'000'000ULL);
    const bool hasLidar = lidar.valid && connectedAndFresh(lidarStatus, nowUs, 2'000'000ULL);
    const bool hasHmp = hmp.valid && connectedAndFresh(hmpStatus, nowUs, 3'000'000ULL);
    const bool hasPtb = ptb.valid && connectedAndFresh(ptbStatus, nowUs, 3'000'000ULL);

    if (hasEpsilon)
    {
        data.validity_flags |= BasicHasEpsilonTime | BasicHasPosition | BasicHasEcef;
        data.epsilon_time_us = epsilon.device_timestamp_us;
        data.latitude_deg = epsilon.latitude_deg;
        data.longitude_deg = epsilon.longitude_deg;
        data.height_m = epsilon.height_m;
        data.ecef_x_m = epsilon.ecef_x_m;
        data.ecef_y_m = epsilon.ecef_y_m;
        data.ecef_z_m = epsilon.ecef_z_m;
        data.status_bits = epsilon.system_status_bits;
        data.filter_status_bits = epsilon.filter_status_bits;
        data.update_status_bits = epsilon.update_status_bits;
        data.gnss_fix_code = static_cast<quint8>(std::clamp(epsilon.gnss_fix_code, 0, 255));
        data.validity_flags |= BasicHasGnssQuality | BasicHasNedVelocity | BasicHasImu |
                               BasicHasAttitude | BasicHasEpsilonDiagnostics;
        data.gnss_satellites = static_cast<quint16>(std::clamp(epsilon.gnss_satellites, 0, 65535));
        data.hdop = static_cast<float>(epsilon.hdop);
        data.vdop = static_cast<float>(epsilon.vdop);
        data.hacc_m = static_cast<float>(epsilon.hacc_m);
        data.vacc_m = static_cast<float>(epsilon.vacc_m);
        data.heading_valid = epsilon.heading_valid;
        data.vel_n_mps = static_cast<float>(epsilon.vel_n_mps);
        data.vel_e_mps = static_cast<float>(epsilon.vel_e_mps);
        data.vel_d_mps = static_cast<float>(epsilon.vel_d_mps);
        data.imu_acc_x_mps2 = static_cast<float>(epsilon.imu_acc_x_mps2);
        data.imu_acc_y_mps2 = static_cast<float>(epsilon.imu_acc_y_mps2);
        data.imu_acc_z_mps2 = static_cast<float>(epsilon.imu_acc_z_mps2);
        data.imu_gyr_x_radps = static_cast<float>(epsilon.imu_gyr_x_radps);
        data.imu_gyr_y_radps = static_cast<float>(epsilon.imu_gyr_y_radps);
        data.imu_gyr_z_radps = static_cast<float>(epsilon.imu_gyr_z_radps);
        data.roll_deg = static_cast<float>(epsilon.roll_deg);
        data.pitch_deg = static_cast<float>(epsilon.pitch_deg);
        data.yaw_deg = static_cast<float>(epsilon.yaw_deg);
        data.raw_frame_count = epsilon.raw_frame_count;
        data.dropped_frame_count = epsilon.dropped_frame_count;
        data.imu_packet_rate_hz = static_cast<float>(epsilon.imu_packet_rate_hz);
        data.ahrs_packet_rate_hz = static_cast<float>(epsilon.ahrs_packet_rate_hz);
        data.insgps_packet_rate_hz = static_cast<float>(epsilon.insgps_packet_rate_hz);
        data.sys_state_packet_rate_hz = static_cast<float>(epsilon.sys_state_packet_rate_hz);
        data.raw_gnss_packet_rate_hz = static_cast<float>(epsilon.raw_gnss_packet_rate_hz);
        data.satellite_packet_rate_hz = static_cast<float>(epsilon.satellite_packet_rate_hz);
        data.geodetic_packet_rate_hz = static_cast<float>(epsilon.geodetic_packet_rate_hz);
        data.ecef_packet_rate_hz = static_cast<float>(epsilon.ecef_packet_rate_hz);
    }

    if (hasLidar)
    {
        data.validity_flags |= BasicHasLidar | BasicHasLidarStrength;
        data.lidar_height_m = static_cast<float>(lidar.distance_m);
        data.lidar_signal_strength = lidar.signal_strength;
    }

    if (hasHmp)
    {
        data.validity_flags |= BasicHasTemperature | BasicHasHumidity;
        data.temperature_c = static_cast<float>(hmp.temperature);
        data.humidity_percent = static_cast<float>(hmp.humidity);
    }

    if (hasPtb)
    {
        data.validity_flags |= BasicHasPressure;
        data.pressure_hpa = static_cast<float>(ptb.pressure_hpa);
    }

    session_recorder_.recordDeviceSnapshot(nowUs,
                                           epsilonStatus.last_data_time_us != 0 ? epsilonStatus.last_data_time_us : nowUs,
                                           epsilon,
                                           hasEpsilon,
                                           ptb,
                                           hasPtb,
                                           hmp,
                                           hasHmp,
                                           lidar,
                                           hasLidar);
    sendFrame(MsgType::TelemetryBasic, TelemetryCodec::serializeBasicTelemetry(data));
}

void SkyRuntime::sendWaveformFeature()
{
    const quint64 nowUs = currentTimestampUs();
    if (!connectedAndFresh(device_manager_.status(SkyDeviceId::WaveTcp), nowUs, 3'000'000ULL))
    {
        return;
    }
    const WaveformFeature feature = device_manager_.latestWaveformFeature();
    if (feature.host_time_us == 0 || feature.host_time_us == last_sent_feature_time_us_)
    {
        return;
    }
    last_sent_feature_time_us_ = feature.host_time_us;
    if (feature.quality_flags != 0 || !std::isfinite(feature.peak))
    {
        sendFrame(MsgType::WaveformFeature, TelemetryCodec::serializeWaveformFeature(feature));
        return;
    }
    peak_trend_.push_back(feature.peak);
    while (peak_trend_.size() > 256)
    {
        peak_trend_.removeFirst();
    }
    session_recorder_.recordWaveformFeature(feature);
    sendFrame(MsgType::WaveformFeature, TelemetryCodec::serializeWaveformFeature(feature));
}

void SkyRuntime::sendDownsampledWaveform()
{
    sendDownsampledWaveformFrame(true);
}

void SkyRuntime::sendDownsampledWaveformFrame(bool honorStreamingEnabled)
{
    const quint64 hostTimeUs = currentTimestampUs();
    if (!connectedAndFresh(device_manager_.status(SkyDeviceId::WaveTcp), hostTimeUs, 3'000'000ULL))
    {
        return;
    }
    const QVector<float> rawSamples = device_manager_.latestRawWaveform();
    const QVector<float> harmonicSamples = device_manager_.latestWaveform();
    if (rawSamples.isEmpty() && harmonicSamples.isEmpty())
    {
        return;
    }
    const quint64 epsilonTimeUs = device_manager_.latestEpsilon().device_timestamp_us;
    if (!harmonicSamples.isEmpty())
    {
        session_recorder_.recordWaveformSnapshot(hostTimeUs, epsilonTimeUs, rawSamples, harmonicSamples);
    }
    if (honorStreamingEnabled && !waveform_streaming_enabled_)
    {
        return;
    }
    const int ratio = std::max(1, device_manager_.config().wave_tcp.downsample_ratio);
    if (!rawSamples.isEmpty())
    {
        const DownsampledWaveform rawWaveform =
            makeDownsampledWaveform(rawSamples, hostTimeUs, epsilonTimeUs, 1, ratio);
        sendFrame(MsgType::WaveformDownsampled, TelemetryCodec::serializeDownsampledWaveform(rawWaveform));
    }
    if (!harmonicSamples.isEmpty())
    {
        const DownsampledWaveform harmonicWaveform =
            makeDownsampledWaveform(harmonicSamples, hostTimeUs, epsilonTimeUs, 4, ratio);
        sendFrame(MsgType::WaveformDownsampled, TelemetryCodec::serializeDownsampledWaveform(harmonicWaveform));
    }
}

void SkyRuntime::sendHeartbeat()
{
    QByteArray payload;
    payload.append(TelemetryCodec::serializeRatePayload(1));
    sendFrame(MsgType::Heartbeat, payload);
}

void SkyRuntime::sendTelemetryStatus()
{
    const TelemetryStatus status = currentStatus();
    sendFrame(MsgType::TelemetryStatus, TelemetryCodec::serializeTelemetryStatus(status));
}

void SkyRuntime::sendTemperatureControllerStatus()
{
    const DeviceStatusItem status = device_manager_.status(SkyDeviceId::TemperatureController);
    const quint64 nowUs = currentTimestampUs();
    if (!connectedAndFresh(status, nowUs, 3'000'000ULL))
    {
        return;
    }
    const TemperatureControllerData data = device_manager_.latestTemperatureController();
    session_recorder_.recordTemperatureControllerStatus(nowUs, data);
    sendFrame(MsgType::TemperatureControllerStatus, TelemetryCodec::serializeTemperatureControllerStatus(data));
}

void SkyRuntime::dispatchFrame(const TelemetryFrame& frame)
{
    ++rx_total_frames_;
    last_frame_time_us_ = currentTimestampUs();
    if (frame.type == MsgType::Command)
    {
        CommandMessage command;
        if (TelemetryCodec::parseCommand(frame.payload, command))
        {
            handleCommand(command);
        }
    }
}

void SkyRuntime::handleCommand(const CommandMessage& command)
{
    const SkyCommandResult result = executeCommand(command);
    sendAck(result.ack);
    sendCommandResultFrames(result);
}

SkyCommandResult SkyRuntime::executeCommand(const CommandMessage& command)
{
    SkyCommandResult result;
    result.ack = makeAck(command);

    auto deviceCommand = [&](auto method) {
        SkyDeviceId id = SkyDeviceId::All;
        if (!TelemetryCodec::parseDeviceCommand(command.payload, id))
        {
            result.ack = makeAck(command, CommandErrorCode::InvalidPayload);
            return result;
        }
        CommandErrorCode error = CommandErrorCode::Ok;
        const bool ok = method(id, &error);
        result.ack = makeAck(command, ok ? CommandErrorCode::Ok : error);
        result.send_status = true;
        return result;
    };

    auto temperatureCommand = [&](auto method, bool requireChannel = true) {
        TemperatureControllerCommand request;
        if (!TelemetryCodec::parseTemperatureControllerCommand(command.payload, request) ||
            (requireChannel && (request.channel < 1 || request.channel > 2)))
        {
            result.ack = makeAck(command, CommandErrorCode::InvalidPayload);
            return result;
        }
        if (device_manager_.status(SkyDeviceId::TemperatureController).state != DeviceState::Connected)
        {
            result.ack = makeAck(command, CommandErrorCode::DeviceNotConnected);
            return result;
        }
        const bool ok = method(request);
        result.ack = makeAck(command, ok ? CommandErrorCode::Ok : CommandErrorCode::ConfigApplyFailed);
        result.send_status = true;
        return result;
    };

    switch (command.command_id)
    {
    case CommandId::StartRecording:
        if (session_recorder_.isRecording())
        {
            result.ack = makeAck(command, CommandErrorCode::RecordingAlreadyStarted);
            break;
        }
    {
        QString error;
        if (!startRecording(&error))
        {
            emit logMessage(QStringLiteral("Failed to start sky recording: %1").arg(error));
            result.ack = makeAck(command, CommandErrorCode::InternalError);
            break;
        }
        result.send_status = true;
        break;
    }
    case CommandId::PauseRecording:
        if (!session_recorder_.isRecording() && !session_recorder_.isPaused())
        {
            result.ack = makeAck(command, CommandErrorCode::RecordingNotStarted);
            break;
        }
        pauseRecording();
        result.send_status = true;
        break;
    case CommandId::StopRecording:
    {
        if (!session_recorder_.isRecording() && !session_recorder_.isPaused())
        {
            result.ack = makeAck(command, CommandErrorCode::RecordingNotStarted);
            break;
        }
        QString stopError;
        if (!stopRecording(&stopError))
        {
            emit logMessage(QStringLiteral("Failed to stop sky recording: %1").arg(stopError));
            result.ack = makeAck(command, CommandErrorCode::InternalError);
            result.send_status = true;
            break;
        }
        result.send_status = true;
        break;
    }
    case CommandId::SetTelemetryRate:
    case CommandId::SetWaveformRate:
    case CommandId::SetFeatureRate:
    {
        quint16 hz = 0;
        if (!TelemetryCodec::parseRatePayload(command.payload, hz) || hz == 0)
        {
            result.ack = makeAck(command, CommandErrorCode::InvalidPayload);
            break;
        }
        SkyConfig config = device_manager_.config();
        if (command.command_id == CommandId::SetTelemetryRate) config.telemetry.basic_rate_hz = hz;
        if (command.command_id == CommandId::SetWaveformRate) config.telemetry.waveform_rate_hz = hz;
        if (command.command_id == CommandId::SetFeatureRate) config.telemetry.feature_rate_hz = hz;
        device_manager_.applyConfig(config);
        updateTimerIntervals();
        break;
    }
    case CommandId::SetPeakSearchRange:
    {
        PeakSearchRange range;
        if (!TelemetryCodec::parsePeakSearchRange(command.payload, range))
        {
            result.ack = makeAck(command, CommandErrorCode::InvalidPayload);
            break;
        }
        CommandErrorCode error = CommandErrorCode::Ok;
        if (!device_manager_.setPeakSearchRange(range.start_index, range.end_index, &error))
        {
            result.ack = makeAck(command, error);
            break;
        }
        last_sent_feature_time_us_ = 0;
        peak_trend_.clear();
        result.send_status = true;
        break;
    }
    case CommandId::SetTemperatureTarget:
        return temperatureCommand([this](const TemperatureControllerCommand& request) {
            CommandErrorCode error = CommandErrorCode::Ok;
            return device_manager_.setTemperatureTarget(request.channel, request.target_temperature_c, &error);
        });
    case CommandId::SetTemperatureOutputEnabled:
        return temperatureCommand([this](const TemperatureControllerCommand& request) {
            CommandErrorCode error = CommandErrorCode::Ok;
            return device_manager_.setTemperatureOutputEnabled(request.channel, request.output_enabled, &error);
        });
    case CommandId::SetTemperatureOutputMode:
        return temperatureCommand([this](const TemperatureControllerCommand& request) {
            CommandErrorCode error = CommandErrorCode::Ok;
            return device_manager_.setTemperatureOutputMode(request.channel, request.output_mode, &error);
        });
    case CommandId::SetTemperatureMaxOutputPercent:
        return temperatureCommand([this](const TemperatureControllerCommand& request) {
            CommandErrorCode error = CommandErrorCode::Ok;
            return device_manager_.setTemperatureMaxOutputPercent(request.channel, request.max_output_percent, &error);
        });
    case CommandId::SetTemperaturePid:
        return temperatureCommand([this](const TemperatureControllerCommand& request) {
            CommandErrorCode error = CommandErrorCode::Ok;
            return device_manager_.setTemperaturePid(request.channel, request.kp, request.ki, request.kd, &error);
        });
    case CommandId::SetTemperatureAutoPid:
        return temperatureCommand([this](const TemperatureControllerCommand& request) {
            CommandErrorCode error = CommandErrorCode::Ok;
            return device_manager_.setTemperatureAutoPid(request.channel, request.auto_pid_mode, &error);
        });
    case CommandId::SetTemperatureControllerMode:
        return temperatureCommand([this](const TemperatureControllerCommand& request) {
            CommandErrorCode error = CommandErrorCode::Ok;
            return device_manager_.setTemperatureControllerMode(request.controller_mode, &error);
        }, false);
    case CommandId::SetTemperatureDeviceAddress:
        return temperatureCommand([this](const TemperatureControllerCommand& request) {
            CommandErrorCode error = CommandErrorCode::Ok;
            return device_manager_.setTemperatureDeviceAddress(request.device_address, &error);
        }, false);
    case CommandId::SetTemperatureRs485Baud:
        return temperatureCommand([this](const TemperatureControllerCommand& request) {
            CommandErrorCode error = CommandErrorCode::Ok;
            return device_manager_.setTemperatureRs485Baud(request.rs485_baud_index, &error);
        }, false);
    case CommandId::SetTemperatureOvertempOutputMode:
        return temperatureCommand([this](const TemperatureControllerCommand& request) {
            CommandErrorCode error = CommandErrorCode::Ok;
            return device_manager_.setTemperatureOvertempOutputMode(request.overtemp_output_mode, &error);
        }, false);
    case CommandId::SetTemperatureSensorConfig:
        return temperatureCommand([this](const TemperatureControllerCommand& request) {
            CommandErrorCode error = CommandErrorCode::Ok;
            return device_manager_.setTemperatureSensorConfig(request, &error);
        });
    case CommandId::RestoreTemperatureFactoryDefaults:
        return temperatureCommand([this](const TemperatureControllerCommand&) {
            CommandErrorCode error = CommandErrorCode::Ok;
            return device_manager_.restoreTemperatureFactoryDefaults(&error);
        }, false);
    case CommandId::EnableWaveformStreaming:
        setWaveformStreamingEnabled(true);
        break;
    case CommandId::DisableWaveformStreaming:
        setWaveformStreamingEnabled(false);
        break;
    case CommandId::RequestOneWaveform:
        if (!connectedAndFresh(device_manager_.status(SkyDeviceId::WaveTcp), currentTimestampUs(), 3'000'000ULL))
        {
            result.ack = makeAck(command, CommandErrorCode::DeviceNotConnected);
            break;
        }
        result.send_one_waveform = true;
        break;
    case CommandId::RequestStatus:
    case CommandId::QueryDeviceStatus:
        result.send_status = true;
        break;
    case CommandId::ConnectDevice:
        return deviceCommand([this](SkyDeviceId id, CommandErrorCode *error) { return connectDevice(id, error); });
    case CommandId::DisconnectDevice:
        return deviceCommand([this](SkyDeviceId id, CommandErrorCode *error) { return disconnectDevice(id, error); });
    case CommandId::ReconnectDevice:
        return deviceCommand([this](SkyDeviceId id, CommandErrorCode *error) { return reconnectDevice(id, error); });
    case CommandId::ConnectAllDevices:
        connectAllDevices();
        result.send_status = true;
        break;
    case CommandId::DisconnectAllDevices:
        disconnectAllDevices();
        result.send_status = true;
        break;
    case CommandId::ReconnectAllDevices:
        reconnectAllDevices();
        result.send_status = true;
        break;
    case CommandId::GetSkyConfig:
        result.send_sky_config = true;
        break;
    case CommandId::SetSkyConfig:
    {
        const QJsonDocument document = QJsonDocument::fromJson(command.payload);
        SkyConfig config;
        QString error;
        if (!document.isObject() || !SkyConfig::fromJson(document.object(), config, &error))
        {
            emit logMessage(QStringLiteral("Invalid sky config: %1").arg(error));
            result.ack = makeAck(command, CommandErrorCode::ConfigInvalid);
            break;
        }
        const ApplyConfigResult applyResult = device_manager_.applyConfig(config);
        updateTimerIntervals();
        const CommandErrorCode errorCode = applyResult.error_code == CommandErrorCode::Ok
            ? CommandErrorCode::ConfigApplyFailed
            : applyResult.error_code;
        result.ack = makeAck(command, applyResult.success ? CommandErrorCode::Ok : errorCode);
        result.send_config_apply_result = true;
        result.config_apply_result = applyResult.json;
        break;
    }
    case CommandId::SaveSkyConfig:
    {
        QString error;
        const QString configPath = options_.config_path.isEmpty() ? defaultConfigPath() : options_.config_path;
        if (!device_manager_.config().saveToFile(configPath, &error))
        {
            emit logMessage(QStringLiteral("Failed to save sky config: %1").arg(error));
            result.ack = makeAck(command, CommandErrorCode::ConfigSaveFailed);
            break;
        }
        break;
    }
    case CommandId::RebootDevice:
        QTimer::singleShot(200, this, []() {
            const QString program = QCoreApplication::applicationFilePath();
            QStringList args = QCoreApplication::arguments();
            if (!args.isEmpty()) args.removeFirst();
            QProcess::startDetached(program, args);
            QCoreApplication::quit();
        });
        break;
    case CommandId::ReloadSkyConfig:
    default:
        result.ack = makeAck(command, CommandErrorCode::UnknownCommand);
        break;
    }
    return result;
}

void SkyRuntime::sendCommandResultFrames(const SkyCommandResult& result)
{
    if (result.send_status)
    {
        sendTelemetryStatus();
    }
    if (result.send_sky_config)
    {
        sendSkyConfig();
    }
    if (result.send_config_apply_result)
    {
        sendSkyConfigApplyResult(result.config_apply_result);
    }
    if (result.send_one_waveform)
    {
        sendOneWaveformNow();
    }
}

void SkyRuntime::sendFrame(MsgType type, const QByteArray& payload)
{
    emit telemetryFrameReady(type, payload);
    if (!link_ || !link_->isOpen())
    {
        return;
    }
    link_->writeBytes(codec_.encodeFrame(type, payload, next_frame_seq_++, currentTimestampUs()));
}

void SkyRuntime::sendAck(const CommandAck& ack)
{
    sendFrame(MsgType::CommandAck, TelemetryCodec::serializeCommandAck(ack));
}

void SkyRuntime::sendSkyConfig()
{
    sendFrame(MsgType::SkyConfig, QJsonDocument(device_manager_.config().toJson()).toJson(QJsonDocument::Compact));
}

void SkyRuntime::sendSkyConfigApplyResult(const QJsonObject& result)
{
    sendFrame(MsgType::SkyConfigApplyResult, QJsonDocument(result).toJson(QJsonDocument::Compact));
}

void SkyRuntime::updateTimerIntervals()
{
    const TelemetryRateConfig rates = device_manager_.config().telemetry;
    basic_timer_.setInterval(intervalMs(rates.basic_rate_hz));
    feature_timer_.setInterval(intervalMs(rates.feature_rate_hz));
    waveform_timer_.setInterval(intervalMs(rates.waveform_rate_hz));
    heartbeat_timer_.setInterval(intervalMs(rates.heartbeat_rate_hz));
    status_timer_.setInterval(intervalMs(rates.status_rate_hz));
}

quint64 SkyRuntime::currentTimestampUs() const
{
    return static_cast<quint64>(QDateTime::currentDateTimeUtc().toMSecsSinceEpoch()) * 1000ULL;
}

QVector<float> SkyRuntime::waveformPreview(const QVector<float>& samples, int maxPoints) const
{
    QVector<float> preview;
    if (samples.isEmpty() || maxPoints <= 0)
    {
        return preview;
    }
    if (samples.size() <= maxPoints)
    {
        return samples;
    }
    preview.reserve(maxPoints);
    const double step = static_cast<double>(samples.size()) / static_cast<double>(maxPoints);
    for (int i = 0; i < maxPoints; ++i)
    {
        const int sampleCount = static_cast<int>(samples.size());
        const int begin = std::clamp(static_cast<int>(i * step), 0, sampleCount - 1);
        const int end = std::clamp(static_cast<int>((i + 1) * step), begin + 1, sampleCount);
        float minValue = std::numeric_limits<float>::infinity();
        float maxValue = -std::numeric_limits<float>::infinity();
        for (int j = begin; j < end; ++j)
        {
            const float value = samples.at(j);
            if (!std::isfinite(value))
            {
                continue;
            }
            minValue = std::min(minValue, value);
            maxValue = std::max(maxValue, value);
        }
        if (std::isfinite(minValue) && std::isfinite(maxValue))
        {
            preview.push_back(std::abs(maxValue) >= std::abs(minValue) ? maxValue : minValue);
        }
        else
        {
            preview.push_back(0.0f);
        }
    }
    return preview;
}

bool SkyRuntime::deviceStale(SkyDeviceId id, quint64 nowUs, quint64 timeoutUs) const
{
    const DeviceStatusItem status = device_manager_.status(id);
    return status.last_data_time_us == 0 || nowUs < status.last_data_time_us || nowUs - status.last_data_time_us > timeoutUs;
}

}  // namespace VaporView
