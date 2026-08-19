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

QString deviceStateText(DeviceState state)
{
    switch (state)
    {
    case DeviceState::Disabled:
        return QStringLiteral("已禁用");
    case DeviceState::Disconnected:
        return QStringLiteral("未连接");
    case DeviceState::Connecting:
        return QStringLiteral("连接中");
    case DeviceState::Connected:
        return QStringLiteral("已连接");
    case DeviceState::Error:
        return QStringLiteral("错误");
    case DeviceState::Reconnecting:
        return QStringLiteral("重连中");
    }
    return QStringLiteral("未知");
}

QString deviceDisplayName(SkyDeviceId id)
{
    switch (id)
    {
    case SkyDeviceId::Epsilon:
        return QStringLiteral("EPSILON");
    case SkyDeviceId::Ptb:
        return QStringLiteral("PTB");
    case SkyDeviceId::Hmp:
        return QStringLiteral("HMP");
    case SkyDeviceId::Lidar:
        return QStringLiteral("Lidar");
    case SkyDeviceId::TemperatureController:
        return QStringLiteral("激光温控");
    case SkyDeviceId::Ai8TemperatureController:
        return QStringLiteral("系统温控");
    case SkyDeviceId::WaveTcp:
        return QStringLiteral("Wave TCP");
    case SkyDeviceId::All:
        return QStringLiteral("all");
    }
    return QStringLiteral("未知设备");
}

}  // namespace

SkyTuiController::SkyTuiController(SkyLocalIpcClient *client, const SkyTuiOptions& options, QObject *parent)
    : QObject(parent)
    , client_(client)
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
        if (tokens.value(1).toLower() == QStringLiteral("overview"))
        {
            result.messages << QStringLiteral("输入 /device overview 打开设备总览页面。");
        }
        else
        {
            result.messages = deviceLines();
        }
    }
    else if (command == QStringLiteral("quit") ||
             command == QStringLiteral("exit") ||
             command == QStringLiteral("stop"))
    {
        result.type = SkyTuiCommandResult::Type::Quit;
    }
    else if ((command == QStringLiteral("core") &&
              (tokens.value(1).toLower() == QStringLiteral("stop") ||
               tokens.value(1).toLower() == QStringLiteral("shutdown") ||
               tokens.value(1).toLower() == QStringLiteral("exit") ||
               tokens.value(1).toLower() == QStringLiteral("quit"))) ||
             command == QStringLiteral("shutdown-core"))
    {
        if (!client_ || !client_->isConnected())
        {
            result.messages << QStringLiteral("SkyCore IPC 未连接。");
            return result;
        }
        const quint16 seq = client_->requestCoreShutdown();
        result.messages << QStringLiteral("SkyCore 退出请求：%1").arg(seq != 0 ? QStringLiteral("已发送") : QStringLiteral("发送失败"));
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
        {QStringLiteral("/exit"), QStringLiteral("退出 TUI，SkyCore 继续运行")},
        {QStringLiteral("/core stop"), QStringLiteral("请求 SkyCore 正常退出")},
        {QStringLiteral("/device overview"), QStringLiteral("打开设备总览")},
        {QStringLiteral("/overview"), QStringLiteral("打开设备总览")},
        {QStringLiteral("/home"), QStringLiteral("返回天空端首页")},
        {QStringLiteral("/status"), QStringLiteral("查看天空端运行状态、记录状态和链路统计")},
        {QStringLiteral("/devices"), QStringLiteral("查看 EPSILON/PTB/HMP/Lidar/激光温控/系统温控/Wave TCP 设备状态")},
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
        {QStringLiteral("/connect rd105"), QStringLiteral("请求天空端连接激光温控")},
        {QStringLiteral("/disconnect rd105"), QStringLiteral("请求天空端断开激光温控")},
        {QStringLiteral("/reconnect rd105"), QStringLiteral("请求天空端重连激光温控")},
        {QStringLiteral("/connect ai8"), QStringLiteral("请求天空端连接系统温控")},
        {QStringLiteral("/disconnect ai8"), QStringLiteral("请求天空端断开系统温控")},
        {QStringLiteral("/reconnect ai8"), QStringLiteral("请求天空端重连系统温控")},
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
        {QStringLiteral("/quit"), QStringLiteral("退出 TUI，SkyCore 继续运行")},
    };
}

QStringList SkyTuiController::helpLines() const
{
    return {
        QStringLiteral("命令列表："),
        QStringLiteral("  help, h, /help                 # 显示帮助"),
        QStringLiteral("  status, s, /status             # 查看天空端运行状态"),
        QStringLiteral("  devices, /devices              # 查看设备状态"),
        QStringLiteral("  /device overview, /overview    # 打开设备总览页面"),
        QStringLiteral("  /home                          # 返回首页"),
        QStringLiteral("  connect <device>               # 连接设备：epsilon/ptb/hmp/lidar/rd105/ai8/wave/all"),
        QStringLiteral("  disconnect <device>            # 断开设备"),
        QStringLiteral("  reconnect <device>             # 重连设备"),
        QStringLiteral("  record start|pause|stop        # 控制天空端本地记录"),
        QStringLiteral("  waveform on|off|once           # 控制二次谐波波形下传"),
        QStringLiteral("  config show, cfg               # 显示当前配置 JSON"),
        QStringLiteral("  clear, logs, palette, theme dark # 日志、命令面板和主题辅助命令"),
        QStringLiteral("  quit, exit, stop, /quit, /exit # 退出 TUI，SkyCore 继续运行"),
        QStringLiteral("  core stop, /core stop          # 请求 SkyCore 正常退出"),
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
    else if (key == QStringLiteral("temperature") ||
             key == QStringLiteral("temperature_controller") ||
             key == QStringLiteral("laser_temperature") ||
             key == QStringLiteral("laser_temperature_controller") ||
             key == QStringLiteral("激光温控") ||
             key == QStringLiteral("temp") ||
             key == QStringLiteral("rd105"))
    {
        id = SkyDeviceId::TemperatureController;
    }
    else if (key == QStringLiteral("ai8") ||
             key == QStringLiteral("ai-8") ||
             key == QStringLiteral("ai8288") ||
             key == QStringLiteral("ai-8288") ||
             key == QStringLiteral("system_temperature") ||
             key == QStringLiteral("system_temperature_controller") ||
             key == QStringLiteral("系统温控") ||
             key == QStringLiteral("ai8_temperature_controller"))
    {
        id = SkyDeviceId::Ai8TemperatureController;
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

#ifdef VAPORVIEW_SKY_TUI_TESTING
bool SkyTuiController::parseDeviceNameForTest(const QString& name, SkyDeviceId& id) const
{
    return parseDeviceName(name, id);
}
#endif

SkyTuiCommandResult SkyTuiController::handleDeviceCommand(const QString& action, const QString& deviceName)
{
    SkyTuiCommandResult result;
    if (!client_ || !client_->isConnected())
    {
        result.messages << QStringLiteral("SkyCore IPC 未连接。");
        return result;
    }

    SkyDeviceId id = SkyDeviceId::All;
    if (!parseDeviceName(deviceName, id))
    {
        result.messages << QStringLiteral("设备名无效。可用：epsilon、ptb、hmp、lidar、rd105(激光温控)、ai8(系统温控)、wave、all。");
        return result;
    }

    if (id == SkyDeviceId::All)
    {
        if (action == QStringLiteral("connect")) client_->connectAllDevices();
        if (action == QStringLiteral("disconnect")) client_->disconnectAllDevices();
        if (action == QStringLiteral("reconnect")) client_->reconnectAllDevices();
        result.messages << QStringLiteral("%1 all：已发送").arg(action);
        return result;
    }

    quint16 seq = 0;
    if (action == QStringLiteral("connect")) seq = client_->connectDevice(id);
    if (action == QStringLiteral("disconnect")) seq = client_->disconnectDevice(id);
    if (action == QStringLiteral("reconnect")) seq = client_->reconnectDevice(id);

    result.messages << QStringLiteral("%1 %2: %3")
                           .arg(action, deviceDisplayName(id), seq != 0 ? QStringLiteral("已发送") : QStringLiteral("发送失败"));
    return result;
}

SkyTuiCommandResult SkyTuiController::handleRecordCommand(const QString& action)
{
    SkyTuiCommandResult result;
    if (!client_ || !client_->isConnected())
    {
        result.messages << QStringLiteral("SkyCore IPC 未连接。");
        return result;
    }

    quint16 seq = 0;
    if (action == QStringLiteral("start"))
    {
        seq = client_->startRecording();
    }
    else if (action == QStringLiteral("pause"))
    {
        seq = client_->pauseRecording();
    }
    else if (action == QStringLiteral("stop"))
    {
        seq = client_->stopRecording();
    }
    else
    {
        result.messages << QStringLiteral("用法：record start|pause|stop");
        return result;
    }

    result.messages << QStringLiteral("record %1：%2").arg(action, seq != 0 ? QStringLiteral("已发送") : QStringLiteral("发送失败"));
    return result;
}

SkyTuiCommandResult SkyTuiController::handleWaveformCommand(const QString& action)
{
    SkyTuiCommandResult result;
    if (!client_ || !client_->isConnected())
    {
        result.messages << QStringLiteral("SkyCore IPC 未连接。");
        return result;
    }

    if (action == QStringLiteral("on"))
    {
        client_->enableWaveformStreaming();
        result.messages << QStringLiteral("波形下传：已请求开启");
    }
    else if (action == QStringLiteral("off"))
    {
        client_->disableWaveformStreaming();
        result.messages << QStringLiteral("波形下传：已请求关闭");
    }
    else if (action == QStringLiteral("once"))
    {
        client_->requestOneWaveform();
        result.messages << QStringLiteral("已请求 SkyCore 立即发送一帧波形，如当前有数据则会发送。");
    }
    else
    {
        result.messages << QStringLiteral("用法：waveform on|off|once");
    }
    return result;
}

QStringList SkyTuiController::statusLines() const
{
    if (!client_)
    {
        return {QStringLiteral("SkyCore IPC 客户端不可用。")};
    }
    const TelemetryStatus status = client_->currentStatus();
    return {
        QStringLiteral("IPC 连接：%1").arg(yesNo(client_->isConnected())),
        QStringLiteral("IPC 地址：%1:%2").arg(options_.ipc_host).arg(options_.ipc_port),
        QStringLiteral("记录状态：%1").arg(recordingStateText(status.recording_state)),
        QStringLiteral("会话：%1").arg(status.session_name.isEmpty() ? QStringLiteral("-") : status.session_name),
        QStringLiteral("剩余磁盘：%1 B").arg(status.disk_free_bytes),
        QStringLiteral("基础遥测频率：%1 Hz").arg(status.telemetry_basic_rate_hz),
        QStringLiteral("特征值频率：%1 Hz").arg(status.feature_rate_hz),
        QStringLiteral("波形频率：%1 Hz").arg(status.waveform_rate_hz),
        QStringLiteral("Wave TCP 实际频率：%1 Hz").arg(status.wave_tcp_actual_rate_hz, 0, 'f', 1),
        QStringLiteral("心跳频率：%1 Hz").arg(status.heartbeat_rate_hz),
        QStringLiteral("状态频率：%1 Hz").arg(status.status_rate_hz),
        QStringLiteral("波形下传：%1").arg(client_->waveformStreamingEnabled() ? QStringLiteral("开") : QStringLiteral("关")),
        QStringLiteral("接收帧数：%1").arg(status.rx_total_frames),
        QStringLiteral("CRC 错误：%1").arg(status.crc_error_count),
    };
}

QStringList SkyTuiController::deviceLines() const
{
    if (!client_)
    {
        return {QStringLiteral("SkyCore IPC 客户端不可用。")};
    }
    QStringList lines;
    const TelemetryStatus status = client_->currentStatus();
    for (const DeviceStatusItem& item : status.devices)
    {
        lines << QStringLiteral("%1：%2  接收=%3  错误=%4  最近数据(us)=%5  错误码=%6")
                     .arg(deviceDisplayName(item.device_id),
                          deviceStateText(item.state))
                     .arg(item.rx_count)
                     .arg(item.error_count)
                     .arg(item.last_data_time_us)
                     .arg(item.error_code);
    }
    return lines.isEmpty() ? QStringList{QStringLiteral("暂无设备状态。")} : lines;
}

QStringList SkyTuiController::configLines() const
{
    if (!client_)
    {
        return {QStringLiteral("SkyCore IPC 客户端不可用。")};
    }
    client_->getConfig();
    QStringList lines;
    lines << QStringLiteral("天空端配置 SkyConfig：");
    lines << jsonLines(client_->currentConfig().toJson());
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
