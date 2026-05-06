#include "SkyTuiController.h"

#include <QJsonDocument>

namespace VaporView
{
namespace
{
QString yesNo(bool value)
{
    return value ? QStringLiteral("yes") : QStringLiteral("no");
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
        result.messages << QStringLiteral("Event stream is already focused. Use PageUp/PageDown to scroll.");
    }
    else if (command == QStringLiteral("palette"))
    {
        result.messages << QStringLiteral("Press Ctrl+P, Tab, or type / to open the command palette.");
    }
    else if (command == QStringLiteral("theme") && tokens.value(1).toLower() == QStringLiteral("dark"))
    {
        result.messages << QStringLiteral("Dark terminal theme is active.");
    }
    else
    {
        result.messages << QStringLiteral("Unknown command: %1").arg(normalized)
                        << QStringLiteral("Type /help for available commands.");
    }

    return result;
}

QList<SkyTuiCommandItem> SkyTuiController::commandPalette() const
{
    return {
        {QStringLiteral("/help"), QStringLiteral("Show command help")},
        {QStringLiteral("/status"), QStringLiteral("Print sky runtime status")},
        {QStringLiteral("/devices"), QStringLiteral("Print device states")},
        {QStringLiteral("/connect epsilon"), QStringLiteral("Connect EPSILON on the sky host")},
        {QStringLiteral("/disconnect epsilon"), QStringLiteral("Disconnect EPSILON")},
        {QStringLiteral("/reconnect epsilon"), QStringLiteral("Reconnect EPSILON")},
        {QStringLiteral("/connect ptb"), QStringLiteral("Connect PTB")},
        {QStringLiteral("/disconnect ptb"), QStringLiteral("Disconnect PTB")},
        {QStringLiteral("/reconnect ptb"), QStringLiteral("Reconnect PTB")},
        {QStringLiteral("/connect hmp"), QStringLiteral("Connect HMP")},
        {QStringLiteral("/disconnect hmp"), QStringLiteral("Disconnect HMP")},
        {QStringLiteral("/reconnect hmp"), QStringLiteral("Reconnect HMP")},
        {QStringLiteral("/connect lidar"), QStringLiteral("Connect Lidar")},
        {QStringLiteral("/disconnect lidar"), QStringLiteral("Disconnect Lidar")},
        {QStringLiteral("/reconnect lidar"), QStringLiteral("Reconnect Lidar")},
        {QStringLiteral("/connect wave"), QStringLiteral("Connect Wave TCP source")},
        {QStringLiteral("/disconnect wave"), QStringLiteral("Disconnect Wave TCP source")},
        {QStringLiteral("/reconnect wave"), QStringLiteral("Reconnect Wave TCP source")},
        {QStringLiteral("/connect all"), QStringLiteral("Connect all enabled sky devices")},
        {QStringLiteral("/disconnect all"), QStringLiteral("Disconnect all sky devices")},
        {QStringLiteral("/reconnect all"), QStringLiteral("Reconnect all sky devices")},
        {QStringLiteral("/record start"), QStringLiteral("Start sky session recording")},
        {QStringLiteral("/record pause"), QStringLiteral("Pause sky session recording")},
        {QStringLiteral("/record stop"), QStringLiteral("Stop sky session recording")},
        {QStringLiteral("/waveform on"), QStringLiteral("Enable downsampled waveform streaming")},
        {QStringLiteral("/waveform off"), QStringLiteral("Disable waveform streaming")},
        {QStringLiteral("/waveform once"), QStringLiteral("Send one waveform frame now")},
        {QStringLiteral("/config show"), QStringLiteral("Show current sky_config JSON")},
        {QStringLiteral("/clear"), QStringLiteral("Clear visible log buffer")},
        {QStringLiteral("/quit"), QStringLiteral("Stop runtime and exit VaporViewSky")},
    };
}

QStringList SkyTuiController::helpLines() const
{
    return {
        QStringLiteral("Commands:"),
        QStringLiteral("  help, h, /help"),
        QStringLiteral("  status, s, /status"),
        QStringLiteral("  devices, /devices"),
        QStringLiteral("  connect|disconnect|reconnect epsilon|ptb|hmp|lidar|wave|all"),
        QStringLiteral("  record start|pause|stop"),
        QStringLiteral("  waveform on|off|once"),
        QStringLiteral("  config show, cfg"),
        QStringLiteral("  clear, logs, palette, theme dark"),
        QStringLiteral("  quit, exit, stop, /quit"),
        QStringLiteral("Keys: Enter execute, Ctrl+P palette, Tab commands, Esc close, Ctrl+L clear, q quit when input is empty."),
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
        result.messages << QStringLiteral("SkyRuntime is unavailable.");
        return result;
    }

    SkyDeviceId id = SkyDeviceId::All;
    if (!parseDeviceName(deviceName, id))
    {
        result.messages << QStringLiteral("Invalid device. Use epsilon, ptb, hmp, lidar, wave, or all.");
        return result;
    }

    if (id == SkyDeviceId::All)
    {
        if (action == QStringLiteral("connect")) runtime_->connectAllDevices();
        if (action == QStringLiteral("disconnect")) runtime_->disconnectAllDevices();
        if (action == QStringLiteral("reconnect")) runtime_->reconnectAllDevices();
        result.messages << QStringLiteral("%1 all: ok").arg(action);
        return result;
    }

    CommandErrorCode error = CommandErrorCode::Ok;
    bool ok = false;
    if (action == QStringLiteral("connect")) ok = runtime_->connectDevice(id, &error);
    if (action == QStringLiteral("disconnect")) ok = runtime_->disconnectDevice(id, &error);
    if (action == QStringLiteral("reconnect")) ok = runtime_->reconnectDevice(id, &error);

    result.messages << QStringLiteral("%1 %2: %3")
                           .arg(action, skyDeviceIdName(id), ok ? QStringLiteral("ok") : commandErrorText(error));
    return result;
}

SkyTuiCommandResult SkyTuiController::handleRecordCommand(const QString& action)
{
    SkyTuiCommandResult result;
    if (!runtime_)
    {
        result.messages << QStringLiteral("SkyRuntime is unavailable.");
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
        result.messages << QStringLiteral("Usage: record start|pause|stop");
        return result;
    }

    result.messages << QStringLiteral("record %1: %2").arg(action, ok ? QStringLiteral("ok") : error);
    return result;
}

SkyTuiCommandResult SkyTuiController::handleWaveformCommand(const QString& action)
{
    SkyTuiCommandResult result;
    if (!runtime_)
    {
        result.messages << QStringLiteral("SkyRuntime is unavailable.");
        return result;
    }

    if (action == QStringLiteral("on"))
    {
        runtime_->setWaveformStreamingEnabled(true);
        result.messages << QStringLiteral("waveform streaming: on");
    }
    else if (action == QStringLiteral("off"))
    {
        runtime_->setWaveformStreamingEnabled(false);
        result.messages << QStringLiteral("waveform streaming: off");
    }
    else if (action == QStringLiteral("once"))
    {
        runtime_->sendOneWaveformNow();
        result.messages << QStringLiteral("waveform once: sent if data is available");
    }
    else
    {
        result.messages << QStringLiteral("Usage: waveform on|off|once");
    }
    return result;
}

QStringList SkyTuiController::statusLines() const
{
    if (!runtime_)
    {
        return {QStringLiteral("SkyRuntime is unavailable.")};
    }
    const TelemetryStatus status = runtime_->currentStatus();
    return {
        QStringLiteral("Running: %1").arg(yesNo(runtime_->isRunning())),
        QStringLiteral("Telemetry port: %1").arg(options_.telemetry_port),
        QStringLiteral("Telemetry baud: %1").arg(options_.telemetry_baud),
        QStringLiteral("Recording state: %1").arg(recordingStateText(status.recording_state)),
        QStringLiteral("Session name: %1").arg(status.session_name.isEmpty() ? QStringLiteral("-") : status.session_name),
        QStringLiteral("Disk free: %1 bytes").arg(status.disk_free_bytes),
        QStringLiteral("Basic telemetry rate: %1 Hz").arg(status.telemetry_basic_rate_hz),
        QStringLiteral("Feature rate: %1 Hz").arg(status.feature_rate_hz),
        QStringLiteral("Waveform rate: %1 Hz").arg(status.waveform_rate_hz),
        QStringLiteral("Heartbeat rate: %1 Hz").arg(status.heartbeat_rate_hz),
        QStringLiteral("Status rate: %1 Hz").arg(status.status_rate_hz),
        QStringLiteral("Waveform streaming: %1").arg(runtime_->waveformStreamingEnabled() ? QStringLiteral("on") : QStringLiteral("off")),
        QStringLiteral("RX total frames: %1").arg(status.rx_total_frames),
        QStringLiteral("CRC error count: %1").arg(status.crc_error_count),
    };
}

QStringList SkyTuiController::deviceLines() const
{
    if (!runtime_)
    {
        return {QStringLiteral("SkyRuntime is unavailable.")};
    }
    QStringList lines;
    const TelemetryStatus status = runtime_->currentStatus();
    for (const DeviceStatusItem& item : status.devices)
    {
        lines << QStringLiteral("%1: %2 rx=%3 errors=%4 last_us=%5 error_code=%6")
                     .arg(skyDeviceIdName(item.device_id),
                          deviceStateName(item.state))
                     .arg(item.rx_count)
                     .arg(item.error_count)
                     .arg(item.last_data_time_us)
                     .arg(item.error_code);
    }
    return lines.isEmpty() ? QStringList{QStringLiteral("No device status available yet.")} : lines;
}

QStringList SkyTuiController::configLines() const
{
    if (!runtime_)
    {
        return {QStringLiteral("SkyRuntime is unavailable.")};
    }
    QStringList lines;
    lines << QStringLiteral("SkyConfig:");
    lines << jsonLines(runtime_->currentConfig().toJson());
    return lines;
}

QString SkyTuiController::recordingStateText(quint8 state) const
{
    switch (state)
    {
    case 1:
        return QStringLiteral("recording");
    case 2:
        return QStringLiteral("paused");
    default:
        return QStringLiteral("off");
    }
}

QString SkyTuiController::commandErrorText(CommandErrorCode error) const
{
    switch (error)
    {
    case CommandErrorCode::Ok:
        return QStringLiteral("ok");
    case CommandErrorCode::DeviceAlreadyConnected:
        return QStringLiteral("device already connected");
    case CommandErrorCode::DeviceNotConnected:
        return QStringLiteral("device not connected");
    case CommandErrorCode::DeviceConnectFailed:
        return QStringLiteral("device connect failed");
    case CommandErrorCode::DeviceDisconnectFailed:
        return QStringLiteral("device disconnect failed");
    case CommandErrorCode::DeviceReconnectFailed:
        return QStringLiteral("device reconnect failed");
    case CommandErrorCode::InvalidDeviceId:
        return QStringLiteral("invalid device id");
    case CommandErrorCode::InvalidPayload:
        return QStringLiteral("invalid payload");
    case CommandErrorCode::ConfigInvalid:
        return QStringLiteral("config invalid");
    case CommandErrorCode::ConfigApplyFailed:
        return QStringLiteral("config apply failed");
    case CommandErrorCode::ConfigSaveFailed:
        return QStringLiteral("config save failed");
    case CommandErrorCode::RecordingAlreadyStarted:
        return QStringLiteral("recording already started");
    case CommandErrorCode::RecordingNotStarted:
        return QStringLiteral("recording not started");
    default:
        return QStringLiteral("error %1").arg(static_cast<quint32>(error));
    }
}

}  // namespace VaporView
