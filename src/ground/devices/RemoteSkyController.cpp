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
    connect(&service_, &GroundTelemetryService::ai8TemperatureControllerStatusUpdated,
            this, [this](const Ai8TemperatureControllerProtocol::LiveData& data) {
                const quint64 generation = service_.linkGeneration();
                QMetaObject::invokeMethod(this, [this, generation, data]() {
                    if (!isCurrentOpenEvent(generation)) return;
                    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
                    state_.notePacket(MsgType::Ai8TemperatureControllerStatus, nowMs);
                    state_.noteDeviceData(SkyDeviceId::Ai8TemperatureController, nowMs);
                    emit ai8TemperatureControllerStatusUpdated(data);
                }, Qt::QueuedConnection);
            }, Qt::DirectConnection);
    connect(&service_, &GroundTelemetryService::deviceOperationResponseReceived,
            this, [this](const DeviceOperationResponse& response) {
                const quint64 generation = service_.linkGeneration();
                QMetaObject::invokeMethod(this, [this, generation, response]() {
                    if (!isCurrentOpenEvent(generation)) return;
                    if (device_operation_support_ != DeviceOperationSupport::Supported)
                    {
                        device_operation_support_ = DeviceOperationSupport::Supported;
                        emit deviceOperationSupportChanged(device_operation_support_);
                    }
                    const quint16 sequence = device_operation_commands_.take(response.request_id);
                    if (sequence != 0)
                    {
                        device_operation_requests_.remove(sequence);
                    }
                    emit deviceOperationResponseReceived(response);
                }, Qt::QueuedConnection);
            }, Qt::DirectConnection);
    connect(&service_, &GroundTelemetryService::commandAckReceived,
            this, [this](const CommandAck& ack) {
                const quint64 generation = service_.linkGeneration();
                QMetaObject::invokeMethod(this, [this, generation, ack]() {
                    if (!isCurrentOpenEvent(generation)) return;
                    if (ack.command_id == CommandId::DeviceOperation && ack.error_code != CommandErrorCode::Ok)
                    {
                        if (ack.error_code == CommandErrorCode::UnknownCommand &&
                            device_operation_support_ != DeviceOperationSupport::Unsupported)
                        {
                            device_operation_support_ = DeviceOperationSupport::Unsupported;
                            emit deviceOperationSupportChanged(device_operation_support_);
                        }
                        const quint32 requestId = device_operation_requests_.take(ack.command_seq);
                        if (requestId != 0)
                        {
                            device_operation_commands_.remove(requestId);
                            emit deviceOperationRejected(requestId, ack);
                        }
                    }
                    emit commandAckReceived(ack);
                }, Qt::QueuedConnection);
            }, Qt::DirectConnection);
    connect(&service_, &GroundTelemetryService::commandTimedOut,
            this, [this](CommandId command, quint16 sequence) {
                const quint64 generation = service_.linkGeneration();
                QMetaObject::invokeMethod(this, [this, generation, command, sequence]() {
                    if (!isCurrentEvent(generation)) return;
                    if (command == CommandId::DeviceOperation)
                    {
                        const quint32 requestId = device_operation_requests_.take(sequence);
                        if (requestId != 0)
                        {
                            device_operation_commands_.remove(requestId);
                            emit deviceOperationTimedOut(requestId);
                        }
                    }
                    emit commandTimedOut(command, sequence);
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

quint32 RemoteSkyController::readAi8Page(Ai8TemperatureControllerProtocol::Page page,
                                         const Ai8TemperatureControllerProtocol::Selection& selection)
{
    Ai8TemperatureControllerProtocol::PageData data;
    data.page = page;
    data.selection = selection;
    return sendAi8Operation(DeviceOperation::ReadParameters, data);
}

quint32 RemoteSkyController::writeAi8Page(const Ai8TemperatureControllerProtocol::PageData& data)
{
    return sendAi8Operation(DeviceOperation::WriteParameters, data);
}

quint32 RemoteSkyController::restoreAi8FactoryDefaults(
    Ai8TemperatureControllerProtocol::Page page,
    const Ai8TemperatureControllerProtocol::Selection& selection)
{
    Ai8TemperatureControllerProtocol::PageData data;
    data.page = page;
    data.selection = selection;
    return sendAi8Operation(DeviceOperation::FactoryReset, data);
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
    device_operation_requests_.clear();
    device_operation_commands_.clear();
    device_operation_support_ = DeviceOperationSupport::Unknown;
}

void RemoteSkyController::markLinkClosed()
{
    state_.markLinkClosed();
    device_operation_requests_.clear();
    device_operation_commands_.clear();
    device_operation_support_ = DeviceOperationSupport::Unknown;
}

DeviceOperationSupport RemoteSkyController::deviceOperationSupport() const
{
    return device_operation_support_;
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

quint32 RemoteSkyController::sendAi8Operation(
    DeviceOperation operation,
    const Ai8TemperatureControllerProtocol::PageData& data)
{
    if (VaporView::settingsWritesSuspended() || !service_.isOpen() ||
        device_operation_support_ == DeviceOperationSupport::Unsupported)
    {
        return 0;
    }
    DeviceOperationRequest request;
    request.request_id = next_device_operation_request_id_++;
    if (request.request_id == 0)
    {
        request.request_id = next_device_operation_request_id_++;
    }
    request.device_id = SkyDeviceId::Ai8TemperatureController;
    request.operation = operation;
    request.payload = TelemetryCodec::serializeAi8PageData(data);
    const quint16 commandSequence = service_.sendDeviceOperation(request);
    if (commandSequence == 0)
    {
        return 0;
    }
    device_operation_requests_.insert(commandSequence, request.request_id);
    device_operation_commands_.insert(request.request_id, commandSequence);
    return request.request_id;
}

}  // namespace VaporView::Ground::Devices
