#include "RtkConfigDialog.h"
#include "WindowSizing.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QFormLayout>
#include <QFileDialog>
#include <QMessageBox>
#include <QPointer>
#include <QSettings>
#include <QDir>
#include <QDirIterator>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QCloseEvent>
#include <QDoubleValidator>
#include <QFontMetrics>
#include <QFrame>
#include <QIntValidator>
#include <QLabel>
#include <QLocale>
#include <QElapsedTimer>
#include <QRegularExpression>
#include <QSerialPortInfo>
#include <QSignalBlocker>
#include <QScrollArea>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTextBlock>
#include <QTextCursor>
#include <QTimer>
#include <QTimeZone>
#include <QToolButton>
#include <QUrl>
#include <QUrlQuery>
#include <cmath>
#include <utility>

#include "serial_probe_utils.h"

namespace
{
constexpr int kGgaSendCycleMs = 1000;
constexpr int kGgaPollIntervalMs = kGgaSendCycleMs / 2;
constexpr int kGgaReconnectIntervalMs = 1500;
constexpr int kGgaStaleTimeoutMs = 1500;
constexpr int kGgaMaxVisibleLines = 200;
constexpr int kRtkLogVisibleLines = 5;
constexpr int kRtkHttpTimeoutMs = 5000;
constexpr int kRtkPreferredDialogWidth = 980;
constexpr int kRtkMinimumDialogWidth = 640;
constexpr int kRtkMinimumDialogHeight = 420;
constexpr const char *kEpsilonMainGgaSourceKey = "__epsilon_main__";
const QRegularExpression kGgaSentencePattern("^\\$..GGA,");

QStringList buildProbeBaudList(const QComboBox *baudrateCombo)
{
    QStringList baudTexts;
    if (baudrateCombo)
    {
        const QString currentText = baudrateCombo->currentText().trimmed();
        if (!currentText.isEmpty())
        {
            baudTexts.append(currentText);
        }

        for (int i = 0; i < baudrateCombo->count(); ++i)
        {
            const QString text = baudrateCombo->itemText(i).trimmed();
            if (!text.isEmpty())
            {
                baudTexts.append(text);
            }
        }
    }

    baudTexts.removeDuplicates();
    return baudTexts;
}

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
    bool generatedGga = false;
};

QString describeNoSignalTestFailure(const NoSignalTestResult& result, bool english)
{
    if (!result.runtimeError.isEmpty())
    {
        return result.runtimeError;
    }

    if (!result.linkReady)
    {
        return english
            ? QStringLiteral("The local loopback serial link did not become ready within timeout.")
            : QStringLiteral("超时时间内本地 loopback 模拟串口链路未进入可用状态。");
    }

    const QString rtklibMessage = result.finalMessage.trimmed();
    const bool inputDisconnected = rtklibMessage.contains(QStringLiteral("(0) disconnected"), Qt::CaseInsensitive);
    if (inputDisconnected)
    {
        return english
            ? QStringLiteral("The local 127.0.0.1 loopback link is only the mock serial used by this test and is already connected. "
                             "The NTRIP input stream is disconnected, so no RTCM data can be returned. "
                             "Check the caster address, port, mountpoint, account/password, and network access.\n"
                             "RTKLIB status: %1")
                  .arg(rtklibMessage)
            : QStringLiteral("127.0.0.1 是本次测试使用的本地 loopback 模拟串口，已经连上；真正断开的是 NTRIP 输入流，"
                             "因此不会有 RTCM 数据返回。请检查差分服务器地址、端口、挂载点、账号密码和网络连接。\n"
                             "RTKLIB 状态: %1")
                  .arg(rtklibMessage);
    }

    if (result.inputBytes <= 0)
    {
        if (result.generatedGga)
        {
            return english
                ? QStringLiteral("The EPSILON main-port position was sent to the NTRIP caster as GGA, but no bytes were received. "
                                 "Check whether the caster requires a different mountpoint, valid credentials, or a different rover position.\n"
                                 "RTKLIB status: %1")
                      .arg(rtklibMessage.isEmpty() ? QStringLiteral("--") : rtklibMessage)
                : QStringLiteral("已把 EPSILON 主串口定位组装成 GGA 发给 NTRIP 差分服务器，但没有收到任何字节。"
                                 "请检查挂载点、账号密码，或该服务是否要求不同的流动站位置。\n"
                                 "RTKLIB 状态: %1")
                      .arg(rtklibMessage.isEmpty() ? QStringLiteral("--") : rtklibMessage);
        }

        return english
            ? QStringLiteral("The test GGA was injected through the local loopback, but no bytes were received from the NTRIP caster. "
                             "Check whether the caster requires a different mountpoint, valid credentials, or a real rover position.\n"
                             "RTKLIB status: %1")
                  .arg(rtklibMessage.isEmpty() ? QStringLiteral("--") : rtklibMessage)
            : QStringLiteral("测试 GGA 已通过本地 loopback 注入，但没有从 NTRIP 差分服务器收到任何字节。"
                             "请检查挂载点、账号密码，或该服务是否要求真实流动站位置。\n"
                             "RTKLIB 状态: %1")
                  .arg(rtklibMessage.isEmpty() ? QStringLiteral("--") : rtklibMessage);
    }

    return rtklibMessage.isEmpty()
        ? (english
              ? QStringLiteral("No RTCM data returned within timeout.")
              : QStringLiteral("超时时间内未收到 RTCM 返回数据。"))
        : rtklibMessage;
}

QComboBox *createTimingComboBox(QWidget *parent, const QString &defaultValue)
{
    auto *combo = new QComboBox(parent);
    combo->setEditable(true);
    combo->addItems({"1000", "2000", "5000", "10000", "30000", "60000"});
    combo->setCurrentText(defaultValue);
    if (combo->lineEdit())
    {
        combo->lineEdit()->setValidator(new QIntValidator(1000, 60000, combo));
    }
    return combo;
}

int comboIntValue(const QComboBox *combo, int defaultValue)
{
    if (!combo)
    {
        return defaultValue;
    }

    bool ok = false;
    const int value = combo->currentText().toInt(&ok);
    return ok ? value : defaultValue;
}

bool isUsableEpsilonNmeaPosition(const VaporView::EpsilonData &data)
{
    return data.valid &&
        std::isfinite(data.latitude_deg) &&
        std::isfinite(data.longitude_deg) &&
        std::isfinite(data.height_m) &&
        std::isfinite(data.hdop) &&
        std::abs(data.latitude_deg) <= 90.0 &&
        std::abs(data.longitude_deg) <= 180.0 &&
        std::abs(data.height_m) <= 20000.0 &&
        data.gnss_fix_code > 0 &&
        data.gnss_satellites >= 4 &&
        data.hdop > 0.0 &&
        data.hdop <= 50.0 &&
        (std::abs(data.latitude_deg) > 1e-9 || std::abs(data.longitude_deg) > 1e-9);
}

QString wrapNmeaSentence(const QString &body)
{
    unsigned char checksum = 0;
    const QByteArray bytes = body.toLatin1();
    for (char ch : bytes)
    {
        checksum ^= static_cast<unsigned char>(ch);
    }

    return QStringLiteral("$%1*%2")
        .arg(body)
        .arg(static_cast<int>(checksum), 2, 16, QLatin1Char('0'))
        .toUpper();
}

QString formatNmeaCoordinate(double degrees, int degreeWidth)
{
    const double absoluteDegrees = std::abs(degrees);
    const int wholeDegrees = static_cast<int>(std::floor(absoluteDegrees));
    const double minutes = (absoluteDegrees - wholeDegrees) * 60.0;
    QString minutesText = QString::number(minutes, 'f', 6);
    if (minutes < 10.0)
    {
        minutesText.prepend(QLatin1Char('0'));
    }

    return QStringLiteral("%1%2")
        .arg(wholeDegrees, degreeWidth, 10, QLatin1Char('0'))
        .arg(minutesText);
}

QString ggaTimeFieldFromEpsilon(const VaporView::EpsilonData &data)
{
    QDateTime utc = QDateTime::currentDateTimeUtc();
    if (data.utc_unix_s > 0)
    {
        utc = QDateTime::fromSecsSinceEpoch(static_cast<qint64>(data.utc_unix_s), QTimeZone::UTC)
            .addMSecs(static_cast<qint64>(data.utc_microseconds / 1000));
    }

    const QTime time = utc.time();
    return QStringLiteral("%1%2%3.%4")
        .arg(time.hour(), 2, 10, QLatin1Char('0'))
        .arg(time.minute(), 2, 10, QLatin1Char('0'))
        .arg(time.second(), 2, 10, QLatin1Char('0'))
        .arg(time.msec() / 10, 2, 10, QLatin1Char('0'));
}

QString buildEpsilonGgaSentence(const VaporView::EpsilonData &data)
{
    if (!isUsableEpsilonNmeaPosition(data))
    {
        return {};
    }

    const QString latitude = formatNmeaCoordinate(data.latitude_deg, 2);
    const QString longitude = formatNmeaCoordinate(data.longitude_deg, 3);
    const QString northSouth = data.latitude_deg < 0.0 ? QStringLiteral("S") : QStringLiteral("N");
    const QString eastWest = data.longitude_deg < 0.0 ? QStringLiteral("W") : QStringLiteral("E");
    const int satellites = std::clamp(data.gnss_satellites, 0, 99);
    const double hdop = std::isfinite(data.hdop) && data.hdop > 0.0 ? data.hdop : 1.0;
    const double altitude = std::isfinite(data.height_m) ? data.height_m : 0.0;
    const double diffAge = std::isfinite(data.diff_age_s) && data.diff_age_s > 0.0 ? data.diff_age_s : 0.0;

    const QString body = QStringLiteral("GPGGA,%1,%2,%3,%4,%5,1,%6,%7,%8,M,0.0,M,%9,")
        .arg(ggaTimeFieldFromEpsilon(data),
             latitude,
             northSouth,
             longitude,
             eastWest)
        .arg(satellites, 2, 10, QLatin1Char('0'))
        .arg(QString::number(hdop, 'f', 1),
             QString::number(altitude, 'f', 3),
             diffAge > 0.0 ? QString::number(diffAge, 'f', 1) : QString());

    return wrapNmeaSentence(body);
}

QString buildMockGgaSentence()
{
    const QTime utc = QDateTime::currentDateTimeUtc().time();
    const QString timeField = QStringLiteral("%1%2%3.%4")
        .arg(utc.hour(), 2, 10, QLatin1Char('0'))
        .arg(utc.minute(), 2, 10, QLatin1Char('0'))
        .arg(utc.second(), 2, 10, QLatin1Char('0'))
        .arg(utc.msec() / 10, 2, 10, QLatin1Char('0'));

    const QString body = QStringLiteral(
        "GPGGA,%1,3000.0000,N,12000.0000,E,1,12,1.0,0.0,M,0.0,M,,")
        .arg(timeField);

    return wrapNmeaSentence(body);
}

QString formatRtcmDiagnostic(const RtkStreamStats &stats, bool english)
{
    if (stats.inputBytes <= 0 && stats.rtcmDiagnosticBytes == 0)
    {
        return english
            ? QStringLiteral("RTCM diagnostic: no caster bytes received yet")
            : QStringLiteral("RTCM诊断: 尚未收到服务器字节");
    }

    if (stats.rtcmDiagnosticBytes == 0)
    {
        return english
            ? QStringLiteral("RTCM diagnostic: waiting for raw input bytes")
            : QStringLiteral("RTCM诊断: 正在等待原始输入字节");
    }

    QStringList parts;
    parts << (english
        ? QStringLiteral("RTCM diagnostic: inspected %1 B").arg(stats.rtcmDiagnosticBytes)
        : QStringLiteral("RTCM诊断: 已检查 %1 B").arg(stats.rtcmDiagnosticBytes));

    if (stats.rtcm3FrameCount > 0)
    {
        parts << (english
            ? QStringLiteral("RTCM3/D3 frames %1, CRC ok %2, bad %3")
                  .arg(stats.rtcm3FrameCount)
                  .arg(stats.rtcm3CrcOkCount)
                  .arg(stats.rtcm3CrcFailCount)
            : QStringLiteral("RTCM3/D3帧 %1，CRC正确 %2，错误 %3")
                  .arg(stats.rtcm3FrameCount)
                  .arg(stats.rtcm3CrcOkCount)
                  .arg(stats.rtcm3CrcFailCount));
    }
    else
    {
        parts << (english
            ? QStringLiteral("no complete RTCM3/D3 frame yet")
            : QStringLiteral("尚无完整RTCM3/D3帧"));
    }

    if (stats.nonRtcmByteCount > 0)
    {
        parts << (english
            ? QStringLiteral("non-RTCM bytes %1").arg(stats.nonRtcmByteCount)
            : QStringLiteral("非RTCM字节 %1").arg(stats.nonRtcmByteCount));
    }

    if (stats.rtcm3PendingBytes > 0)
    {
        parts << (english
            ? QStringLiteral("pending %1 B").arg(stats.rtcm3PendingBytes)
            : QStringLiteral("待拼帧 %1 B").arg(stats.rtcm3PendingBytes));
    }

    if (!stats.rtcmMessageTypes.isEmpty())
    {
        parts << (english
            ? QStringLiteral("MT %1").arg(stats.rtcmMessageTypes)
            : QStringLiteral("消息类型 %1").arg(stats.rtcmMessageTypes));
    }

    if (!stats.firstInputHex.isEmpty())
    {
        parts << (english
            ? QStringLiteral("first bytes %1").arg(stats.firstInputHex)
            : QStringLiteral("首字节 %1").arg(stats.firstInputHex));
    }

    if (stats.rtcm3FrameCount == 0 &&
        !stats.firstInputAscii.isEmpty() &&
        stats.firstInputAscii.contains(QRegularExpression(QStringLiteral("[A-Za-z]"))))
    {
        parts << (english
            ? QStringLiteral("ASCII preview \"%1\"").arg(stats.firstInputAscii.left(48))
            : QStringLiteral("文本预览 \"%1\"").arg(stats.firstInputAscii.left(48)));
    }

    return parts.join(QStringLiteral("; "));
}

QString formatRtkStatusLine(const RtkStreamStats &stats, const QString &fallbackMessage, bool english)
{
    const QString message = stats.message.isEmpty() ? fallbackMessage : stats.message;
    const QString base = QString("%1 [%2] %3 B %4 bps %5")
        .arg(QDateTime::currentDateTime().toString("yyyy/MM/dd hh:mm:ss"))
        .arg(stats.streamStateMask.isEmpty() ? QStringLiteral("-----") : stats.streamStateMask)
        .arg(QString::number(stats.inputBytes).rightJustified(10, QLatin1Char(' ')))
        .arg(QString::number(stats.inputBps).rightJustified(7, QLatin1Char(' ')))
        .arg(message);
    const QString diagnostic = formatRtcmDiagnostic(stats, english);
    return diagnostic.isEmpty() ? base : QStringLiteral("%1 | %2").arg(base, diagnostic);
}

QUrl buildRtkUrl(const QString &server, const QString &port, const QString &path = QString())
{
    QUrl url;
    url.setScheme(QStringLiteral("http"));
    url.setHost(server.trimmed());
    bool portOk = false;
    const int parsedPort = port.trimmed().toInt(&portOk);
    if (portOk)
    {
        url.setPort(parsedPort);
    }
    url.setPath(path.isEmpty() ? QStringLiteral("/") : QStringLiteral("/") + path);
    return url;
}

HttpResponse performRtkHttpGet(
    QObject *context,
    const QUrl &url,
    const QString &username,
    const QString &password,
    const QString &acceptHeader = QStringLiteral("*/*"))
{
    Q_UNUSED(context);

    HttpResponse result;
    QTcpSocket socket;

    const QString host = url.host().trimmed();
    const int port = url.port(80);
    QString path = url.path();
    if (path.isEmpty())
    {
        path = QStringLiteral("/");
    }
    if (!url.query().isEmpty())
    {
        path += QStringLiteral("?") + url.query();
    }

    socket.connectToHost(host, static_cast<quint16>(port));
    if (!socket.waitForConnected(kRtkHttpTimeoutMs))
    {
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

    if (!username.trimmed().isEmpty())
    {
        const QByteArray credentials = QStringLiteral("%1:%2").arg(username.trimmed(), password).toUtf8().toBase64();
        requestData += "Authorization: Basic " + credentials + "\r\n";
    }

    requestData += "\r\n";

    if (socket.write(requestData) != requestData.size() || !socket.waitForBytesWritten(kRtkHttpTimeoutMs))
    {
        result.error = socket.errorString();
        result.timedOut = (socket.error() == QAbstractSocket::SocketTimeoutError);
        return result;
    }

    QByteArray rawResponse;
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < kRtkHttpTimeoutMs)
    {
        if (socket.waitForReadyRead(200))
        {
            rawResponse += socket.readAll();
            while (socket.bytesAvailable() > 0)
            {
                rawResponse += socket.readAll();
            }
        }

        if (socket.state() == QAbstractSocket::UnconnectedState)
        {
            break;
        }
    }

    rawResponse += socket.readAll();
    socket.disconnectFromHost();

    if (rawResponse.isEmpty())
    {
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
    if (headerEnd >= 0)
    {
        headerBytes = rawResponse.left(headerEnd);
        bodyBytes = rawResponse.mid(headerEnd + 4);
    }

    const QList<QByteArray> headerLines = headerBytes.split('\n');
    const QByteArray statusLine = headerLines.isEmpty() ? QByteArray() : headerLines.first().trimmed();
    result.statusCode = parseStatusCode(statusLine);

    if (headerEnd >= 0)
    {
        result.body = QString::fromLatin1(bodyBytes);
    }
    else if (statusLine.startsWith("STR;") || statusLine.startsWith("CAS;") || statusLine.startsWith("NET;"))
    {
        result.statusCode = 200;
        result.body = QString::fromLatin1(rawResponse);
    }
    else
    {
        result.body = QString::fromLatin1(rawResponse);
    }

    return result;
}

QStringList parseMountpoints(const QString &responseBody)
{
    QStringList mountpoints;
    const QStringList lines = responseBody.split(QRegularExpression("[\r\n]+"), Qt::SkipEmptyParts);
    for (const QString &line : lines)
    {
        if (!line.startsWith(QStringLiteral("STR;")))
        {
            continue;
        }

        const QStringList parts = line.split(';');
        if (parts.size() > 1 && !parts.at(1).trimmed().isEmpty())
        {
            mountpoints.append(parts.at(1).trimmed());
        }
    }

    mountpoints.removeDuplicates();
    mountpoints.sort();
    return mountpoints;
}
}

RtkConfigDialog::RtkConfigDialog(QWidget *parent)
    : QDialog(parent)
    , main_layout_(nullptr)
    , config_layout_(nullptr)
    , output_layout_(nullptr)
    , button_layout_(nullptr)
    , log_layout_(nullptr)
    , log_button_layout_(nullptr)
    , gga_layout_(nullptr)
    , gga_header_layout_(nullptr)
    , gga_text_container_layout_(nullptr)
    , log_text_container_layout_(nullptr)
    , gga_button_spacer_(nullptr)
    , config_group_(nullptr)
    , output_group_(nullptr)
    , gga_group_(nullptr)
    , log_group_(nullptr)
    , gga_text_container_(nullptr)
    , log_text_container_(nullptr)
    , server_label_(nullptr)
    , port_label_(nullptr)
    , username_label_(nullptr)
    , password_label_(nullptr)
    , mountpoint_label_(nullptr)
    , output_port_label_(nullptr)
    , baudrate_label_(nullptr)
    , main_antenna_lever_label_(nullptr)
    , timeout_label_(nullptr)
    , reconnect_label_(nullptr)
    , gga_port_info_label_(nullptr)
    , gga_status_label_(nullptr)
    , gga_frequency_label_(nullptr)
    , server_edit_(nullptr)
    , port_edit_(nullptr)
    , username_edit_(nullptr)
    , password_edit_(nullptr)
    , mountpoint_edit_(nullptr)
    , main_antenna_lever_x_edit_(nullptr)
    , main_antenna_lever_y_edit_(nullptr)
    , main_antenna_lever_z_edit_(nullptr)
    , output_port_combo_(nullptr)
    , baudrate_combo_(nullptr)
    , timeout_combo_(nullptr)
    , reconnect_combo_(nullptr)
    , gga_port_combo_(nullptr)
    , gga_text_edit_(nullptr)
    , log_text_edit_(nullptr)
    , start_btn_(nullptr)
    , stop_btn_(nullptr)
    , test_btn_(nullptr)
    , gga_toggle_btn_(nullptr)
    , refresh_ports_btn_(nullptr)
    , auto_detect_ports_btn_(nullptr)
    , fetch_mountpoints_btn_(nullptr)
    , main_antenna_lever_help_btn_(nullptr)
    , apply_main_antenna_lever_btn_(nullptr)
    , save_config_btn_(nullptr)
    , load_config_btn_(nullptr)
    , clear_log_btn_(nullptr)
    , status_label_(nullptr)
    , rtk_service_(std::make_unique<RtkStreamService>())
    , is_running_(false)
    , is_english_(false)
    , font_scale_percent_(100)
    , epsilon_main_baudrate_(921600)
    , base_dialog_size_(kRtkPreferredDialogWidth, 0)
    , base_minimum_dialog_size_(kRtkMinimumDialogWidth, kRtkMinimumDialogHeight)
    , rtk_status_timer_(nullptr)
    , gga_poll_timer_(nullptr)
    , last_rtk_status_message_()
    , gga_last_open_attempt_()
    , gga_last_sentence_time_()
    , gga_last_epsilon_sample_time_()
    , gga_last_epsilon_device_timestamp_us_(0)
    , gga_has_sentence_time_(false)
    , gga_monitor_enabled_(false)
{
    setObjectName(QStringLiteral("rtkConfigDialog"));
    setWindowFlags(windowFlags()
                   | Qt::WindowMinimizeButtonHint
                   | Qt::WindowMaximizeButtonHint);
    setSizeGripEnabled(true);

    setupUi();
    loadSettings();
    setFontScale(100);
    setEnglish(false);
    VaporView::centerWindowOnScreen(this, parent);

    config_file_path_ = QDir::homePath() + "/.config/VaporView/rtk_config.ini";

    rtk_status_timer_ = new QTimer(this);
    rtk_status_timer_->setInterval(1000);
    connect(rtk_status_timer_, &QTimer::timeout, this, &RtkConfigDialog::onRtkStatusTimer);

    gga_poll_timer_ = new QTimer(this);
    connect(gga_poll_timer_, &QTimer::timeout, this, &RtkConfigDialog::onGgaPollTimer);
    connect(baudrate_combo_, &QComboBox::currentTextChanged, this, [this](const QString&) {
        stopGgaMonitor();
    });
    connect(gga_port_combo_, &QComboBox::currentTextChanged, this, [this](const QString&) {
        stopGgaMonitor();
    });
}

RtkConfigDialog::~RtkConfigDialog()
{
    shutdown_requested_.store(true);
    if (rtk_status_timer_ && rtk_status_timer_->isActive())
    {
        rtk_status_timer_->stop();
    }
    if (rtk_service_)
    {
        rtk_service_->stop();
    }
    stopGgaMonitor();
    joinBackgroundTasks();
    saveSettings();
}

void RtkConfigDialog::closeEvent(QCloseEvent *event)
{
    saveSettings();
    event->ignore();
    hide();
    if (is_running_ || gga_monitor_enabled_)
    {
        appendLog(textFor("RTK config window hidden; running tasks continue in background.",
                          "RTK 配置窗口已隐藏；运行中的任务会继续在后台执行。"));
    }
}

void RtkConfigDialog::joinBackgroundTasks()
{
    if (fetch_mountpoints_thread_.joinable())
    {
        fetch_mountpoints_thread_.join();
    }
    if (port_detection_thread_.joinable())
    {
        port_detection_thread_.join();
    }
    if (test_thread_.joinable())
    {
        test_thread_.join();
    }
}

bool RtkConfigDialog::isBackgroundTaskRunning() const
{
    return fetch_mountpoints_in_progress_.load() || port_detection_in_progress_.load() || test_in_progress_.load();
}

void RtkConfigDialog::setupUi()
{
    auto *outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->setSpacing(0);

    auto *scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    outerLayout->addWidget(scrollArea);

    auto *contentWidget = new QWidget(scrollArea);
    scrollArea->setWidget(contentWidget);

    main_layout_ = new QVBoxLayout(contentWidget);
    main_layout_->setSpacing(8);
    main_layout_->setContentsMargins(12, 12, 12, 12);

    config_group_ = new QGroupBox(this);
    config_layout_ = new QGridLayout(config_group_);
    config_layout_->setSpacing(6);
    config_layout_->setContentsMargins(10, 30, 10, 10);
    config_layout_->setColumnStretch(1, 2);
    config_layout_->setColumnStretch(5, 1);

    int row = 0;
    server_label_ = new QLabel(this);
    config_layout_->addWidget(server_label_, row, 0);
    server_edit_ = new QLineEdit(this);
    config_layout_->addWidget(server_edit_, row, 1);

    port_label_ = new QLabel(this);
    config_layout_->addWidget(port_label_, row, 2);
    port_edit_ = new QLineEdit(this);
    port_edit_->setText("2101");
    config_layout_->addWidget(port_edit_, row, 3);

    mountpoint_label_ = new QLabel(this);
    config_layout_->addWidget(mountpoint_label_, row, 4);
    mountpoint_edit_ = new QLineEdit(this);
    config_layout_->addWidget(mountpoint_edit_, row, 5);
    fetch_mountpoints_btn_ = new QPushButton(this);
    connect(fetch_mountpoints_btn_, &QPushButton::clicked, this, &RtkConfigDialog::onFetchMountpointsClicked);
    config_layout_->addWidget(fetch_mountpoints_btn_, row, 6);
    row++;

    username_label_ = new QLabel(this);
    config_layout_->addWidget(username_label_, row, 0);
    username_edit_ = new QLineEdit(this);
    config_layout_->addWidget(username_edit_, row, 1, 1, 2);

    password_label_ = new QLabel(this);
    config_layout_->addWidget(password_label_, row, 3);
    password_edit_ = new QLineEdit(this);
    config_layout_->addWidget(password_edit_, row, 4, 1, 3);
    row++;

    main_layout_->addWidget(config_group_);

    output_group_ = new QGroupBox(this);
    output_layout_ = new QGridLayout(output_group_);
    output_layout_->setSpacing(6);
    output_layout_->setContentsMargins(10, 30, 10, 10);
    output_layout_->setColumnStretch(1, 1);
    output_layout_->setColumnStretch(5, 1);

    row = 0;
    output_port_label_ = new QLabel(this);
    output_layout_->addWidget(output_port_label_, row, 0);
    output_port_combo_ = new QComboBox(this);
    output_port_combo_->setEditable(true);
    output_layout_->addWidget(output_port_combo_, row, 1);

    refresh_ports_btn_ = new QPushButton(this);
    connect(refresh_ports_btn_, &QPushButton::clicked, this, &RtkConfigDialog::onRefreshPortsClicked);
    output_layout_->addWidget(refresh_ports_btn_, row, 2);

    auto_detect_ports_btn_ = new QPushButton(this);
    connect(auto_detect_ports_btn_, &QPushButton::clicked, this, &RtkConfigDialog::onAutoDetectPortsClicked);
    output_layout_->addWidget(auto_detect_ports_btn_, row, 3);

    baudrate_label_ = new QLabel(this);
    output_layout_->addWidget(baudrate_label_, row, 4);
    baudrate_combo_ = new QComboBox(this);
    baudrate_combo_->addItems({"9600", "19200", "38400", "57600", "115200", "230400", "460800", "921600"});
    baudrate_combo_->setCurrentText("115200");
    output_layout_->addWidget(baudrate_combo_, row, 5);
    row++;

    auto *lever_label_widget = new QWidget(this);
    auto *lever_label_layout = new QHBoxLayout(lever_label_widget);
    lever_label_layout->setContentsMargins(0, 0, 0, 0);
    lever_label_layout->setSpacing(4);
    main_antenna_lever_label_ = new QLabel(this);
    lever_label_layout->addWidget(main_antenna_lever_label_);
    main_antenna_lever_help_btn_ = new QToolButton(this);
    main_antenna_lever_help_btn_->setText(QStringLiteral("?"));
    main_antenna_lever_help_btn_->setAutoRaise(true);
    main_antenna_lever_help_btn_->setCursor(Qt::PointingHandCursor);
    connect(main_antenna_lever_help_btn_, &QToolButton::clicked, this, &RtkConfigDialog::onMainAntennaLeverHelpClicked);
    lever_label_layout->addWidget(main_antenna_lever_help_btn_);
    lever_label_layout->addStretch();
    output_layout_->addWidget(lever_label_widget, row, 0);

    auto *lever_edit_widget = new QWidget(this);
    auto *lever_edit_layout = new QHBoxLayout(lever_edit_widget);
    lever_edit_layout->setContentsMargins(0, 0, 0, 0);
    lever_edit_layout->setSpacing(4);
    auto createLeverEdit = [this]() {
        auto *edit = new QLineEdit(this);
        auto *validator = new QDoubleValidator(-10000.0, 10000.0, 4, edit);
        validator->setNotation(QDoubleValidator::StandardNotation);
        edit->setValidator(validator);
        edit->setAlignment(Qt::AlignRight);
        edit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        return edit;
    };
    lever_edit_layout->addWidget(new QLabel(QStringLiteral("X"), this));
    main_antenna_lever_x_edit_ = createLeverEdit();
    lever_edit_layout->addWidget(main_antenna_lever_x_edit_);
    lever_edit_layout->addWidget(new QLabel(QStringLiteral("Y"), this));
    main_antenna_lever_y_edit_ = createLeverEdit();
    lever_edit_layout->addWidget(main_antenna_lever_y_edit_);
    lever_edit_layout->addWidget(new QLabel(QStringLiteral("Z"), this));
    main_antenna_lever_z_edit_ = createLeverEdit();
    lever_edit_layout->addWidget(main_antenna_lever_z_edit_);
    output_layout_->addWidget(lever_edit_widget, row, 1, 1, 4);

    apply_main_antenna_lever_btn_ = new QPushButton(this);
    connect(apply_main_antenna_lever_btn_, &QPushButton::clicked, this, &RtkConfigDialog::onApplyMainAntennaLeverArmClicked);
    output_layout_->addWidget(apply_main_antenna_lever_btn_, row, 5);
    row++;

    timeout_label_ = new QLabel(this);
    output_layout_->addWidget(timeout_label_, row, 0);
    timeout_combo_ = createTimingComboBox(this, "5000");
    output_layout_->addWidget(timeout_combo_, row, 1);

    reconnect_label_ = new QLabel(this);
    output_layout_->addWidget(reconnect_label_, row, 2);
    reconnect_combo_ = createTimingComboBox(this, "1000");
    output_layout_->addWidget(reconnect_combo_, row, 3, 1, 3);
    row++;

    main_layout_->addWidget(output_group_);

    gga_group_ = new QGroupBox(this);
    gga_layout_ = new QVBoxLayout(gga_group_);
    gga_layout_->setSpacing(6);
    gga_layout_->setContentsMargins(10, 30, 10, 12);
    gga_group_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

    gga_header_layout_ = new QHBoxLayout();
    gga_header_layout_->setSpacing(8);

    gga_port_info_label_ = new QLabel(this);
    gga_header_layout_->addWidget(gga_port_info_label_);

    gga_port_combo_ = new QComboBox(this);
    gga_port_combo_->setEditable(true);
    gga_header_layout_->addWidget(gga_port_combo_);

    gga_toggle_btn_ = new QPushButton(this);
    connect(gga_toggle_btn_, &QPushButton::clicked, this, &RtkConfigDialog::onGgaToggleClicked);
    gga_header_layout_->addWidget(gga_toggle_btn_);
    gga_header_layout_->addStretch();

    gga_frequency_label_ = new QLabel(this);
    gga_header_layout_->addWidget(gga_frequency_label_);
    gga_layout_->addLayout(gga_header_layout_);

    gga_status_label_ = new QLabel(this);
    gga_layout_->addWidget(gga_status_label_);

    gga_text_container_ = new QWidget(gga_group_);
    gga_text_container_layout_ = new QVBoxLayout(gga_text_container_);
    gga_text_container_layout_->setContentsMargins(0, 0, 0, 8);
    gga_text_container_layout_->setSpacing(0);

    gga_text_edit_ = new QTextEdit(gga_text_container_);
    gga_text_edit_->setReadOnly(true);
    gga_text_edit_->document()->setMaximumBlockCount(kGgaMaxVisibleLines);
    gga_text_container_layout_->addWidget(gga_text_edit_);
    gga_layout_->addWidget(gga_text_container_);

    main_layout_->addWidget(gga_group_);
    gga_button_spacer_ = new QSpacerItem(0, 8, QSizePolicy::Minimum, QSizePolicy::Fixed);
    main_layout_->addSpacerItem(gga_button_spacer_);

    button_layout_ = new QHBoxLayout();
    button_layout_->setSpacing(6);

    start_btn_ = new QPushButton(this);
    connect(start_btn_, &QPushButton::clicked, this, &RtkConfigDialog::onStartClicked);

    stop_btn_ = new QPushButton(this);
    stop_btn_->setEnabled(false);
    connect(stop_btn_, &QPushButton::clicked, this, &RtkConfigDialog::onStopClicked);

    test_btn_ = new QPushButton(this);
    connect(test_btn_, &QPushButton::clicked, this, &RtkConfigDialog::onTestClicked);

    clear_log_btn_ = new QPushButton(this);
    connect(clear_log_btn_, &QPushButton::clicked, this, &RtkConfigDialog::onClearLogClicked);

    save_config_btn_ = new QPushButton(this);
    connect(save_config_btn_, &QPushButton::clicked, this, &RtkConfigDialog::onSaveConfigClicked);

    load_config_btn_ = new QPushButton(this);
    connect(load_config_btn_, &QPushButton::clicked, this, &RtkConfigDialog::onLoadConfigClicked);

    button_layout_->addWidget(start_btn_);
    button_layout_->addWidget(stop_btn_);
    button_layout_->addWidget(test_btn_);
    button_layout_->addWidget(clear_log_btn_);
    button_layout_->addStretch();
    button_layout_->addWidget(save_config_btn_);
    button_layout_->addWidget(load_config_btn_);

    main_layout_->addLayout(button_layout_);

    log_group_ = new QGroupBox(this);
    log_layout_ = new QVBoxLayout(log_group_);
    log_layout_->setSpacing(4);
    log_layout_->setContentsMargins(10, 30, 10, 4);

    log_text_container_ = new QWidget(log_group_);
    log_text_container_layout_ = new QVBoxLayout(log_text_container_);
    log_text_container_layout_->setContentsMargins(0, 0, 0, 2);
    log_text_container_layout_->setSpacing(0);

    log_text_edit_ = new QTextEdit(log_text_container_);
    log_text_edit_->setReadOnly(true);
    log_text_container_layout_->addWidget(log_text_edit_);
    log_layout_->addWidget(log_text_container_);

    main_layout_->addWidget(log_group_);

    status_label_ = new QLabel(this);
    status_label_->setObjectName(QStringLiteral("rtkStatusLabel"));
    status_label_->setWordWrap(false);
    status_label_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    main_layout_->addWidget(status_label_);
}

QString RtkConfigDialog::textFor(const QString& english, const QString& chinese) const
{
    return is_english_ ? english : chinese;
}

void RtkConfigDialog::setEnglish(bool english)
{
    is_english_ = english;
    refreshPortCombos();

    setWindowTitle(textFor("RTK NTRIP Configuration", "RTK NTRIP 配置"));
    config_group_->setTitle(textFor("NTRIP Server Configuration", "NTRIP 服务器配置"));
    output_group_->setTitle(textFor("RTCM Output Configuration", "RTCM 输出配置"));
    gga_group_->setTitle(textFor("GGA Monitor", "GGA 监视"));
    log_group_->setTitle(textFor("RTK Service Log", "RTK 服务日志"));

    server_label_->setText(textFor("Server Address:", "服务器地址:"));
    port_label_->setText(textFor("Port:", "端口:"));
    username_label_->setText(textFor("Username:", "用户名:"));
    password_label_->setText(textFor("Password:", "密码:"));
    mountpoint_label_->setText(textFor("Mountpoint:", "挂载点:"));
    output_port_label_->setText(textFor("Output Port:", "输出串口:"));
    baudrate_label_->setText(textFor("Baudrate:", "波特率:"));
    main_antenna_lever_label_->setText(textFor("Main Antenna Lever Arm (m):", "主天线杆臂 (m):"));
    timeout_label_->setText(textFor("Timeout (ms):", "超时 (ms):"));
    reconnect_label_->setText(textFor("Reconnect (ms):", "重连间隔 (ms):"));

    server_edit_->setPlaceholderText(textFor("e.g. rtk.ntrip.org", "例如: rtk.ntrip.org"));
    mountpoint_edit_->setPlaceholderText(textFor("e.g. RTCM33", "例如: RTCM33"));
    main_antenna_lever_x_edit_->setPlaceholderText(textFor("forward", "前向"));
    main_antenna_lever_y_edit_->setPlaceholderText(textFor("right", "右向"));
    main_antenna_lever_z_edit_->setPlaceholderText(textFor("down", "下向"));
    main_antenna_lever_help_btn_->setToolTip(mainAntennaLeverArmHelpText());

    refresh_ports_btn_->setText(textFor("Refresh", "刷新"));
    auto_detect_ports_btn_->setText(textFor("Auto Detect", "自动识别"));
    fetch_mountpoints_btn_->setText(textFor("Detect Mountpoints", "检测挂载点"));
    apply_main_antenna_lever_btn_->setText(textFor("Apply Lever Arm", "下发杆臂"));
    start_btn_->setText(textFor("Start", "启动"));
    stop_btn_->setText(textFor("Stop", "停止"));
    test_btn_->setText(textFor("Test Connection", "测试连接"));
    save_config_btn_->setText(textFor("Save Config", "保存配置"));
    load_config_btn_->setText(textFor("Load Config", "加载配置"));
    clear_log_btn_->setText(textFor("Clear Log", "清空日志"));

    updateGgaMonitorText();
    updateGgaMonitorButton();
    applyScaledUiMetrics();
    updateButtonStates();
}

int RtkConfigDialog::scalePixels(int pixels) const
{
    return static_cast<int>(std::lround(pixels * font_scale_percent_ / 100.0));
}

void RtkConfigDialog::applyScaledUiMetrics()
{
    auto applyButtonWidth = [this](QPushButton *button, int baseWidth) {
        if (!button)
        {
            return;
        }

        const QFontMetrics metrics(button->font());
        const int textWidth = metrics.horizontalAdvance(button->text());
        const int targetWidth = std::max(scalePixels(baseWidth), textWidth + scalePixels(28));
        const int targetHeight = std::max(scalePixels(38), metrics.height() + scalePixels(8));
        button->setFixedWidth(targetWidth);
        button->setFixedHeight(targetHeight);
        button->setStyleSheet(QString(
            "QPushButton { padding: %1px %2px; min-height: %3px; }")
            .arg(scalePixels(2))
            .arg(scalePixels(10))
            .arg(std::max(1, targetHeight - scalePixels(4))));
    };

    if (main_layout_)
    {
        main_layout_->setSpacing(scalePixels(8));
        main_layout_->setContentsMargins(scalePixels(12), scalePixels(12), scalePixels(12), scalePixels(12));
    }

    if (config_layout_)
    {
        config_layout_->setHorizontalSpacing(scalePixels(6));
        config_layout_->setVerticalSpacing(scalePixels(10));
        config_layout_->setContentsMargins(scalePixels(10), scalePixels(30), scalePixels(10), scalePixels(10));
        for (int row = 0; row < 2; ++row)
        {
            config_layout_->setRowMinimumHeight(row, scalePixels(42));
        }
    }

    if (output_layout_)
    {
        output_layout_->setHorizontalSpacing(scalePixels(6));
        output_layout_->setVerticalSpacing(scalePixels(10));
        output_layout_->setContentsMargins(scalePixels(10), scalePixels(30), scalePixels(10), scalePixels(10));
        output_layout_->setColumnMinimumWidth(2, scalePixels(88));
        output_layout_->setColumnMinimumWidth(3, scalePixels(108));
        output_layout_->setColumnMinimumWidth(4, scalePixels(74));
        for (int row = 0; row < 3; ++row)
        {
            output_layout_->setRowMinimumHeight(row, scalePixels(40));
        }
    }

    if (button_layout_)
    {
        button_layout_->setSpacing(scalePixels(6));
    }

    if (gga_layout_)
    {
        gga_layout_->setSpacing(scalePixels(6));
        gga_layout_->setContentsMargins(scalePixels(10), scalePixels(30), scalePixels(10), scalePixels(12));
    }

    if (gga_text_container_layout_)
    {
        gga_text_container_layout_->setContentsMargins(0, 0, 0, scalePixels(8));
    }

    if (gga_button_spacer_)
    {
        gga_button_spacer_->changeSize(0, scalePixels(8), QSizePolicy::Minimum, QSizePolicy::Fixed);
    }

    if (gga_header_layout_)
    {
        gga_header_layout_->setSpacing(scalePixels(8));
    }

    if (log_layout_)
    {
        log_layout_->setSpacing(scalePixels(4));
        log_layout_->setContentsMargins(scalePixels(10), scalePixels(30), scalePixels(10), scalePixels(4));
    }

    if (log_text_container_layout_)
    {
        log_text_container_layout_->setContentsMargins(0, 0, 0, scalePixels(2));
        log_text_container_layout_->setSpacing(0);
    }

    server_edit_->setMinimumWidth(scalePixels(220));
    server_edit_->setMinimumHeight(scalePixels(34));
    port_edit_->setMaximumWidth(scalePixels(80));
    port_edit_->setMinimumHeight(scalePixels(34));
    username_edit_->setMinimumWidth(scalePixels(180));
    username_edit_->setMinimumHeight(scalePixels(34));
    password_edit_->setMinimumWidth(scalePixels(180));
    password_edit_->setMinimumHeight(scalePixels(34));
    mountpoint_edit_->setMinimumHeight(scalePixels(34));
    mountpoint_edit_->setMinimumWidth(scalePixels(150));

    output_port_combo_->setMinimumWidth(scalePixels(200));
    output_port_combo_->setMinimumHeight(scalePixels(30));
    baudrate_combo_->setMinimumWidth(scalePixels(140));
    baudrate_combo_->setMinimumHeight(scalePixels(30));
    if (main_antenna_lever_help_btn_)
    {
        const int helpSize = scalePixels(22);
        main_antenna_lever_help_btn_->setFixedSize(helpSize, helpSize);
        main_antenna_lever_help_btn_->setStyleSheet(QString(
            "QToolButton { border: 1px solid #9e9e9e; border-radius: %1px; color: #000000; font-weight: bold; }"
            "QToolButton:hover { background: #eeeeee; }")
            .arg(helpSize / 2));
    }
    for (QLineEdit *edit : {main_antenna_lever_x_edit_, main_antenna_lever_y_edit_, main_antenna_lever_z_edit_})
    {
        if (edit)
        {
            edit->setMinimumWidth(scalePixels(70));
            edit->setMinimumHeight(scalePixels(34));
        }
    }
    timeout_combo_->setMinimumWidth(scalePixels(140));
    timeout_combo_->setMinimumHeight(scalePixels(30));
    reconnect_combo_->setMinimumWidth(scalePixels(140));
    reconnect_combo_->setMinimumHeight(scalePixels(30));
    gga_port_combo_->setMinimumWidth(scalePixels(240));
    gga_port_combo_->setMinimumHeight(scalePixels(30));

    gga_status_label_->setMinimumHeight(scalePixels(24));
    const int ggaTextHeight = scalePixels(72);
    const int ggaTextBottomGap = scalePixels(8);
    gga_text_edit_->setFixedHeight(ggaTextHeight);
    gga_text_edit_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    if (gga_text_container_)
    {
        gga_text_container_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        gga_text_container_->setFixedHeight(ggaTextHeight + ggaTextBottomGap);
    }
    gga_text_edit_->document()->setDocumentMargin(scalePixels(12));
    const QMargins ggaMargins = gga_layout_ ? gga_layout_->contentsMargins() : QMargins();
    const int headerHeight = std::max({gga_port_info_label_->sizeHint().height(),
                                       gga_port_combo_->sizeHint().height(),
                                       gga_frequency_label_->sizeHint().height()});
    const int statusHeight = std::max(gga_status_label_->minimumHeight(), gga_status_label_->sizeHint().height());
    const int verticalSpacing = gga_layout_ ? gga_layout_->spacing() : 0;
    const int ggaGroupHeight = ggaMargins.top()
        + headerHeight
        + verticalSpacing
        + statusHeight
        + verticalSpacing
        + ggaTextHeight
        + ggaTextBottomGap
        + ggaMargins.bottom();
    gga_group_->setFixedHeight(ggaGroupHeight);

    applyButtonWidth(refresh_ports_btn_, 80);
    applyButtonWidth(auto_detect_ports_btn_, 96);
    applyButtonWidth(apply_main_antenna_lever_btn_, 132);
    applyButtonWidth(fetch_mountpoints_btn_, 128);
    applyButtonWidth(start_btn_, 80);
    applyButtonWidth(stop_btn_, 80);
    applyButtonWidth(test_btn_, 120);
    applyButtonWidth(gga_toggle_btn_, 110);
    applyButtonWidth(save_config_btn_, 100);
    applyButtonWidth(load_config_btn_, 100);
    applyButtonWidth(clear_log_btn_, 96);

    log_text_edit_->setMinimumWidth(scalePixels(200));
    const QFontMetrics logMetrics(log_text_edit_->font());
    const int logDocumentMargin = scalePixels(10);
    const int logTextHeight = logMetrics.lineSpacing() * kRtkLogVisibleLines + logDocumentMargin * 2;
    log_text_edit_->setFixedHeight(logTextHeight);
    log_text_edit_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    log_text_edit_->document()->setDocumentMargin(logDocumentMargin);
    const int logTextBottomGap = scalePixels(2);
    if (log_text_container_)
    {
        log_text_container_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        log_text_container_->setFixedHeight(logTextHeight + logTextBottomGap);
    }
    if (log_group_ && log_layout_)
    {
        const QMargins logMargins = log_layout_->contentsMargins();
        const int logGroupHeight = logMargins.top() + logTextHeight + logTextBottomGap + logMargins.bottom();
        log_group_->setFixedHeight(logGroupHeight);
        log_group_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    }
    gga_text_edit_->setMinimumWidth(scalePixels(200));

    if (main_layout_)
    {
        main_layout_->invalidate();
    }

    const QSize layoutHint = main_layout_ ? main_layout_->sizeHint() : QSize();
    const QSize minimumDialogSize(scalePixels(base_minimum_dialog_size_.width()), scalePixels(base_minimum_dialog_size_.height()));
    const QSize targetMinimumSize = minimumDialogSize.boundedTo(VaporView::screenFractionSize(this));
    setMinimumSize(targetMinimumSize);
    if (!isMaximized() && !isFullScreen())
    {
        const QSize preferredDialogSize(
            scalePixels(base_dialog_size_.width()),
            std::max(minimumDialogSize.height(), layoutHint.height()));
        const QSize targetSize = VaporView::defaultWindowSizeWithinScreenFraction(
            this,
            preferredDialogSize,
            0.5,
            targetMinimumSize);
        if (targetSize != size())
        {
            resize(targetSize);
        }
    }
}

void RtkConfigDialog::setFontScale(int percent)
{
    if (percent < 70 || percent > 150)
    {
        percent = 100;
    }

    QSize targetSize = size();
    if (!isMaximized() && !isFullScreen())
    {
        targetSize = VaporView::defaultWindowSizeWithinScreenFraction(
            this,
            QSize(
                std::max(1, static_cast<int>(std::lround(base_dialog_size_.width() * percent / 100.0))),
                std::max(1, height())),
            0.5,
            QSize(
                std::max(1, static_cast<int>(std::lround(base_minimum_dialog_size_.width() * percent / 100.0))),
                std::max(1, static_cast<int>(std::lround(base_minimum_dialog_size_.height() * percent / 100.0))))
        );
    }

    if (font_scale_percent_ == percent)
    {
        applyScaledUiMetrics();
        if (!isMaximized() && !isFullScreen())
        {
            targetSize = targetSize.expandedTo(minimumSize()).boundedTo(VaporView::screenFractionSize(this));
            if (targetSize != size())
            {
                resize(targetSize);
            }
        }
        return;
    }

    font_scale_percent_ = percent;
    applyScaledUiMetrics();
    if (!isMaximized() && !isFullScreen())
    {
        targetSize = targetSize.expandedTo(minimumSize()).boundedTo(VaporView::screenFractionSize(this));
        if (targetSize != size())
        {
            resize(targetSize);
        }
    }
}

void RtkConfigDialog::loadSettings()
{
    QSettings settings("VaporView", "RtkConfig");

    server_edit_->setText(settings.value("server", "").toString());
    port_edit_->setText(settings.value("port", "2101").toString());
    username_edit_->setText(settings.value("username", "").toString());
    password_edit_->setText(settings.value("password", "").toString());
    mountpoint_edit_->setText(settings.value("mountpoint", "").toString());
    main_antenna_lever_x_edit_->setText(settings.value("main_antenna_lever_x_m", "").toString());
    main_antenna_lever_y_edit_->setText(settings.value("main_antenna_lever_y_m", "").toString());
    main_antenna_lever_z_edit_->setText(settings.value("main_antenna_lever_z_m", "").toString());
    refreshPortCombos();
#ifdef _WIN32
    output_port_combo_->setCurrentText(settings.value("output_port", "COM1").toString());
#else
    output_port_combo_->setCurrentText(settings.value("output_port", "/dev/ttyS0").toString());
#endif
    applySavedGgaSource(settings.value("gga_source", settings.value("gga_port", QString::fromLatin1(kEpsilonMainGgaSourceKey))).toString());
    baudrate_combo_->setCurrentText(settings.value("baudrate", "115200").toString());
    timeout_combo_->setCurrentText(settings.value("timeout", "5000").toString());
    reconnect_combo_->setCurrentText(settings.value("reconnect", "1000").toString());
    updateGgaMonitorText();
}

void RtkConfigDialog::saveSettings()
{
    QSettings settings("VaporView", "RtkConfig");

    settings.setValue("server", server_edit_->text());
    settings.setValue("port", port_edit_->text());
    settings.setValue("username", username_edit_->text());
    settings.setValue("password", password_edit_->text());
    settings.setValue("mountpoint", mountpoint_edit_->text());
    settings.setValue("main_antenna_lever_x_m", main_antenna_lever_x_edit_->text());
    settings.setValue("main_antenna_lever_y_m", main_antenna_lever_y_edit_->text());
    settings.setValue("main_antenna_lever_z_m", main_antenna_lever_z_edit_->text());
    settings.setValue("output_port", output_port_combo_->currentText());
    settings.setValue("gga_source", savedGgaSourceValue());
    settings.setValue("gga_port", isMainGgaSourceSelected() ? QString() : ggaPortName());
    settings.setValue("baudrate", baudrate_combo_->currentText());
    settings.setValue("timeout", timeout_combo_->currentText());
    settings.setValue("reconnect", reconnect_combo_->currentText());
}

void RtkConfigDialog::setPreferredOutputPortAndBaud(const QString& portName, const QString& baudText)
{
    refreshPortCombos();
    if (!portName.trimmed().isEmpty() && output_port_combo_)
    {
        output_port_combo_->setCurrentText(portName.trimmed());
    }
    if (!baudText.trimmed().isEmpty() && baudrate_combo_)
    {
        baudrate_combo_->setCurrentText(baudText.trimmed());
    }
}

void RtkConfigDialog::setEpsilonMainPortAndBaud(const QString& portName, const QString& baudText)
{
    epsilon_main_port_ = portName.trimmed();
    bool ok = false;
    const int baudrate = baudText.trimmed().toInt(&ok);
    epsilon_main_baudrate_ = ok ? baudrate : 921600;
}

void RtkConfigDialog::setEpsilonDataProvider(std::function<VaporView::EpsilonData()> provider)
{
    epsilon_data_provider_ = std::move(provider);
}

void RtkConfigDialog::setEpsilonMainAntennaLeverArmApplier(std::function<bool(double, double, double, QString*)> applier)
{
    epsilon_main_antenna_lever_arm_applier_ = std::move(applier);
}

bool RtkConfigDialog::buildRtkStreamConfig(RtkStreamConfig *config, QString *description) const
{
    const QString server = server_edit_->text().trimmed();
    const QString port = port_edit_->text().trimmed();
    const QString username = username_edit_->text().trimmed();
    const QString password = password_edit_->text();
    const QString mountpoint = mountpoint_edit_->text().trimmed();
    const QString outputPort = output_port_combo_->currentText().trimmed();
    bool baudrateOk = false;
    const int baudrate = baudrate_combo_->currentText().toInt(&baudrateOk);
    const int timeout = comboIntValue(timeout_combo_, 5000);
    const int reconnect = comboIntValue(reconnect_combo_, 1000);
    const VaporView::EpsilonData epsilonData = epsilon_data_provider_
        ? epsilon_data_provider_()
        : VaporView::EpsilonData();
    const bool hasEpsilonPosition = isUsableEpsilonNmeaPosition(epsilonData);

    if (server.isEmpty() || mountpoint.isEmpty() || outputPort.isEmpty())
    {
        return false;
    }

    if (config)
    {
        config->server = server;
        config->port = port.isEmpty() ? QStringLiteral("2101") : port;
        config->username = username;
        config->password = password;
        config->mountpoint = mountpoint;
        config->outputPort = outputPort;
        config->baudrate = baudrateOk ? baudrate : 115200;
        config->timeoutMs = timeout;
        config->reconnectMs = reconnect;
        config->relayBack = hasEpsilonPosition ? 0 : 1;
        config->sendNmeaGga = hasEpsilonPosition;
        config->nmeaGgaCycleMs = kGgaSendCycleMs;
        config->nmeaLatitudeDeg = epsilonData.latitude_deg;
        config->nmeaLongitudeDeg = epsilonData.longitude_deg;
        config->nmeaHeightM = epsilonData.height_m;
    }

    QString ntripUrl;
    if (!username.isEmpty())
    {
        ntripUrl = QString("ntrip://%1:%2@%3:%4/%5")
            .arg(username)
            .arg(password)
            .arg(server)
            .arg(port.isEmpty() ? QStringLiteral("2101") : port)
            .arg(mountpoint);
    }
    else
    {
        ntripUrl = QString("ntrip://%1:%2/%3")
            .arg(server)
            .arg(port.isEmpty() ? QStringLiteral("2101") : port)
            .arg(mountpoint);
    }

    if (description)
    {
        const QString serialUrl = QString("serial://%1:%2:8:n:1:off")
            .arg(outputPort)
            .arg(baudrateOk ? baudrate : 115200);
        const QString ggaSource = hasEpsilonPosition
            ? textFor("GGA source: EPSILON main-port position [%1, %2, %3 m] at 1 Hz",
                      "GGA 来源: EPSILON 主串口定位 [%1, %2, %3 m]，1Hz")
                  .arg(QString::number(epsilonData.latitude_deg, 'f', 8),
                       QString::number(epsilonData.longitude_deg, 'f', 8),
                       QString::number(epsilonData.height_m, 'f', 3))
            : textFor("GGA source: output-port relay fallback; connect EPSILON main port first to generate GGA without reading from the RTCM port.",
                      "GGA 来源: 输出口回读兼容模式；请先连接 EPSILON 主串口，才能不依赖 RTCM 串口生成 GGA。");
        *description = textFor("Embedded RTK stream: %1 -> %2\n%3",
                               "内嵌 RTK 流服务: %1 -> %2\n%3")
            .arg(ntripUrl, serialUrl)
            .arg(ggaSource);
    }

    return true;
}

void RtkConfigDialog::updateButtonStates()
{
    const bool busy = isBackgroundTaskRunning();
    start_btn_->setEnabled(!is_running_ && !busy);
    stop_btn_->setEnabled(is_running_ && !busy);
    test_btn_->setEnabled(!is_running_ && !busy);
    apply_main_antenna_lever_btn_->setEnabled(!is_running_ && !busy);
    fetch_mountpoints_btn_->setEnabled(!busy);
    refresh_ports_btn_->setEnabled(!busy);
    auto_detect_ports_btn_->setEnabled(!busy);
    save_config_btn_->setEnabled(!busy);
    load_config_btn_->setEnabled(!busy);
    gga_toggle_btn_->setEnabled(!busy);

    if (busy)
    {
        const QString busyText = fetch_mountpoints_in_progress_.load()
            ? textFor("Status: Fetching mountpoints", "状态: 正在获取挂载点")
            : port_detection_in_progress_.load()
                ? textFor("Status: Detecting serial ports", "状态: 正在识别串口")
            : textFor("Status: Running no-signal RTK test", "状态: 正在执行无信号 RTK 测试");
        status_label_->setText(busyText);
        status_label_->setStyleSheet("QLabel { color: #ef6c00; font-weight: bold; }");
    }
    else if (is_running_)
    {
        status_label_->setText(textFor("Status: Running", "状态: 运行中"));
        status_label_->setStyleSheet("QLabel { color: #43a047; font-weight: bold; }");
    }
    else
    {
        status_label_->setText(textFor("Status: Stopped", "状态: 已停止"));
        status_label_->setStyleSheet(QString());
    }
}

void RtkConfigDialog::pollRtkServiceStatus(bool forceLog)
{
    if (!rtk_service_)
    {
        return;
    }

    const RtkStreamStats stats = rtk_service_->stats();
    const QString message = stats.message.isEmpty()
        ? textFor("Streaming RTCM data", "正在转发 RTCM 数据")
        : stats.message;

    const QString summaryLine = formatRtkStatusLine(
        stats,
        textFor("Streaming RTCM data", "正在转发 RTCM 数据"),
        is_english_);

    if ((forceLog || is_running_) && !message.isEmpty())
    {
        appendRawLogLine(summaryLine);
    }
    last_rtk_status_message_ = message;

    if (!stats.running && is_running_)
    {
        is_running_ = false;
        if (rtk_status_timer_ && rtk_status_timer_->isActive())
        {
            rtk_status_timer_->stop();
        }
        updateButtonStates();
        appendLog(textFor("RTK service stopped unexpectedly", "RTK 服务已意外停止"));
        return;
    }

    if (is_running_)
    {
        status_label_->setText(
            textFor("Status: Running (%1 bps in / %2 bps out)", "状态: 运行中 (%1 bps 输入 / %2 bps 输出)")
                .arg(stats.inputBps)
                .arg(stats.outputBps));
        status_label_->setStyleSheet("QLabel { color: #43a047; font-weight: bold; }");
    }
}

void RtkConfigDialog::onRtkStatusTimer()
{
    pollRtkServiceStatus(false);
}

QString RtkConfigDialog::mainGgaSourceLabel() const
{
    return textFor("EPSILON main port (generated GGA)", "EPSILON 主串口（生成GGA）");
}

bool RtkConfigDialog::isMainGgaSourceSelected() const
{
    if (!gga_port_combo_)
    {
        return true;
    }

    const QVariant data = gga_port_combo_->currentData();
    if (data.toString() == QString::fromLatin1(kEpsilonMainGgaSourceKey))
    {
        return true;
    }

    const QString text = gga_port_combo_->currentText().trimmed();
    return text == QString::fromLatin1(kEpsilonMainGgaSourceKey) ||
        text == mainGgaSourceLabel() ||
        (text.contains(QStringLiteral("EPSILON"), Qt::CaseInsensitive) &&
         (text.contains(QStringLiteral("main"), Qt::CaseInsensitive) ||
          text.contains(QStringLiteral("主串口"), Qt::CaseInsensitive)));
}

QString RtkConfigDialog::savedGgaSourceValue() const
{
    return isMainGgaSourceSelected()
        ? QString::fromLatin1(kEpsilonMainGgaSourceKey)
        : ggaPortName();
}

void RtkConfigDialog::applySavedGgaSource(const QString& source)
{
    if (!gga_port_combo_)
    {
        return;
    }

    const QString trimmed = source.trimmed();
    if (trimmed.isEmpty() ||
        trimmed == QString::fromLatin1(kEpsilonMainGgaSourceKey) ||
        trimmed == mainGgaSourceLabel() ||
        (trimmed.contains(QStringLiteral("EPSILON"), Qt::CaseInsensitive) &&
         (trimmed.contains(QStringLiteral("main"), Qt::CaseInsensitive) ||
          trimmed.contains(QStringLiteral("主串口"), Qt::CaseInsensitive))))
    {
        const int mainIndex = gga_port_combo_->findData(QString::fromLatin1(kEpsilonMainGgaSourceKey));
        gga_port_combo_->setCurrentIndex(mainIndex >= 0 ? mainIndex : 0);
        return;
    }

    gga_port_combo_->setCurrentText(trimmed);
}

QString RtkConfigDialog::ggaPortName() const
{
    if (!gga_port_combo_)
    {
#ifdef _WIN32
        return QStringLiteral("COM1");
#else
        return QStringLiteral("/dev/ttyS0");
#endif
    }

    return gga_port_combo_->currentText().trimmed();
}

int RtkConfigDialog::currentGgaBaudrate() const
{
    bool ok = false;
    const int baudrate = baudrate_combo_ ? baudrate_combo_->currentText().toInt(&ok) : 115200;
    return ok ? baudrate : 115200;
}

int RtkConfigDialog::currentOutputBaudrate() const
{
    bool ok = false;
    const int baudrate = baudrate_combo_ ? baudrate_combo_->currentText().toInt(&ok) : 115200;
    return ok ? baudrate : 115200;
}

void RtkConfigDialog::updateGgaFrequency(double hz)
{
    if (!gga_frequency_label_)
    {
        return;
    }

    gga_frequency_label_->setText(textFor("Actual Rate: %1 Hz", "真实频率: %1 Hz").arg(QString::number(std::max(0.0, hz), 'f', 2)));
}

void RtkConfigDialog::updateGgaStatusLabel(const QString& message, bool healthy)
{
    if (!gga_status_label_)
    {
        return;
    }

    gga_status_message_ = message;
    const QString color = healthy ? QStringLiteral("#2e7d32") : QStringLiteral("#a26a00");
    gga_status_label_->setText(message);
    gga_status_label_->setStyleSheet(QString("QLabel { color: %1; font-weight: bold; }").arg(color));
}

void RtkConfigDialog::updateGgaMonitorButton()
{
    if (!gga_toggle_btn_)
    {
        return;
    }

    gga_toggle_btn_->setText(gga_monitor_enabled_
        ? textFor("Stop Reading", "停止读取")
        : textFor("Read GGA", "读取GGA"));
    gga_toggle_btn_->setEnabled(true);
}

void RtkConfigDialog::updateGgaMonitorText()
{
    if (!gga_port_info_label_)
    {
        return;
    }

    gga_port_info_label_->setText(textFor("GGA Source:", "GGA来源:"));

    if (gga_status_message_.isEmpty())
    {
        const bool mainSource = isMainGgaSourceSelected();
        updateGgaStatusLabel(
            gga_monitor_enabled_
                ? (mainSource
                    ? textFor("Status: Waiting for EPSILON main-port position", "状态: 正在等待 EPSILON 主串口定位")
                    : textFor("Status: Waiting for serial data", "状态: 正在等待串口数据"))
                : textFor("Status: Click button to read GGA", "状态: 点击按钮开始读取GGA"),
            false);
    }
    else if (gga_status_message_.startsWith("Status:") || gga_status_message_.startsWith("状态:"))
    {
        const bool healthy = gga_status_label_->styleSheet().contains("#2e7d32");
        updateGgaStatusLabel(gga_status_message_, healthy);
    }

    if (gga_frequency_label_->text().isEmpty())
    {
        updateGgaFrequency(0.0);
    }
}

void RtkConfigDialog::startGgaMonitor()
{
    if (!gga_poll_timer_)
    {
        return;
    }

    gga_monitor_enabled_ = true;
    gga_buffer_.clear();
    gga_recent_intervals_sec_.clear();
    gga_has_sentence_time_ = false;
    gga_last_epsilon_sample_time_ = std::chrono::steady_clock::time_point();
    gga_last_epsilon_device_timestamp_us_ = 0;
    updateGgaFrequency(0.0);
    gga_status_message_.clear();
    updateGgaMonitorText();
    updateGgaMonitorButton();
    gga_last_open_attempt_ = std::chrono::steady_clock::time_point();
    if (!gga_poll_timer_->isActive())
    {
        gga_poll_timer_->start(kGgaPollIntervalMs);
    }
    onGgaPollTimer();
}

void RtkConfigDialog::stopGgaMonitor()
{
    if (gga_poll_timer_ && gga_poll_timer_->isActive())
    {
        gga_poll_timer_->stop();
    }

    if (gga_serial_.isOpen())
    {
        gga_serial_.close();
    }

    gga_buffer_.clear();
    gga_recent_intervals_sec_.clear();
    gga_has_sentence_time_ = false;
    gga_last_epsilon_sample_time_ = std::chrono::steady_clock::time_point();
    gga_last_epsilon_device_timestamp_us_ = 0;
    gga_monitor_enabled_ = false;
    gga_status_message_.clear();
    updateGgaFrequency(0.0);
    updateGgaMonitorText();
    updateGgaMonitorButton();
}

bool RtkConfigDialog::tryOpenGgaPort()
{
    if (!gga_monitor_enabled_)
    {
        return false;
    }

    if (isMainGgaSourceSelected())
    {
        if (gga_serial_.isOpen())
        {
            gga_serial_.close();
        }
        return true;
    }

    if (gga_serial_.isOpen())
    {
        return true;
    }

    const auto now = std::chrono::steady_clock::now();
    if (gga_last_open_attempt_.time_since_epoch().count() > 0 &&
        std::chrono::duration_cast<std::chrono::milliseconds>(now - gga_last_open_attempt_).count() < kGgaReconnectIntervalMs)
    {
        return false;
    }

    gga_last_open_attempt_ = now;
    const std::string port = ggaPortName().toStdString();
    if (port.empty())
    {
        updateGgaStatusLabel(textFor("Status: Please select a GGA source", "状态: 请选择 GGA 来源"), false);
        return false;
    }

    if (!gga_serial_.open(port, currentGgaBaudrate()))
    {
        updateGgaStatusLabel(
            textFor("Status: %1 unavailable, retrying...", "状态: %1 不可用，正在重试...").arg(ggaPortName()),
            false);
        return false;
    }

    gga_serial_.setNonBlocking(true);
    gga_buffer_.clear();
    gga_recent_intervals_sec_.clear();
    gga_has_sentence_time_ = false;
    updateGgaFrequency(0.0);
    updateGgaStatusLabel(textFor("Status: Listening on %1", "状态: 正在监听 %1").arg(ggaPortName()), true);
    updateGgaMonitorText();
    return true;
}

void RtkConfigDialog::pollMainGgaSource()
{
    if (!gga_monitor_enabled_)
    {
        return;
    }

    if (!epsilon_data_provider_)
    {
        updateGgaStatusLabel(
            textFor("Status: EPSILON main-port source is not connected", "状态: EPSILON 主串口来源未接入"),
            false);
        return;
    }

    const VaporView::EpsilonData data = epsilon_data_provider_();
    if (!isUsableEpsilonNmeaPosition(data))
    {
        updateGgaStatusLabel(
            textFor("Status: Waiting for valid EPSILON main-port position", "状态: 正在等待有效的 EPSILON 主串口定位"),
            false);
        return;
    }

    const bool sameSample =
        data.timestamp == gga_last_epsilon_sample_time_ &&
        data.device_timestamp_us == gga_last_epsilon_device_timestamp_us_;
    if (sameSample && gga_has_sentence_time_)
    {
        return;
    }

    const QString sentence = buildEpsilonGgaSentence(data);
    if (sentence.isEmpty())
    {
        updateGgaStatusLabel(
            textFor("Status: Failed to build GGA from EPSILON position", "状态: EPSILON 定位无法组装 GGA"),
            false);
        return;
    }

    gga_last_epsilon_sample_time_ = data.timestamp;
    gga_last_epsilon_device_timestamp_us_ = data.device_timestamp_us;
    handleGgaSentence(sentence);
    updateGgaStatusLabel(
        textFor("Status: Reading generated GGA from EPSILON main port", "状态: 正在读取 EPSILON 主串口生成的 GGA"),
        true);
}

void RtkConfigDialog::onGgaToggleClicked()
{
    if (gga_monitor_enabled_)
    {
        stopGgaMonitor();
        return;
    }

    startGgaMonitor();
}

bool RtkConfigDialog::sendReceiverCommands(const QStringList& commands, QString *errorMessage)
{
    const QString outputPort = output_port_combo_ ? output_port_combo_->currentText().trimmed() : QString();
    if (outputPort.isEmpty())
    {
        if (errorMessage)
        {
            *errorMessage = textFor("Please select an RTK output port first.", "请先选择 RTK 输出串口。");
        }
        return false;
    }

    VaporView::SerialPort serial;
    if (!serial.open(outputPort.toStdString(), currentOutputBaudrate()))
    {
        if (errorMessage)
        {
            *errorMessage = textFor("Failed to open %1: %2", "打开 %1 失败: %2")
                .arg(outputPort, QString::fromStdString(serial.lastError()));
        }
        return false;
    }

    auto appendResponseLines = [this](const QByteArray &buffer) {
        const QList<QByteArray> lines = buffer.split('\n');
        bool logged = false;
        for (const QByteArray &rawLine : lines)
        {
            const QString line = QString::fromLatin1(rawLine).trimmed();
            if (line.isEmpty())
            {
                continue;
            }
            appendLog(QStringLiteral("[RTK 接收] %1").arg(line));
            logged = true;
        }
        if (!logged)
        {
            appendLog(textFor("[RTK 接收] No response (command may have been accepted)",
                              "[RTK 接收] 无返回（命令可能已被接受）"));
        }
    };

    for (const QString &command : commands)
    {
        const QString trimmedCommand = command.trimmed();
        if (trimmedCommand.isEmpty())
        {
            continue;
        }

        const QByteArray payload = (trimmedCommand + QStringLiteral("\r\n")).toLatin1();
        appendLog(QStringLiteral("[RTK 发送] %1").arg(trimmedCommand));
        const ssize_t written = serial.write(payload.constData(), static_cast<size_t>(payload.size()));
        if (written != payload.size())
        {
            if (errorMessage)
            {
                *errorMessage = textFor("Failed to send command: %1", "命令发送失败: %1").arg(trimmedCommand);
            }
            serial.close();
            return false;
        }

        serial.flush();
        std::this_thread::sleep_for(std::chrono::milliseconds(200));

        QByteArray responseBuffer;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
        while (std::chrono::steady_clock::now() < deadline)
        {
            char buffer[512];
            const ssize_t readBytes = serial.read(buffer, sizeof(buffer));
            if (readBytes > 0)
            {
                responseBuffer.append(buffer, static_cast<int>(readBytes));
                std::this_thread::sleep_for(std::chrono::milliseconds(60));
                continue;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        appendResponseLines(responseBuffer);
    }

    serial.close();
    return true;
}

QString RtkConfigDialog::mainAntennaLeverArmHelpText() const
{
    return textFor(
        QStringLiteral("Main antenna lever arm is the GNSS main antenna phase-center position in the EPSILON/IMU frame, used to compensate the offset between the antenna and IMU during GNSS/INS fusion.\n\n"
                       "Measure from the EPSILON module center to the GNSS main antenna phase center. Enter X/Y/Z in meters in the module frame: X forward, Y right, Z down. If the antenna is above the module, Z is negative.\n\n"
                       "Command sent to EPSILON main port: #fconfig -> #fantearm x y z -> #fsave -> #fdeconfig."),
        QStringLiteral("主天线杆臂是 GNSS 主天线相位中心在 EPSILON/IMU 模组坐标系下的位置，用来补偿天线与惯导不重合带来的 GNSS/INS 杆臂误差。\n\n"
                       "测量时从 EPSILON 模组中心量到 GNSS 主天线相位中心，分别填写 X/Y/Z，单位米；坐标系为模组坐标系：X 向前、Y 向右、Z 向下。天线在模组上方时，Z 为负值。\n\n"
                       "下发到 EPSILON 主串口的命令：#fconfig -> #fantearm x y z -> #fsave -> #fdeconfig。"));
}

void RtkConfigDialog::onMainAntennaLeverHelpClicked()
{
    QMessageBox::information(
        this,
        textFor("Main Antenna Lever Arm", "主天线杆臂"),
        mainAntennaLeverArmHelpText());
}

bool RtkConfigDialog::parseMainAntennaLeverArm(double *x, double *y, double *z, QString *errorMessage) const
{
    auto parseValue = [this, errorMessage](const QLineEdit *edit, const QString& axis, double *value) {
        const QString original = edit ? edit->text().trimmed() : QString();
        if (original.isEmpty())
        {
            if (errorMessage)
            {
                *errorMessage = textFor("Enter %1 lever-arm value in meters. Use 0 if the offset is unknown.",
                                        "请输入 %1 方向杆臂值，单位米；未知可填 0。").arg(axis);
            }
            return false;
        }

        QString normalized = original;
        normalized.replace(QLatin1Char(','), QLatin1Char('.'));
        bool ok = false;
        double parsed = QLocale::c().toDouble(normalized, &ok);
        if (!ok)
        {
            parsed = normalized.toDouble(&ok);
        }

        if (!ok || !std::isfinite(parsed))
        {
            if (errorMessage)
            {
                *errorMessage = textFor("Invalid %1 lever-arm value: %2",
                                        "%1 方向杆臂值无效: %2").arg(axis, original);
            }
            return false;
        }

        *value = parsed;
        return true;
    };

    return parseValue(main_antenna_lever_x_edit_, QStringLiteral("X"), x) &&
        parseValue(main_antenna_lever_y_edit_, QStringLiteral("Y"), y) &&
        parseValue(main_antenna_lever_z_edit_, QStringLiteral("Z"), z);
}

void RtkConfigDialog::onApplyMainAntennaLeverArmClicked()
{
    if (is_running_)
    {
        QMessageBox::warning(
            this,
            textFor("RTK Running", "RTK 运行中"),
            textFor("Stop the RTK service before changing the EPSILON main antenna lever arm.",
                    "请先停止 RTK 服务，再修改 EPSILON 主天线杆臂。"));
        return;
    }

    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    QString errorMessage;
    if (!parseMainAntennaLeverArm(&x, &y, &z, &errorMessage))
    {
        QMessageBox::warning(
            this,
            textFor("Invalid Lever Arm", "杆臂无效"),
            errorMessage);
        return;
    }

    if (!epsilon_main_antenna_lever_arm_applier_)
    {
        QMessageBox::warning(
            this,
            textFor("EPSILON Unavailable", "EPSILON 不可用"),
            textFor("EPSILON main-port command channel is not available. Open this dialog from the main window after selecting the EPSILON main port.",
                    "EPSILON 主串口命令通道不可用。请在主页面选择 EPSILON 主串口后再打开此配置。"));
        return;
    }

    const QString values = QStringLiteral("X=%1 m, Y=%2 m, Z=%3 m")
        .arg(QString::number(x, 'f', 4),
             QString::number(y, 'f', 4),
             QString::number(z, 'f', 4));
    const QString target = epsilon_main_port_.isEmpty()
        ? textFor("selected EPSILON main port", "已选择的 EPSILON 主串口")
        : QStringLiteral("%1 @ %2").arg(epsilon_main_port_).arg(epsilon_main_baudrate_);
    appendLog(textFor("Applying EPSILON main antenna lever arm via %1: %2",
                      "正在通过 %1 下发 EPSILON 主天线杆臂: %2").arg(target, values));

    errorMessage.clear();
    if (!epsilon_main_antenna_lever_arm_applier_(x, y, z, &errorMessage))
    {
        QMessageBox::warning(
            this,
            textFor("Command Failed", "命令发送失败"),
            errorMessage.isEmpty()
                ? textFor("Failed to apply EPSILON main antenna lever arm.",
                          "EPSILON 主天线杆臂下发失败。")
                : errorMessage);
        return;
    }

    saveSettings();
    appendLog(textFor("EPSILON main antenna lever arm updated: %1",
                      "EPSILON 主天线杆臂已更新: %1").arg(values));
    QMessageBox::information(
        this,
        textFor("Lever Arm Updated", "杆臂已更新"),
        textFor("EPSILON has been sent: #fantearm %1 %2 %3",
                "已向 EPSILON 下发: #fantearm %1 %2 %3")
            .arg(QString::number(x, 'f', 4),
                 QString::number(y, 'f', 4),
                 QString::number(z, 'f', 4)));
}

void RtkConfigDialog::processGgaBuffer()
{
    while (true)
    {
        int newlineIndex = gga_buffer_.indexOf('\n');
        if (newlineIndex < 0)
        {
            break;
        }

        QString line = gga_buffer_.left(newlineIndex);
        gga_buffer_.remove(0, newlineIndex + 1);
        line = line.trimmed();
        if (line.isEmpty())
        {
            continue;
        }

        if (kGgaSentencePattern.match(line).hasMatch())
        {
            handleGgaSentence(line);
        }
    }
}

void RtkConfigDialog::handleGgaSentence(const QString& sentence)
{
    const auto now = std::chrono::steady_clock::now();
    if (gga_has_sentence_time_)
    {
        const double intervalSeconds =
            std::chrono::duration_cast<std::chrono::duration<double>>(now - gga_last_sentence_time_).count();
        if (intervalSeconds > 0.0)
        {
            gga_recent_intervals_sec_.push_back(intervalSeconds);
            while (gga_recent_intervals_sec_.size() > 20)
            {
                gga_recent_intervals_sec_.pop_front();
            }

            double total = 0.0;
            for (double value : gga_recent_intervals_sec_)
            {
                total += value;
            }

            if (!gga_recent_intervals_sec_.empty() && total > 0.0)
            {
                updateGgaFrequency(static_cast<double>(gga_recent_intervals_sec_.size()) / total);
            }
        }
    }
    else
    {
        updateGgaFrequency(0.0);
    }

    gga_has_sentence_time_ = true;
    gga_last_sentence_time_ = now;
    updateGgaStatusLabel(textFor("Status: Receiving GGA data", "状态: 正在接收 GGA 数据"), true);

    if (gga_text_edit_)
    {
        QScrollBar *scrollBar = gga_text_edit_->verticalScrollBar();
        const bool stickToBottom = !scrollBar || scrollBar->value() >= (scrollBar->maximum() - 2);
        const int previousValue = scrollBar ? scrollBar->value() : 0;
        const QString timestamp = QDateTime::currentDateTime().toString("hh:mm:ss.zzz");
        gga_text_edit_->append(QString("[%1] %2").arg(timestamp, sentence));
        trimGgaDisplay();
        QTimer::singleShot(0, this, [this, stickToBottom, previousValue]() {
            if (!gga_text_edit_)
            {
                return;
            }

            QScrollBar *updatedScrollBar = gga_text_edit_->verticalScrollBar();
            if (!updatedScrollBar)
            {
                return;
            }

            if (stickToBottom)
            {
                updatedScrollBar->setValue(updatedScrollBar->maximum());
            }
            else
            {
                updatedScrollBar->setValue(std::min(previousValue, updatedScrollBar->maximum()));
            }
        });
    }
}

void RtkConfigDialog::trimGgaDisplay()
{
    if (!gga_text_edit_)
    {
        return;
    }

    QTextDocument *document = gga_text_edit_->document();
    if (!document)
    {
        return;
    }

    while (document->blockCount() > kGgaMaxVisibleLines)
    {
        QTextBlock firstBlock = document->begin();
        if (!firstBlock.isValid())
        {
            break;
        }

        QTextCursor cursor(firstBlock);
        cursor.select(QTextCursor::BlockUnderCursor);
        cursor.removeSelectedText();
        cursor.deleteChar();
    }
}

void RtkConfigDialog::onGgaPollTimer()
{
    if (isMainGgaSourceSelected())
    {
        if (gga_serial_.isOpen())
        {
            gga_serial_.close();
        }
        pollMainGgaSource();
        if (gga_has_sentence_time_)
        {
            const auto now = std::chrono::steady_clock::now();
            const auto staleMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - gga_last_sentence_time_).count();
            if (staleMs > kGgaStaleTimeoutMs)
            {
                gga_recent_intervals_sec_.clear();
                updateGgaFrequency(0.0);
                updateGgaStatusLabel(textFor("Status: Waiting for next EPSILON main-port position", "状态: 正在等待下一帧 EPSILON 主串口定位"), false);
            }
        }
        return;
    }

    if (!tryOpenGgaPort())
    {
        return;
    }

    char buffer[512];
    while (true)
    {
        const ssize_t bytesRead = gga_serial_.read(buffer, sizeof(buffer));
        if (bytesRead > 0)
        {
            gga_buffer_.append(QString::fromLatin1(buffer, static_cast<int>(bytesRead)));
            processGgaBuffer();
            continue;
        }

        if (bytesRead < 0)
        {
            gga_serial_.close();
            updateGgaFrequency(0.0);
            updateGgaStatusLabel(textFor("Status: %1 read failed, reconnecting...", "状态: %1 读取失败，正在重连...").arg(ggaPortName()), false);
        }
        break;
    }

    if (gga_has_sentence_time_)
    {
        const auto now = std::chrono::steady_clock::now();
        const auto staleMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - gga_last_sentence_time_).count();
        if (staleMs > kGgaStaleTimeoutMs)
        {
            gga_recent_intervals_sec_.clear();
            updateGgaFrequency(0.0);
            updateGgaStatusLabel(textFor("Status: Waiting for next GGA sentence", "状态: 正在等待下一条 GGA 语句"), false);
        }
    }
}

void RtkConfigDialog::refreshPortCombos()
{
    const QStringList ports = getAvailablePorts();
    const QString currentOutput = output_port_combo_ ? output_port_combo_->currentText().trimmed() : QString();
    const QString currentGga = gga_port_combo_ ? gga_port_combo_->currentText().trimmed() : QString();

    if (output_port_combo_)
    {
        const QSignalBlocker blocker(output_port_combo_);
        output_port_combo_->clear();
        output_port_combo_->addItems(ports);
        if (!currentOutput.isEmpty())
        {
            output_port_combo_->setCurrentText(currentOutput);
        }
    }

    if (gga_port_combo_)
    {
        const QSignalBlocker blocker(gga_port_combo_);
        gga_port_combo_->clear();
        gga_port_combo_->addItem(mainGgaSourceLabel(), QString::fromLatin1(kEpsilonMainGgaSourceKey));
        gga_port_combo_->insertSeparator(gga_port_combo_->count());
        gga_port_combo_->addItems(ports);
        applySavedGgaSource(currentGga);
    }
}

QStringList RtkConfigDialog::getAvailablePorts() const
{
    QStringList ports;
    const auto infos = QSerialPortInfo::availablePorts();
    for (const QSerialPortInfo& info : infos)
    {
#ifdef _WIN32
        ports.append(info.portName());
#else
        const QString path = info.systemLocation();
        ports.append(path.isEmpty() ? info.portName() : path);
#endif
    }

    ports.removeDuplicates();
    ports.sort();
    return ports;
}

void RtkConfigDialog::onRefreshPortsClicked()
{
    refreshPortCombos();
    appendLog(textFor("Ports refreshed: %1 found", "串口已刷新: 发现 %1 个").arg(getAvailablePorts().size()));
}

void RtkConfigDialog::onAutoDetectPortsClicked()
{
    if (is_running_ || isBackgroundTaskRunning())
    {
        return;
    }

    if (port_detection_thread_.joinable())
    {
        port_detection_thread_.join();
    }

    refreshPortCombos();
    const QStringList portNames = getAvailablePorts();
    if (portNames.isEmpty())
    {
        appendLog(textFor("Auto detect stopped: no serial ports found.", "自动识别结束：当前没有发现可用串口。"));
        return;
    }

    const QStringList baudTexts = buildProbeBaudList(baudrate_combo_);
    port_detection_in_progress_.store(true);
    updateButtonStates();
    appendLog(textFor("Starting RTK output port auto detect...", "开始自动识别 RTK 输出串口..."));

    QPointer<RtkConfigDialog> self(this);
    port_detection_thread_ = std::thread([self, portNames, baudTexts]() {
        if (!self)
        {
            return;
        }

        auto queueLog = [self](const QString& message) {
            if (!self)
            {
                return;
            }
            QMetaObject::invokeMethod(self.data(), [self, message]() {
                if (self)
                {
                    self->appendLog(message);
                }
            }, Qt::QueuedConnection);
        };

        QString detectedPort;
        QString detectedBaud;
        for (const QString& portName : portNames)
        {
            if (!self || self->shutdown_requested_.load())
            {
                break;
            }

            queueLog(self->textFor("[Auto Detect] Probing GGA on %1...", "[自动识别] 正在探测 GGA: %1 ...").arg(portName));
            const auto probeResult = VaporView::probeSerialPortForHeader(
                portName,
                baudTexts,
                VaporView::SerialHeaderProbeKind::Gga);
            if (!probeResult.matched)
            {
                continue;
            }

            detectedPort = portName;
            detectedBaud = probeResult.baudText;
            queueLog(self->textFor("[Auto Detect] Identified GGA output on %1 @ %2",
                                   "[自动识别] 已识别 GGA 输出串口: %1 @ %2")
                         .arg(detectedPort, detectedBaud));
            break;
        }

        if (!self)
        {
            return;
        }

        QMetaObject::invokeMethod(self.data(), [self, detectedPort, detectedBaud]() {
            if (!self)
            {
                return;
            }

            self->port_detection_in_progress_.store(false);
            if (!detectedPort.isEmpty())
            {
                self->applyDetectedOutputAndGgaPort(detectedPort, detectedBaud);
            }
            else
            {
                self->appendLog(self->textFor("Auto detect finished: no GGA output port found.",
                                              "自动识别完成：未找到 GGA 输出串口。"));
            }
            self->updateButtonStates();
        }, Qt::QueuedConnection);
    });
}

void RtkConfigDialog::applyDetectedOutputAndGgaPort(const QString& portName, const QString& baudText)
{
    if (portName.isEmpty())
    {
        return;
    }

    if (output_port_combo_)
    {
        output_port_combo_->setCurrentText(portName);
    }
    if (gga_port_combo_)
    {
        applySavedGgaSource(QString::fromLatin1(kEpsilonMainGgaSourceKey));
    }
    if (!baudText.isEmpty() && baudrate_combo_)
    {
        baudrate_combo_->setCurrentText(baudText);
    }

    const QString appliedBaud = !baudText.isEmpty() && baudrate_combo_
        ? baudText
        : (baudrate_combo_ ? baudrate_combo_->currentText() : QString());
    appendLog(textFor("Auto detect applied: output port set to %1 @ %2; GGA source remains EPSILON main port.",
                      "自动识别已应用：输出串口已设置为 %1 @ %2；GGA 来源保持 EPSILON 主串口。")
                  .arg(portName, appliedBaud));
}

void RtkConfigDialog::onFetchMountpointsClicked()
{
    if (isBackgroundTaskRunning())
    {
        return;
    }

    const QString server = server_edit_->text().trimmed();
    const QString port = port_edit_->text().trimmed();
    const QString username = username_edit_->text().trimmed();
    const QString password = password_edit_->text();

    if (server.isEmpty() || port.isEmpty())
    {
        QMessageBox::warning(this, textFor("Error", "错误"), textFor("Please enter server address and port first.", "请先填写服务器地址和端口。"));
        return;
    }

    appendLog(textFor("Fetching mountpoint list from %1:%2...", "正在从 %1:%2 获取挂载点列表...").arg(server, port));
    if (fetch_mountpoints_thread_.joinable())
    {
        fetch_mountpoints_thread_.join();
    }

    fetch_mountpoints_in_progress_.store(true);
    updateButtonStates();

    QPointer<RtkConfigDialog> self(this);
    fetch_mountpoints_thread_ = std::thread([self, server, port, username, password]() {
        MountpointFetchResult result;
        result.response = performRtkHttpGet(
            nullptr,
            buildRtkUrl(server, port),
            username,
            password,
            QStringLiteral("text/plain, */*"));

        if (!result.response.timedOut &&
            (result.response.error.isEmpty() || !result.response.body.trimmed().isEmpty()))
        {
            result.mountpoints = parseMountpoints(result.response.body);
        }

        if (!self)
        {
            return;
        }

        QObject *receiver = self.data();
        QMetaObject::invokeMethod(receiver, [self, result = std::move(result)]() mutable {
            if (!self)
            {
                return;
            }

            self->fetch_mountpoints_in_progress_.store(false);
            self->updateButtonStates();

            const HttpResponse &response = result.response;
            if (response.timedOut || (!response.error.isEmpty() && response.body.trimmed().isEmpty()))
            {
                const QString errorText = response.timedOut
                    ? self->textFor("Request timed out", "请求超时")
                    : response.error;
                self->appendLog(self->textFor("Failed to fetch mountpoint list: %1", "获取挂载点列表失败: %1").arg(errorText));
                QMessageBox::warning(
                    self,
                    self->textFor("Failed", "失败"),
                    self->textFor("Failed to fetch mountpoint list: %1", "获取挂载点列表失败: %1").arg(errorText));
                return;
            }

            if (result.mountpoints.isEmpty())
            {
                self->appendLog(self->textFor("No mountpoints found in sourcetable response.", "返回的源表中未找到挂载点。"));
                QMessageBox::information(
                    self,
                    self->textFor("No Data", "无数据"),
                    self->textFor("No mountpoints were found for this server.", "该服务器未返回可用挂载点。"));
                return;
            }

            const QString currentMountpoint = self->mountpoint_edit_->text().trimmed();
            const qsizetype currentIndex = std::max<qsizetype>(0, result.mountpoints.indexOf(currentMountpoint));
            QDialog mountpointDialog(self);
            mountpointDialog.setWindowTitle(self->textFor("Select Mountpoint", "选择挂载点"));
            mountpointDialog.setModal(true);
            mountpointDialog.setMinimumWidth(self->scalePixels(520));

            auto *dialogLayout = new QVBoxLayout(&mountpointDialog);
            dialogLayout->setContentsMargins(self->scalePixels(16), self->scalePixels(14), self->scalePixels(16), self->scalePixels(14));
            dialogLayout->setSpacing(self->scalePixels(10));

            auto *dialogLabel = new QLabel(self->textFor("Available mountpoints:", "可用挂载点:"), &mountpointDialog);
            dialogLayout->addWidget(dialogLabel);

            auto *mountpointCombo = new QComboBox(&mountpointDialog);
            mountpointCombo->addItems(result.mountpoints);
            mountpointCombo->setEditable(false);
            mountpointCombo->setMinimumWidth(self->scalePixels(480));
            if (currentIndex >= 0 && currentIndex < result.mountpoints.size())
            {
                mountpointCombo->setCurrentIndex(static_cast<int>(currentIndex));
            }
            dialogLayout->addWidget(mountpointCombo);

            auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &mountpointDialog);
            dialogLayout->addWidget(buttonBox);
            QObject::connect(buttonBox, &QDialogButtonBox::accepted, &mountpointDialog, [&mountpointDialog]() {
                mountpointDialog.accept();
            });
            QObject::connect(buttonBox, &QDialogButtonBox::rejected, &mountpointDialog, [&mountpointDialog]() {
                mountpointDialog.reject();
            });

            const QString selected = mountpointDialog.exec() == QDialog::Accepted
                ? mountpointCombo->currentText().trimmed()
                : QString();

            self->appendLog(self->textFor("Fetched %1 mountpoints.", "已获取 %1 个挂载点。").arg(result.mountpoints.size()));

            if (!selected.isEmpty())
            {
                self->mountpoint_edit_->setText(selected);
                self->appendLog(self->textFor("Selected mountpoint: %1", "已选择挂载点: %1").arg(selected));
            }
        }, Qt::QueuedConnection);
    });
}

void RtkConfigDialog::onStartClicked()
{
    RtkStreamConfig config;
    QString description;
    if (!buildRtkStreamConfig(&config, &description))
    {
        QMessageBox::warning(this, textFor("Error", "错误"), textFor("Please fill in server, mountpoint and output port.", "请填写服务器、挂载点和输出串口。"));
        return;
    }

    appendLog(textFor("Starting RTK service...", "正在启动 RTK 服务..."));
    appendLog(description);
    if (config.sendNmeaGga)
    {
        appendLog(textFor("NTRIP GGA will be generated from the EPSILON main-port position; RTCM is written only to the configured output port.",
                          "将使用 EPSILON 主串口定位生成 NTRIP GGA；RTCM 只写入配置的输出串口。"));
    }
    else
    {
        appendLog(textFor("No valid EPSILON main-port position is available, so RTK service keeps output-port GGA relay fallback.",
                          "当前没有可用的 EPSILON 主串口定位，RTK 服务保留输出口回读 GGA 的兼容模式。"));
    }

    QString errorMessage;
    if (rtk_service_ && rtk_service_->start(config, &errorMessage))
    {
        is_running_ = true;
        last_rtk_status_message_.clear();
        updateButtonStates();
        if (rtk_status_timer_ && !rtk_status_timer_->isActive())
        {
            rtk_status_timer_->start();
        }
        pollRtkServiceStatus(true);
        appendLog(textFor("RTK service started successfully", "RTK 服务启动成功"));
    }
    else
    {
        appendLog(textFor("Failed to start RTK service: %1", "RTK 服务启动失败: %1")
            .arg(errorMessage.isEmpty() ? textFor("Unknown error", "未知错误") : errorMessage));
    }
}

void RtkConfigDialog::onStopClicked()
{
    if (rtk_service_ && rtk_service_->isRunning())
    {
        appendLog(textFor("Stopping RTK service...", "正在停止 RTK 服务..."));
        rtk_service_->stop();
        if (rtk_status_timer_ && rtk_status_timer_->isActive())
        {
            rtk_status_timer_->stop();
        }
        is_running_ = false;
        last_rtk_status_message_.clear();
        updateButtonStates();
        appendLog(textFor("RTK service stopped", "RTK 服务已停止"));
    }
}

void RtkConfigDialog::onTestClicked()
{
    if (isBackgroundTaskRunning())
    {
        return;
    }

    if (is_running_)
    {
        QMessageBox::information(this, textFor("Busy", "请先停止"),
            textFor("Stop the running RTK service before starting a no-signal test.", "请先停止当前 RTK 服务，再启动无信号测试。"));
        return;
    }

    RtkStreamConfig config;
    QString description;
    if (!buildRtkStreamConfig(&config, &description))
    {
        QMessageBox::warning(this, textFor("Error", "错误"), textFor("Please fill in server, mountpoint and output port.", "请填写服务器、挂载点和输出串口。"));
        return;
    }

    appendLog(textFor("Starting no-signal RTK test...", "正在启动无信号 RTK 测试..."));
    appendLog(description);

    if (test_thread_.joinable())
    {
        test_thread_.join();
    }

    test_in_progress_.store(true);
    updateButtonStates();

    QPointer<RtkConfigDialog> self(this);
    test_thread_ = std::thread([self, config]() mutable {
        auto queueLog = [self](const QString &message) {
            if (!self)
            {
                return;
            }
            QObject *receiver = self.data();
            QMetaObject::invokeMethod(receiver, [self, message]() {
                if (!self)
                {
                    return;
                }
                self->appendLog(message);
            }, Qt::QueuedConnection);
        };

        auto queueRawLog = [self](const QString &message) {
            if (!self)
            {
                return;
            }
            QObject *receiver = self.data();
            QMetaObject::invokeMethod(receiver, [self, message]() {
                if (!self)
                {
                    return;
                }
                self->appendRawLogLine(message);
            }, Qt::QueuedConnection);
        };

        NoSignalTestResult result;
        QTcpServer mockSerialServer;
        if (!mockSerialServer.listen(QHostAddress::LocalHost))
        {
            result.startError = mockSerialServer.errorString();
        }
        else
        {
            const bool useGeneratedGga = config.sendNmeaGga;
            config.outputMode = RtkStreamConfig::OutputMode::TcpClient;
            config.outputPathOverride = QStringLiteral("127.0.0.1:%1").arg(mockSerialServer.serverPort());
            config.relayBack = useGeneratedGga ? 0 : 1;
            queueLog(self->textFor("Using loopback mock serial on 127.0.0.1:%1", "正在使用 127.0.0.1:%1 的 loopback 模拟串口")
                .arg(mockSerialServer.serverPort()));
            if (useGeneratedGga)
            {
                queueLog(self->textFor("The test will send EPSILON-position GGA directly to NTRIP; loopback only receives RTCM.",
                                       "本次测试将把 EPSILON 定位 GGA 直接发给 NTRIP；loopback 只接收 RTCM。"));
            }

            std::unique_ptr<RtkStreamService> testService = std::make_unique<RtkStreamService>();
            QString errorMessage;
            if (!testService->start(config, &errorMessage))
            {
                result.startError = errorMessage.isEmpty() ? self->textFor("Unknown error", "未知错误") : errorMessage;
            }
            else
            {
                QElapsedTimer timer;
                timer.start();
                RtkStreamStats finalStats;
                qint64 lastInjectMs = -1000;
                qint64 lastStatusLogMs = -1000;
                int rtcmResponseBursts = 0;
                bool loggedMockGgaTemplate = false;
                std::unique_ptr<QTcpSocket> mockSerialPeer;
                result.generatedGga = useGeneratedGga;

                while (!self->shutdown_requested_.load() && timer.elapsed() < 15000)
                {
                    finalStats = testService->stats();

                    if (!mockSerialPeer &&
                        (mockSerialServer.hasPendingConnections() || mockSerialServer.waitForNewConnection(100)))
                    {
                        mockSerialPeer.reset(mockSerialServer.nextPendingConnection());
                        if (mockSerialPeer)
                        {
                            queueLog(self->textFor("Mock serial loopback connected.", "模拟串口 loopback 已连接。"));
                        }
                    }

                    if (timer.elapsed() - lastStatusLogMs >= 1000)
                    {
                        queueRawLog(formatRtkStatusLine(
                            finalStats,
                            self->textFor("Running no-signal RTK test", "正在执行无信号 RTK 测试"),
                            self->is_english_));
                        lastStatusLogMs = timer.elapsed();
                    }

                    const QString messageLower = finalStats.message.toLower();
                    const bool stillConnecting =
                        messageLower.contains(QStringLiteral("connecting")) ||
                        messageLower.contains(QStringLiteral("disconnected"));
                    if (!result.linkReady && mockSerialPeer &&
                        mockSerialPeer->state() == QAbstractSocket::ConnectedState && !stillConnecting)
                    {
                        result.linkReady = true;
                        if (!useGeneratedGga)
                        {
                            const QString mockGga = buildMockGgaSentence();
                            queueLog(self->textFor("Injecting GGA at 1 Hz: %1", "已按 1Hz 频率注入 GGA 数据: %1").arg(mockGga));
                            loggedMockGgaTemplate = true;
                        }
                    }

                    if (mockSerialPeer)
                    {
                        if (mockSerialPeer->waitForReadyRead(20) || mockSerialPeer->bytesAvailable() > 0)
                        {
                            QByteArray rtcmData = mockSerialPeer->readAll();
                            while (mockSerialPeer->bytesAvailable() > 0)
                            {
                                rtcmData += mockSerialPeer->readAll();
                            }
                            if (!rtcmData.isEmpty())
                            {
                                result.receivedRtcmBytes += rtcmData.size();
                                ++rtcmResponseBursts;
                            }
                        }
                    }

                    if (!useGeneratedGga && result.linkReady && mockSerialPeer && timer.elapsed() - lastInjectMs >= 1000)
                    {
                        const QString mockGga = buildMockGgaSentence();
                        if (!loggedMockGgaTemplate)
                        {
                            queueLog(self->textFor("Injecting GGA at 1 Hz: %1", "已按 1Hz 频率注入 GGA 数据: %1").arg(mockGga));
                            loggedMockGgaTemplate = true;
                        }
                        QByteArray payload = mockGga.toLatin1();
                        payload += "\r\n";
                        const qint64 written = mockSerialPeer->write(payload);
                        if (written != payload.size() || !mockSerialPeer->waitForBytesWritten(500))
                        {
                            result.runtimeError = mockSerialPeer->errorString().isEmpty()
                                ? self->textFor("Unknown error", "未知错误")
                                : mockSerialPeer->errorString();
                            break;
                        }
                        lastInjectMs = timer.elapsed();
                    }

                    if (rtcmResponseBursts >= 8)
                    {
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
                if (mockSerialPeer)
                {
                    mockSerialPeer->disconnectFromHost();
                }
            }
        }

        if (!self)
        {
            return;
        }

        QObject *receiver = self.data();
        QMetaObject::invokeMethod(receiver, [self, result = std::move(result)]() mutable {
            if (!self)
            {
                return;
            }

            self->test_in_progress_.store(false);
            self->updateButtonStates();

            if (result.cancelled)
            {
                return;
            }

            if (!result.startError.isEmpty())
            {
                self->appendLog(self->textFor("No-signal RTK test failed to start: %1", "无信号 RTK 测试启动失败: %1").arg(result.startError));
                QMessageBox::warning(
                    self,
                    self->textFor("Failed", "失败"),
                    self->textFor("Failed to start no-signal RTK test: %1", "无信号 RTK 测试启动失败: %1").arg(result.startError));
                return;
            }

            if (result.gotResponse)
            {
                self->appendLog(self->textFor("No-signal RTK test succeeded: input %1 B, output %2 B, loopback %3 B",
                                              "无信号 RTK 测试成功: 输入 %1 B, 输出 %2 B, loopback %3 B")
                    .arg(result.inputBytes)
                    .arg(result.outputBytes)
                    .arg(result.receivedRtcmBytes));
                QMessageBox::information(
                    self,
                    self->textFor("Success", "成功"),
                    self->textFor("Mock GGA test succeeded. RTCM data was received multiple times.", "模拟 GGA 测试成功，已多次收到 RTCM 返回数据。"));
                return;
            }

            const QString detail = describeNoSignalTestFailure(result, self->is_english_);
            self->appendLog(self->textFor("No-signal RTK test finished without RTCM response: %1", "无信号 RTK 测试结束，未收到 RTCM 返回: %1").arg(detail));
            QMessageBox::warning(
                self,
                self->textFor("No Response", "无返回"),
                self->textFor("Mock GGA test did not receive RTCM data.\n%1", "模拟 GGA 测试未收到 RTCM 返回数据。\n%1").arg(detail));
        }, Qt::QueuedConnection);
    });
}

void RtkConfigDialog::onSaveConfigClicked()
{
    QString filename = QFileDialog::getSaveFileName(
        this, textFor("Save RTK Configuration", "保存 RTK 配置"),
        QDir::homePath() + "/rtk_config.ini",
        textFor("INI Files (*.ini);;All Files (*)", "INI 文件 (*.ini);;所有文件 (*)")
    );

    if (filename.isEmpty()) return;

    QSettings settings(filename, QSettings::IniFormat);
    settings.setValue("server", server_edit_->text());
    settings.setValue("port", port_edit_->text());
    settings.setValue("username", username_edit_->text());
    settings.setValue("password", password_edit_->text());
    settings.setValue("mountpoint", mountpoint_edit_->text());
    settings.setValue("main_antenna_lever_x_m", main_antenna_lever_x_edit_->text());
    settings.setValue("main_antenna_lever_y_m", main_antenna_lever_y_edit_->text());
    settings.setValue("main_antenna_lever_z_m", main_antenna_lever_z_edit_->text());
    settings.setValue("output_port", output_port_combo_->currentText());
    settings.setValue("gga_source", savedGgaSourceValue());
    settings.setValue("gga_port", isMainGgaSourceSelected() ? QString() : ggaPortName());
    settings.setValue("baudrate", baudrate_combo_->currentText());
    settings.setValue("timeout", timeout_combo_->currentText());
    settings.setValue("reconnect", reconnect_combo_->currentText());
    appendLog(textFor("Configuration saved to: %1", "配置已保存到: %1").arg(filename));
    QMessageBox::information(this, textFor("Saved", "已保存"), textFor("Configuration saved successfully!", "配置保存成功！"));
}

void RtkConfigDialog::onLoadConfigClicked()
{
    QString filename = QFileDialog::getOpenFileName(
        this, textFor("Load RTK Configuration", "加载 RTK 配置"),
        QDir::homePath(),
        textFor("INI Files (*.ini);;All Files (*)", "INI 文件 (*.ini);;所有文件 (*)")
    );

    if (filename.isEmpty()) return;

    QSettings settings(filename, QSettings::IniFormat);

    server_edit_->setText(settings.value("server", "").toString());
    port_edit_->setText(settings.value("port", "2101").toString());
    username_edit_->setText(settings.value("username", "").toString());
    password_edit_->setText(settings.value("password", "").toString());
    mountpoint_edit_->setText(settings.value("mountpoint", "").toString());
    main_antenna_lever_x_edit_->setText(settings.value("main_antenna_lever_x_m", "").toString());
    main_antenna_lever_y_edit_->setText(settings.value("main_antenna_lever_y_m", "").toString());
    main_antenna_lever_z_edit_->setText(settings.value("main_antenna_lever_z_m", "").toString());
    output_port_combo_->setCurrentText(settings.value("output_port", "").toString());
    applySavedGgaSource(settings.value("gga_source", settings.value("gga_port", QString::fromLatin1(kEpsilonMainGgaSourceKey))).toString());
    baudrate_combo_->setCurrentText(settings.value("baudrate", "115200").toString());
    timeout_combo_->setCurrentText(settings.value("timeout", "5000").toString());
    reconnect_combo_->setCurrentText(settings.value("reconnect", "1000").toString());
    appendLog(textFor("Configuration loaded from: %1", "配置已从以下位置加载: %1").arg(filename));
}

void RtkConfigDialog::onClearLogClicked()
{
    log_text_edit_->clear();
}

void RtkConfigDialog::appendLog(const QString& message)
{
    if (!log_text_edit_) return;

    QString timestamp = QDateTime::currentDateTime().toString("hh:mm:ss");
    log_text_edit_->append(QString("[%1] %2").arg(timestamp, message));

    QTextCursor cursor = log_text_edit_->textCursor();
    cursor.movePosition(QTextCursor::End);
    log_text_edit_->setTextCursor(cursor);
}

void RtkConfigDialog::appendRawLogLine(const QString& line)
{
    if (!log_text_edit_ || line.isEmpty()) return;

    log_text_edit_->append(line);

    QTextCursor cursor = log_text_edit_->textCursor();
    cursor.movePosition(QTextCursor::End);
    log_text_edit_->setTextCursor(cursor);
}

bool RtkConfigDialog::isRunning() const
{
    return is_running_;
}

