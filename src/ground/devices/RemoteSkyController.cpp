#include "ground/devices/RemoteSkyController.h"

#include "ground/devices/RemoteTelemetryDecoder.h"
#include "shared/config/SettingsWriteBarrier.h"

#include <QDateTime>
#include <QMetaObject>

#include <chrono>

namespace VaporView::Ground::Devices
{

RemoteSkyController::RemoteSkyController(QObject *parent)
    : QObject(parent)
{
    connect(&service_, &GroundTelemetryService::linkOpenChanged,
            this, [this](bool open) {
                const quint64 generation = service_.linkGeneration();
                QMetaObject::invokeMethod(this, [this, generation, open]() {
                    if (isCurrentEvent(generation))
                    {
                        emit linkOpenChanged(open);
                    }
                }, Qt::QueuedConnection);
            }, Qt::DirectConnection);
    connect(&service_, &GroundTelemetryService::basicTelemetryUpdated,
            this, [this](const TelemetryBasic& telemetry) {
                const quint64 generation = service_.linkGeneration();
                QMetaObject::invokeMethod(this, [this, generation, telemetry]() {
                    if (!isCurrentOpenEvent(generation)) return;
                    updateBasicState(telemetry);
                    emit basicTelemetryUpdated(telemetry);
                }, Qt::QueuedConnection);
            }, Qt::DirectConnection);
    connect(&service_, &GroundTelemetryService::waveformUpdated,
            this, [this](const DownsampledWaveform& waveform) {
                const quint64 generation = service_.linkGeneration();
                QMetaObject::invokeMethod(this, [this, generation, waveform]() {
                    if (!isCurrentOpenEvent(generation)) return;
                    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
                    state_.notePacket(MsgType::WaveformDownsampled, nowMs);
                    state_.noteWaveformPacket(waveform.channel_id, nowMs);
                    state_.noteDeviceData(SkyDeviceId::WaveTcp, nowMs);
                    emit waveformUpdated(waveform);
                }, Qt::QueuedConnection);
            }, Qt::DirectConnection);
    connect(&service_, &GroundTelemetryService::waveformFeatureUpdated,
            this, [this](const WaveformFeature& feature) {
                const quint64 generation = service_.linkGeneration();
                QMetaObject::invokeMethod(this, [this, generation, feature]() {
                    if (!isCurrentOpenEvent(generation)) return;
                    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
                    state_.notePacket(MsgType::WaveformFeature, nowMs);
                    state_.noteDeviceData(SkyDeviceId::WaveTcp, nowMs);
                    emit waveformFeatureUpdated(feature);
                }, Qt::QueuedConnection);
            }, Qt::DirectConnection);
    connect(&service_, &GroundTelemetryService::statusUpdated,
            this, [this](const TelemetryStatus& status) {
                const quint64 generation = service_.linkGeneration();
                QMetaObject::invokeMethod(this, [this, generation, status]() {
                    if (!isCurrentOpenEvent(generation)) return;
                    updateStatusState(status);
                    emit statusUpdated(status);
                }, Qt::QueuedConnection);
            }, Qt::DirectConnection);
    connect(&service_, &GroundTelemetryService::temperatureControllerStatusUpdated,
            this, [this](const TemperatureControllerData& data) {
                const quint64 generation = service_.linkGeneration();
                QMetaObject::invokeMethod(this, [this, generation, data]() {
                    if (!isCurrentOpenEvent(generation)) return;
                    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
                    state_.notePacket(MsgType::TemperatureControllerStatus, nowMs);
                    state_.noteDeviceData(SkyDeviceId::TemperatureController, nowMs);
                    emit temperatureControllerStatusUpdated(data);
                }, Qt::QueuedConnection);
            }, Qt::DirectConnection);
    connect(&service_, &GroundTelemetryService::commandAckReceived,
            this, [this](const CommandAck& ack) {
                const quint64 generation = service_.linkGeneration();
                QMetaObject::invokeMethod(this, [this, generation, ack]() {
                    if (isCurrentOpenEvent(generation)) emit commandAckReceived(ack);
                }, Qt::QueuedConnection);
            }, Qt::DirectConnection);
    connect(&service_, &GroundTelemetryService::commandTimedOut,
            this, [this](CommandId command, quint16 sequence) {
                const quint64 generation = service_.linkGeneration();
                QMetaObject::invokeMethod(this, [this, generation, command, sequence]() {
                    if (isCurrentEvent(generation)) emit commandTimedOut(command, sequence);
                }, Qt::QueuedConnection);
            }, Qt::DirectConnection);
}

bool RemoteSkyController::openSerial(const QString& port, int baud)
{
    if (VaporView::settingsWritesSuspended()) return false;
    return service_.open(port, baud);
}

bool RemoteSkyController::openTcp(const QString& host, quint16 port)
{
    if (VaporView::settingsWritesSuspended()) return false;
    return service_.openTcp(host, port);
}

void RemoteSkyController::close()
{
    service_.close();
}

bool RemoteSkyController::isOpen() const
{
    return service_.isOpen();
}

quint64 RemoteSkyController::linkGeneration() const
{
    return service_.linkGeneration();
}

double RemoteSkyController::receiveBitsPerSecond() const
{
    return service_.receiveBitsPerSecond();
}

double RemoteSkyController::transmitBitsPerSecond() const
{
    return service_.transmitBitsPerSecond();
}

quint16 RemoteSkyController::sendCommand(CommandId command, const QByteArray& payload)
{
    if (VaporView::settingsWritesSuspended()) return 0;
    return service_.sendCommand(command, payload);
}

quint16 RemoteSkyController::sendDeviceCommand(CommandId command, SkyDeviceId device)
{
    if (VaporView::settingsWritesSuspended()) return 0;
    return service_.sendDeviceCommand(command, device);
}

quint16 RemoteSkyController::sendRateCommand(CommandId command, quint16 rateHz)
{
    if (VaporView::settingsWritesSuspended()) return 0;
    return service_.sendRateCommand(command, rateHz);
}

quint16 RemoteSkyController::sendPeakSearchRangeCommand(quint32 startIndex, quint32 endIndex)
{
    if (VaporView::settingsWritesSuspended()) return 0;
    return service_.sendPeakSearchRangeCommand(startIndex, endIndex);
}

quint16 RemoteSkyController::requestSkyConfig()
{
    if (VaporView::settingsWritesSuspended()) return 0;
    return service_.requestSkyConfig();
}

GroundTelemetryService *RemoteSkyController::telemetryService()
{
    return &service_;
}

void RemoteSkyController::resetState()
{
    state_.reset();
}

void RemoteSkyController::markLinkClosed()
{
    state_.markLinkClosed();
}

void RemoteSkyController::setDeviceState(SkyDeviceId device, DeviceState state)
{
    state_.setDeviceState(device, state);
}

void RemoteSkyController::noteDeviceData(SkyDeviceId device, qint64 nowMs)
{
    state_.noteDeviceData(device, nowMs);
}

void RemoteSkyController::clearDeviceData(SkyDeviceId device)
{
    state_.clearDeviceData(device);
}

void RemoteSkyController::noteStatus(qint64 nowMs)
{
    state_.noteStatus(nowMs);
}

void RemoteSkyController::notePacket(MsgType type, qint64 nowMs)
{
    state_.notePacket(type, nowMs);
}

void RemoteSkyController::noteWaveformPacket(quint16 channelId, qint64 nowMs)
{
    state_.noteWaveformPacket(channelId, nowMs);
}

DeviceState RemoteSkyController::deviceState(SkyDeviceId device) const
{
    return state_.deviceState(device);
}

bool RemoteSkyController::statusFresh(qint64 nowMs, qint64 timeoutMs) const
{
    return state_.statusFresh(nowMs, timeoutMs);
}

qint64 RemoteSkyController::lastStatusMs() const
{
    return state_.lastStatusMs();
}

bool RemoteSkyController::deviceDataFresh(SkyDeviceId device,
                                          qint64 nowMs,
                                          qint64 timeoutMs) const
{
    return state_.deviceDataFresh(device, nowMs, timeoutMs);
}

qint64 RemoteSkyController::lastDeviceDataMs(SkyDeviceId device) const
{
    return state_.lastDeviceDataMs(device);
}

double RemoteSkyController::packetRate(MsgType type) const
{
    return state_.packetRate(type);
}

double RemoteSkyController::waveformPacketRate(quint16 channelId) const
{
    return state_.waveformPacketRate(channelId);
}

bool RemoteSkyController::isCurrentEvent(quint64 generation) const
{
    return service_.linkGeneration() == generation;
}

bool RemoteSkyController::isCurrentOpenEvent(quint64 generation) const
{
    return isCurrentEvent(generation) && service_.isOpen();
}

void RemoteSkyController::updateBasicState(const TelemetryBasic& telemetry)
{
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    state_.notePacket(MsgType::TelemetryBasic, nowMs);
    const auto hasFlag = [&telemetry](quint32 flag) {
        return (telemetry.validity_flags & flag) != 0;
    };
    const auto epsilon = Ground::decodeRemoteEpsilonTelemetry(
        telemetry,
        std::chrono::steady_clock::now());
    epsilon.available
        ? state_.noteDeviceData(SkyDeviceId::Epsilon, nowMs)
        : state_.clearDeviceData(SkyDeviceId::Epsilon);
    hasFlag(BasicHasLidar)
        ? state_.noteDeviceData(SkyDeviceId::Lidar, nowMs)
        : state_.clearDeviceData(SkyDeviceId::Lidar);
    (hasFlag(BasicHasTemperature) && hasFlag(BasicHasHumidity))
        ? state_.noteDeviceData(SkyDeviceId::Hmp, nowMs)
        : state_.clearDeviceData(SkyDeviceId::Hmp);
    hasFlag(BasicHasPressure)
        ? state_.noteDeviceData(SkyDeviceId::Ptb, nowMs)
        : state_.clearDeviceData(SkyDeviceId::Ptb);
}

void RemoteSkyController::updateStatusState(const TelemetryStatus& status)
{
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    state_.notePacket(MsgType::TelemetryStatus, nowMs);
    state_.noteStatus(nowMs);
    for (const DeviceStatusItem& item : status.devices)
    {
        state_.setDeviceState(item.device_id, item.state);
        if (item.state != DeviceState::Connected)
        {
            state_.clearDeviceData(item.device_id);
        }
    }
}

}  // namespace VaporView::Ground::Devices
