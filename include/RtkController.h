#ifndef VAPORVIEW_RTK_CONTROLLER_H
#define VAPORVIEW_RTK_CONTROLLER_H

#include "RtkStreamService.h"
#include "serial_port.h"

#include <QObject>
#include <QStringList>
#include <QStringListModel>
#include <QTimer>
#include <QUrl>

#include <atomic>
#include <chrono>
#include <deque>
#include <memory>
#include <thread>

class RtkController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool english READ english NOTIFY englishChanged)
    Q_PROPERTY(bool running READ running NOTIFY stateChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(bool fetchInProgress READ fetchInProgress NOTIFY stateChanged)
    Q_PROPERTY(bool testInProgress READ testInProgress NOTIFY stateChanged)
    Q_PROPERTY(bool ggaMonitorEnabled READ ggaMonitorEnabled NOTIFY stateChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
    Q_PROPERTY(QString ggaStatusText READ ggaStatusText NOTIFY ggaStatusChanged)
    Q_PROPERTY(QString ggaFrequencyText READ ggaFrequencyText NOTIFY ggaStatusChanged)
    Q_PROPERTY(QStringList portOptions READ portOptions NOTIFY portOptionsChanged)
    Q_PROPERTY(QStringList mountpointOptions READ mountpointOptions NOTIFY mountpointOptionsChanged)
    Q_PROPERTY(QStringList baudOptions READ baudOptions CONSTANT)
    Q_PROPERTY(QStringList timingOptions READ timingOptions CONSTANT)
    Q_PROPERTY(QString server READ server WRITE setServer NOTIFY configChanged)
    Q_PROPERTY(QString port READ port WRITE setPort NOTIFY configChanged)
    Q_PROPERTY(QString username READ username WRITE setUsername NOTIFY configChanged)
    Q_PROPERTY(QString password READ password WRITE setPassword NOTIFY configChanged)
    Q_PROPERTY(QString mountpoint READ mountpoint WRITE setMountpoint NOTIFY configChanged)
    Q_PROPERTY(QString outputPort READ outputPort WRITE setOutputPort NOTIFY configChanged)
    Q_PROPERTY(QString ggaPort READ ggaPort WRITE setGgaPort NOTIFY configChanged)
    Q_PROPERTY(QString baudrate READ baudrate WRITE setBaudrate NOTIFY configChanged)
    Q_PROPERTY(QString timeoutMs READ timeoutMs WRITE setTimeoutMs NOTIFY configChanged)
    Q_PROPERTY(QString reconnectMs READ reconnectMs WRITE setReconnectMs NOTIFY configChanged)
    Q_PROPERTY(QStringListModel* logModel READ logModel CONSTANT)
    Q_PROPERTY(QStringListModel* ggaLogModel READ ggaLogModel CONSTANT)

public:
    explicit RtkController(QObject *parent = nullptr);
    ~RtkController() override;

    bool english() const;
    bool running() const;
    bool busy() const;
    bool fetchInProgress() const;
    bool testInProgress() const;
    bool ggaMonitorEnabled() const;

    QString statusText() const;
    QString ggaStatusText() const;
    QString ggaFrequencyText() const;

    QStringList portOptions() const;
    QStringList mountpointOptions() const;
    QStringList baudOptions() const;
    QStringList timingOptions() const;

    QString server() const;
    QString port() const;
    QString username() const;
    QString password() const;
    QString mountpoint() const;
    QString outputPort() const;
    QString ggaPort() const;
    QString baudrate() const;
    QString timeoutMs() const;
    QString reconnectMs() const;

    QStringListModel *logModel();
    QStringListModel *ggaLogModel();

    void setEnglish(bool english);
    void setServer(const QString &value);
    void setPort(const QString &value);
    void setUsername(const QString &value);
    void setPassword(const QString &value);
    void setMountpoint(const QString &value);
    void setOutputPort(const QString &value);
    void setGgaPort(const QString &value);
    void setBaudrate(const QString &value);
    void setTimeoutMs(const QString &value);
    void setReconnectMs(const QString &value);

    Q_INVOKABLE void refreshPorts();
    Q_INVOKABLE void fetchMountpoints();
    Q_INVOKABLE void startService();
    Q_INVOKABLE void stopService();
    Q_INVOKABLE void runNoSignalTest();
    Q_INVOKABLE void toggleGgaMonitor();
    Q_INVOKABLE bool saveProfileToUrl(const QUrl &url);
    Q_INVOKABLE bool loadProfileFromUrl(const QUrl &url);

signals:
    void englishChanged();
    void stateChanged();
    void statusTextChanged();
    void ggaStatusChanged();
    void portOptionsChanged();
    void mountpointOptionsChanged();
    void configChanged();

private:
    struct NoSignalTestResult
    {
        bool cancelled = false;
        bool gotResponse = false;
        bool linkReady = false;
        QString startError;
        QString runtimeError;
        QString finalMessage;
        qint64 inputBytes = 0;
        qint64 outputBytes = 0;
        qint64 receivedRtcmBytes = 0;
    };

    void appendLog(const QString &message);
    void appendRawLogLine(const QString &message);
    void appendGgaLog(const QString &message);
    void trimStringListModel(QStringListModel &model, int limit);

    void loadSettings();
    void saveSettings() const;
    bool saveProfile(const QString &filename);
    bool loadProfile(const QString &filename);
    bool buildConfig(RtkStreamConfig *config, QString *description = nullptr) const;
    QString textFor(const QString &englishText, const QString &chineseText) const;
    QStringList availablePorts() const;
    QString ggaPortName() const;
    int currentGgaBaudrate() const;
    void updateStatusText();
    void pollRtkServiceStatus(bool forceLog);
    void updateGgaFrequency(double hz);
    void updateGgaStatusLabel(const QString &message, bool healthy);
    void updateMountpointOptions(const QStringList &options);
    bool tryOpenGgaPort();
    void processGgaBuffer();
    void handleGgaSentence(const QString &sentence);
    void stopGgaMonitorInternal();
    void joinBackgroundThreads();
    static QString localFilePath(const QUrl &url);

    bool english_ = false;
    bool running_ = false;
    QString status_text_;
    QString gga_status_text_;
    QString gga_frequency_text_;
    QStringList port_options_;
    QStringList mountpoint_options_;
    QStringList baud_options_;
    QStringList timing_options_;

    QString server_;
    QString port_;
    QString username_;
    QString password_;
    QString mountpoint_;
    QString output_port_;
    QString gga_port_;
    QString baudrate_;
    QString timeout_ms_;
    QString reconnect_ms_;

    QStringListModel log_model_;
    QStringListModel gga_log_model_;

    std::unique_ptr<RtkStreamService> rtk_service_;
    QTimer status_timer_;
    QTimer gga_poll_timer_;
    VaporView::SerialPort gga_serial_;
    QString gga_buffer_;
    std::chrono::steady_clock::time_point gga_last_open_attempt_;
    std::chrono::steady_clock::time_point gga_last_sentence_time_;
    std::deque<double> gga_recent_intervals_sec_;
    bool gga_has_sentence_time_ = false;
    bool gga_monitor_enabled_ = false;

    std::atomic<bool> fetch_mountpoints_in_progress_{false};
    std::atomic<bool> test_in_progress_{false};
    std::atomic<bool> shutdown_requested_{false};
    std::thread fetch_mountpoints_thread_;
    std::thread test_thread_;
};

#endif
