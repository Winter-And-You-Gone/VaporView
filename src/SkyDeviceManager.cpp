#include "SkyDeviceManager.h"

#include <QDateTime>
#include <QJsonObject>
#include <QMetaObject>
#include <QPointer>
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <limits>

namespace VaporView
{
namespace
{
constexpr int kWaveTcpHeaderSize = 4;
constexpr int kWaveTcpFloatSize = 4;
constexpr quint32 kMaxWaveTcpPayloadBytes = 16u * 1024u * 1024u;
constexpr double kPi = 3.14159265358979323846;

enum class WaveTcpHeaderOrder
{
    LittleEndian,
    BigEndian,
};

quint64 nowUs()
{
    return static_cast<quint64>(QDateTime::currentDateTimeUtc().toMSecsSinceEpoch()) * 1000ULL;
}

double degToRad(double degrees)
{
    return degrees * kPi / 180.0;
}

double positiveDegrees(double degrees)
{
    double value = std::fmod(degrees, 360.0);
    if (value < 0.0)
    {
        value += 360.0;
    }
    return value;
}

void setQuaternionFromEuler(EpsilonData& data)
{
    const double roll = degToRad(data.roll_deg);
    const double pitch = degToRad(data.pitch_deg);
    const double yaw = degToRad(data.yaw_deg);
    const double cr = std::cos(roll * 0.5);
    const double sr = std::sin(roll * 0.5);
    const double cp = std::cos(pitch * 0.5);
    const double sp = std::sin(pitch * 0.5);
    const double cy = std::cos(yaw * 0.5);
    const double sy = std::sin(yaw * 0.5);
    data.quat_w = cr * cp * cy + sr * sp * sy;
    data.quat_x = sr * cp * cy - cr * sp * sy;
    data.quat_y = cr * sp * cy + sr * cp * sy;
    data.quat_z = cr * cp * sy - sr * sp * cy;
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

quint32 decodeWaveTcpHeader(const char *raw, WaveTcpHeaderOrder order)
{
    const uchar *bytes = reinterpret_cast<const uchar*>(raw);
    return order == WaveTcpHeaderOrder::LittleEndian
        ? qFromLittleEndian<quint32>(bytes)
        : qFromBigEndian<quint32>(bytes);
}

bool isValidWaveTcpPayloadSize(quint32 size)
{
    return size > 0 &&
           size <= kMaxWaveTcpPayloadBytes &&
           (size % kWaveTcpFloatSize) == 0;
}

QString waveTcpHeaderOrderText(WaveTcpHeaderOrder order)
{
    return order == WaveTcpHeaderOrder::LittleEndian
        ? QStringLiteral("little-endian")
        : QStringLiteral("big-endian");
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

    auto reconfigureDevice = [this, &result](SkyDeviceId id, bool changed, bool enabled) {
        bool reconfigured = false;
        if (changed)
        {
            CommandErrorCode ignored = CommandErrorCode::Ok;
            disconnectDevice(id, &ignored);
            reconfigured = true;
            if (enabled)
            {
                CommandErrorCode connectError = CommandErrorCode::Ok;
                if (!connectDevice(id, &connectError))
                {
                    result.success = false;
                    if (result.error_code == CommandErrorCode::Ok)
                    {
                        result.error_code = connectError;
                    }
                }
            }
            else
            {
                setState(id, DeviceState::Disabled);
            }
        }
        return reconfigured;
    };

    const bool epsilonReconfigured = reconfigureDevice(SkyDeviceId::Epsilon, diff.epsilon_changed, config_.epsilon.enabled);
    const bool ptbReconfigured = reconfigureDevice(SkyDeviceId::Ptb, diff.ptb_changed, config_.ptb.enabled);
    const bool hmpReconfigured = reconfigureDevice(SkyDeviceId::Hmp, diff.hmp_changed, config_.hmp.enabled);
    const bool lidarReconfigured = reconfigureDevice(SkyDeviceId::Lidar, diff.lidar_changed, config_.lidar.enabled);
    const bool waveReconfigured = reconfigureDevice(SkyDeviceId::WaveTcp, diff.wave_tcp_changed, config_.wave_tcp.enabled);

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
    result.json["error_code"] = static_cast<int>(result.error_code);
    result.json["error"] = commandErrorCodeText(result.error_code);
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

QVector<float> SkyDeviceManager::latestRawWaveform() const
{
    return latest_raw_waveform_;
}

QVector<float> SkyDeviceManager::latestWaveform() const
{
    return latest_waveform_;
}

WaveformFeature SkyDeviceManager::latestWaveformFeature() const
{
    return latest_feature_;
}

double SkyDeviceManager::waveTcpActualRateHz() const
{
    if (wave_tcp_status_.state != DeviceState::Connected || wave_tcp_status_.last_data_time_us == 0)
    {
        return 0.0;
    }
    const quint64 now = nowUs();
    if (now < wave_tcp_status_.last_data_time_us || now - wave_tcp_status_.last_data_time_us > 3'000'000ULL)
    {
        return 0.0;
    }
    if (wave_frame_time_samples_us_.size() < 2)
    {
        return 0.0;
    }
    const quint64 first = wave_frame_time_samples_us_.first();
    const quint64 last = wave_frame_time_samples_us_.last();
    if (last <= first)
    {
        return 0.0;
    }
    return static_cast<double>(wave_frame_time_samples_us_.size() - 1) * 1'000'000.0 /
           static_cast<double>(last - first);
}

void SkyDeviceManager::generateSimulatedData()
{
    simulate_phase_ += 0.04;
    const quint64 t = nowUs();

    if (epsilon_status_.state == DeviceState::Connected)
    {
        const double phaseRateRadPerSecond = 0.4;
        latest_epsilon_.valid = true;
        latest_epsilon_.timestamp = std::chrono::steady_clock::now();
        latest_epsilon_.device_timestamp_us = t;
        latest_epsilon_.utc_unix_s = t / 1000000ULL;
        latest_epsilon_.utc_microseconds = static_cast<quint32>(t % 1000000ULL);
        latest_epsilon_.gnss_fix_code = 6;
        latest_epsilon_.gnss_fix_text = "RTK_FIXED";
        latest_epsilon_.filter_status_bits = static_cast<uint16_t>(latest_epsilon_.gnss_fix_code << 4);
        latest_epsilon_.gnss_satellites = 18 + static_cast<int>(std::lround(std::sin(simulate_phase_ * 0.12) * 2.0));
        latest_epsilon_.latitude_deg = 31.2304 + std::sin(simulate_phase_) * 0.0001;
        latest_epsilon_.longitude_deg = 121.4737 + std::cos(simulate_phase_) * 0.0001;
        latest_epsilon_.height_m = 1200.0 + std::sin(simulate_phase_ * 0.7) * 3.0;
        latest_epsilon_.ecef_x_m = 1000.0 + std::sin(simulate_phase_) * 5.0;
        latest_epsilon_.ecef_y_m = 2000.0 + std::cos(simulate_phase_) * 5.0;
        latest_epsilon_.ecef_z_m = 3000.0 + std::sin(simulate_phase_ * 0.5) * 5.0;
        latest_epsilon_.ned_n_m = std::sin(simulate_phase_ * 0.8) * 4.0;
        latest_epsilon_.ned_e_m = std::cos(simulate_phase_ * 0.6) * 4.0;
        latest_epsilon_.ned_d_m = -std::sin(simulate_phase_ * 0.4) * 1.5;
        latest_epsilon_.vel_n_mps = 4.0 * 0.8 * phaseRateRadPerSecond * std::cos(simulate_phase_ * 0.8);
        latest_epsilon_.vel_e_mps = -4.0 * 0.6 * phaseRateRadPerSecond * std::sin(simulate_phase_ * 0.6);
        latest_epsilon_.vel_d_mps = -1.5 * 0.4 * phaseRateRadPerSecond * std::cos(simulate_phase_ * 0.4);
        latest_epsilon_.body_vel_x_mps = latest_epsilon_.vel_n_mps * 0.95 + std::sin(simulate_phase_ * 0.3) * 0.05;
        latest_epsilon_.body_vel_y_mps = latest_epsilon_.vel_e_mps * 0.95 + std::cos(simulate_phase_ * 0.3) * 0.05;
        latest_epsilon_.body_vel_z_mps = latest_epsilon_.vel_d_mps;
        latest_epsilon_.body_acc_x_mps2 =
            -4.0 * std::pow(0.8 * phaseRateRadPerSecond, 2.0) * std::sin(simulate_phase_ * 0.8);
        latest_epsilon_.body_acc_y_mps2 =
            -4.0 * std::pow(0.6 * phaseRateRadPerSecond, 2.0) * std::cos(simulate_phase_ * 0.6);
        latest_epsilon_.body_acc_z_mps2 =
            1.5 * std::pow(0.4 * phaseRateRadPerSecond, 2.0) * std::sin(simulate_phase_ * 0.4);
        latest_epsilon_.roll_deg = std::sin(simulate_phase_ * 0.5) * 2.0;
        latest_epsilon_.pitch_deg = std::cos(simulate_phase_ * 0.45) * 1.5;
        latest_epsilon_.yaw_deg = positiveDegrees(85.0 + simulate_phase_ * 8.0 + std::sin(simulate_phase_ * 0.2) * 3.0);
        setQuaternionFromEuler(latest_epsilon_);
        latest_epsilon_.ang_vel_x_radps =
            degToRad(2.0 * 0.5 * phaseRateRadPerSecond * std::cos(simulate_phase_ * 0.5));
        latest_epsilon_.ang_vel_y_radps =
            degToRad(-1.5 * 0.45 * phaseRateRadPerSecond * std::sin(simulate_phase_ * 0.45));
        latest_epsilon_.ang_vel_z_radps =
            degToRad((8.0 + 3.0 * 0.2 * std::cos(simulate_phase_ * 0.2)) * phaseRateRadPerSecond);
        latest_epsilon_.imu_acc_x_mps2 = latest_epsilon_.body_acc_x_mps2 + std::sin(simulate_phase_ * 1.3) * 0.02;
        latest_epsilon_.imu_acc_y_mps2 = latest_epsilon_.body_acc_y_mps2 + std::cos(simulate_phase_ * 1.1) * 0.02;
        latest_epsilon_.imu_acc_z_mps2 = 9.80665 + latest_epsilon_.body_acc_z_mps2;
        latest_epsilon_.imu_gyr_x_radps = latest_epsilon_.ang_vel_x_radps;
        latest_epsilon_.imu_gyr_y_radps = latest_epsilon_.ang_vel_y_radps;
        latest_epsilon_.imu_gyr_z_radps = latest_epsilon_.ang_vel_z_radps;
        latest_epsilon_.mag_x_mg = 280.0 + std::sin(simulate_phase_ * 0.2) * 5.0;
        latest_epsilon_.mag_y_mg = -35.0 + std::cos(simulate_phase_ * 0.25) * 4.0;
        latest_epsilon_.mag_z_mg = 410.0 + std::sin(simulate_phase_ * 0.18) * 6.0;
        latest_epsilon_.hdop = 0.65 + std::abs(std::sin(simulate_phase_ * 0.2)) * 0.08;
        latest_epsilon_.vdop = 0.95 + std::abs(std::cos(simulate_phase_ * 0.17)) * 0.12;
        latest_epsilon_.hacc_m = 0.018 + std::abs(std::sin(simulate_phase_ * 0.3)) * 0.006;
        latest_epsilon_.vacc_m = 0.028 + std::abs(std::cos(simulate_phase_ * 0.25)) * 0.008;
        latest_epsilon_.lat_std_m = latest_epsilon_.hacc_m * 0.7;
        latest_epsilon_.lon_std_m = latest_epsilon_.hacc_m * 0.8;
        latest_epsilon_.height_std_m = latest_epsilon_.vacc_m;
        latest_epsilon_.diff_age_s = 0.8 + std::abs(std::sin(simulate_phase_ * 0.15)) * 0.4;
        latest_epsilon_.heading_valid = true;
        latest_epsilon_.raw_frame_count++;
        latest_epsilon_.imu_packet_rate_hz = 100.0;
        latest_epsilon_.ahrs_packet_rate_hz = 50.0;
        latest_epsilon_.insgps_packet_rate_hz = 50.0;
        latest_epsilon_.sys_state_packet_rate_hz = 10.0;
        latest_epsilon_.raw_gnss_packet_rate_hz = 1.0;
        latest_epsilon_.satellite_packet_rate_hz = 1.0;
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
        if (latest_raw_waveform_.isEmpty())
        {
            latest_raw_waveform_.resize(50000);
        }
        if (latest_waveform_.isEmpty())
        {
            latest_waveform_.resize(50000);
        }
        for (int i = 0; i < latest_waveform_.size(); ++i)
        {
            const double x = static_cast<double>(i) / 500.0;
            latest_raw_waveform_[i] = static_cast<float>(0.4 * std::sin(x * 0.37 + simulate_phase_ * 0.8) +
                                                         0.15 * std::sin(x * 2.2));
            latest_waveform_[i] = static_cast<float>(std::sin(x + simulate_phase_) * 0.05 + std::exp(-std::pow((i - 24000) / 3500.0, 2.0)));
        }
        wave_tcp_status_.rx_count++;
        wave_tcp_status_.last_data_time_us = t;
        publishWaveform(latest_raw_waveform_, latest_waveform_);
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
        epsilon_->setDataCallback([self = QPointer<SkyDeviceManager>(this), weakCollector = std::weak_ptr<EpsilonCollector>(epsilon_)]() {
            if (!self)
            {
                return;
            }
            const std::shared_ptr<EpsilonCollector> collector = weakCollector.lock();
            if (!collector)
            {
                return;
            }
            const EpsilonData data = collector->getLatestData();
            QMetaObject::invokeMethod(self.data(), [self, collector, data]() {
                if (!self || self->epsilon_ != collector)
                {
                    return;
                }
                self->handleEpsilonData(data);
            }, Qt::QueuedConnection);
        });
        epsilon_->setRawFrameCallback([self = QPointer<SkyDeviceManager>(this), weakCollector = std::weak_ptr<EpsilonCollector>(epsilon_)](
                                          uint64_t hostTimestampUs,
                                          uint8_t packetId,
                                          uint8_t serialNumber,
                                          const uint8_t* frameData,
                                          size_t size) {
            if (!self || !frameData || size > static_cast<size_t>(std::numeric_limits<int>::max()))
            {
                return;
            }
            const std::shared_ptr<EpsilonCollector> collector = weakCollector.lock();
            if (!collector)
            {
                return;
            }
            const QByteArray frame(reinterpret_cast<const char*>(frameData), static_cast<int>(size));
            QMetaObject::invokeMethod(self.data(), [self, collector, hostTimestampUs, packetId, serialNumber, frame]() {
                if (!self || self->epsilon_ != collector)
                {
                    return;
                }
                emit self->epsilonRawFrameReceived(static_cast<quint64>(hostTimestampUs), packetId, serialNumber, frame);
            }, Qt::QueuedConnection);
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
        ptb_->setDataCallback([self = QPointer<SkyDeviceManager>(this), weakCollector = std::weak_ptr<PtbCollector>(ptb_)]() {
            if (!self)
            {
                return;
            }
            const std::shared_ptr<PtbCollector> collector = weakCollector.lock();
            if (!collector)
            {
                return;
            }
            const PtbData data = collector->getLatestData();
            QMetaObject::invokeMethod(self.data(), [self, collector, data]() {
                if (!self || self->ptb_ != collector)
                {
                    return;
                }
                self->handlePtbData(data);
            }, Qt::QueuedConnection);
        });
        ptb_->setRawResponseCallback([self = QPointer<SkyDeviceManager>(this), weakCollector = std::weak_ptr<PtbCollector>(ptb_)](
                                         uint64_t hostTimestampUs,
                                         const uint8_t* responseData,
                                         size_t size) {
            if (!self || !responseData || size > static_cast<size_t>(std::numeric_limits<int>::max()))
            {
                return;
            }
            const std::shared_ptr<PtbCollector> collector = weakCollector.lock();
            if (!collector)
            {
                return;
            }
            const QByteArray response(reinterpret_cast<const char*>(responseData), static_cast<int>(size));
            QMetaObject::invokeMethod(self.data(), [self, collector, hostTimestampUs, response]() {
                if (!self || self->ptb_ != collector)
                {
                    return;
                }
                emit self->ptbRawResponseReceived(static_cast<quint64>(hostTimestampUs), response);
            }, Qt::QueuedConnection);
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
        hmp_->setDataCallback([self = QPointer<SkyDeviceManager>(this), weakCollector = std::weak_ptr<HmpCollector>(hmp_)]() {
            if (!self)
            {
                return;
            }
            const std::shared_ptr<HmpCollector> collector = weakCollector.lock();
            if (!collector)
            {
                return;
            }
            const HmpData data = collector->getLatestData();
            QMetaObject::invokeMethod(self.data(), [self, collector, data]() {
                if (!self || self->hmp_ != collector)
                {
                    return;
                }
                self->handleHmpData(data);
            }, Qt::QueuedConnection);
        });
        hmp_->setRawResponseCallback([self = QPointer<SkyDeviceManager>(this), weakCollector = std::weak_ptr<HmpCollector>(hmp_)](
                                         uint64_t hostTimestampUs,
                                         const uint8_t* responseData,
                                         size_t size) {
            if (!self || !responseData || size > static_cast<size_t>(std::numeric_limits<int>::max()))
            {
                return;
            }
            const std::shared_ptr<HmpCollector> collector = weakCollector.lock();
            if (!collector)
            {
                return;
            }
            const QByteArray response(reinterpret_cast<const char*>(responseData), static_cast<int>(size));
            QMetaObject::invokeMethod(self.data(), [self, collector, hostTimestampUs, response]() {
                if (!self || self->hmp_ != collector)
                {
                    return;
                }
                emit self->hmpRawResponseReceived(static_cast<quint64>(hostTimestampUs), response);
            }, Qt::QueuedConnection);
        });
        if (!hmp_->start(config.port.toStdString(), SerialConfig::N82(config.baud_rate))) return fail(CommandErrorCode::DeviceConnectFailed);
        if (!hmp_->checkDeviceResponse()) return fail(CommandErrorCode::DeviceConnectFailed);
        if (!hmp_->startStreaming()) return fail(CommandErrorCode::DeviceConnectFailed);
        break;
    case SkyDeviceId::Lidar:
        lidar_ = std::make_shared<LidarCollector>();
        lidar_->setLogCallback(logCallback);
        lidar_->setSampleRate(static_cast<int>(config.frequency_hz));
        lidar_->setDataCallback([self = QPointer<SkyDeviceManager>(this), weakCollector = std::weak_ptr<LidarCollector>(lidar_)]() {
            if (!self)
            {
                return;
            }
            const std::shared_ptr<LidarCollector> collector = weakCollector.lock();
            if (!collector)
            {
                return;
            }
            const LidarData data = collector->getLatestData();
            QMetaObject::invokeMethod(self.data(), [self, collector, data]() {
                if (!self || self->lidar_ != collector)
                {
                    return;
                }
                self->handleLidarData(data);
            }, Qt::QueuedConnection);
        });
        lidar_->setRawFrameCallback([self = QPointer<SkyDeviceManager>(this), weakCollector = std::weak_ptr<LidarCollector>(lidar_)](
                                        uint64_t hostTimestampUs,
                                        LidarProtocol protocol,
                                        const uint8_t* frameData,
                                        size_t size) {
            if (!self || !frameData || size > static_cast<size_t>(std::numeric_limits<int>::max()))
            {
                return;
            }
            const std::shared_ptr<LidarCollector> collector = weakCollector.lock();
            if (!collector)
            {
                return;
            }
            const QByteArray frame(reinterpret_cast<const char*>(frameData), static_cast<int>(size));
            QMetaObject::invokeMethod(self.data(), [self, collector, hostTimestampUs, protocol, frame]() {
                if (!self || self->lidar_ != collector)
                {
                    return;
                }
                emit self->lidarRawFrameReceived(
                    static_cast<quint64>(hostTimestampUs),
                    static_cast<quint16>(protocol),
                    frame);
            }, Qt::QueuedConnection);
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

void SkyDeviceManager::handleEpsilonData(const EpsilonData& data)
{
    latest_epsilon_ = data;
    epsilon_status_.rx_count++;
    epsilon_status_.last_data_time_us = nowUs();
    emit epsilonDataUpdated(latest_epsilon_);
}

void SkyDeviceManager::handlePtbData(const PtbData& data)
{
    latest_ptb_ = data;
    ptb_status_.rx_count++;
    ptb_status_.last_data_time_us = nowUs();
    emit ptbDataUpdated(latest_ptb_);
}

void SkyDeviceManager::handleHmpData(const HmpData& data)
{
    latest_hmp_ = data;
    hmp_status_.rx_count++;
    hmp_status_.last_data_time_us = nowUs();
    emit hmpDataUpdated(latest_hmp_);
}

void SkyDeviceManager::handleLidarData(const LidarData& data)
{
    latest_lidar_ = data;
    lidar_status_.rx_count++;
    lidar_status_.last_data_time_us = nowUs();
    emit lidarDataUpdated(latest_lidar_);
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
    while (wave_buffer_.size() >= kWaveTcpHeaderSize)
    {
        bool foundCandidate = false;
        bool frameComplete = false;
        int frameOffset = 0;
        quint32 rawSize = 0;
        quint32 harmonicSize = 0;
        WaveTcpHeaderOrder headerOrder = WaveTcpHeaderOrder::LittleEndian;

        const WaveTcpHeaderOrder orders[] = {
            WaveTcpHeaderOrder::LittleEndian,
            WaveTcpHeaderOrder::BigEndian,
        };
        for (int offset = 0; offset <= wave_buffer_.size() - kWaveTcpHeaderSize && !foundCandidate; ++offset)
        {
            for (WaveTcpHeaderOrder order : orders)
            {
                const quint32 candidateRawSize = decodeWaveTcpHeader(wave_buffer_.constData() + offset, order);
                if (!isValidWaveTcpPayloadSize(candidateRawSize))
                {
                    continue;
                }

                const qsizetype secondHeaderOffset =
                    static_cast<qsizetype>(offset) + kWaveTcpHeaderSize + static_cast<qsizetype>(candidateRawSize);
                if (secondHeaderOffset + kWaveTcpHeaderSize > wave_buffer_.size())
                {
                    foundCandidate = true;
                    frameOffset = offset;
                    rawSize = candidateRawSize;
                    headerOrder = order;
                    break;
                }

                const quint32 candidateHarmonicSize = decodeWaveTcpHeader(wave_buffer_.constData() + secondHeaderOffset, order);
                if (!isValidWaveTcpPayloadSize(candidateHarmonicSize))
                {
                    continue;
                }

                const qsizetype frameSize =
                    secondHeaderOffset + kWaveTcpHeaderSize + static_cast<qsizetype>(candidateHarmonicSize);
                foundCandidate = true;
                frameComplete = frameSize <= wave_buffer_.size();
                frameOffset = offset;
                rawSize = candidateRawSize;
                harmonicSize = candidateHarmonicSize;
                headerOrder = order;
                break;
            }
        }

        if (!foundCandidate)
        {
            const int bytesToDrop = static_cast<int>(
                std::max<qsizetype>(0, wave_buffer_.size() - (kWaveTcpHeaderSize - 1)));
            if (bytesToDrop > 0)
            {
                wave_buffer_.remove(0, bytesToDrop);
                emit logMessage(QStringLiteral("Wave TCP resync: discarded %1 byte(s) while looking for a valid frame header")
                                    .arg(bytesToDrop));
            }
            return;
        }

        if (frameOffset > 0)
        {
            wave_buffer_.remove(0, frameOffset);
            emit logMessage(QStringLiteral("Wave TCP resync: skipped %1 byte(s), header order %2")
                                .arg(frameOffset)
                                .arg(waveTcpHeaderOrderText(headerOrder)));
        }

        if (!frameComplete)
        {
            return;
        }

        const int rawPayloadSize = static_cast<int>(rawSize);
        const int harmonicPayloadSize = static_cast<int>(harmonicSize);
        const int rawPayloadOffset = kWaveTcpHeaderSize;
        const int harmonicHeaderOffset = kWaveTcpHeaderSize + rawPayloadSize;
        const int harmonicPayloadOffset = harmonicHeaderOffset + kWaveTcpHeaderSize;
        const int totalFrameSize = harmonicPayloadOffset + harmonicPayloadSize;
        const QByteArray rawPayload = wave_buffer_.mid(rawPayloadOffset, rawPayloadSize);
        const QByteArray harmonicPayload = wave_buffer_.mid(harmonicPayloadOffset, harmonicPayloadSize);
        wave_buffer_.remove(0, totalFrameSize);
        if (wave_float_encoding_ == TcpFloatEncoding::Unknown)
        {
            const QByteArray& payloadForDetection = harmonicPayload.isEmpty() ? rawPayload : harmonicPayload;
            wave_float_encoding_ = autoDetectTcpFloatEncoding(payloadForDetection);
            emit logMessage(QStringLiteral("Wave TCP payload format locked: header=%1, float=%2")
                                .arg(waveTcpHeaderOrderText(headerOrder))
                                .arg(tcpFloatEncodingLabel(false, wave_float_encoding_)));
        }
        const quint64 timestampUs = nowUs();
        emit tcpRawWaveFrameReceived(timestampUs, rawPayload, harmonicPayload, wave_float_encoding_);
        publishWaveform(decodeTcpFloatPayload(rawPayload, wave_float_encoding_),
                        decodeTcpFloatPayload(harmonicPayload, wave_float_encoding_));
        wave_tcp_status_.rx_count++;
        wave_tcp_status_.last_data_time_us = timestampUs;
    }
}

void SkyDeviceManager::publishWaveform(const QVector<float>& raw, const QVector<float>& harmonic)
{
    if (wave_tcp_status_.state != DeviceState::Connected)
    {
        return;
    }
    const quint64 now = nowUs();
    latest_raw_waveform_ = raw;
    latest_waveform_ = harmonic;
    if (harmonic.isEmpty())
    {
        return;
    }
    ++wave_frame_count_;
    recordWaveTcpFrameTime(now);
    emit waveformUpdated(now, harmonic);

    const double targetRateHz = config_.telemetry.feature_rate_hz;
    if (!(targetRateHz > 0.0) || !std::isfinite(targetRateHz))
    {
        return;
    }
    const quint64 minIntervalUs = static_cast<quint64>(std::max(1.0, 1'000'000.0 / targetRateHz));
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
        latest_feature_.epsilon_time_us = latest_epsilon_.device_timestamp_us;
        latest_feature_.original_point_count = static_cast<quint32>(sampleCount);
        latest_feature_.search_start_index = static_cast<quint32>(searchStart);
        latest_feature_.search_end_index = static_cast<quint32>(searchEnd);
        latest_feature_.channel_id = 4;
        latest_feature_.quality_flags = 1u;
        last_feature_compute_time_us_ = now;
        emit waveformFeatureUpdated(latest_feature_);
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

void SkyDeviceManager::recordWaveTcpFrameTime(quint64 timestampUs)
{
    wave_frame_time_samples_us_.push_back(timestampUs);
    while (!wave_frame_time_samples_us_.isEmpty() &&
           timestampUs >= wave_frame_time_samples_us_.front() &&
           timestampUs - wave_frame_time_samples_us_.front() > 5'000'000ULL)
    {
        wave_frame_time_samples_us_.removeFirst();
    }
    while (wave_frame_time_samples_us_.size() > 2048)
    {
        wave_frame_time_samples_us_.removeFirst();
    }
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
        latest_raw_waveform_.clear();
        latest_waveform_.clear();
        latest_feature_ = WaveformFeature();
        wave_float_encoding_ = TcpFloatEncoding::Unknown;
        wave_frame_count_ = 0;
        feature_frame_count_ = 0;
        last_feature_compute_time_us_ = 0;
        wave_frame_time_samples_us_.clear();
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
