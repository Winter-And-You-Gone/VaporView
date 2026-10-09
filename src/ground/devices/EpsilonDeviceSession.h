#pragma once

#include "TelemetryTypes.h"
#include "ground/devices/EpsilonConfigurationService.h"

#include <QObject>
#include <QHash>
#include <QThread>

#include <functional>

namespace VaporView::Ground::Devices
{

class RemoteSkyController;

enum class EpsilonBackend
{
    Local,
    Remote,
};

enum class EpsilonOperation
{
    ConfigurePacketRates,
    ConfigureMainAntennaLeverArm,
    ConfigureRtcmInput,
    ReadSettings,
    ApplySettings,
    RestartDevice,
    CalibrateLevel,
    CalibrateAccelerometer,
    CalibrateGyroscope,
    ReadDgnss,
    ApplyDgnss,
    CalibrateMagnetic2D,
    CalibrateMagnetic3D,
};

enum class EpsilonOperationOutcome
{
    Success,
    Failed,
    Timeout,
    Disconnected,
    Unsupported,
};

struct EpsilonSessionResult
{
    quint64 request_id = 0;
    EpsilonOperation operation = EpsilonOperation::ConfigurePacketRates;
    EpsilonOperationOutcome outcome = EpsilonOperationOutcome::Failed;
    CommandErrorCode error_code = CommandErrorCode::Ok;
    QString message;
    VaporView::Ground::EpsilonConfigurationResult local_result;
    EpsilonMaintenanceResult maintenance_result;

    bool success() const { return outcome == EpsilonOperationOutcome::Success; }
};

class EpsilonDeviceSession final : public QObject
{
    Q_OBJECT

public:
    struct LocalAdapter
    {
        std::function<VaporView::Ground::EpsilonConfigurationResult(
            const EpsilonPacketRatesOperation&,
            const VaporView::Ground::EpsilonDeviceOperation&)> configurePacketRates;
        std::function<VaporView::Ground::EpsilonConfigurationResult(
            const EpsilonMainAntennaLeverArmOperation&,
            const VaporView::Ground::EpsilonDeviceOperation&)> configureMainAntennaLeverArm;
        std::function<VaporView::Ground::EpsilonConfigurationResult(
            const EpsilonRtcmInputOperation&,
            const VaporView::Ground::EpsilonDeviceOperation&)> configureRtcmInput;
        std::function<VaporView::Ground::EpsilonConfigurationResult(EpsilonSettingsGroup,
            const VaporView::Ground::EpsilonDeviceOperation&)> readSettings;
        std::function<VaporView::Ground::EpsilonConfigurationResult(const EpsilonSettingsOperation&,
            const VaporView::Ground::EpsilonDeviceOperation&)> applySettings;
        std::function<VaporView::Ground::EpsilonConfigurationResult(
            const VaporView::Ground::EpsilonDeviceOperation&)> restartDevice;
        std::function<VaporView::Ground::EpsilonConfigurationResult(EpsilonMaintenanceAction,
            const VaporView::Ground::EpsilonDeviceOperation&)> calibrate;
        std::function<VaporView::Ground::EpsilonConfigurationResult(const VaporView::Ground::EpsilonDeviceOperation&)> readDgnss;
        std::function<VaporView::Ground::EpsilonConfigurationResult(const EpsilonDgnssOperation&,
            const VaporView::Ground::EpsilonDeviceOperation&)> applyDgnss;
    };

    EpsilonDeviceSession(LocalAdapter localAdapter,
                         RemoteSkyController *remoteController,
                         QObject *parent = nullptr);
    ~EpsilonDeviceSession() override;

    void setEnglish(bool english);
    void setBackend(EpsilonBackend backend);
    EpsilonBackend backend() const;

    void setLocalAvailable(bool available, const QString& detail = QString());
    void setRemoteAvailable(bool available, const QString& detail = QString());
    bool operationsAvailable() const;
    bool operationPending() const;

    quint64 configurePacketRates(
        const EpsilonPacketRatesOperation& operation,
        const VaporView::Ground::EpsilonDeviceOperation& localDeviceOperation = {});
    quint64 configureMainAntennaLeverArm(
        const EpsilonMainAntennaLeverArmOperation& operation,
        const VaporView::Ground::EpsilonDeviceOperation& localDeviceOperation = {});
    quint64 configureRtcmInput(
        const EpsilonRtcmInputOperation& operation,
        const VaporView::Ground::EpsilonDeviceOperation& localDeviceOperation = {});

    quint64 readSettings(EpsilonSettingsGroup group,
        const VaporView::Ground::EpsilonDeviceOperation& localDeviceOperation = {});
    quint64 applySettings(const EpsilonSettingsOperation& operation,
        const VaporView::Ground::EpsilonDeviceOperation& localDeviceOperation = {});
    quint64 restartDevice(const VaporView::Ground::EpsilonDeviceOperation& localDeviceOperation = {});
    quint64 calibrate(EpsilonMaintenanceAction action,
        const VaporView::Ground::EpsilonDeviceOperation& localDeviceOperation = {});
    quint64 readDgnss(const VaporView::Ground::EpsilonDeviceOperation& localDeviceOperation = {});
    quint64 applyDgnss(const EpsilonDgnssOperation& settings, const VaporView::Ground::EpsilonDeviceOperation& localDeviceOperation = {});
    void cancelMaintenance();

signals:
    void maintenanceProgress(const VaporView::EpsilonMaintenanceResult& result);
    void availabilityChanged(bool available, const QString& reason);
    void operationStarted(quint64 requestId,
                          VaporView::Ground::Devices::EpsilonOperation operation);
    void operationFinished(
        const VaporView::Ground::Devices::EpsilonSessionResult& result);

private:
    struct PendingOperation
    {
        quint64 request_id = 0;
        quint32 remote_request_id = 0;
        quint64 session_generation = 0;
        quint64 link_generation = 0;
        EpsilonBackend backend = EpsilonBackend::Local;
        EpsilonOperation operation = EpsilonOperation::ConfigurePacketRates;
        EpsilonPacketRatesOperation packet_rates;
        EpsilonMainAntennaLeverArmOperation lever_arm;
        EpsilonRtcmInputOperation rtcm_input;
        EpsilonSettingsOperation settings;
        EpsilonDgnssOperation dgnss;
        EpsilonMaintenanceAction maintenance_action = EpsilonMaintenanceAction::Level;
        EpsilonMaintenanceResult last_maintenance_result;
        VaporView::Ground::EpsilonDeviceOperation local_device_operation;
    };

    quint64 beginOperation(PendingOperation pending);
    void dispatchLocal(PendingOperation pending);
    void dispatchRemote(PendingOperation pending);
    void finishPending(const PendingOperation& pending,
                       EpsilonOperationOutcome outcome,
                       CommandErrorCode errorCode,
                       const QString& message,
                       const VaporView::Ground::EpsilonConfigurationResult& localResult = {});
    void failActive(EpsilonOperationOutcome outcome, const QString& message);
    void refreshAvailability();
    QString unavailableReason() const;

    LocalAdapter local_adapter_;
    RemoteSkyController *remote_controller_ = nullptr;
    QThread local_worker_thread_;
    QObject *local_worker_ = nullptr;
    EpsilonBackend backend_ = EpsilonBackend::Local;
    bool english_ = false;
    bool local_worker_busy_ = false;
    bool local_available_ = false;
    bool remote_available_ = false;
    QString local_detail_;
    QString remote_detail_;
    quint64 session_generation_ = 1;
    quint64 next_request_id_ = 1;
    QHash<quint64, PendingOperation> pending_operations_;
    QHash<quint32, quint64> remote_request_to_session_;
    std::shared_ptr<std::atomic_bool> local_maintenance_cancel_;
};

}  // namespace VaporView::Ground::Devices

Q_DECLARE_METATYPE(VaporView::Ground::Devices::EpsilonOperation)
Q_DECLARE_METATYPE(VaporView::Ground::Devices::EpsilonSessionResult)

Q_DECLARE_METATYPE(VaporView::EpsilonSettingsGroup)
Q_DECLARE_METATYPE(VaporView::EpsilonSettingsOperation)
Q_DECLARE_METATYPE(VaporView::EpsilonSettingsSnapshot)
Q_DECLARE_METATYPE(VaporView::EpsilonMaintenanceAction)
Q_DECLARE_METATYPE(VaporView::EpsilonMaintenanceResult)
Q_DECLARE_METATYPE(VaporView::EpsilonDgnssOperation)
Q_DECLARE_METATYPE(VaporView::EpsilonDgnssSnapshot)
