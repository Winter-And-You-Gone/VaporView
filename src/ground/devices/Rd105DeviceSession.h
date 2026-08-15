#pragma once

#include "TelemetryTypes.h"
#include "data_types.h"
#include "ground/devices/LocalDeviceConnectionController.h"

#include <QObject>
#include <QHash>

#include <functional>

namespace VaporView::Ground::Devices
{

class RemoteSkyController;

enum class Rd105Backend
{
    Local,
    Remote,
};

enum class Rd105OperationOutcome
{
    Success,
    Failed,
    Timeout,
    Disconnected,
    Unsupported,
};

struct Rd105SessionResult
{
    quint64 request_id = 0;
    CommandId command = CommandId::SetTemperatureTarget;
    TemperatureControllerCommand payload;
    Rd105Backend backend = Rd105Backend::Local;
    Rd105OperationOutcome outcome = Rd105OperationOutcome::Failed;
    CommandErrorCode error_code = CommandErrorCode::Ok;
    QString message;
    TemperatureControllerData latest_data;
    bool has_latest_data = false;

    bool success() const { return outcome == Rd105OperationOutcome::Success; }
};

class Rd105DeviceSession final : public QObject
{
    Q_OBJECT

public:
    struct LocalAdapter
    {
        std::function<LocalTemperatureCommandResult(
            CommandId,
            const TemperatureControllerCommand&)> sendCommand;
    };

    Rd105DeviceSession(LocalAdapter localAdapter,
                       RemoteSkyController *remoteController,
                       QObject *parent = nullptr);

    void setEnglish(bool english);
    void setBackend(Rd105Backend backend);
    Rd105Backend backend() const;

    void setLocalAvailable(bool available, const QString& detail = QString());
    void setRemoteAvailable(bool available, const QString& detail = QString());
    bool operationsAvailable() const;
    bool operationPending() const;

    quint64 sendCommand(CommandId command, const TemperatureControllerCommand& payload);

signals:
    void availabilityChanged(bool available, const QString& reason);
    void operationStarted(quint64 requestId, VaporView::CommandId command,
                          VaporView::TemperatureControllerCommand payload);
    void operationFinished(const VaporView::Ground::Devices::Rd105SessionResult& result);

private:
    struct PendingCommand
    {
        quint64 request_id = 0;
        quint16 remote_sequence = 0;
        quint64 session_generation = 0;
        quint64 link_generation = 0;
        Rd105Backend backend = Rd105Backend::Local;
        CommandId command = CommandId::SetTemperatureTarget;
        TemperatureControllerCommand payload;
    };

    void finishPending(const PendingCommand& pending,
                       Rd105OperationOutcome outcome,
                       CommandErrorCode errorCode,
                       const QString& message,
                       const TemperatureControllerData& latestData = {},
                       bool hasLatestData = false);
    void failActive(Rd105OperationOutcome outcome, const QString& message);
    void refreshAvailability();
    QString unavailableReason() const;

    LocalAdapter local_adapter_;
    RemoteSkyController *remote_controller_ = nullptr;
    Rd105Backend backend_ = Rd105Backend::Local;
    bool english_ = false;
    bool local_available_ = false;
    bool remote_available_ = false;
    QString local_detail_;
    QString remote_detail_;
    quint64 session_generation_ = 1;
    quint64 next_request_id_ = 1;
    QHash<quint64, PendingCommand> pending_commands_;
    QHash<quint16, quint64> remote_sequence_to_request_;
};

}  // namespace VaporView::Ground::Devices

Q_DECLARE_METATYPE(VaporView::Ground::Devices::Rd105SessionResult)
