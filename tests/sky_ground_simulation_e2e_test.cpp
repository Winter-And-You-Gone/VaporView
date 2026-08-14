#include "SkyConfig.h"
#include "TelemetryCodec.h"
#include "ground/devices/RemoteSkyController.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <QTcpServer>

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace
{

void require(bool condition, const char *message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

template <typename Predicate>
bool waitUntil(Predicate predicate, QProcess *process = nullptr, int timeoutMs = 8000)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        if (predicate())
        {
            return true;
        }
        if (process && process->state() == QProcess::NotRunning)
        {
            return predicate();
        }
    }
    return predicate();
}

quint16 reserveLocalTcpPort()
{
    QTcpServer server;
    require(server.listen(QHostAddress::LocalHost, 0), "reserve localhost TCP port");
    const quint16 port = server.serverPort();
    server.close();
    require(port > 0, "reserved localhost TCP port is valid");
    return port;
}

QString skyCoreExecutablePath()
{
    QString fileName = QStringLiteral("VaporViewSkyCore");
#ifdef Q_OS_WIN
    fileName += QStringLiteral(".exe");
#endif
    const QString path = QCoreApplication::applicationDirPath() + QLatin1Char('/') + fileName;
    require(QFileInfo::exists(path), "VaporViewSkyCore executable exists beside test binary");
    return path;
}

void stopSkyCore(QProcess& process)
{
    if (process.state() == QProcess::NotRunning)
    {
        return;
    }
    process.write("quit\n");
    process.closeWriteChannel();
    if (!process.waitForFinished(5000))
    {
        process.kill();
        process.waitForFinished(5000);
    }
}

void startSkyCore(QProcess& process, quint16 port, const QString& configPath)
{
    process.setProgram(skyCoreExecutablePath());
    process.setArguments({
        QStringLiteral("--sky-simulate-data"),
        QStringLiteral("--sky-config"), configPath,
        QStringLiteral("--telemetry-transport"), QStringLiteral("tcp"),
        QStringLiteral("--telemetry-host"), QStringLiteral("127.0.0.1"),
        QStringLiteral("--telemetry-tcp-port"), QString::number(port),
        QStringLiteral("--sky-wave-host"), QStringLiteral("127.0.0.1"),
        QStringLiteral("--sky-wave-port"), QStringLiteral("9"),
        QStringLiteral("--no-ipc"),
    });
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start();
    require(process.waitForStarted(5000), "SkyCore process starts");
}

VaporView::SkyConfig testConfig()
{
    VaporView::SkyConfig config = VaporView::SkyConfig::defaults();
    config.epsilon = {true, QStringLiteral("/dev/test-epsilon"), 921600, 100.0};
    config.ptb = {true, QStringLiteral("/dev/test-ptb210"), 9600, 20.0};
    config.ptb.source = QStringLiteral("ptb210");
    config.hmp = {true, QStringLiteral("/dev/test-hmp3"), 19200, 20.0};
    config.hmp.source = QStringLiteral("hmp3");
    config.lidar = {true, QStringLiteral("/dev/test-lidar"), 500000, 100.0};
    config.temperature_controller = {true, QStringLiteral("/dev/test-rd105"), 38400, 5.0, 1};
    config.ai8_temperature_controller = {true, QStringLiteral("/dev/test-ai8"), 19200, 5.0, 1};
    config.wave_tcp = {true, QStringLiteral("127.0.0.1"), 9, 10, 0, 0};
    config.telemetry = {10.0, 10.0, 2.0, 2.0, 2.0};
    return config;
}

bool hasConnectedDevice(const VaporView::TelemetryStatus& status, VaporView::SkyDeviceId id)
{
    for (const VaporView::DeviceStatusItem& item : status.devices)
    {
        if (item.device_id == id && item.state == VaporView::DeviceState::Connected &&
            item.rx_count > 0)
        {
            return true;
        }
    }
    return false;
}

}  // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    qRegisterMetaType<VaporView::TelemetryBasic>("VaporView::TelemetryBasic");
    qRegisterMetaType<VaporView::TelemetryStatus>("VaporView::TelemetryStatus");
    qRegisterMetaType<VaporView::TemperatureControllerData>("VaporView::TemperatureControllerData");
    qRegisterMetaType<VaporView::Ai8TemperatureControllerProtocol::LiveData>(
        "VaporView::Ai8TemperatureControllerProtocol::LiveData");
    qRegisterMetaType<VaporView::CommandAck>("VaporView::CommandAck");
    qRegisterMetaType<VaporView::CommandId>("VaporView::CommandId");

    QTemporaryDir tempDir;
    require(tempDir.isValid(), "temporary E2E directory");
    const QString configPath = tempDir.filePath(QStringLiteral("sky_config_e2e_test.json"));
    QString error;
    VaporView::SkyConfig config = testConfig();
    require(config.saveToFile(configPath, &error), "initial SkyConfig saved");

    const quint16 port = reserveLocalTcpPort();
    QProcess process;
    startSkyCore(process, port, configPath);

    VaporView::Ground::Devices::RemoteSkyController controller;
    bool linkOpen = false;
    QJsonObject receivedConfig;
    QJsonObject applyResult;
    VaporView::TelemetryStatus lastStatus;
    VaporView::TelemetryBasic lastBasic;
    VaporView::TemperatureControllerData lastRd105;
    VaporView::Ai8TemperatureControllerProtocol::LiveData lastAi8;
    int basicCount = 0;
    int rd105Count = 0;
    int ai8Count = 0;
    QHash<quint16, VaporView::CommandAck> acks;

    QObject::connect(&controller, &VaporView::Ground::Devices::RemoteSkyController::linkOpenChanged,
                     [&](bool open) { linkOpen = open; });
    QObject::connect(controller.telemetryService(), &VaporView::GroundTelemetryService::skyConfigReceived,
                     [&](const QJsonObject& object) { receivedConfig = object; });
    QObject::connect(controller.telemetryService(), &VaporView::GroundTelemetryService::skyConfigApplyResultReceived,
                     [&](const QJsonObject& object) { applyResult = object; });
    QObject::connect(&controller, &VaporView::Ground::Devices::RemoteSkyController::commandAckReceived,
                     [&](const VaporView::CommandAck& ack) { acks.insert(ack.command_seq, ack); });
    QObject::connect(&controller, &VaporView::Ground::Devices::RemoteSkyController::statusUpdated,
                     [&](const VaporView::TelemetryStatus& status) { lastStatus = status; });
    QObject::connect(&controller, &VaporView::Ground::Devices::RemoteSkyController::basicTelemetryUpdated,
                     [&](const VaporView::TelemetryBasic& telemetry) {
                         lastBasic = telemetry;
                         ++basicCount;
                     });
    QObject::connect(&controller, &VaporView::Ground::Devices::RemoteSkyController::temperatureControllerStatusUpdated,
                     [&](const VaporView::TemperatureControllerData& data) {
                         lastRd105 = data;
                         ++rd105Count;
                     });
    QObject::connect(&controller, &VaporView::Ground::Devices::RemoteSkyController::ai8TemperatureControllerStatusUpdated,
                     [&](const VaporView::Ai8TemperatureControllerProtocol::LiveData& data) {
                         lastAi8 = data;
                         ++ai8Count;
                     });

    require(waitUntil([&]() {
                if (!linkOpen)
                {
                    controller.openTcp(QStringLiteral("127.0.0.1"), port);
                }
                return linkOpen;
            }, &process),
            "Ground RemoteSkyController connects to SkyCore TCP");

    quint16 seq = controller.requestSkyConfig();
    require(waitUntil([&]() { return receivedConfig.contains(QStringLiteral("ai8_temperature_controller")); },
                      &process),
            "GetSkyConfig returns AI-8 section");
    require(receivedConfig.value(QStringLiteral("ptb")).toObject().value(QStringLiteral("source")).toString() ==
                QStringLiteral("ptb210"),
            "initial pressure source returned");
    require(receivedConfig.value(QStringLiteral("hmp")).toObject().value(QStringLiteral("source")).toString() ==
                QStringLiteral("hmp3"),
            "initial humidity source returned");
    require(acks.contains(seq) && acks.value(seq).error_code == VaporView::CommandErrorCode::Ok,
            "GetSkyConfig acknowledged");

    config.ptb.source = QStringLiteral("bmp390");
    config.ptb.port = QStringLiteral("/dev/test-bmp390");
    config.ptb.baud_rate = 115200;
    config.hmp.source = QStringLiteral("sht45");
    config.hmp.port = QStringLiteral("/dev/test-sht45");
    config.hmp.baud_rate = 115200;
    config.temperature_controller.port = QStringLiteral("/dev/test-rd105");
    config.temperature_controller.slave_address = 3;
    config.ai8_temperature_controller.port = QStringLiteral("/dev/test-ai8-updated");
    config.ai8_temperature_controller.baud_rate = 115200;
    config.ai8_temperature_controller.frequency_hz = 12.0;
    config.ai8_temperature_controller.slave_address = 7;

    seq = controller.telemetryService()->setSkyConfig(config.toJson());
    require(waitUntil([&]() {
                return acks.contains(seq) &&
                       acks.value(seq).error_code == VaporView::CommandErrorCode::Ok &&
                       !applyResult.isEmpty();
            }, &process),
            "SetSkyConfig apply succeeds over TCP");

    receivedConfig = QJsonObject();
    seq = controller.requestSkyConfig();
    require(waitUntil([&]() {
                return receivedConfig.value(QStringLiteral("ptb")).toObject().value(QStringLiteral("source")).toString() ==
                       QStringLiteral("bmp390");
            }, &process),
            "GetSkyConfig reflects BMP390 after apply");
    require(receivedConfig.value(QStringLiteral("hmp")).toObject().value(QStringLiteral("source")).toString() ==
                QStringLiteral("sht45"),
            "GetSkyConfig reflects SHT45 after apply");
    require(receivedConfig.value(QStringLiteral("ai8_temperature_controller")).toObject()
                .value(QStringLiteral("slave_address")).toInt() == 7,
            "GetSkyConfig reflects AI-8 slave address after apply");
    require(acks.contains(seq) && acks.value(seq).error_code == VaporView::CommandErrorCode::Ok,
            "post-apply GetSkyConfig acknowledged");

    require(waitUntil([&]() {
                return hasConnectedDevice(lastStatus, VaporView::SkyDeviceId::Epsilon) &&
                       hasConnectedDevice(lastStatus, VaporView::SkyDeviceId::Ptb) &&
                       hasConnectedDevice(lastStatus, VaporView::SkyDeviceId::Hmp) &&
                       hasConnectedDevice(lastStatus, VaporView::SkyDeviceId::Lidar) &&
                       hasConnectedDevice(lastStatus, VaporView::SkyDeviceId::TemperatureController) &&
                       hasConnectedDevice(lastStatus, VaporView::SkyDeviceId::Ai8TemperatureController) &&
                       hasConnectedDevice(lastStatus, VaporView::SkyDeviceId::WaveTcp);
            }, &process, 10000),
            "all formal Sky devices report connected simulated data");
    require(waitUntil([&]() {
                return basicCount > 0 &&
                       (lastBasic.validity_flags & VaporView::BasicHasPressure) &&
                       (lastBasic.validity_flags & VaporView::BasicHasHumidity) &&
                       rd105Count > 0 && lastRd105.valid &&
                       ai8Count > 0 && lastAi8.valid && lastAi8.controlStatesValid;
            }, &process, 10000),
            "Ground receives basic, RD105, and AI-8 telemetry");

    VaporView::TemperatureControllerCommand rd105Command;
    rd105Command.channel = 1;
    rd105Command.target_temperature_c = 31.0;
    seq = controller.sendCommand(
        VaporView::CommandId::SetTemperatureTarget,
        VaporView::TelemetryCodec::serializeTemperatureControllerCommand(rd105Command));
    require(waitUntil([&]() {
                return acks.contains(seq) &&
                       acks.value(seq).error_code == VaporView::CommandErrorCode::Ok;
            }, &process),
            "RD105 target command acknowledged");
    require(waitUntil([&]() {
                return lastRd105.valid &&
                       std::fabs(lastRd105.channels[0].target_temperature_c - 31.0) < 0.001;
            }, &process, 10000),
            "RD105 simulated telemetry reflects target command");

    seq = controller.telemetryService()->saveSkyConfig();
    require(waitUntil([&]() {
                return acks.contains(seq) &&
                       acks.value(seq).error_code == VaporView::CommandErrorCode::Ok;
            }, &process),
            "SaveSkyConfig acknowledged");

    controller.close();
    stopSkyCore(process);
    require(QFileInfo::exists(configPath), "saved SkyConfig file remains");

    startSkyCore(process, port, configPath);
    linkOpen = false;
    receivedConfig = QJsonObject();
    acks.clear();
    require(waitUntil([&]() {
                if (!linkOpen)
                {
                    controller.openTcp(QStringLiteral("127.0.0.1"), port);
                }
                return linkOpen;
            }, &process),
            "Ground reconnects after SkyCore restart");
    seq = controller.requestSkyConfig();
    require(waitUntil([&]() {
                return receivedConfig.value(QStringLiteral("ptb")).toObject().value(QStringLiteral("source")).toString() ==
                       QStringLiteral("bmp390");
            }, &process),
            "Restarted SkyCore reloads saved pressure source");
    require(receivedConfig.value(QStringLiteral("hmp")).toObject().value(QStringLiteral("source")).toString() ==
                QStringLiteral("sht45"),
            "Restarted SkyCore reloads saved humidity source");
    require(receivedConfig.value(QStringLiteral("ai8_temperature_controller")).toObject()
                .value(QStringLiteral("port")).toString() == QStringLiteral("/dev/test-ai8-updated"),
            "Restarted SkyCore reloads saved AI-8 config");
    require(acks.contains(seq) && acks.value(seq).error_code == VaporView::CommandErrorCode::Ok,
            "restart GetSkyConfig acknowledged");

    controller.close();
    stopSkyCore(process);
    require(process.state() == QProcess::NotRunning, "SkyCore process stopped");

    std::cout << "sky_ground_simulation_e2e_test passed\n";
    return 0;
}
