#include "ground/devices/RemoteSkyController.h"
#include <QtEndian>

#include "ground/devices/RemoteTelemetryDecoder.h"
#include "shared/config/SettingsWriteBarrier.h"
#include "LogService.h"

#include <QDateTime>
#include <QMetaObject>
#include <QRandomGenerator>
#include <QTimer>

#include <chrono>

namespace VaporView::Ground::Devices
{

RemoteSkyController::RemoteSkyController(QObject *parent)
    : QObject(parent)
{
    rtcm_clock_.start();
    next_device_operation_request_id_ = QRandomGenerator::global()->generate();
    connect(&service_, &GroundTelemetryService::linkOpenChanged,
            this, [this](bool open) {
                const quint64 generation = service_.linkGeneration();
                QMetaObject::invokeMethod(this, [this, generation, open]() {
                    if (isCurrentEvent(generation))
                    {
                        if (open)
                        {
                            rtcm_status_.reconnect();
                            rtcm_diagnostics_need_baseline_ = true;
                        }
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
                    const auto operation = device_operation_types_.value(response.request_id);
                    const bool epsilonOperation = response.device_id == SkyDeviceId::Epsilon ||
                        static_cast<quint8>(response.operation) >= static_cast<quint8>(DeviceOperation::ConfigureEpsilonPacketRates) ||
                        static_cast<quint8>(operation) >= static_cast<quint8>(DeviceOperation::ConfigureEpsilonPacketRates);
                    // Other devices historically forward their typed failure after an error ACK
                    // has removed the request. EPSILON progress requires a still-active match.
                    if (epsilonOperation && (response.device_id != SkyDeviceId::Epsilon ||
                        !device_operation_types_.contains(response.request_id) || response.operation != operation)) return;
                    if (operation == DeviceOperation::CalibrateEpsilonMagnetic2D || operation == DeviceOperation::CalibrateEpsilonMagnetic3D)
                    {
                        EpsilonMaintenanceResult progress;
                        if (!(response.payload.isEmpty() && response.error_code != CommandErrorCode::Ok) && (response.device_id != SkyDeviceId::Epsilon ||
                            !TelemetryCodec::parseEpsilonMaintenanceResult(response.payload, progress) ||
                            progress.action != (operation == DeviceOperation::CalibrateEpsilonMagnetic2D ? EpsilonMaintenanceAction::Magnetic2D : EpsilonMaintenanceAction::Magnetic3D))) return;
                        if (progress.status == EpsilonMaintenanceStatus::Running)
                        {
                            if (response.error_code == CommandErrorCode::Ok) emit deviceOperationResponseReceived(response);
                            return;
                        }
                    }
                    device_operation_types_.remove(response.request_id);
                    if (operation == DeviceOperation::ReadEpsilonSettings || operation == DeviceOperation::ApplyEpsilonSettings ||
                        operation == DeviceOperation::RestartEpsilonDevice)
                    {
                        EpsilonSettingsSnapshot snapshot;
                        const bool valid = response.device_id == SkyDeviceId::Epsilon && response.operation == operation &&
                            (operation == DeviceOperation::RestartEpsilonDevice ||
                             (TelemetryCodec::parseEpsilonSettingsSnapshot(response.payload, snapshot) && !snapshot.values.empty()));
                        const auto support = valid && response.error_code == CommandErrorCode::Ok ? DeviceOperationSupport::Supported :
                            (response.error_code == CommandErrorCode::UnknownCommand || response.error_code == CommandErrorCode::InvalidPayload ?
                             DeviceOperationSupport::Unsupported : epsilon_settings_support_);
                        if (support != epsilon_settings_support_)
                        {
                            epsilon_settings_support_ = support;
                            emit epsilonSettingsSupportChanged(support);
                        }
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
                        const auto requestOperation = device_operation_types_.value(device_operation_requests_.value(ack.command_seq));
                        const bool settingsOperation = requestOperation == DeviceOperation::ReadEpsilonSettings ||
                            requestOperation == DeviceOperation::ApplyEpsilonSettings || requestOperation == DeviceOperation::RestartEpsilonDevice ||
                            requestOperation == DeviceOperation::CalibrateEpsilonLevel || requestOperation == DeviceOperation::CalibrateEpsilonAccelerometer ||
                            requestOperation == DeviceOperation::CalibrateEpsilonGyroscope ||
                            requestOperation == DeviceOperation::ReadEpsilonDgnss || requestOperation == DeviceOperation::ApplyEpsilonDgnss ||
                            requestOperation == DeviceOperation::CalibrateEpsilonMagnetic2D || requestOperation == DeviceOperation::CalibrateEpsilonMagnetic3D ||
                            requestOperation == DeviceOperation::CancelEpsilonMagneticCalibration;
                        if (ack.error_code == CommandErrorCode::UnknownCommand && !settingsOperation &&
                            device_operation_support_ != DeviceOperationSupport::Unsupported)
                        {
                            device_operation_support_ = DeviceOperationSupport::Unsupported;
                            emit deviceOperationSupportChanged(device_operation_support_);
                        }
                        // New Sky sends a typed failure after its ACK, carrying partial writes
                        // and restart risk. Allow that response before falling back for old peers.
                        const quint32 acknowledgedRequest = device_operation_requests_.value(ack.command_seq);
                        if (settingsOperation && acknowledgedRequest != 0 &&
                            ack.error_code != CommandErrorCode::UnknownCommand &&
                            ack.error_code != CommandErrorCode::InvalidPayload)
                        {
                            QTimer::singleShot(30000, this, [this, generation, acknowledgedRequest, ack]() {
                                if (!isCurrentOpenEvent(generation) ||
                                    device_operation_requests_.value(ack.command_seq) != acknowledgedRequest) return;
                                device_operation_requests_.remove(ack.command_seq);
                                device_operation_commands_.remove(acknowledgedRequest);
                                device_operation_types_.remove(acknowledgedRequest);
                                emit deviceOperationRejected(acknowledgedRequest, ack);
                            });
                            emit commandAckReceived(ack);
                            return;
                        }
                        const quint32 requestId = device_operation_requests_.take(ack.command_seq);
                        if (requestId != 0)
                        {
                            device_operation_commands_.remove(requestId);
                            const auto operation = device_operation_types_.take(requestId);
                            CommandAck rejection = ack;
                            if ((operation == DeviceOperation::ReadEpsilonSettings || operation == DeviceOperation::ApplyEpsilonSettings ||
                                 operation == DeviceOperation::RestartEpsilonDevice) &&
                                (ack.error_code == CommandErrorCode::UnknownCommand || ack.error_code == CommandErrorCode::InvalidPayload))
                            {
                                epsilon_settings_support_ = DeviceOperationSupport::Unsupported;
                                emit epsilonSettingsSupportChanged(epsilon_settings_support_);
                                rejection.error_code = CommandErrorCode::UnknownCommand;
                            }
                            emit deviceOperationRejected(requestId, rejection);
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
                            device_operation_types_.remove(requestId);
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

quint32 RemoteSkyController::configureEpsilonPacketRates(
    const EpsilonPacketRatesOperation& operation)
{
    return sendDeviceOperation(
        SkyDeviceId::Epsilon,
        DeviceOperation::ConfigureEpsilonPacketRates,
        TelemetryCodec::serializeEpsilonPacketRatesOperation(operation));
}

quint32 RemoteSkyController::configureEpsilonMainAntennaLeverArm(
    const EpsilonMainAntennaLeverArmOperation& operation)
{
    return sendDeviceOperation(
        SkyDeviceId::Epsilon,
        DeviceOperation::ConfigureEpsilonMainAntennaLeverArm,
        TelemetryCodec::serializeEpsilonMainAntennaLeverArmOperation(operation));
}

quint32 RemoteSkyController::configureEpsilonRtcmInput(
    const EpsilonRtcmInputOperation& operation)
{
    return sendDeviceOperation(
        SkyDeviceId::Epsilon,
        DeviceOperation::ConfigureEpsilonRtcmInput,
        TelemetryCodec::serializeEpsilonRtcmInputOperation(operation));
}

bool RemoteSkyController::sendRtcmCorrectionData(const QByteArray& data)
{
    if (VaporView::settingsWritesSuspended())
    {
        return false;
    }
    return service_.sendRtcmCorrectionData(data);
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
    rtcm_status_.reset();
    rtcm_diagnostics_need_baseline_ = true;
    device_operation_requests_.clear();
    device_operation_commands_.clear();
    device_operation_support_ = DeviceOperationSupport::Unknown;
    epsilon_settings_support_ = DeviceOperationSupport::Unknown;
    device_operation_types_.clear();
    emit epsilonSettingsSupportChanged(epsilon_settings_support_);
}

void RemoteSkyController::markLinkClosed()
{
    state_.markLinkClosed();
    device_operation_requests_.clear();
    device_operation_commands_.clear();
    device_operation_support_ = DeviceOperationSupport::Unknown;
    epsilon_settings_support_ = DeviceOperationSupport::Unknown;
    device_operation_types_.clear();
    emit epsilonSettingsSupportChanged(epsilon_settings_support_);
}

quint32 RemoteSkyController::readEpsilonSettings(EpsilonSettingsGroup group)
{
    if (!isValidEpsilonSettingsGroup(group) || epsilon_settings_support_ == DeviceOperationSupport::Unsupported) return 0;
    return sendDeviceOperation(SkyDeviceId::Epsilon, DeviceOperation::ReadEpsilonSettings,
                               TelemetryCodec::serializeEpsilonSettingsRead(group));
}

quint32 RemoteSkyController::applyEpsilonSettings(const EpsilonSettingsOperation& operation)
{
    std::string error;
    if (!validateEpsilonSettings(operation, error) || epsilon_settings_support_ != DeviceOperationSupport::Supported) return 0;
    return sendDeviceOperation(SkyDeviceId::Epsilon, DeviceOperation::ApplyEpsilonSettings,
                               TelemetryCodec::serializeEpsilonSettingsOperation(operation));
}

quint32 RemoteSkyController::calibrateEpsilon(EpsilonMaintenanceAction action)
{
    if (!validEpsilonMaintenanceAction(action)) return 0;
    const auto operation = action == EpsilonMaintenanceAction::Level ? DeviceOperation::CalibrateEpsilonLevel :
        action == EpsilonMaintenanceAction::Accelerometer ? DeviceOperation::CalibrateEpsilonAccelerometer :
        action == EpsilonMaintenanceAction::Gyroscope ? DeviceOperation::CalibrateEpsilonGyroscope :
        action == EpsilonMaintenanceAction::Magnetic2D ? DeviceOperation::CalibrateEpsilonMagnetic2D : DeviceOperation::CalibrateEpsilonMagnetic3D;
    return sendDeviceOperation(SkyDeviceId::Epsilon, operation, TelemetryCodec::serializeEpsilonMaintenanceAction(action));
}

quint32 RemoteSkyController::readEpsilonDgnss()
{
    return sendDeviceOperation(SkyDeviceId::Epsilon, DeviceOperation::ReadEpsilonDgnss, {});
}

quint32 RemoteSkyController::applyEpsilonDgnss(const EpsilonDgnssOperation& operation)
{
    std::string error;
    if (!validateEpsilonDgnss(operation, error)) return 0;
    return sendDeviceOperation(SkyDeviceId::Epsilon, DeviceOperation::ApplyEpsilonDgnss,
                               TelemetryCodec::serializeEpsilonDgnssOperation(operation));
}

quint32 RemoteSkyController::cancelEpsilonMagneticCalibration(quint32 requestId)
{
    if (!requestId) return 0;
    QByteArray payload(4, '\0');
    qToLittleEndian<quint32>(requestId, reinterpret_cast<uchar*>(payload.data()));
    return sendDeviceOperation(SkyDeviceId::Epsilon, DeviceOperation::CancelEpsilonMagneticCalibration, payload);
}

quint32 RemoteSkyController::restartEpsilonDevice()
{
    if (epsilon_settings_support_ != DeviceOperationSupport::Supported) return 0;
    return sendDeviceOperation(SkyDeviceId::Epsilon, DeviceOperation::RestartEpsilonDevice, QByteArray());
}

DeviceOperationSupport RemoteSkyController::epsilonSettingsSupport() const
{
    return epsilon_settings_support_;
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
    if (rtcm_diagnostics_need_baseline_ || status.rtcm_boot_id != last_rtcm_boot_id_)
    {
        last_rtcm_drop_logged_ = status.rtcm_correction_dropped_chunks;
        rtcm_loss_warning_logged_ = false;
        rtcm_diagnostics_need_baseline_ = false;
    }
    last_rtcm_boot_id_ = status.rtcm_boot_id;
    rtcm_status_.receiveStatus(status, rtcm_clock_.elapsed());
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

RtcmStatusSnapshot RemoteSkyController::rtcmStatus(bool remote, bool rtkRunning)
{
    rtcm_status_.setContext(remote, rtkRunning);
    const RtcmStatusSnapshot snapshot = rtcm_status_.snapshot(isOpen(), rtcm_clock_.elapsed());
    const bool interrupted = snapshot.health == RtcmHealth::Interrupted ||
        snapshot.health == RtcmHealth::LinkDisconnected;
    const bool wasInterrupted = last_rtcm_health_ == RtcmHealth::Interrupted ||
        last_rtcm_health_ == RtcmHealth::LinkDisconnected;
    if ((interrupted && !wasInterrupted && remote && rtkRunning) ||
        (wasInterrupted && (snapshot.health == RtcmHealth::Normal || snapshot.health == RtcmHealth::Warning)))
    {
        LogService::withCurrentInstance([&](LogService& logs) {
            if (interrupted)
                logs.publish(LogLevel::Warning, QStringLiteral("Ground"), QStringLiteral("telemetry.rtcm"),
                    QStringLiteral("天空端 RTCM 数据已中断。"),
                    {{QStringLiteral("event"), QStringLiteral("rtcm_stream_interrupted")},
                     {QStringLiteral("age_ms"), snapshot.ageMs}});
            else
                logs.publish(LogLevel::Info, QStringLiteral("Ground"), QStringLiteral("telemetry.rtcm"),
                    QStringLiteral("天空端 RTCM 数据已恢复。"),
                    {{QStringLiteral("event"), QStringLiteral("rtcm_stream_recovered")},
                     {QStringLiteral("age_ms"), snapshot.ageMs}});
        });
    }
    if (!remote || !rtkRunning || !snapshot.available || snapshot.droppedChunks < last_rtcm_drop_logged_)
        last_rtcm_drop_logged_ = snapshot.droppedChunks;
    if (!snapshot.lossWarning) rtcm_loss_warning_logged_ = false;
    const qint64 nowMs = rtcm_clock_.elapsed();
    if (remote && rtkRunning && snapshot.available && nowMs - last_rtcm_diagnostic_ms_ >= 10000)
    {
        if (snapshot.droppedChunks - last_rtcm_drop_logged_ >= 10)
        {
            LogService::withCurrentInstance([&](LogService& logs) {
                logs.publish(LogLevel::Warning, QStringLiteral("Ground"), QStringLiteral("telemetry.rtcm"),
                    QStringLiteral("天空端 RTCM 本地丢弃明显增加。"),
                    {{QStringLiteral("event"), QStringLiteral("rtcm_sky_drops_increased")},
                     {QStringLiteral("dropped_chunks"), snapshot.droppedChunks},
                     {QStringLiteral("delta_chunks"), snapshot.droppedChunks - last_rtcm_drop_logged_}});
            });
            last_rtcm_drop_logged_ = snapshot.droppedChunks;
            last_rtcm_diagnostic_ms_ = nowMs;
        }
        if (snapshot.lossWarning && !rtcm_loss_warning_logged_)
        {
            LogService::withCurrentInstance([&](LogService& logs) {
                logs.publish(LogLevel::Warning, QStringLiteral("Ground"), QStringLiteral("telemetry.rtcm"),
                    QStringLiteral("RTCM 天地链路丢失率升高。"),
                    {{QStringLiteral("event"), QStringLiteral("rtcm_link_loss_increased")},
                     {QStringLiteral("loss_percent"), snapshot.lossPercent}});
            });
            rtcm_loss_warning_logged_ = true;
            last_rtcm_diagnostic_ms_ = nowMs;
        }
    }
    last_rtcm_health_ = snapshot.health;
    return snapshot;
}

quint32 RemoteSkyController::sendDeviceOperation(
    SkyDeviceId device,
    DeviceOperation operation,
    const QByteArray& payload)
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
    request.device_id = device;
    request.operation = operation;
    request.payload = payload;
    const quint16 commandSequence = service_.sendDeviceOperation(request);
    if (commandSequence == 0)
    {
        return 0;
    }
    device_operation_requests_.insert(commandSequence, request.request_id);
    device_operation_commands_.insert(request.request_id, commandSequence);
    device_operation_types_.insert(request.request_id, operation);
    return request.request_id;
}

quint32 RemoteSkyController::sendAi8Operation(
    DeviceOperation operation,
    const Ai8TemperatureControllerProtocol::PageData& data)
{
    return sendDeviceOperation(SkyDeviceId::Ai8TemperatureController,
                               operation,
                               TelemetryCodec::serializeAi8PageData(data));
}

}  // namespace VaporView::Ground::Devices
