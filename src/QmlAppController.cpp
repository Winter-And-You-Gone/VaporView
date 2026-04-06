#include "QmlAppController.h"

#include <QApplication>
#include <QDateTime>
#include <QMetaObject>
#include <QSerialPortInfo>

#include <algorithm>

QmlAppController::QmlAppController(QObject *parent)
    : QObject(parent)
{
    baud_options_ = {"9600", "19200", "38400", "57600", "115200", "230400", "460800", "921600"};
    rate_options_ = {"1", "2", "5", "10", "20", "50", "100", "200", "500"};

    gnss_port_ = "COM3";
    imu_port_ = "COM4";
    ptb_port_ = "COM5";
    hmp_port_ = "COM6";
    lidar_port_ = "COM7";

    gnss_baud_ = "115200";
    imu_baud_ = "115200";
    ptb_baud_ = "9600";
    hmp_baud_ = "19200";
    lidar_baud_ = "115200";

    gnss_rate_ = "20";
    imu_rate_ = "20";
    ptb_rate_ = "20";
    hmp_rate_ = "20";
    lidar_rate_ = "20";

    refresh_timer_.setInterval(250);
    connect(&refresh_timer_, &QTimer::timeout, this, [this]() {
        refreshSensorCards();
    });
    refresh_timer_.start();

    resetSensorData();
    refreshPorts();
    updateStatusPresentation();
    appendLog("FluentUI shell initialized");
}

QmlAppController::~QmlAppController()
{
    cancel_connection_requested_.store(true);
    stopAllCollectors();
    joinThreads();
}

bool QmlAppController::english() const { return english_; }
bool QmlAppController::connected() const { return connected_; }
bool QmlAppController::connectionAttemptInProgress() const { return connection_attempt_in_progress_; }
bool QmlAppController::portDetectionInProgress() const { return port_detection_in_progress_; }

bool QmlAppController::canConnect() const
{
    return !connected_ && !connection_attempt_in_progress_ && !port_detection_in_progress_;
}

bool QmlAppController::canDisconnect() const
{
    return connected_ && !connection_attempt_in_progress_;
}

bool QmlAppController::canCancelConnect() const
{
    return connection_attempt_in_progress_;
}

bool QmlAppController::canEditPorts() const
{
    return !connected_ && !connection_attempt_in_progress_ && !port_detection_in_progress_;
}

QString QmlAppController::statusText() const { return status_text_; }
QString QmlAppController::statusKind() const { return status_kind_; }
QStringList QmlAppController::portOptions() const { return port_options_; }
QStringList QmlAppController::baudOptions() const { return baud_options_; }
QStringList QmlAppController::rateOptions() const { return rate_options_; }

QString QmlAppController::gnssPort() const { return gnss_port_; }
QString QmlAppController::imuPort() const { return imu_port_; }
QString QmlAppController::ptbPort() const { return ptb_port_; }
QString QmlAppController::hmpPort() const { return hmp_port_; }
QString QmlAppController::lidarPort() const { return lidar_port_; }

QString QmlAppController::gnssBaud() const { return gnss_baud_; }
QString QmlAppController::imuBaud() const { return imu_baud_; }
QString QmlAppController::ptbBaud() const { return ptb_baud_; }
QString QmlAppController::hmpBaud() const { return hmp_baud_; }
QString QmlAppController::lidarBaud() const { return lidar_baud_; }

QString QmlAppController::gnssRate() const { return gnss_rate_; }
QString QmlAppController::imuRate() const { return imu_rate_; }
QString QmlAppController::ptbRate() const { return ptb_rate_; }
QString QmlAppController::hmpRate() const { return hmp_rate_; }
QString QmlAppController::lidarRate() const { return lidar_rate_; }

QVariantMap QmlAppController::gnssData() const { return gnss_map_; }
QVariantMap QmlAppController::imuData() const { return imu_map_; }
QVariantMap QmlAppController::ptbData() const { return ptb_map_; }
QVariantMap QmlAppController::hmpData() const { return hmp_map_; }
QVariantMap QmlAppController::lidarData() const { return lidar_map_; }

QStringListModel *QmlAppController::logModel()
{
    return &log_model_;
}

void QmlAppController::setGnssPort(const QString &value) { assignPortValue(gnss_port_, value); }
void QmlAppController::setImuPort(const QString &value) { assignPortValue(imu_port_, value); }
void QmlAppController::setPtbPort(const QString &value) { assignPortValue(ptb_port_, value); }
void QmlAppController::setHmpPort(const QString &value) { assignPortValue(hmp_port_, value); }
void QmlAppController::setLidarPort(const QString &value) { assignPortValue(lidar_port_, value); }

void QmlAppController::setGnssBaud(const QString &value) { assignTextValue(gnss_baud_, value, &QmlAppController::baudSelectionChanged); }
void QmlAppController::setImuBaud(const QString &value) { assignTextValue(imu_baud_, value, &QmlAppController::baudSelectionChanged); }
void QmlAppController::setPtbBaud(const QString &value) { assignTextValue(ptb_baud_, value, &QmlAppController::baudSelectionChanged); }
void QmlAppController::setHmpBaud(const QString &value) { assignTextValue(hmp_baud_, value, &QmlAppController::baudSelectionChanged); }
void QmlAppController::setLidarBaud(const QString &value) { assignTextValue(lidar_baud_, value, &QmlAppController::baudSelectionChanged); }

void QmlAppController::setGnssRate(const QString &value) { assignTextValue(gnss_rate_, value, &QmlAppController::rateSelectionChanged); }
void QmlAppController::setImuRate(const QString &value) { assignTextValue(imu_rate_, value, &QmlAppController::rateSelectionChanged); }
void QmlAppController::setPtbRate(const QString &value) { assignTextValue(ptb_rate_, value, &QmlAppController::rateSelectionChanged); }
void QmlAppController::setHmpRate(const QString &value) { assignTextValue(hmp_rate_, value, &QmlAppController::rateSelectionChanged); }
void QmlAppController::setLidarRate(const QString &value) { assignTextValue(lidar_rate_, value, &QmlAppController::rateSelectionChanged); }

void QmlAppController::toggleLanguage()
{
    const QString oldEmpty = emptyPortSelectionText();
    english_ = !english_;
    const QString newEmpty = emptyPortSelectionText();

    auto swapPlaceholder = [&](QString &value) {
        if (value.isEmpty() || value == oldEmpty) {
            value = newEmpty;
        }
    };

    swapPlaceholder(gnss_port_);
    swapPlaceholder(imu_port_);
    swapPlaceholder(ptb_port_);
    swapPlaceholder(hmp_port_);
    swapPlaceholder(lidar_port_);

    emit englishChanged();
    emit portSelectionChanged();
    refreshPorts();
    updateStatusPresentation();
    appendLog(english_ ? "Language switched to English" : "语言已切换为中文");
}

void QmlAppController::refreshPorts()
{
    const QString previousEmpty = port_options_.isEmpty() ? emptyPortSelectionText() : port_options_.front();
    port_options_.clear();
    port_options_.append(emptyPortSelectionText());
    port_options_.append(currentPortNames());
    emit portOptionsChanged();

    auto preserveSelection = [&](QString &value) {
        if (value.isEmpty() || value == previousEmpty) {
            value = emptyPortSelectionText();
        }
    };

    preserveSelection(gnss_port_);
    preserveSelection(imu_port_);
    preserveSelection(ptb_port_);
    preserveSelection(hmp_port_);
    preserveSelection(lidar_port_);

    emit portSelectionChanged();
    appendLog(QString(english_ ? "Ports refreshed: %1 serial ports" : "端口已刷新: %1 个串口")
                  .arg(port_options_.size() > 0 ? port_options_.size() - 1 : 0));
}

void QmlAppController::autoDetectPorts()
{
    if (connected_ || connection_attempt_in_progress_ || port_detection_in_progress_) {
        return;
    }

    if (port_detection_thread_.joinable()) {
        port_detection_thread_.join();
    }

    refreshPorts();
    port_detection_in_progress_ = true;
    emit connectionStateChanged();
    updateStatusPresentation();
    appendLog(english_ ? "Starting automatic serial-port detection..." : "开始自动识别串口...");

    port_detection_thread_ = std::thread([this]() {
        struct ProbeSpec
        {
            QString key;
            QString label;
            QString baudText;
            std::function<bool(const QString &)> probe;
        };

        struct DetectionResult
        {
            QString key;
            QString portName;
            QString baudText;
        };

        const bool english = english_;
        auto postLog = [this](const QString &message) {
            QMetaObject::invokeMethod(this, [this, message]() { appendLog(message); }, Qt::QueuedConnection);
        };
        auto finishOnUi = [this](QVector<DetectionResult> detections) {
            QMetaObject::invokeMethod(
                this,
                [this, detections = std::move(detections)]() {
                    for (const DetectionResult &detection : detections) {
                        if (detection.key == "gnss") {
                            gnss_port_ = detection.portName;
                            gnss_baud_ = detection.baudText;
                        } else if (detection.key == "imu") {
                            imu_port_ = detection.portName;
                            imu_baud_ = detection.baudText;
                        } else if (detection.key == "ptb") {
                            ptb_port_ = detection.portName;
                            ptb_baud_ = detection.baudText;
                        } else if (detection.key == "hmp") {
                            hmp_port_ = detection.portName;
                            hmp_baud_ = detection.baudText;
                        } else if (detection.key == "lidar") {
                            lidar_port_ = detection.portName;
                            lidar_baud_ = detection.baudText;
                        }
                    }

                    port_detection_in_progress_ = false;
                    emit portSelectionChanged();
                    emit baudSelectionChanged();
                    emit connectionStateChanged();
                    updateStatusPresentation();
                },
                Qt::QueuedConnection);
        };

        auto probeCollector = [](const QString &portName, auto &&collector, const VaporView::SerialConfig &config) {
            if (!collector->start(portName.toStdString(), config)) {
                return false;
            }
            const bool responded = collector->checkDeviceResponse();
            collector->stop();
            return responded;
        };

        QVector<ProbeSpec> probeSpecs = {
            {"gnss", "GNSS", "115200", [probeCollector](const QString &portName) {
                 auto collector = std::make_unique<VaporView::GnssCollector>();
                 return probeCollector(portName, std::move(collector), VaporView::SerialConfig::N81(115200));
             }},
            {"imu", "IMU", "115200", [probeCollector](const QString &portName) {
                 auto collector = std::make_unique<VaporView::ImuCollector>();
                 return probeCollector(portName, std::move(collector), VaporView::SerialConfig::N81(115200));
             }},
            {"lidar", "TF03", "115200", [probeCollector](const QString &portName) {
                 auto collector = std::make_unique<VaporView::LidarCollector>();
                 return probeCollector(portName, std::move(collector), VaporView::SerialConfig::N81(115200));
             }},
            {"ptb", "PTB210", "9600", [probeCollector](const QString &portName) {
                 auto collector = std::make_unique<VaporView::PtbCollector>();
                 return probeCollector(portName, std::move(collector), VaporView::SerialConfig::E71(9600));
             }},
            {"hmp", "HMP3", "19200", [probeCollector](const QString &portName) {
                 auto collector = std::make_unique<VaporView::HmpCollector>();
                 return probeCollector(portName, std::move(collector), VaporView::SerialConfig::N82(19200));
             }}};

        const QStringList portNames = currentPortNames();
        if (portNames.isEmpty()) {
            postLog(english ? "Auto detect stopped: no serial ports found." : "自动识别结束：当前没有发现可用串口。");
            finishOnUi({});
            return;
        }

        QVector<DetectionResult> detections;
        postLog(QString(english ? "Auto detect: probing %1 serial ports..." : "自动识别：开始探测 %1 个串口...")
                    .arg(portNames.size()));

        for (const QString &portName : portNames) {
            bool matched = false;

            for (const ProbeSpec &spec : probeSpecs) {
                const bool alreadyDetected = std::any_of(
                    detections.cbegin(),
                    detections.cend(),
                    [&spec](const DetectionResult &result) { return result.key == spec.key; });
                if (alreadyDetected) {
                    continue;
                }

                postLog(QString(english ? "[Auto Detect] Probing %1 on %2 @ %3..." : "[自动识别] 正在探测 %1: %2 @ %3 ...")
                            .arg(spec.label, portName, spec.baudText));

                if (!spec.probe(portName)) {
                    continue;
                }

                detections.push_back({spec.key, portName, spec.baudText});
                postLog(QString(english ? "[Auto Detect] Identified %1 on %2 @ %3" : "[自动识别] 已识别 %1: %2 @ %3")
                            .arg(spec.label, portName, spec.baudText));
                matched = true;
                break;
            }

            if (!matched) {
                postLog(QString(english ? "[Auto Detect] No known device signature found on %1" : "[自动识别] 未在 %1 上识别到已知设备")
                            .arg(portName));
            }
        }

        for (const ProbeSpec &spec : probeSpecs) {
            const bool found = std::any_of(
                detections.cbegin(),
                detections.cend(),
                [&spec](const DetectionResult &result) { return result.key == spec.key; });
            if (!found) {
                postLog(QString(english ? "[Auto Detect] %1 not found" : "[自动识别] 未找到 %1").arg(spec.label));
            }
        }

        postLog(QString(english ? "Auto detect finished: identified %1 device(s)." : "自动识别完成：共识别出 %1 个设备。")
                    .arg(detections.size()));
        finishOnUi(std::move(detections));
    });
}

void QmlAppController::connectDevices()
{
    if (connection_thread_.joinable()) {
        connection_thread_.join();
    }

    connection_attempt_in_progress_ = true;
    cancel_connection_requested_.store(false);
    emit connectionStateChanged();
    updateStatusPresentation();
    appendLog(english_ ? "Connecting..." : "正在连接...");

    resetSensorData();

    const bool english = english_;
    const QString selectText = emptyPortSelectionText();
    const QString gnssPort = normalizePortSelection(gnss_port_);
    const QString imuPort = normalizePortSelection(imu_port_);
    const QString ptbPort = normalizePortSelection(ptb_port_);
    const QString hmpPort = normalizePortSelection(hmp_port_);
    const QString lidarPort = normalizePortSelection(lidar_port_);
    const QString gnssBaudText = gnss_baud_;
    const QString imuBaudText = imu_baud_;
    const QString ptbBaudText = ptb_baud_;
    const QString hmpBaudText = hmp_baud_;
    const QString lidarBaudText = lidar_baud_;
    const int gnssRate = parseRate(gnss_rate_);
    const int imuRate = parseRate(imu_rate_);
    const int ptbRate = parseRate(ptb_rate_);
    const int hmpRate = parseRate(hmp_rate_);
    const int lidarRate = std::min(parseRate(lidar_rate_), 100);

    stopAllCollectors();

    connection_thread_ = std::thread([this,
                                      english,
                                      selectText,
                                      gnssPort,
                                      imuPort,
                                      ptbPort,
                                      hmpPort,
                                      lidarPort,
                                      gnssBaudText,
                                      imuBaudText,
                                      ptbBaudText,
                                      hmpBaudText,
                                      lidarBaudText,
                                      gnssRate,
                                      imuRate,
                                      ptbRate,
                                      hmpRate,
                                      lidarRate]() {
        auto postLog = [this](const QString &message) {
            QMetaObject::invokeMethod(this, [this, message]() { appendLog(message); }, Qt::QueuedConnection);
        };
        auto finishOnUi = [this](bool connected) {
            QMetaObject::invokeMethod(this, [this, connected]() { finishConnectionAttempt(connected); }, Qt::QueuedConnection);
        };

        CollectorSnapshot collectors;
        collectors.gnss = std::make_shared<VaporView::GnssCollector>();
        collectors.imu = std::make_shared<VaporView::ImuCollector>();
        collectors.ptb = std::make_shared<VaporView::PtbCollector>();
        collectors.hmp = std::make_shared<VaporView::HmpCollector>();
        collectors.lidar = std::make_shared<VaporView::LidarCollector>();
        setCollectors(collectors);

        auto logCallback = [this](const std::string &msg) {
            const QString qmsg = QString::fromStdString(msg);
            QMetaObject::invokeMethod(this, [this, qmsg]() { appendLog(qmsg); }, Qt::QueuedConnection);
        };
        auto cancelCallback = [this]() { return cancel_connection_requested_.load(); };

        collectors.gnss->setSampleRate(gnssRate);
        collectors.imu->setSampleRate(imuRate);
        collectors.ptb->setSampleRate(ptbRate);
        collectors.hmp->setSampleRate(hmpRate);
        collectors.lidar->setSampleRate(lidarRate);

        collectors.gnss->setLogCallback(logCallback);
        collectors.imu->setLogCallback(logCallback);
        collectors.ptb->setLogCallback(logCallback);
        collectors.hmp->setLogCallback(logCallback);
        collectors.lidar->setLogCallback(logCallback);

        collectors.gnss->setCancelCallback(cancelCallback);
        collectors.imu->setCancelCallback(cancelCallback);
        collectors.ptb->setCancelCallback(cancelCallback);
        collectors.hmp->setCancelCallback(cancelCallback);
        collectors.lidar->setCancelCallback(cancelCallback);

        int totalDevices = 0;
        int connectedDevices = 0;

        auto cancelAttempt = [&]() {
            stopAllCollectors();
            postLog(english ? "Connection canceled" : "连接已取消");
            finishOnUi(false);
        };
        auto abortIfRequested = [&]() {
            if (!shouldAbortConnectionAttempt()) {
                return false;
            }
            cancelAttempt();
            return true;
        };
        auto connectCollector = [&](const QString &tag,
                                    const QString &port,
                                    const QString &baudText,
                                    auto *collector,
                                    const VaporView::SerialConfig &config,
                                    auto &&onReady) -> int {
            if (port == selectText || port.isEmpty()) {
                postLog(QString(english ? "[%1] Skipped (not selected)" : "[%1] 跳过 (未选择)").arg(tag));
                return 0;
            }

            totalDevices++;
            postLog(QString(english ? "[%1] Checking port: %2" : "[%1] 检查端口: %2").arg(tag, port));
            if (abortIfRequested()) {
                return -1;
            }

            if (!collector->start(port.toStdString(), config)) {
                postLog(QString(english ? "[%1] Failed to open port: %2" : "[%1] 打开端口失败: %2")
                            .arg(tag, QString::fromStdString(collector->getLastError())));
                return 0;
            }

            postLog(QString(english ? "[%1] Serial port opened, checking device response..." : "[%1] 串口已打开，正在检测设备响应...")
                        .arg(tag));
            if (abortIfRequested()) {
                return -1;
            }

            if (!collector->checkDeviceResponse()) {
                if (abortIfRequested()) {
                    return -1;
                }
                postLog(QString(english ? "[%1] Device not responding! Check power and cables." : "[%1] 设备无响应！请检查电源和连接线。")
                            .arg(tag));
                collector->stop();
                return 0;
            }

            postLog(QString(english ? "[%1] Device responding, connected: %2 @ %3 baud" : "[%1] 设备响应正常，连接成功: %2 @ %3 波特率")
                        .arg(tag, port, baudText));
            if (!onReady()) {
                collector->stop();
                return 0;
            }

            connectedDevices++;
            return 1;
        };

        postLog(english ? "========== Starting Connection ==========" : "========== 开始连接 ==========");
        if (abortIfRequested()) {
            return;
        }

        if (connectCollector("GNSS",
                             gnssPort,
                             gnssBaudText,
                             collectors.gnss.get(),
                             VaporView::SerialConfig::N81(gnssBaudText.toInt()),
                             [&]() {
                                 collectors.gnss->setDataCallback([this]() {
                                     QMetaObject::invokeMethod(this, [this]() {
                                         const CollectorSnapshot snapshot = snapshotCollectors();
                                         if (snapshot.gnss) {
                                             current_gnss_ = snapshot.gnss->getLatestData();
                                         }
                                     }, Qt::QueuedConnection);
                                 });
                                 collectors.gnss->setDeviceSampleRate(gnssRate);
                                 postLog(QString(english ? "[GNSS] Sample rate set to %1 Hz" : "[GNSS] 采样频率设置为 %1 Hz").arg(gnssRate));
                                 if (collectors.gnss->startStreaming()) {
                                     return true;
                                 }
                                 postLog(english ? "[GNSS] Failed to start data stream." : "[GNSS] 启动数据流失败。");
                                 return false;
                             }) < 0) {
            return;
        }

        if (connectCollector("IMU",
                             imuPort,
                             imuBaudText,
                             collectors.imu.get(),
                             VaporView::SerialConfig::N81(imuBaudText.toInt()),
                             [&]() {
                                 collectors.imu->setDataCallback([this]() {
                                     QMetaObject::invokeMethod(this, [this]() {
                                         const CollectorSnapshot snapshot = snapshotCollectors();
                                         if (snapshot.imu) {
                                             current_imu_ = snapshot.imu->getLatestData();
                                         }
                                     }, Qt::QueuedConnection);
                                 });
                                 collectors.imu->setDeviceSampleRate(imuRate);
                                 postLog(QString(english ? "[IMU] Sample rate set to %1 Hz" : "[IMU] 采样频率设置为 %1 Hz").arg(imuRate));
                                 if (collectors.imu->startStreaming()) {
                                     return true;
                                 }
                                 postLog(english ? "[IMU] Failed to start data stream." : "[IMU] 启动数据流失败。");
                                 return false;
                             }) < 0) {
            return;
        }

        if (connectCollector("PTB",
                             ptbPort,
                             ptbBaudText,
                             collectors.ptb.get(),
                             VaporView::SerialConfig::E71(ptbBaudText.toInt()),
                             [&]() {
                                 collectors.ptb->setDataCallback([this]() {
                                     QMetaObject::invokeMethod(this, [this]() {
                                         const CollectorSnapshot snapshot = snapshotCollectors();
                                         if (snapshot.ptb) {
                                             current_ptb_ = snapshot.ptb->getLatestData();
                                         }
                                     }, Qt::QueuedConnection);
                                 });
                                 collectors.ptb->setDeviceSampleRate(ptbRate);
                                 postLog(QString(english ? "[PTB] Sample rate set to %1 Hz" : "[PTB] 采样频率设置为 %1 Hz").arg(ptbRate));
                                 if (collectors.ptb->startStreaming()) {
                                     return true;
                                 }
                                 postLog(english ? "[PTB] Failed to start data stream." : "[PTB] 启动数据流失败。");
                                 return false;
                             }) < 0) {
            return;
        }

        if (connectCollector("HMP",
                             hmpPort,
                             hmpBaudText,
                             collectors.hmp.get(),
                             VaporView::SerialConfig::N82(hmpBaudText.toInt()),
                             [&]() {
                                 collectors.hmp->setDataCallback([this]() {
                                     QMetaObject::invokeMethod(this, [this]() {
                                         const CollectorSnapshot snapshot = snapshotCollectors();
                                         if (snapshot.hmp) {
                                             current_hmp_ = snapshot.hmp->getLatestData();
                                         }
                                     }, Qt::QueuedConnection);
                                 });
                                 postLog(QString(english ? "[HMP] Sample rate set to %1 Hz" : "[HMP] 采样频率设置为 %1 Hz").arg(hmpRate));
                                 if (collectors.hmp->startStreaming()) {
                                     return true;
                                 }
                                 postLog(english ? "[HMP] Failed to start data stream." : "[HMP] 启动数据流失败。");
                                 return false;
                             }) < 0) {
            return;
        }

        if (connectCollector("TF03",
                             lidarPort,
                             lidarBaudText,
                             collectors.lidar.get(),
                             VaporView::SerialConfig::N81(lidarBaudText.toInt()),
                             [&]() {
                                 collectors.lidar->setDataCallback([this]() {
                                     QMetaObject::invokeMethod(this, [this]() {
                                         const CollectorSnapshot snapshot = snapshotCollectors();
                                         if (snapshot.lidar) {
                                             current_lidar_ = snapshot.lidar->getLatestData();
                                         }
                                     }, Qt::QueuedConnection);
                                 });
                                 if (!collectors.lidar->setDeviceSampleRate(lidarRate)) {
                                     postLog(QString(english ? "[TF03] Failed to apply frame rate %1 Hz, using device default."
                                                             : "[TF03] 应用 %1 Hz 输出频率失败，使用设备默认频率。")
                                                 .arg(lidarRate));
                                 } else {
                                     postLog(QString(english ? "[TF03] Frame rate set to %1 Hz" : "[TF03] 输出频率设置为 %1 Hz").arg(lidarRate));
                                 }
                                 if (collectors.lidar->startStreaming()) {
                                     return true;
                                 }
                                 postLog(english ? "[TF03] Failed to start data stream." : "[TF03] 启动数据流失败。");
                                 return false;
                             }) < 0) {
            return;
        }

        postLog(QString(english ? "========== Connection Summary: %1/%2 devices connected =========="
                                : "========== 连接摘要: %1/%2 设备已连接 ==========")
                    .arg(connectedDevices)
                    .arg(totalDevices));
        if (connectedDevices == 0) {
            postLog(english ? "No ports connected" : "没有端口连接成功");
            finishOnUi(false);
            return;
        }

        finishOnUi(true);
    });
}

void QmlAppController::disconnectDevices()
{
    appendLog(english_ ? "Disconnecting..." : "正在断开...");
    stopAllCollectors();
    if (connection_thread_.joinable()) {
        connection_thread_.join();
    }
    finishConnectionAttempt(false);
    appendLog(english_ ? "Disconnected" : "已断开");
}

void QmlAppController::cancelConnect()
{
    if (!connection_attempt_in_progress_) {
        return;
    }

    cancel_connection_requested_.store(true);
    appendLog(english_ ? "Cancel requested, stopping connection attempt..." : "已请求取消，正在停止连接流程...");
}

void QmlAppController::openRtkConfig()
{
    appendLog(english_ ? "RTK tools are now available in the Fluent RTK page." : "RTK 工具已经迁移到新的 Fluent RTK 页面。");
}

void QmlAppController::openSessionViewer()
{
    appendLog(english_ ? "Session tools are now available in the Fluent session page." : "会话工具已经迁移到新的 Fluent 会话页面。");
}

QString QmlAppController::emptyPortSelectionText() const
{
    return english_ ? "-- Select --" : "-- 选择 --";
}

QString QmlAppController::normalizePortSelection(const QString &value) const
{
    if (value.isEmpty() || value == emptyPortSelectionText()) {
        return emptyPortSelectionText();
    }
    return value.trimmed();
}

void QmlAppController::assignPortValue(QString &target, const QString &value)
{
    const QString normalized = normalizePortSelection(value);
    if (target == normalized) {
        return;
    }
    target = normalized;
    emit portSelectionChanged();
}

void QmlAppController::assignTextValue(QString &target, const QString &value, void (QmlAppController::*signal)())
{
    const QString trimmed = value.trimmed();
    if (target == trimmed) {
        return;
    }
    target = trimmed;
    (this->*signal)();
}

QStringList QmlAppController::currentPortNames() const
{
    QStringList ports;
    const auto infos = QSerialPortInfo::availablePorts();
    for (const QSerialPortInfo &info : infos) {
        ports.append(info.portName());
    }
    ports.removeDuplicates();
    ports.sort();
    return ports;
}

int QmlAppController::parseRate(const QString &text)
{
    bool ok = false;
    const int rate = text.toInt(&ok);
    if (ok && rate >= 1 && rate <= 500) {
        return rate;
    }
    return 20;
}

void QmlAppController::appendLog(const QString &message)
{
    QStringList entries = log_model_.stringList();
    const QString timestamp = QDateTime::currentDateTime().toString("hh:mm:ss");
    entries.append(QString("[%1] %2").arg(timestamp, message));
    log_model_.setStringList(entries);
    trimLogs();
}

void QmlAppController::trimLogs()
{
    QStringList entries = log_model_.stringList();
    while (entries.size() > 300) {
        entries.removeFirst();
    }
    log_model_.setStringList(entries);
}

void QmlAppController::resetSensorData()
{
    current_gnss_ = VaporView::GnssData();
    current_imu_ = VaporView::ImuData();
    current_ptb_ = VaporView::PtbData();
    current_hmp_ = VaporView::HmpData();
    current_lidar_ = VaporView::LidarData();
    refreshSensorCards();
}

void QmlAppController::refreshSensorCards()
{
    const CollectorSnapshot collectors = snapshotCollectors();
    const QVariantMap nextGnss = makeGnssMap(collectors);
    const QVariantMap nextImu = makeImuMap(collectors);
    const QVariantMap nextPtb = makePtbMap(collectors);
    const QVariantMap nextHmp = makeHmpMap(collectors);
    const QVariantMap nextLidar = makeLidarMap(collectors);

    if (nextGnss != gnss_map_ || nextImu != imu_map_ || nextPtb != ptb_map_ || nextHmp != hmp_map_ || nextLidar != lidar_map_) {
        gnss_map_ = nextGnss;
        imu_map_ = nextImu;
        ptb_map_ = nextPtb;
        hmp_map_ = nextHmp;
        lidar_map_ = nextLidar;
        emit sensorDataChanged();
    }
}

void QmlAppController::updateStatusPresentation()
{
    QString nextText;
    QString nextKind;
    if (port_detection_in_progress_) {
        nextText = english_ ? "Detecting ports..." : "正在识别串口...";
        nextKind = "detecting";
    } else if (connection_attempt_in_progress_) {
        nextText = english_ ? "Connecting..." : "正在连接...";
        nextKind = "connecting";
    } else if (connected_) {
        nextText = english_ ? "Connected" : "已连接";
        nextKind = "connected";
    } else {
        nextText = english_ ? "Disconnected" : "未连接";
        nextKind = "disconnected";
    }

    if (status_text_ != nextText || status_kind_ != nextKind) {
        status_text_ = nextText;
        status_kind_ = nextKind;
        emit statusTextChanged();
    }
}

QmlAppController::CollectorSnapshot QmlAppController::snapshotCollectors() const
{
    std::lock_guard<std::mutex> lock(collector_mutex_);
    return {gnss_collector_, imu_collector_, ptb_collector_, hmp_collector_, lidar_collector_};
}

void QmlAppController::setCollectors(CollectorSnapshot collectors)
{
    std::lock_guard<std::mutex> lock(collector_mutex_);
    gnss_collector_ = std::move(collectors.gnss);
    imu_collector_ = std::move(collectors.imu);
    ptb_collector_ = std::move(collectors.ptb);
    hmp_collector_ = std::move(collectors.hmp);
    lidar_collector_ = std::move(collectors.lidar);
}

void QmlAppController::stopAllCollectors()
{
    CollectorSnapshot collectors;
    {
        std::lock_guard<std::mutex> lock(collector_mutex_);
        collectors.gnss = std::move(gnss_collector_);
        collectors.imu = std::move(imu_collector_);
        collectors.ptb = std::move(ptb_collector_);
        collectors.hmp = std::move(hmp_collector_);
        collectors.lidar = std::move(lidar_collector_);
    }

    if (collectors.gnss) {
        collectors.gnss->stop();
    }
    if (collectors.imu) {
        collectors.imu->stop();
    }
    if (collectors.ptb) {
        collectors.ptb->stop();
    }
    if (collectors.hmp) {
        collectors.hmp->stop();
    }
    if (collectors.lidar) {
        collectors.lidar->stop();
    }
}

bool QmlAppController::shouldAbortConnectionAttempt() const
{
    return cancel_connection_requested_.load();
}

void QmlAppController::finishConnectionAttempt(bool connected)
{
    connection_attempt_in_progress_ = false;
    cancel_connection_requested_.store(false);
    connected_ = connected;
    emit connectionStateChanged();
    updateStatusPresentation();
    refreshSensorCards();
}

void QmlAppController::joinThreads()
{
    if (connection_thread_.joinable()) {
        connection_thread_.join();
    }
    if (port_detection_thread_.joinable()) {
        port_detection_thread_.join();
    }
}

QVariantMap QmlAppController::makeGnssMap(const CollectorSnapshot &collectors) const
{
    return {
        {"status", QString::fromStdString(current_gnss_.position_status.empty() ? (current_gnss_.valid ? "OK" : "Idle") : current_gnss_.position_status)},
        {"latitude", current_gnss_.latitude},
        {"longitude", current_gnss_.longitude},
        {"altitude", current_gnss_.altitude},
        {"heading", current_gnss_.heading},
        {"groundSpeed", current_gnss_.vel_ground},
        {"satellites", current_gnss_.num_satellites_used},
        {"rate", collectors.gnss ? collectors.gnss->getActualRate() : 0.0},
        {"valid", current_gnss_.valid}
    };
}

QVariantMap QmlAppController::makeImuMap(const CollectorSnapshot &collectors) const
{
    return {
        {"source", current_imu_.from_hi83 ? "HI-83" : "HPT-900"},
        {"roll", current_imu_.rpy[0]},
        {"pitch", current_imu_.rpy[1]},
        {"yaw", current_imu_.rpy[2]},
        {"temperature", current_imu_.temperature},
        {"pressure", current_imu_.air_pressure},
        {"rate", collectors.imu ? collectors.imu->getActualRate() : 0.0},
        {"valid", current_imu_.valid}
    };
}

QVariantMap QmlAppController::makePtbMap(const CollectorSnapshot &collectors) const
{
    return {
        {"pressure", current_ptb_.pressure_hpa},
        {"status", QString::fromStdString(current_ptb_.error_message.empty() ? (current_ptb_.valid ? "OK" : "Idle") : current_ptb_.error_message)},
        {"rate", collectors.ptb ? collectors.ptb->getActualRate() : 0.0},
        {"valid", current_ptb_.valid}
    };
}

QVariantMap QmlAppController::makeHmpMap(const CollectorSnapshot &collectors) const
{
    return {
        {"temperature", current_hmp_.temperature},
        {"humidity", current_hmp_.humidity},
        {"status", QString::fromStdString(current_hmp_.error_message.empty() ? (current_hmp_.valid ? "OK" : "Idle") : current_hmp_.error_message)},
        {"rate", collectors.hmp ? collectors.hmp->getActualRate() : 0.0},
        {"valid", current_hmp_.valid}
    };
}

QVariantMap QmlAppController::makeLidarMap(const CollectorSnapshot &collectors) const
{
    return {
        {"distance", current_lidar_.distance_m},
        {"strength", current_lidar_.signal_strength},
        {"status", QString::fromStdString(current_lidar_.error_message.empty() ? (current_lidar_.valid ? "OK" : "Idle") : current_lidar_.error_message)},
        {"rate", collectors.lidar ? collectors.lidar->getActualRate() : 0.0},
        {"valid", current_lidar_.valid}
    };
}
