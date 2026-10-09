#include "ground/devices/EpsilonDeviceSession.h"

#include "TelemetryCodec.h"
#include "ground/devices/RemoteSkyController.h"

#include <QMetaObject>
#include <QPointer>

#include <utility>
#include <cmath>

namespace VaporView::Ground::Devices
{

namespace
{

bool validMagneticCompletion(const EpsilonMaintenanceResult& result)
{
    if (result.status != EpsilonMaintenanceStatus::Completed || !result.saved || !result.restart_required) return false;
    if (result.action == EpsilonMaintenanceAction::Magnetic2D)
        return result.progress_known && result.progress_percent == 100;
    return result.action == EpsilonMaintenanceAction::Magnetic3D && result.fit_error_known &&
        std::isfinite(result.fit_error) && result.fit_error >= 0 && result.fit_error < 3 && result.algorithm == "High";
}

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
                const quint64 sessionRequestId = remote_request_to_session_.value(response.request_id);
                const auto pendingIt = pending_operations_.find(sessionRequestId);
                if (sessionRequestId == 0 || pendingIt == pending_operations_.end())
                {
                    return;
                }
                const PendingOperation pending = pendingIt.value();
                if (pending.backend != EpsilonBackend::Remote ||
                    pending.session_generation != session_generation_ ||
                    pending.link_generation != remote_controller_->linkGeneration())
                {
                    return;
                }

                bool ok = response.error_code == CommandErrorCode::Ok;
                const bool magneticOperation = pending.operation == EpsilonOperation::CalibrateMagnetic2D || pending.operation == EpsilonOperation::CalibrateMagnetic3D;
                if (magneticOperation)
                {
                    EpsilonMaintenanceResult progress;
                    if (response.device_id == SkyDeviceId::Epsilon &&
                        response.operation == (pending.operation == EpsilonOperation::CalibrateMagnetic2D ? DeviceOperation::CalibrateEpsilonMagnetic2D : DeviceOperation::CalibrateEpsilonMagnetic3D) &&
                        TelemetryCodec::parseEpsilonMaintenanceResult(response.payload, progress) &&
                        progress.action == pending.maintenance_action && progress.status == EpsilonMaintenanceStatus::Running)
                    {
                        if (ok)
                        {
                            pendingIt->last_maintenance_result = progress;
                            emit maintenanceProgress(progress);
                        }
                        return;
                    }
                }
                remote_request_to_session_.remove(response.request_id);
                pending_operations_.erase(pendingIt);
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
                case EpsilonOperation::CalibrateLevel: expected = DeviceOperation::CalibrateEpsilonLevel; break;
                case EpsilonOperation::CalibrateAccelerometer: expected = DeviceOperation::CalibrateEpsilonAccelerometer; break;
                case EpsilonOperation::CalibrateGyroscope: expected = DeviceOperation::CalibrateEpsilonGyroscope; break;
                case EpsilonOperation::ReadDgnss: expected = DeviceOperation::ReadEpsilonDgnss; break;
                case EpsilonOperation::ApplyDgnss: expected = DeviceOperation::ApplyEpsilonDgnss; break;
                case EpsilonOperation::CalibrateMagnetic2D: expected = DeviceOperation::CalibrateEpsilonMagnetic2D; break;
                case EpsilonOperation::CalibrateMagnetic3D: expected = DeviceOperation::CalibrateEpsilonMagnetic3D; break;
                }
                if (response.device_id != SkyDeviceId::Epsilon || response.operation != expected)
                    { ok = false; error = CommandErrorCode::InvalidPayload; }
                const bool calibrationOperation = pending.operation == EpsilonOperation::CalibrateLevel ||
                    pending.operation == EpsilonOperation::CalibrateAccelerometer || pending.operation == EpsilonOperation::CalibrateGyroscope || magneticOperation;
                if (calibrationOperation)
                {
                    EpsilonMaintenanceResult parsedResult;
                    const bool parsed = TelemetryCodec::parseEpsilonMaintenanceResult(response.payload, parsedResult);
                    if (parsed && parsedResult.action == pending.maintenance_action)
                        localResult.maintenance_result = parsedResult;
                    else
                    {
                        localResult.maintenance_result.action = pending.maintenance_action;
                        // Failed responses must not attach another action's restart state.
                        if (ok || !response.payload.isEmpty()) { ok = false; error = CommandErrorCode::InvalidPayload; }
                    }
                    if (ok && (!parsed || parsedResult.action != pending.maintenance_action ||
                        (magneticOperation ? !validMagneticCompletion(localResult.maintenance_result) :
                         (localResult.maintenance_result.status != EpsilonMaintenanceStatus::Acknowledged &&
                          localResult.maintenance_result.status != EpsilonMaintenanceStatus::SentUnverified)) ||
                        !localResult.maintenance_result.saved || !localResult.maintenance_result.restart_required))
                    { ok = false; error = localResult.maintenance_result.status == EpsilonMaintenanceStatus::Unsupported ? CommandErrorCode::UnknownCommand : CommandErrorCode::InvalidPayload; }
                    localResult.command_succeeded = ok;
                    localResult.live_stream_restarted = ok;
                }
                if (pending.operation == EpsilonOperation::ReadSettings || pending.operation == EpsilonOperation::ApplySettings)
                {
                    EpsilonSettingsSnapshot parsedSnapshot;
                    const bool parsed = TelemetryCodec::parseEpsilonSettingsSnapshot(response.payload, parsedSnapshot);
                    if (parsed && parsedSnapshot.group == pending.settings.group)
                        localResult.settings_snapshot = std::move(parsedSnapshot);
                    else
                        localResult.settings_snapshot.group = pending.settings.group;
                    if (ok && (!parsed || parsedSnapshot.group != pending.settings.group ||
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
                if (pending.operation == EpsilonOperation::ReadDgnss || pending.operation == EpsilonOperation::ApplyDgnss)
                {
                    const bool parsed = TelemetryCodec::parseEpsilonDgnssSnapshot(response.payload, localResult.dgnss_snapshot);
                    if (ok && (!parsed || localResult.dgnss_snapshot.values.empty())) { ok = false; error = CommandErrorCode::InvalidPayload; }
                    if (ok && pending.operation == EpsilonOperation::ApplyDgnss)
                    {
                        if (!localResult.dgnss_snapshot.readback_verified ||
                            (localResult.dgnss_snapshot.restart_required && !localResult.dgnss_snapshot.saved))
                            { ok = false; error = CommandErrorCode::ConfigApplyFailed; }
                        for (const auto& value : pending.dgnss.values)
                            if (!localResult.dgnss_snapshot.values.count(value.first) || localResult.dgnss_snapshot.values.at(value.first) != value.second)
                                { ok = false; error = CommandErrorCode::ConfigApplyFailed; }
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
                if (pending.operation == EpsilonOperation::CalibrateMagnetic2D || pending.operation == EpsilonOperation::CalibrateMagnetic3D)
                    remote_controller_->cancelEpsilonMagneticCalibration(pending.remote_request_id);
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
    cancelMaintenance();
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

quint64 EpsilonDeviceSession::calibrate(EpsilonMaintenanceAction action,
    const VaporView::Ground::EpsilonDeviceOperation& localDeviceOperation)
{
    PendingOperation pending;
    pending.operation = action == EpsilonMaintenanceAction::Level ? EpsilonOperation::CalibrateLevel :
        action == EpsilonMaintenanceAction::Accelerometer ? EpsilonOperation::CalibrateAccelerometer :
        action == EpsilonMaintenanceAction::Gyroscope ? EpsilonOperation::CalibrateGyroscope :
        action == EpsilonMaintenanceAction::Magnetic2D ? EpsilonOperation::CalibrateMagnetic2D : EpsilonOperation::CalibrateMagnetic3D;
    pending.maintenance_action = action;
    pending.local_device_operation = localDeviceOperation;
    return beginOperation(pending);
}

quint64 EpsilonDeviceSession::readDgnss(const VaporView::Ground::EpsilonDeviceOperation& operation)
{
    PendingOperation pending;
    pending.operation = EpsilonOperation::ReadDgnss;
    pending.local_device_operation = operation;
    return beginOperation(pending);
}

quint64 EpsilonDeviceSession::applyDgnss(const EpsilonDgnssOperation& settings, const VaporView::Ground::EpsilonDeviceOperation& operation)
{
    PendingOperation pending;
    pending.operation = EpsilonOperation::ApplyDgnss;
    pending.dgnss = settings;
    pending.local_device_operation = operation;
    return beginOperation(pending);
}

void EpsilonDeviceSession::cancelMaintenance()
{
    if (local_maintenance_cancel_) local_maintenance_cancel_->store(true);
    for (const auto& pending : pending_operations_)
        if (pending.backend == EpsilonBackend::Remote && remote_controller_ && remote_controller_->isOpen() &&
            (pending.operation == EpsilonOperation::CalibrateMagnetic2D || pending.operation == EpsilonOperation::CalibrateMagnetic3D))
            remote_controller_->cancelEpsilonMagneticCalibration(pending.remote_request_id);
}

quint64 EpsilonDeviceSession::beginOperation(PendingOperation pending)
{
    const quint64 requestId = next_request_id_++;
    pending.request_id = requestId;
    pending.session_generation = session_generation_;
    pending.backend = backend_;

    const bool settingsOperation = pending.operation == EpsilonOperation::ReadSettings ||
        pending.operation == EpsilonOperation::ApplySettings || pending.operation == EpsilonOperation::RestartDevice;
    const bool maintenanceOperation = pending.operation == EpsilonOperation::CalibrateLevel ||
        pending.operation == EpsilonOperation::CalibrateAccelerometer || pending.operation == EpsilonOperation::CalibrateGyroscope ||
        pending.operation == EpsilonOperation::CalibrateMagnetic2D || pending.operation == EpsilonOperation::CalibrateMagnetic3D;
    const bool settingsUnsupported = settingsOperation && backend_ == EpsilonBackend::Remote && remote_controller_ &&
        (remote_controller_->epsilonSettingsSupport() == DeviceOperationSupport::Unsupported ||
         (pending.operation != EpsilonOperation::ReadSettings && remote_controller_->epsilonSettingsSupport() != DeviceOperationSupport::Supported));
    const bool localCallbackMissing = backend_ == EpsilonBackend::Local &&
        ((maintenanceOperation && !local_adapter_.calibrate) ||
         (pending.operation == EpsilonOperation::ReadSettings && !local_adapter_.readSettings) ||
         (pending.operation == EpsilonOperation::ApplySettings && !local_adapter_.applySettings) ||
         (pending.operation == EpsilonOperation::ReadDgnss && !local_adapter_.readDgnss) ||
         (pending.operation == EpsilonOperation::ApplyDgnss && !local_adapter_.applyDgnss) ||
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
    if (pending.operation == EpsilonOperation::CalibrateMagnetic2D || pending.operation == EpsilonOperation::CalibrateMagnetic3D)
    {
        local_maintenance_cancel_ = std::make_shared<std::atomic_bool>(false);
        pending.local_device_operation.maintenance_cancel = local_maintenance_cancel_;
        QPointer<EpsilonDeviceSession> progressGuard(this);
        pending.local_device_operation.maintenance_progress = [progressGuard, id = pending.request_id, generation = pending.session_generation](const EpsilonMaintenanceResult& progress) {
            if (!progressGuard) return;
            QMetaObject::invokeMethod(progressGuard, [progressGuard, id, generation, progress]() {
                if (progressGuard && generation == progressGuard->session_generation_ && progressGuard->pending_operations_.contains(id))
                {
                    progressGuard->pending_operations_[id].last_maintenance_result = progress;
                    emit progressGuard->maintenanceProgress(progress);
                }
            }, Qt::QueuedConnection);
        };
    }
    local_worker_busy_ = true;
    const LocalAdapter adapter = local_adapter_;
    QPointer<EpsilonDeviceSession> guard(this);
    QMetaObject::invokeMethod(local_worker_, [guard, adapter, pending]() {
        VaporView::Ground::EpsilonConfigurationResult localResult;
        try
        {
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
        case EpsilonOperation::ReadDgnss:
            if (adapter.readDgnss) localResult = adapter.readDgnss(pending.local_device_operation);
            break;
        case EpsilonOperation::ApplyDgnss:
            if (adapter.applyDgnss) localResult = adapter.applyDgnss(pending.dgnss, pending.local_device_operation);
            break;
        case EpsilonOperation::CalibrateLevel:
        case EpsilonOperation::CalibrateAccelerometer:
        case EpsilonOperation::CalibrateGyroscope:
        case EpsilonOperation::CalibrateMagnetic2D:
        case EpsilonOperation::CalibrateMagnetic3D:
            if (adapter.calibrate) localResult = adapter.calibrate(pending.maintenance_action, pending.local_device_operation);
            if (localResult.command_succeeded &&
                (localResult.maintenance_result.action != pending.maintenance_action ||
                 ((pending.operation == EpsilonOperation::CalibrateMagnetic2D || pending.operation == EpsilonOperation::CalibrateMagnetic3D)
                    ? !validMagneticCompletion(localResult.maintenance_result)
                    : (localResult.maintenance_result.status != EpsilonMaintenanceStatus::Acknowledged &&
                       localResult.maintenance_result.status != EpsilonMaintenanceStatus::SentUnverified)) ||
                 !localResult.maintenance_result.saved || !localResult.maintenance_result.restart_required))
            {
                localResult.command_succeeded = false;
                localResult.error_message = QStringLiteral("EPSILON maintenance response did not acknowledge the requested action.");
            }
            break;
        }
        }
        catch (...)
        {
            localResult.error_message = QStringLiteral("EPSILON worker failed unexpectedly; reconnect and verify the device before retrying.");
            localResult.maintenance_result.action = pending.maintenance_action;
            localResult.maintenance_result.status = EpsilonMaintenanceStatus::Failed;
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
            guard->local_maintenance_cancel_.reset();
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
    case EpsilonOperation::ReadDgnss:
        remoteRequestId = remote_controller_->readEpsilonDgnss();
        break;
    case EpsilonOperation::ApplyDgnss:
        remoteRequestId = remote_controller_->applyEpsilonDgnss(pending.dgnss);
        break;
    case EpsilonOperation::CalibrateLevel:
    case EpsilonOperation::CalibrateAccelerometer:
    case EpsilonOperation::CalibrateGyroscope:
    case EpsilonOperation::CalibrateMagnetic2D:
    case EpsilonOperation::CalibrateMagnetic3D:
        remoteRequestId = remote_controller_->calibrateEpsilon(pending.maintenance_action);
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
         pending.operation == EpsilonOperation::RestartDevice || pending.operation == EpsilonOperation::CalibrateLevel ||
         pending.operation == EpsilonOperation::CalibrateAccelerometer || pending.operation == EpsilonOperation::CalibrateGyroscope ||
         pending.operation == EpsilonOperation::CalibrateMagnetic2D || pending.operation == EpsilonOperation::CalibrateMagnetic3D ||
         pending.operation == EpsilonOperation::ReadDgnss || pending.operation == EpsilonOperation::ApplyDgnss) &&
        remote_controller_ && remote_controller_->isOpen())
        remote_available_ = true;
    EpsilonSessionResult result;
    result.request_id = pending.request_id;
    result.operation = pending.operation;
    result.outcome = outcome;
    result.error_code = errorCode;
    result.message = message;
    result.local_result = localResult;
    if ((outcome == EpsilonOperationOutcome::Timeout || outcome == EpsilonOperationOutcome::Disconnected) &&
        (pending.operation == EpsilonOperation::CalibrateMagnetic2D || pending.operation == EpsilonOperation::CalibrateMagnetic3D))
    {
        result.local_result.maintenance_result = pending.last_maintenance_result;
        result.local_result.maintenance_result.action = pending.maintenance_action;
        result.local_result.maintenance_result.status = EpsilonMaintenanceStatus::Failed;
        result.message += english_ ? QStringLiteral(" Calibration outcome is unknown; reconnect and verify the device before retrying.")
                                   : QStringLiteral(" 校准结果未知，请重新连接并核对设备后再重试。");
    }
    result.maintenance_result = result.local_result.maintenance_result;
    emit operationFinished(result);
    refreshAvailability();
}

void EpsilonDeviceSession::failActive(EpsilonOperationOutcome outcome, const QString& message)
{
    cancelMaintenance();
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
