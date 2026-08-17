#pragma once

#include "TelemetryTypes.h"
#include "ground/devices/LocalDeviceConnectionController.h"

#include <QObject>
#include <QHash>
#include <QThread>

#include <functional>

namespace VaporView::Ground::Devices
{

class RemoteSkyController;

enum class Ai8Backend
{
    Local,
    Remote,
};

enum class Ai8Operation
{
    Read,
    Write,
    FactoryReset,
};

enum class Ai8OperationOutcome
{
    Success,
    Failed,
    Timeout,
    Disconnected,
    Unsupported,
};

struct Ai8SessionResult
{
    quint64 request_id = 0;
    Ai8Operation operation = Ai8Operation::Read;
    Ai8OperationOutcome outcome = Ai8OperationOutcome::Failed;
    CommandErrorCode error_code = CommandErrorCode::Ok;
    Ai8TemperatureControllerProtocol::PageData requested;
    Ai8TemperatureControllerProtocol::PageData data;
    QString message;

    bool success() const { return outcome == Ai8OperationOutcome::Success; }
};

class Ai8DeviceSession final : public QObject
{
    Q_OBJECT

public:
    struct LocalAdapter
    {
        std::function<LocalAi8OperationResult(
            Ai8TemperatureControllerProtocol::Page,
            const Ai8TemperatureControllerProtocol::Selection&)> readPage;
        std::function<LocalAi8OperationResult(
            const Ai8TemperatureControllerProtocol::PageData&)> writePage;
    };

    Ai8DeviceSession(LocalAdapter localAdapter,
                     RemoteSkyController *remoteController,
                     QObject *parent = nullptr);
    ~Ai8DeviceSession() override;

    void setEnglish(bool english);
    void setBackend(Ai8Backend backend);
    Ai8Backend backend() const;

    void setLocalAvailable(bool available, const QString& detail = QString());
    void setRemoteAvailable(bool available, const QString& detail = QString());
    bool operationsAvailable() const;
    bool operationPending() const;

    quint64 readPage(Ai8TemperatureControllerProtocol::Page page,
                     const Ai8TemperatureControllerProtocol::Selection& selection);
    quint64 writePage(const Ai8TemperatureControllerProtocol::PageData& data);
    quint64 restoreFactoryDefaults(
        Ai8TemperatureControllerProtocol::Page page,
        const Ai8TemperatureControllerProtocol::Selection& selection);
    void activatePage(Ai8TemperatureControllerProtocol::Page page,
                      const Ai8TemperatureControllerProtocol::Selection& selection);

signals:
    void availabilityChanged(bool available, const QString& reason);
    void operationStarted(quint64 requestId,
                          VaporView::Ground::Devices::Ai8Operation operation);
    void operationFinished(const VaporView::Ground::Devices::Ai8SessionResult& result);
    void pageDataAvailable(
        const VaporView::Ai8TemperatureControllerProtocol::PageData& data);

private:
    struct PendingOperation
    {
        quint64 request_id = 0;
        quint32 remote_request_id = 0;
        quint64 session_generation = 0;
        quint64 link_generation = 0;
        Ai8Backend backend = Ai8Backend::Local;
        Ai8Operation operation = Ai8Operation::Read;
        Ai8TemperatureControllerProtocol::PageData requested;
    };

    quint64 beginOperation(Ai8Operation operation,
                           const Ai8TemperatureControllerProtocol::PageData& requested);
    void dispatchLocal(PendingOperation pending);
    void dispatchRemote(PendingOperation pending);
    void finishPending(const PendingOperation& pending,
                       Ai8OperationOutcome outcome,
                       CommandErrorCode errorCode,
                       const Ai8TemperatureControllerProtocol::PageData& data,
                       const QString& message);
    void failActive(Ai8OperationOutcome outcome, const QString& message);
    void refreshAvailability();
    QString unavailableReason() const;
    static QString pageKey(Ai8TemperatureControllerProtocol::Page page,
                           const Ai8TemperatureControllerProtocol::Selection& selection);
    QHash<QString, Ai8TemperatureControllerProtocol::PageData>& activeCache();
    const QHash<QString, Ai8TemperatureControllerProtocol::PageData>& activeCache() const;

    LocalAdapter local_adapter_;
    RemoteSkyController *remote_controller_ = nullptr;
    QThread local_worker_thread_;
    QObject *local_worker_ = nullptr;
    Ai8Backend backend_ = Ai8Backend::Local;
    bool english_ = false;
    bool local_available_ = false;
    bool remote_available_ = false;
    QString local_detail_;
    QString remote_detail_;
    quint64 session_generation_ = 1;
    quint64 next_request_id_ = 1;
    QString active_page_key_;
    QHash<QString, Ai8TemperatureControllerProtocol::PageData> local_cache_;
    QHash<QString, Ai8TemperatureControllerProtocol::PageData> remote_cache_;
    QHash<quint64, PendingOperation> pending_operations_;
    QHash<quint32, quint64> remote_request_to_session_;
};

}  // namespace VaporView::Ground::Devices

Q_DECLARE_METATYPE(VaporView::Ground::Devices::Ai8Operation)
Q_DECLARE_METATYPE(VaporView::Ground::Devices::Ai8SessionResult)
