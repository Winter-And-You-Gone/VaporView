#include "SkyDeviceManager.h"

#include <QDateTime>
#include <QJsonObject>
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <limits>

namespace VaporView
{
namespace
{
quint64 nowUs()
{
    return static_cast<quint64>(QDateTime::currentDateTimeUtc().toMSecsSinceEpoch()) * 1000ULL;
}

template <typename Collector>
void stopCollector(std::shared_ptr<Collector>& collector)
{
    if (collector)
    {
        collector->stop();
        collector.reset();
    }
}

float finiteOrZero(double value)
{
    return std::isfinite(value) ? static_cast<float>(value) : 0.0f;
}

QJsonObject resultItem(bool changed, bool reconfigured, const DeviceStatusItem& status)
{
    QJsonObject item;
    item["changed"] = changed;
    item["reconfigured"] = reconfigured;
    item["state"] = deviceStateName(status.state);
    item["error_code"] = static_cast<int>(status.error_code);
    return item;
}

}  // namespace

SkyDeviceManager::SkyDeviceManager(QObject *parent)
    : QObject(parent)
{
    initializeStatuses();
    connect(&simulate_timer_, &QTimer::timeout, this, &SkyDeviceManager::generateSimulatedData);
}

SkyDeviceManager::~SkyDeviceManager()
{
    disconnectAll();
}

void SkyDeviceManager::setSimulateData(bool simulate)
{
    simulate_data_ = simulate;
    if (simulate_data_)
    {
        simulate_timer_.start(100);
    }
    else
    {
        simulate_timer_.stop();
    }
}

void SkyDeviceManager::loadConfig(const SkyConfig& config)
{
    config_ = config;
}

const SkyConfig& SkyDeviceManager::config() const
{
    return config_;
}

bool SkyDeviceManager::connectDevice(SkyDeviceId id, CommandErrorCode *errorCode)
{
    if (id == SkyDeviceId::All)
    {
        connectAll();
        if (errorCode) *errorCode = CommandErrorCode::Ok;
        return true;
    }
    DeviceStatusItem& statusItem = mutableStatus(id);
    if (statusItem.state == DeviceState::Connected)
    {
        if (errorCode) *errorCode = CommandErrorCode::DeviceAlreadyConnected;
        return false;
    }

    if (simulate_data_)
    {
        setState(id, DeviceState::Connected);
        if (errorCode) *errorCode = CommandErrorCode::Ok;
        return true;
    }

    if (id == SkyDeviceId::WaveTcp)
    {
        return connectWaveTcp(errorCode);
    }

    return connectSerialCollector(id, serialConfigFor(id), errorCode);
}

bool SkyDeviceManager::disconnectDevice(SkyDeviceId id, CommandErrorCode *errorCode)
{
    if (id == SkyDeviceId::All)
    {
        disconnectAll();
        if (errorCode) *errorCode = CommandErrorCode::Ok;
        return true;
    }
    switch (id)
    {
    case SkyDeviceId::Epsilon:
        stopCollector(epsilon_);
        break;
    case SkyDeviceId::Ptb:
        stopCollector(ptb_);
        break;
    case SkyDeviceId::Hmp:
        stopCollector(hmp_);
        break;
    case SkyDeviceId::Lidar:
        stopCollector(lidar_);
        break;
    case SkyDeviceId::WaveTcp:
        disconnectWaveTcp();
        break;
    case SkyDeviceId::All:
        break;
    }
    setState(id, DeviceState::Disconnected);
    if (errorCode) *errorCode = CommandErrorCode::Ok;
    return true;
}

bool SkyDeviceManager::reconnectDevice(SkyDeviceId id, CommandErrorCode *errorCode)
{
    if (id == SkyDeviceId::All)
    {
        reconnectAll();
        if (errorCode) *errorCode = CommandErrorCode::Ok;
        return true;
    }
    setState(id, DeviceState::Reconnecting);
    disconnectDevice(id);
    return connectDevice(id, errorCode);
}

void SkyDeviceManager::connectAll()
{
    for (SkyDeviceId id : {SkyDeviceId::Epsilon, SkyDeviceId::Ptb, SkyDeviceId::Hmp, SkyDeviceId::Lidar, SkyDeviceId::WaveTcp})
    {
        const bool enabled =
            (id == SkyDeviceId::Epsilon && config_.epsilon.enabled) ||
            (id == SkyDeviceId::Ptb && config_.ptb.enabled) ||
            (id == SkyDeviceId::Hmp && config_.hmp.enabled) ||
            (id == SkyDeviceId::Lidar && config_.lidar.enabled) ||
            (id == SkyDeviceId::WaveTcp && config_.wave_tcp.enabled);
        if (enabled)
        {
            CommandErrorCode ignored = CommandErrorCode::Ok;
            connectDevice(id, &ignored);
        }
        else
        {
            setState(id, DeviceState::Disabled);
        }
    }
}

void SkyDeviceManager::disconnectAll()
{
    for (SkyDeviceId id : {SkyDeviceId::Epsilon, SkyDeviceId::Ptb, SkyDeviceId::Hmp, SkyDeviceId::Lidar, SkyDeviceId::WaveTcp})
    {
        disconnectDevice(id);
    }
}

void SkyDeviceManager::reconnectAll()
{
    disconnectAll();
    connectAll();
}

DeviceStatusItem SkyDeviceManager::status(SkyDeviceId id) const
{
    switch (id)
    {
    case SkyDeviceId::Epsilon:
        return epsilon_status_;
    case SkyDeviceId::Ptb:
        return ptb_status_;
    case SkyDeviceId::Hmp:
        return hmp_status_;
    case SkyDeviceId::Lidar:
        return lidar_status_;
    case SkyDeviceId::WaveTcp:
        return wave_tcp_status_;
    case SkyDeviceId::All:
        break;
    }
    return {};
}

QVector<DeviceStatusItem> SkyDeviceManager::allStatuses() const
{
    return {epsilon_status_, ptb_status_, hmp_status_, lidar_status_, wave_tcp_status_};
}

ApplyConfigResult SkyDeviceManager::applyConfig(const SkyConfig& newConfig)
{
    ApplyConfigResult result;
    const SkyConfig oldConfig = config_;
    const SkyConfigDiff diff = oldConfig.diff(newConfig);
    config_ = newConfig;

    auto reconfigureSerial = [this](SkyDeviceId id, bool changed, bool enabled) {
        bool reconfigured = false;
        if (changed)
        {
            CommandErrorCode ignored = CommandErrorCode::Ok;
            disconnectDevice(id, &ignored);
            reconfigured = true;
            if (enabled)
            {
                connectDevice(id, &ignored);
            }
            else
            {
                setState(id, DeviceState::Disabled);
            }
        }
        return reconfigured;
    };

    const bool epsilonReconfigured = reconfigureSerial(SkyDeviceId::Epsilon, diff.epsilon_changed, config_.epsilon.enabled);
    const bool ptbReconfigured = reconfigureSerial(SkyDeviceId::Ptb, diff.ptb_changed, config_.ptb.enabled);
    const bool hmpReconfigured = reconfigureSerial(SkyDeviceId::Hmp, diff.hmp_changed, config_.hmp.enabled);
    const bool lidarReconfigured = reconfigureSerial(SkyDeviceId::Lidar, diff.lidar_changed, config_.lidar.enabled);
    const bool waveReconfigured = reconfigureSerial(SkyDeviceId::WaveTcp, diff.wave_tcp_changed, config_.wave_tcp.enabled);

    QJsonObject devices;
    devices["epsilon"] = resultItem(diff.epsilon_changed, epsilonReconfigured, epsilon_status_);
    devices["ptb"] = resultItem(diff.ptb_changed, ptbReconfigured, ptb_status_);
    devices["hmp"] = resultItem(diff.hmp_changed, hmpReconfigured, hmp_status_);
    devices["lidar"] = resultItem(diff.lidar_changed, lidarReconfigured, lidar_status_);
    devices["wave_tcp"] = resultItem(diff.wave_tcp_changed, waveReconfigured, wave_tcp_status_);

    QJsonObject telemetry;
    telemetry["changed"] = diff.telemetry_changed;
    telemetry["timers_updated"] = diff.telemetry_changed;

    result.json["success"] = result.success;
    result.json["devices"] = devices;
    result.json["telemetry"] = telemetry;
    return result;
}

EpsilonData SkyDeviceManager::latestEpsilon() const
{
    return latest_epsilon_;
}

PtbData SkyDeviceManager::latestPtb() const
{
    return latest_ptb_;
}

HmpData SkyDeviceManager::latestHmp() const
{
    return latest_hmp_;
}

LidarData SkyDeviceManager::latestLidar() const
{
    return latest_lidar_;
}

QVector<float> SkyDeviceManager::latestWaveform() const
{
    return latest_waveform_;
}

WaveformFeature SkyDeviceManager::latestWaveformFeature() const
{
    return latest_feature_;
}

void SkyDeviceManager::generateSimulatedData()
{
    simulate_phase_ += 0.04;
    const quint64 t = nowUs();

    latest_epsilon_.valid = true;
    latest_epsilon_.timestamp = std::chrono::steady_clock::now();
    latest_epsilon_.device_timestamp_us = t;
    latest_epsilon_.utc_unix_s = t / 1000000ULL;
    latest_epsilon_.utc_microseconds = static_cast<quint32>(t % 1000000ULL);
    latest_epsilon_.gnss_fix_code = 4;
    latest_epsilon_.gnss_fix_text = "Remote";
    latest_epsilon_.gnss_satellites = 18;
    latest_epsilon_.latitude_deg = 31.2304 + std::sin(simulate_phase_) * 0.0001;
    latest_epsilon_.longitude_deg = 121.4737 + std::cos(simulate_phase_) * 0.0001;
    latest_epsilon_.height_m = 1200.0 + std::sin(simulate_phase_ * 0.7) * 3.0;
    latest_epsilon_.ecef_x_m = 1000.0 + std::sin(simulate_phase_) * 5.0;
    latest_epsilon_.ecef_y_m = 2000.0 + std::cos(simulate_phase_) * 5.0;
    latest_epsilon_.ecef_z_m = 3000.0 + std::sin(simulate_phase_ * 0.5) * 5.0;
    latest_epsilon_.raw_frame_count++;
    latest_epsilon_.imu_packet_rate_hz = 100.0;
    latest_epsilon_.geodetic_packet_rate_hz = 10.0;
    latest_epsilon_.ecef_packet_rate_hz = 10.0;
    epsilon_status_.rx_count++;
    epsilon_status_.last_data_time_us = t;
    setState(SkyDeviceId::Epsilon, DeviceState::Connected);
    emit epsilonDataUpdated(latest_epsilon_);

    latest_ptb_.valid = true;
    latest_ptb_.timestamp = latest_epsilon_.timestamp;
    latest_ptb_.pressure_hpa = 900.0 + std::sin(simulate_phase_ * 0.3) * 1.5;
    ptb_status_.rx_count++;
    ptb_status_.last_data_time_us = t;
    setState(SkyDeviceId::Ptb, DeviceState::Connected);
    emit ptbDataUpdated(latest_ptb_);

    latest_hmp_.valid = true;
    latest_hmp_.timestamp = latest_epsilon_.timestamp;
    latest_hmp_.temperature = 23.0 + std::sin(simulate_phase_ * 0.2) * 2.0;
    latest_hmp_.humidity = 45.0 + std::cos(simulate_phase_ * 0.15) * 5.0;
    hmp_status_.rx_count++;
    hmp_status_.last_data_time_us = t;
    setState(SkyDeviceId::Hmp, DeviceState::Connected);
    emit hmpDataUpdated(latest_hmp_);

    latest_lidar_.valid = true;
    latest_lidar_.timestamp = latest_epsilon_.timestamp;
    latest_lidar_.distance_m = 120.0 + std::sin(simulate_phase_ * 0.6) * 8.0;
    latest_lidar_.signal_strength = 180;
    lidar_status_.rx_count++;
    lidar_status_.last_data_time_us = t;
    setState(SkyDeviceId::Lidar, DeviceState::Connected);
    emit lidarDataUpdated(latest_lidar_);

    if (latest_waveform_.isEmpty())
    {
        latest_waveform_.resize(50000);
    }
    for (int i = 0; i < latest_waveform_.size(); ++i)
    {
        const double x = static_cast<double>(i) / 500.0;
        latest_waveform_[i] = static_cast<float>(std::sin(x + simulate_phase_) * 0.05 + std::exp(-std::pow((i - 24000) / 3500.0, 2.0)));
    }
    wave_tcp_status_.rx_count++;
    wave_tcp_status_.last_data_time_us = t;
    setState(SkyDeviceId::WaveTcp, DeviceState::Connected);
    publishWaveform(latest_waveform_);
}

void SkyDeviceManager::onWaveTcpConnected()
{
    setState(SkyDeviceId::WaveTcp, DeviceState::Connected);
}

void SkyDeviceManager::onWaveTcpDisconnected()
{
    setState(SkyDeviceId::WaveTcp, DeviceState::Disconnected);
}

void SkyDeviceManager::onWaveTcpReadyRead()
{
    if (!wave_socket_)
    {
        return;
    }
    wave_buffer_.append(wave_socket_->readAll());
    processWaveTcpBuffer();
}

void SkyDeviceManager::onWaveTcpError()
{
    setState(SkyDeviceId::WaveTcp, DeviceState::Error, 1);
}

void SkyDeviceManager::initializeStatuses()
{
    epsilon_status_.device_id = SkyDeviceId::Epsilon;
    ptb_status_.device_id = SkyDeviceId::Ptb;
    hmp_status_.device_id = SkyDeviceId::Hmp;
    lidar_status_.device_id = SkyDeviceId::Lidar;
    wave_tcp_status_.device_id = SkyDeviceId::WaveTcp;
}

void SkyDeviceManager::setState(SkyDeviceId id, DeviceState state, quint16 errorCode)
{
    DeviceStatusItem& item = mutableStatus(id);
    if (item.state == state && item.error_code == errorCode)
    {
        return;
    }
    item.state = state;
    item.error_code = errorCode;
    emit deviceStatusChanged(id, item);
}

DeviceStatusItem& SkyDeviceManager::mutableStatus(SkyDeviceId id)
{
    switch (id)
    {
    case SkyDeviceId::Epsilon:
        return epsilon_status_;
    case SkyDeviceId::Ptb:
        return ptb_status_;
    case SkyDeviceId::Hmp:
        return hmp_status_;
    case SkyDeviceId::Lidar:
        return lidar_status_;
    case SkyDeviceId::WaveTcp:
        return wave_tcp_status_;
    case SkyDeviceId::All:
        break;
    }
    return epsilon_status_;
}

const SerialDeviceConfig& SkyDeviceManager::serialConfigFor(SkyDeviceId id) const
{
    switch (id)
    {
    case SkyDeviceId::Epsilon:
        return config_.epsilon;
    case SkyDeviceId::Ptb:
        return config_.ptb;
    case SkyDeviceId::Hmp:
        return config_.hmp;
    case SkyDeviceId::Lidar:
        return config_.lidar;
    case SkyDeviceId::WaveTcp:
    case SkyDeviceId::All:
        break;
    }
    return config_.epsilon;
}

bool SkyDeviceManager::connectSerialCollector(SkyDeviceId id, const SerialDeviceConfig& config, CommandErrorCode *errorCode)
{
    if (!config.enabled)
    {
        setState(id, DeviceState::Disabled);
        if (errorCode) *errorCode = CommandErrorCode::Ok;
        return true;
    }

    setState(id, DeviceState::Connecting);
    auto fail = [&](CommandErrorCode code) {
        setState(id, DeviceState::Error, static_cast<quint16>(code));
        if (errorCode) *errorCode = code;
        return false;
    };

    auto logCallback = [this, id](const std::string& message) {
        emit logMessage(QStringLiteral("[%1] %2").arg(skyDeviceIdName(id), QString::fromStdString(message)));
    };

    switch (id)
    {
    case SkyDeviceId::Epsilon:
        epsilon_ = std::make_shared<EpsilonCollector>();
        epsilon_->setLogCallback(logCallback);
        epsilon_->setSampleRate(static_cast<int>(config.frequency_hz));
        epsilon_->setDataCallback([this]() {
            latest_epsilon_ = epsilon_ ? epsilon_->getLatestData() : EpsilonData();
            epsilon_status_.rx_count++;
            epsilon_status_.last_data_time_us = nowUs();
            emit epsilonDataUpdated(latest_epsilon_);
        });
        if (!epsilon_->start(config.port.toStdString(), SerialConfig::N81(config.baud_rate))) return fail(CommandErrorCode::DeviceConnectFailed);
        if (!epsilon_->checkDeviceResponse()) return fail(CommandErrorCode::DeviceConnectFailed);
        epsilon_->setDeviceSampleRate(static_cast<int>(config.frequency_hz));
        if (!epsilon_->startStreaming()) return fail(CommandErrorCode::DeviceConnectFailed);
        break;
    case SkyDeviceId::Ptb:
        ptb_ = std::make_shared<PtbCollector>();
        ptb_->setLogCallback(logCallback);
        ptb_->setSampleRate(static_cast<int>(config.frequency_hz));
        ptb_->setDataCallback([this]() {
            latest_ptb_ = ptb_ ? ptb_->getLatestData() : PtbData();
            ptb_status_.rx_count++;
            ptb_status_.last_data_time_us = nowUs();
            emit ptbDataUpdated(latest_ptb_);
        });
        if (!ptb_->start(config.port.toStdString(), SerialConfig::E71(config.baud_rate))) return fail(CommandErrorCode::DeviceConnectFailed);
        if (!ptb_->checkDeviceResponse()) return fail(CommandErrorCode::DeviceConnectFailed);
        ptb_->setDeviceSampleRate(static_cast<int>(config.frequency_hz));
        if (!ptb_->startStreaming()) return fail(CommandErrorCode::DeviceConnectFailed);
        break;
    case SkyDeviceId::Hmp:
        hmp_ = std::make_shared<HmpCollector>();
        hmp_->setLogCallback(logCallback);
        hmp_->setSampleRate(static_cast<int>(config.frequency_hz));
        hmp_->setDataCallback([this]() {
            latest_hmp_ = hmp_ ? hmp_->getLatestData() : HmpData();
            hmp_status_.rx_count++;
            hmp_status_.last_data_time_us = nowUs();
            emit hmpDataUpdated(latest_hmp_);
        });
        if (!hmp_->start(config.port.toStdString(), SerialConfig::N82(config.baud_rate))) return fail(CommandErrorCode::DeviceConnectFailed);
        if (!hmp_->checkDeviceResponse()) return fail(CommandErrorCode::DeviceConnectFailed);
        if (!hmp_->startStreaming()) return fail(CommandErrorCode::DeviceConnectFailed);
        break;
    case SkyDeviceId::Lidar:
        lidar_ = std::make_shared<LidarCollector>();
        lidar_->setLogCallback(logCallback);
        lidar_->setSampleRate(static_cast<int>(config.frequency_hz));
        lidar_->setDataCallback([this]() {
            latest_lidar_ = lidar_ ? lidar_->getLatestData() : LidarData();
            lidar_status_.rx_count++;
            lidar_status_.last_data_time_us = nowUs();
            emit lidarDataUpdated(latest_lidar_);
        });
        if (!lidar_->start(config.port.toStdString(), SerialConfig::N81(config.baud_rate))) return fail(CommandErrorCode::DeviceConnectFailed);
        if (!lidar_->checkDeviceResponse()) return fail(CommandErrorCode::DeviceConnectFailed);
        lidar_->setDeviceSampleRate(static_cast<int>(config.frequency_hz));
        if (!lidar_->startStreaming()) return fail(CommandErrorCode::DeviceConnectFailed);
        break;
    case SkyDeviceId::WaveTcp:
    case SkyDeviceId::All:
        return fail(CommandErrorCode::InvalidDeviceId);
    }

    setState(id, DeviceState::Connected);
    if (errorCode) *errorCode = CommandErrorCode::Ok;
    return true;
}

bool SkyDeviceManager::connectWaveTcp(CommandErrorCode *errorCode)
{
    if (!config_.wave_tcp.enabled)
    {
        setState(SkyDeviceId::WaveTcp, DeviceState::Disabled);
        if (errorCode) *errorCode = CommandErrorCode::Ok;
        return true;
    }
    disconnectWaveTcp();
    setState(SkyDeviceId::WaveTcp, DeviceState::Connecting);
    wave_socket_ = new QTcpSocket(this);
    connect(wave_socket_, &QTcpSocket::connected, this, &SkyDeviceManager::onWaveTcpConnected);
    connect(wave_socket_, &QTcpSocket::disconnected, this, &SkyDeviceManager::onWaveTcpDisconnected);
    connect(wave_socket_, &QTcpSocket::readyRead, this, &SkyDeviceManager::onWaveTcpReadyRead);
    connect(wave_socket_, &QTcpSocket::errorOccurred, this, &SkyDeviceManager::onWaveTcpError);
    wave_socket_->connectToHost(config_.wave_tcp.host, static_cast<quint16>(config_.wave_tcp.port));
    if (errorCode) *errorCode = CommandErrorCode::Ok;
    return true;
}

void SkyDeviceManager::disconnectWaveTcp()
{
    if (wave_socket_)
    {
        wave_socket_->disconnectFromHost();
        wave_socket_->deleteLater();
        wave_socket_ = nullptr;
    }
    wave_buffer_.clear();
}

void SkyDeviceManager::processWaveTcpBuffer()
{
    while (wave_buffer_.size() >= 8)
    {
        const quint32 rawSize = qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(wave_buffer_.constData()));
        if (rawSize > 16u * 1024u * 1024u || wave_buffer_.size() < static_cast<int>(4 + rawSize + 4))
        {
            return;
        }
        const quint32 harmonicSize = qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(wave_buffer_.constData() + 4 + rawSize));
        if (harmonicSize > 16u * 1024u * 1024u || wave_buffer_.size() < static_cast<int>(8 + rawSize + harmonicSize))
        {
            return;
        }
        const QByteArray harmonicPayload = wave_buffer_.mid(static_cast<int>(8 + rawSize), static_cast<int>(harmonicSize));
        wave_buffer_.remove(0, static_cast<int>(8 + rawSize + harmonicSize));
        wave_float_encoding_ = autoDetectTcpFloatEncoding(harmonicPayload);
        publishWaveform(decodeTcpFloatPayload(harmonicPayload, wave_float_encoding_));
        wave_tcp_status_.rx_count++;
        wave_tcp_status_.last_data_time_us = nowUs();
    }
}

void SkyDeviceManager::publishWaveform(const QVector<float>& harmonic)
{
    latest_waveform_ = harmonic;
    if (harmonic.isEmpty())
    {
        return;
    }
    double sum = 0.0;
    double sq = 0.0;
    float minValue = std::numeric_limits<float>::infinity();
    float maxValue = -std::numeric_limits<float>::infinity();
    int peakIndex = 0;
    for (int i = 0; i < harmonic.size(); ++i)
    {
        const float value = harmonic.at(i);
        sum += value;
        sq += static_cast<double>(value) * value;
        if (value < minValue) minValue = value;
        if (value > maxValue)
        {
            maxValue = value;
            peakIndex = i;
        }
    }
    latest_feature_.host_time_us = nowUs();
    latest_feature_.epsilon_time_us = latest_epsilon_.device_timestamp_us;
    latest_feature_.channel_id = 4;
    latest_feature_.peak = maxValue;
    latest_feature_.mean = static_cast<float>(sum / harmonic.size());
    latest_feature_.rms = static_cast<float>(std::sqrt(sq / harmonic.size()));
    latest_feature_.peak_index = static_cast<float>(peakIndex);
    latest_feature_.peak_x = static_cast<float>(peakIndex);
    latest_feature_.min_value = minValue;
    latest_feature_.max_value = maxValue;
    emit waveformUpdated(latest_feature_.host_time_us, harmonic);
    emit waveformFeatureUpdated(latest_feature_);
}

}  // namespace VaporView
