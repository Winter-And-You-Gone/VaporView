#include "SkyLocalIpcServer.h"
#include "SkyRuntime.h"
#include "LogService.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QTextStream>
#include <QTimer>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <csignal>
#include <iostream>
#include <string>
#include <thread>

#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace
{
std::atomic_bool g_shutdownRequested = false;

void configureConsoleEncoding()
{
#ifdef Q_OS_WIN
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
}

void handleProcessSignal(int)
{
    g_shutdownRequested.store(true, std::memory_order_relaxed);
}

bool isShutdownInput(std::string line)
{
    line.erase(line.begin(), std::find_if(line.begin(), line.end(), [](unsigned char ch) {
        return !std::isspace(ch);
    }));
    line.erase(std::find_if(line.rbegin(), line.rend(), [](unsigned char ch) {
        return !std::isspace(ch);
    }).base(), line.end());
    std::transform(line.begin(), line.end(), line.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return line == "quit" ||
           line == "exit" ||
           line == "stop" ||
           line == "/quit" ||
           line == "/exit";
}

void startStdinShutdownWatcher()
{
    std::thread([]() {
        std::string line;
        while (std::getline(std::cin, line))
        {
            if (isShutdownInput(line))
            {
                g_shutdownRequested.store(true, std::memory_order_relaxed);
                return;
            }
        }
    }).detach();
}

#ifdef Q_OS_WIN
BOOL WINAPI handleConsoleControl(DWORD controlType)
{
    switch (controlType)
    {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
    case CTRL_CLOSE_EVENT:
    case CTRL_LOGOFF_EVENT:
    case CTRL_SHUTDOWN_EVENT:
        g_shutdownRequested.store(true, std::memory_order_relaxed);
        return TRUE;
    default:
        return FALSE;
    }
}
#endif

void registerTelemetryMetaTypes()
{
    qRegisterMetaType<VaporView::TelemetryBasic>("VaporView::TelemetryBasic");
    qRegisterMetaType<VaporView::DownsampledWaveform>("VaporView::DownsampledWaveform");
    qRegisterMetaType<VaporView::WaveformFeature>("VaporView::WaveformFeature");
    qRegisterMetaType<VaporView::DeviceStatusItem>("VaporView::DeviceStatusItem");
    qRegisterMetaType<VaporView::TelemetryStatus>("VaporView::TelemetryStatus");
    qRegisterMetaType<VaporView::CommandAck>("VaporView::CommandAck");
    qRegisterMetaType<VaporView::CommandId>("VaporView::CommandId");
    qRegisterMetaType<VaporView::SkyDeviceId>("VaporView::SkyDeviceId");
}

}  // namespace

int main(int argc, char *argv[])
{
    configureConsoleEncoding();

    QCoreApplication app(argc, argv);
    std::signal(SIGINT, handleProcessSignal);
    std::signal(SIGTERM, handleProcessSignal);
#ifdef SIGBREAK
    std::signal(SIGBREAK, handleProcessSignal);
#endif
#ifdef Q_OS_WIN
    SetConsoleCtrlHandler(handleConsoleControl, TRUE);
#endif

    app.setApplicationName("VaporViewSkyCore");
    app.setApplicationVersion("1.0.22");
    app.setOrganizationName("VaporView");
    VaporView::LogService logService(QStringLiteral("VaporViewSkyCore"));
    logService.installQtMessageHandler();
    registerTelemetryMetaTypes();

    QCommandLineParser parser;
    parser.setApplicationDescription("VaporView SkyCore");

    QCommandLineOption helpOption(QStringList{QStringLiteral("h"), QStringLiteral("help")}, QStringLiteral("Displays help on commandline options."));
    QCommandLineOption versionOption(QStringLiteral("version"), QStringLiteral("Displays version information."));
    QCommandLineOption telemetryPortOption(QStringLiteral("telemetry-port"), QStringLiteral("Telemetry serial port"), QStringLiteral("port"));
    QCommandLineOption telemetryBaudOption(QStringLiteral("telemetry-baud"), QStringLiteral("Telemetry serial baud"), QStringLiteral("baud"), QStringLiteral("921600"));
    QCommandLineOption telemetryTransportOption(QStringLiteral("telemetry-transport"), QStringLiteral("Telemetry transport: tcp or serial"), QStringLiteral("transport"), QStringLiteral("tcp"));
    QCommandLineOption telemetryHostOption(QStringLiteral("telemetry-host"), QStringLiteral("Telemetry TCP listen host"), QStringLiteral("host"), QStringLiteral("0.0.0.0"));
    QCommandLineOption telemetryTcpPortOption(QStringLiteral("telemetry-tcp-port"), QStringLiteral("Telemetry TCP listen port"), QStringLiteral("port"), QStringLiteral("39100"));
    QCommandLineOption skyConfigOption(QStringLiteral("sky-config"), QStringLiteral("Sky config JSON path"), QStringLiteral("path"));
    QCommandLineOption skySimulateOption(QStringLiteral("sky-simulate-data"), QStringLiteral("Generate simulated sky data"));
    QCommandLineOption skyWaveHostOption(QStringLiteral("sky-wave-host"), QStringLiteral("Sky TCP wave host"), QStringLiteral("host"), QStringLiteral("127.0.0.1"));
    QCommandLineOption skyWavePortOption(QStringLiteral("sky-wave-port"), QStringLiteral("Sky TCP wave port"), QStringLiteral("port"), QStringLiteral("8888"));
    QCommandLineOption ipcHostOption(QStringLiteral("ipc-host"), QStringLiteral("Local IPC host"), QStringLiteral("host"), QStringLiteral("127.0.0.1"));
    QCommandLineOption ipcPortOption(QStringLiteral("ipc-port"), QStringLiteral("Local IPC port"), QStringLiteral("port"), QStringLiteral("39001"));
    QCommandLineOption profileOption(QStringLiteral("profile"), QStringLiteral("Runtime profile: flight or debug"), QStringLiteral("profile"), QStringLiteral("debug"));
    QCommandLineOption noIpcOption(QStringLiteral("no-ipc"), QStringLiteral("Disable local IPC server"));
    parser.addOptions({helpOption, versionOption, telemetryPortOption, telemetryBaudOption,
                       telemetryTransportOption, telemetryHostOption, telemetryTcpPortOption,
                       skyConfigOption, skySimulateOption,
                       skyWaveHostOption, skyWavePortOption, ipcHostOption, ipcPortOption,
                       profileOption, noIpcOption});
    parser.process(app);

    if (parser.isSet(helpOption))
    {
        QTextStream(stdout) << parser.helpText();
        return 0;
    }
    if (parser.isSet(versionOption))
    {
        QTextStream(stdout) << app.applicationName() << " " << app.applicationVersion() << "\n";
        return 0;
    }

    VaporView::SkyRuntimeOptions options;
    VaporView::TelemetryTransportType transport = VaporView::TelemetryTransportType::Tcp;
    if (!VaporView::parseTelemetryTransport(parser.value(telemetryTransportOption), transport))
    {
        QTextStream(stderr) << "--telemetry-transport 必须为 tcp 或 serial。\n";
        return 2;
    }
    if (!parser.isSet(telemetryTransportOption) && parser.isSet(telemetryPortOption))
    {
        transport = VaporView::TelemetryTransportType::Serial;
        QTextStream(stdout) << "兼容模式：未指定 --telemetry-transport 时，--telemetry-port 将选择串口遥测。\n";
    }
    options.telemetry_transport = transport;
    options.telemetry_host = parser.value(telemetryHostOption);
    options.telemetry_tcp_port = parser.value(telemetryTcpPortOption).toInt();
    options.telemetry_port = parser.value(telemetryPortOption);
    options.telemetry_baud = parser.value(telemetryBaudOption).toInt();
    options.config_path = parser.value(skyConfigOption);
    options.simulate_data = parser.isSet(skySimulateOption);
    options.wave_host = parser.value(skyWaveHostOption);
    options.wave_port = parser.value(skyWavePortOption).toInt();

    if (options.telemetry_transport == VaporView::TelemetryTransportType::Serial &&
        options.telemetry_port.trimmed().isEmpty())
    {
        QTextStream(stderr) << "串口遥测必须指定 --telemetry-port。\n";
        return 2;
    }
    if (options.telemetry_transport == VaporView::TelemetryTransportType::Tcp &&
        (options.telemetry_tcp_port <= 0 || options.telemetry_tcp_port > 65535))
    {
        QTextStream(stderr) << "--telemetry-tcp-port 必须为 1 到 65535。\n";
        return 2;
    }

    QTimer shutdownTimer;
    shutdownTimer.setInterval(100);
    QObject::connect(&shutdownTimer, &QTimer::timeout, &app, [&app]() {
        if (g_shutdownRequested.exchange(false, std::memory_order_relaxed))
        {
            QTextStream(stdout) << "已收到 SkyCore 停止请求。\n";
            QCoreApplication::quit();
        }
    });
    shutdownTimer.start();
    startStdinShutdownWatcher();

    VaporView::SkyRuntime runtime(options);
    QObject::connect(&runtime, &VaporView::SkyRuntime::logRecord, [](const VaporView::LogRecord& record) {
        QTextStream(stdout) << record.message << "\n";
    });
    QObject::connect(&app, &QCoreApplication::aboutToQuit, [&runtime]() {
        QTextStream(stdout) << "正在停止 SkyCore 运行时。\n";
        runtime.stop();
        QTextStream(stdout) << "SkyCore 运行时已停止。\n";
    });

    VaporView::SkyLocalIpcServer ipcServer(&runtime);
    QObject::connect(&ipcServer, &VaporView::SkyLocalIpcServer::logRecordGenerated,
                     [](const VaporView::LogRecord& record) {
        QTextStream(stdout) << record.message << "\n";
    });

    if (!parser.isSet(noIpcOption))
    {
        if (!ipcServer.listen(parser.value(ipcHostOption), static_cast<quint16>(parser.value(ipcPortOption).toUShort())))
        {
            return 4;
        }
    }

    QTextStream(stdout) << "SkyCore 运行配置：" << parser.value(profileOption) << "\n";
    QTextStream(stdout) << "SkyCore 退出方式：按 Ctrl+C/Ctrl+Break、输入 quit，或从 SkyTui 发送 /core stop。\n";
    if (!runtime.start())
    {
        return 3;
    }

    return app.exec();
}
