#include "SkyRuntime.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStorageInfo>
#include <algorithm>
#include <cmath>
#include <limits>

namespace VaporView
{
namespace
{
int intervalMs(double hz)
{
    return std::max(1, static_cast<int>(1000.0 / std::max(0.001, hz)));
}

QString defaultConfigPath()
{
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("sky_config.json"));
}

}  // namespace

SkyRuntime::SkyRuntime(const SkyRuntimeOptions& options, QObject *parent)
    : QObject(parent)
    , options_(options)
    , link_(this)
    , codec_(1024u * 1024u)
    , device_manager_(this)
{
    connect(&link_, &SerialTelemetryLink::bytesReceived, this, &SkyRuntime::onBytesReceived);
    connect(&link_, &SerialTelemetryLink::errorOccurred, this, &SkyRuntime::logMessage);
    connect(&device_manager_, &SkyDeviceManager::logMessage, this, &SkyRuntime::logMessage);
    connect(&basic_timer_, &QTimer::timeout, this, &SkyRuntime::sendBasicTelemetry);
    connect(&feature_timer_, &QTimer::timeout, this, &SkyRuntime::sendWaveformFeature);
    connect(&waveform_timer_, &QTimer::timeout, this, &SkyRuntime::sendDownsampledWaveform);
    connect(&heartbeat_timer_, &QTimer::timeout, this, &SkyRuntime::sendHeartbeat);
    connect(&status_timer_, &QTimer::timeout, this, &SkyRuntime::sendTelemetryStatus);
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

    if (!link_.open(options_.telemetry_port, options_.telemetry_baud))
    {
        emit logMessage(QStringLiteral("Failed to open telemetry port %1").arg(options_.telemetry_port));
        return false;
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
    emit logMessage(QStringLiteral("SkyRuntime started on %1 @ %2").arg(options_.telemetry_port).arg(options_.telemetry_baud));
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
        session_recorder_.stop();
    }

    device_manager_.setSimulateData(false);
    device_manager_.disconnectAll();
    link_.close();
    running_ = false;
    started_time_us_ = 0;
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
    if (!session_recorder_.start(QCoreApplication::applicationDirPath(), options_.telemetry_port, options_.telemetry_baud, error))
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
    session_recorder_.stop();
    emit logMessage(QStringLiteral("Sky recording stopped"));
    return true;
}

TelemetryStatus SkyRuntime::currentStatus() const
{
    TelemetryStatus status;
    status.recording_state = session_recorder_.recordingState();
    status.session_name = session_recorder_.sessionName();
    status.disk_free_bytes = static_cast<quint64>(QStorageInfo(QCoreApplication::applicationDirPath()).bytesAvailable());
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
    const QVector<float> waveform = device_manager_.latestWaveform();
    snapshot.latest_harmonic_waveform_preview = waveformPreview(waveform, 2048);
    snapshot.latest_raw_waveform_preview = snapshot.latest_harmonic_waveform_preview;
    snapshot.peak_trend = peak_trend_;
    snapshot.telemetry_status = currentStatus();
    snapshot.epsilon_acquisition_rate_hz = snapshot.epsilon.imu_packet_rate_hz > 0.0
                                               ? snapshot.epsilon.imu_packet_rate_hz
                                               : config.epsilon.frequency_hz;
    snapshot.ptb_acquisition_rate_hz = config.ptb.frequency_hz;
    snapshot.hmp_acquisition_rate_hz = config.hmp.frequency_hz;
    snapshot.lidar_acquisition_rate_hz = config.lidar.frequency_hz;
    snapshot.wave_tcp_acquisition_rate_hz = config.wave_tcp.frequency_hz;
    snapshot.devices_csv_recording_rate_hz = config.telemetry.basic_rate_hz;
    snapshot.raw_wave_recording_rate_hz = config.wave_tcp.frequency_hz;
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
    const EpsilonData epsilon = device_manager_.latestEpsilon();
    const PtbData ptb = device_manager_.latestPtb();
    const HmpData hmp = device_manager_.latestHmp();
    const LidarData lidar = device_manager_.latestLidar();

    TelemetryBasic data;
    data.host_time_us = currentTimestampUs();
    data.epsilon_time_us = epsilon.device_timestamp_us;
    data.latitude_deg = epsilon.latitude_deg;
    data.longitude_deg = epsilon.longitude_deg;
    data.height_m = epsilon.height_m;
    data.ecef_x_m = epsilon.ecef_x_m;
    data.ecef_y_m = epsilon.ecef_y_m;
    data.ecef_z_m = epsilon.ecef_z_m;
    data.lidar_height_m = static_cast<float>(lidar.distance_m);
    data.temperature_c = static_cast<float>(hmp.temperature);
    data.humidity_percent = static_cast<float>(hmp.humidity);
    data.pressure_hpa = static_cast<float>(ptb.pressure_hpa);
    data.status_bits = epsilon.system_status_bits;
    data.filter_status_bits = epsilon.filter_status_bits;
    data.update_status_bits = epsilon.update_status_bits;
    data.gnss_fix_code = static_cast<quint8>(std::clamp(epsilon.gnss_fix_code, 0, 255));
    session_recorder_.recordBasicTelemetry(data);
    sendFrame(MsgType::TelemetryBasic, TelemetryCodec::serializeBasicTelemetry(data));
}

void SkyRuntime::sendWaveformFeature()
{
    const WaveformFeature feature = device_manager_.latestWaveformFeature();
    if (std::isfinite(feature.peak))
    {
        peak_trend_.push_back(feature.peak);
        while (peak_trend_.size() > 256)
        {
            peak_trend_.removeFirst();
        }
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
    const QVector<float> samples = device_manager_.latestWaveform();
    if (samples.isEmpty())
    {
        return;
    }
    const quint64 hostTimeUs = currentTimestampUs();
    session_recorder_.recordWaveformSnapshot(hostTimeUs, device_manager_.latestEpsilon().device_timestamp_us, samples);
    if (honorStreamingEnabled && !waveform_streaming_enabled_)
    {
        return;
    }
    const int ratio = std::max(1, device_manager_.config().wave_tcp.downsample_ratio);
    DownsampledWaveform waveform;
    waveform.host_time_us = hostTimeUs;
    waveform.epsilon_time_us = device_manager_.latestEpsilon().device_timestamp_us;
    waveform.original_point_count = static_cast<quint32>(samples.size());
    waveform.channel_id = 4;
    waveform.sample_format = 1;
    waveform.x_start = 0.0f;
    waveform.x_step = static_cast<float>(ratio);
    waveform.samples.reserve((samples.size() + ratio - 1) / ratio);
    for (int i = 0; i < samples.size(); i += ratio)
    {
        waveform.samples.push_back(samples.at(i));
    }
    waveform.downsampled_point_count = static_cast<quint32>(waveform.samples.size());
    sendFrame(MsgType::WaveformDownsampled, TelemetryCodec::serializeDownsampledWaveform(waveform));
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
    auto deviceCommand = [&](auto method) {
        SkyDeviceId id = SkyDeviceId::All;
        if (!TelemetryCodec::parseDeviceCommand(command.payload, id))
        {
            sendAck(command, CommandErrorCode::InvalidPayload);
            return;
        }
        CommandErrorCode error = CommandErrorCode::Ok;
        const bool ok = method(id, &error);
        sendAck(command, ok ? CommandErrorCode::Ok : error);
        sendTelemetryStatus();
    };

    switch (command.command_id)
    {
    case CommandId::StartRecording:
        if (session_recorder_.isRecording())
        {
            sendAck(command, CommandErrorCode::RecordingAlreadyStarted);
            break;
        }
    {
        QString error;
        if (!startRecording(&error))
        {
            emit logMessage(QStringLiteral("Failed to start sky recording: %1").arg(error));
            sendAck(command, CommandErrorCode::InternalError);
            break;
        }
        sendAck(command);
        sendTelemetryStatus();
        break;
    }
    case CommandId::PauseRecording:
        if (!session_recorder_.isRecording() && !session_recorder_.isPaused())
        {
            sendAck(command, CommandErrorCode::RecordingNotStarted);
            break;
        }
        pauseRecording();
        sendAck(command);
        sendTelemetryStatus();
        break;
    case CommandId::StopRecording:
        if (!session_recorder_.isRecording() && !session_recorder_.isPaused())
        {
            sendAck(command, CommandErrorCode::RecordingNotStarted);
            break;
        }
        stopRecording();
        sendAck(command);
        sendTelemetryStatus();
        break;
    case CommandId::SetTelemetryRate:
    case CommandId::SetWaveformRate:
    case CommandId::SetFeatureRate:
    {
        quint16 hz = 0;
        if (!TelemetryCodec::parseRatePayload(command.payload, hz) || hz == 0)
        {
            sendAck(command, CommandErrorCode::InvalidPayload);
            break;
        }
        SkyConfig config = device_manager_.config();
        if (command.command_id == CommandId::SetTelemetryRate) config.telemetry.basic_rate_hz = hz;
        if (command.command_id == CommandId::SetWaveformRate) config.telemetry.waveform_rate_hz = hz;
        if (command.command_id == CommandId::SetFeatureRate) config.telemetry.feature_rate_hz = hz;
        device_manager_.applyConfig(config);
        updateTimerIntervals();
        sendAck(command);
        break;
    }
    case CommandId::EnableWaveformStreaming:
        setWaveformStreamingEnabled(true);
        sendAck(command);
        break;
    case CommandId::DisableWaveformStreaming:
        setWaveformStreamingEnabled(false);
        sendAck(command);
        break;
    case CommandId::RequestOneWaveform:
        sendAck(command);
        sendOneWaveformNow();
        break;
    case CommandId::RequestStatus:
    case CommandId::QueryDeviceStatus:
        sendAck(command);
        sendTelemetryStatus();
        break;
    case CommandId::ConnectDevice:
        deviceCommand([this](SkyDeviceId id, CommandErrorCode *error) { return connectDevice(id, error); });
        break;
    case CommandId::DisconnectDevice:
        deviceCommand([this](SkyDeviceId id, CommandErrorCode *error) { return disconnectDevice(id, error); });
        break;
    case CommandId::ReconnectDevice:
        deviceCommand([this](SkyDeviceId id, CommandErrorCode *error) { return reconnectDevice(id, error); });
        break;
    case CommandId::ConnectAllDevices:
        connectAllDevices();
        sendAck(command);
        sendTelemetryStatus();
        break;
    case CommandId::DisconnectAllDevices:
        disconnectAllDevices();
        sendAck(command);
        sendTelemetryStatus();
        break;
    case CommandId::ReconnectAllDevices:
        reconnectAllDevices();
        sendAck(command);
        sendTelemetryStatus();
        break;
    case CommandId::GetSkyConfig:
        sendAck(command);
        sendSkyConfig();
        break;
    case CommandId::SetSkyConfig:
    {
        const QJsonDocument document = QJsonDocument::fromJson(command.payload);
        SkyConfig config;
        QString error;
        if (!document.isObject() || !SkyConfig::fromJson(document.object(), config, &error))
        {
            emit logMessage(QStringLiteral("Invalid sky config: %1").arg(error));
            sendAck(command, CommandErrorCode::ConfigInvalid);
            break;
        }
        const ApplyConfigResult result = device_manager_.applyConfig(config);
        updateTimerIntervals();
        sendAck(command, result.success ? CommandErrorCode::Ok : CommandErrorCode::ConfigApplyFailed);
        sendSkyConfigApplyResult(result.json);
        break;
    }
    case CommandId::SaveSkyConfig:
    {
        QString error;
        const QString configPath = options_.config_path.isEmpty() ? defaultConfigPath() : options_.config_path;
        if (!device_manager_.config().saveToFile(configPath, &error))
        {
            emit logMessage(QStringLiteral("Failed to save sky config: %1").arg(error));
            sendAck(command, CommandErrorCode::ConfigSaveFailed);
            break;
        }
        sendAck(command);
        break;
    }
    case CommandId::RebootDevice:
        sendAck(command);
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
        sendAck(command, CommandErrorCode::UnknownCommand);
        break;
    }
}

void SkyRuntime::sendFrame(MsgType type, const QByteArray& payload)
{
    if (!link_.isOpen())
    {
        return;
    }
    link_.writeBytes(codec_.encodeFrame(type, payload, next_frame_seq_++, currentTimestampUs()));
}

void SkyRuntime::sendAck(const CommandMessage& command, CommandErrorCode errorCode)
{
    CommandAck ack;
    ack.command_id = command.command_id;
    ack.command_seq = command.command_seq;
    ack.result = errorCode == CommandErrorCode::Ok ? 0 : 1;
    ack.error_code = errorCode;
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
