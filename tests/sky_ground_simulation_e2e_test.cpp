#include "SkyConfig.h"
#include "TelemetryCodec.h"
#include "ground/devices/RemoteSkyController.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonDocument>
#include <QProcess>
#include <QProcessEnvironment>
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

QString vaporViewExecutablePath()
{
    QString fileName = QStringLiteral("VaporView");
#ifdef Q_OS_WIN
    fileName += QStringLiteral(".exe");
#endif
    const QString path = QCoreApplication::applicationDirPath() + QLatin1Char('/') + fileName;
    require(QFileInfo::exists(path), "VaporView executable exists beside test binary");
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
    qRegisterMetaType<VaporView::DeviceOperationResponse>("VaporView::DeviceOperationResponse");
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
    QHash<quint32, VaporView::DeviceOperationResponse> ai8Responses;

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
    QObject::connect(&controller, &VaporView::Ground::Devices::RemoteSkyController::deviceOperationResponseReceived,
                     [&](const VaporView::DeviceOperationResponse& response) {
                         ai8Responses.insert(response.request_id, response);
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

    VaporView::EpsilonPacketRatesOperation epsilonPacketRates;
    epsilonPacketRates.output_rate_hz = 100;
    epsilonPacketRates.callback_rate_hz = 250;
    epsilonPacketRates.packet_rates = {{0x40, 250}, {0x50, 100}, {0x5C, 10}};
    epsilonPacketRates.packet_rate_signature = QStringLiteral("40=250;50=100;5C=10");
    quint32 requestId = controller.configureEpsilonPacketRates(epsilonPacketRates);
    require(requestId != 0 &&
                waitUntil([&]() { return ai8Responses.contains(requestId); }, &process),
            "EPSILON remote packet-rate operation returns response");
    require(ai8Responses.value(requestId).error_code == VaporView::CommandErrorCode::Ok,
            "EPSILON remote packet-rate operation succeeds");
    require(controller.deviceOperationSupport() ==
                VaporView::Ground::Devices::DeviceOperationSupport::Supported,
            "EPSILON remote operation capability is learned from the response");

    VaporView::EpsilonMainAntennaLeverArmOperation leverArm;
    leverArm.x_m = 1.25;
    leverArm.y_m = -0.5;
    leverArm.z_m = 0.75;
    requestId = controller.configureEpsilonMainAntennaLeverArm(leverArm);
    require(requestId != 0 &&
                waitUntil([&]() { return ai8Responses.contains(requestId); }, &process),
            "EPSILON remote lever-arm operation returns response");
    require(ai8Responses.value(requestId).error_code == VaporView::CommandErrorCode::Ok,
            "EPSILON remote lever-arm operation succeeds");

    VaporView::EpsilonRtcmInputOperation rtcmInput;
    rtcmInput.device_port_index = 3;
    rtcmInput.forward_port = QStringLiteral("/dev/e2e-rtcm");
    rtcmInput.forward_baud = 230400;
    requestId = controller.configureEpsilonRtcmInput(rtcmInput);
    require(requestId != 0 &&
                waitUntil([&]() { return ai8Responses.contains(requestId); }, &process),
            "EPSILON remote RTCM input operation returns response");
    require(ai8Responses.value(requestId).error_code == VaporView::CommandErrorCode::Ok,
            "EPSILON remote RTCM input operation succeeds");

    const QByteArray rtcmBytes = QByteArray::fromHex("D30000123456");
    require(controller.sendRtcmCorrectionData(rtcmBytes),
            "Ground sends RTCM correction data over the Sky link");
    require(waitUntil([&]() {
                return lastStatus.rtcm_correction_bytes_received >=
                           static_cast<quint64>(rtcmBytes.size()) &&
                       lastStatus.rtcm_correction_chunks_received > 0 &&
                       lastStatus.rtcm_correction_last_receive_time_us > 0;
            }, &process, 10000),
            "SkyCore receives RTCM correction data over the telemetry link");

    VaporView::Ai8TemperatureControllerProtocol::Selection ai8Selection;
    ai8Selection.channel = 3;
    ai8Selection.inputGroup = 2;
    ai8Selection.outputGroup = 2;
    requestId = controller.readAi8Page(
        VaporView::Ai8TemperatureControllerProtocol::Page::Channel, ai8Selection);
    require(requestId != 0 &&
                waitUntil([&]() { return ai8Responses.contains(requestId); }, &process),
            "AI-8 remote page read returns response");
    require(ai8Responses.value(requestId).error_code == VaporView::CommandErrorCode::Ok,
            "AI-8 remote page read succeeds");
    require(controller.deviceOperationSupport() ==
                VaporView::Ground::Devices::DeviceOperationSupport::Supported,
            "AI-8 remote operation capability is learned from the response");
    VaporView::Ai8TemperatureControllerProtocol::PageData ai8Page;
    require(VaporView::TelemetryCodec::parseAi8PageData(
                ai8Responses.value(requestId).payload, ai8Page) &&
                std::fabs(ai8Page.channel.setpointC - 25.0) < 0.001,
            "AI-8 remote page read returns simulation defaults");

    ai8Page.channel.setpointC = 36.5;
    requestId = controller.writeAi8Page(ai8Page);
    require(requestId != 0 &&
                waitUntil([&]() { return ai8Responses.contains(requestId); }, &process),
            "AI-8 remote page write returns response");
    VaporView::Ai8TemperatureControllerProtocol::PageData confirmedAi8Page;
    require(ai8Responses.value(requestId).error_code == VaporView::CommandErrorCode::Ok &&
                VaporView::TelemetryCodec::parseAi8PageData(
                    ai8Responses.value(requestId).payload, confirmedAi8Page) &&
                std::fabs(confirmedAi8Page.channel.setpointC - 36.5) < 0.001,
            "AI-8 remote page write is confirmed by read-back");

    requestId = controller.readAi8Page(
        VaporView::Ai8TemperatureControllerProtocol::Page::Channel, ai8Selection);
    require(requestId != 0 &&
                waitUntil([&]() { return ai8Responses.contains(requestId); }, &process),
            "AI-8 remote read-back returns response");
    require(VaporView::TelemetryCodec::parseAi8PageData(
                ai8Responses.value(requestId).payload, confirmedAi8Page) &&
                std::fabs(confirmedAi8Page.channel.setpointC - 36.5) < 0.001,
            "AI-8 remote read-back persists written value");

    requestId = controller.readAi8Page(
        VaporView::Ai8TemperatureControllerProtocol::Page::InputGroup, ai8Selection);
    require(requestId != 0 &&
                waitUntil([&]() { return ai8Responses.contains(requestId); }, &process),
            "AI-8 remote input-group read returns response");
    VaporView::Ai8TemperatureControllerProtocol::PageData inputPage;
    require(ai8Responses.value(requestId).error_code == VaporView::CommandErrorCode::Ok &&
                VaporView::TelemetryCodec::parseAi8PageData(
                    ai8Responses.value(requestId).payload, inputPage),
            "AI-8 remote input-group read succeeds");
    inputPage.input.filter = 7;
    requestId = controller.writeAi8Page(inputPage);
    require(requestId != 0 &&
                waitUntil([&]() { return ai8Responses.contains(requestId); }, &process),
            "AI-8 remote input-group write returns response");
    require(ai8Responses.value(requestId).error_code == VaporView::CommandErrorCode::Ok &&
                VaporView::TelemetryCodec::parseAi8PageData(
                    ai8Responses.value(requestId).payload, confirmedAi8Page) &&
                confirmedAi8Page.input.filter == 7,
            "AI-8 remote input-group write is confirmed by read-back");

    requestId = controller.readAi8Page(
        VaporView::Ai8TemperatureControllerProtocol::Page::OutputGroup, ai8Selection);
    require(requestId != 0 &&
                waitUntil([&]() { return ai8Responses.contains(requestId); }, &process),
            "AI-8 remote output-group read returns response");
    VaporView::Ai8TemperatureControllerProtocol::PageData outputPage;
    require(ai8Responses.value(requestId).error_code == VaporView::CommandErrorCode::Ok &&
                VaporView::TelemetryCodec::parseAi8PageData(
                    ai8Responses.value(requestId).payload, outputPage),
            "AI-8 remote output-group read succeeds");
    outputPage.output.outputHighPercent = 101;
    requestId = controller.writeAi8Page(outputPage);
    require(requestId != 0 &&
                waitUntil([&]() { return ai8Responses.contains(requestId); }, &process),
            "AI-8 remote output-group write returns response");
    require(ai8Responses.value(requestId).error_code == VaporView::CommandErrorCode::Ok &&
                VaporView::TelemetryCodec::parseAi8PageData(
                    ai8Responses.value(requestId).payload, confirmedAi8Page) &&
                confirmedAi8Page.output.outputHighPercent == 101,
            "AI-8 remote output-group write is confirmed by read-back");

    requestId = controller.readAi8Page(
        VaporView::Ai8TemperatureControllerProtocol::Page::Global, ai8Selection);
    require(requestId != 0 &&
                waitUntil([&]() { return ai8Responses.contains(requestId); }, &process),
            "AI-8 remote global read returns response");
    VaporView::Ai8TemperatureControllerProtocol::PageData globalPage;
    require(ai8Responses.value(requestId).error_code == VaporView::CommandErrorCode::Ok &&
                VaporView::TelemetryCodec::parseAi8PageData(
                    ai8Responses.value(requestId).payload, globalPage),
            "AI-8 remote global read succeeds");
    globalPage.global.address = 7;
    requestId = controller.writeAi8Page(globalPage);
    require(requestId != 0 &&
                waitUntil([&]() { return ai8Responses.contains(requestId); }, &process),
            "AI-8 remote global write returns response");
    require(ai8Responses.value(requestId).error_code == VaporView::CommandErrorCode::Ok &&
                VaporView::TelemetryCodec::parseAi8PageData(
                    ai8Responses.value(requestId).payload, confirmedAi8Page) &&
                confirmedAi8Page.global.address == 7,
            "AI-8 remote global write is confirmed by read-back");

    auto invalidAi8Page = ai8Page;
    invalidAi8Page.output.outputHighPercent = 106;
    requestId = controller.writeAi8Page(invalidAi8Page);
    require(requestId != 0 &&
                waitUntil([&]() { return ai8Responses.contains(requestId); }, &process),
            "AI-8 invalid remote write returns response");
    require(ai8Responses.value(requestId).error_code == VaporView::CommandErrorCode::InvalidPayload,
            "AI-8 invalid remote write is rejected");

    requestId = controller.restoreAi8FactoryDefaults(
        VaporView::Ai8TemperatureControllerProtocol::Page::Channel, ai8Selection);
    require(requestId != 0 &&
                waitUntil([&]() { return ai8Responses.contains(requestId); }, &process),
            "AI-8 simulation factory reset returns response");
    require(ai8Responses.value(requestId).error_code == VaporView::CommandErrorCode::Ok &&
                VaporView::TelemetryCodec::parseAi8PageData(
                    ai8Responses.value(requestId).payload, confirmedAi8Page) &&
                std::fabs(confirmedAi8Page.channel.setpointC - 25.0) < 0.001,
            "AI-8 simulation factory reset restores defaults");

    controller.close();
    QTemporaryDir groundUiTempDir;
    require(groundUiTempDir.isValid(), "temporary Ground UI E2E directory");
    const QString groundUiResultPath = groundUiTempDir.filePath(QStringLiteral("remote_device_ui_e2e.txt"));
    QProcess groundUi;
    groundUi.setProgram(vaporViewExecutablePath());
    groundUi.setArguments({
        QStringLiteral("--source"), QStringLiteral("remote"),
        QStringLiteral("--telemetry-transport"), QStringLiteral("tcp"),
        QStringLiteral("--telemetry-host"), QStringLiteral("127.0.0.1"),
        QStringLiteral("--telemetry-tcp-port"), QString::number(port),
        QStringLiteral("--remote-device-e2e-output"), groundUiResultPath,
    });
    QProcessEnvironment groundUiEnvironment = QProcessEnvironment::systemEnvironment();
    groundUiEnvironment.insert(QStringLiteral("VAPORVIEW_SETTINGS_DIR"), groundUiTempDir.path());
    groundUi.setProcessEnvironment(groundUiEnvironment);
    groundUi.setProcessChannelMode(QProcess::MergedChannels);
    groundUi.start();
    require(groundUi.waitForStarted(5000), "VaporView Ground UI E2E process starts");
    require(waitUntil([&]() {
                return QFileInfo::exists(groundUiResultPath) ||
                       groundUi.state() == QProcess::NotRunning;
            }, &groundUi, 55000),
            "VaporView Ground UI E2E process writes a result");
    if (!QFileInfo::exists(groundUiResultPath))
    {
        const QByteArray diagnostics = groundUi.readAllStandardOutput();
        std::cerr << "VaporView Ground UI E2E output:\n"
                  << diagnostics.constData() << '\n';
    }
    if (groundUi.state() != QProcess::NotRunning)
    {
        groundUi.terminate();
        groundUi.waitForFinished(5000);
    }
    if (groundUi.state() != QProcess::NotRunning)
    {
        groundUi.kill();
        groundUi.waitForFinished(5000);
    }
    QFile groundUiResult(groundUiResultPath);
    require(groundUiResult.open(QIODevice::ReadOnly), "VaporView Ground UI E2E result opens");
    const QByteArray groundUiResultText = groundUiResult.readAll();
    if (!groundUiResultText.startsWith("PASS\n"))
    {
        std::cerr << "VaporView Ground UI E2E result:\n"
                  << groundUiResultText.constData() << '\n';
        const QByteArray diagnostics = groundUi.readAllStandardOutput();
        if (!diagnostics.isEmpty())
        {
            std::cerr << "VaporView Ground UI E2E output:\n"
                      << diagnostics.constData() << '\n';
        }
    }
    require(groundUiResultText.startsWith("PASS\n"),
            "VaporView Ground UI E2E remote EPSILON, RTCM, RD105, and AI-8 paths pass");

    linkOpen = false;
    require(waitUntil([&]() {
                if (!linkOpen)
                {
                    controller.openTcp(QStringLiteral("127.0.0.1"), port);
                }
                return linkOpen;
            }, &process),
            "Ground backend reconnects after the real VaporView UI E2E");

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
    const QJsonObject restartedEpsilon =
        receivedConfig.value(QStringLiteral("epsilon")).toObject();
    const QJsonObject restartedRtcm =
        restartedEpsilon.value(QStringLiteral("rtcm")).toObject();
    require(restartedRtcm.value(QStringLiteral("enabled")).toBool() &&
                restartedRtcm.value(QStringLiteral("device_port_index")).toInt() == 3 &&
                restartedRtcm.value(QStringLiteral("forward_port")).toString() ==
                    QStringLiteral("/dev/ui-e2e-rtcm") &&
                restartedRtcm.value(QStringLiteral("baud")).toInt() == 230400,
            "Restarted SkyCore reloads saved EPSILON RTCM config");
    require(acks.contains(seq) && acks.value(seq).error_code == VaporView::CommandErrorCode::Ok,
            "restart GetSkyConfig acknowledged");

    controller.close();
    stopSkyCore(process);
    require(process.state() == QProcess::NotRunning, "SkyCore process stopped");

    std::cout << "sky_ground_simulation_e2e_test passed\n";
    return 0;
}
