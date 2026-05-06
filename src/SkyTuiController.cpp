#include "SkyTuiController.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QJsonDocument>
#include <QPointer>
#include <QTextStream>
#include <iostream>

namespace VaporView
{
namespace
{
QTextStream& out()
{
    static QTextStream stream(stdout);
    return stream;
}

QString yesNo(bool value)
{
    return value ? QStringLiteral("yes") : QStringLiteral("no");
}

}  // namespace

SkyTuiController::SkyTuiController(SkyRuntime *runtime, const SkyRuntimeOptions& options, QObject *parent)
    : QObject(parent)
    , runtime_(runtime)
    , options_(options)
    , input_running_(std::make_shared<std::atomic_bool>(false))
{
}

SkyTuiController::~SkyTuiController()
{
    input_running_->store(false);
}

void SkyTuiController::start()
{
    if (started_)
    {
        return;
    }
    started_ = true;
    input_running_->store(true);
    printBanner();
    const QStringList pendingLogs = pending_logs_;
    pending_logs_.clear();
    for (const QString& message : pendingLogs)
    {
        appendLog(message);
    }
    if (pendingLogs.isEmpty())
    {
        printPrompt();
    }

    QPointer<SkyTuiController> self(this);
    const std::shared_ptr<std::atomic_bool> running = input_running_;
    input_thread_ = std::thread([self, running]() {
        std::string line;
        while (running->load() && std::getline(std::cin, line))
        {
            if (!self)
            {
                break;
            }
            const QString command = QString::fromStdString(line).trimmed();
            QMetaObject::invokeMethod(self.data(), [self, command]() {
                if (self)
                {
                    self->handleCommand(command);
                }
            }, Qt::QueuedConnection);
        }
    });
    input_thread_.detach();
}

void SkyTuiController::appendLog(const QString& message)
{
    if (!started_)
    {
        pending_logs_.push_back(message);
        return;
    }
    out() << "\n[" << QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss")) << "] "
          << message << "\n";
    printPrompt();
}

void SkyTuiController::handleCommand(const QString& line)
{
    const QString normalized = line.simplified();
    if (normalized.isEmpty())
    {
        printPrompt();
        return;
    }

    const QStringList tokens = normalized.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    const QString command = tokens.value(0).toLower();

    if (command == QStringLiteral("help") || command == QStringLiteral("h"))
    {
        printHelp();
    }
    else if (command == QStringLiteral("status") || command == QStringLiteral("s"))
    {
        printStatus();
    }
    else if (command == QStringLiteral("devices"))
    {
        printDevices();
    }
    else if (command == QStringLiteral("quit") || command == QStringLiteral("exit") || command == QStringLiteral("stop"))
    {
        out() << "Stopping sky runtime...\n";
        input_running_->store(false);
        if (runtime_)
        {
            runtime_->stop();
        }
        out() << "Bye.\n";
        out().flush();
        QCoreApplication::quit();
        return;
    }
    else if (command == QStringLiteral("connect") ||
             command == QStringLiteral("disconnect") ||
             command == QStringLiteral("reconnect"))
    {
        handleDeviceCommand(command, tokens.value(1));
    }
    else if (command == QStringLiteral("record"))
    {
        handleRecordCommand(tokens.value(1).toLower());
    }
    else if (command == QStringLiteral("waveform"))
    {
        handleWaveformCommand(tokens.value(1).toLower());
    }
    else if (command == QStringLiteral("config") && tokens.value(1).toLower() == QStringLiteral("show"))
    {
        printConfig();
    }
    else if (command == QStringLiteral("cfg"))
    {
        printConfig();
    }
    else
    {
        out() << "Unknown command: " << normalized << "\n";
        out() << "Type 'help' for commands.\n";
    }

    printPrompt();
}

void SkyTuiController::printBanner()
{
    out() << "VaporView Sky Mode\n"
          << "Telemetry Port: " << options_.telemetry_port << "\n"
          << "Telemetry Baud: " << options_.telemetry_baud << "\n"
          << "Config Path: " << (options_.config_path.isEmpty() ? QStringLiteral("(default sky_config.json)") : options_.config_path) << "\n"
          << "Simulate Data: " << (options_.simulate_data ? QStringLiteral("true") : QStringLiteral("false")) << "\n"
          << "Wave TCP: " << options_.wave_host << ":" << options_.wave_port << "\n\n"
          << "Type 'help' for commands.\n\n";
    out().flush();
}

void SkyTuiController::printHelp()
{
    out() << "Commands:\n"
          << "  help, h\n"
          << "  status, s\n"
          << "  devices\n"
          << "  connect|disconnect|reconnect epsilon|ptb|hmp|lidar|wave|all\n"
          << "  record start|pause|stop\n"
          << "  waveform on|off|once\n"
          << "  config show, cfg\n"
          << "  quit, exit, stop\n";
}

void SkyTuiController::printStatus()
{
    if (!runtime_)
    {
        return;
    }
    const TelemetryStatus status = runtime_->currentStatus();
    out() << "Running: " << yesNo(runtime_->isRunning()) << "\n"
          << "Telemetry port: " << options_.telemetry_port << "\n"
          << "Telemetry baud: " << options_.telemetry_baud << "\n"
          << "Recording state: " << recordingStateText(status.recording_state) << "\n"
          << "Session name: " << (status.session_name.isEmpty() ? QStringLiteral("-") : status.session_name) << "\n"
          << "Disk free: " << status.disk_free_bytes << " bytes\n"
          << "Basic telemetry rate: " << status.telemetry_basic_rate_hz << " Hz\n"
          << "Feature rate: " << status.feature_rate_hz << " Hz\n"
          << "Waveform rate: " << status.waveform_rate_hz << " Hz\n"
          << "Heartbeat rate: " << status.heartbeat_rate_hz << " Hz\n"
          << "Status rate: " << status.status_rate_hz << " Hz\n"
          << "Waveform streaming: " << (runtime_->waveformStreamingEnabled() ? QStringLiteral("on") : QStringLiteral("off")) << "\n"
          << "RX total frames: " << status.rx_total_frames << "\n"
          << "CRC error count: " << status.crc_error_count << "\n";
}

void SkyTuiController::printDevices()
{
    if (!runtime_)
    {
        return;
    }
    const TelemetryStatus status = runtime_->currentStatus();
    for (const DeviceStatusItem& item : status.devices)
    {
        out() << skyDeviceIdName(item.device_id) << ": " << deviceStateName(item.state)
              << " rx=" << item.rx_count
              << " errors=" << item.error_count
              << " last_us=" << item.last_data_time_us
              << " error_code=" << item.error_code
              << "\n";
    }
}

void SkyTuiController::printConfig()
{
    if (!runtime_)
    {
        return;
    }
    out() << QJsonDocument(runtime_->currentConfig().toJson()).toJson(QJsonDocument::Indented);
}

void SkyTuiController::printPrompt()
{
    out() << "sky> ";
    out().flush();
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

void SkyTuiController::handleDeviceCommand(const QString& action, const QString& deviceName)
{
    if (!runtime_)
    {
        return;
    }
    SkyDeviceId id = SkyDeviceId::All;
    if (!parseDeviceName(deviceName, id))
    {
        out() << "Invalid device. Use epsilon, ptb, hmp, lidar, wave, or all.\n";
        return;
    }

    if (id == SkyDeviceId::All)
    {
        if (action == QStringLiteral("connect")) runtime_->connectAllDevices();
        if (action == QStringLiteral("disconnect")) runtime_->disconnectAllDevices();
        if (action == QStringLiteral("reconnect")) runtime_->reconnectAllDevices();
        out() << action << " all: ok\n";
        return;
    }

    CommandErrorCode error = CommandErrorCode::Ok;
    bool ok = false;
    if (action == QStringLiteral("connect")) ok = runtime_->connectDevice(id, &error);
    if (action == QStringLiteral("disconnect")) ok = runtime_->disconnectDevice(id, &error);
    if (action == QStringLiteral("reconnect")) ok = runtime_->reconnectDevice(id, &error);
    out() << action << " " << skyDeviceIdName(id) << ": "
          << (ok ? QStringLiteral("ok") : commandErrorText(error)) << "\n";
}

void SkyTuiController::handleRecordCommand(const QString& action)
{
    if (!runtime_)
    {
        return;
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
        out() << "Usage: record start|pause|stop\n";
        return;
    }
    out() << "record " << action << ": " << (ok ? QStringLiteral("ok") : error) << "\n";
}

void SkyTuiController::handleWaveformCommand(const QString& action)
{
    if (!runtime_)
    {
        return;
    }
    if (action == QStringLiteral("on"))
    {
        runtime_->setWaveformStreamingEnabled(true);
        out() << "waveform streaming: on\n";
    }
    else if (action == QStringLiteral("off"))
    {
        runtime_->setWaveformStreamingEnabled(false);
        out() << "waveform streaming: off\n";
    }
    else if (action == QStringLiteral("once"))
    {
        runtime_->sendOneWaveformNow();
        out() << "waveform once: sent if data is available\n";
    }
    else
    {
        out() << "Usage: waveform on|off|once\n";
    }
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
    default:
        return QStringLiteral("error %1").arg(static_cast<quint32>(error));
    }
}

}  // namespace VaporView
