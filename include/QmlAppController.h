#ifndef VAPORVIEW_QML_APP_CONTROLLER_H
#define VAPORVIEW_QML_APP_CONTROLLER_H

#include "data_collector.h"
#include "data_types.h"

#include <QObject>
#include <QStringList>
#include <QStringListModel>
#include <QTimer>
#include <QVariantMap>

#include <atomic>
#include <memory>
#include <mutex>
#include <thread>

class QmlAppController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool english READ english NOTIFY englishChanged)
    Q_PROPERTY(bool connected READ connected NOTIFY connectionStateChanged)
    Q_PROPERTY(bool connectionAttemptInProgress READ connectionAttemptInProgress NOTIFY connectionStateChanged)
    Q_PROPERTY(bool portDetectionInProgress READ portDetectionInProgress NOTIFY connectionStateChanged)
    Q_PROPERTY(bool canConnect READ canConnect NOTIFY connectionStateChanged)
    Q_PROPERTY(bool canDisconnect READ canDisconnect NOTIFY connectionStateChanged)
    Q_PROPERTY(bool canCancelConnect READ canCancelConnect NOTIFY connectionStateChanged)
    Q_PROPERTY(bool canEditPorts READ canEditPorts NOTIFY connectionStateChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
    Q_PROPERTY(QString statusKind READ statusKind NOTIFY statusTextChanged)
    Q_PROPERTY(QStringList portOptions READ portOptions NOTIFY portOptionsChanged)
    Q_PROPERTY(QStringList baudOptions READ baudOptions CONSTANT)
    Q_PROPERTY(QStringList rateOptions READ rateOptions CONSTANT)
    Q_PROPERTY(QString gnssPort READ gnssPort WRITE setGnssPort NOTIFY portSelectionChanged)
    Q_PROPERTY(QString imuPort READ imuPort WRITE setImuPort NOTIFY portSelectionChanged)
    Q_PROPERTY(QString ptbPort READ ptbPort WRITE setPtbPort NOTIFY portSelectionChanged)
    Q_PROPERTY(QString hmpPort READ hmpPort WRITE setHmpPort NOTIFY portSelectionChanged)
    Q_PROPERTY(QString lidarPort READ lidarPort WRITE setLidarPort NOTIFY portSelectionChanged)
    Q_PROPERTY(QString gnssBaud READ gnssBaud WRITE setGnssBaud NOTIFY baudSelectionChanged)
    Q_PROPERTY(QString imuBaud READ imuBaud WRITE setImuBaud NOTIFY baudSelectionChanged)
    Q_PROPERTY(QString ptbBaud READ ptbBaud WRITE setPtbBaud NOTIFY baudSelectionChanged)
    Q_PROPERTY(QString hmpBaud READ hmpBaud WRITE setHmpBaud NOTIFY baudSelectionChanged)
    Q_PROPERTY(QString lidarBaud READ lidarBaud WRITE setLidarBaud NOTIFY baudSelectionChanged)
    Q_PROPERTY(QString gnssRate READ gnssRate WRITE setGnssRate NOTIFY rateSelectionChanged)
    Q_PROPERTY(QString imuRate READ imuRate WRITE setImuRate NOTIFY rateSelectionChanged)
    Q_PROPERTY(QString ptbRate READ ptbRate WRITE setPtbRate NOTIFY rateSelectionChanged)
    Q_PROPERTY(QString hmpRate READ hmpRate WRITE setHmpRate NOTIFY rateSelectionChanged)
    Q_PROPERTY(QString lidarRate READ lidarRate WRITE setLidarRate NOTIFY rateSelectionChanged)
    Q_PROPERTY(QVariantMap gnssData READ gnssData NOTIFY sensorDataChanged)
    Q_PROPERTY(QVariantMap imuData READ imuData NOTIFY sensorDataChanged)
    Q_PROPERTY(QVariantMap ptbData READ ptbData NOTIFY sensorDataChanged)
    Q_PROPERTY(QVariantMap hmpData READ hmpData NOTIFY sensorDataChanged)
    Q_PROPERTY(QVariantMap lidarData READ lidarData NOTIFY sensorDataChanged)
    Q_PROPERTY(QStringListModel* logModel READ logModel CONSTANT)

public:
    explicit QmlAppController(QObject *parent = nullptr);
    ~QmlAppController() override;

    bool english() const;
    bool connected() const;
    bool connectionAttemptInProgress() const;
    bool portDetectionInProgress() const;
    bool canConnect() const;
    bool canDisconnect() const;
    bool canCancelConnect() const;
    bool canEditPorts() const;

    QString statusText() const;
    QString statusKind() const;

    QStringList portOptions() const;
    QStringList baudOptions() const;
    QStringList rateOptions() const;

    QString gnssPort() const;
    QString imuPort() const;
    QString ptbPort() const;
    QString hmpPort() const;
    QString lidarPort() const;

    QString gnssBaud() const;
    QString imuBaud() const;
    QString ptbBaud() const;
    QString hmpBaud() const;
    QString lidarBaud() const;

    QString gnssRate() const;
    QString imuRate() const;
    QString ptbRate() const;
    QString hmpRate() const;
    QString lidarRate() const;

    QVariantMap gnssData() const;
    QVariantMap imuData() const;
    QVariantMap ptbData() const;
    QVariantMap hmpData() const;
    QVariantMap lidarData() const;

    QStringListModel *logModel();

    void setGnssPort(const QString &value);
    void setImuPort(const QString &value);
    void setPtbPort(const QString &value);
    void setHmpPort(const QString &value);
    void setLidarPort(const QString &value);

    void setGnssBaud(const QString &value);
    void setImuBaud(const QString &value);
    void setPtbBaud(const QString &value);
    void setHmpBaud(const QString &value);
    void setLidarBaud(const QString &value);

    void setGnssRate(const QString &value);
    void setImuRate(const QString &value);
    void setPtbRate(const QString &value);
    void setHmpRate(const QString &value);
    void setLidarRate(const QString &value);

    Q_INVOKABLE void toggleLanguage();
    Q_INVOKABLE void refreshPorts();
    Q_INVOKABLE void autoDetectPorts();
    Q_INVOKABLE void connectDevices();
    Q_INVOKABLE void disconnectDevices();
    Q_INVOKABLE void cancelConnect();
    Q_INVOKABLE void openRtkConfig();
    Q_INVOKABLE void openSessionViewer();

signals:
    void englishChanged();
    void connectionStateChanged();
    void statusTextChanged();
    void portOptionsChanged();
    void portSelectionChanged();
    void baudSelectionChanged();
    void rateSelectionChanged();
    void sensorDataChanged();

private:
    struct CollectorSnapshot
    {
        std::shared_ptr<VaporView::GnssCollector> gnss;
        std::shared_ptr<VaporView::ImuCollector> imu;
        std::shared_ptr<VaporView::PtbCollector> ptb;
        std::shared_ptr<VaporView::HmpCollector> hmp;
        std::shared_ptr<VaporView::LidarCollector> lidar;
    };

    QString emptyPortSelectionText() const;
    QString normalizePortSelection(const QString &value) const;
    void assignPortValue(QString &target, const QString &value);
    void assignTextValue(QString &target, const QString &value, void (QmlAppController::*signal)());
    QStringList currentPortNames() const;
    static int parseRate(const QString &text);

    void appendLog(const QString &message);
    void trimLogs();
    void resetSensorData();
    void refreshSensorCards();
    void updateStatusPresentation();

    CollectorSnapshot snapshotCollectors() const;
    void setCollectors(CollectorSnapshot collectors);
    void stopAllCollectors();
    bool shouldAbortConnectionAttempt() const;
    void finishConnectionAttempt(bool connected);
    void joinThreads();

    QVariantMap makeGnssMap(const CollectorSnapshot &collectors) const;
    QVariantMap makeImuMap(const CollectorSnapshot &collectors) const;
    QVariantMap makePtbMap(const CollectorSnapshot &collectors) const;
    QVariantMap makeHmpMap(const CollectorSnapshot &collectors) const;
    QVariantMap makeLidarMap(const CollectorSnapshot &collectors) const;

    bool english_ = false;
    bool connected_ = false;
    bool connection_attempt_in_progress_ = false;
    bool port_detection_in_progress_ = false;
    QString status_text_;
    QString status_kind_;
    QStringList port_options_;
    QStringList baud_options_;
    QStringList rate_options_;

    QString gnss_port_;
    QString imu_port_;
    QString ptb_port_;
    QString hmp_port_;
    QString lidar_port_;

    QString gnss_baud_;
    QString imu_baud_;
    QString ptb_baud_;
    QString hmp_baud_;
    QString lidar_baud_;

    QString gnss_rate_;
    QString imu_rate_;
    QString ptb_rate_;
    QString hmp_rate_;
    QString lidar_rate_;

    mutable std::mutex collector_mutex_;
    std::shared_ptr<VaporView::GnssCollector> gnss_collector_;
    std::shared_ptr<VaporView::ImuCollector> imu_collector_;
    std::shared_ptr<VaporView::PtbCollector> ptb_collector_;
    std::shared_ptr<VaporView::HmpCollector> hmp_collector_;
    std::shared_ptr<VaporView::LidarCollector> lidar_collector_;

    VaporView::GnssData current_gnss_;
    VaporView::ImuData current_imu_;
    VaporView::PtbData current_ptb_;
    VaporView::HmpData current_hmp_;
    VaporView::LidarData current_lidar_;

    QVariantMap gnss_map_;
    QVariantMap imu_map_;
    QVariantMap ptb_map_;
    QVariantMap hmp_map_;
    QVariantMap lidar_map_;

    QStringListModel log_model_;
    QTimer refresh_timer_;
    std::atomic<bool> cancel_connection_requested_{false};
    std::thread connection_thread_;
    std::thread port_detection_thread_;
};

#endif
