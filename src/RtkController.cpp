#include "RtkController.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QFile>
#include <QMetaObject>
#include <QPointer>
#include <QRegularExpression>
#include <QSerialPortInfo>
#include <QSettings>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTextStream>

#include <algorithm>

namespace
{
constexpr int kGgaPollIntervalMs = 50;
constexpr int kGgaReconnectIntervalMs = 1500;
constexpr int kGgaStaleTimeoutMs = 1500;
constexpr int kGgaMaxVisibleLines = 200;
constexpr int kRtkLogVisibleLines = 300;
constexpr int kRtkHttpTimeoutMs = 5000;
const QRegularExpression kGgaSentencePattern("^\\$..GGA,");

struct HttpResponse
{
    int statusCode = 0;
    QString body;
    QString error;
    bool timedOut = false;
};

struct MountpointFetchResult
{
    HttpResponse response;
    QStringList mountpoints;
};

QString buildMockGgaSentence()
{
    const QTime utc = QDateTime::currentDateTimeUtc().time();
    const QString timeField = QStringLiteral("%1%2%3.%4")
                                  .arg(utc.hour(), 2, 10, QLatin1Char('0'))
                                  .arg(utc.minute(), 2, 10, QLatin1Char('0'))
                                  .arg(utc.second(), 2, 10, QLatin1Char('0'))
                                  .arg(utc.msec() / 10, 2, 10, QLatin1Char('0'));

    const QString body = QStringLiteral("GPGGA,%1,3000.0000,N,12000.0000,E,1,12,1.0,0.0,M,0.0,M,,").arg(timeField);

    unsigned char checksum = 0;
    const QByteArray bytes = body.toLatin1();
    for (char ch : bytes) {
        checksum ^= static_cast<unsigned char>(ch);
    }

    return QStringLiteral("$%1*%2").arg(body).arg(static_cast<int>(checksum), 2, 16, QLatin1Char('0')).toUpper();
}

QString formatRtkStatusLine(const RtkStreamStats &stats, const QString &fallbackMessage)
{
    const QString message = stats.message.isEmpty() ? fallbackMessage : stats.message;
    return QString("%1 [%2] %3 B %4 bps %5")
        .arg(QDateTime::currentDateTime().toString("yyyy/MM/dd hh:mm:ss"))
        .arg(stats.streamStateMask.isEmpty() ? QStringLiteral("-----") : stats.streamStateMask)
        .arg(QString::number(stats.inputBytes).rightJustified(10, QLatin1Char(' ')))
        .arg(QString::number(stats.inputBps).rightJustified(7, QLatin1Char(' ')))
        .arg(message);
}

QUrl buildRtkUrl(const QString &server, const QString &port, const QString &path = QString())
{
    QUrl url;
    url.setScheme(QStringLiteral("http"));
    url.setHost(server.trimmed());
    bool portOk = false;
    const int parsedPort = port.trimmed().toInt(&portOk);
    if (portOk) {
        url.setPort(parsedPort);
    }
    url.setPath(path.isEmpty() ? QStringLiteral("/") : QStringLiteral("/") + path);
    return url;
}

HttpResponse performRtkHttpGet(const QUrl &url, const QString &username, const QString &password,
                               const QString &acceptHeader = QStringLiteral("*/*"))
{
    HttpResponse result;
    QTcpSocket socket;

    const QString host = url.host().trimmed();
    const int port = url.port(80);
    QString path = url.path();
    if (path.isEmpty()) {
        path = QStringLiteral("/");
    }
    if (!url.query().isEmpty()) {
        path += QStringLiteral("?") + url.query();
    }

    socket.connectToHost(host, static_cast<quint16>(port));
    if (!socket.waitForConnected(kRtkHttpTimeoutMs)) {
        result.error = socket.errorString();
        result.timedOut = (socket.error() == QAbstractSocket::SocketTimeoutError);
        return result;
    }

    QByteArray requestData;
    requestData += "GET " + path.toUtf8() + " HTTP/1.0\r\n";
    requestData += "Host: " + host.toUtf8() + ":" + QByteArray::number(port) + "\r\n";
    requestData += "User-Agent: NTRIP VaporView/1.0\r\n";
    requestData += "Ntrip-Version: Ntrip/2.0\r\n";
    requestData += "Connection: close\r\n";
    requestData += "Accept: " + acceptHeader.toUtf8() + "\r\n";

    if (!username.trimmed().isEmpty()) {
        const QByteArray credentials = QStringLiteral("%1:%2").arg(username.trimmed(), password).toUtf8().toBase64();
        requestData += "Authorization: Basic " + credentials + "\r\n";
    }

    requestData += "\r\n";

    if (socket.write(requestData) != requestData.size() || !socket.waitForBytesWritten(kRtkHttpTimeoutMs)) {
        result.error = socket.errorString();
        result.timedOut = (socket.error() == QAbstractSocket::SocketTimeoutError);
        return result;
    }

    QByteArray rawResponse;
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < kRtkHttpTimeoutMs) {
        if (socket.waitForReadyRead(200)) {
            rawResponse += socket.readAll();
            while (socket.bytesAvailable() > 0) {
                rawResponse += socket.readAll();
            }
        }
        if (socket.state() == QAbstractSocket::UnconnectedState) {
            break;
        }
    }

    rawResponse += socket.readAll();
    socket.disconnectFromHost();

    if (rawResponse.isEmpty()) {
        result.error = QStringLiteral("No response from server");
        return result;
    }

    auto parseStatusCode = [](const QByteArray &statusLine) {
        const QRegularExpression statusPattern(QStringLiteral("(^|\\s)(\\d{3})(\\s|$)"));
        const QRegularExpressionMatch match = statusPattern.match(QString::fromLatin1(statusLine));
        return match.hasMatch() ? match.captured(2).toInt() : 0;
    };

    const int headerEnd = rawResponse.indexOf("\r\n\r\n");
    QByteArray headerBytes = rawResponse;
    QByteArray bodyBytes;
    if (headerEnd >= 0) {
        headerBytes = rawResponse.left(headerEnd);
        bodyBytes = rawResponse.mid(headerEnd + 4);
    }

    const QList<QByteArray> headerLines = headerBytes.split('\n');
    const QByteArray statusLine = headerLines.isEmpty() ? QByteArray() : headerLines.first().trimmed();
    result.statusCode = parseStatusCode(statusLine);

    if (headerEnd >= 0) {
        result.body = QString::fromLatin1(bodyBytes);
    } else if (statusLine.startsWith("STR;") || statusLine.startsWith("CAS;") || statusLine.startsWith("NET;")) {
        result.statusCode = 200;
        result.body = QString::fromLatin1(rawResponse);
    } else {
        result.body = QString::fromLatin1(rawResponse);
    }

    return result;
}

QStringList parseMountpoints(const QString &responseBody)
{
    QStringList mountpoints;
    const QStringList lines = responseBody.split(QRegularExpression("[\r\n]+"), Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        if (!line.startsWith(QStringLiteral("STR;"))) {
            continue;
        }
        const QStringList parts = line.split(';');
        if (parts.size() > 1 && !parts.at(1).trimmed().isEmpty()) {
            mountpoints.append(parts.at(1).trimmed());
        }
    }
    mountpoints.removeDuplicates();
    mountpoints.sort();
    return mountpoints;
}
}

RtkController::RtkController(QObject *parent)
    : QObject(parent)
    , rtk_service_(std::make_unique<RtkStreamService>())
{
    baud_options_ = {"9600", "19200", "38400", "57600", "115200", "230400", "460800", "921600"};
    timing_options_ = {"1000", "2000", "5000", "10000", "30000", "60000"};

    port_ = QStringLiteral("2101");
    baudrate_ = QStringLiteral("115200");
    timeout_ms_ = QStringLiteral("5000");
    reconnect_ms_ = QStringLiteral("1000");

    connect(&status_timer_, &QTimer::timeout, this, [this]() { pollRtkServiceStatus(false); });
    status_timer_.setInterval(1000);

    connect(&gga_poll_timer_, &QTimer::timeout, this, [this]() {
        if (!tryOpenGgaPort()) {
            return;
        }

        char buffer[512];
        while (true) {
            const ssize_t bytesRead = gga_serial_.read(buffer, sizeof(buffer));
            if (bytesRead > 0) {
                gga_buffer_.append(QString::fromLatin1(buffer, static_cast<int>(bytesRead)));
                processGgaBuffer();
                continue;
            }
            if (bytesRead < 0) {
                gga_serial_.close();
                updateGgaFrequency(0.0);
                updateGgaStatusLabel(textFor("Status: %1 read failed, reconnecting...",
                                             "状态: %1 读取失败，正在重连...")
                                         .arg(ggaPortName()),
                                     false);
            }
            break;
        }

        if (gga_has_sentence_time_) {
            const auto now = std::chrono::steady_clock::now();
            const auto staleMs =
                std::chrono::duration_cast<std::chrono::milliseconds>(now - gga_last_sentence_time_).count();
            if (staleMs > kGgaStaleTimeoutMs) {
                gga_recent_intervals_sec_.clear();
                updateGgaFrequency(0.0);
                updateGgaStatusLabel(textFor("Status: Waiting for next GGA sentence", "状态: 正在等待下一条 GGA 语句"),
                                     false);
            }
        }
    });
    gga_poll_timer_.setInterval(kGgaPollIntervalMs);

    loadSettings();
    refreshPorts();
    updateGgaFrequency(0.0);
    updateGgaStatusLabel(textFor("Status: Click button to read GGA", "状态: 点击按钮开始读取GGA"), false);
    updateStatusText();
}

RtkController::~RtkController()
{
    shutdown_requested_.store(true);
    stopService();
    stopGgaMonitorInternal();
    joinBackgroundThreads();
    saveSettings();
}

bool RtkController::english() const { return english_; }
bool RtkController::running() const { return running_; }
bool RtkController::busy() const { return fetch_mountpoints_in_progress_.load() || test_in_progress_.load(); }
bool RtkController::fetchInProgress() const { return fetch_mountpoints_in_progress_.load(); }
bool RtkController::testInProgress() const { return test_in_progress_.load(); }
bool RtkController::ggaMonitorEnabled() const { return gga_monitor_enabled_; }
QString RtkController::statusText() const { return status_text_; }
QString RtkController::ggaStatusText() const { return gga_status_text_; }
QString RtkController::ggaFrequencyText() const { return gga_frequency_text_; }
QStringList RtkController::portOptions() const { return port_options_; }
QStringList RtkController::mountpointOptions() const { return mountpoint_options_; }
QStringList RtkController::baudOptions() const { return baud_options_; }
QStringList RtkController::timingOptions() const { return timing_options_; }
QString RtkController::server() const { return server_; }
QString RtkController::port() const { return port_; }
QString RtkController::username() const { return username_; }
QString RtkController::password() const { return password_; }
QString RtkController::mountpoint() const { return mountpoint_; }
QString RtkController::outputPort() const { return output_port_; }
QString RtkController::ggaPort() const { return gga_port_; }
QString RtkController::baudrate() const { return baudrate_; }
QString RtkController::timeoutMs() const { return timeout_ms_; }
QString RtkController::reconnectMs() const { return reconnect_ms_; }
QStringListModel *RtkController::logModel() { return &log_model_; }
QStringListModel *RtkController::ggaLogModel() { return &gga_log_model_; }

void RtkController::setEnglish(bool english)
{
    if (english_ == english) {
        return;
    }
    english_ = english;
    emit englishChanged();
    updateStatusText();
    updateGgaFrequency(0.0);
    if (gga_monitor_enabled_) {
        updateGgaStatusLabel(textFor("Status: Waiting for serial data", "状态: 正在等待串口数据"), false);
    } else {
        updateGgaStatusLabel(textFor("Status: Click button to read GGA", "状态: 点击按钮开始读取GGA"), false);
    }
}

void RtkController::setServer(const QString &value) { if (server_ != value) { server_ = value.trimmed(); emit configChanged(); } }
void RtkController::setPort(const QString &value) { if (port_ != value) { port_ = value.trimmed(); emit configChanged(); } }
void RtkController::setUsername(const QString &value) { if (username_ != value) { username_ = value.trimmed(); emit configChanged(); } }
void RtkController::setPassword(const QString &value) { if (password_ != value) { password_ = value; emit configChanged(); } }
void RtkController::setMountpoint(const QString &value) { if (mountpoint_ != value) { mountpoint_ = value.trimmed(); emit configChanged(); } }
void RtkController::setOutputPort(const QString &value) { if (output_port_ != value) { output_port_ = value.trimmed(); emit configChanged(); } }
void RtkController::setGgaPort(const QString &value) { if (gga_port_ != value) { gga_port_ = value.trimmed(); emit configChanged(); } }
void RtkController::setBaudrate(const QString &value) { if (baudrate_ != value) { baudrate_ = value.trimmed(); emit configChanged(); } }
void RtkController::setTimeoutMs(const QString &value) { if (timeout_ms_ != value) { timeout_ms_ = value.trimmed(); emit configChanged(); } }
void RtkController::setReconnectMs(const QString &value) { if (reconnect_ms_ != value) { reconnect_ms_ = value.trimmed(); emit configChanged(); } }

void RtkController::refreshPorts()
{
    port_options_ = availablePorts();
    emit portOptionsChanged();
    appendLog(textFor("Ports refreshed: %1 found", "串口已刷新: 发现 %1 个").arg(port_options_.size()));
}

void RtkController::fetchMountpoints()
{
    if (busy()) {
        return;
    }

    const QString host = server_.trimmed();
    const QString hostPort = port_.trimmed().isEmpty() ? QStringLiteral("2101") : port_.trimmed();
    if (host.isEmpty()) {
        appendLog(textFor("Please enter server address first.", "请先填写服务器地址。"));
        return;
    }

    if (fetch_mountpoints_thread_.joinable()) {
        fetch_mountpoints_thread_.join();
    }

    fetch_mountpoints_in_progress_.store(true);
    emit stateChanged();
    updateStatusText();
    appendLog(textFor("Fetching mountpoint list from %1:%2...", "正在从 %1:%2 获取挂载点列表...")
                  .arg(host, hostPort));

    QPointer<RtkController> self(this);
    fetch_mountpoints_thread_ = std::thread([self, host, hostPort]() {
        MountpointFetchResult result;
        if (self) {
            result.response = performRtkHttpGet(buildRtkUrl(host, hostPort), self->username(), self->password(),
                                                QStringLiteral("text/plain, */*"));
            if (!result.response.timedOut &&
                (result.response.error.isEmpty() || !result.response.body.trimmed().isEmpty())) {
                result.mountpoints = parseMountpoints(result.response.body);
            }
        }

        if (!self) {
            return;
        }

        QMetaObject::invokeMethod(self, [self, result = std::move(result)]() {
            if (!self) {
                return;
            }

            self->fetch_mountpoints_in_progress_.store(false);
            emit self->stateChanged();
            self->updateStatusText();

            const HttpResponse &response = result.response;
            if (response.timedOut || (!response.error.isEmpty() && response.body.trimmed().isEmpty())) {
                const QString errorText =
                    response.timedOut ? self->textFor("Request timed out", "请求超时") : response.error;
                self->appendLog(self->textFor("Failed to fetch mountpoint list: %1", "获取挂载点列表失败: %1")
                                    .arg(errorText));
                return;
            }

            self->updateMountpointOptions(result.mountpoints);
            if (result.mountpoints.isEmpty()) {
                self->appendLog(
                    self->textFor("No mountpoints found in sourcetable response.",
                                  "返回的源表中未找到挂载点。"));
                return;
            }

            if (self->mountpoint_.trimmed().isEmpty()) {
                self->mountpoint_ = result.mountpoints.first();
                emit self->configChanged();
            }

            self->appendLog(self->textFor("Fetched %1 mountpoints.", "已获取 %1 个挂载点。")
                                .arg(result.mountpoints.size()));
        }, Qt::QueuedConnection);
    });
}

void RtkController::startService()
{
    if (busy() || running_) {
        return;
    }

    RtkStreamConfig config;
    QString description;
    if (!buildConfig(&config, &description)) {
        appendLog(textFor("Please fill in server, mountpoint and output port.",
                          "请填写服务器、挂载点和输出串口。"));
        return;
    }

    appendLog(textFor("Starting RTK service...", "正在启动 RTK 服务..."));
    appendLog(description);

    QString errorMessage;
    if (rtk_service_ && rtk_service_->start(config, &errorMessage)) {
        running_ = true;
        emit stateChanged();
        updateStatusText();
        status_timer_.start();
        pollRtkServiceStatus(true);
        appendLog(textFor("RTK service started successfully", "RTK 服务启动成功"));
    } else {
        appendLog(textFor("Failed to start RTK service: %1", "RTK 服务启动失败: %1")
                      .arg(errorMessage.isEmpty() ? textFor("Unknown error", "未知错误") : errorMessage));
    }
}

void RtkController::stopService()
{
    if (rtk_service_ && rtk_service_->isRunning()) {
        appendLog(textFor("Stopping RTK service...", "正在停止 RTK 服务..."));
        rtk_service_->stop();
    }
    if (status_timer_.isActive()) {
        status_timer_.stop();
    }
    if (running_) {
        running_ = false;
        emit stateChanged();
        updateStatusText();
        appendLog(textFor("RTK service stopped", "RTK 服务已停止"));
    } else {
        updateStatusText();
    }
}

void RtkController::runNoSignalTest()
{
    if (busy() || running_) {
        return;
    }

    RtkStreamConfig config;
    QString description;
    if (!buildConfig(&config, &description)) {
        appendLog(textFor("Please fill in server, mountpoint and output port.",
                          "请填写服务器、挂载点和输出串口。"));
        return;
    }

    if (test_thread_.joinable()) {
        test_thread_.join();
    }

    appendLog(textFor("Starting no-signal RTK test...", "正在启动无信号 RTK 测试..."));
    appendLog(description);

    test_in_progress_.store(true);
    emit stateChanged();
    updateStatusText();

    QPointer<RtkController> self(this);
    test_thread_ = std::thread([self, config]() mutable {
        if (!self) {
            return;
        }

        auto queueLog = [self](const QString &message) {
            if (!self) {
                return;
            }
            QMetaObject::invokeMethod(self, [self, message]() {
                if (self) {
                    self->appendLog(message);
                }
            }, Qt::QueuedConnection);
        };

        auto queueRawLog = [self](const QString &message) {
            if (!self) {
                return;
            }
            QMetaObject::invokeMethod(self, [self, message]() {
                if (self) {
                    self->appendRawLogLine(message);
                }
            }, Qt::QueuedConnection);
        };

        NoSignalTestResult result;
        QTcpServer mockSerialServer;
        if (!mockSerialServer.listen(QHostAddress::LocalHost)) {
            result.startError = mockSerialServer.errorString();
        } else {
            RtkStreamConfig testConfig = config;
            testConfig.outputMode = RtkStreamConfig::OutputMode::TcpClient;
            testConfig.outputPathOverride = QStringLiteral("127.0.0.1:%1").arg(mockSerialServer.serverPort());
            testConfig.relayBack = 1;
            queueLog(self->textFor("Using loopback mock serial on 127.0.0.1:%1",
                                   "正在使用 127.0.0.1:%1 的 loopback 模拟串口")
                         .arg(mockSerialServer.serverPort()));

            std::unique_ptr<RtkStreamService> testService = std::make_unique<RtkStreamService>();
            QString errorMessage;
            if (!testService->start(testConfig, &errorMessage)) {
                result.startError =
                    errorMessage.isEmpty() ? self->textFor("Unknown error", "未知错误") : errorMessage;
            } else {
                QElapsedTimer timer;
                timer.start();
                RtkStreamStats finalStats;
                qint64 lastInjectMs = -1000;
                qint64 lastStatusLogMs = -1000;
                int rtcmResponseBursts = 0;
                std::unique_ptr<QTcpSocket> mockSerialPeer;

                while (!self->shutdown_requested_.load() && timer.elapsed() < 15000) {
                    finalStats = testService->stats();

                    if (!mockSerialPeer &&
                        (mockSerialServer.hasPendingConnections() || mockSerialServer.waitForNewConnection(100))) {
                        mockSerialPeer.reset(mockSerialServer.nextPendingConnection());
                        if (mockSerialPeer) {
                            queueLog(self->textFor("Mock serial loopback connected.", "模拟串口 loopback 已连接。"));
                        }
                    }

                    if (timer.elapsed() - lastStatusLogMs >= 1000) {
                        queueRawLog(formatRtkStatusLine(
                            finalStats, self->textFor("Running no-signal RTK test", "正在执行无信号 RTK 测试")));
                        lastStatusLogMs = timer.elapsed();
                    }

                    const QString messageLower = finalStats.message.toLower();
                    const bool stillConnecting = messageLower.contains(QStringLiteral("connecting")) ||
                                                 messageLower.contains(QStringLiteral("disconnected"));
                    if (!result.linkReady && mockSerialPeer &&
                        mockSerialPeer->state() == QAbstractSocket::ConnectedState && !stillConnecting) {
                        result.linkReady = true;
                        queueLog(self->textFor("Injecting GGA at 1 Hz: %1", "已按 1Hz 频率注入 GGA 数据: %1")
                                     .arg(buildMockGgaSentence()));
                    }

                    if (mockSerialPeer) {
                        if (mockSerialPeer->waitForReadyRead(20) || mockSerialPeer->bytesAvailable() > 0) {
                            QByteArray rtcmData = mockSerialPeer->readAll();
                            while (mockSerialPeer->bytesAvailable() > 0) {
                                rtcmData += mockSerialPeer->readAll();
                            }
                            if (!rtcmData.isEmpty()) {
                                result.receivedRtcmBytes += rtcmData.size();
                                ++rtcmResponseBursts;
                            }
                        }
                    }

                    if (result.linkReady && mockSerialPeer && timer.elapsed() - lastInjectMs >= 1000) {
                        QByteArray payload = buildMockGgaSentence().toLatin1();
                        payload += "\r\n";
                        const qint64 written = mockSerialPeer->write(payload);
                        if (written != payload.size() || !mockSerialPeer->waitForBytesWritten(500)) {
                            result.runtimeError = mockSerialPeer->errorString().isEmpty()
                                                      ? self->textFor("Unknown error", "未知错误")
                                                      : mockSerialPeer->errorString();
                            break;
                        }
                        lastInjectMs = timer.elapsed();
                    }

                    if (rtcmResponseBursts >= 8) {
                        result.gotResponse = true;
                        break;
                    }

                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }

                finalStats = testService->stats();
                result.inputBytes = finalStats.inputBytes;
                result.outputBytes = finalStats.outputBytes;
                result.finalMessage = finalStats.message;
                result.cancelled = self->shutdown_requested_.load();
                testService->stop();
                if (mockSerialPeer) {
                    mockSerialPeer->disconnectFromHost();
                }
            }
        }

        if (!self) {
            return;
        }

        QMetaObject::invokeMethod(self, [self, result = std::move(result)]() mutable {
            if (!self) {
                return;
            }

            self->test_in_progress_.store(false);
            emit self->stateChanged();
            self->updateStatusText();

            if (result.cancelled) {
                return;
            }

            if (!result.startError.isEmpty()) {
                self->appendLog(self->textFor("No-signal RTK test failed to start: %1",
                                              "无信号 RTK 测试启动失败: %1")
                                    .arg(result.startError));
                return;
            }

            if (result.gotResponse) {
                self->appendLog(self->textFor("No-signal RTK test succeeded: input %1 B, output %2 B, loopback %3 B",
                                              "无信号 RTK 测试成功: 输入 %1 B, 输出 %2 B, loopback %3 B")
                                    .arg(result.inputBytes)
                                    .arg(result.outputBytes)
                                    .arg(result.receivedRtcmBytes));
                return;
            }

            const QString detail =
                !result.runtimeError.isEmpty()
                    ? result.runtimeError
                    : (!result.linkReady
                           ? self->textFor("RTK loopback link did not become ready within timeout.",
                                           "超时时间内 RTK loopback 链路未进入可用状态。")
                           : (result.finalMessage.isEmpty()
                                  ? self->textFor("No RTCM data returned within timeout.",
                                                  "超时时间内未收到 RTCM 返回数据。")
                                  : result.finalMessage));
            self->appendLog(self->textFor("No-signal RTK test finished without RTCM response: %1",
                                          "无信号 RTK 测试结束，未收到 RTCM 返回: %1")
                                .arg(detail));
        }, Qt::QueuedConnection);
    });
}

void RtkController::toggleGgaMonitor()
{
    if (busy()) {
        return;
    }
    if (gga_monitor_enabled_) {
        stopGgaMonitorInternal();
        return;
    }

    gga_monitor_enabled_ = true;
    gga_buffer_.clear();
    gga_recent_intervals_sec_.clear();
    gga_has_sentence_time_ = false;
    gga_last_open_attempt_ = std::chrono::steady_clock::time_point();
    updateGgaFrequency(0.0);
    updateGgaStatusLabel(textFor("Status: Waiting for serial data", "状态: 正在等待串口数据"), false);
    emit stateChanged();
    if (!gga_poll_timer_.isActive()) {
        gga_poll_timer_.start();
    }
}

bool RtkController::saveProfileToUrl(const QUrl &url)
{
    return saveProfile(localFilePath(url));
}

bool RtkController::loadProfileFromUrl(const QUrl &url)
{
    return loadProfile(localFilePath(url));
}

void RtkController::appendLog(const QString &message)
{
    QStringList entries = log_model_.stringList();
    entries.append(QString("[%1] %2").arg(QDateTime::currentDateTime().toString("hh:mm:ss"), message));
    log_model_.setStringList(entries);
    trimStringListModel(log_model_, kRtkLogVisibleLines);
}

void RtkController::appendRawLogLine(const QString &message)
{
    QStringList entries = log_model_.stringList();
    entries.append(message);
    log_model_.setStringList(entries);
    trimStringListModel(log_model_, kRtkLogVisibleLines);
}

void RtkController::appendGgaLog(const QString &message)
{
    QStringList entries = gga_log_model_.stringList();
    entries.append(QString("[%1] %2").arg(QDateTime::currentDateTime().toString("hh:mm:ss.zzz"), message));
    gga_log_model_.setStringList(entries);
    trimStringListModel(gga_log_model_, kGgaMaxVisibleLines);
}

void RtkController::trimStringListModel(QStringListModel &model, int limit)
{
    QStringList entries = model.stringList();
    while (entries.size() > limit) {
        entries.removeFirst();
    }
    model.setStringList(entries);
}

void RtkController::loadSettings()
{
    QSettings settings("VaporView", "RtkConfig");
    server_ = settings.value("server", "").toString();
    port_ = settings.value("port", "2101").toString();
    username_ = settings.value("username", "").toString();
    password_ = settings.value("password", "").toString();
    mountpoint_ = settings.value("mountpoint", "").toString();
#ifdef _WIN32
    output_port_ = settings.value("output_port", "COM1").toString();
    gga_port_ = settings.value("gga_port", "COM1").toString();
#else
    output_port_ = settings.value("output_port", "/dev/ttyS0").toString();
    gga_port_ = settings.value("gga_port", "/dev/ttyS0").toString();
#endif
    baudrate_ = settings.value("baudrate", "115200").toString();
    timeout_ms_ = settings.value("timeout", "5000").toString();
    reconnect_ms_ = settings.value("reconnect", "1000").toString();
}

void RtkController::saveSettings() const
{
    QSettings settings("VaporView", "RtkConfig");
    settings.setValue("server", server_);
    settings.setValue("port", port_);
    settings.setValue("username", username_);
    settings.setValue("password", password_);
    settings.setValue("mountpoint", mountpoint_);
    settings.setValue("output_port", output_port_);
    settings.setValue("gga_port", gga_port_);
    settings.setValue("baudrate", baudrate_);
    settings.setValue("timeout", timeout_ms_);
    settings.setValue("reconnect", reconnect_ms_);
}

bool RtkController::saveProfile(const QString &filename)
{
    if (filename.trimmed().isEmpty()) {
        appendLog(textFor("Please choose a target INI file.", "请选择要保存的 INI 文件。"));
        return false;
    }

    QSettings settings(filename, QSettings::IniFormat);
    settings.setValue("server", server_);
    settings.setValue("port", port_);
    settings.setValue("username", username_);
    settings.setValue("password", password_);
    settings.setValue("mountpoint", mountpoint_);
    settings.setValue("output_port", output_port_);
    settings.setValue("gga_port", gga_port_);
    settings.setValue("baudrate", baudrate_);
    settings.setValue("timeout", timeout_ms_);
    settings.setValue("reconnect", reconnect_ms_);
    appendLog(textFor("Configuration saved to: %1", "配置已保存到: %1").arg(filename));
    return true;
}

bool RtkController::loadProfile(const QString &filename)
{
    if (filename.trimmed().isEmpty()) {
        appendLog(textFor("Please choose an INI file to load.", "请选择要加载的 INI 文件。"));
        return false;
    }

    QSettings settings(filename, QSettings::IniFormat);
    server_ = settings.value("server", "").toString();
    port_ = settings.value("port", "2101").toString();
    username_ = settings.value("username", "").toString();
    password_ = settings.value("password", "").toString();
    mountpoint_ = settings.value("mountpoint", "").toString();
    output_port_ = settings.value("output_port", output_port_).toString();
    gga_port_ = settings.value("gga_port", gga_port_).toString();
    baudrate_ = settings.value("baudrate", "115200").toString();
    timeout_ms_ = settings.value("timeout", "5000").toString();
    reconnect_ms_ = settings.value("reconnect", "1000").toString();
    emit configChanged();
    appendLog(textFor("Configuration loaded from: %1", "配置已从以下位置加载: %1").arg(filename));
    return true;
}

bool RtkController::buildConfig(RtkStreamConfig *config, QString *description) const
{
    const QString host = server_.trimmed();
    const QString hostPort = port_.trimmed().isEmpty() ? QStringLiteral("2101") : port_.trimmed();
    const QString outputPort = output_port_.trimmed();
    const QString currentMountpoint = mountpoint_.trimmed();

    if (host.isEmpty() || currentMountpoint.isEmpty() || outputPort.isEmpty()) {
        return false;
    }

    bool baudrateOk = false;
    const int baudrate = baudrate_.toInt(&baudrateOk);
    bool timeoutOk = false;
    const int timeoutValue = timeout_ms_.toInt(&timeoutOk);
    bool reconnectOk = false;
    const int reconnectValue = reconnect_ms_.toInt(&reconnectOk);

    if (config) {
        config->server = host;
        config->port = hostPort;
        config->username = username_.trimmed();
        config->password = password_;
        config->mountpoint = currentMountpoint;
        config->outputPort = outputPort;
        config->baudrate = baudrateOk ? baudrate : 115200;
        config->timeoutMs = timeoutOk ? timeoutValue : 5000;
        config->reconnectMs = reconnectOk ? reconnectValue : 1000;
    }

    if (description) {
        const QString ntripUrl = username_.trimmed().isEmpty()
                                     ? QString("ntrip://%1:%2/%3").arg(host, hostPort, currentMountpoint)
                                     : QString("ntrip://%1:%2@%3:%4/%5")
                                           .arg(username_.trimmed(), password_, host, hostPort, currentMountpoint);
        const QString serialUrl =
            QString("serial://%1:%2:8:n:1:off").arg(outputPort).arg(baudrateOk ? baudrate : 115200);
        *description =
            textFor("Embedded RTK stream: %1 -> %2", "内嵌 RTK 流服务: %1 -> %2").arg(ntripUrl, serialUrl);
    }

    return true;
}

QString RtkController::textFor(const QString &englishText, const QString &chineseText) const
{
    return english_ ? englishText : chineseText;
}

QStringList RtkController::availablePorts() const
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

QString RtkController::ggaPortName() const
{
    return gga_port_.trimmed();
}

int RtkController::currentGgaBaudrate() const
{
    bool ok = false;
    const int value = baudrate_.toInt(&ok);
    return ok ? value : 115200;
}

void RtkController::updateStatusText()
{
    QString next;
    if (fetch_mountpoints_in_progress_.load()) {
        next = textFor("Status: Fetching mountpoints", "状态: 正在获取挂载点");
    } else if (test_in_progress_.load()) {
        next = textFor("Status: Running no-signal RTK test", "状态: 正在执行无信号 RTK 测试");
    } else if (running_) {
        next = textFor("Status: Running", "状态: 运行中");
    } else {
        next = textFor("Status: Stopped", "状态: 已停止");
    }

    if (status_text_ != next) {
        status_text_ = next;
        emit statusTextChanged();
    }
}

void RtkController::pollRtkServiceStatus(bool forceLog)
{
    if (!rtk_service_) {
        return;
    }

    const RtkStreamStats stats = rtk_service_->stats();
    if ((forceLog || running_) && !stats.message.isEmpty()) {
        appendRawLogLine(formatRtkStatusLine(stats, textFor("Streaming RTCM data", "正在转发 RTCM 数据")));
    }

    if (!stats.running && running_) {
        running_ = false;
        status_timer_.stop();
        emit stateChanged();
        updateStatusText();
        appendLog(textFor("RTK service stopped unexpectedly", "RTK 服务已意外停止"));
        return;
    }

    if (running_) {
        const QString next =
            textFor("Status: Running (%1 bps in / %2 bps out)", "状态: 运行中 (%1 bps 输入 / %2 bps 输出)")
                .arg(stats.inputBps)
                .arg(stats.outputBps);
        if (status_text_ != next) {
            status_text_ = next;
            emit statusTextChanged();
        }
    }
}

void RtkController::updateGgaFrequency(double hz)
{
    const QString next =
        textFor("Actual Rate: %1 Hz", "真实频率: %1 Hz").arg(QString::number(std::max(0.0, hz), 'f', 2));
    if (gga_frequency_text_ != next) {
        gga_frequency_text_ = next;
        emit ggaStatusChanged();
    }
}

void RtkController::updateGgaStatusLabel(const QString &message, bool)
{
    if (gga_status_text_ != message) {
        gga_status_text_ = message;
        emit ggaStatusChanged();
    }
}

void RtkController::updateMountpointOptions(const QStringList &options)
{
    if (mountpoint_options_ == options) {
        return;
    }
    mountpoint_options_ = options;
    emit mountpointOptionsChanged();
}

bool RtkController::tryOpenGgaPort()
{
    if (!gga_monitor_enabled_) {
        return false;
    }
    if (gga_serial_.isOpen()) {
        return true;
    }

    const auto now = std::chrono::steady_clock::now();
    if (gga_last_open_attempt_.time_since_epoch().count() > 0 &&
        std::chrono::duration_cast<std::chrono::milliseconds>(now - gga_last_open_attempt_).count() <
            kGgaReconnectIntervalMs) {
        return false;
    }

    gga_last_open_attempt_ = now;
    const std::string port = ggaPortName().toStdString();
    if (port.empty()) {
        updateGgaStatusLabel(textFor("Status: Please select a GGA port", "状态: 请选择 GGA 串口"), false);
        return false;
    }

    if (!gga_serial_.open(port, currentGgaBaudrate())) {
        updateGgaStatusLabel(textFor("Status: %1 unavailable, retrying...", "状态: %1 不可用，正在重试...")
                                 .arg(ggaPortName()),
                             false);
        return false;
    }

    gga_serial_.setNonBlocking(true);
    gga_buffer_.clear();
    gga_recent_intervals_sec_.clear();
    gga_has_sentence_time_ = false;
    updateGgaFrequency(0.0);
    updateGgaStatusLabel(textFor("Status: Listening on %1", "状态: 正在监听 %1").arg(ggaPortName()), true);
    return true;
}

void RtkController::processGgaBuffer()
{
    while (true) {
        const int newlineIndex = gga_buffer_.indexOf('\n');
        if (newlineIndex < 0) {
            break;
        }

        QString line = gga_buffer_.left(newlineIndex);
        gga_buffer_.remove(0, newlineIndex + 1);
        line = line.trimmed();
        if (line.isEmpty()) {
            continue;
        }

        if (kGgaSentencePattern.match(line).hasMatch()) {
            handleGgaSentence(line);
        }
    }
}

void RtkController::handleGgaSentence(const QString &sentence)
{
    const auto now = std::chrono::steady_clock::now();
    if (gga_has_sentence_time_) {
        const double intervalSeconds =
            std::chrono::duration_cast<std::chrono::duration<double>>(now - gga_last_sentence_time_).count();
        if (intervalSeconds > 0.0) {
            gga_recent_intervals_sec_.push_back(intervalSeconds);
            while (gga_recent_intervals_sec_.size() > 20) {
                gga_recent_intervals_sec_.pop_front();
            }

            double total = 0.0;
            for (double value : gga_recent_intervals_sec_) {
                total += value;
            }
            if (!gga_recent_intervals_sec_.empty() && total > 0.0) {
                updateGgaFrequency(static_cast<double>(gga_recent_intervals_sec_.size()) / total);
            }
        }
    } else {
        updateGgaFrequency(0.0);
    }

    gga_has_sentence_time_ = true;
    gga_last_sentence_time_ = now;
    updateGgaStatusLabel(textFor("Status: Receiving GGA data", "状态: 正在接收 GGA 数据"), true);
    appendGgaLog(sentence);
}

void RtkController::stopGgaMonitorInternal()
{
    if (gga_poll_timer_.isActive()) {
        gga_poll_timer_.stop();
    }
    if (gga_serial_.isOpen()) {
        gga_serial_.close();
    }
    gga_buffer_.clear();
    gga_recent_intervals_sec_.clear();
    gga_has_sentence_time_ = false;
    gga_monitor_enabled_ = false;
    updateGgaFrequency(0.0);
    updateGgaStatusLabel(textFor("Status: Click button to read GGA", "状态: 点击按钮开始读取GGA"), false);
    emit stateChanged();
}

void RtkController::joinBackgroundThreads()
{
    if (fetch_mountpoints_thread_.joinable()) {
        fetch_mountpoints_thread_.join();
    }
    if (test_thread_.joinable()) {
        test_thread_.join();
    }
}

QString RtkController::localFilePath(const QUrl &url)
{
    if (url.isLocalFile()) {
        return url.toLocalFile();
    }
    const QString stringValue = url.toString();
    if (stringValue.startsWith(QStringLiteral("file:///"))) {
        return QUrl(stringValue).toLocalFile();
    }
    return stringValue;
}
