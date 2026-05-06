#include "SkyTuiController.h"

#include <QJsonDocument>

namespace VaporView
{
namespace
{
QString yesNo(bool value)
{
    return value ? QStringLiteral("是") : QStringLiteral("否");
}

QString normalizedCommand(QString line)
{
    line = line.simplified();
    if (line.startsWith(QLatin1Char('/')))
    {
        line.remove(0, 1);
    }
    return line.simplified();
}

QStringList jsonLines(const QJsonObject& object)
{
    const QString json = QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Indented)).trimmed();
    return json.split(QLatin1Char('\n'));
}

}  // namespace

SkyTuiController::SkyTuiController(SkyRuntime *runtime, const SkyRuntimeOptions& options, QObject *parent)
    : QObject(parent)
    , runtime_(runtime)
    , options_(options)
{
}

SkyTuiCommandResult SkyTuiController::executeCommand(const QString& line)
{
    SkyTuiCommandResult result;
    const QString normalized = normalizedCommand(line);
    if (normalized.isEmpty())
    {
        return result;
    }

    const QStringList tokens = normalized.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    const QString command = tokens.value(0).toLower();

    if (command == QStringLiteral("help") || command == QStringLiteral("h"))
    {
        result.messages = helpLines();
    }
    else if (command == QStringLiteral("status") || command == QStringLiteral("s"))
    {
        result.messages = statusLines();
    }
    else if (command == QStringLiteral("devices"))
    {
        result.messages = deviceLines();
    }
    else if (command == QStringLiteral("quit") ||
             command == QStringLiteral("exit") ||
             command == QStringLiteral("stop"))
    {
        result.type = SkyTuiCommandResult::Type::Quit;
    }
    else if (command == QStringLiteral("connect") ||
             command == QStringLiteral("disconnect") ||
             command == QStringLiteral("reconnect"))
    {
        result = handleDeviceCommand(command, tokens.value(1));
    }
    else if (command == QStringLiteral("record"))
    {
        result = handleRecordCommand(tokens.value(1).toLower());
    }
    else if (command == QStringLiteral("waveform"))
    {
        result = handleWaveformCommand(tokens.value(1).toLower());
    }
    else if (command == QStringLiteral("config") && tokens.value(1).toLower() == QStringLiteral("show"))
    {
        result.messages = configLines();
    }
    else if (command == QStringLiteral("cfg"))
    {
        result.messages = configLines();
    }
    else if (command == QStringLiteral("clear"))
    {
        result.type = SkyTuiCommandResult::Type::ClearLogs;
    }
    else if (command == QStringLiteral("logs"))
    {
        result.messages << QStringLiteral("日志区已经在主面板中。可用 PageUp/PageDown 滚动查看。");
    }
    else if (command == QStringLiteral("palette"))
    {
        result.messages << QStringLiteral("按 Ctrl+P，或输入 / 打开命令面板。");
    }
    else if (command == QStringLiteral("theme") && tokens.value(1).toLower() == QStringLiteral("dark"))
    {
        result.messages << QStringLiteral("深色终端主题已启用。");
    }
    else
    {
        result.messages << QStringLiteral("未知命令：%1").arg(normalized)
                        << QStringLiteral("输入 /help 查看可用命令。");
    }

    return result;
}

QList<SkyTuiCommandItem> SkyTuiController::commandPalette() const
{
    return {
        {QStringLiteral("/help"), QStringLiteral("显示帮助和快捷键说明")},
        {QStringLiteral("/status"), QStringLiteral("查看天空端运行状态、记录状态和链路统计")},
        {QStringLiteral("/devices"), QStringLiteral("查看 EPSILON/PTB/HMP/Lidar/Wave TCP 设备状态")},
        {QStringLiteral("/connect epsilon"), QStringLiteral("请求天空端连接 EPSILON")},
        {QStringLiteral("/disconnect epsilon"), QStringLiteral("请求天空端断开 EPSILON")},
        {QStringLiteral("/reconnect epsilon"), QStringLiteral("请求天空端重连 EPSILON")},
        {QStringLiteral("/connect ptb"), QStringLiteral("请求天空端连接 PTB 气压计")},
        {QStringLiteral("/disconnect ptb"), QStringLiteral("请求天空端断开 PTB 气压计")},
        {QStringLiteral("/reconnect ptb"), QStringLiteral("请求天空端重连 PTB 气压计")},
        {QStringLiteral("/connect hmp"), QStringLiteral("请求天空端连接 HMP 温湿度计")},
        {QStringLiteral("/disconnect hmp"), QStringLiteral("请求天空端断开 HMP 温湿度计")},
        {QStringLiteral("/reconnect hmp"), QStringLiteral("请求天空端重连 HMP 温湿度计")},
        {QStringLiteral("/connect lidar"), QStringLiteral("请求天空端连接激光测距模块")},
        {QStringLiteral("/disconnect lidar"), QStringLiteral("请求天空端断开激光测距模块")},
        {QStringLiteral("/reconnect lidar"), QStringLiteral("请求天空端重连激光测距模块")},
        {QStringLiteral("/connect wave"), QStringLiteral("请求天空端连接二次谐波 TCP 波形源")},
        {QStringLiteral("/disconnect wave"), QStringLiteral("请求天空端断开二次谐波 TCP 波形源")},
        {QStringLiteral("/reconnect wave"), QStringLiteral("请求天空端重连二次谐波 TCP 波形源")},
        {QStringLiteral("/connect all"), QStringLiteral("连接所有已启用的天空端设备")},
        {QStringLiteral("/disconnect all"), QStringLiteral("断开所有天空端设备")},
        {QStringLiteral("/reconnect all"), QStringLiteral("重连所有已启用的天空端设备")},
        {QStringLiteral("/record start"), QStringLiteral("开始天空端本地 session 记录")},
        {QStringLiteral("/record pause"), QStringLiteral("暂停天空端本地 session 记录")},
        {QStringLiteral("/record stop"), QStringLiteral("停止天空端本地 session 记录")},
        {QStringLiteral("/waveform on"), QStringLiteral("开启降采样二次谐波波形下传")},
        {QStringLiteral("/waveform off"), QStringLiteral("关闭降采样二次谐波波形下传")},
        {QStringLiteral("/waveform once"), QStringLiteral("立即发送一帧降采样波形")},
        {QStringLiteral("/config show"), QStringLiteral("显示当前 sky_config JSON 配置")},
        {QStringLiteral("/clear"), QStringLiteral("清空当前可视日志")},
        {QStringLiteral("/quit"), QStringLiteral("安全停止天空端并退出 VaporViewSky")},
        {QStringLiteral("/exit"), QStringLiteral("安全停止天空端并退出 VaporViewSky")},
    };
}

QStringList SkyTuiController::helpLines() const
{
    return {
        QStringLiteral("命令列表："),
        QStringLiteral("  help, h, /help                 # 显示帮助"),
        QStringLiteral("  status, s, /status             # 查看天空端运行状态"),
        QStringLiteral("  devices, /devices              # 查看设备状态"),
        QStringLiteral("  connect <device>               # 连接设备：epsilon/ptb/hmp/lidar/wave/all"),
        QStringLiteral("  disconnect <device>            # 断开设备"),
        QStringLiteral("  reconnect <device>             # 重连设备"),
        QStringLiteral("  record start|pause|stop        # 控制天空端本地记录"),
        QStringLiteral("  waveform on|off|once           # 控制二次谐波波形下传"),
        QStringLiteral("  config show, cfg               # 显示当前配置 JSON"),
        QStringLiteral("  clear, logs, palette, theme dark # 日志、命令面板和主题辅助命令"),
        QStringLiteral("  quit, exit, stop, /quit, /exit # 安全退出天空端"),
        QStringLiteral("快捷键：Enter 执行，Ctrl+P 或 / 打开命令面板，Tab 切换焦点，Left/Right 切日志/状态，Esc 关闭，Ctrl+L 清屏。"),
    };
}

bool SkyTuiController::parseDeviceName(const QString& name, SkyDeviceId& id) const
{
    const QString key = name.toLower();
    if (key == QStringLiteral("epsilon"))
    {
        id = SkyDeviceId::Epsilon;
    }
    else if (key == QStringLiteral("ptb"))
    {
        id = SkyDeviceId::Ptb;
    }
    else if (key == QStringLiteral("hmp"))
    {
        id = SkyDeviceId::Hmp;
    }
    else if (key == QStringLiteral("lidar"))
    {
        id = SkyDeviceId::Lidar;
    }
    else if (key == QStringLiteral("wave") || key == QStringLiteral("wavetcp") || key == QStringLiteral("tcp"))
    {
        id = SkyDeviceId::WaveTcp;
    }
    else if (key == QStringLiteral("all"))
    {
        id = SkyDeviceId::All;
    }
    else
    {
        return false;
    }
    return true;
}

SkyTuiCommandResult SkyTuiController::handleDeviceCommand(const QString& action, const QString& deviceName)
{
    SkyTuiCommandResult result;
    if (!runtime_)
    {
        result.messages << QStringLiteral("SkyRuntime 不可用。");
        return result;
    }

    SkyDeviceId id = SkyDeviceId::All;
    if (!parseDeviceName(deviceName, id))
    {
        result.messages << QStringLiteral("设备名无效。可用：epsilon、ptb、hmp、lidar、wave、all。");
        return result;
    }

    if (id == SkyDeviceId::All)
    {
        if (action == QStringLiteral("connect")) runtime_->connectAllDevices();
        if (action == QStringLiteral("disconnect")) runtime_->disconnectAllDevices();
        if (action == QStringLiteral("reconnect")) runtime_->reconnectAllDevices();
        result.messages << QStringLiteral("%1 all：成功").arg(action);
        return result;
    }

    CommandErrorCode error = CommandErrorCode::Ok;
    bool ok = false;
    if (action == QStringLiteral("connect")) ok = runtime_->connectDevice(id, &error);
    if (action == QStringLiteral("disconnect")) ok = runtime_->disconnectDevice(id, &error);
    if (action == QStringLiteral("reconnect")) ok = runtime_->reconnectDevice(id, &error);

    result.messages << QStringLiteral("%1 %2: %3")
                           .arg(action, skyDeviceIdName(id), ok ? QStringLiteral("成功") : commandErrorText(error));
    return result;
}

SkyTuiCommandResult SkyTuiController::handleRecordCommand(const QString& action)
{
    SkyTuiCommandResult result;
    if (!runtime_)
    {
        result.messages << QStringLiteral("SkyRuntime 不可用。");
        return result;
    }

    QString error;
    bool ok = false;
    if (action == QStringLiteral("start"))
    {
        ok = runtime_->startRecording(&error);
    }
    else if (action == QStringLiteral("pause"))
    {
        ok = runtime_->pauseRecording(&error);
    }
    else if (action == QStringLiteral("stop"))
    {
        ok = runtime_->stopRecording(&error);
    }
    else
    {
        result.messages << QStringLiteral("用法：record start|pause|stop");
        return result;
    }

    result.messages << QStringLiteral("record %1：%2").arg(action, ok ? QStringLiteral("成功") : error);
    return result;
}

SkyTuiCommandResult SkyTuiController::handleWaveformCommand(const QString& action)
{
    SkyTuiCommandResult result;
    if (!runtime_)
    {
        result.messages << QStringLiteral("SkyRuntime 不可用。");
        return result;
    }

    if (action == QStringLiteral("on"))
    {
        runtime_->setWaveformStreamingEnabled(true);
        result.messages << QStringLiteral("波形下传：开启");
    }
    else if (action == QStringLiteral("off"))
    {
        runtime_->setWaveformStreamingEnabled(false);
        result.messages << QStringLiteral("波形下传：关闭");
    }
    else if (action == QStringLiteral("once"))
    {
        runtime_->sendOneWaveformNow();
        result.messages << QStringLiteral("已请求立即发送一帧波形，如当前有数据则会发送。");
    }
    else
    {
        result.messages << QStringLiteral("用法：waveform on|off|once");
    }
    return result;
}

QStringList SkyTuiController::statusLines() const
{
    if (!runtime_)
    {
        return {QStringLiteral("SkyRuntime 不可用。")};
    }
    const TelemetryStatus status = runtime_->currentStatus();
    return {
        QStringLiteral("运行中：%1").arg(yesNo(runtime_->isRunning())),
        QStringLiteral("数传串口：%1").arg(options_.telemetry_port),
        QStringLiteral("数传波特率：%1").arg(options_.telemetry_baud),
        QStringLiteral("记录状态：%1").arg(recordingStateText(status.recording_state)),
        QStringLiteral("Session：%1").arg(status.session_name.isEmpty() ? QStringLiteral("-") : status.session_name),
        QStringLiteral("剩余磁盘：%1 bytes").arg(status.disk_free_bytes),
        QStringLiteral("基础遥测频率：%1 Hz").arg(status.telemetry_basic_rate_hz),
        QStringLiteral("特征值频率：%1 Hz").arg(status.feature_rate_hz),
        QStringLiteral("波形频率：%1 Hz").arg(status.waveform_rate_hz),
        QStringLiteral("心跳频率：%1 Hz").arg(status.heartbeat_rate_hz),
        QStringLiteral("状态频率：%1 Hz").arg(status.status_rate_hz),
        QStringLiteral("波形下传：%1").arg(runtime_->waveformStreamingEnabled() ? QStringLiteral("开启") : QStringLiteral("关闭")),
        QStringLiteral("接收帧数：%1").arg(status.rx_total_frames),
        QStringLiteral("CRC 错误：%1").arg(status.crc_error_count),
    };
}

QStringList SkyTuiController::deviceLines() const
{
    if (!runtime_)
    {
        return {QStringLiteral("SkyRuntime 不可用。")};
    }
    QStringList lines;
    const TelemetryStatus status = runtime_->currentStatus();
    for (const DeviceStatusItem& item : status.devices)
    {
        lines << QStringLiteral("%1：%2  接收=%3  错误=%4  最近数据(us)=%5  错误码=%6")
                     .arg(skyDeviceIdName(item.device_id),
                          deviceStateName(item.state))
                     .arg(item.rx_count)
                     .arg(item.error_count)
                     .arg(item.last_data_time_us)
                     .arg(item.error_code);
    }
    return lines.isEmpty() ? QStringList{QStringLiteral("暂无设备状态。")} : lines;
}

QStringList SkyTuiController::configLines() const
{
    if (!runtime_)
    {
        return {QStringLiteral("SkyRuntime 不可用。")};
    }
    QStringList lines;
    lines << QStringLiteral("天空端配置 SkyConfig：");
    lines << jsonLines(runtime_->currentConfig().toJson());
    return lines;
}

QString SkyTuiController::recordingStateText(quint8 state) const
{
    switch (state)
    {
    case 1:
        return QStringLiteral("记录中");
    case 2:
        return QStringLiteral("已暂停");
    default:
        return QStringLiteral("未记录");
    }
}

QString SkyTuiController::commandErrorText(CommandErrorCode error) const
{
    switch (error)
    {
    case CommandErrorCode::Ok:
        return QStringLiteral("成功");
    case CommandErrorCode::DeviceAlreadyConnected:
        return QStringLiteral("设备已连接");
    case CommandErrorCode::DeviceNotConnected:
        return QStringLiteral("设备未连接");
    case CommandErrorCode::DeviceConnectFailed:
        return QStringLiteral("设备连接失败");
    case CommandErrorCode::DeviceDisconnectFailed:
        return QStringLiteral("设备断开失败");
    case CommandErrorCode::DeviceReconnectFailed:
        return QStringLiteral("设备重连失败");
    case CommandErrorCode::InvalidDeviceId:
        return QStringLiteral("设备 ID 无效");
    case CommandErrorCode::InvalidPayload:
        return QStringLiteral("载荷无效");
    case CommandErrorCode::ConfigInvalid:
        return QStringLiteral("配置无效");
    case CommandErrorCode::ConfigApplyFailed:
        return QStringLiteral("配置应用失败");
    case CommandErrorCode::ConfigSaveFailed:
        return QStringLiteral("配置保存失败");
    case CommandErrorCode::RecordingAlreadyStarted:
        return QStringLiteral("记录已经开始");
    case CommandErrorCode::RecordingNotStarted:
        return QStringLiteral("记录尚未开始");
    default:
        return QStringLiteral("错误 %1").arg(static_cast<quint32>(error));
    }
}

}  // namespace VaporView
