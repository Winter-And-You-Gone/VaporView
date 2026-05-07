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
    invalidateDeviceData(id);
    setState(id, DeviceState::Disconnected);
    emit logMessage(QStringLiteral("%1 disconnected, data invalidated").arg(skyDeviceIdName(id)));
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

bool SkyDeviceManager::setPeakSearchRange(quint32 startIndex, quint32 endIndex, CommandErrorCode *errorCode)
{
    if (startIndex > static_cast<quint32>(std::numeric_limits<int>::max()) ||
        endIndex > static_cast<quint32>(std::numeric_limits<int>::max()) ||
        (endIndex > 0 && endIndex <= startIndex))
    {
        if (errorCode) *errorCode = CommandErrorCode::ConfigInvalid;
        return false;
    }
    config_.wave_tcp.peak_search_start_index = static_cast<int>(startIndex);
    config_.wave_tcp.peak_search_end_index = static_cast<int>(endIndex);
    emit logMessage(QStringLiteral("Wave TCP peak search range updated: [%1, %2)")
                        .arg(startIndex)
                        .arg(endIndex == 0 ? QStringLiteral("end") : QString::number(endIndex)));
    if (errorCode) *errorCode = CommandErrorCode::Ok;
    return true;
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

    if (epsilon_status_.state == DeviceState::Connected)
    {
        latest_epsilon_.valid = true;
        latest_epsilon_.timestamp = std::chrono::steady_clock::now();
        latest_epsilon_.device_timestamp_us = t;
        latest_epsilon_.utc_unix_s = t / 1000000ULL;
        latest_epsilon_.utc_microseconds = static_cast<quint32>(t % 1000000ULL);
        latest_epsilon_.gnss_fix_code = 6;
        latest_epsilon_.gnss_fix_text = "RTK_FIXED";
        latest_epsilon_.filter_status_bits = static_cast<uint16_t>(latest_epsilon_.gnss_fix_code << 4);
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
        emit epsilonDataUpdated(latest_epsilon_);
    }

    const auto timestamp = latest_epsilon_.timestamp.time_since_epoch().count() == 0
        ? std::chrono::steady_clock::now()
        : latest_epsilon_.timestamp;

    if (ptb_status_.state == DeviceState::Connected)
    {
        latest_ptb_.valid = true;
        latest_ptb_.timestamp = timestamp;
        latest_ptb_.pressure_hpa = 900.0 + std::sin(simulate_phase_ * 0.3) * 1.5;
        ptb_status_.rx_count++;
        ptb_status_.last_data_time_us = t;
        emit ptbDataUpdated(latest_ptb_);
    }

    if (hmp_status_.state == DeviceState::Connected)
    {
        latest_hmp_.valid = true;
        latest_hmp_.timestamp = timestamp;
        latest_hmp_.temperature = 23.0 + std::sin(simulate_phase_ * 0.2) * 2.0;
        latest_hmp_.humidity = 45.0 + std::cos(simulate_phase_ * 0.15) * 5.0;
        hmp_status_.rx_count++;
        hmp_status_.last_data_time_us = t;
        emit hmpDataUpdated(latest_hmp_);
    }

    if (lidar_status_.state == DeviceState::Connected)
    {
        latest_lidar_.valid = true;
        latest_lidar_.timestamp = timestamp;
        latest_lidar_.distance_m = 120.0 + std::sin(simulate_phase_ * 0.6) * 8.0;
        latest_lidar_.signal_strength = 180;
        lidar_status_.rx_count++;
        lidar_status_.last_data_time_us = t;
        emit lidarDataUpdated(latest_lidar_);
    }

    if (wave_tcp_status_.state == DeviceState::Connected)
    {
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
        publishWaveform(latest_waveform_);
    }
}

void SkyDeviceManager::onWaveTcpConnected()
{
    setState(SkyDeviceId::WaveTcp, DeviceState::Connected);
}

void SkyDeviceManager::onWaveTcpDisconnected()
{
    invalidateDeviceData(SkyDeviceId::WaveTcp);
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
    invalidateDeviceData(SkyDeviceId::WaveTcp);
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
        case SkyDeviceId::All:
            break;
        }
        invalidateDeviceData(id);
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
    if (wave_tcp_status_.state != DeviceState::Connected)
    {
        return;
    }
    latest_waveform_ = harmonic;
    if (harmonic.isEmpty())
    {
        return;
    }
    ++wave_frame_count_;
    emit waveformUpdated(nowUs(), harmonic);

    const double targetRateHz = config_.telemetry.feature_rate_hz;
    if (!(targetRateHz > 0.0) || !std::isfinite(targetRateHz))
    {
        return;
    }
    const double sourceRateHz = config_.wave_tcp.frequency_hz;
    const quint64 stride = sourceRateHz > targetRateHz && std::isfinite(sourceRateHz)
        ? static_cast<quint64>(std::max(1.0, std::round(sourceRateHz / targetRateHz)))
        : 1ULL;
    const quint64 now = nowUs();
    const quint64 minIntervalUs = static_cast<quint64>(std::max(1.0, 1'000'000.0 / targetRateHz));
    const bool frameStrideDue = stride <= 1ULL || (wave_frame_count_ % stride == 0ULL);
    if (!frameStrideDue)
    {
        return;
    }
    if (last_feature_compute_time_us_ != 0ULL && now - last_feature_compute_time_us_ < minIntervalUs)
    {
        return;
    }

    const int sampleCount = harmonic.size();
    const int searchStart = std::clamp(config_.wave_tcp.peak_search_start_index, 0, sampleCount);
    const int searchEnd = config_.wave_tcp.peak_search_end_index <= 0
        ? sampleCount
        : std::clamp(config_.wave_tcp.peak_search_end_index, 0, sampleCount);

    double sum = 0.0;
    double sq = 0.0;
    float minValue = std::numeric_limits<float>::infinity();
    float maxValue = -std::numeric_limits<float>::infinity();
    bool hasSearchPeak = false;
    float searchPeak = -std::numeric_limits<float>::infinity();
    int peakIndex = -1;
    int finiteCount = 0;
    for (int i = 0; i < harmonic.size(); ++i)
    {
        const float value = harmonic.at(i);
        if (!std::isfinite(value))
        {
            continue;
        }
        ++finiteCount;
        sum += value;
        sq += static_cast<double>(value) * value;
        if (value < minValue) minValue = value;
        if (value > maxValue) maxValue = value;
        if (i >= searchStart && i < searchEnd && value > searchPeak)
        {
            hasSearchPeak = true;
            searchPeak = value;
            peakIndex = i;
        }
    }
    if (finiteCount <= 0 || !std::isfinite(minValue) || !std::isfinite(maxValue) || searchStart >= searchEnd || !hasSearchPeak)
    {
        latest_feature_ = WaveformFeature();
        latest_feature_.host_time_us = now;
        latest_feature_.original_point_count = static_cast<quint32>(sampleCount);
        latest_feature_.search_start_index = static_cast<quint32>(searchStart);
        latest_feature_.search_end_index = static_cast<quint32>(searchEnd);
        latest_feature_.quality_flags = 1u;
        last_feature_compute_time_us_ = now;
        return;
    }
    latest_feature_.host_time_us = now;
    latest_feature_.epsilon_time_us = latest_epsilon_.device_timestamp_us;
    latest_feature_.original_point_count = static_cast<quint32>(sampleCount);
    latest_feature_.search_start_index = static_cast<quint32>(searchStart);
    latest_feature_.search_end_index = static_cast<quint32>(searchEnd);
    latest_feature_.channel_id = 4;
    latest_feature_.peak = searchPeak;
    latest_feature_.mean = static_cast<float>(sum / finiteCount);
    latest_feature_.rms = static_cast<float>(std::sqrt(sq / finiteCount));
    latest_feature_.peak_index = static_cast<float>(peakIndex);
    latest_feature_.peak_x = static_cast<float>(peakIndex);
    latest_feature_.min_value = minValue;
    latest_feature_.max_value = maxValue;
    latest_feature_.quality_flags = 0;
    ++feature_frame_count_;
    last_feature_compute_time_us_ = now;
    emit waveformFeatureUpdated(latest_feature_);
}

void SkyDeviceManager::invalidateDeviceData(SkyDeviceId id)
{
    switch (id)
    {
    case SkyDeviceId::Epsilon:
        latest_epsilon_ = EpsilonData();
        epsilon_status_.last_data_time_us = 0;
        break;
    case SkyDeviceId::Ptb:
        latest_ptb_ = PtbData();
        ptb_status_.last_data_time_us = 0;
        break;
    case SkyDeviceId::Hmp:
        latest_hmp_ = HmpData();
        hmp_status_.last_data_time_us = 0;
        break;
    case SkyDeviceId::Lidar:
        latest_lidar_ = LidarData();
        lidar_status_.last_data_time_us = 0;
        break;
    case SkyDeviceId::WaveTcp:
        latest_waveform_.clear();
        latest_feature_ = WaveformFeature();
        wave_frame_count_ = 0;
        feature_frame_count_ = 0;
        last_feature_compute_time_us_ = 0;
        wave_tcp_status_.last_data_time_us = 0;
        break;
    case SkyDeviceId::All:
        invalidateDeviceData(SkyDeviceId::Epsilon);
        invalidateDeviceData(SkyDeviceId::Ptb);
        invalidateDeviceData(SkyDeviceId::Hmp);
        invalidateDeviceData(SkyDeviceId::Lidar);
        invalidateDeviceData(SkyDeviceId::WaveTcp);
        break;
    }
}

}  // namespace VaporView
