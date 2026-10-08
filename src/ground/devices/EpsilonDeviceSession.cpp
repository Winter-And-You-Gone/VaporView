#include "ground/devices/EpsilonDeviceSession.h"

#include "TelemetryCodec.h"
#include "ground/devices/RemoteSkyController.h"

#include <QMetaObject>
#include <QPointer>

#include <utility>

namespace VaporView::Ground::Devices
{

namespace
{

CommandErrorCode localResultErrorCode(const VaporView::Ground::EpsilonConfigurationResult& result)
{
    if (result.succeeded())
    {
        return CommandErrorCode::Ok;
    }
    return result.command_succeeded ? CommandErrorCode::InternalError
                                    : CommandErrorCode::ConfigApplyFailed;
}

}  // namespace

EpsilonDeviceSession::EpsilonDeviceSession(LocalAdapter localAdapter,
                                           RemoteSkyController *remoteController,
                                           QObject *parent)
    : QObject(parent)
    , local_adapter_(std::move(localAdapter))
    , remote_controller_(remoteController)
    , local_worker_(new QObject)
{
    local_worker_->moveToThread(&local_worker_thread_);
    local_worker_thread_.setObjectName(QStringLiteral("EpsilonLocalDeviceWorker"));
    local_worker_thread_.start();

    if (!remote_controller_)
    {
        return;
    }

    connect(remote_controller_, &RemoteSkyController::deviceOperationResponseReceived,
            this, [this](const DeviceOperationResponse& response) {
                const quint64 sessionRequestId = remote_request_to_session_.take(response.request_id);
                const auto pendingIt = pending_operations_.find(sessionRequestId);
                if (sessionRequestId == 0 || pendingIt == pending_operations_.end())
                {
                    return;
                }
                const PendingOperation pending = pendingIt.value();
                pending_operations_.erase(pendingIt);
                if (pending.backend != EpsilonBackend::Remote ||
                    pending.session_generation != session_generation_ ||
                    pending.link_generation != remote_controller_->linkGeneration())
                {
                    return;
                }

                bool ok = response.error_code == CommandErrorCode::Ok;
                auto error = response.error_code;
                VaporView::Ground::EpsilonConfigurationResult localResult;
                DeviceOperation expected = DeviceOperation::ConfigureEpsilonPacketRates;
                switch (pending.operation)
                {
                case EpsilonOperation::ConfigurePacketRates: break;
                case EpsilonOperation::ConfigureMainAntennaLeverArm: expected = DeviceOperation::ConfigureEpsilonMainAntennaLeverArm; break;
                case EpsilonOperation::ConfigureRtcmInput: expected = DeviceOperation::ConfigureEpsilonRtcmInput; break;
                case EpsilonOperation::ReadSettings: expected = DeviceOperation::ReadEpsilonSettings; break;
                case EpsilonOperation::ApplySettings: expected = DeviceOperation::ApplyEpsilonSettings; break;
                case EpsilonOperation::RestartDevice: expected = DeviceOperation::RestartEpsilonDevice; break;
                }
                if (response.device_id != SkyDeviceId::Epsilon || response.operation != expected)
                    { ok = false; error = CommandErrorCode::InvalidPayload; }
                if (pending.operation == EpsilonOperation::ReadSettings || pending.operation == EpsilonOperation::ApplySettings)
                {
                    const bool parsed = TelemetryCodec::parseEpsilonSettingsSnapshot(response.payload, localResult.settings_snapshot);
                    if (ok && (!parsed || localResult.settings_snapshot.group != pending.settings.group ||
                        localResult.settings_snapshot.values.empty())) { ok = false; error = CommandErrorCode::InvalidPayload; }
                    if (ok && pending.operation == EpsilonOperation::ApplySettings)
                    {
                        if ((!localResult.settings_snapshot.saved && localResult.settings_snapshot.restart_required) ||
                            !localResult.settings_snapshot.readback_verified)
                            { ok = false; error = CommandErrorCode::ConfigApplyFailed; }
                        for (const auto& value : pending.settings.values)
                        {
                            const auto it = localResult.settings_snapshot.values.find(value.first);
                            if (it == localResult.settings_snapshot.values.end() || !epsilonSettingsValuesEqual(it->second, value.second))
                                { ok = false; error = CommandErrorCode::ConfigApplyFailed; }
                        }
                    }
                }
                localResult.command_succeeded = ok;
                localResult.live_stream_restarted = ok;
                finishPending(pending,
                              ok ? EpsilonOperationOutcome::Success :
                                  (error == CommandErrorCode::UnknownCommand ? EpsilonOperationOutcome::Unsupported : EpsilonOperationOutcome::Failed),
                              error,
                              ok ? QString() :
                                   (!response.error_message.isEmpty()
                                        ? response.error_message
                                        : commandErrorCodeText(error, english_)), localResult);
            });
    connect(remote_controller_, &RemoteSkyController::deviceOperationRejected,
            this, [this](quint32 remoteRequestId, const CommandAck& ack) {
                const quint64 sessionRequestId = remote_request_to_session_.take(remoteRequestId);
                const auto pendingIt = pending_operations_.find(sessionRequestId);
                if (sessionRequestId == 0 || pendingIt == pending_operations_.end())
                {
                    return;
                }
                const PendingOperation pending = pendingIt.value();
                pending_operations_.erase(pendingIt);
                if (pending.session_generation != session_generation_)
                {
                    return;
                }
                const bool unsupported = ack.error_code == CommandErrorCode::UnknownCommand;
                finishPending(
                    pending,
                    unsupported ? EpsilonOperationOutcome::Unsupported
                                : EpsilonOperationOutcome::Failed,
                    ack.error_code,
                    unsupported
                        ? (english_ ? QStringLiteral("This Sky version does not support EPSILON detailed operations.")
                                    : QStringLiteral("当前天空端版本不支持 EPSILON 详细配置操作。"))
                        : (english_ ? QStringLiteral("Remote Sky rejected the EPSILON request: %1")
                                          .arg(commandErrorCodeText(ack.error_code, true))
                                    : QStringLiteral("天空端拒绝 EPSILON 请求：%1")
                                          .arg(commandErrorCodeText(ack.error_code, false))));
            });
    connect(remote_controller_, &RemoteSkyController::deviceOperationTimedOut,
            this, [this](quint32 remoteRequestId) {
                const quint64 sessionRequestId = remote_request_to_session_.take(remoteRequestId);
                const auto pendingIt = pending_operations_.find(sessionRequestId);
                if (sessionRequestId == 0 || pendingIt == pending_operations_.end())
                {
                    return;
                }
                const PendingOperation pending = pendingIt.value();
                pending_operations_.erase(pendingIt);
                if (pending.session_generation != session_generation_)
                {
                    return;
                }
                finishPending(pending,
                              EpsilonOperationOutcome::Timeout,
                              CommandErrorCode::InternalError,
                              english_ ? QStringLiteral("Remote Sky EPSILON request timed out.")
                                       : QStringLiteral("天空端 EPSILON 请求超时。"));
            });
    connect(remote_controller_, &RemoteSkyController::deviceOperationSupportChanged,
            this, [this](DeviceOperationSupport support) {
                if (support == DeviceOperationSupport::Unsupported)
                {
                    failActive(EpsilonOperationOutcome::Unsupported,
                               english_ ? QStringLiteral("This Sky version does not support EPSILON detailed operations.")
                                        : QStringLiteral("当前天空端版本不支持 EPSILON 详细配置操作。"));
                }
                refreshAvailability();
            });
    connect(remote_controller_, &RemoteSkyController::linkOpenChanged,
            this, [this](bool open) {
                if (!open)
                {
                    remote_available_ = false;
                    failActive(EpsilonOperationOutcome::Disconnected,
                               english_ ? QStringLiteral("Remote Sky disconnected during the EPSILON operation.")
                                        : QStringLiteral("EPSILON 操作期间天空端链路已断开。"));
                }
                refreshAvailability();
            });
}

EpsilonDeviceSession::~EpsilonDeviceSession()
{
    ++session_generation_;
    pending_operations_.clear();
    remote_request_to_session_.clear();
    local_worker_thread_.quit();
    local_worker_thread_.wait();
    delete local_worker_;
}

void EpsilonDeviceSession::setEnglish(bool english)
{
    english_ = english;
    refreshAvailability();
}

void EpsilonDeviceSession::setBackend(EpsilonBackend backend)
{
    if (backend_ == backend)
    {
        refreshAvailability();
        return;
    }
    failActive(EpsilonOperationOutcome::Disconnected,
               english_ ? QStringLiteral("The EPSILON backend changed before the operation completed.")
                        : QStringLiteral("EPSILON 后端已切换，未完成的操作已取消。"));
    ++session_generation_;
    backend_ = backend;
    refreshAvailability();
}

EpsilonBackend EpsilonDeviceSession::backend() const
{
    return backend_;
}

void EpsilonDeviceSession::setLocalAvailable(bool available, const QString& detail)
{
    if (local_available_ == available && local_detail_ == detail)
    {
        return;
    }
    local_available_ = available;
    local_detail_ = detail;
    if (!available && backend_ == EpsilonBackend::Local)
    {
        failActive(EpsilonOperationOutcome::Disconnected,
                   english_ ? QStringLiteral("Local EPSILON disconnected during the operation.")
                            : QStringLiteral("操作期间本地 EPSILON 已断开。"));
    }
    refreshAvailability();
}

void EpsilonDeviceSession::setRemoteAvailable(bool available, const QString& detail)
{
    if (remote_available_ == available && remote_detail_ == detail)
    {
        return;
    }
    remote_available_ = available;
    remote_detail_ = detail;
    if (!available && backend_ == EpsilonBackend::Remote &&
        !(operationPending() && remote_controller_ && remote_controller_->isOpen()))
    {
        failActive(EpsilonOperationOutcome::Disconnected,
                   english_ ? QStringLiteral("Remote Sky EPSILON is disconnected or stale.")
                            : QStringLiteral("天空端 EPSILON 已断开或数据已过期。"));
    }
    refreshAvailability();
}

bool EpsilonDeviceSession::operationsAvailable() const
{
    if (local_worker_busy_ || !pending_operations_.isEmpty())
    {
        return false;
    }
    if (backend_ == EpsilonBackend::Local)
    {
        return local_available_ &&
               local_adapter_.configurePacketRates &&
               local_adapter_.configureMainAntennaLeverArm &&
               local_adapter_.configureRtcmInput;
    }
    return remote_available_ && remote_controller_ && remote_controller_->isOpen() &&
           remote_controller_->deviceOperationSupport() != DeviceOperationSupport::Unsupported;
}

bool EpsilonDeviceSession::operationPending() const
{
    return local_worker_busy_ || !pending_operations_.isEmpty();
}

quint64 EpsilonDeviceSession::configurePacketRates(
    const EpsilonPacketRatesOperation& operation,
    const VaporView::Ground::EpsilonDeviceOperation& localDeviceOperation)
{
    PendingOperation pending;
    pending.operation = EpsilonOperation::ConfigurePacketRates;
    pending.packet_rates = operation;
    pending.local_device_operation = localDeviceOperation;
    return beginOperation(pending);
}

quint64 EpsilonDeviceSession::configureMainAntennaLeverArm(
    const EpsilonMainAntennaLeverArmOperation& operation,
    const VaporView::Ground::EpsilonDeviceOperation& localDeviceOperation)
{
    PendingOperation pending;
    pending.operation = EpsilonOperation::ConfigureMainAntennaLeverArm;
    pending.lever_arm = operation;
    pending.local_device_operation = localDeviceOperation;
    return beginOperation(pending);
}

quint64 EpsilonDeviceSession::configureRtcmInput(
    const EpsilonRtcmInputOperation& operation,
    const VaporView::Ground::EpsilonDeviceOperation& localDeviceOperation)
{
    PendingOperation pending;
    pending.operation = EpsilonOperation::ConfigureRtcmInput;
    pending.rtcm_input = operation;
    pending.local_device_operation = localDeviceOperation;
    return beginOperation(pending);
}

quint64 EpsilonDeviceSession::readSettings(EpsilonSettingsGroup group,
    const VaporView::Ground::EpsilonDeviceOperation& localDeviceOperation)
{
    PendingOperation pending;
    pending.operation = EpsilonOperation::ReadSettings;
    pending.settings.group = group;
    pending.local_device_operation = localDeviceOperation;
    return beginOperation(pending);
}

quint64 EpsilonDeviceSession::applySettings(const EpsilonSettingsOperation& operation,
    const VaporView::Ground::EpsilonDeviceOperation& localDeviceOperation)
{
    PendingOperation pending;
    pending.operation = EpsilonOperation::ApplySettings;
    pending.settings = operation;
    pending.local_device_operation = localDeviceOperation;
    return beginOperation(pending);
}

quint64 EpsilonDeviceSession::restartDevice(const VaporView::Ground::EpsilonDeviceOperation& localDeviceOperation)
{
    PendingOperation pending;
    pending.operation = EpsilonOperation::RestartDevice;
    pending.local_device_operation = localDeviceOperation;
    return beginOperation(pending);
}

quint64 EpsilonDeviceSession::beginOperation(PendingOperation pending)
{
    const quint64 requestId = next_request_id_++;
    pending.request_id = requestId;
    pending.session_generation = session_generation_;
    pending.backend = backend_;

    const bool settingsOperation = pending.operation == EpsilonOperation::ReadSettings ||
        pending.operation == EpsilonOperation::ApplySettings || pending.operation == EpsilonOperation::RestartDevice;
    const bool settingsUnsupported = settingsOperation && backend_ == EpsilonBackend::Remote && remote_controller_ &&
        (remote_controller_->epsilonSettingsSupport() == DeviceOperationSupport::Unsupported ||
         (pending.operation != EpsilonOperation::ReadSettings && remote_controller_->epsilonSettingsSupport() != DeviceOperationSupport::Supported));
    const bool localCallbackMissing = backend_ == EpsilonBackend::Local && settingsOperation &&
        ((pending.operation == EpsilonOperation::ReadSettings && !local_adapter_.readSettings) ||
         (pending.operation == EpsilonOperation::ApplySettings && !local_adapter_.applySettings) ||
         (pending.operation == EpsilonOperation::RestartDevice && !local_adapter_.restartDevice));
    if (!operationsAvailable() || settingsUnsupported || localCallbackMissing)
    {
        EpsilonSessionResult result;
        result.request_id = requestId;
        result.operation = pending.operation;
        result.outcome = settingsUnsupported || localCallbackMissing || (backend_ == EpsilonBackend::Remote && remote_controller_ &&
                                 remote_controller_->deviceOperationSupport() ==
                                     DeviceOperationSupport::Unsupported)
            ? EpsilonOperationOutcome::Unsupported
            : EpsilonOperationOutcome::Disconnected;
        result.error_code = result.outcome == EpsilonOperationOutcome::Unsupported
            ? CommandErrorCode::UnknownCommand
            : CommandErrorCode::DeviceNotConnected;
        result.message = settingsUnsupported ?
            (english_ ? QStringLiteral("Read EPSILON settings first to verify Sky support; this Sky version may not support settings.") :
                        QStringLiteral("请先读取 EPSILON 参数确认天空端能力；当前版本可能不支持参数操作。")) : unavailableReason();
        emit operationFinished(result);
        return requestId;
    }

    pending_operations_.insert(requestId, pending);
    emit operationStarted(requestId, pending.operation);
    if (backend_ == EpsilonBackend::Local)
    {
        dispatchLocal(pending);
    }
    else
    {
        dispatchRemote(pending);
    }
    refreshAvailability();
    return requestId;
}

void EpsilonDeviceSession::dispatchLocal(PendingOperation pending)
{
    local_worker_busy_ = true;
    const LocalAdapter adapter = local_adapter_;
    QPointer<EpsilonDeviceSession> guard(this);
    QMetaObject::invokeMethod(local_worker_, [guard, adapter, pending]() {
        VaporView::Ground::EpsilonConfigurationResult localResult;
        switch (pending.operation)
        {
        case EpsilonOperation::ConfigurePacketRates:
            if (adapter.configurePacketRates)
            {
                localResult = adapter.configurePacketRates(
                    pending.packet_rates, pending.local_device_operation);
            }
            break;
        case EpsilonOperation::ConfigureMainAntennaLeverArm:
            if (adapter.configureMainAntennaLeverArm)
            {
                localResult = adapter.configureMainAntennaLeverArm(
                    pending.lever_arm, pending.local_device_operation);
            }
            break;
        case EpsilonOperation::ConfigureRtcmInput:
            if (adapter.configureRtcmInput)
            {
                localResult = adapter.configureRtcmInput(
                    pending.rtcm_input, pending.local_device_operation);
            }
            break;
        case EpsilonOperation::ReadSettings:
            if (adapter.readSettings) localResult = adapter.readSettings(pending.settings.group, pending.local_device_operation);
            break;
        case EpsilonOperation::ApplySettings:
            if (adapter.applySettings) localResult = adapter.applySettings(pending.settings, pending.local_device_operation);
            break;
        case EpsilonOperation::RestartDevice:
            if (adapter.restartDevice) localResult = adapter.restartDevice(pending.local_device_operation);
            break;
        }
        if (!guard)
        {
            return;
        }
        QMetaObject::invokeMethod(guard, [guard, pending, localResult]() {
            if (!guard)
            {
                return;
            }
            guard->local_worker_busy_ = false;
            guard->refreshAvailability();
            const auto pendingIt = guard->pending_operations_.find(pending.request_id);
            if (pendingIt == guard->pending_operations_.end())
            {
                return;
            }
            guard->pending_operations_.erase(pendingIt);
            if (pending.session_generation != guard->session_generation_ ||
                guard->backend_ != EpsilonBackend::Local)
            {
                return;
            }
            const bool ok = localResult.succeeded();
            guard->finishPending(
                pending,
                ok ? EpsilonOperationOutcome::Success : EpsilonOperationOutcome::Failed,
                localResultErrorCode(localResult),
                localResult.error_message,
                localResult);
        }, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
}

void EpsilonDeviceSession::dispatchRemote(PendingOperation pending)
{
    pending.link_generation = remote_controller_->linkGeneration();
    quint32 remoteRequestId = 0;
    switch (pending.operation)
    {
    case EpsilonOperation::ConfigurePacketRates:
        remoteRequestId = remote_controller_->configureEpsilonPacketRates(pending.packet_rates);
        break;
    case EpsilonOperation::ConfigureMainAntennaLeverArm:
        remoteRequestId = remote_controller_->configureEpsilonMainAntennaLeverArm(pending.lever_arm);
        break;
    case EpsilonOperation::ConfigureRtcmInput:
        remoteRequestId = remote_controller_->configureEpsilonRtcmInput(pending.rtcm_input);
        break;
    case EpsilonOperation::ReadSettings:
        remoteRequestId = remote_controller_->readEpsilonSettings(pending.settings.group);
        break;
    case EpsilonOperation::ApplySettings:
        remoteRequestId = remote_controller_->applyEpsilonSettings(pending.settings);
        break;
    case EpsilonOperation::RestartDevice:
        remoteRequestId = remote_controller_->restartEpsilonDevice();
        break;
    }
    if (remoteRequestId == 0)
    {
        pending_operations_.remove(pending.request_id);
        finishPending(pending,
                      remote_controller_->deviceOperationSupport() ==
                              DeviceOperationSupport::Unsupported
                          ? EpsilonOperationOutcome::Unsupported
                          : EpsilonOperationOutcome::Disconnected,
                      remote_controller_->deviceOperationSupport() ==
                              DeviceOperationSupport::Unsupported
                          ? CommandErrorCode::UnknownCommand
                          : CommandErrorCode::DeviceNotConnected,
                      unavailableReason());
        return;
    }
    pending.remote_request_id = remoteRequestId;
    pending_operations_[pending.request_id] = pending;
    remote_request_to_session_.insert(remoteRequestId, pending.request_id);
}

void EpsilonDeviceSession::finishPending(
    const PendingOperation& pending,
    EpsilonOperationOutcome outcome,
    CommandErrorCode errorCode,
    const QString& message,
    const VaporView::Ground::EpsilonConfigurationResult& localResult)
{
    if (outcome == EpsilonOperationOutcome::Success && pending.backend == EpsilonBackend::Remote &&
        (pending.operation == EpsilonOperation::ReadSettings || pending.operation == EpsilonOperation::ApplySettings ||
         pending.operation == EpsilonOperation::RestartDevice) && remote_controller_ && remote_controller_->isOpen())
        remote_available_ = true;
    EpsilonSessionResult result;
    result.request_id = pending.request_id;
    result.operation = pending.operation;
    result.outcome = outcome;
    result.error_code = errorCode;
    result.message = message;
    result.local_result = localResult;
    emit operationFinished(result);
    refreshAvailability();
}

void EpsilonDeviceSession::failActive(EpsilonOperationOutcome outcome, const QString& message)
{
    const auto pending = pending_operations_.values();
    pending_operations_.clear();
    remote_request_to_session_.clear();
    for (const PendingOperation& operation : pending)
    {
        finishPending(operation,
                      outcome,
                      outcome == EpsilonOperationOutcome::Unsupported
                          ? CommandErrorCode::UnknownCommand
                          : CommandErrorCode::DeviceNotConnected,
                      message);
    }
}

void EpsilonDeviceSession::refreshAvailability()
{
    emit availabilityChanged(operationsAvailable(), unavailableReason());
}

QString EpsilonDeviceSession::unavailableReason() const
{
    if (!pending_operations_.isEmpty())
    {
        return english_ ? QStringLiteral("Waiting for the EPSILON operation to complete.")
                        : QStringLiteral("正在等待 EPSILON 操作完成。");
    }
    if (backend_ == EpsilonBackend::Local)
    {
        if (!local_detail_.isEmpty())
        {
            return local_detail_;
        }
        return english_ ? QStringLiteral("Local EPSILON is not connected.")
                        : QStringLiteral("本地 EPSILON 尚未连接。");
    }
    if (remote_controller_ &&
        remote_controller_->deviceOperationSupport() == DeviceOperationSupport::Unsupported)
    {
        return english_ ? QStringLiteral("This Sky version does not support EPSILON detailed operations.")
                        : QStringLiteral("当前天空端版本不支持 EPSILON 详细配置操作。");
    }
    if (!remote_detail_.isEmpty())
    {
        return remote_detail_;
    }
    return english_ ? QStringLiteral("Remote Sky EPSILON is disconnected or stale.")
                    : QStringLiteral("天空端 EPSILON 未连接或数据已过期。");
}

}  // namespace VaporView::Ground::Devices
