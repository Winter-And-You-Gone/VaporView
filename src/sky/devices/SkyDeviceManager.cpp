#include "SkyDeviceManager.h"

#include "serial_port.h"

#include <QDateTime>
#include <QJsonObject>
#include <QMetaObject>
#include <QPointer>
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <string>
#include <vector>

namespace VaporView
{
namespace
{
constexpr int kWaveTcpHeaderSize = 4;
constexpr int kWaveTcpFloatSize = 4;
constexpr quint32 kMaxWaveTcpPayloadBytes = 16u * 1024u * 1024u;
constexpr int kRawEventDrainBatchSize = 64;
constexpr int kMaxRtcmCorrectionPayloadBytes = 4096;
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

int temperatureRs485BaudRateForIndex(quint16 index)
{
    switch (index)
    {
    case 0: return 4800;
    case 1: return 9600;
    case 2: return 19200;
    case 3: return 38400;
    case 4: return 57600;
    case 5: return 115200;
    case 6: return 230400;
    case 7: return 460800;
    default: return 9600;
    }
}

QVariantMap structuredLogFieldsFromStd(const DataCollector::StructuredLogFields& fields)
{
    QVariantMap result;
    for (const auto& [key, value] : fields)
    {
        result.insert(QString::fromStdString(key), QString::fromStdString(value));
    }
    return result;
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

bool deviceEnabled(const SkyConfig& config, SkyDeviceId id)
{
    switch (id)
    {
    case SkyDeviceId::Epsilon: return config.epsilon.enabled;
    case SkyDeviceId::Ptb: return config.ptb.enabled;
    case SkyDeviceId::Hmp: return config.hmp.enabled;
    case SkyDeviceId::Lidar: return config.lidar.enabled;
    case SkyDeviceId::TemperatureController: return config.temperature_controller.enabled;
    case SkyDeviceId::Ai8TemperatureController: return config.ai8_temperature_controller.enabled;
    case SkyDeviceId::WaveTcp: return config.wave_tcp.enabled;
    case SkyDeviceId::All: return true;
    }
    return false;
}

PressureSensorProtocol pressureProtocolForSource(const QString& source)
{
    return source.trimmed().compare(QStringLiteral("bmp390"), Qt::CaseInsensitive) == 0
        ? PressureSensorProtocol::Bmp390Serial
        : PressureSensorProtocol::Ptb210;
}

HumiditySensorProtocol humidityProtocolForSource(const QString& source)
{
    return source.trimmed().compare(QStringLiteral("sht45"), Qt::CaseInsensitive) == 0
        ? HumiditySensorProtocol::Sht45Serial
        : HumiditySensorProtocol::Hmp3Modbus;
}

SerialDeviceConfig serialConfigFromTemperatureConfig(const TemperatureControllerConfig& config)
{
    SerialDeviceConfig serial;
    serial.enabled = config.enabled;
    serial.port = config.port;
    serial.baud_rate = config.baud_rate;
    serial.frequency_hz = config.frequency_hz;
    return serial;
}

SerialDeviceConfig serialConfigFromEpsilonConfig(const EpsilonSerialConfig& config)
{
    SerialDeviceConfig serial;
    serial.enabled = config.enabled;
    serial.port = config.port;
    serial.baud_rate = config.baud_rate;
    serial.frequency_hz = kDefaultEpsilonCallbackRateHz;
    return serial;
}

void initializeSimulatedTemperatureController(TemperatureControllerData& data)
{
    if (data.valid)
    {
        return;
    }
    data = TemperatureControllerData();
    data.valid = true;
    data.controller_mode = 0;
    data.device_address = 1;
    data.rs485_baud_index = 3;
    data.overtemp_output_mode = 1;
    data.internal_temperature_c = 32.0;
    for (int i = 0; i < static_cast<int>(data.channels.size()); ++i)
    {
        TemperatureControllerChannelData& channel = data.channels[static_cast<size_t>(i)];
        channel.target_temperature_c = 25.0 + i;
        channel.measured_temperature_c = 23.0 + i;
        channel.output_enabled = false;
        channel.output_mode = 1;
        channel.max_output_percent = 70;
        channel.output_percent = 0.0;
        channel.output_current_a = 0.0;
        channel.auto_pid_mode = 0;
        channel.kp = 100;
        channel.ki = 20;
        channel.kd = 5;
        channel.overtemp_upper_c = 80.0;
        channel.overtemp_lower_c = -20.0;
        channel.temperature_slope_c_per_s = 0.0;
        channel.startup_delay_s = 3;
        channel.sensor_resistance_ohm = 10000.0;
    }
}

void initializeSimulatedAi8State(
    std::array<Ai8TemperatureControllerProtocol::ChannelParameters,
               Ai8TemperatureControllerProtocol::kChannelCount>& channels,
    std::array<Ai8TemperatureControllerProtocol::InputParameters,
               Ai8TemperatureControllerProtocol::kParameterGroupCount>& inputs,
    std::array<Ai8TemperatureControllerProtocol::OutputParameters,
               Ai8TemperatureControllerProtocol::kParameterGroupCount>& outputs,
    Ai8TemperatureControllerProtocol::GlobalParameters& global)
{
    using namespace Ai8TemperatureControllerProtocol;
    for (int index = 0; index < kChannelCount; ++index)
    {
        auto& channel = channels[static_cast<size_t>(index)];
        channel = ChannelParameters{};
        channel.setpointC = 25.0;
        channel.measuredC = 24.5;
        channel.proportionalBand = 10.0;
        channel.integralTimeS = 20.0;
        channel.derivativeTimeS = 5.0;
        channel.channelInputGroup = 1;
        channel.channelOutputGroupRaw = 1;
        channel.highAlarmC = 80.0;
        channel.lowAlarmC = -20.0;
        channel.displayedSetpointC = channel.setpointC;
    }
    for (int index = 0; index < kParameterGroupCount; ++index)
    {
        auto& input = inputs[static_cast<size_t>(index)];
        input = InputParameters{};
        input.inputType = 1;
        input.scaleLow = 0.0;
        input.scaleHigh = 100.0;
        input.filter = 1;
        input.channelInputGroup = 1;

        auto& output = outputs[static_cast<size_t>(index)];
        output = OutputParameters{};
        output.controlAction = 0;
        output.deviationHighAlarm = 80.0;
        output.deviationLowAlarm = -20.0;
        output.outputLowPercent = 0;
        output.outputHighPercent = 100;
        output.outputHighThreshold = 100.0;
    }
    global = GlobalParameters{};
    global.address = 1;
    global.baudRate = 19200;
    global.localInputChannelCount = kChannelCount;
    global.expansionInputChannelCount = kChannelCount;
    global.controlChannelCount = kChannelCount;
    global.controlCycleS = 1.0;
    global.runStateIsDocumented = true;
    global.decimalPoint = 1;
}

Ai8TemperatureControllerProtocol::PageData simulatedAi8Page(
    Ai8TemperatureControllerProtocol::Page page,
    const Ai8TemperatureControllerProtocol::Selection& selection,
    const std::array<Ai8TemperatureControllerProtocol::ChannelParameters,
                     Ai8TemperatureControllerProtocol::kChannelCount>& channels,
    const std::array<Ai8TemperatureControllerProtocol::InputParameters,
                     Ai8TemperatureControllerProtocol::kParameterGroupCount>& inputs,
    const std::array<Ai8TemperatureControllerProtocol::OutputParameters,
                     Ai8TemperatureControllerProtocol::kParameterGroupCount>& outputs,
    const Ai8TemperatureControllerProtocol::GlobalParameters& global)
{
    using namespace Ai8TemperatureControllerProtocol;
    PageData data;
    data.page = page;
    data.selection = selection;
    data.channel = channels[static_cast<size_t>(selection.channel - 1)];
    data.input = inputs[static_cast<size_t>(selection.inputGroup - 1)];
    data.output = outputs[static_cast<size_t>(selection.outputGroup - 1)];
    data.global = global;
    return data;
}

bool finiteAi8PageValues(const Ai8TemperatureControllerProtocol::PageData& data)
{
    using namespace Ai8TemperatureControllerProtocol;
    const auto finite = [](double value) { return std::isfinite(value); };
    return finite(data.channel.setpointC) && finite(data.channel.measuredC) &&
           finite(data.channel.proportionalBand) && finite(data.channel.integralTimeS) &&
           finite(data.channel.derivativeTimeS) && finite(data.channel.measurementOffset) &&
           finite(data.channel.manualOutputPercent) && finite(data.channel.highAlarmC) &&
           finite(data.channel.lowAlarmC) && finite(data.channel.displayedSetpointC) &&
           finite(data.input.scaleLow) && finite(data.input.scaleHigh) &&
           finite(data.input.measurementOffset) && finite(data.output.deviationHighAlarm) &&
           finite(data.output.deviationLowAlarm) && finite(data.output.hysteresis) &&
           finite(data.output.outputHighThreshold) && finite(data.output.riseSlope) &&
           finite(data.output.fallSlope) && finite(data.output.setpointLowLimit) &&
           finite(data.output.setpointHighLimit);
}

bool validAi8PageValues(const Ai8TemperatureControllerProtocol::PageData& data)
{
    using namespace Ai8TemperatureControllerProtocol;
    return finiteAi8PageValues(data) &&
           data.channel.setpointC >= -999.0 && data.channel.setpointC <= 3200.0 &&
           data.channel.proportionalBand >= 0.0 && data.channel.proportionalBand <= 3200.0 &&
           data.channel.integralTimeS >= 0.0 && data.channel.integralTimeS <= 3200.0 &&
           data.channel.derivativeTimeS >= -327.6 && data.channel.derivativeTimeS <= 327.6 &&
           data.channel.channelInputGroup >= 0 && data.channel.channelInputGroup <= 4 &&
           data.channel.channelOutputGroupRaw >= 0 && data.channel.channelOutputGroupRaw <= 4 &&
           data.channel.programNumber >= 0 && data.channel.programNumber <= 9999 &&
           data.channel.workMode >= 0 && data.channel.workMode <= 5 &&
           data.channel.manualOutputPercent >= 0.0 && data.channel.manualOutputPercent <= 100.0 &&
           data.channel.highAlarmC >= -999.0 && data.channel.highAlarmC <= 3200.0 &&
           data.channel.lowAlarmC >= -999.0 && data.channel.lowAlarmC <= 3200.0 &&
           data.input.inputType >= 0 && data.input.inputType <= 51 &&
           data.input.scaleLow >= -999.0 && data.input.scaleLow <= 3200.0 &&
           data.input.scaleHigh >= -999.0 && data.input.scaleHigh <= 3200.0 &&
           data.input.filter >= 0 && data.input.filter <= 999 &&
           data.input.channelInputGroup >= 0 && data.input.channelInputGroup <= 4 &&
           data.input.measurementOffset >= -999.0 && data.input.measurementOffset <= 3200.0 &&
           data.input.correctionEntry >= 0 && data.input.correctionEntry <= 999 &&
           data.output.controlAction >= 0 && data.output.controlAction <= 1 &&
           data.output.deviationHighAlarm >= -999.0 && data.output.deviationHighAlarm <= 3200.0 &&
           data.output.deviationLowAlarm >= -999.0 && data.output.deviationLowAlarm <= 3200.0 &&
           data.output.hysteresis >= -999.0 && data.output.hysteresis <= 3200.0 &&
           data.output.outputLowPercent >= 0 && data.output.outputLowPercent <= 100 &&
           data.output.outputHighPercent >= 0 && data.output.outputHighPercent <= 105 &&
           data.output.outputHighThreshold >= -999.0 && data.output.outputHighThreshold <= 3200.0 &&
           data.output.riseSlope >= 0.0 && data.output.riseSlope <= 3200.0 &&
           data.output.fallSlope >= 0.0 && data.output.fallSlope <= 3200.0 &&
           data.output.setpointLowLimit >= -999.0 && data.output.setpointLowLimit <= 3200.0 &&
           data.output.setpointHighLimit >= -999.0 && data.output.setpointHighLimit <= 3200.0 &&
           data.output.alarmResetFlags >= 0 && data.output.alarmResetFlags <= 31 &&
           data.global.address >= 1 && data.global.address <= 247 &&
           isSupportedBaudRate(data.global.baudRate) &&
           data.global.controlChannelCount >= 1 && data.global.controlChannelCount <= kChannelCount &&
           data.global.controlCycleS >= 0.0 && data.global.controlCycleS <= 50.0 &&
           data.global.sampleMode >= 0 && data.global.sampleMode <= 3 &&
           data.global.decimalPoint >= 0 && data.global.decimalPoint <= 3 &&
           (!data.global.runStateWriteRequested || isDocumentedRunState(data.global.runStateWriteValue));
}

bool validAi8Selection(const Ai8TemperatureControllerProtocol::Selection& selection)
{
    using namespace Ai8TemperatureControllerProtocol;
    return selection.channel >= 1 && selection.channel <= kChannelCount &&
           selection.inputGroup >= 1 && selection.inputGroup <= kParameterGroupCount &&
           selection.outputGroup >= 1 && selection.outputGroup <= kParameterGroupCount;
}

bool validAi8Page(Ai8TemperatureControllerProtocol::Page page)
{
    return static_cast<int>(page) >= static_cast<int>(Ai8TemperatureControllerProtocol::Page::Channel) &&
           static_cast<int>(page) <= static_cast<int>(Ai8TemperatureControllerProtocol::Page::Global);
}

void setAi8Error(CommandErrorCode *errorCode, QString *errorMessage,
                 CommandErrorCode code, const QString& message)
{
    if (errorCode) *errorCode = code;
    if (errorMessage) *errorMessage = message;
}

bool validTemperatureChannel(quint8 channel)
{
    return channel >= 1 && channel <= 2;
}

const std::map<uint8_t, std::vector<int>>& supportedRemoteEpsilonPacketRates()
{
    static const std::map<uint8_t, std::vector<int>> kRates = {
        {0x40, {0, 1, 2, 5, 10, 20, 50, 100, 200, 250, 500, 1000}},
        {0x41, {0, 1, 2, 5, 10, 20, 50, 100, 250, 500}},
        {0x42, {0, 1, 2, 5, 10, 20, 50, 100, 250, 500}},
        {0x50, {0, 1, 2, 5, 10, 20, 50, 100, 250, 500}},
        {0x53, {0, 1, 2, 5, 10, 20, 50, 100, 250, 500}},
        {0x59, {0, 1, 2, 5, 10, 20, 50, 100, 250, 500}},
        {0x5A, {0, 1, 2, 5, 10, 20, 50, 100, 250, 500}},
        {0x5C, {0, 1, 2, 5, 10, 20, 50, 100, 250, 500}},
        {0x5D, {0, 1, 2, 5, 10, 20, 50, 100, 250, 500}},
        {0x63, {0, 1, 2, 5, 10, 20, 50, 100, 250, 500}},
        {0x64, {0, 1, 2, 5, 10, 20, 50, 100, 250, 500}},
    };
    return kRates;
}

bool supportedRemoteEpsilonPacketRate(uint8_t packetId, int rateHz)
{
    const auto it = supportedRemoteEpsilonPacketRates().find(packetId);
    return it != supportedRemoteEpsilonPacketRates().end() &&
           std::find(it->second.cbegin(), it->second.cend(), rateHz) != it->second.cend();
}

bool supportedEpsilonRtcmBaud(int baudRate)
{
    switch (baudRate)
    {
    case 9600:
    case 19200:
    case 38400:
    case 76800:
    case 115200:
    case 230400:
    case 460800:
    case 921600:
    case 2625000:
    case 5250000:
    case 10500000:
        return true;
    default:
        return false;
    }
}

TemperatureControllerChannelData *simulatedTemperatureChannel(TemperatureControllerData& data, quint8 channel)
{
    if (!validTemperatureChannel(channel))
    {
        return nullptr;
    }
    initializeSimulatedTemperatureController(data);
    return &data.channels[static_cast<size_t>(channel - 1)];
}

}  // namespace

SkyDeviceManager::SkyDeviceManager(QObject *parent)
    : QObject(parent)
{
    initializeStatuses();
    pending_raw_events_.reset(true);
    connect(&simulate_timer_, &QTimer::timeout, this, &SkyDeviceManager::generateSimulatedData);
}

SkyDeviceManager::~SkyDeviceManager()
{
    shutdown(false);
    pending_raw_events_.close();
}

void SkyDeviceManager::publishDeviceLog(LogLevel level,
                                        const QString& category,
                                        const QString& event,
                                        const QString& message,
                                        QVariantMap fields)
{
    fields.insert(QStringLiteral("event"), event);
    if (level >= LogLevel::Error &&
        !fields.contains(QStringLiteral("error_code")) &&
        !fields.contains(QStringLiteral("reason_code")))
    {
        fields.insert(QStringLiteral("error_code"), QStringLiteral("SKY_DEVICE_ERROR"));
    }

    LogRecord record;
    record.level = level;
    record.source = QStringLiteral("SkyCore");
    record.category = category;
    record.message = message;
    record.fields = fields;
    emit logRecord(record);
}

void SkyDeviceManager::scheduleRawEventDrain()
{
    if (raw_event_drain_scheduled_.exchange(true))
    {
        return;
    }

    QPointer<SkyDeviceManager> self(this);
    QMetaObject::invokeMethod(this, [self]() {
        if (self)
        {
            self->drainRawEvents();
        }
    }, Qt::QueuedConnection);
}

void SkyDeviceManager::enqueueRawEvent(PendingRawEvent event)
{
    const quint64 payloadBytes = static_cast<quint64>(event.payload.size());
    const auto result = pending_raw_events_.push(std::move(event), payloadBytes);
    if (result.status == BoundedByteQueue<PendingRawEvent>::PushStatus::Enqueued ||
        result.status == BoundedByteQueue<PendingRawEvent>::PushStatus::Full)
    {
        scheduleRawEventDrain();
    }
}

void SkyDeviceManager::drainRawEvents()
{
    PendingRawEvent event;
    for (int i = 0; i < kRawEventDrainBatchSize && pending_raw_events_.tryPop(&event); ++i)
    {
        switch (event.deviceId)
        {
        case SkyDeviceId::Epsilon:
            if (epsilon_.get() == event.collectorIdentity)
            {
                emit epsilonRawFrameReceived(event.timestampUs,
                                             static_cast<quint8>(event.metadata),
                                             event.serialNumber,
                                             event.payload);
            }
            break;
        case SkyDeviceId::Ptb:
            if (ptb_.get() == event.collectorIdentity)
            {
                emit ptbRawResponseReceived(event.timestampUs, event.payload);
            }
            break;
        case SkyDeviceId::Hmp:
            if (hmp_.get() == event.collectorIdentity)
            {
                emit hmpRawResponseReceived(event.timestampUs, event.payload);
            }
            break;
        case SkyDeviceId::Lidar:
            if (lidar_.get() == event.collectorIdentity)
            {
                emit lidarRawFrameReceived(event.timestampUs, event.metadata, event.payload);
            }
            break;
        case SkyDeviceId::TemperatureController:
        case SkyDeviceId::Ai8TemperatureController:
        case SkyDeviceId::WaveTcp:
        case SkyDeviceId::All:
            break;
        }
    }

    const quint64 dropped = pending_raw_events_.droppedRecords();
    if (dropped > raw_event_drops_reported_)
    {
        publishDeviceLog(LogLevel::Warning,
                         QStringLiteral("device.raw_queue"),
                         QStringLiteral("raw_frame_queue_overloaded"),
                         QStringLiteral("原始数据帧队列已满，已丢弃部分数据。"),
                         {{QStringLiteral("reason_code"), QStringLiteral("RAW_FRAME_QUEUE_FULL")},
                          {QStringLiteral("dropped_count"),
                           static_cast<qulonglong>(dropped - raw_event_drops_reported_)},
                          {QStringLiteral("total_dropped_count"), static_cast<qulonglong>(dropped)}});
        raw_event_drops_reported_ = dropped;
    }

    raw_event_drain_scheduled_.store(false);
    if (!pending_raw_events_.empty())
    {
        scheduleRawEventDrain();
    }
}

void SkyDeviceManager::setSimulateData(bool simulate)
{
    simulate_data_ = simulate;
    if (simulate_data_)
    {
        stopRtcmWriter();
        simulate_timer_.start(100);
    }
    else
    {
        simulate_timer_.stop();
        restartRtcmWriter();
    }
}

void SkyDeviceManager::loadConfig(const SkyConfig& config)
{
    config_ = config;
    restartRtcmWriter();
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
        if (!deviceEnabled(config_, id))
        {
            invalidateDeviceData(id);
            setState(id, DeviceState::Disabled);
            if (errorCode) *errorCode = CommandErrorCode::Ok;
            return true;
        }
        setState(id, DeviceState::Connected);
        if (errorCode) *errorCode = CommandErrorCode::Ok;
        return true;
    }

    if (id == SkyDeviceId::WaveTcp)
    {
        return connectWaveTcp(errorCode);
    }

    if (id == SkyDeviceId::TemperatureController)
    {
        return connectSerialCollector(id, serialConfigFromTemperatureConfig(config_.temperature_controller), errorCode);
    }

    if (id == SkyDeviceId::Ai8TemperatureController)
    {
        return connectSerialCollector(id, serialConfigFromTemperatureConfig(config_.ai8_temperature_controller), errorCode);
    }

    return connectSerialCollector(id, serialConfigFor(id), errorCode);
}

bool SkyDeviceManager::disconnectDevice(SkyDeviceId id, CommandErrorCode *errorCode)
{
    return disconnectDeviceInternal(id, errorCode, true);
}

bool SkyDeviceManager::disconnectDeviceInternal(SkyDeviceId id,
                                                CommandErrorCode *errorCode,
                                                bool publishLog)
{
    if (id == SkyDeviceId::All)
    {
        disconnectAll(publishLog);
        if (errorCode) *errorCode = CommandErrorCode::Ok;
        return true;
    }
    const DeviceStatusItem previousStatus = status(id);
    const bool shouldPublishDisconnectLog =
        previousStatus.state != DeviceState::Disconnected &&
        previousStatus.state != DeviceState::Disabled;
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
    case SkyDeviceId::TemperatureController:
        stopCollector(temperature_controller_);
        break;
    case SkyDeviceId::Ai8TemperatureController:
        stopCollector(ai8_temperature_controller_);
        break;
    case SkyDeviceId::WaveTcp:
        disconnectWaveTcp();
        break;
    case SkyDeviceId::All:
        break;
    }
    invalidateDeviceData(id);
    setState(id, DeviceState::Disconnected);
    if (publishLog && shouldPublishDisconnectLog)
    {
        publishDeviceLog(LogLevel::Info,
                         QStringLiteral("device.connection"),
                         QStringLiteral("device_disconnected"),
                         QStringLiteral("设备已断开，缓存数据已失效。"),
                         {{QStringLiteral("device_id"), skyDeviceIdName(id)}});
    }
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
    for (SkyDeviceId id : {SkyDeviceId::Epsilon, SkyDeviceId::Ptb, SkyDeviceId::Hmp, SkyDeviceId::Lidar, SkyDeviceId::TemperatureController, SkyDeviceId::Ai8TemperatureController, SkyDeviceId::WaveTcp})
    {
        if (deviceEnabled(config_, id))
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

void SkyDeviceManager::disconnectAll(bool publishLogs)
{
    for (SkyDeviceId id : {SkyDeviceId::Epsilon, SkyDeviceId::Ptb, SkyDeviceId::Hmp, SkyDeviceId::Lidar, SkyDeviceId::TemperatureController, SkyDeviceId::Ai8TemperatureController, SkyDeviceId::WaveTcp})
    {
        disconnectDeviceInternal(id, nullptr, publishLogs);
    }
}

void SkyDeviceManager::reconnectAll()
{
    disconnectAll();
    connectAll();
}

void SkyDeviceManager::shutdown(bool publishLogs)
{
    simulate_timer_.stop();
    simulate_data_ = false;
    disconnectAll(publishLogs);
    stopRtcmWriter();
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
    case SkyDeviceId::TemperatureController:
        return temperature_controller_status_;
    case SkyDeviceId::Ai8TemperatureController:
        return ai8_temperature_controller_status_;
    case SkyDeviceId::WaveTcp:
        return wave_tcp_status_;
    case SkyDeviceId::All:
        break;
    }
    return {};
}

QVector<DeviceStatusItem> SkyDeviceManager::allStatuses() const
{
    return {epsilon_status_, ptb_status_, hmp_status_, lidar_status_, temperature_controller_status_, ai8_temperature_controller_status_, wave_tcp_status_};
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
    const bool temperatureControllerReconfigured = reconfigureDevice(SkyDeviceId::TemperatureController, diff.temperature_controller_changed, config_.temperature_controller.enabled);
    const bool ai8TemperatureControllerReconfigured = reconfigureDevice(SkyDeviceId::Ai8TemperatureController, diff.ai8_temperature_controller_changed, config_.ai8_temperature_controller.enabled);
    const bool waveReconfigured = reconfigureDevice(SkyDeviceId::WaveTcp, diff.wave_tcp_changed, config_.wave_tcp.enabled);
    if (diff.epsilon_rtcm_changed)
    {
        restartRtcmWriter();
    }

    QJsonObject devices;
    devices["epsilon"] = resultItem(diff.epsilon_changed, epsilonReconfigured, epsilon_status_);
    devices["ptb"] = resultItem(diff.ptb_changed, ptbReconfigured, ptb_status_);
    devices["hmp"] = resultItem(diff.hmp_changed, hmpReconfigured, hmp_status_);
    devices["lidar"] = resultItem(diff.lidar_changed, lidarReconfigured, lidar_status_);
    devices["temperature_controller"] = resultItem(diff.temperature_controller_changed, temperatureControllerReconfigured, temperature_controller_status_);
    devices["ai8_temperature_controller"] = resultItem(diff.ai8_temperature_controller_changed, ai8TemperatureControllerReconfigured, ai8_temperature_controller_status_);
    devices["wave_tcp"] = resultItem(diff.wave_tcp_changed, waveReconfigured, wave_tcp_status_);

    QJsonObject telemetry;
    telemetry["changed"] = diff.telemetry_changed;
    telemetry["timers_updated"] = diff.telemetry_changed;

    QJsonObject epsilonRtcm;
    epsilonRtcm["changed"] = diff.epsilon_rtcm_changed;
    epsilonRtcm["enabled"] = config_.epsilon_rtcm.enabled;
    epsilonRtcm["device_port_index"] = config_.epsilon_rtcm.device_port_index;
    epsilonRtcm["forward_port"] = config_.epsilon_rtcm.forward_port;
    epsilonRtcm["baud"] = config_.epsilon_rtcm.baud_rate;

    result.json["success"] = result.success;
    result.json["error_code"] = static_cast<int>(result.error_code);
    result.json["error"] = commandErrorCodeText(result.error_code);
    result.json["devices"] = devices;
    result.json["telemetry"] = telemetry;
    result.json["epsilon_rtcm"] = epsilonRtcm;
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
    publishDeviceLog(LogLevel::Info,
                     QStringLiteral("device.wave_tcp"),
                     QStringLiteral("wave_tcp_peak_search_range_updated"),
                     QStringLiteral("Wave TCP 峰值搜索范围已更新。"),
                     {{QStringLiteral("start_index"), startIndex},
                      {QStringLiteral("end_index"), endIndex}});
    if (errorCode) *errorCode = CommandErrorCode::Ok;
    return true;
}

bool SkyDeviceManager::configureEpsilonPacketRates(
    const EpsilonPacketRatesOperation& operation,
    CommandErrorCode *errorCode,
    QString *errorMessage)
{
    if (operation.output_rate_hz <= 0 || operation.output_rate_hz > 1000 ||
        operation.callback_rate_hz <= 0 || operation.callback_rate_hz > 1000 ||
        operation.packet_rates.empty())
    {
        if (errorCode) *errorCode = CommandErrorCode::InvalidPayload;
        if (errorMessage) *errorMessage = QStringLiteral("EPSILON packet-rate payload is invalid.");
        return false;
    }
    for (const auto& entry : operation.packet_rates)
    {
        if (!supportedRemoteEpsilonPacketRate(entry.first, entry.second))
        {
            if (errorCode) *errorCode = CommandErrorCode::ConfigInvalid;
            if (errorMessage) *errorMessage = QStringLiteral("EPSILON packet id or rate is unsupported.");
            return false;
        }
    }
    if (epsilon_status_.state != DeviceState::Connected)
    {
        if (errorCode) *errorCode = CommandErrorCode::DeviceNotConnected;
        if (errorMessage) *errorMessage = QStringLiteral("EPSILON is not connected.");
        return false;
    }
    if (simulate_data_)
    {
        simulated_epsilon_packet_rates_ = operation.packet_rates;
        if (errorCode) *errorCode = CommandErrorCode::Ok;
        if (errorMessage) *errorMessage = QStringLiteral("EPSILON packet rates were applied in simulation.");
        return true;
    }
    if (!epsilon_ || !epsilon_->isRunning())
    {
        if (errorCode) *errorCode = CommandErrorCode::DeviceNotConnected;
        if (errorMessage) *errorMessage = QStringLiteral("EPSILON collector is not running.");
        return false;
    }
    const bool ok = epsilon_->setOutputPacketRates(operation.packet_rates, true);
    if (ok)
    {
        epsilon_->setSampleRate(operation.callback_rate_hz);
    }
    if (errorCode) *errorCode = ok ? CommandErrorCode::Ok : CommandErrorCode::ConfigApplyFailed;
    if (errorMessage && !ok) *errorMessage = QStringLiteral("EPSILON packet-rate configuration failed.");
    return ok;
}

bool SkyDeviceManager::configureEpsilonMainAntennaLeverArm(
    const EpsilonMainAntennaLeverArmOperation& operation,
    CommandErrorCode *errorCode,
    QString *errorMessage)
{
    if (!std::isfinite(operation.x_m) || !std::isfinite(operation.y_m) ||
        !std::isfinite(operation.z_m) || std::abs(operation.x_m) > 100.0 ||
        std::abs(operation.y_m) > 100.0 || std::abs(operation.z_m) > 100.0)
    {
        if (errorCode) *errorCode = CommandErrorCode::InvalidPayload;
        if (errorMessage) *errorMessage = QStringLiteral("EPSILON lever-arm payload is invalid.");
        return false;
    }
    if (epsilon_status_.state != DeviceState::Connected)
    {
        if (errorCode) *errorCode = CommandErrorCode::DeviceNotConnected;
        if (errorMessage) *errorMessage = QStringLiteral("EPSILON is not connected.");
        return false;
    }
    if (simulate_data_)
    {
        simulated_epsilon_lever_arm_ = operation;
        if (errorCode) *errorCode = CommandErrorCode::Ok;
        if (errorMessage) *errorMessage = QStringLiteral("EPSILON lever arm was applied in simulation.");
        return true;
    }
    if (!epsilon_ || !epsilon_->isRunning())
    {
        if (errorCode) *errorCode = CommandErrorCode::DeviceNotConnected;
        if (errorMessage) *errorMessage = QStringLiteral("EPSILON collector is not running.");
        return false;
    }
    const QString port = config_.epsilon.port;
    const int baudRate = config_.epsilon.baud_rate;
    epsilon_->stop();
    if (!epsilon_->start(port.toStdString(), SerialConfig::N81(baudRate)))
    {
        if (errorCode) *errorCode = CommandErrorCode::DeviceConnectFailed;
        if (errorMessage) *errorMessage = QStringLiteral("EPSILON serial port could not be reopened for lever-arm configuration.");
        return false;
    }
    const bool ok = epsilon_->configureMainAntennaLeverArm(
        operation.x_m, operation.y_m, operation.z_m);
    epsilon_->stop();
    const bool restored = epsilon_->start(port.toStdString(), SerialConfig::N81(baudRate)) &&
                          epsilon_->checkDeviceResponse() &&
                          epsilon_->startStreaming();
    if (!restored)
    {
        setState(SkyDeviceId::Epsilon, DeviceState::Error,
                 static_cast<quint16>(CommandErrorCode::InternalError));
        if (errorCode) *errorCode = CommandErrorCode::InternalError;
        if (errorMessage) *errorMessage = QStringLiteral("EPSILON lever-arm operation finished but live stream restore failed.");
        return false;
    }
    if (errorCode) *errorCode = ok ? CommandErrorCode::Ok : CommandErrorCode::ConfigApplyFailed;
    if (errorMessage && !ok) *errorMessage = QStringLiteral("EPSILON lever-arm configuration failed.");
    return ok;
}

bool SkyDeviceManager::configureEpsilonRtcmInput(
    const EpsilonRtcmInputOperation& operation,
    CommandErrorCode *errorCode,
    QString *errorMessage)
{
    if (operation.device_port_index < 2 || operation.device_port_index > 5 ||
        !supportedEpsilonRtcmBaud(operation.forward_baud))
    {
        if (errorCode) *errorCode = CommandErrorCode::InvalidPayload;
        if (errorMessage) *errorMessage = QStringLiteral("EPSILON RTCM input payload is invalid.");
        return false;
    }
    if (epsilon_status_.state != DeviceState::Connected)
    {
        if (errorCode) *errorCode = CommandErrorCode::DeviceNotConnected;
        if (errorMessage) *errorMessage = QStringLiteral("EPSILON is not connected.");
        return false;
    }
    if (simulate_data_)
    {
        simulated_epsilon_rtcm_input_ = operation;
        config_.epsilon_rtcm.enabled = true;
        config_.epsilon_rtcm.device_port_index = operation.device_port_index;
        config_.epsilon_rtcm.forward_port = operation.forward_port.trimmed();
        config_.epsilon_rtcm.baud_rate = operation.forward_baud;
        if (errorCode) *errorCode = CommandErrorCode::Ok;
        if (errorMessage) *errorMessage = QStringLiteral("EPSILON RTCM input was applied in simulation.");
        return true;
    }
    if (!epsilon_ || !epsilon_->isRunning())
    {
        if (errorCode) *errorCode = CommandErrorCode::DeviceNotConnected;
        if (errorMessage) *errorMessage = QStringLiteral("EPSILON collector is not running.");
        return false;
    }
    const QString port = config_.epsilon.port;
    const int baudRate = config_.epsilon.baud_rate;
    epsilon_->stop();
    if (!epsilon_->start(port.toStdString(), SerialConfig::N81(baudRate)))
    {
        if (errorCode) *errorCode = CommandErrorCode::DeviceConnectFailed;
        if (errorMessage) *errorMessage = QStringLiteral("EPSILON serial port could not be reopened for RTCM configuration.");
        return false;
    }
    const bool ok = epsilon_->configureRtcmPort(
        operation.device_port_index, operation.forward_baud);
    epsilon_->stop();
    const bool restored = epsilon_->start(port.toStdString(), SerialConfig::N81(baudRate)) &&
                          epsilon_->checkDeviceResponse() &&
                          epsilon_->startStreaming();
    if (!restored)
    {
        setState(SkyDeviceId::Epsilon, DeviceState::Error,
                 static_cast<quint16>(CommandErrorCode::InternalError));
        if (errorCode) *errorCode = CommandErrorCode::InternalError;
        if (errorMessage) *errorMessage = QStringLiteral("EPSILON RTCM operation finished but live stream restore failed.");
        return false;
    }
    if (ok)
    {
        config_.epsilon_rtcm.enabled = !operation.forward_port.trimmed().isEmpty();
        config_.epsilon_rtcm.device_port_index = operation.device_port_index;
        config_.epsilon_rtcm.forward_port = operation.forward_port.trimmed();
        config_.epsilon_rtcm.baud_rate = operation.forward_baud;
        restartRtcmWriter();
    }
    if (errorCode) *errorCode = ok ? CommandErrorCode::Ok : CommandErrorCode::ConfigApplyFailed;
    if (errorMessage && !ok) *errorMessage = QStringLiteral("EPSILON RTCM input configuration failed.");
    return ok;
}

bool SkyDeviceManager::receiveRtcmCorrectionData(
    const QByteArray& data,
    CommandErrorCode *errorCode)
{
    if (data.isEmpty() || data.size() > kMaxRtcmCorrectionPayloadBytes)
    {
        if (errorCode) *errorCode = CommandErrorCode::InvalidPayload;
        return false;
    }
    if (simulate_data_)
    {
        rtcm_correction_bytes_received_.fetch_add(static_cast<quint64>(data.size()));
        rtcm_correction_chunks_received_.fetch_add(1);
        rtcm_correction_last_receive_time_us_.store(nowUs());
        if (errorCode) *errorCode = CommandErrorCode::Ok;
        return true;
    }
    if (!config_.epsilon_rtcm.enabled || config_.epsilon_rtcm.forward_port.trimmed().isEmpty())
    {
        rtcm_correction_dropped_bytes_.fetch_add(static_cast<quint64>(data.size()));
        rtcm_correction_dropped_chunks_.fetch_add(1);
        if (errorCode) *errorCode = CommandErrorCode::ConfigInvalid;
        return false;
    }
    const auto push = pending_rtcm_corrections_.push(data, static_cast<quint64>(data.size()));
    if (push.status != BoundedByteQueue<QByteArray>::PushStatus::Enqueued)
    {
        rtcm_correction_dropped_bytes_.fetch_add(static_cast<quint64>(data.size()));
        rtcm_correction_dropped_chunks_.fetch_add(1);
        if (errorCode) *errorCode = CommandErrorCode::ConfigApplyFailed;
        return false;
    }
    rtcm_correction_bytes_received_.fetch_add(static_cast<quint64>(data.size()));
    rtcm_correction_chunks_received_.fetch_add(1);
    rtcm_correction_last_receive_time_us_.store(nowUs());
    if (errorCode) *errorCode = CommandErrorCode::Ok;
    return true;
}

RtcmCorrectionStats SkyDeviceManager::rtcmCorrectionStats() const
{
    RtcmCorrectionStats stats;
    stats.bytes_received = rtcm_correction_bytes_received_.load();
    stats.chunks_received = rtcm_correction_chunks_received_.load();
    stats.dropped_bytes = rtcm_correction_dropped_bytes_.load();
    stats.dropped_chunks = rtcm_correction_dropped_chunks_.load();
    stats.last_receive_time_us = rtcm_correction_last_receive_time_us_.load();
    return stats;
}

void SkyDeviceManager::restartRtcmWriter()
{
    stopRtcmWriter();
    if (simulate_data_ ||
        !config_.epsilon_rtcm.enabled ||
        config_.epsilon_rtcm.forward_port.trimmed().isEmpty() ||
        config_.epsilon_rtcm.baud_rate <= 0)
    {
        return;
    }
    pending_rtcm_corrections_.reset(true);
    rtcm_writer_thread_ = std::thread([this,
                                       port = config_.epsilon_rtcm.forward_port.trimmed(),
                                       baud = config_.epsilon_rtcm.baud_rate]() {
        rtcmWriterLoop(port, baud);
    });
}

void SkyDeviceManager::stopRtcmWriter()
{
    pending_rtcm_corrections_.close();
    if (rtcm_writer_thread_.joinable())
    {
        rtcm_writer_thread_.join();
    }
    pending_rtcm_corrections_.reset(false);
}

void SkyDeviceManager::rtcmWriterLoop(QString port, int baudRate)
{
    SerialPort serial;
    bool opened = false;
    QByteArray payload;
    while (pending_rtcm_corrections_.waitPop(&payload))
    {
        if (!opened)
        {
            opened = serial.open(port.toStdString(), SerialConfig::N81(baudRate));
        }
        if (!opened)
        {
            rtcm_correction_dropped_bytes_.fetch_add(static_cast<quint64>(payload.size()));
            rtcm_correction_dropped_chunks_.fetch_add(1);
            continue;
        }
        const ssize_t written = serial.write(payload.constData(), static_cast<size_t>(payload.size()));
        if (written != payload.size())
        {
            rtcm_correction_dropped_bytes_.fetch_add(static_cast<quint64>(payload.size()));
            rtcm_correction_dropped_chunks_.fetch_add(1);
            serial.close();
            opened = false;
        }
    }
    serial.close();
}

bool SkyDeviceManager::setTemperatureTarget(quint8 channel, double celsius, CommandErrorCode *errorCode)
{
    if (simulate_data_ && temperature_controller_status_.state == DeviceState::Connected)
    {
        TemperatureControllerChannelData *simulated = simulatedTemperatureChannel(latest_temperature_controller_, channel);
        const bool ok = simulated && std::isfinite(celsius) && celsius >= -40.0 && celsius <= 100.0;
        if (ok) simulated->target_temperature_c = celsius;
        if (errorCode) *errorCode = ok ? CommandErrorCode::Ok : CommandErrorCode::InvalidPayload;
        return ok;
    }
    if (!temperature_controller_ || temperature_controller_status_.state != DeviceState::Connected)
    {
        if (errorCode) *errorCode = CommandErrorCode::DeviceNotConnected;
        return false;
    }
    const bool ok = temperature_controller_->setTargetTemperature(channel, celsius);
    if (errorCode) *errorCode = ok ? CommandErrorCode::Ok : CommandErrorCode::ConfigApplyFailed;
    return ok;
}

bool SkyDeviceManager::setTemperatureOutputEnabled(quint8 channel, bool enabled, CommandErrorCode *errorCode)
{
    if (simulate_data_ && temperature_controller_status_.state == DeviceState::Connected)
    {
        TemperatureControllerChannelData *simulated = simulatedTemperatureChannel(latest_temperature_controller_, channel);
        const bool ok = simulated != nullptr;
        if (ok) simulated->output_enabled = enabled;
        if (errorCode) *errorCode = ok ? CommandErrorCode::Ok : CommandErrorCode::InvalidPayload;
        return ok;
    }
    if (!temperature_controller_ || temperature_controller_status_.state != DeviceState::Connected)
    {
        if (errorCode) *errorCode = CommandErrorCode::DeviceNotConnected;
        return false;
    }
    const bool ok = temperature_controller_->setOutputEnabled(channel, enabled);
    if (errorCode) *errorCode = ok ? CommandErrorCode::Ok : CommandErrorCode::ConfigApplyFailed;
    return ok;
}

bool SkyDeviceManager::setTemperatureOutputMode(quint8 channel, quint16 mode, CommandErrorCode *errorCode)
{
    if (simulate_data_ && temperature_controller_status_.state == DeviceState::Connected)
    {
        TemperatureControllerChannelData *simulated = simulatedTemperatureChannel(latest_temperature_controller_, channel);
        const bool ok = simulated && mode <= 3;
        if (ok) simulated->output_mode = mode;
        if (errorCode) *errorCode = ok ? CommandErrorCode::Ok : CommandErrorCode::InvalidPayload;
        return ok;
    }
    if (!temperature_controller_ || temperature_controller_status_.state != DeviceState::Connected)
    {
        if (errorCode) *errorCode = CommandErrorCode::DeviceNotConnected;
        return false;
    }
    const bool ok = temperature_controller_->setOutputMode(channel, mode);
    if (errorCode) *errorCode = ok ? CommandErrorCode::Ok : CommandErrorCode::ConfigApplyFailed;
    return ok;
}

bool SkyDeviceManager::setTemperatureMaxOutputPercent(quint8 channel, quint16 percent, CommandErrorCode *errorCode)
{
    if (simulate_data_ && temperature_controller_status_.state == DeviceState::Connected)
    {
        TemperatureControllerChannelData *simulated = simulatedTemperatureChannel(latest_temperature_controller_, channel);
        const bool ok = simulated && percent <= 100;
        if (ok) simulated->max_output_percent = percent;
        if (errorCode) *errorCode = ok ? CommandErrorCode::Ok : CommandErrorCode::InvalidPayload;
        return ok;
    }
    if (!temperature_controller_ || temperature_controller_status_.state != DeviceState::Connected)
    {
        if (errorCode) *errorCode = CommandErrorCode::DeviceNotConnected;
        return false;
    }
    const bool ok = temperature_controller_->setMaxOutputPercent(channel, percent);
    if (errorCode) *errorCode = ok ? CommandErrorCode::Ok : CommandErrorCode::ConfigApplyFailed;
    return ok;
}

bool SkyDeviceManager::setTemperaturePid(quint8 channel, quint32 kp, quint32 ki, quint32 kd, CommandErrorCode *errorCode)
{
    if (simulate_data_ && temperature_controller_status_.state == DeviceState::Connected)
    {
        TemperatureControllerChannelData *simulated = simulatedTemperatureChannel(latest_temperature_controller_, channel);
        const bool ok = simulated && kp <= 999999 && ki <= 999999 && kd <= 999999;
        if (ok)
        {
            simulated->kp = static_cast<int>(kp);
            simulated->ki = static_cast<int>(ki);
            simulated->kd = static_cast<int>(kd);
        }
        if (errorCode) *errorCode = ok ? CommandErrorCode::Ok : CommandErrorCode::InvalidPayload;
        return ok;
    }
    if (!temperature_controller_ || temperature_controller_status_.state != DeviceState::Connected)
    {
        if (errorCode) *errorCode = CommandErrorCode::DeviceNotConnected;
        return false;
    }
    const bool ok = temperature_controller_->setPid(channel, kp, ki, kd);
    if (errorCode) *errorCode = ok ? CommandErrorCode::Ok : CommandErrorCode::ConfigApplyFailed;
    return ok;
}

bool SkyDeviceManager::setTemperatureAutoPid(quint8 channel, quint16 mode, CommandErrorCode *errorCode)
{
    if (simulate_data_ && temperature_controller_status_.state == DeviceState::Connected)
    {
        TemperatureControllerChannelData *simulated = simulatedTemperatureChannel(latest_temperature_controller_, channel);
        const bool ok = simulated && mode <= 2;
        if (ok) simulated->auto_pid_mode = mode;
        if (errorCode) *errorCode = ok ? CommandErrorCode::Ok : CommandErrorCode::InvalidPayload;
        return ok;
    }
    if (!temperature_controller_ || temperature_controller_status_.state != DeviceState::Connected)
    {
        if (errorCode) *errorCode = CommandErrorCode::DeviceNotConnected;
        return false;
    }
    const bool ok = temperature_controller_->setAutoPid(channel, mode);
    if (errorCode) *errorCode = ok ? CommandErrorCode::Ok : CommandErrorCode::ConfigApplyFailed;
    return ok;
}

bool SkyDeviceManager::setTemperatureOvertempUpper(quint8 channel, double celsius, CommandErrorCode *errorCode)
{
    if (simulate_data_ && temperature_controller_status_.state == DeviceState::Connected)
    {
        TemperatureControllerChannelData *simulated = simulatedTemperatureChannel(latest_temperature_controller_, channel);
        const bool ok = simulated && std::isfinite(celsius);
        if (ok) simulated->overtemp_upper_c = celsius;
        if (errorCode) *errorCode = ok ? CommandErrorCode::Ok : CommandErrorCode::InvalidPayload;
        return ok;
    }
    if (!temperature_controller_ || temperature_controller_status_.state != DeviceState::Connected)
    {
        if (errorCode) *errorCode = CommandErrorCode::DeviceNotConnected;
        return false;
    }
    const bool ok = temperature_controller_->setOvertempUpper(channel, celsius);
    if (errorCode) *errorCode = ok ? CommandErrorCode::Ok : CommandErrorCode::ConfigApplyFailed;
    return ok;
}

bool SkyDeviceManager::setTemperatureOvertempLower(quint8 channel, double celsius, CommandErrorCode *errorCode)
{
    if (simulate_data_ && temperature_controller_status_.state == DeviceState::Connected)
    {
        TemperatureControllerChannelData *simulated = simulatedTemperatureChannel(latest_temperature_controller_, channel);
        const bool ok = simulated && std::isfinite(celsius);
        if (ok) simulated->overtemp_lower_c = celsius;
        if (errorCode) *errorCode = ok ? CommandErrorCode::Ok : CommandErrorCode::InvalidPayload;
        return ok;
    }
    if (!temperature_controller_ || temperature_controller_status_.state != DeviceState::Connected)
    {
        if (errorCode) *errorCode = CommandErrorCode::DeviceNotConnected;
        return false;
    }
    const bool ok = temperature_controller_->setOvertempLower(channel, celsius);
    if (errorCode) *errorCode = ok ? CommandErrorCode::Ok : CommandErrorCode::ConfigApplyFailed;
    return ok;
}

bool SkyDeviceManager::setTemperatureSlope(quint8 channel, double celsiusPerSecond, CommandErrorCode *errorCode)
{
    if (simulate_data_ && temperature_controller_status_.state == DeviceState::Connected)
    {
        TemperatureControllerChannelData *simulated = simulatedTemperatureChannel(latest_temperature_controller_, channel);
        const bool ok = simulated && std::isfinite(celsiusPerSecond) && celsiusPerSecond >= 0.0;
        if (ok) simulated->temperature_slope_c_per_s = celsiusPerSecond;
        if (errorCode) *errorCode = ok ? CommandErrorCode::Ok : CommandErrorCode::InvalidPayload;
        return ok;
    }
    if (!temperature_controller_ || temperature_controller_status_.state != DeviceState::Connected)
    {
        if (errorCode) *errorCode = CommandErrorCode::DeviceNotConnected;
        return false;
    }
    const bool ok = temperature_controller_->setTemperatureSlope(channel, celsiusPerSecond);
    if (errorCode) *errorCode = ok ? CommandErrorCode::Ok : CommandErrorCode::ConfigApplyFailed;
    return ok;
}

bool SkyDeviceManager::setTemperatureStartupDelay(quint8 channel, quint16 seconds, CommandErrorCode *errorCode)
{
    if (simulate_data_ && temperature_controller_status_.state == DeviceState::Connected)
    {
        TemperatureControllerChannelData *simulated = simulatedTemperatureChannel(latest_temperature_controller_, channel);
        const bool ok = simulated != nullptr;
        if (ok) simulated->startup_delay_s = seconds;
        if (errorCode) *errorCode = ok ? CommandErrorCode::Ok : CommandErrorCode::InvalidPayload;
        return ok;
    }
    if (!temperature_controller_ || temperature_controller_status_.state != DeviceState::Connected)
    {
        if (errorCode) *errorCode = CommandErrorCode::DeviceNotConnected;
        return false;
    }
    const bool ok = temperature_controller_->setStartupDelay(channel, seconds);
    if (errorCode) *errorCode = ok ? CommandErrorCode::Ok : CommandErrorCode::ConfigApplyFailed;
    return ok;
}

bool SkyDeviceManager::setTemperatureControllerMode(quint16 mode, CommandErrorCode *errorCode)
{
    if (simulate_data_ && temperature_controller_status_.state == DeviceState::Connected)
    {
        const bool ok = mode <= 2;
        initializeSimulatedTemperatureController(latest_temperature_controller_);
        if (ok) latest_temperature_controller_.controller_mode = mode;
        if (errorCode) *errorCode = ok ? CommandErrorCode::Ok : CommandErrorCode::InvalidPayload;
        return ok;
    }
    if (!temperature_controller_ || temperature_controller_status_.state != DeviceState::Connected)
    {
        if (errorCode) *errorCode = CommandErrorCode::DeviceNotConnected;
        return false;
    }
    const bool ok = temperature_controller_->setControllerMode(mode);
    if (errorCode) *errorCode = ok ? CommandErrorCode::Ok : CommandErrorCode::ConfigApplyFailed;
    return ok;
}

bool SkyDeviceManager::setTemperatureDeviceAddress(quint16 address, CommandErrorCode *errorCode)
{
    if (simulate_data_ && temperature_controller_status_.state == DeviceState::Connected)
    {
        const bool ok = address >= 1 && address <= 247;
        initializeSimulatedTemperatureController(latest_temperature_controller_);
        if (ok)
        {
            latest_temperature_controller_.device_address = address;
            config_.temperature_controller.slave_address = address;
        }
        if (errorCode) *errorCode = ok ? CommandErrorCode::Ok : CommandErrorCode::InvalidPayload;
        return ok;
    }
    if (!temperature_controller_ || temperature_controller_status_.state != DeviceState::Connected)
    {
        if (errorCode) *errorCode = CommandErrorCode::DeviceNotConnected;
        return false;
    }
    const bool ok = temperature_controller_->setDeviceAddress(address);
    if (ok)
    {
        config_.temperature_controller.slave_address = static_cast<int>(address);
    }
    if (errorCode) *errorCode = ok ? CommandErrorCode::Ok : CommandErrorCode::ConfigApplyFailed;
    return ok;
}

bool SkyDeviceManager::setTemperatureRs485Baud(quint16 baudIndex, CommandErrorCode *errorCode)
{
    if (simulate_data_ && temperature_controller_status_.state == DeviceState::Connected)
    {
        const bool ok = baudIndex <= 7;
        initializeSimulatedTemperatureController(latest_temperature_controller_);
        if (ok)
        {
            latest_temperature_controller_.rs485_baud_index = baudIndex;
            config_.temperature_controller.baud_rate = temperatureRs485BaudRateForIndex(baudIndex);
        }
        if (errorCode) *errorCode = ok ? CommandErrorCode::Ok : CommandErrorCode::InvalidPayload;
        return ok;
    }
    if (!temperature_controller_ || temperature_controller_status_.state != DeviceState::Connected)
    {
        if (errorCode) *errorCode = CommandErrorCode::DeviceNotConnected;
        return false;
    }
    const bool ok = temperature_controller_->setRs485BaudIndex(baudIndex);
    if (ok)
    {
        config_.temperature_controller.baud_rate = temperatureRs485BaudRateForIndex(baudIndex);
    }
    if (errorCode) *errorCode = ok ? CommandErrorCode::Ok : CommandErrorCode::ConfigApplyFailed;
    return ok;
}

bool SkyDeviceManager::setTemperatureOvertempOutputMode(quint16 mode, CommandErrorCode *errorCode)
{
    if (simulate_data_ && temperature_controller_status_.state == DeviceState::Connected)
    {
        const bool ok = mode <= 1;
        initializeSimulatedTemperatureController(latest_temperature_controller_);
        if (ok) latest_temperature_controller_.overtemp_output_mode = mode;
        if (errorCode) *errorCode = ok ? CommandErrorCode::Ok : CommandErrorCode::InvalidPayload;
        return ok;
    }
    if (!temperature_controller_ || temperature_controller_status_.state != DeviceState::Connected)
    {
        if (errorCode) *errorCode = CommandErrorCode::DeviceNotConnected;
        return false;
    }
    const bool ok = temperature_controller_->setOvertempOutputMode(mode);
    if (errorCode) *errorCode = ok ? CommandErrorCode::Ok : CommandErrorCode::ConfigApplyFailed;
    return ok;
}

bool SkyDeviceManager::setTemperatureSensorConfig(const TemperatureControllerCommand& command, CommandErrorCode *errorCode)
{
    if (simulate_data_ && temperature_controller_status_.state == DeviceState::Connected)
    {
        TemperatureControllerChannelData *simulated = simulatedTemperatureChannel(latest_temperature_controller_, command.channel);
        const bool ok = simulated != nullptr;
        if (ok)
        {
            simulated->sensor_model = command.sensor_model;
            simulated->ntc_b = static_cast<int>(command.ntc_b);
            simulated->ntc_r0 = static_cast<int>(command.ntc_r0);
            simulated->pt_r0 = static_cast<int>(command.pt_r0);
            simulated->pt_a = static_cast<int>(command.pt_a);
            simulated->pt_b = static_cast<int>(command.pt_b);
            simulated->pt_c = static_cast<int>(command.pt_c);
            simulated->polynomial_mantissas = command.polynomial_mantissas;
            for (size_t i = 0; i < simulated->polynomial_exponents.size(); ++i)
            {
                simulated->polynomial_exponents[i] = command.polynomial_exponents[i];
            }
        }
        if (errorCode) *errorCode = ok ? CommandErrorCode::Ok : CommandErrorCode::InvalidPayload;
        return ok;
    }
    if (!temperature_controller_ || temperature_controller_status_.state != DeviceState::Connected)
    {
        if (errorCode) *errorCode = CommandErrorCode::DeviceNotConnected;
        return false;
    }
    const bool ok = temperature_controller_->setSensorConfig(command.channel,
                                                            command.sensor_model,
                                                            command.ntc_b,
                                                            command.ntc_r0,
                                                            command.pt_r0,
                                                            command.pt_a,
                                                            command.pt_b,
                                                            command.pt_c,
                                                            command.polynomial_mantissas,
                                                            command.polynomial_exponents);
    if (errorCode) *errorCode = ok ? CommandErrorCode::Ok : CommandErrorCode::ConfigApplyFailed;
    return ok;
}

bool SkyDeviceManager::restoreTemperatureFactoryDefaults(CommandErrorCode *errorCode)
{
    if (simulate_data_ && temperature_controller_status_.state == DeviceState::Connected)
    {
        latest_temperature_controller_ = TemperatureControllerData();
        initializeSimulatedTemperatureController(latest_temperature_controller_);
        config_.temperature_controller.slave_address = 1;
        config_.temperature_controller.baud_rate = 9600;
        if (errorCode) *errorCode = CommandErrorCode::Ok;
        return true;
    }
    if (!temperature_controller_ || temperature_controller_status_.state != DeviceState::Connected)
    {
        if (errorCode) *errorCode = CommandErrorCode::DeviceNotConnected;
        return false;
    }
    const bool ok = temperature_controller_->restoreFactoryDefaults();
    if (ok)
    {
        config_.temperature_controller.slave_address = 1;
        config_.temperature_controller.baud_rate = 9600;
        temperature_controller_->setSlaveAddress(1);
    }
    if (errorCode) *errorCode = ok ? CommandErrorCode::Ok : CommandErrorCode::ConfigApplyFailed;
    return ok;
}

bool SkyDeviceManager::readAi8Page(Ai8TemperatureControllerProtocol::Page page,
                                   const Ai8TemperatureControllerProtocol::Selection& selection,
                                   Ai8TemperatureControllerProtocol::PageData& data,
                                   CommandErrorCode *errorCode,
                                   QString *errorMessage)
{
    if (!validAi8Page(page) || !validAi8Selection(selection))
    {
        setAi8Error(errorCode, errorMessage, CommandErrorCode::InvalidPayload,
                    QStringLiteral("System temperature controller page or selection is invalid."));
        return false;
    }
    if (ai8_temperature_controller_status_.state != DeviceState::Connected)
    {
        setAi8Error(errorCode, errorMessage, CommandErrorCode::DeviceNotConnected,
                    QStringLiteral("System temperature controller is not connected."));
        return false;
    }
    if (simulate_data_)
    {
        if (!simulated_ai8_pages_initialized_)
        {
            initializeSimulatedAi8State(simulated_ai8_channels_,
                                        simulated_ai8_inputs_,
                                        simulated_ai8_outputs_,
                                        simulated_ai8_global_);
            simulated_ai8_pages_initialized_ = true;
        }
        data = simulatedAi8Page(page,
                                selection,
                                simulated_ai8_channels_,
                                simulated_ai8_inputs_,
                                simulated_ai8_outputs_,
                                simulated_ai8_global_);
        setAi8Error(errorCode, errorMessage, CommandErrorCode::Ok,
                    QStringLiteral("System temperature controller parameters were read from simulation."));
        return true;
    }
    if (!ai8_temperature_controller_ || !ai8_temperature_controller_->isRunning())
    {
        setAi8Error(errorCode, errorMessage, CommandErrorCode::DeviceNotConnected,
                    QStringLiteral("System temperature controller collector is not running."));
        return false;
    }
    QString collectorError;
    const bool ok = ai8_temperature_controller_->readPage(page, selection, data, &collectorError);
    setAi8Error(errorCode, errorMessage,
                ok ? CommandErrorCode::Ok : CommandErrorCode::ConfigApplyFailed,
                ok ? QStringLiteral("System temperature controller parameters were read.") : collectorError);
    return ok;
}

bool SkyDeviceManager::writeAi8Page(const Ai8TemperatureControllerProtocol::PageData& requested,
                                    Ai8TemperatureControllerProtocol::PageData& confirmed,
                                    CommandErrorCode *errorCode,
                                    QString *errorMessage)
{
    if (!validAi8Page(requested.page) || !validAi8Selection(requested.selection) ||
        !validAi8PageValues(requested))
    {
        setAi8Error(errorCode, errorMessage, CommandErrorCode::InvalidPayload,
                    QStringLiteral("System temperature controller page values are invalid."));
        return false;
    }
    if (ai8_temperature_controller_status_.state != DeviceState::Connected)
    {
        setAi8Error(errorCode, errorMessage, CommandErrorCode::DeviceNotConnected,
                    QStringLiteral("System temperature controller is not connected."));
        return false;
    }
    if (simulate_data_)
    {
        if (!simulated_ai8_pages_initialized_)
        {
            initializeSimulatedAi8State(simulated_ai8_channels_,
                                        simulated_ai8_inputs_,
                                        simulated_ai8_outputs_,
                                        simulated_ai8_global_);
            simulated_ai8_pages_initialized_ = true;
        }
        switch (requested.page)
        {
        case Ai8TemperatureControllerProtocol::Page::Channel:
            simulated_ai8_channels_[static_cast<size_t>(requested.selection.channel - 1)] = requested.channel;
            break;
        case Ai8TemperatureControllerProtocol::Page::InputGroup:
            simulated_ai8_inputs_[static_cast<size_t>(requested.selection.inputGroup - 1)] = requested.input;
            break;
        case Ai8TemperatureControllerProtocol::Page::OutputGroup:
            simulated_ai8_outputs_[static_cast<size_t>(requested.selection.outputGroup - 1)] = requested.output;
            break;
        case Ai8TemperatureControllerProtocol::Page::Global:
            simulated_ai8_global_ = requested.global;
            break;
        }
        confirmed = simulatedAi8Page(requested.page,
                                     requested.selection,
                                     simulated_ai8_channels_,
                                     simulated_ai8_inputs_,
                                     simulated_ai8_outputs_,
                                     simulated_ai8_global_);
        setAi8Error(errorCode, errorMessage, CommandErrorCode::Ok,
                    QStringLiteral("System temperature controller parameters were written and read back from simulation."));
        return true;
    }
    if (!ai8_temperature_controller_ || !ai8_temperature_controller_->isRunning())
    {
        setAi8Error(errorCode, errorMessage, CommandErrorCode::DeviceNotConnected,
                    QStringLiteral("System temperature controller collector is not running."));
        return false;
    }
    QString collectorError;
    if (!ai8_temperature_controller_->writePage(requested, &collectorError))
    {
        setAi8Error(errorCode, errorMessage, CommandErrorCode::ConfigApplyFailed, collectorError);
        return false;
    }
    const bool ok = ai8_temperature_controller_->readPage(requested.page,
                                                           requested.selection,
                                                           confirmed,
                                                           &collectorError);
    setAi8Error(errorCode, errorMessage,
                ok ? CommandErrorCode::Ok : CommandErrorCode::ConfigApplyFailed,
                ok ? QStringLiteral("System temperature controller parameters were written and read back.") : collectorError);
    return ok;
}

bool SkyDeviceManager::restoreAi8FactoryDefaults(Ai8TemperatureControllerProtocol::Page page,
                                                 const Ai8TemperatureControllerProtocol::Selection& selection,
                                                 Ai8TemperatureControllerProtocol::PageData& data,
                                                 CommandErrorCode *errorCode,
                                                 QString *errorMessage)
{
    if (!validAi8Page(page) || !validAi8Selection(selection))
    {
        setAi8Error(errorCode, errorMessage, CommandErrorCode::InvalidPayload,
                    QStringLiteral("System temperature controller page or selection is invalid."));
        return false;
    }
    if (ai8_temperature_controller_status_.state != DeviceState::Connected)
    {
        setAi8Error(errorCode, errorMessage, CommandErrorCode::DeviceNotConnected,
                    QStringLiteral("System temperature controller is not connected."));
        return false;
    }
    if (!simulate_data_)
    {
        setAi8Error(errorCode, errorMessage, CommandErrorCode::ConfigApplyFailed,
                    QStringLiteral("System temperature controller factory reset is not supported by the collector."));
        return false;
    }
    initializeSimulatedAi8State(simulated_ai8_channels_,
                                simulated_ai8_inputs_,
                                simulated_ai8_outputs_,
                                simulated_ai8_global_);
    simulated_ai8_pages_initialized_ = true;
    data = simulatedAi8Page(page,
                            selection,
                            simulated_ai8_channels_,
                            simulated_ai8_inputs_,
                            simulated_ai8_outputs_,
                            simulated_ai8_global_);
    setAi8Error(errorCode, errorMessage, CommandErrorCode::Ok,
                QStringLiteral("System temperature controller simulation parameters were restored to factory defaults."));
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

TemperatureControllerData SkyDeviceManager::latestTemperatureController() const
{
    return latest_temperature_controller_;
}

Ai8TemperatureControllerProtocol::LiveData SkyDeviceManager::latestAi8TemperatureController() const
{
    return latest_ai8_temperature_controller_;
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
        const double phaseRateRadPerSecond = 0.65;
        const double northAmplitudeM = 24.0;
        const double eastAmplitudeM = 16.0;
        const double downAmplitudeM = 4.0;
        const double rollAmplitudeDeg = 12.5;
        const double pitchAmplitudeDeg = 8.5;
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
        latest_epsilon_.ned_n_m = std::sin(simulate_phase_ * 0.8) * northAmplitudeM;
        latest_epsilon_.ned_e_m = std::cos(simulate_phase_ * 0.6) * eastAmplitudeM;
        latest_epsilon_.ned_d_m = -std::sin(simulate_phase_ * 0.4) * downAmplitudeM;
        latest_epsilon_.vel_n_mps = northAmplitudeM * 0.8 * phaseRateRadPerSecond * std::cos(simulate_phase_ * 0.8);
        latest_epsilon_.vel_e_mps = -eastAmplitudeM * 0.6 * phaseRateRadPerSecond * std::sin(simulate_phase_ * 0.6);
        latest_epsilon_.vel_d_mps = -downAmplitudeM * 0.4 * phaseRateRadPerSecond * std::cos(simulate_phase_ * 0.4);
        latest_epsilon_.body_vel_x_mps = latest_epsilon_.vel_n_mps * 0.95 + std::sin(simulate_phase_ * 0.3) * 0.05;
        latest_epsilon_.body_vel_y_mps = latest_epsilon_.vel_e_mps * 0.95 + std::cos(simulate_phase_ * 0.3) * 0.05;
        latest_epsilon_.body_vel_z_mps = latest_epsilon_.vel_d_mps;
        latest_epsilon_.body_acc_x_mps2 =
            -northAmplitudeM * std::pow(0.8 * phaseRateRadPerSecond, 2.0) * std::sin(simulate_phase_ * 0.8);
        latest_epsilon_.body_acc_y_mps2 =
            -eastAmplitudeM * std::pow(0.6 * phaseRateRadPerSecond, 2.0) * std::cos(simulate_phase_ * 0.6);
        latest_epsilon_.body_acc_z_mps2 =
            downAmplitudeM * std::pow(0.4 * phaseRateRadPerSecond, 2.0) * std::sin(simulate_phase_ * 0.4);
        latest_epsilon_.roll_deg = std::sin(simulate_phase_ * 0.5) * rollAmplitudeDeg;
        latest_epsilon_.pitch_deg = std::cos(simulate_phase_ * 0.45) * pitchAmplitudeDeg;
        latest_epsilon_.yaw_deg = positiveDegrees(85.0 + simulate_phase_ * 8.0 + std::sin(simulate_phase_ * 0.2) * 3.0);
        setQuaternionFromEuler(latest_epsilon_);
        latest_epsilon_.ang_vel_x_radps =
            degToRad(rollAmplitudeDeg * 0.5 * phaseRateRadPerSecond * std::cos(simulate_phase_ * 0.5));
        latest_epsilon_.ang_vel_y_radps =
            degToRad(-pitchAmplitudeDeg * 0.45 * phaseRateRadPerSecond * std::sin(simulate_phase_ * 0.45));
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
        const auto simulatedPacketRate = [this](uint8_t packetId, double fallbackHz) {
            const auto it = simulated_epsilon_packet_rates_.find(packetId);
            return it != simulated_epsilon_packet_rates_.end()
                ? static_cast<double>(it->second)
                : fallbackHz;
        };
        latest_epsilon_.imu_packet_rate_hz = simulatedPacketRate(0x40, 100.0);
        latest_epsilon_.ahrs_packet_rate_hz = simulatedPacketRate(0x41, 50.0);
        latest_epsilon_.insgps_packet_rate_hz = simulatedPacketRate(0x42, 50.0);
        latest_epsilon_.sys_state_packet_rate_hz = simulatedPacketRate(0x50, 10.0);
        latest_epsilon_.raw_gnss_packet_rate_hz = simulatedPacketRate(0x59, 1.0);
        latest_epsilon_.satellite_packet_rate_hz = simulatedPacketRate(0x5A, 1.0);
        latest_epsilon_.geodetic_packet_rate_hz = simulatedPacketRate(0x5C, 10.0);
        latest_epsilon_.ecef_packet_rate_hz = simulatedPacketRate(0x5D, 10.0);
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

    if (temperature_controller_status_.state == DeviceState::Connected)
    {
        initializeSimulatedTemperatureController(latest_temperature_controller_);
        latest_temperature_controller_.timestamp = timestamp;
        latest_temperature_controller_.valid = true;
        latest_temperature_controller_.internal_temperature_c =
            32.0 + std::sin(simulate_phase_ * 0.12) * 0.8;
        latest_temperature_controller_.device_address = config_.temperature_controller.slave_address;
        for (TemperatureControllerChannelData& channel : latest_temperature_controller_.channels)
        {
            if (!std::isfinite(channel.measured_temperature_c))
            {
                channel.measured_temperature_c = std::isfinite(channel.target_temperature_c)
                    ? channel.target_temperature_c - 1.5
                    : 24.0;
            }
            const double target = std::isfinite(channel.target_temperature_c)
                ? channel.target_temperature_c
                : 25.0;
            channel.measured_temperature_c += (target - channel.measured_temperature_c) * 0.06;
            if (channel.output_enabled)
            {
                const double demand = std::abs(target - channel.measured_temperature_c) * 18.0;
                const double maxOutput = std::max(0, channel.max_output_percent);
                channel.output_percent = std::clamp(demand, 0.0, maxOutput);
                channel.output_current_a = channel.output_percent * 0.02;
            }
            else
            {
                channel.output_percent = 0.0;
                channel.output_current_a = 0.0;
            }
        }
        temperature_controller_status_.rx_count++;
        temperature_controller_status_.last_data_time_us = t;
        emit temperatureControllerDataUpdated(latest_temperature_controller_);
    }

    if (ai8_temperature_controller_status_.state == DeviceState::Connected)
    {
        latest_ai8_temperature_controller_.valid = true;
        latest_ai8_temperature_controller_.controlStatesValid = true;
        latest_ai8_temperature_controller_.alarmStatusValid = true;
        latest_ai8_temperature_controller_.mainStatusValid = true;
        latest_ai8_temperature_controller_.mainStatusRaw = 0;
        latest_ai8_temperature_controller_.errorMessage.clear();
        for (int i = 0; i < Ai8TemperatureControllerProtocol::kChannelCount; ++i)
        {
            latest_ai8_temperature_controller_.measuredC[static_cast<size_t>(i)] =
                24.0 + i * 0.35 + std::sin(simulate_phase_ * 0.18 + i * 0.7) * 1.2;
            latest_ai8_temperature_controller_.controlStates[static_cast<size_t>(i)] =
                (i % 2 == 0)
                    ? Ai8TemperatureControllerProtocol::ChannelControlState::ApidOutput
                    : Ai8TemperatureControllerProtocol::ChannelControlState::Stopped;
        }
        latest_ai8_temperature_controller_.alarmStatusRegisters.fill(0);
        ai8_temperature_controller_status_.rx_count++;
        ai8_temperature_controller_status_.last_data_time_us = t;
        emit ai8TemperatureControllerDataUpdated(latest_ai8_temperature_controller_);
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
    temperature_controller_status_.device_id = SkyDeviceId::TemperatureController;
    ai8_temperature_controller_status_.device_id = SkyDeviceId::Ai8TemperatureController;
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
    case SkyDeviceId::TemperatureController:
        return temperature_controller_status_;
    case SkyDeviceId::Ai8TemperatureController:
        return ai8_temperature_controller_status_;
    case SkyDeviceId::WaveTcp:
        return wave_tcp_status_;
    case SkyDeviceId::All:
        break;
    }
    return epsilon_status_;
}

SerialDeviceConfig SkyDeviceManager::serialConfigFor(SkyDeviceId id) const
{
    switch (id)
    {
    case SkyDeviceId::Epsilon:
        return serialConfigFromEpsilonConfig(config_.epsilon);
    case SkyDeviceId::Ptb:
        return config_.ptb;
    case SkyDeviceId::Hmp:
        return config_.hmp;
    case SkyDeviceId::Lidar:
        return config_.lidar;
    case SkyDeviceId::TemperatureController:
    case SkyDeviceId::Ai8TemperatureController:
    case SkyDeviceId::WaveTcp:
    case SkyDeviceId::All:
        break;
    }
    return SerialDeviceConfig();
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
        case SkyDeviceId::TemperatureController:
            stopCollector(temperature_controller_);
            break;
        case SkyDeviceId::Ai8TemperatureController:
            stopCollector(ai8_temperature_controller_);
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
        publishDeviceLog(LogLevel::Info,
                         QStringLiteral("device.collector"),
                         QStringLiteral("device_collector_output"),
                         QStringLiteral("设备采集器输出了原始诊断信息。"),
                         {{QStringLiteral("device_id"), skyDeviceIdName(id)},
                          {QStringLiteral("process_output"), QString::fromStdString(message)},
                          {QStringLiteral("external_raw_text"), true}});
    };
    auto structuredLogCallback = [this, id](LogLevel level,
                                            const std::string& category,
                                            const std::string& event,
                                            const std::string& message,
                                            DataCollector::StructuredLogFields fields) {
        QVariantMap mappedFields = structuredLogFieldsFromStd(fields);
        mappedFields.insert(QStringLiteral("device_id"), skyDeviceIdName(id));
        publishDeviceLog(level,
                         QString::fromStdString(category),
                         QString::fromStdString(event),
                         QString::fromStdString(message),
                         mappedFields);
    };

    switch (id)
    {
    case SkyDeviceId::Epsilon:
        epsilon_ = std::make_shared<EpsilonCollector>();
        epsilon_->setLogCallback(logCallback);
        epsilon_->setStructuredLogCallback(structuredLogCallback);
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
            PendingRawEvent event;
            event.deviceId = SkyDeviceId::Epsilon;
            event.collectorIdentity = collector.get();
            event.timestampUs = static_cast<quint64>(hostTimestampUs);
            event.metadata = packetId;
            event.serialNumber = serialNumber;
            event.payload = QByteArray(reinterpret_cast<const char*>(frameData), static_cast<int>(size));
            self->enqueueRawEvent(std::move(event));
        });
        if (!epsilon_->start(config.port.toStdString(), SerialConfig::N81(config.baud_rate))) return fail(CommandErrorCode::DeviceConnectFailed);
        if (!epsilon_->checkDeviceResponse()) return fail(CommandErrorCode::DeviceConnectFailed);
        if (!epsilon_->startStreaming()) return fail(CommandErrorCode::DeviceConnectFailed);
        break;
    case SkyDeviceId::Ptb:
        ptb_ = std::make_shared<PtbCollector>();
        ptb_->setProtocol(pressureProtocolForSource(config.source));
        ptb_->setLogCallback(logCallback);
        ptb_->setStructuredLogCallback(structuredLogCallback);
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
            PendingRawEvent event;
            event.deviceId = SkyDeviceId::Ptb;
            event.collectorIdentity = collector.get();
            event.timestampUs = static_cast<quint64>(hostTimestampUs);
            event.payload = QByteArray(reinterpret_cast<const char*>(responseData), static_cast<int>(size));
            self->enqueueRawEvent(std::move(event));
        });
        if (!ptb_->start(config.port.toStdString(),
                         pressureProtocolForSource(config.source) == PressureSensorProtocol::Bmp390Serial
                             ? SerialConfig::N81(config.baud_rate)
                             : SerialConfig::E71(config.baud_rate))) return fail(CommandErrorCode::DeviceConnectFailed);
        if (!ptb_->checkDeviceResponse()) return fail(CommandErrorCode::DeviceConnectFailed);
        ptb_->setDeviceSampleRate(static_cast<int>(config.frequency_hz));
        if (!ptb_->startStreaming()) return fail(CommandErrorCode::DeviceConnectFailed);
        break;
    case SkyDeviceId::Hmp:
        hmp_ = std::make_shared<HmpCollector>();
        hmp_->setProtocol(humidityProtocolForSource(config.source));
        hmp_->setLogCallback(logCallback);
        hmp_->setStructuredLogCallback(structuredLogCallback);
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
            PendingRawEvent event;
            event.deviceId = SkyDeviceId::Hmp;
            event.collectorIdentity = collector.get();
            event.timestampUs = static_cast<quint64>(hostTimestampUs);
            event.payload = QByteArray(reinterpret_cast<const char*>(responseData), static_cast<int>(size));
            self->enqueueRawEvent(std::move(event));
        });
        if (!hmp_->start(config.port.toStdString(),
                         humidityProtocolForSource(config.source) == HumiditySensorProtocol::Sht45Serial
                             ? SerialConfig::N81(config.baud_rate)
                             : SerialConfig::N82(config.baud_rate))) return fail(CommandErrorCode::DeviceConnectFailed);
        if (!hmp_->checkDeviceResponse()) return fail(CommandErrorCode::DeviceConnectFailed);
        if (!hmp_->startStreaming()) return fail(CommandErrorCode::DeviceConnectFailed);
        break;
    case SkyDeviceId::Lidar:
        lidar_ = std::make_shared<LidarCollector>();
        lidar_->setLogCallback(logCallback);
        lidar_->setStructuredLogCallback(structuredLogCallback);
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
            PendingRawEvent event;
            event.deviceId = SkyDeviceId::Lidar;
            event.collectorIdentity = collector.get();
            event.timestampUs = static_cast<quint64>(hostTimestampUs);
            event.metadata = static_cast<quint16>(protocol);
            event.payload = QByteArray(reinterpret_cast<const char*>(frameData), static_cast<int>(size));
            self->enqueueRawEvent(std::move(event));
        });
        if (!lidar_->start(config.port.toStdString(), SerialConfig::N81(config.baud_rate))) return fail(CommandErrorCode::DeviceConnectFailed);
        if (!lidar_->checkDeviceResponse()) return fail(CommandErrorCode::DeviceConnectFailed);
        lidar_->setDeviceSampleRate(static_cast<int>(config.frequency_hz));
        if (!lidar_->startStreaming()) return fail(CommandErrorCode::DeviceConnectFailed);
        break;
    case SkyDeviceId::TemperatureController:
        temperature_controller_ = std::make_shared<TemperatureControllerCollector>();
        temperature_controller_->setLogCallback(logCallback);
        temperature_controller_->setStructuredLogCallback(structuredLogCallback);
        temperature_controller_->setSampleRate(static_cast<int>(config.frequency_hz));
        temperature_controller_->setSlaveAddress(static_cast<uint8_t>(std::clamp(config_.temperature_controller.slave_address, 1, 247)));
        temperature_controller_->setDataCallback([self = QPointer<SkyDeviceManager>(this), weakCollector = std::weak_ptr<TemperatureControllerCollector>(temperature_controller_)]() {
            if (!self)
            {
                return;
            }
            const std::shared_ptr<TemperatureControllerCollector> collector = weakCollector.lock();
            if (!collector)
            {
                return;
            }
            const TemperatureControllerData data = collector->getLatestData();
            QMetaObject::invokeMethod(self.data(), [self, collector, data]() {
                if (!self || self->temperature_controller_ != collector)
                {
                    return;
                }
                self->handleTemperatureControllerData(data);
            }, Qt::QueuedConnection);
        });
        if (!temperature_controller_->start(config.port.toStdString(), SerialConfig::N81(config.baud_rate))) return fail(CommandErrorCode::DeviceConnectFailed);
        if (!temperature_controller_->checkDeviceResponse()) return fail(CommandErrorCode::DeviceConnectFailed);
        if (!temperature_controller_->startStreaming()) return fail(CommandErrorCode::DeviceConnectFailed);
        break;
    case SkyDeviceId::Ai8TemperatureController:
        ai8_temperature_controller_ = std::make_shared<Ai8TemperatureControllerCollector>();
        ai8_temperature_controller_->setLogCallback(logCallback);
        ai8_temperature_controller_->setStructuredLogCallback(structuredLogCallback);
        ai8_temperature_controller_->setSampleRate(static_cast<int>(config.frequency_hz));
        ai8_temperature_controller_->setSlaveAddress(
            static_cast<uint8_t>(std::clamp(config_.ai8_temperature_controller.slave_address, 1, 88)));
        ai8_temperature_controller_->setDataCallback([self = QPointer<SkyDeviceManager>(this), weakCollector = std::weak_ptr<Ai8TemperatureControllerCollector>(ai8_temperature_controller_)]() {
            if (!self)
            {
                return;
            }
            const std::shared_ptr<Ai8TemperatureControllerCollector> collector = weakCollector.lock();
            if (!collector)
            {
                return;
            }
            const Ai8TemperatureControllerProtocol::LiveData data = collector->getLatestData();
            QMetaObject::invokeMethod(self.data(), [self, collector, data]() {
                if (!self || self->ai8_temperature_controller_ != collector)
                {
                    return;
                }
                self->handleAi8TemperatureControllerData(data);
            }, Qt::QueuedConnection);
        });
        if (!ai8_temperature_controller_->start(config.port.toStdString(), SerialConfig::N81(config.baud_rate))) return fail(CommandErrorCode::DeviceConnectFailed);
        if (!ai8_temperature_controller_->checkDeviceResponse()) return fail(CommandErrorCode::DeviceConnectFailed);
        if (!ai8_temperature_controller_->startStreaming()) return fail(CommandErrorCode::DeviceConnectFailed);
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

void SkyDeviceManager::handleTemperatureControllerData(const TemperatureControllerData& data)
{
    latest_temperature_controller_ = data;
    temperature_controller_status_.rx_count++;
    temperature_controller_status_.last_data_time_us = nowUs();
    emit temperatureControllerDataUpdated(latest_temperature_controller_);
}

void SkyDeviceManager::handleAi8TemperatureControllerData(const Ai8TemperatureControllerProtocol::LiveData& data)
{
    latest_ai8_temperature_controller_ = data;
    ai8_temperature_controller_status_.rx_count++;
    ai8_temperature_controller_status_.last_data_time_us = nowUs();
    emit ai8TemperatureControllerDataUpdated(latest_ai8_temperature_controller_);
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
    QTcpSocket *socket = new QTcpSocket(this);
    wave_socket_ = socket;
    connect(socket, &QTcpSocket::connected, this, [this, socket]() {
        if (socket == wave_socket_)
        {
            onWaveTcpConnected();
        }
    });
    connect(socket, &QTcpSocket::disconnected, this, [this, socket]() {
        if (socket == wave_socket_)
        {
            onWaveTcpDisconnected();
        }
    });
    connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
        if (socket == wave_socket_)
        {
            onWaveTcpReadyRead();
        }
    });
    connect(socket, &QTcpSocket::errorOccurred, this, [this, socket](QAbstractSocket::SocketError) {
        if (socket == wave_socket_)
        {
            onWaveTcpError();
        }
    });
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
                publishDeviceLog(LogLevel::Warning,
                                 QStringLiteral("device.wave_tcp"),
                                 QStringLiteral("wave_tcp_resync_discarded_bytes"),
                                 QStringLiteral("Wave TCP 重新同步时已丢弃部分字节。"),
                                 {{QStringLiteral("reason_code"), QStringLiteral("WAVE_TCP_FRAME_HEADER_NOT_FOUND")},
                                  {QStringLiteral("dropped_bytes"), bytesToDrop}});
            }
            return;
        }

        if (frameOffset > 0)
        {
            wave_buffer_.remove(0, frameOffset);
            publishDeviceLog(LogLevel::Warning,
                             QStringLiteral("device.wave_tcp"),
                             QStringLiteral("wave_tcp_resync_skipped_bytes"),
                             QStringLiteral("Wave TCP 重新同步时已跳过部分字节。"),
                             {{QStringLiteral("reason_code"), QStringLiteral("WAVE_TCP_FRAME_OFFSET")},
                              {QStringLiteral("skipped_bytes"), frameOffset},
                              {QStringLiteral("header_order"), waveTcpHeaderOrderText(headerOrder)}});
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
            publishDeviceLog(LogLevel::Info,
                             QStringLiteral("device.wave_tcp"),
                             QStringLiteral("wave_tcp_payload_format_locked"),
                             QStringLiteral("Wave TCP 载荷格式已锁定。"),
                             {{QStringLiteral("header_order"), waveTcpHeaderOrderText(headerOrder)},
                              {QStringLiteral("float_encoding"),
                               tcpFloatEncodingLabel(false, wave_float_encoding_)}});
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
    case SkyDeviceId::TemperatureController:
        latest_temperature_controller_ = TemperatureControllerData();
        temperature_controller_status_.last_data_time_us = 0;
        break;
    case SkyDeviceId::Ai8TemperatureController:
        latest_ai8_temperature_controller_ = Ai8TemperatureControllerProtocol::LiveData();
        ai8_temperature_controller_status_.last_data_time_us = 0;
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
        invalidateDeviceData(SkyDeviceId::TemperatureController);
        invalidateDeviceData(SkyDeviceId::Ai8TemperatureController);
        invalidateDeviceData(SkyDeviceId::WaveTcp);
        break;
    }
}

}  // namespace VaporView
