#include "ground/devices/Rd105DeviceSession.h"

#include "TelemetryCodec.h"
#include "ground/devices/RemoteSkyController.h"

#include <utility>

namespace VaporView::Ground::Devices
{

namespace
{

CommandErrorCode localErrorCode(LocalTemperatureCommandStatus status)
{
    switch (status)
    {
    case LocalTemperatureCommandStatus::Confirmed:
        return CommandErrorCode::Ok;
    case LocalTemperatureCommandStatus::Rejected:
        return CommandErrorCode::ConfigApplyFailed;
    case LocalTemperatureCommandStatus::NotConnected:
        return CommandErrorCode::DeviceNotConnected;
    }
    return CommandErrorCode::InternalError;
}

bool isRd105TemperatureCommand(CommandId command)
{
    switch (command)
    {
    case CommandId::SetTemperatureTarget:
    case CommandId::SetTemperatureOutputEnabled:
    case CommandId::SetTemperatureOutputMode:
    case CommandId::SetTemperatureMaxOutputPercent:
    case CommandId::SetTemperaturePid:
    case CommandId::SetTemperatureAutoPid:
    case CommandId::SetTemperatureControllerMode:
    case CommandId::SetTemperatureDeviceAddress:
    case CommandId::SetTemperatureRs485Baud:
    case CommandId::SetTemperatureOvertempOutputMode:
    case CommandId::RestoreTemperatureFactoryDefaults:
    case CommandId::SetTemperatureSensorConfig:
    case CommandId::SetTemperatureOvertempUpper:
    case CommandId::SetTemperatureOvertempLower:
    case CommandId::SetTemperatureSlope:
    case CommandId::SetTemperatureStartupDelay:
        return true;
    default:
        return false;
    }
}

}  // namespace

Rd105DeviceSession::Rd105DeviceSession(LocalAdapter localAdapter,
                                       RemoteSkyController *remoteController,
                                       QObject *parent)
    : QObject(parent)
    , local_adapter_(std::move(localAdapter))
    , remote_controller_(remoteController)
{
    if (!remote_controller_)
    {
        return;
    }

    connect(remote_controller_, &RemoteSkyController::commandAckReceived,
            this, [this](const CommandAck& ack) {
                if (!isRd105TemperatureCommand(ack.command_id))
                {
                    return;
                }
                const quint64 requestId = remote_sequence_to_request_.take(ack.command_seq);
                const auto pendingIt = pending_commands_.find(requestId);
                if (requestId == 0 || pendingIt == pending_commands_.end())
                {
                    return;
                }
                const PendingCommand pending = pendingIt.value();
                pending_commands_.erase(pendingIt);
                if (pending.backend != Rd105Backend::Remote ||
                    pending.session_generation != session_generation_ ||
                    pending.link_generation != remote_controller_->linkGeneration())
                {
                    return;
                }
                const bool ok = ack.result == 0 && ack.error_code == CommandErrorCode::Ok;
                finishPending(pending,
                              ok ? Rd105OperationOutcome::Success
                                 : Rd105OperationOutcome::Failed,
                              ack.error_code,
                              ok ? QString() : commandErrorCodeText(ack.error_code, english_));
            });
    connect(remote_controller_, &RemoteSkyController::commandTimedOut,
            this, [this](CommandId command, quint16 sequence) {
                if (!isRd105TemperatureCommand(command))
                {
                    return;
                }
                const quint64 requestId = remote_sequence_to_request_.take(sequence);
                const auto pendingIt = pending_commands_.find(requestId);
                if (requestId == 0 || pendingIt == pending_commands_.end())
                {
                    return;
                }
                const PendingCommand pending = pendingIt.value();
                pending_commands_.erase(pendingIt);
                if (pending.session_generation != session_generation_)
                {
                    return;
                }
                finishPending(pending,
                              Rd105OperationOutcome::Timeout,
                              CommandErrorCode::InternalError,
                              english_ ? QStringLiteral("Remote Sky RD105 request timed out.")
                                       : QStringLiteral("天空端 RD105 请求超时。"));
            });
    connect(remote_controller_, &RemoteSkyController::linkOpenChanged,
            this, [this](bool open) {
                if (!open)
                {
                    remote_available_ = false;
                    failActive(Rd105OperationOutcome::Disconnected,
                               english_ ? QStringLiteral("Remote Sky disconnected during the RD105 command.")
                                        : QStringLiteral("RD105 命令执行期间天空端链路已断开。"));
                }
                refreshAvailability();
            });
}

void Rd105DeviceSession::setEnglish(bool english)
{
    english_ = english;
    refreshAvailability();
}

void Rd105DeviceSession::setBackend(Rd105Backend backend)
{
    if (backend_ == backend)
    {
        refreshAvailability();
        return;
    }
    failActive(Rd105OperationOutcome::Disconnected,
               english_ ? QStringLiteral("The RD105 backend changed before the command completed.")
                        : QStringLiteral("RD105 后端已切换，未完成的命令已取消。"));
    ++session_generation_;
    backend_ = backend;
    refreshAvailability();
}

Rd105Backend Rd105DeviceSession::backend() const
{
    return backend_;
}

void Rd105DeviceSession::setLocalAvailable(bool available, const QString& detail)
{
    local_available_ = available;
    local_detail_ = detail;
    if (!available && backend_ == Rd105Backend::Local)
    {
        failActive(Rd105OperationOutcome::Disconnected,
                   english_ ? QStringLiteral("Local RD105 disconnected during the command.")
                            : QStringLiteral("命令执行期间本地 RD105 已断开。"));
    }
    refreshAvailability();
}

void Rd105DeviceSession::setRemoteAvailable(bool available, const QString& detail)
{
    remote_available_ = available;
    remote_detail_ = detail;
    if (!available && backend_ == Rd105Backend::Remote)
    {
        failActive(Rd105OperationOutcome::Disconnected,
                   english_ ? QStringLiteral("Remote Sky RD105 is disconnected or stale.")
                            : QStringLiteral("天空端 RD105 已断开或数据已过期。"));
    }
    refreshAvailability();
}

bool Rd105DeviceSession::operationsAvailable() const
{
    if (!pending_commands_.isEmpty())
    {
        return false;
    }
    if (backend_ == Rd105Backend::Local)
    {
        return local_available_ && local_adapter_.sendCommand;
    }
    return remote_available_ && remote_controller_ && remote_controller_->isOpen();
}

bool Rd105DeviceSession::operationPending() const
{
    return !pending_commands_.isEmpty();
}

quint64 Rd105DeviceSession::sendCommand(
    CommandId command,
    const TemperatureControllerCommand& payload)
{
    PendingCommand pending;
    pending.request_id = next_request_id_++;
    pending.session_generation = session_generation_;
    pending.backend = backend_;
    pending.command = command;
    pending.payload = payload;

    if (!operationsAvailable())
    {
        finishPending(pending,
                      Rd105OperationOutcome::Disconnected,
                      CommandErrorCode::DeviceNotConnected,
                      unavailableReason());
        return pending.request_id;
    }

    pending_commands_.insert(pending.request_id, pending);
    emit operationStarted(pending.request_id, command, payload);

    if (backend_ == Rd105Backend::Local)
    {
        LocalTemperatureCommandResult localResult =
            local_adapter_.sendCommand(command, payload);
        pending_commands_.remove(pending.request_id);
        finishPending(pending,
                      localResult.status == LocalTemperatureCommandStatus::Confirmed
                          ? Rd105OperationOutcome::Success
                          : localResult.status == LocalTemperatureCommandStatus::NotConnected
                              ? Rd105OperationOutcome::Disconnected
                              : Rd105OperationOutcome::Failed,
                      localErrorCode(localResult.status),
                      QString(),
                      localResult.latestData,
                      localResult.status != LocalTemperatureCommandStatus::NotConnected);
        return pending.request_id;
    }

    pending.link_generation = remote_controller_->linkGeneration();
    const quint16 sequence = remote_controller_->sendCommand(
        command, TelemetryCodec::serializeTemperatureControllerCommand(payload));
    if (sequence == 0)
    {
        pending_commands_.remove(pending.request_id);
        finishPending(pending,
                      Rd105OperationOutcome::Disconnected,
                      CommandErrorCode::DeviceNotConnected,
                      unavailableReason());
        return pending.request_id;
    }
    pending.remote_sequence = sequence;
    pending_commands_[pending.request_id] = pending;
    remote_sequence_to_request_.insert(sequence, pending.request_id);
    refreshAvailability();
    return pending.request_id;
}

void Rd105DeviceSession::finishPending(
    const PendingCommand& pending,
    Rd105OperationOutcome outcome,
    CommandErrorCode errorCode,
    const QString& message,
    const TemperatureControllerData& latestData,
    bool hasLatestData)
{
    Rd105SessionResult result;
    result.request_id = pending.request_id;
    result.command = pending.command;
    result.payload = pending.payload;
    result.backend = pending.backend;
    result.outcome = outcome;
    result.error_code = errorCode;
    result.message = message;
    result.latest_data = latestData;
    result.has_latest_data = hasLatestData;
    emit operationFinished(result);
    refreshAvailability();
}

void Rd105DeviceSession::failActive(Rd105OperationOutcome outcome, const QString& message)
{
    const auto pending = pending_commands_.values();
    pending_commands_.clear();
    remote_sequence_to_request_.clear();
    for (const PendingCommand& command : pending)
    {
        finishPending(command,
                      outcome,
                      outcome == Rd105OperationOutcome::Timeout
                          ? CommandErrorCode::InternalError
                          : CommandErrorCode::DeviceNotConnected,
                      message);
    }
}

void Rd105DeviceSession::refreshAvailability()
{
    emit availabilityChanged(operationsAvailable(), unavailableReason());
}

QString Rd105DeviceSession::unavailableReason() const
{
    if (!pending_commands_.isEmpty())
    {
        return english_ ? QStringLiteral("Waiting for the RD105 command to complete.")
                        : QStringLiteral("正在等待 RD105 命令完成。");
    }
    if (backend_ == Rd105Backend::Local)
    {
        if (!local_detail_.isEmpty()) return local_detail_;
        return english_ ? QStringLiteral("Local RD105 is not connected.")
                        : QStringLiteral("本地 RD105 尚未连接。");
    }
    if (!remote_detail_.isEmpty()) return remote_detail_;
    return english_ ? QStringLiteral("Remote Sky RD105 is disconnected or stale.")
                    : QStringLiteral("天空端 RD105 未连接或数据已过期。");
}

}  // namespace VaporView::Ground::Devices
