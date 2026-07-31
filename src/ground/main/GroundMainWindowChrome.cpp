#include "ground/main/GroundMainWindowImplementation.h"
#include "ground/devices/DeviceRatePolicy.h"

#include <QCoreApplication>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QVersionNumber>
#include <QXmlStreamReader>

namespace
{

QString vaporViewUpdateRepositoryUrl()
{
    const QString overrideUrl = qEnvironmentVariable("VAPORVIEW_IFW_REPOSITORY_URL").trimmed();
    if (!overrideUrl.isEmpty())
    {
        return overrideUrl;
    }
#ifdef Q_OS_WIN
    return QStringLiteral("https://winter-and-you-gone.github.io/VaporView/ifw/windows/x64/repository/");
#else
    return QStringLiteral("https://winter-and-you-gone.github.io/VaporView/ifw/linux/x64/repository/");
#endif
}

QString vaporViewApplicationVersion()
{
    const QString applicationVersion = QCoreApplication::applicationVersion().trimmed();
    return applicationVersion.isEmpty() ? QStringLiteral("1.0.11") : applicationVersion;
}

QString vaporViewUpdateRepositoryDisplayName(const QString& repositoryUrl, bool english)
{
    if (repositoryUrl.contains(QStringLiteral("github.io"), Qt::CaseInsensitive))
    {
        return english ? QStringLiteral("GitHub Pages IFW repository")
                       : QStringLiteral("GitHub Pages IFW 更新源");
    }
    if (!qEnvironmentVariable("VAPORVIEW_IFW_REPOSITORY_URL").trimmed().isEmpty())
    {
        return english ? QStringLiteral("Custom IFW repository")
                       : QStringLiteral("自定义 IFW 更新源");
    }
    return english ? QStringLiteral("Qt IFW repository")
                   : QStringLiteral("Qt IFW 更新源");
}

QString vaporViewCompactRepositoryUrl(const QString& repositoryUrl)
{
    constexpr int kMaxVisibleRepositoryUrlLength = 76;
    if (repositoryUrl.size() <= kMaxVisibleRepositoryUrlLength)
    {
        return repositoryUrl;
    }
    return repositoryUrl.left(46).trimmed() + QStringLiteral("…") +
           repositoryUrl.right(26).trimmed();
}

QStringList vaporViewUpdateCheckArguments(const QString& repositoryUrl)
{
    return QStringList{QStringLiteral("--set-temp-repository"),
                       repositoryUrl,
                       QStringLiteral("check-updates")};
}

QStringList vaporViewStartUpdaterArguments(const QString& repositoryUrl)
{
    return QStringList{QStringLiteral("--set-temp-repository"),
                       repositoryUrl,
                       QStringLiteral("--start-updater")};
}

enum class VaporViewUpdateCheckStatus
{
    UpdatesAvailable,
    UpToDate,
    Failed,
    Unknown
};

struct VaporViewUpdateCheckResult
{
    VaporViewUpdateCheckStatus status = VaporViewUpdateCheckStatus::Unknown;
    QString output;
    QString errorText;
    QString latestVersion;
    int exitCode = -1;
};

QUrl vaporViewUpdateManifestUrl(const QString& repositoryUrl)
{
    QUrl url(repositoryUrl.trimmed());
    if (!url.isValid() || url.scheme().isEmpty())
    {
        return QUrl();
    }

    QString path = url.path();
    if (!path.endsWith(QLatin1Char('/')))
    {
        path.append(QLatin1Char('/'));
    }
    path.append(QStringLiteral("Updates.xml"));
    url.setPath(path);
    url.setQuery(QString());
    url.setFragment(QString());
    return url;
}

QVersionNumber vaporViewParsedVersion(const QString& version)
{
    qsizetype suffixIndex = 0;
    return QVersionNumber::fromString(version.trimmed(), &suffixIndex);
}

bool vaporViewOutputContainsAny(const QString& lowerOutput, const QStringList& markers)
{
    for (const QString& marker : markers)
    {
        if (lowerOutput.contains(marker))
        {
            return true;
        }
    }
    return false;
}

QString vaporViewTrimmedUpdateOutput(const QString& output, int maxLines = 10, int maxCharacters = 1400)
{
    QString normalized = output;
    normalized.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    const QStringList rawLines = normalized.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    QStringList lines;
    lines.reserve(std::min(static_cast<int>(rawLines.size()), maxLines));
    for (const QString& rawLine : rawLines)
    {
        const QString line = rawLine.simplified();
        if (line.isEmpty())
        {
            continue;
        }
        lines.push_back(line);
        if (lines.size() >= maxLines)
        {
            break;
        }
    }

    QString trimmed = lines.join(QLatin1Char('\n'));
    if (trimmed.size() > maxCharacters)
    {
        trimmed = trimmed.left(maxCharacters).trimmed() + QStringLiteral("...");
    }
    return trimmed;
}

VaporViewUpdateCheckResult vaporViewClassifyUpdateCheckResult(const QString& output,
                                                              int exitCode,
                                                              QProcess::ExitStatus exitStatus,
                                                              const QString& errorText = QString())
{
    VaporViewUpdateCheckResult result;
    result.output = output.trimmed();
    result.errorText = errorText.trimmed();
    result.exitCode = exitCode;

    const QString lowerOutput = result.output.toLower();
    const bool mentionsNoUpdates = vaporViewOutputContainsAny(lowerOutput,
        QStringList{QStringLiteral("no updates"),
                    QStringLiteral("no update available"),
                    QStringLiteral("currently no updates"),
                    QStringLiteral("already up to date"),
                    QStringLiteral("nothing to update"),
                    QStringLiteral("no packages to update")});
    const bool mentionsAvailableUpdates = vaporViewOutputContainsAny(lowerOutput,
        QStringList{QStringLiteral("updates available"),
                    QStringLiteral("update available"),
                    QStringLiteral("available updates"),
                    QStringLiteral("new updates"),
                    QStringLiteral("will be updated"),
                    QStringLiteral("can be updated"),
                    QStringLiteral("package updates")});

    if (mentionsNoUpdates)
    {
        result.status = VaporViewUpdateCheckStatus::UpToDate;
    }
    else if (mentionsAvailableUpdates)
    {
        result.status = VaporViewUpdateCheckStatus::UpdatesAvailable;
    }
    else if (exitStatus != QProcess::NormalExit || exitCode != 0 || !result.errorText.isEmpty())
    {
        result.status = VaporViewUpdateCheckStatus::Failed;
    }
    else if (result.output.isEmpty())
    {
        result.status = VaporViewUpdateCheckStatus::UpToDate;
    }
    else
    {
        result.status = VaporViewUpdateCheckStatus::Unknown;
    }

    return result;
}

VaporViewUpdateCheckResult vaporViewClassifyRepositoryManifest(const QByteArray& manifestData,
                                                               const QString& currentVersion)
{
    VaporViewUpdateCheckResult result;
    result.exitCode = 0;

    QVersionNumber currentParsed = vaporViewParsedVersion(currentVersion);
    if (currentParsed.isNull())
    {
        currentParsed = QVersionNumber::fromString(QStringLiteral("0"));
    }

    QVersionNumber latestParsed;
    QString latestText;
    int versionCount = 0;

    QXmlStreamReader xml(manifestData);
    while (!xml.atEnd())
    {
        xml.readNext();
        if (!xml.isStartElement() || xml.name() != QLatin1String("Version"))
        {
            continue;
        }

        const QString versionText = xml.readElementText(QXmlStreamReader::SkipChildElements).trimmed();
        const QVersionNumber parsed = vaporViewParsedVersion(versionText);
        if (parsed.isNull())
        {
            continue;
        }

        ++versionCount;
        if (latestParsed.isNull() ||
            QVersionNumber::compare(parsed.normalized(), latestParsed.normalized()) > 0)
        {
            latestParsed = parsed;
            latestText = versionText;
        }
    }

    if (xml.hasError())
    {
        result.status = VaporViewUpdateCheckStatus::Failed;
        result.errorText = QStringLiteral("Updates.xml parse error: %1").arg(xml.errorString());
        result.output = QString::fromUtf8(manifestData).trimmed();
        return result;
    }

    if (versionCount == 0 || latestParsed.isNull())
    {
        result.status = VaporViewUpdateCheckStatus::Unknown;
        result.output = QStringLiteral("No package versions were found in Updates.xml.");
        return result;
    }

    result.latestVersion = latestText;
    result.output = QStringLiteral("Current version: %1\nNewest repository version: %2")
        .arg(currentVersion, latestText);
    result.status = QVersionNumber::compare(latestParsed.normalized(), currentParsed.normalized()) > 0
        ? VaporViewUpdateCheckStatus::UpdatesAvailable
        : VaporViewUpdateCheckStatus::UpToDate;
    return result;
}

VaporViewUpdateCheckResult vaporViewClassifyRepositoryReply(QNetworkReply *reply,
                                                            const QString& currentVersion)
{
    const QByteArray responseData = reply ? reply->readAll() : QByteArray();
    const QVariant status = reply
        ? reply->attribute(QNetworkRequest::HttpStatusCodeAttribute)
        : QVariant();
    if (!reply || reply->error() != QNetworkReply::NoError ||
        (status.isValid() && status.toInt() >= 400))
    {
        VaporViewUpdateCheckResult result;
        result.status = VaporViewUpdateCheckStatus::Failed;
        result.output = QString::fromUtf8(responseData).trimmed();
        if (reply && reply->error() != QNetworkReply::NoError)
        {
            result.errorText = reply->errorString();
        }
        else if (status.isValid())
        {
            result.errorText = QStringLiteral("HTTP %1").arg(status.toInt());
        }
        else
        {
            result.errorText = QStringLiteral("The update manifest request failed.");
        }
        return result;
    }

    return vaporViewClassifyRepositoryManifest(responseData, currentVersion);
}

} // namespace

void MainWindow::setEnglish(bool english)
{
    auto setNativeMenuTitle = [this](QMenu *menu, const QString& title) {
        if (!menu || state_->custom_title_bar_)
        {
            return;
        }
        menu->setTitle(title);
    };

    state_->is_english_ = english;
    if (qApp)
    {
        qApp->setProperty(kEnglishProperty, state_->is_english_);
    }

    setNativeMenuTitle(state_->data_menu_, english ? QStringLiteral("&Data") : QStringLiteral("数据(&D)"));
    state_->recording_directory_action_->setText(english ? "Recording Folder" : "记录目录");
    setNativeMenuTitle(state_->recording_rate_menu_, english ? QStringLiteral("Record Rates") : QStringLiteral("记录频率"));
    rebuildRecordingRateMenu();
    setNativeMenuTitle(state_->devices_menu_, english ? QStringLiteral("&Devices") : QStringLiteral("设备(&E)"));
    if (state_->epsilon_packet_rates_action_)
    {
        state_->epsilon_packet_rates_action_->setText(english ? "EPSILON Packet Rates" : "设置EPSILON包频率");
    }
    if (state_->epsilon_rtcm_port_action_)
    {
        state_->epsilon_rtcm_port_action_->setText(english ? "Configure EPSILON RTCM Port" : "配置EPSILON RTCM串口");
    }
    if (state_->epsilon_reconfigure_action_)
    {
        state_->epsilon_reconfigure_action_->setText(english ? "Reconfigure EPSILON Output" : "重新配置EPSILON输出");
    }
    state_->session_viewer_action_->setText(english ? "Data Viewer" : "数据查看器");
    setNativeMenuTitle(state_->view_menu_, english ? QStringLiteral("&View") : QStringLiteral("视图(&V)"));
    setNativeMenuTitle(state_->developer_menu_, english ? QStringLiteral("Develo&per") : QStringLiteral("开发者(&P)"));
    if (state_->ui_test_mode_action_)
    {
        state_->ui_test_mode_action_->setText(english ? QStringLiteral("UI Test Mode") : QStringLiteral("界面测试模式"));
    }
    if (state_->ui_test_normal_action_)
    {
        QMenu *scenarioMenu = findChild<QMenu *>(QStringLiteral("uiTestScenarioMenu"));
        if (scenarioMenu)
        {
            scenarioMenu->setTitle(english ? QStringLiteral("UI Test Scenario") : QStringLiteral("界面测试场景"));
        }
        state_->ui_test_normal_action_->setText(english ? QStringLiteral("Normal Operation") : QStringLiteral("正常运行"));
        state_->ui_test_partial_failure_action_->setText(english ? QStringLiteral("Partial Device Failure") : QStringLiteral("部分设备异常"));
        state_->ui_test_stalled_action_->setText(english ? QStringLiteral("Data Stalled") : QStringLiteral("数据停更"));
    }
#ifdef VAPORVIEW_HAS_OSGEARTH
    if (state_->map3d_action_)
    {
        state_->map3d_action_->setText(english ? "3D Map" : "三维地图");
        state_->map3d_action_->setToolTip(english ? "Open 3D map" : "打开三维地图");
    }
    if (state_->map3d_diagnostics_action_)
    {
        state_->map3d_diagnostics_action_->setText(english ? "Map Data Diagnostics" : "地图数据诊断");
        state_->map3d_diagnostics_action_->setToolTip(english ? "Open 3D map data diagnostics" : "打开三维地图数据诊断");
    }
#endif
    state_->exit_action_->setText(english ? "E&xit" : "退出(&X)");

    setNativeMenuTitle(state_->font_menu_, english ? QStringLiteral("Font &Size") : QStringLiteral("字号(&S)"));
    state_->font_tiny_action_->setText(english ? "Tiny (70%)" : "超小 (70%)");
    state_->font_extra_small_action_->setText(english ? "Extra Small (80%)" : "特小 (80%)");
    state_->font_small_action_->setText(english ? "Small (90%)" : "小号 (90%)");
    state_->font_normal_action_->setText(english ? "Normal (100%)" : "标准 (100%)");
    state_->font_large_action_->setText(english ? "Large (115%)" : "大号 (115%)");
    state_->font_extra_large_action_->setText(english ? "Extra Large (130%)" : "超大 (130%)");

    setNativeMenuTitle(state_->language_menu_, english ? QStringLiteral("&Language") : QStringLiteral("语言(&L)"));
    state_->lang_action_->setText(english ? "Switch to Chinese" : "切换到英文");
    state_->lang_action_->setToolTip(english ? "Switch to Chinese" : "切换到英文");
    updateThemeAction();
    updateCustomTitleBarTexts();
    discardTitleApplicationMenuPanel();

    setNativeMenuTitle(state_->help_menu_, english ? QStringLiteral("&Help") : QStringLiteral("帮助(&H)"));
    state_->check_updates_action_->setText(english ? "Check for Updates" : "检查更新");
    state_->check_updates_action_->setToolTip(english ? "Check for VaporView updates" : "检查 VaporView 更新");
    state_->about_action_->setText(english ? "&About" : "关于(&A)");

    state_->refresh_ports_btn_->setText(english ? "Refresh" : "刷新");
    state_->refresh_ports_btn_->setToolTip(english ? "Refresh ports" : "刷新串口");
    state_->connect_btn_->setText(english ? "Connect" : "连接");
    state_->connect_btn_->setToolTip(english ? "Connect" : "连接");
    state_->cancel_connect_btn_->setText(english ? "Cancel" : "取消");
    state_->cancel_connect_btn_->setToolTip(english ? "Cancel connection" : "取消连接");
    state_->disconnect_btn_->setText(english ? "Disconnect" : "断开");
    state_->disconnect_btn_->setToolTip(english ? "Disconnect" : "断开连接");
    updateScheduledRecordingAction();
    state_->start_recording_btn_->setText(english ? "Start Recording" : "开始记录");
    state_->start_recording_btn_->setToolTip(english ? "Start recording" : "开始记录");
    state_->pause_recording_btn_->setText(english ? "Pause Recording" : "暂停记录");
    state_->pause_recording_btn_->setToolTip(english ? "Pause recording" : "暂停记录");
    state_->stop_recording_btn_->setText(english ? "Stop Recording" : "结束记录");
    state_->stop_recording_btn_->setToolTip(english ? "Stop recording" : "结束记录");
    state_->clear_log_action_->setText(english ? "Clear Log" : "清空日志");
    state_->clear_log_action_->setToolTip(english ? "Clear Log" : "清空日志");
    updateLogFilterAction();
    state_->rtk_config_action_->setText(english ? "RTK Config" : "RTK配置");
    updateRtkConfigIcon();
    state_->session_viewer_action_->setToolTip(english ? "Data viewer" : "数据查看器");

    state_->config_group_->setTitle(QString());
    state_->data_group_->setTitle(QString());
    state_->tcp_wave_group_->setTitle(QString());

    if (state_->epsilon_group_) state_->epsilon_group_->setTitle(QString());
    if (state_->gnss_group_) state_->gnss_group_->setTitle(QString());
    if (state_->imu_group_) state_->imu_group_->setTitle(QString());
    if (state_->env_group_) state_->env_group_->setTitle(QString());

    if (state_->epsilon_lbl_) state_->epsilon_lbl_->setText(english ? "EPSILON:" : "EPSILON:");
    if (state_->gnss_lbl_) state_->gnss_lbl_->setText(english ? "GNSS:" : "GNSS:");
    if (state_->imu_lbl_) state_->imu_lbl_->setText(english ? "IMU:" : "IMU:");
    if (state_->ptb_lbl_) state_->ptb_lbl_->setText(english ? "PTB210:" : "PTB210:");
    if (state_->hmp_lbl_) state_->hmp_lbl_->setText(english ? "HMP3:" : "HMP3:");
    if (state_->lidar_lbl_) state_->lidar_lbl_->setText(english ? "TFA1500-L:" : "TFA1500-L:");
    if (state_->temperature_lbl_) state_->temperature_lbl_->setText(QStringLiteral("RD105:"));

    if (state_->config_inline_title_lbl_)
    {
        state_->config_inline_title_lbl_->setText(english ? "Device Overview" : "设备概览");
    }
    if (state_->data_source_mode_lbl_) state_->data_source_mode_lbl_->setText(english ? "Source:" : "数据源:");
    if (state_->source_mode_switch_) state_->source_mode_switch_->setEnglish(english);
    if (state_->sky_telemetry_transport_lbl_) state_->sky_telemetry_transport_lbl_->setText(english ? "Link:" : "链路:");
    updateSkyTelemetryTransportComboTexts(state_->sky_telemetry_transport_combo_, english);
    if (state_->sky_telemetry_tcp_host_lbl_) state_->sky_telemetry_tcp_host_lbl_->setText(english ? "Sky IP:" : "天空端IP:");
    if (state_->sky_telemetry_tcp_port_lbl_) state_->sky_telemetry_tcp_port_lbl_->setText(english ? "Port:" : "端口:");
    if (state_->sky_telemetry_port_lbl_) state_->sky_telemetry_port_lbl_->setText(english ? "Serial:" : "串口:");
    if (state_->sky_telemetry_baud_lbl_) state_->sky_telemetry_baud_lbl_->setText(english ? "Baud:" : "波特率:");
    if (state_->sky_device_config_btn_) state_->sky_device_config_btn_->setText(english ? "Sky Device Config" : "天空端设备配置");
    if (state_->data_source_mode_combo_)
    {
        const QSignalBlocker blocker(state_->data_source_mode_combo_);
        state_->data_source_mode_combo_->setItemText(0, sourceModeDisplayText(english, 0));
        state_->data_source_mode_combo_->setItemText(1, sourceModeDisplayText(english, 1));
    }
    if (state_->auto_detect_ports_btn_)
    {
        state_->auto_detect_ports_btn_->setText(state_->port_detection_in_progress_
            ? (english ? "Cancel Auto Detect" : "取消自动识别")
            : (english ? "Auto Detect Ports" : "自动识别串口"));
        state_->auto_detect_ports_btn_->setToolTip(state_->port_detection_in_progress_
            ? (english ? "Stop the current serial-port detection task." : "停止当前串口自动识别任务。")
            : (english ? "Probe available serial ports and automatically assign detected devices."
                       : "扫描可用串口，并将识别出的设备自动填入对应端口。"));
    }
    refreshLocalSerialPortManualOptionTexts();
    if (state_->log_inline_title_lbl_)
    {
        state_->log_inline_title_lbl_->setText(english ? "Log" : "日志");
    }
    updateAppSidebarButtonTexts();
    if (state_->recording_status_title_lbl_)
    {
        state_->recording_status_title_lbl_->setText(english ? "Recording Status" : "记录状态");
    }
    if (state_->log_side_panel_)
    {
        state_->log_side_panel_->setMinimumWidth(minimumLogSidePanelWidth());
    }
    if (state_->epsilon_inline_title_lbl_)
    {
        state_->epsilon_inline_title_lbl_->setText(english ? "EPSILON Integrated Navigation" : "EPSILON组合导航");
    }
    if (state_->gnss_inline_title_lbl_)
    {
        state_->gnss_inline_title_lbl_->setText(english ? "GNSS / RTK" : "GNSS / RTK");
    }
    if (state_->imu_inline_title_lbl_)
    {
        state_->imu_inline_title_lbl_->setText(english ? "IMU" : "IMU");
    }
    if (state_->env_inline_title_lbl_)
    {
        state_->env_inline_title_lbl_->setText(english ? "Environment / Range" : "环境与测距");
    }
    if (state_->temperature_overview_inline_title_lbl_)
    {
        state_->temperature_overview_inline_title_lbl_->setText(english ? "Laser Driver Temperature Overview" : "激光驱动温控概览");
    }
    if (state_->temperature_overview_panel_)
    {
        state_->temperature_overview_panel_->setEnglish(english);
    }
    if (state_->temperature_controller_inline_title_lbl_)
    {
        updateTemperatureControllerTitleText();
    }
    if (state_->ai8_temperature_controller_inline_title_lbl_)
    {
        state_->ai8_temperature_controller_inline_title_lbl_->setText(
            english
                ? QStringLiteral("AI-8 Series Multi-loop Temperature Controller ·")
                : QStringLiteral("AI-8 系列多回路智能温控器 ·"));
    }
    if (state_->ai8_temperature_controller_panel_)
    {
        state_->ai8_temperature_controller_panel_->setEnglish(english);
    }
    updateTemperatureTitleButtonsState();
    if (state_->global_rate_lbl_) state_->global_rate_lbl_->setText(english ? "Global Rate:" : "统一频率:");
    if (state_->epsilon_rate_lbl_) state_->epsilon_rate_lbl_->setText(english ? "Packets:" : "包频率:");
    if (state_->epsilon_packet_rates_btn_)
    {
        state_->epsilon_packet_rates_btn_->setText(english ? "Packet Rates" : "配置EPSILON包频率");
        state_->epsilon_packet_rates_btn_->setToolTip(english
            ? "Configure EPSILON packet output rates"
            : "配置 EPSILON 各数据包输出频率");
    }
    if (state_->gnss_rate_lbl_) state_->gnss_rate_lbl_->setText(english ? "Rate:" : "频率:");
    if (state_->imu_rate_lbl_) state_->imu_rate_lbl_->setText(english ? "Rate:" : "频率:");
    if (state_->imu_apply_btn_)
    {
        state_->imu_apply_btn_->setText(english ? "Apply IMU" : "应用IMU");
        state_->imu_apply_btn_->setToolTip(english ? "Apply the selected IMU format, baud rate, and output frequency" : "应用当前选择的 IMU 输出格式、波特率和输出频率");
    }
    if (state_->imu_hi91_btn_)
    {
        state_->imu_hi91_btn_->setToolTip(english ? "Switch IMU output to HI91 immediately" : "立即切换 IMU 输出为 HI91");
    }
    if (state_->imu_hi92_btn_)
    {
        state_->imu_hi92_btn_->setToolTip(english ? "Switch IMU output to HI92 immediately" : "立即切换 IMU 输出为 HI92");
    }
    if (state_->imu_baud_115200_btn_)
    {
        state_->imu_baud_115200_btn_->setToolTip(english ? "Switch IMU baud rate to 115200" : "一键切换 IMU 波特率到 115200");
    }
    if (state_->imu_baud_921600_btn_)
    {
        state_->imu_baud_921600_btn_->setToolTip(english ? "Switch IMU baud rate to 921600" : "一键切换 IMU 波特率到 921600");
    }
    if (state_->imu_rate_100_btn_)
    {
        state_->imu_rate_100_btn_->setToolTip(english ? "Switch IMU output frequency to 100 Hz" : "一键切换 IMU 输出频率到 100 Hz");
    }
    if (state_->imu_rate_200_btn_)
    {
        state_->imu_rate_200_btn_->setToolTip(english ? "Switch IMU output frequency to 200 Hz" : "一键切换 IMU 输出频率到 200 Hz");
    }
    if (state_->imu_rate_500_btn_)
    {
        state_->imu_rate_500_btn_->setToolTip(english ? "Switch IMU output frequency to 500 Hz" : "一键切换 IMU 输出频率到 500 Hz");
    }
    if (state_->imu_rate_1000_btn_)
    {
        state_->imu_rate_1000_btn_->setToolTip(english ? "Switch IMU output frequency to 1000 Hz" : "一键切换 IMU 输出频率到 1000 Hz");
    }
    state_->ptb_rate_lbl_->setText(english ? "Rate:" : "频率:");
    state_->hmp_rate_lbl_->setText(english ? "Rate:" : "频率:");
    state_->lidar_rate_lbl_->setText(english ? "Rate:" : "频率:");
    if (state_->temperature_rate_lbl_) state_->temperature_rate_lbl_->setText(english ? "Poll:" : "轮询:");
    for (QComboBox *combo : {state_->ptb_rate_combo_, state_->hmp_rate_combo_, state_->lidar_rate_combo_, state_->temperature_rate_combo_})
    {
        if (!combo)
        {
            continue;
        }
        const QSignalBlocker blocker(combo);
        const QString oldText = english ? QStringLiteral("不设定") : QStringLiteral("No Set");
        const QString newText = english ? QStringLiteral("No Set") : QStringLiteral("不设定");
        const int idx = combo->findText(oldText);
        if (idx >= 0)
        {
            combo->setItemText(idx, newText);
        }
        else if (combo->findText(newText) < 0)
        {
            combo->addItem(newText);
        }
        if (isRateUnspecified(combo->currentText()))
        {
            combo->setCurrentText(newText);
        }
    }

    if (state_->epsilon_panel_) state_->epsilon_panel_->setEnglish(english);
    if (state_->gnss_panel_) state_->gnss_panel_->setEnglish(english);
    if (state_->imu_panel_) state_->imu_panel_->setEnglish(english);
    if (state_->ptb_panel_) state_->ptb_panel_->setEnglish(english);
    if (state_->hmp_panel_) state_->hmp_panel_->setEnglish(english);
    if (state_->lidar_panel_) state_->lidar_panel_->setEnglish(english);
    if (state_->tcp_wave_panel_) state_->tcp_wave_panel_->setEnglish(english);
    if (state_->sky_device_config_dialog_) state_->sky_device_config_dialog_->setEnglish(english);

    const CollectorSnapshot collectors = snapshotCollectors();
    if (collectors.epsilon) collectors.epsilon->setEnglish(english);
    if (collectors.gnss) collectors.gnss->setEnglish(english);
    if (collectors.imu) collectors.imu->setEnglish(english);
    if (collectors.ptb) collectors.ptb->setEnglish(english);
    if (collectors.hmp) collectors.hmp->setEnglish(english);
    if (collectors.lidar) collectors.lidar->setEnglish(english);
    if (collectors.temperature_controller) collectors.temperature_controller->setEnglish(english);

    if (state_->rtk_config_dialog_)
    {
        state_->rtk_config_dialog_->setEnglish(english);
    }
    if (state_->session_viewer_window_)
    {
        state_->session_viewer_window_->setEnglish(english);
    }

    if (isRemoteSkyMode())
    {
        refreshRemoteSkyDataUi();
    }
    else
    {
        updateEnvironmentStatusIcons(state_->current_lidar_.valid, state_->current_ptb_.valid, state_->current_hmp_.valid);
    }
    updateSourceModeUi();
    updateDeviceConfigTexts();
    updateSidebarNavIcons();
    updateRecordingStatusLabel();
}

void MainWindow::onOpenSessionViewerClicked()
{
    if (!state_->session_viewer_window_)
    {
        state_->session_viewer_window_ = new SessionViewerWindow();
        state_->session_viewer_window_->setAttribute(Qt::WA_QuitOnClose, false);
        connect(state_->session_viewer_window_, &QObject::destroyed, this, [this]() {
            state_->session_viewer_window_ = nullptr;
        });
        state_->session_viewer_window_->setEnglish(state_->is_english_);
        state_->session_viewer_window_->setUiTestMode(isUiTestMode());
        state_->session_viewer_window_->setRecordingDirectoryProvider([this]() {
            return state_->recording_directory_.isEmpty()
                ? defaultRecordingDirectory()
                : state_->recording_directory_;
        });
    }

    const bool wasMinimized =
        state_->session_viewer_window_->isMinimized() ||
        state_->session_viewer_window_->windowState().testFlag(Qt::WindowMinimized);
    const bool restoreMaximized =
        state_->session_viewer_window_->isMaximized() ||
        state_->session_viewer_window_->windowState().testFlag(Qt::WindowMaximized);
    if (!wasMinimized)
    {
        VaporView::centerWindowOnScreen(state_->session_viewer_window_, this);
    }
    if (wasMinimized)
    {
        state_->session_viewer_window_->setWindowState(
            state_->session_viewer_window_->windowState() & ~Qt::WindowMinimized);
        if (restoreMaximized)
        {
            state_->session_viewer_window_->showMaximized();
        }
        else
        {
            state_->session_viewer_window_->showNormal();
        }
    }
    else
    {
        state_->session_viewer_window_->show();
    }
    state_->session_viewer_window_->raise();
    state_->session_viewer_window_->activateWindow();
}

#ifdef VAPORVIEW_HAS_OSGEARTH
void MainWindow::onOpenMap3DWindowClicked()
{
    if (state_->map3d_controller_)
    {
        state_->map3d_controller_->open();
    }
}

void MainWindow::onOpenMap3DDiagnosticsClicked()
{
    if (state_->map3d_controller_)
    {
        state_->map3d_controller_->showDiagnostics();
    }
}
#else
void MainWindow::onOpenMap3DWindowClicked()
{
    QMessageBox::information(this,
                             QStringLiteral("VaporView 3D Map"),
                             state_->is_english_
                                 ? QStringLiteral("3D map module is not enabled. Rebuild with -DVAPORVIEW_ENABLE_OSGEARTH=ON.")
                                 : QStringLiteral("三维地图模块未启用。请使用 -DVAPORVIEW_ENABLE_OSGEARTH=ON 重新构建。"));
}

void MainWindow::onOpenMap3DDiagnosticsClicked()
{
    onOpenMap3DWindowClicked();
}
#endif

void MainWindow::onCheckUpdatesClicked()
{
    const bool english = state_->is_english_;
    const bool dark = state_->dark_theme_enabled_;
    const QString applicationDir = QCoreApplication::applicationDirPath();
    const QString toolName =
#ifdef Q_OS_WIN
        QStringLiteral("VaporViewMaintenanceTool.exe");
#else
        QStringLiteral("VaporViewMaintenanceTool");
#endif
    const QString maintenanceToolPath = QDir(applicationDir).absoluteFilePath(toolName);
    const bool maintenanceToolAvailable = QFileInfo::exists(maintenanceToolPath);

    const QString repositoryUrl = vaporViewUpdateRepositoryUrl();
    const QString applicationVersion = vaporViewApplicationVersion();
    const QString repositoryDisplayName = vaporViewUpdateRepositoryDisplayName(repositoryUrl, english);
    const QString compactRepositoryUrl = vaporViewCompactRepositoryUrl(repositoryUrl);
    const auto startUpdater = [this, english, maintenanceToolPath, repositoryUrl, applicationDir]() {
        if (!QProcess::startDetached(maintenanceToolPath,
                                     vaporViewStartUpdaterArguments(repositoryUrl),
                                     applicationDir))
        {
            QMessageBox::warning(this,
                                 english ? QStringLiteral("Updates")
                                         : QStringLiteral("软件更新"),
                                 english
                                     ? QStringLiteral("Failed to start VaporViewMaintenanceTool.")
                                     : QStringLiteral("无法启动 VaporViewMaintenanceTool。"));
            return false;
        }
        return true;
    };

    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("updateCheckDialog"));
    dialog.setWindowTitle(english ? QStringLiteral("Updates") : QStringLiteral("软件更新"));
    dialog.setWindowModality(Qt::WindowModal);
    dialog.setMinimumSize(480, 280);

    auto *rootLayout = new QVBoxLayout(&dialog);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    auto *body = new QWidget(&dialog);
    body->setObjectName(QStringLiteral("updateCheckDialogBody"));
    auto *bodyLayout = new QVBoxLayout(body);
    bodyLayout->setContentsMargins(26, 18, 26, 16);
    bodyLayout->setSpacing(8);

    auto *titleLabel = new QLabel(english ? QStringLiteral("Check for VaporView Updates")
                                          : QStringLiteral("检查 VaporView 更新"),
                                  body);
    titleLabel->setObjectName(QStringLiteral("updateCheckTitleLabel"));
    QFont titleFont = titleLabel->font();
    titleFont.setBold(true);
    titleFont.setPointSize(std::max(titleFont.pointSize() + 1, 11));
    titleLabel->setFont(titleFont);
    titleLabel->setWordWrap(true);
    bodyLayout->addWidget(titleLabel);

    auto *statusLabel = new QLabel(english ? QStringLiteral("Checking the update repository...")
                                           : QStringLiteral("正在检查更新仓库..."),
                                   body);
    statusLabel->setObjectName(QStringLiteral("updateCheckStatusLabel"));
    statusLabel->setWordWrap(true);
    bodyLayout->addWidget(statusLabel);

    auto *detailLabel = new QLabel(english
                                       ? (maintenanceToolAvailable
                                              ? QStringLiteral("Checking the configured source for program updates.")
                                              : QStringLiteral("Development build: checking the update manifest only. Applying updates requires the setup package installation."))
                                       : (maintenanceToolAvailable
                                              ? QStringLiteral("正在通过配置的更新源检查程序更新。")
                                              : QStringLiteral("当前是开发构建：仅检查更新清单。应用更新需要先使用安装包安装。")),
                                   body);
    detailLabel->setObjectName(QStringLiteral("updateCheckDetailLabel"));
    detailLabel->setWordWrap(true);
    bodyLayout->addWidget(detailLabel);

    auto *sourcePanel = new QWidget(body);
    sourcePanel->setObjectName(QStringLiteral("updateCheckSourcePanel"));
    auto *sourceLayout = new QGridLayout(sourcePanel);
    sourceLayout->setContentsMargins(12, 8, 12, 8);
    sourceLayout->setHorizontalSpacing(10);
    sourceLayout->setVerticalSpacing(4);

    auto addSourceRow = [&](int row,
                            const QString& labelText,
                            const QString& valueText,
                            const QString& tooltipText = QString()) {
        auto *label = new QLabel(labelText, sourcePanel);
        label->setObjectName(QStringLiteral("updateCheckSourceKeyLabel"));
        auto *value = new QLabel(valueText, sourcePanel);
        value->setObjectName(QStringLiteral("updateCheckSourceValueLabel"));
        value->setWordWrap(true);
        value->setTextInteractionFlags(Qt::TextSelectableByMouse);
        if (!tooltipText.isEmpty())
        {
            value->setToolTip(tooltipText);
        }
        sourceLayout->addWidget(label, row, 0, Qt::AlignTop);
        sourceLayout->addWidget(value, row, 1);
        return value;
    };
    addSourceRow(0,
                 english ? QStringLiteral("Version") : QStringLiteral("当前版本"),
                 applicationVersion);
    addSourceRow(1,
                 english ? QStringLiteral("Source") : QStringLiteral("更新来源"),
                 repositoryDisplayName);
    addSourceRow(2,
                 english ? QStringLiteral("Repository") : QStringLiteral("仓库地址"),
                 compactRepositoryUrl,
                 repositoryUrl);
    addSourceRow(3,
                 english ? QStringLiteral("Mode") : QStringLiteral("检查模式"),
                 maintenanceToolAvailable
                     ? (english ? QStringLiteral("Installed maintenance tool")
                                : QStringLiteral("安装目录维护工具"))
                     : (english ? QStringLiteral("Development build manifest check")
                                : QStringLiteral("开发构建清单检查")));
    sourceLayout->setColumnStretch(1, 1);
    bodyLayout->addWidget(sourcePanel);

    auto *outputTitleLabel = new QLabel(english ? QStringLiteral("Diagnostic output")
                                                : QStringLiteral("诊断输出"),
                                        body);
    outputTitleLabel->setObjectName(QStringLiteral("updateCheckOutputTitleLabel"));
    outputTitleLabel->setVisible(false);
    bodyLayout->addWidget(outputTitleLabel);

    auto *outputEdit = new QPlainTextEdit(body);
    outputEdit->setObjectName(QStringLiteral("updateCheckOutputEdit"));
    outputEdit->setReadOnly(true);
    outputEdit->setVisible(false);
    outputEdit->setMaximumHeight(92);
    outputEdit->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    bodyLayout->addWidget(outputEdit);

    rootLayout->addWidget(body, 1);

    auto *footer = new QWidget(&dialog);
    footer->setObjectName(QStringLiteral("updateCheckDialogFooter"));
    footer->setMinimumHeight(60);
    auto *footerLayout = new QHBoxLayout(footer);
    footerLayout->setContentsMargins(20, 10, 24, 10);
    footerLayout->setSpacing(10);
    footerLayout->addStretch(1);

    auto *updateButton = new QPushButton(english ? QStringLiteral("Update Now") : QStringLiteral("立即更新"), footer);
    updateButton->setObjectName(QStringLiteral("updateCheckUpdateButton"));
    updateButton->setFixedSize(112, 34);
    updateButton->setVisible(false);
    updateButton->setEnabled(false);
    footerLayout->addWidget(updateButton);

    auto *closeButton = new QPushButton(english ? QStringLiteral("Cancel") : QStringLiteral("取消"), footer);
    closeButton->setObjectName(QStringLiteral("updateCheckCloseButton"));
    closeButton->setFixedSize(100, 34);
    closeButton->setDefault(true);
    closeButton->setAutoDefault(true);
    footerLayout->addWidget(closeButton);
    rootLayout->addWidget(footer);

    VaporView::installCustomTitleBar(&dialog, false);
    if (QLabel *titleLogo = dialog.findChild<QLabel *>(QStringLiteral("customTitleLogo")))
    {
        titleLogo->setFixedSize(34, 34);
        titleLogo->setPixmap(renderVaporViewLogo(dark, 28, titleLogo->devicePixelRatioF()));
    }
    for (QToolButton *button : dialog.findChildren<QToolButton *>())
    {
        if (button->accessibleName() == QStringLiteral("titleLanguageButton") ||
            button->accessibleName() == QStringLiteral("titleThemeButton"))
        {
            button->hide();
        }
        else if (button->objectName() == QStringLiteral("windowCloseButton"))
        {
            button->setAccessibleName(english ? QStringLiteral("Close") : QStringLiteral("关闭"));
            button->setFocusPolicy(Qt::TabFocus);
        }
    }
    for (QFrame *separator : dialog.findChildren<QFrame *>(QStringLiteral("titleBarSeparator")))
    {
        separator->hide();
    }

    dialog.setStyleSheet(applyAppThemeTokens(
        customTitleBarStyleSheet(dark) + QStringLiteral(R"(
QDialog#updateCheckDialog,
QDialog#updateCheckDialog QWidget#customTitleBarContent,
QWidget#updateCheckDialogBody {
    background-color: @vv-surface;
    color: @vv-text;
}
QDialog#updateCheckDialog QLabel {
    background-color: transparent;
    border: none;
}
QLabel#updateCheckTitleLabel {
    color: @vv-text-strong;
}
QLabel#updateCheckStatusLabel {
    color: @vv-text;
    font-weight: 600;
}
QLabel#updateCheckDetailLabel {
    color: @vv-text-secondary;
}
QWidget#updateCheckSourcePanel {
    background-color: @vv-surface-alt;
    border: 1px solid @vv-border;
    border-radius: 8px;
}
QLabel#updateCheckSourceKeyLabel {
    color: @vv-text-muted;
    font-size: 12px;
    font-weight: 600;
}
QLabel#updateCheckSourceValueLabel {
    color: @vv-text-secondary;
    font-size: 12px;
}
QLabel#updateCheckOutputTitleLabel {
    color: @vv-text-muted;
    font-size: 13px;
    font-weight: 600;
}
QPlainTextEdit#updateCheckOutputEdit {
    background-color: @vv-surface-alt;
    color: @vv-text-secondary;
    border: 1px solid @vv-border;
    border-radius: 8px;
    padding: 8px;
    font-family: "Cascadia Mono", "Consolas", monospace;
    font-size: 12px;
    selection-background-color: @vv-primary-subtle-pressed;
    selection-color: @vv-white;
}
QWidget#updateCheckDialogFooter {
    background-color: @vv-surface-alt;
    border-top: 1px solid @vv-border;
}
QPushButton#updateCheckCloseButton {
    background-color: @vv-surface-raised;
    color: @vv-text-strong;
    border: 1px solid @vv-border-strong;
    border-radius: 6px;
    padding: 0px 18px;
}
QPushButton#updateCheckCloseButton:hover {
    background-color: @vv-surface-subtle;
}
QPushButton#updateCheckCloseButton:pressed {
    background-color: @vv-surface-sunken;
}
QPushButton#updateCheckCloseButton:focus {
    border: 2px solid @vv-focus;
}
QPushButton#updateCheckUpdateButton {
    background-color: @vv-primary;
    color: @vv-white;
    border: 1px solid @vv-primary;
    border-radius: 6px;
    padding: 0px 18px;
    font-weight: 600;
}
QPushButton#updateCheckUpdateButton:hover {
    background-color: @vv-primary-hover;
    border-color: @vv-primary-hover;
}
QPushButton#updateCheckUpdateButton:pressed {
    background-color: @vv-primary-pressed;
    border-color: @vv-primary-pressed;
}
QPushButton#updateCheckUpdateButton:focus {
    border: 2px solid @vv-focus;
}
)"), dark));

    QProcess *checkProcess = nullptr;
    QNetworkAccessManager *manifestNetwork = nullptr;
    QNetworkReply *manifestReply = nullptr;
    if (maintenanceToolAvailable)
    {
        checkProcess = new QProcess(&dialog);
        checkProcess->setProgram(maintenanceToolPath);
        checkProcess->setArguments(vaporViewUpdateCheckArguments(repositoryUrl));
        checkProcess->setWorkingDirectory(applicationDir);
        checkProcess->setProcessChannelMode(QProcess::SeparateChannels);
    }
    else
    {
        manifestNetwork = new QNetworkAccessManager(&dialog);
    }

    bool checkCompleted = false;
    bool manifestFallbackPending = false;
    auto finishCheck = [&](const VaporViewUpdateCheckResult& result) {
        if (checkCompleted)
        {
            return;
        }
        checkCompleted = true;
        closeButton->setText(english ? QStringLiteral("Close") : QStringLiteral("关闭"));

        const QString outputSummary = vaporViewTrimmedUpdateOutput(result.output);
        const auto showDiagnosticOutput = [outputTitleLabel, outputEdit, outputSummary](bool visible) {
            const bool show = visible && !outputSummary.isEmpty();
            outputTitleLabel->setVisible(show);
            outputEdit->setVisible(show);
            if (show)
            {
                outputEdit->setPlainText(outputSummary);
            }
        };

        switch (result.status)
        {
        case VaporViewUpdateCheckStatus::UpdatesAvailable:
            statusLabel->setText(english ? QStringLiteral("Updates are available.")
                                         : QStringLiteral("发现可用更新。"));
            if (maintenanceToolAvailable)
            {
                detailLabel->setText(english
                                         ? (result.latestVersion.isEmpty()
                                                ? QStringLiteral("Click Update Now to open the maintenance wizard and apply the update from the source below.")
                                                : QStringLiteral("Version %1 is available. Click Update Now to open the maintenance wizard and apply the update from the source below.").arg(result.latestVersion))
                                         : (result.latestVersion.isEmpty()
                                                ? QStringLiteral("点击“立即更新”会打开维护向导，并从下方更新源执行更新。")
                                                : QStringLiteral("发现版本 %1。点击“立即更新”会打开维护向导，并从下方更新源执行更新。").arg(result.latestVersion)));
            }
            else
            {
                detailLabel->setText(english
                                         ? (result.latestVersion.isEmpty()
                                                ? QStringLiteral("This development build can check updates, but it cannot apply them without VaporViewMaintenanceTool. Install VaporView with the setup package to update.")
                                                : QStringLiteral("Version %1 is available. This development build can check updates, but it cannot apply them without VaporViewMaintenanceTool. Install VaporView with the setup package to update.").arg(result.latestVersion))
                                         : (result.latestVersion.isEmpty()
                                                ? QStringLiteral("当前开发构建可以检查更新，但缺少 VaporViewMaintenanceTool，无法直接应用更新。请使用安装包安装 VaporView 后再更新。")
                                                : QStringLiteral("发现版本 %1。当前开发构建可以检查更新，但缺少 VaporViewMaintenanceTool，无法直接应用更新。请使用安装包安装 VaporView 后再更新。").arg(result.latestVersion)));
            }
            showDiagnosticOutput(false);
            updateButton->setVisible(maintenanceToolAvailable);
            updateButton->setEnabled(maintenanceToolAvailable);
            updateButton->setDefault(maintenanceToolAvailable);
            closeButton->setDefault(!maintenanceToolAvailable);
            break;
        case VaporViewUpdateCheckStatus::UpToDate:
            statusLabel->setText(english ? QStringLiteral("VaporView is up to date.")
                                         : QStringLiteral("VaporView 已是最新版本。"));
            detailLabel->setText(english
                                     ? (result.latestVersion.isEmpty()
                                            ? QStringLiteral("The configured source did not report a newer program version.")
                                            : QStringLiteral("The newest repository version is %1; no newer program version was reported.").arg(result.latestVersion))
                                     : (result.latestVersion.isEmpty()
                                            ? QStringLiteral("配置的更新源未报告比当前版本更新的程序。")
                                            : QStringLiteral("更新源最新版本为 %1，未报告比当前版本更新的程序。").arg(result.latestVersion)));
            showDiagnosticOutput(false);
            break;
        case VaporViewUpdateCheckStatus::Failed:
        {
            const QString errorText = result.errorText.isEmpty()
                ? (english ? QStringLiteral("The maintenance tool returned exit code %1.").arg(result.exitCode)
                           : QStringLiteral("维护工具返回退出码 %1。").arg(result.exitCode))
                : result.errorText;
            statusLabel->setText(english ? QStringLiteral("Update check failed.")
                                         : QStringLiteral("检查更新失败。"));
            detailLabel->setText(english
                                     ? QStringLiteral("%1\nPlease check the network connection and try again.").arg(errorText)
                                     : QStringLiteral("%1\n请检查网络连接后重试。").arg(errorText));
            showDiagnosticOutput(true);
            break;
        }
        case VaporViewUpdateCheckStatus::Unknown:
            statusLabel->setText(english ? QStringLiteral("Update check finished.")
                                         : QStringLiteral("检查更新已完成。"));
            detailLabel->setText(english
                                     ? QStringLiteral("The maintenance tool did not report a clear update state. Please try again later or run the maintenance tool from the installation folder if needed.")
                                     : QStringLiteral("维护工具没有返回明确的更新状态。可稍后重试，必要时从安装目录手动运行维护工具。"));
            showDiagnosticOutput(true);
            break;
        }
    };

    QTimer checkTimeout;
    checkTimeout.setSingleShot(true);
    checkTimeout.setInterval(30000);
    QObject::connect(&checkTimeout, &QTimer::timeout, &dialog, [&]() {
        if (checkCompleted)
        {
            return;
        }

        if (manifestReply && manifestReply->isFinished())
        {
            return;
        }
        if (maintenanceToolAvailable && !manifestFallbackPending &&
            (!checkProcess || checkProcess->state() == QProcess::NotRunning))
        {
            return;
        }

        finishCheck(VaporViewUpdateCheckResult{VaporViewUpdateCheckStatus::Failed,
                                               maintenanceToolAvailable && checkProcess
                                                   ? QString::fromLocal8Bit(
                                                         VaporView::processLoggedStandardOutput(checkProcess) +
                                                         checkProcess->readAllStandardOutput())
                                                   : QString(),
                                               english ? QStringLiteral("The update check timed out.")
                                                       : QStringLiteral("检查更新超时。"),
                                               QString(),
                                               -1});
        if (maintenanceToolAvailable && checkProcess)
        {
            checkProcess->kill();
        }
        else if (manifestReply)
        {
            manifestReply->abort();
        }
    });

    auto requestManifestFallback = [&](const VaporViewUpdateCheckResult& maintenanceResult) {
        if (manifestFallbackPending || checkCompleted)
        {
            return;
        }
        manifestFallbackPending = true;
        if (!manifestNetwork)
        {
            manifestNetwork = new QNetworkAccessManager(&dialog);
        }

        const QUrl manifestUrl = vaporViewUpdateManifestUrl(repositoryUrl);
        if (!manifestUrl.isValid() ||
            (manifestUrl.scheme() != QStringLiteral("http") &&
             manifestUrl.scheme() != QStringLiteral("https")))
        {
            manifestFallbackPending = false;
            finishCheck(maintenanceResult);
            return;
        }

        checkTimeout.start();
        QNetworkRequest request(manifestUrl);
        request.setHeader(QNetworkRequest::UserAgentHeader,
                          QStringLiteral("VaporView/%1").arg(applicationVersion));
        manifestReply = manifestNetwork->get(request);
        QObject::connect(manifestReply,
                         &QNetworkReply::finished,
                         &dialog,
                         [&, reply = manifestReply, maintenanceResult]() {
            if (checkCompleted)
            {
                reply->deleteLater();
                return;
            }
            manifestFallbackPending = false;
            checkTimeout.stop();
            VaporViewUpdateCheckResult manifestResult =
                vaporViewClassifyRepositoryReply(reply, applicationVersion);
            if (!maintenanceResult.output.isEmpty())
            {
                const QString maintenanceOutput = vaporViewTrimmedUpdateOutput(maintenanceResult.output);
                manifestResult.output = maintenanceOutput.isEmpty()
                    ? manifestResult.output
                    : QStringLiteral("MaintenanceTool output:\n%1\n\n%2")
                          .arg(maintenanceOutput, manifestResult.output);
            }
            finishCheck(manifestResult);
            reply->deleteLater();
        });
    };

    if (maintenanceToolAvailable)
    {
        VaporView::attachProcessLogging(checkProcess,
                                        QStringLiteral("UpdateCheck"),
                                        QStringLiteral("maintenance_tool"));
        QObject::connect(checkProcess, &QProcess::errorOccurred, &dialog, [&](QProcess::ProcessError error) {
            if (checkCompleted || error != QProcess::FailedToStart)
            {
                return;
            }
            finishCheck(VaporViewUpdateCheckResult{VaporViewUpdateCheckStatus::Failed,
                                                   QString::fromLocal8Bit(
                                                       VaporView::processLoggedStandardOutput(checkProcess) +
                                                       checkProcess->readAllStandardOutput()),
                                                   checkProcess->errorString(),
                                                   QString(),
                                                   -1});
        });
        QObject::connect(checkProcess,
                         qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
                         &dialog,
                         [&](int exitCode, QProcess::ExitStatus exitStatus) {
            if (checkCompleted)
            {
                return;
            }
            checkTimeout.stop();
            const QString output =
                QString::fromLocal8Bit(VaporView::processLoggedStandardOutput(checkProcess) +
                                       checkProcess->readAllStandardOutput()) +
                QString::fromLocal8Bit(VaporView::processLoggedStandardError(checkProcess) +
                                       checkProcess->readAllStandardError());
            const VaporViewUpdateCheckResult maintenanceResult =
                vaporViewClassifyUpdateCheckResult(output, exitCode, exitStatus);
            if (maintenanceResult.status == VaporViewUpdateCheckStatus::Unknown)
            {
                requestManifestFallback(maintenanceResult);
            }
            else
            {
                finishCheck(maintenanceResult);
            }
        });
    }
    QObject::connect(updateButton, &QPushButton::clicked, &dialog, [&]() {
        if (startUpdater())
        {
            dialog.accept();
            QTimer::singleShot(0, []() {
                QCoreApplication::quit();
            });
        }
    });
    QObject::connect(closeButton, &QPushButton::clicked, &dialog, &QDialog::reject);
    QObject::connect(&dialog, &QDialog::rejected, &dialog, [&]() {
        if (checkProcess && checkProcess->state() != QProcess::NotRunning)
        {
            checkProcess->kill();
        }
        if (manifestReply && !manifestReply->isFinished())
        {
            manifestReply->abort();
        }
    });

    QTimer::singleShot(0, &dialog, [&]() {
        checkTimeout.start();
        if (maintenanceToolAvailable)
        {
            checkProcess->start();
            return;
        }

        const QUrl manifestUrl = vaporViewUpdateManifestUrl(repositoryUrl);
        if (!manifestUrl.isValid() ||
            (manifestUrl.scheme() != QStringLiteral("http") &&
             manifestUrl.scheme() != QStringLiteral("https")))
        {
            checkTimeout.stop();
            finishCheck(VaporViewUpdateCheckResult{
                VaporViewUpdateCheckStatus::Failed,
                QString(),
                english ? QStringLiteral("The update repository URL is invalid.")
                        : QStringLiteral("更新仓库地址无效。"),
                QString(),
                -1});
            return;
        }

        QNetworkRequest request(manifestUrl);
        request.setHeader(QNetworkRequest::UserAgentHeader,
                          QStringLiteral("VaporView/%1").arg(applicationVersion));
        manifestReply = manifestNetwork->get(request);
        QObject::connect(manifestReply, &QNetworkReply::finished, &dialog, [&, reply = manifestReply]() {
            if (checkCompleted)
            {
                reply->deleteLater();
                return;
            }
            checkTimeout.stop();
            finishCheck(vaporViewClassifyRepositoryReply(reply, applicationVersion));
            reply->deleteLater();
        });
    });
    dialog.resize(dialog.sizeHint().expandedTo(QSize(500, 300)));
    dialog.exec();
}

void MainWindow::onSwitchLanguage()
{
    if (state_->language_switch_in_progress_)
    {
        return;
    }

    state_->language_switch_in_progress_ = true;
    QTimer::singleShot(0, this, [this]() {
        state_->is_english_ = !state_->is_english_;
        setEnglish(state_->is_english_);
        log(state_->is_english_ ? "Language switched to English" : "语言已切换为中文");
        state_->language_switch_in_progress_ = false;
    });
}
void MainWindow::showAboutDialog()
{
    const bool english = state_->is_english_;
    const bool dark = state_->dark_theme_enabled_;
    const QString title = english ? QStringLiteral("About VaporView") : QStringLiteral("关于 VaporView");
    const QString applicationVersion = vaporViewApplicationVersion();

    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("aboutDialog"));
    dialog.setWindowTitle(title);
    dialog.setWindowModality(Qt::WindowModal);
    dialog.setMinimumSize(520, 460);

    auto *rootLayout = new QVBoxLayout(&dialog);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    auto *body = new QWidget(&dialog);
    body->setObjectName(QStringLiteral("aboutDialogBody"));
    auto *bodyLayout = new QVBoxLayout(body);
    bodyLayout->setContentsMargins(48, 24, 48, 24);
    bodyLayout->setSpacing(0);
    bodyLayout->addStretch(1);

    auto *logoLabel = new QLabel(body);
    logoLabel->setObjectName(QStringLiteral("aboutDialogLogo"));
    logoLabel->setAccessibleName(english ? QStringLiteral("VaporView logo") : QStringLiteral("VaporView 标志"));
    logoLabel->setFixedSize(108, 108);
    logoLabel->setAlignment(Qt::AlignCenter);
    logoLabel->setPixmap(renderVaporViewLogo(dark, 104, logoLabel->devicePixelRatioF()));
    bodyLayout->addWidget(logoLabel, 0, Qt::AlignHCenter);
    bodyLayout->addSpacing(18);

    auto *productNameLabel = new QLabel(QStringLiteral("VaporView"), body);
    productNameLabel->setObjectName(QStringLiteral("aboutDialogProductNameLabel"));
    productNameLabel->setAlignment(Qt::AlignCenter);
    QFont productNameFont = productNameLabel->font();
    const qreal basePointSize = productNameFont.pointSizeF() > 0.0 ? productNameFont.pointSizeF() : 10.0;
    productNameFont.setPointSizeF(std::max<qreal>(20.0, basePointSize * 1.9));
    productNameFont.setWeight(QFont::DemiBold);
    productNameLabel->setFont(productNameFont);
    bodyLayout->addWidget(productNameLabel);
    bodyLayout->addSpacing(18);

    auto createCenteredLabel = [body](const QString& objectName, const QString& text) {
        auto *label = new QLabel(text, body);
        label->setObjectName(objectName);
        label->setAlignment(Qt::AlignCenter);
        label->setWordWrap(true);
        label->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        return label;
    };

    QLabel *descriptionLabel = createCenteredLabel(
        QStringLiteral("aboutDialogDescriptionLabel"),
        english
            ? QStringLiteral("Integrated Navigation and Environmental Monitoring System")
            : QStringLiteral("组合导航与环境监控系统"));
    QFont descriptionFont = descriptionLabel->font();
    descriptionFont.setPointSizeF(std::max<qreal>(11.0, basePointSize * 1.12));
    descriptionFont.setWeight(QFont::Medium);
    descriptionLabel->setFont(descriptionFont);
    bodyLayout->addWidget(descriptionLabel);
    bodyLayout->addSpacing(10);

    bodyLayout->addWidget(createCenteredLabel(
        QStringLiteral("aboutDialogFrameworkLabel"),
        english ? QStringLiteral("Built with Qt 6") : QStringLiteral("基于 Qt 6 构建")));
    bodyLayout->addSpacing(2);
    bodyLayout->addWidget(createCenteredLabel(
        QStringLiteral("aboutDialogVersionLabel"),
        english
            ? QStringLiteral("Version %1").arg(applicationVersion)
            : QStringLiteral("版本 %1").arg(applicationVersion)));
    bodyLayout->addSpacing(20);
    bodyLayout->addWidget(createCenteredLabel(
        QStringLiteral("aboutDialogSupportedDevicesLabel"),
        english
            ? QStringLiteral("Supports EPSILON, PTB210, HMP3, TFA1500-L, and RD105")
            : QStringLiteral("支持 EPSILON、PTB210、HMP3、TFA1500-L 与 RD105")));
    bodyLayout->addSpacing(20);
    bodyLayout->addWidget(createCenteredLabel(
        QStringLiteral("aboutDialogCopyrightLabel"),
        QStringLiteral("© 2026 VaporView")));
    bodyLayout->addStretch(1);
    rootLayout->addWidget(body, 1);

    auto *footer = new QWidget(&dialog);
    footer->setObjectName(QStringLiteral("aboutDialogFooter"));
    footer->setMinimumHeight(82);
    auto *footerLayout = new QHBoxLayout(footer);
    footerLayout->setContentsMargins(24, 18, 28, 18);
    footerLayout->setSpacing(0);
    footerLayout->addStretch(1);

    auto *okButton = new QPushButton(english ? QStringLiteral("OK") : QStringLiteral("确定"), footer);
    okButton->setObjectName(QStringLiteral("aboutDialogOkButton"));
    okButton->setFixedSize(124, 40);
    okButton->setDefault(true);
    okButton->setAutoDefault(true);
    QObject::connect(okButton, &QPushButton::clicked, &dialog, &QDialog::accept);
    footerLayout->addWidget(okButton, 0, Qt::AlignRight | Qt::AlignVCenter);
    rootLayout->addWidget(footer);

    VaporView::installCustomTitleBar(&dialog, false);
    if (QLabel *titleLogo = dialog.findChild<QLabel *>(QStringLiteral("customTitleLogo")))
    {
        titleLogo->setFixedSize(36, 36);
        titleLogo->setPixmap(renderVaporViewLogo(dark, 30, titleLogo->devicePixelRatioF()));
    }
    for (QToolButton *button : dialog.findChildren<QToolButton *>())
    {
        if (button->accessibleName() == QStringLiteral("titleLanguageButton") ||
            button->accessibleName() == QStringLiteral("titleThemeButton"))
        {
            button->hide();
        }
        else if (button->objectName() == QStringLiteral("windowCloseButton"))
        {
            button->setAccessibleName(english ? QStringLiteral("Close") : QStringLiteral("关闭"));
            button->setFocusPolicy(Qt::TabFocus);
        }
    }
    for (QFrame *separator : dialog.findChildren<QFrame *>(QStringLiteral("titleBarSeparator")))
    {
        separator->hide();
    }

    dialog.setStyleSheet(applyAppThemeTokens(
        customTitleBarStyleSheet(dark) + QStringLiteral(R"(
QDialog#aboutDialog,
QDialog#aboutDialog QWidget#customTitleBarContent,
QWidget#aboutDialogBody {
    background-color: @vv-surface;
    color: @vv-text;
}
QDialog#aboutDialog QLabel {
    background-color: transparent;
    border: none;
}
QLabel#aboutDialogProductNameLabel {
    color: @vv-text-strong;
}
QLabel#aboutDialogDescriptionLabel,
QLabel#aboutDialogFrameworkLabel,
QLabel#aboutDialogVersionLabel,
QLabel#aboutDialogSupportedDevicesLabel {
    color: @vv-text-secondary;
}
QLabel#aboutDialogCopyrightLabel {
    color: @vv-text-muted;
}
QWidget#aboutDialogFooter {
    background-color: @vv-surface-alt;
    border-top: 1px solid @vv-border;
}
QPushButton#aboutDialogOkButton {
    background-color: @vv-surface-raised;
    color: @vv-text-strong;
    border: 1px solid @vv-border-strong;
    border-radius: 6px;
    padding: 0px 18px;
}
QPushButton#aboutDialogOkButton:hover {
    background-color: @vv-surface-subtle;
}
QPushButton#aboutDialogOkButton:pressed {
    background-color: @vv-surface-sunken;
}
QPushButton#aboutDialogOkButton:focus {
    border: 2px solid @vv-focus;
}
)"), dark));

    dialog.resize(QSize(560, 500));
    dialog.exec();
}

void MainWindow::updateThemeAction()
{
    if (!state_->theme_toggle_action_)
    {
        return;
    }

    const bool targetLight = state_->dark_theme_enabled_;
    state_->theme_toggle_action_->setIcon(targetLight ? createLightThemeIcon() : createDarkThemeIcon());
    state_->theme_toggle_action_->setText(targetLight
        ? (state_->is_english_ ? "Light Theme" : "亮色模式")
        : (state_->is_english_ ? "Dark Theme" : "暗色模式"));
    state_->theme_toggle_action_->setToolTip(targetLight
        ? (state_->is_english_ ? "Switch to light theme" : "切换到亮色模式")
        : (state_->is_english_ ? "Switch to dark theme" : "切换到暗色模式"));
}

void MainWindow::updateThemedIcons()
{
    if (state_->lang_action_)
    {
        state_->lang_action_->setIcon(createLanguageIcon());
    }
    if (state_->refresh_ports_btn_)
    {
        state_->refresh_ports_btn_->setIcon(createRefreshIcon());
    }
    if (state_->connect_btn_)
    {
        state_->connect_btn_->setIcon(createConnectIcon());
    }
    if (state_->cancel_connect_btn_)
    {
        state_->cancel_connect_btn_->setIcon(createCancelIcon());
    }
    if (state_->disconnect_btn_)
    {
        state_->disconnect_btn_->setIcon(createDisconnectIcon());
    }
    if (state_->scheduled_recording_action_)
    {
        state_->scheduled_recording_action_->setIcon(createTimerIcon());
    }
    if (state_->start_recording_btn_)
    {
        state_->start_recording_btn_->setIcon(createPlayIcon());
    }
    if (state_->pause_recording_btn_)
    {
        state_->pause_recording_btn_->setIcon(createPauseIcon());
    }
    if (state_->stop_recording_btn_)
    {
        state_->stop_recording_btn_->setIcon(createStopIcon());
    }
    updateRtkConfigIcon();
    if (state_->clear_log_action_)
    {
        state_->clear_log_action_->setIcon(createClearLogIcon());
    }
    if (state_->session_viewer_action_)
    {
        state_->session_viewer_action_->setIcon(createWaveformViewerIcon());
    }
#ifdef VAPORVIEW_HAS_OSGEARTH
    if (state_->map3d_action_)
    {
        state_->map3d_action_->setIcon(createLucideIcon(QStringLiteral("earth"), toolbarColor(AppThemeColor::ToolbarBlue)));
    }
    if (state_->map3d_diagnostics_action_)
    {
        state_->map3d_diagnostics_action_->setIcon(createLucideIcon(QStringLiteral("activity"), toolbarColor(AppThemeColor::ToolbarBlue)));
    }
#endif
    if (state_->temperature_overview_panel_)
    {
        state_->temperature_overview_panel_->updateThemedIcons();
    }
    updateSidebarNavIcons();
    updateCustomLogoPixmap();
    if (state_->log_filter_btn_)
    {
        state_->log_filter_btn_->setIcon(createLogFilterIcon());
    }
    updateFontScaleMenuCheckIcons();
    updateLogSidePanelToggleButton();
    updateSectionTitleIcons(this, state_->dark_theme_enabled_);
    updateLogFilterAction();
}

void MainWindow::updateRtkConfigIcon()
{
    const QString baseText = state_->is_english_ ? QStringLiteral("RTK config") : QStringLiteral("RTK配置");
    const QString stateText = state_->rtk_service_running_
        ? (state_->is_english_ ? QStringLiteral("running") : QStringLiteral("运行中"))
        : (state_->is_english_ ? QStringLiteral("stopped") : QStringLiteral("未启动"));
    const QString toolTip = QStringLiteral("%1 (%2)").arg(baseText, stateText);
    if (state_->rtk_config_action_)
    {
        state_->rtk_config_action_->setIcon(createRtkSatelliteIcon(state_->rtk_service_running_));
        state_->rtk_config_action_->setToolTip(toolTip);
    }
    if (state_->rtk_config_nav_btn_)
    {
        state_->rtk_config_nav_btn_->setToolTip(toolTip);
    }
}

void MainWindow::updateFontScaleMenuCheckIcons()
{
    const QIcon checkIcon = createMenuCheckIcon(state_->dark_theme_enabled_);
    const auto applyIcon = [this, &checkIcon](QAction *action, int minPercent, int maxPercent) {
        if (!action)
        {
            return;
        }
        action->setIcon(state_->font_scale_percent_ >= minPercent && state_->font_scale_percent_ <= maxPercent ? checkIcon : QIcon());
    };

    applyIcon(state_->font_tiny_action_, 70, 75);
    applyIcon(state_->font_extra_small_action_, 76, 85);
    applyIcon(state_->font_small_action_, 86, 95);
    applyIcon(state_->font_normal_action_, 96, 107);
    applyIcon(state_->font_large_action_, 108, 122);
    applyIcon(state_->font_extra_large_action_, 123, 150);
}

QString MainWindow::currentMainPageTitleText() const
{
    const int pageIndex = state_->main_page_stack_ ? state_->main_page_stack_->currentIndex() : 0;
    if (state_->app_nav_button_group_)
    {
        if (QAbstractButton *button = state_->app_nav_button_group_->button(pageIndex))
        {
            const QString accessibleName = button->accessibleName().trimmed();
            if (!accessibleName.isEmpty())
            {
                return accessibleName;
            }
            const QString text = button->text().trimmed();
            if (!text.isEmpty())
            {
                return text;
            }
            const QString toolTip = button->toolTip().trimmed();
            if (!toolTip.isEmpty())
            {
                return toolTip;
            }
        }
    }

    switch (pageIndex)
    {
    case 1:
        return state_->is_english_ ? QStringLiteral("Device") : QStringLiteral("设备配置");
    case 2:
        return state_->is_english_ ? QStringLiteral("Thermal") : QStringLiteral("温控");
    case 0:
    default:
        return state_->is_english_ ? QStringLiteral("Home") : QStringLiteral("首页");
    }
}

void MainWindow::updateCustomTitleBarTexts()
{
    if (state_->custom_title_label_)
    {
        state_->custom_title_label_->setText(currentMainPageTitleText());
    }
    if (state_->ui_test_mode_badge_)
    {
        state_->ui_test_mode_badge_->setText(state_->is_english_ ? QStringLiteral("UI TEST") : QStringLiteral("界面测试"));
        state_->ui_test_mode_badge_->setAccessibleName(
            state_->is_english_ ? QStringLiteral("UI test mode active") : QStringLiteral("界面测试模式已启用"));
        state_->ui_test_mode_badge_->setToolTip(
            state_->is_english_
                ? QStringLiteral("Device, recording, and business-file operations stay simulated. RTK mountpoint detection and Test Connection may access the NTRIP network with sandbox settings.")
                : QStringLiteral("设备、记录和业务文件操作保持模拟；RTK“检测挂载点”和“测试连接”可使用沙箱配置访问 NTRIP 网络。"));
    }
    if (state_->title_menu_btn_)
    {
        state_->title_menu_btn_->setToolTip(state_->is_english_ ? "Menu" : "菜单");
    }
    updateCustomLogoTooltip();
    if (state_->title_language_btn_)
    {
        state_->title_language_btn_->setToolTip(state_->is_english_ ? "Switch to Chinese" : "切换到英文");
    }
    updateLogSidePanelToggleButton();
    if (state_->window_minimize_btn_)
    {
        state_->window_minimize_btn_->setToolTip(state_->is_english_ ? "Minimize" : "最小化");
    }
    if (state_->window_close_btn_)
    {
        state_->window_close_btn_->setToolTip(state_->is_english_ ? "Close" : "关闭");
    }
    updateWindowControlButtons();
}

void MainWindow::updateCustomTitleBarStyle()
{
    if (!state_->custom_title_bar_)
    {
        return;
    }

    state_->custom_title_bar_->setFixedHeight(scalePixels(48));
    const QSize actionButtonSize(scalePixels(34), scalePixels(34));
    const QSize windowButtonSize(scalePixels(34), scalePixels(34));
    const QSize iconSize(scalePixels(24), scalePixels(24));
    const QSize maximizeIconSize(scalePixels(21), scalePixels(21));

    const auto buttons = state_->custom_title_bar_->findChildren<QToolButton *>();
    for (QToolButton *button : buttons)
    {
        if (!button)
        {
            continue;
        }
        const bool windowButton = button == state_->window_minimize_btn_ ||
                                  button == state_->window_maximize_btn_ ||
                                  button == state_->window_close_btn_;
        button->setFixedSize(windowButton ? windowButtonSize : actionButtonSize);
        button->setIconSize(iconSize);
    }

    if (state_->window_maximize_btn_)
    {
        state_->window_maximize_btn_->setIconSize(maximizeIconSize);
    }
    if (state_->log_clear_btn_)
    {
        state_->log_clear_btn_->setFixedSize(actionButtonSize);
        state_->log_clear_btn_->setIconSize(iconSize);
    }
    if (state_->log_filter_btn_)
    {
        state_->log_filter_btn_->setFixedSize(actionButtonSize);
        state_->log_filter_btn_->setIconSize(iconSize);
    }
    updateCustomLogoPixmap();
    updateCustomLogoTooltip();
    const QIcon logoIcon = createVaporViewLogoIcon(state_->dark_theme_enabled_);
    if (!logoIcon.isNull())
    {
        setWindowIcon(logoIcon);
        qApp->setWindowIcon(logoIcon);
    }

    if (state_->title_menu_btn_)
    {
        state_->title_menu_btn_->setIcon(createTitleBarIcon(QStringLiteral("menu"), state_->dark_theme_enabled_));
    }
    if (state_->title_language_btn_)
    {
        state_->title_language_btn_->setIcon(createLanguageIcon());
    }
    updateLogSidePanelToggleButton();
    if (state_->title_application_panel_)
    {
        state_->title_application_panel_->hide();
        state_->title_application_panel_->setStyleSheet(titleApplicationPanelStyleSheet(state_->dark_theme_enabled_));
    }
    if (state_->title_application_sub_panel_)
    {
        state_->title_application_sub_panel_->hide();
        state_->title_application_sub_panel_->setStyleSheet(titleApplicationPanelStyleSheet(state_->dark_theme_enabled_));
    }
    if (state_->title_application_nested_panel_)
    {
        state_->title_application_nested_panel_->hide();
        state_->title_application_nested_panel_->setStyleSheet(titleApplicationPanelStyleSheet(state_->dark_theme_enabled_));
    }
    if (state_->window_minimize_btn_)
    {
        state_->window_minimize_btn_->setIcon(createTitleBarIcon(QStringLiteral("minus"), state_->dark_theme_enabled_));
    }
    if (state_->window_close_btn_)
    {
        state_->window_close_btn_->setIcon(createTitleBarIcon(QStringLiteral("x"), state_->dark_theme_enabled_));
    }
    updateWindowControlButtons();
}

void MainWindow::updateWindowControlButtons()
{
    if (!state_->window_maximize_btn_)
    {
        return;
    }

    const bool shouldRestore = isWindowMaximizedForUi();
    state_->window_maximize_btn_->setIcon(createTitleBarIcon(shouldRestore ? QStringLiteral("copy") : QStringLiteral("square"),
                                                     state_->dark_theme_enabled_));
    state_->window_maximize_btn_->setToolTip(shouldRestore
        ? (state_->is_english_ ? "Restore" : "还原")
        : (state_->is_english_ ? "Maximize" : "最大化"));
}

void MainWindow::toggleWindowMaximized()
{
    if (isFullScreen())
    {
        return;
    }

    if (isWindowMaximizedForUi())
    {
        const QRect restoreGeometry = state_->normal_window_geometry_.isValid()
            ? state_->normal_window_geometry_
            : fallbackNormalWindowGeometry();
        setWindowState(windowState() & ~Qt::WindowMaximized);
        showNormal();
        if (restoreGeometry.isValid())
        {
            setGeometry(restoreGeometry);
        }
    }
    else
    {
        rememberNormalWindowGeometry();
        showMaximized();
    }

    updateWindowControlButtons();
    updateWindowBorderFrames();
    updateWindowResizeHandles();
    QTimer::singleShot(0, this, &MainWindow::updateWindowControlButtons);
    QTimer::singleShot(60, this, &MainWindow::updateWindowControlButtons);
}

bool MainWindow::isWindowMaximizedForUi() const
{
    if (isMaximized() || windowState().testFlag(Qt::WindowMaximized))
    {
        return true;
    }
    if (isFullScreen())
    {
        return false;
    }

    const QRect availableGeometry = currentScreenAvailableGeometry();
    if (!availableGeometry.isValid())
    {
        return false;
    }

    const int tolerance = std::max(3, scalePixels(3));
    auto coversAvailableGeometry = [&](const QRect& rect) {
        return rect.isValid() &&
               rect.left() <= availableGeometry.left() + tolerance &&
               rect.top() <= availableGeometry.top() + tolerance &&
               rect.right() >= availableGeometry.right() - tolerance &&
               rect.bottom() >= availableGeometry.bottom() - tolerance &&
               rect.width() >= availableGeometry.width() - tolerance &&
               rect.height() >= availableGeometry.height() - tolerance;
    };

    return coversAvailableGeometry(frameGeometry()) || coversAvailableGeometry(geometry());
}

void MainWindow::rememberNormalWindowGeometry()
{
    if (isFullScreen() || isMaximized() || windowState().testFlag(Qt::WindowMaximized))
    {
        return;
    }

    const QRect currentGeometry = geometry();
    if (!currentGeometry.isValid() || currentGeometry.width() <= 0 || currentGeometry.height() <= 0)
    {
        return;
    }

    const QRect availableGeometry = currentScreenAvailableGeometry();
    if (availableGeometry.isValid())
    {
        const int tolerance = std::max(3, scalePixels(3));
        const bool visuallyMaximized =
            currentGeometry.left() <= availableGeometry.left() + tolerance &&
            currentGeometry.top() <= availableGeometry.top() + tolerance &&
            currentGeometry.right() >= availableGeometry.right() - tolerance &&
            currentGeometry.bottom() >= availableGeometry.bottom() - tolerance &&
            currentGeometry.width() >= availableGeometry.width() - tolerance &&
            currentGeometry.height() >= availableGeometry.height() - tolerance;
        if (visuallyMaximized)
        {
            return;
        }
    }

    state_->normal_window_geometry_ = currentGeometry;
}

QRect MainWindow::fallbackNormalWindowGeometry() const
{
    const QRect availableGeometry = currentScreenAvailableGeometry();
    const QSize minimumSize = this->minimumSize().expandedTo(minimumSizeHint());
    QSize targetSize = state_->base_window_size_.expandedTo(minimumSize);
    if (availableGeometry.isValid())
    {
        targetSize = targetSize.boundedTo(availableGeometry.size()).expandedTo(minimumSize.boundedTo(availableGeometry.size()));
        const QPoint topLeft(
            availableGeometry.left() + std::max(0, (availableGeometry.width() - targetSize.width()) / 2),
            availableGeometry.top() + std::max(0, (availableGeometry.height() - targetSize.height()) / 2));
        return QRect(topLeft, targetSize);
    }
    return QRect(QPoint(80, 80), targetSize);
}

QRect MainWindow::currentScreenAvailableGeometry() const
{
    const QScreen *targetScreen = screen();
    if (!targetScreen && windowHandle())
    {
        targetScreen = windowHandle()->screen();
    }
    if (!targetScreen)
    {
        targetScreen = QGuiApplication::primaryScreen();
    }
    return targetScreen ? targetScreen->availableGeometry() : QRect();
}

void MainWindow::setupWindowBorderFrames()
{
    auto createBorder = [this]() {
        auto *border = new QFrame(this);
        border->setAttribute(Qt::WA_TransparentForMouseEvents);
        border->setFocusPolicy(Qt::NoFocus);
        border->setFrameShape(QFrame::NoFrame);
        border->setLineWidth(0);
        border->setAutoFillBackground(false);
        return border;
    };

    state_->window_border_top_ = createBorder();
    state_->window_border_right_ = createBorder();
    state_->window_border_bottom_ = createBorder();
    state_->window_border_left_ = createBorder();

    state_->window_border_top_->hide();
    state_->window_border_bottom_->setStyleSheet(QStringLiteral("background-color: %1; border: none;")
        .arg(appThemeColorName(AppThemeColor::SurfaceSunken, state_->dark_theme_enabled_)));
    const QString verticalBorderStyle = QStringLiteral("background-color: %1; border: none;")
        .arg(appThemeColorName(AppThemeColor::SurfaceSunken, state_->dark_theme_enabled_));
    state_->window_border_left_->setStyleSheet(verticalBorderStyle);
    state_->window_border_right_->setStyleSheet(verticalBorderStyle);

    updateWindowBorderFrames();
}

void MainWindow::updateWindowBorderFrames()
{
    const bool visible = !isFullScreen() && !isWindowMaximizedForUi();
    const int borderThickness = 1;
    if (state_->window_border_top_)
    {
        state_->window_border_top_->setVisible(false);
    }
    if (state_->window_border_left_)
    {
        state_->window_border_left_->setVisible(visible);
        state_->window_border_left_->setGeometry(0, 0, borderThickness, height());
        state_->window_border_left_->raise();
    }
    if (state_->window_border_right_)
    {
        state_->window_border_right_->setVisible(visible);
        state_->window_border_right_->setGeometry(std::max(0, width() - borderThickness), 0, borderThickness, height());
        state_->window_border_right_->raise();
    }
    if (state_->window_border_bottom_)
    {
        state_->window_border_bottom_->setVisible(visible);
        state_->window_border_bottom_->setGeometry(0, std::max(0, height() - borderThickness), width(), borderThickness);
        state_->window_border_bottom_->raise();
    }
}

void MainWindow::setupWindowResizeHandles()
{
    const QVector<Qt::Edges> edges = {
        Qt::TopEdge | Qt::LeftEdge,
        Qt::TopEdge,
        Qt::TopEdge | Qt::RightEdge,
        Qt::LeftEdge,
        Qt::RightEdge,
        Qt::BottomEdge | Qt::LeftEdge,
        Qt::BottomEdge,
        Qt::BottomEdge | Qt::RightEdge,
    };

    state_->window_resize_handles_.reserve(edges.size());
    for (Qt::Edges edgeSet : edges)
    {
        auto *handle = createWindowResizeHandle(edgeSet, this);
        handle->setObjectName(QStringLiteral("windowResizeHandle"));
        state_->window_resize_handles_.append(handle);
    }

    updateWindowResizeHandles();
}

void MainWindow::updateWindowResizeHandles()
{
    if (state_->window_resize_handles_.size() != 8)
    {
        return;
    }

    const bool visible = !isFullScreen() && !isWindowMaximizedForUi();
    const int thickness = scalePixels(8);
    const int w = width();
    const int h = height();
    const int rightX = std::max(0, w - thickness);
    const int bottomY = std::max(0, h - thickness);

    const QVector<QRect> geometries = {
        QRect(0, 0, thickness, thickness),
        QRect(thickness, 0, std::max(0, w - thickness * 2), thickness),
        QRect(rightX, 0, thickness, thickness),
        QRect(0, thickness, thickness, std::max(0, h - thickness * 2)),
        QRect(rightX, thickness, thickness, std::max(0, h - thickness * 2)),
        QRect(0, bottomY, thickness, thickness),
        QRect(thickness, bottomY, std::max(0, w - thickness * 2), thickness),
        QRect(rightX, bottomY, thickness, thickness),
    };

    for (int i = 0; i < state_->window_resize_handles_.size(); ++i)
    {
        QWidget *handle = state_->window_resize_handles_.at(i);
        handle->setVisible(visible);
        handle->setGeometry(geometries.at(i));
        handle->raise();
    }

    updateWindowBorderFrames();
    if (state_->title_application_panel_ && state_->title_application_panel_->isVisible())
    {
        state_->title_application_panel_->raise();
    }
    if (state_->title_application_sub_panel_ && state_->title_application_sub_panel_->isVisible())
    {
        state_->title_application_sub_panel_->raise();
    }
    if (state_->title_application_nested_panel_ && state_->title_application_nested_panel_->isVisible())
    {
        state_->title_application_nested_panel_->raise();
    }
}

void MainWindow::onToggleTheme()
{
    state_->dark_theme_enabled_ = !state_->dark_theme_enabled_;
    if (qApp)
    {
        qApp->setProperty(kAppDarkThemeProperty, state_->dark_theme_enabled_);
    }
    discardTitleApplicationMenuPanel();
    applyStyleConfiguration();
    updateThemeAction();

    QSettings settings("VaporView", "MainWindow");
    VaporView::setPersistentSetting(settings, QStringLiteral("dark_theme_enabled"), state_->dark_theme_enabled_);

    log(state_->dark_theme_enabled_
        ? (state_->is_english_ ? "Theme switched to dark" : "已切换为暗色模式")
        : (state_->is_english_ ? "Theme switched to light" : "已切换为亮色模式"));
}

void MainWindow::onFontScaleTriggered(QAction *action)
{
    if (!action)
    {
        return;
    }

    const int percent = action->data().toInt();
    if (percent == state_->font_scale_percent_)
    {
        return;
    }

    setFontScale(percent);
    log(QString(state_->is_english_ ? "Font size set to %1%" : "字体大小已设置为 %1%").arg(percent));
}
