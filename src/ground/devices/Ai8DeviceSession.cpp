#include "ground/devices/Ai8DeviceSession.h"

#include "TelemetryCodec.h"
#include "ground/devices/RemoteSkyController.h"

#include <QMetaObject>
#include <QPointer>

#include <utility>

namespace VaporView::Ground::Devices
{

Ai8DeviceSession::Ai8DeviceSession(LocalAdapter localAdapter,
                                   RemoteSkyController *remoteController,
                                   QObject *parent)
    : QObject(parent)
    , local_adapter_(std::move(localAdapter))
    , remote_controller_(remoteController)
    , local_worker_(new QObject)
{
    local_worker_->moveToThread(&local_worker_thread_);
    local_worker_thread_.setObjectName(QStringLiteral("Ai8LocalDeviceWorker"));
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
                if (pending.backend != Ai8Backend::Remote ||
                    pending.session_generation != session_generation_ ||
                    pending.link_generation != remote_controller_->linkGeneration())
                {
                    return;
                }

                Ai8TemperatureControllerProtocol::PageData data;
                if (response.error_code == CommandErrorCode::Ok &&
                    TelemetryCodec::parseAi8PageData(response.payload, data))
                {
                    finishPending(pending, Ai8OperationOutcome::Success,
                                  CommandErrorCode::Ok, data, QString());
                    return;
                }
                const bool invalidPayload = response.error_code == CommandErrorCode::Ok;
                finishPending(
                    pending,
                    Ai8OperationOutcome::Failed,
                    invalidPayload ? CommandErrorCode::InvalidPayload : response.error_code,
                    {},
                    invalidPayload
                        ? (english_ ? QStringLiteral("Remote Sky returned an invalid AI-8 payload.")
                                    : QStringLiteral("天空端返回的 AI-8 参数载荷无效。"))
                        : commandErrorCodeText(response.error_code, english_));
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
                    unsupported ? Ai8OperationOutcome::Unsupported : Ai8OperationOutcome::Failed,
                    ack.error_code,
                    {},
                    unsupported
                        ? (english_ ? QStringLiteral("This Sky version does not support AI-8 parameter operations.")
                                    : QStringLiteral("当前天空端版本不支持 AI-8 参数操作。"))
                        : (english_ ? QStringLiteral("Remote Sky rejected the AI-8 request: %1")
                                          .arg(commandErrorCodeText(ack.error_code, true))
                                    : QStringLiteral("天空端拒绝 AI-8 请求：%1")
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
                finishPending(pending, Ai8OperationOutcome::Timeout,
                              CommandErrorCode::InternalError, {},
                              english_ ? QStringLiteral("Remote Sky AI-8 request timed out.")
                                       : QStringLiteral("天空端 AI-8 请求超时。"));
            });
    connect(remote_controller_, &RemoteSkyController::deviceOperationSupportChanged,
            this, [this](DeviceOperationSupport support) {
                if (support == DeviceOperationSupport::Unsupported)
                {
                    failActive(Ai8OperationOutcome::Unsupported,
                               english_ ? QStringLiteral("This Sky version does not support AI-8 parameter operations.")
                                        : QStringLiteral("当前天空端版本不支持 AI-8 参数操作。"));
                }
                refreshAvailability();
            });
    connect(remote_controller_, &RemoteSkyController::linkOpenChanged,
            this, [this](bool open) {
                if (!open)
                {
                    remote_available_ = false;
                    failActive(Ai8OperationOutcome::Disconnected,
                               english_ ? QStringLiteral("Remote Sky disconnected during the AI-8 operation.")
                                        : QStringLiteral("AI-8 操作期间天空端链路已断开。"));
                }
                refreshAvailability();
            });
}

Ai8DeviceSession::~Ai8DeviceSession()
{
    ++session_generation_;
    pending_operations_.clear();
    remote_request_to_session_.clear();
    local_worker_thread_.quit();
    local_worker_thread_.wait();
    delete local_worker_;
}

void Ai8DeviceSession::setEnglish(bool english)
{
    english_ = english;
    refreshAvailability();
}

void Ai8DeviceSession::setBackend(Ai8Backend backend)
{
    if (backend_ == backend)
    {
        refreshAvailability();
        return;
    }
    failActive(Ai8OperationOutcome::Disconnected,
               english_ ? QStringLiteral("The AI-8 backend changed before the operation completed.")
                        : QStringLiteral("AI-8 后端已切换，未完成的操作已取消。"));
    ++session_generation_;
    backend_ = backend;
    refreshAvailability();
    const auto cached = activeCache().constFind(active_page_key_);
    if (cached != activeCache().constEnd())
    {
        emit pageDataAvailable(cached.value());
    }
}

Ai8Backend Ai8DeviceSession::backend() const
{
    return backend_;
}

void Ai8DeviceSession::setLocalAvailable(bool available, const QString& detail)
{
    if (local_available_ == available && local_detail_ == detail)
    {
        return;
    }
    local_available_ = available;
    local_detail_ = detail;
    if (!available && backend_ == Ai8Backend::Local)
    {
        failActive(Ai8OperationOutcome::Disconnected,
                   english_ ? QStringLiteral("Local AI-8 disconnected during the operation.")
                            : QStringLiteral("操作期间本地 AI-8 已断开。"));
    }
    refreshAvailability();
}

void Ai8DeviceSession::setRemoteAvailable(bool available, const QString& detail)
{
    if (remote_available_ == available && remote_detail_ == detail)
    {
        return;
    }
    remote_available_ = available;
    remote_detail_ = detail;
    if (!available && backend_ == Ai8Backend::Remote)
    {
        failActive(Ai8OperationOutcome::Disconnected,
                   english_ ? QStringLiteral("Remote Sky AI-8 is disconnected or stale.")
                            : QStringLiteral("天空端 AI-8 已断开或数据已过期。"));
    }
    refreshAvailability();
}

bool Ai8DeviceSession::operationsAvailable() const
{
    if (!pending_operations_.isEmpty())
    {
        return false;
    }
    if (backend_ == Ai8Backend::Local)
    {
        return local_available_ && local_adapter_.readPage && local_adapter_.writePage;
    }
    return remote_available_ && remote_controller_ && remote_controller_->isOpen() &&
           remote_controller_->deviceOperationSupport() != DeviceOperationSupport::Unsupported;
}

bool Ai8DeviceSession::operationPending() const
{
    return !pending_operations_.isEmpty();
}

quint64 Ai8DeviceSession::readPage(
    Ai8TemperatureControllerProtocol::Page page,
    const Ai8TemperatureControllerProtocol::Selection& selection)
{
    Ai8TemperatureControllerProtocol::PageData requested;
    requested.page = page;
    requested.selection = selection;
    return beginOperation(Ai8Operation::Read, requested);
}

quint64 Ai8DeviceSession::writePage(
    const Ai8TemperatureControllerProtocol::PageData& data)
{
    return beginOperation(Ai8Operation::Write, data);
}

quint64 Ai8DeviceSession::restoreFactoryDefaults(
    Ai8TemperatureControllerProtocol::Page page,
    const Ai8TemperatureControllerProtocol::Selection& selection)
{
    Ai8TemperatureControllerProtocol::PageData requested;
    requested.page = page;
    requested.selection = selection;
    return beginOperation(Ai8Operation::FactoryReset, requested);
}

void Ai8DeviceSession::activatePage(
    Ai8TemperatureControllerProtocol::Page page,
    const Ai8TemperatureControllerProtocol::Selection& selection)
{
    active_page_key_ = pageKey(page, selection);
    const auto cached = activeCache().constFind(active_page_key_);
    if (cached != activeCache().constEnd())
    {
        emit pageDataAvailable(cached.value());
    }
}

quint64 Ai8DeviceSession::beginOperation(
    Ai8Operation operation,
    const Ai8TemperatureControllerProtocol::PageData& requested)
{
    const quint64 requestId = next_request_id_++;
    PendingOperation pending;
    pending.request_id = requestId;
    pending.session_generation = session_generation_;
    pending.backend = backend_;
    pending.operation = operation;
    pending.requested = requested;
    active_page_key_ = pageKey(requested.page, requested.selection);

    if (operation == Ai8Operation::FactoryReset && backend_ == Ai8Backend::Local)
    {
        Ai8SessionResult result;
        result.request_id = requestId;
        result.operation = operation;
        result.outcome = Ai8OperationOutcome::Unsupported;
        result.error_code = CommandErrorCode::UnknownCommand;
        result.requested = requested;
        result.message = english_ ? QStringLiteral("Local AI-8 factory reset is not supported.")
                                  : QStringLiteral("本地 AI-8 暂不支持恢复出厂设置。");
        emit operationFinished(result);
        return requestId;
    }

    if (!operationsAvailable())
    {
        Ai8SessionResult result;
        result.request_id = requestId;
        result.operation = operation;
        result.outcome = backend_ == Ai8Backend::Remote && remote_controller_ &&
                                 remote_controller_->deviceOperationSupport() ==
                                     DeviceOperationSupport::Unsupported
            ? Ai8OperationOutcome::Unsupported
            : Ai8OperationOutcome::Disconnected;
        result.error_code = result.outcome == Ai8OperationOutcome::Unsupported
            ? CommandErrorCode::UnknownCommand
            : CommandErrorCode::DeviceNotConnected;
        result.requested = requested;
        result.message = unavailableReason();
        emit operationFinished(result);
        return requestId;
    }

    pending_operations_.insert(requestId, pending);
    emit operationStarted(requestId, operation);
    if (backend_ == Ai8Backend::Local)
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

void Ai8DeviceSession::dispatchLocal(PendingOperation pending)
{
    const LocalAdapter adapter = local_adapter_;
    QPointer<Ai8DeviceSession> guard(this);
    QMetaObject::invokeMethod(local_worker_, [guard, adapter, pending]() {
        LocalAi8OperationResult localResult;
        if (pending.operation == Ai8Operation::Read && adapter.readPage)
        {
            localResult = adapter.readPage(pending.requested.page,
                                           pending.requested.selection);
        }
        else if (pending.operation == Ai8Operation::Write && adapter.writePage)
        {
            localResult = adapter.writePage(pending.requested);
        }
        else
        {
            localResult.message = QStringLiteral("AI-8 operation is not supported by the local backend.");
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
            const auto pendingIt = guard->pending_operations_.find(pending.request_id);
            if (pendingIt == guard->pending_operations_.end())
            {
                return;
            }
            guard->pending_operations_.erase(pendingIt);
            if (pending.session_generation != guard->session_generation_ ||
                guard->backend_ != Ai8Backend::Local)
            {
                return;
            }
            guard->finishPending(
                pending,
                localResult.success ? Ai8OperationOutcome::Success : Ai8OperationOutcome::Failed,
                localResult.success ? CommandErrorCode::Ok : CommandErrorCode::ConfigApplyFailed,
                localResult.data,
                localResult.message);
        }, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
}

void Ai8DeviceSession::dispatchRemote(PendingOperation pending)
{
    pending.link_generation = remote_controller_->linkGeneration();
    quint32 remoteRequestId = 0;
    switch (pending.operation)
    {
    case Ai8Operation::Read:
        remoteRequestId = remote_controller_->readAi8Page(
            pending.requested.page, pending.requested.selection);
        break;
    case Ai8Operation::Write:
        remoteRequestId = remote_controller_->writeAi8Page(pending.requested);
        break;
    case Ai8Operation::FactoryReset:
        remoteRequestId = remote_controller_->restoreAi8FactoryDefaults(
            pending.requested.page, pending.requested.selection);
        break;
    }
    if (remoteRequestId == 0)
    {
        pending_operations_.remove(pending.request_id);
        finishPending(pending,
                      remote_controller_->deviceOperationSupport() ==
                              DeviceOperationSupport::Unsupported
                          ? Ai8OperationOutcome::Unsupported
                          : Ai8OperationOutcome::Disconnected,
                      remote_controller_->deviceOperationSupport() ==
                              DeviceOperationSupport::Unsupported
                          ? CommandErrorCode::UnknownCommand
                          : CommandErrorCode::DeviceNotConnected,
                      {}, unavailableReason());
        return;
    }
    pending.remote_request_id = remoteRequestId;
    pending_operations_[pending.request_id] = pending;
    remote_request_to_session_.insert(remoteRequestId, pending.request_id);
}

void Ai8DeviceSession::finishPending(
    const PendingOperation& pending,
    Ai8OperationOutcome outcome,
    CommandErrorCode errorCode,
    const Ai8TemperatureControllerProtocol::PageData& data,
    const QString& message)
{
    Ai8SessionResult result;
    result.request_id = pending.request_id;
    result.operation = pending.operation;
    result.outcome = outcome;
    result.error_code = errorCode;
    result.requested = pending.requested;
    result.data = data;
    result.message = message;
    if (outcome == Ai8OperationOutcome::Success)
    {
        const QString key = pageKey(data.page, data.selection);
        activeCache().insert(key, data);
        if (key == active_page_key_)
        {
            emit pageDataAvailable(data);
        }
    }
    emit operationFinished(result);
    refreshAvailability();
}

void Ai8DeviceSession::failActive(Ai8OperationOutcome outcome, const QString& message)
{
    const auto pending = pending_operations_.values();
    pending_operations_.clear();
    remote_request_to_session_.clear();
    for (const PendingOperation& operation : pending)
    {
        finishPending(operation, outcome,
                      outcome == Ai8OperationOutcome::Unsupported
                          ? CommandErrorCode::UnknownCommand
                          : CommandErrorCode::DeviceNotConnected,
                      {}, message);
    }
}

void Ai8DeviceSession::refreshAvailability()
{
    emit availabilityChanged(operationsAvailable(), unavailableReason());
}

QString Ai8DeviceSession::unavailableReason() const
{
    if (!pending_operations_.isEmpty())
    {
        return english_ ? QStringLiteral("Waiting for the AI-8 operation to complete.")
                        : QStringLiteral("正在等待 AI-8 操作完成。");
    }
    if (backend_ == Ai8Backend::Local)
    {
        if (!local_detail_.isEmpty())
        {
            return local_detail_;
        }
        return english_ ? QStringLiteral("Local AI-8 is not connected.")
                        : QStringLiteral("本地 AI-8 尚未连接。");
    }
    if (remote_controller_ &&
        remote_controller_->deviceOperationSupport() == DeviceOperationSupport::Unsupported)
    {
        return english_ ? QStringLiteral("This Sky version does not support AI-8 parameter operations.")
                        : QStringLiteral("当前天空端版本不支持 AI-8 参数操作。");
    }
    if (!remote_detail_.isEmpty())
    {
        return remote_detail_;
    }
    return english_ ? QStringLiteral("Remote Sky AI-8 is disconnected or stale.")
                    : QStringLiteral("天空端 AI-8 未连接或数据已过期。");
}

QString Ai8DeviceSession::pageKey(
    Ai8TemperatureControllerProtocol::Page page,
    const Ai8TemperatureControllerProtocol::Selection& selection)
{
    return QStringLiteral("%1:%2:%3:%4")
        .arg(static_cast<int>(page))
        .arg(selection.channel)
        .arg(selection.inputGroup)
        .arg(selection.outputGroup);
}

QHash<QString, Ai8TemperatureControllerProtocol::PageData>& Ai8DeviceSession::activeCache()
{
    return backend_ == Ai8Backend::Local ? local_cache_ : remote_cache_;
}

const QHash<QString, Ai8TemperatureControllerProtocol::PageData>& Ai8DeviceSession::activeCache() const
{
    return backend_ == Ai8Backend::Local ? local_cache_ : remote_cache_;
}

}  // namespace VaporView::Ground::Devices
