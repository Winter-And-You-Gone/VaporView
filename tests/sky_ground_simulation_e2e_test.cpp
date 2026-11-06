#include "SkyConfig.h"
#include "TelemetryCodec.h"
#include "LogService.h"
#include "IndependentCalibrationFixture.h"
#include "ground/devices/RemoteSkyController.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonDocument>
#include <QPointer>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <QSet>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUuid>

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace
{

bool unwindFailures = false;
struct LongPathUnavailable {};

void require(bool condition, const char *message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        if (unwindFailures) throw std::runtime_error(message);
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

void startSkyCore(QProcess& process, quint16 port, const QString& configPath, quint16 ipcPort)
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
        QStringLiteral("--ipc-port"), QString::number(ipcPort),
    });
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start();
    require(process.waitForStarted(5000), "SkyCore process starts");
}

VaporView::SkyConfig testConfig()
{
    VaporView::SkyConfig config = VaporView::SkyConfig::defaults();
    config.epsilon = {true, QStringLiteral("/dev/test-epsilon"), 921600};
    config.ptb = {true, QStringLiteral("/dev/test-ptb210"), 9600, 20.0};
    config.ptb.source = QStringLiteral("ptb210");
    config.hmp = {true, QStringLiteral("/dev/test-hmp3"), 19200, 20.0};
    config.hmp.source = QStringLiteral("hmp3");
    config.bmp390={true,QStringLiteral("/dev/test-internal-bmp"),115200,1.0,{}};
    config.sht45={true,QStringLiteral("/dev/test-internal-sht"),115200,1.0,{}};
    config.lidar = {true, QStringLiteral("/dev/test-lidar"), 500000, 100.0};
    config.temperature_controller = {true, QStringLiteral("/dev/test-rd105"), 38400, 5.0, 1};
    config.ai8_temperature_controller = {true, QStringLiteral("/dev/test-ai8"), 19200, 5.0, 1};
    config.wave_tcp = {true, QStringLiteral("127.0.0.1"), 9, 10, 0, 0};
    config.telemetry = {10.0, 10.0, 2.0, 2.0, 2.0};
    return config;
}

void fpgaCorrelationE2e(QVector<VaporView::LogRecord>& groundLogs, bool longSession = false)
{
    using namespace VaporView;
    QTemporaryDir isolated;
    require(isolated.isValid(), "isolated FPGA simulation directory");
    QString configDirectory = isolated.path();
    if (longSession) {
        for (int i = 0; i < 8; ++i)
            configDirectory = QDir(configDirectory).filePath(QString(180, QChar(0x4e2d)));
        if (!QDir().mkpath(configDirectory)) {
            std::cerr << "SKIP: isolated long Session directory cannot be created\n";
            throw LongPathUnavailable{};
        }
    }
    const QString configPath = QDir(configDirectory).filePath(QStringLiteral("sky.json"));
    auto config = SkyConfig::defaults();
    require(config.saveToFile(configPath), "save isolated FPGA simulator config");
    const quint16 port = reserveLocalTcpPort();
    quint16 ipcPort = reserveLocalTcpPort();
    while (ipcPort == port) ipcPort = reserveLocalTcpPort();
    QProcess sky;
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("LOCALAPPDATA"), isolated.path());
    environment.insert(QStringLiteral("VAPORVIEW_SETTINGS_DIR"), isolated.path());
    sky.setProcessEnvironment(environment);
    sky.setProgram(skyCoreExecutablePath());
    sky.setArguments({QStringLiteral("--fpga-simulate-data"), QStringLiteral("--sky-config"), configPath,
        QStringLiteral("--telemetry-transport"), QStringLiteral("tcp"),
        QStringLiteral("--telemetry-host"), QStringLiteral("127.0.0.1"),
        QStringLiteral("--telemetry-tcp-port"), QString::number(port),
        QStringLiteral("--sky-wave-host"), QStringLiteral("127.0.0.1"),
        QStringLiteral("--sky-wave-port"), QStringLiteral("9"),
        QStringLiteral("--ipc-port"), QString::number(ipcPort)});
    sky.setProcessChannelMode(QProcess::MergedChannels);
    sky.start();
    require(sky.waitForStarted(5000), "isolated FPGA simulator process starts");
    const quint64 skyPid = static_cast<quint64>(sky.processId());
    QVector<LogRecord> skyLogs;
    QHash<quint16, CommandAck> ipcAcks;
    QTcpSocket observer;
    TelemetryCodec observerCodec;
    QObject::connect(&observer, &QTcpSocket::readyRead, &observer, [&] {
        for (const auto& frame : observerCodec.feedBytes(observer.readAll())) {
            LogRecord record;
            CommandAck ack;
            if (frame.type == MsgType::LogEvent && TelemetryCodec::parseLogRecord(frame.payload, record))
                skyLogs.push_back(record);
            else if (frame.type == MsgType::CommandAck && TelemetryCodec::parseCommandAck(frame.payload, ack))
                ipcAcks.insert(ack.command_seq, ack);
        }
    });
    require(waitUntil([&] {
        if (observer.state() == QAbstractSocket::UnconnectedState)
            observer.connectToHost(QHostAddress::LocalHost, ipcPort);
        return observer.state() == QAbstractSocket::ConnectedState;
    }, &sky), "FPGA simulator IPC observer connects");
    // Forward both processes through a local, test-owned TCP proxy. Dropping
    // one command's ACK exercises real timeout logic without device hardware.
    QTcpServer proxy;
    require(proxy.listen(QHostAddress::LocalHost, 0), "isolated telemetry proxy listens");
    quint16 dropAckSequence = 0;
    quint16 dropQueryAckSequence = 0;
    quint16 dropConfigAckSequence = 0;
    quint16 legacyConfigAckSequence = 0;
    bool legacyConfigAckHadExpectation = false;
    bool reorderConfigContexts = false;
    bool wrongConfigContextInjected = false;
    QByteArray delayedConfigContext;
    quint32 dropContextRequest = 0;
    bool dropNextDeviceAck = false, deviceAckDropped = false, contextDropped = false;
    QPointer<QTcpSocket> proxyGroundPeer;
    QVector<DeviceOperationResponse> wireContextResults;
    int configV2Count = 0;
    int deviceV2Count = 0;
    QObject::connect(&proxy, &QTcpServer::newConnection, &proxy, [&] {
        auto *groundPeer = proxy.nextPendingConnection();
        proxyGroundPeer = groundPeer;
        auto *skyPeer = new QTcpSocket(&proxy);
        auto pendingBytes = std::make_shared<QByteArray>();
        auto decoder = std::make_shared<TelemetryCodec>();
        QObject::connect(groundPeer, &QTcpSocket::readyRead, &proxy, [groundPeer, skyPeer, pendingBytes] {
            const QByteArray bytes = groundPeer->readAll();
            if (skyPeer->state() == QAbstractSocket::ConnectedState) skyPeer->write(bytes);
            else pendingBytes->append(bytes);
        });
        QObject::connect(skyPeer, &QTcpSocket::connected, &proxy, [skyPeer, pendingBytes] {
            if (!pendingBytes->isEmpty()) { skyPeer->write(*pendingBytes); pendingBytes->clear(); }
        });
        QObject::connect(skyPeer, &QTcpSocket::readyRead, &proxy, [&, groundPeer, skyPeer, decoder] {
            TelemetryCodec encoder;
            for (const auto& frame : decoder->feedBytes(skyPeer->readAll())) {
                CommandAck ack;
                if (frame.type == MsgType::CommandAck && TelemetryCodec::parseCommandAck(frame.payload, ack) &&
                    dropNextDeviceAck && ack.command_id == CommandId::DeviceOperation) {
                    dropNextDeviceAck = false;
                    deviceAckDropped = true;
                    dropAckSequence = ack.command_seq;
                }
                if (frame.type == MsgType::CommandAck && TelemetryCodec::parseCommandAck(frame.payload, ack) &&
                    ((dropAckSequence != 0 && ack.command_seq == dropAckSequence) ||
                     (dropQueryAckSequence != 0 && ack.command_seq == dropQueryAckSequence) ||
                     (dropConfigAckSequence != 0 && ack.command_seq == dropConfigAckSequence))) continue;
                if (frame.type == MsgType::CommandAck && TelemetryCodec::parseCommandAck(frame.payload, ack) &&
                    legacyConfigAckSequence != 0 && ack.command_seq == legacyConfigAckSequence) {
                    legacyConfigAckHadExpectation = ack.config_result_context_expected.value_or(false);
                    ack.config_result_context_expected.reset();
                    groundPeer->write(encoder.encodeFrame(frame.type, TelemetryCodec::serializeCommandAck(ack),
                                                         frame.seq, frame.time_us, frame.flags));
                    continue;
                }
                if (frame.type == MsgType::SkyConfigApplyResultContext && reorderConfigContexts) {
                    const auto encoded = encoder.encodeFrame(frame.type, frame.payload, frame.seq, frame.time_us, frame.flags);
                    if (delayedConfigContext.isEmpty()) {
                        delayedConfigContext = encoded;
                        QJsonObject result; QString correlation, session; quint16 sequence = 0; bool extended = false;
                        require(TelemetryCodec::parseSkyConfigApplyResultContext(frame.payload, result, correlation, session, sequence, &extended),
                                "parse first configuration context");
                        if (extended) ++configV2Count;
                        const auto wrong = TelemetryCodec::serializeSkyConfigApplyResultContext(
                            result, QStringLiteral("wrong-operation-id"), session, sequence, extended);
                        groundPeer->write(encoder.encodeFrame(MsgType::SkyConfigApplyResultContext, wrong, frame.seq, frame.time_us, frame.flags));
                        wrongConfigContextInjected = true;
                    }
                    else {
                        QJsonObject result; QString correlation, session; quint16 sequence = 0; bool extended = false;
                        require(TelemetryCodec::parseSkyConfigApplyResultContext(frame.payload, result, correlation, session, sequence, &extended),
                                "parse second configuration context");
                        if (extended) ++configV2Count;
                        groundPeer->write(encoded);
                        groundPeer->write(delayedConfigContext);
                        delayedConfigContext.clear();
                        reorderConfigContexts = false;
                    }
                    continue;
                }
                if (frame.type == MsgType::DeviceOperationResponseContext) {
                    DeviceOperationResponse result; bool extended = false;
                    require(TelemetryCodec::parseDeviceOperationResponseContext(frame.payload, result, &extended),
                            "contextual device response on the wire is valid");
                    if (extended) ++deviceV2Count;
                    wireContextResults.push_back(result);
                    if (result.request_id == dropContextRequest) {
                        contextDropped = true;
                        continue;
                    }
                }
                groundPeer->write(encoder.encodeFrame(frame.type, frame.payload, frame.seq, frame.time_us, frame.flags));
            }
        });
        QObject::connect(groundPeer, &QTcpSocket::disconnected, skyPeer, &QTcpSocket::disconnectFromHost);
        skyPeer->connectToHost(QHostAddress::LocalHost, port);
    });
    Ground::Devices::RemoteSkyController controller;
    bool open = false, ready = false;
    QHash<quint32, DeviceOperationResponse> results;
    QObject::connect(&controller, &Ground::Devices::RemoteSkyController::linkOpenChanged,
                     [&](bool value) { open = value; });
    QObject::connect(&controller, &Ground::Devices::RemoteSkyController::fpgaStatusUpdated,
                     [&](const QJsonObject& status) { ready = status.value(QStringLiteral("ready")).toBool(); });
    QObject::connect(&controller, &Ground::Devices::RemoteSkyController::deviceOperationResponseReceived,
                     [&](const DeviceOperationResponse& response) { results.insert(response.request_id, response); });
    require(waitUntil([&] {
        if (!open) controller.openTcp(QStringLiteral("127.0.0.1"), proxy.serverPort());
        return open && ready;
    }, &sky, 15000), "isolated FPGA simulator connects and completes handshake");

    IndependentCalibrationTest::Rig calibration;
    const auto calibrationFile = calibration.complete();
    calibration.service->selectFile(calibrationFile.filename);
    calibration.service->synchronize();
    QJsonObject calibrationSnapshot;
    QString calibrationError;
    require(calibration.service->prepare(calibrationSnapshot, &calibrationError), qPrintable(calibrationError));
    calibrationSnapshot.remove(QStringLiteral("vcal_document"));
    QHash<quint16, CommandAck> commandAcks;
    QObject::connect(&controller, &Ground::Devices::RemoteSkyController::commandAckReceived,
                     [&](const CommandAck& ack) { commandAcks.insert(ack.command_seq, ack); });
    const quint16 startSequence = controller.sendCommand(CommandId::StartRecording,
        QJsonDocument(calibrationSnapshot).toJson(QJsonDocument::Compact));
    require(startSequence != 0 && waitUntil([&] { return commandAcks.contains(startSequence); }, &sky, 15000),
            "recording start returns ACK");
    const CommandAck startAck = commandAcks.value(startSequence);
    const QString sessionId = startAck.session_id;
    require(startAck.error_code == CommandErrorCode::Ok && !startAck.correlation_id.isEmpty() &&
            !sessionId.isEmpty() && QFileInfo(sessionId).isDir() &&
            sessionId.startsWith(QDir(configDirectory).filePath(QStringLiteral("simulated_sessions"))),
            "recording ACK identifies the real isolated session directory");
    if (longSession) {
        require(sessionId.toUtf8().size() > 4096 && startAck.extended_ack_context,
                "real long Session returns extended ACK");
        std::cout << "LONG_SESSION bytes=" << sessionId.toUtf8().size()
                  << " directory_exists=" << QFileInfo(sessionId).isDir() << '\n';
    }
    QJsonObject currentConfig;
    QVector<QJsonObject> legacyConfigResults;
    QObject::connect(controller.telemetryService(), &GroundTelemetryService::skyConfigReceived,
                     [&](const QJsonObject& value) { currentConfig = value; });
    QObject::connect(controller.telemetryService(), &GroundTelemetryService::skyConfigApplyResultReceived,
                     [&](const QJsonObject& value) { legacyConfigResults.push_back(value); });
    controller.requestSkyConfig();
    require(waitUntil([&] { return !currentConfig.isEmpty(); }, &sky, 8000), "read actual SkyCore configuration");
    reorderConfigContexts = true;
    const QByteArray configPayload = QJsonDocument(currentConfig).toJson(QJsonDocument::Compact);
    const auto firstConfig = controller.sendCommand(CommandId::SetSkyConfig, configPayload);
    dropConfigAckSequence = firstConfig;
    const auto secondConfig = controller.sendCommand(CommandId::SetSkyConfig, configPayload);
    require(firstConfig && secondConfig && firstConfig != secondConfig, "concurrent configuration commands start");
    QString firstConfigId, secondConfigId;
    require(waitUntil([&] {
        for (const auto& r : groundLogs) {
            if (r.fields.value(QStringLiteral("event")) != QStringLiteral("ground_command_send")) continue;
            const auto seq = r.fields.value(QStringLiteral("command_seq")).toUInt();
            if (seq == firstConfig) firstConfigId = r.correlation_id;
            if (seq == secondConfig) secondConfigId = r.correlation_id;
        }
        return !firstConfigId.isEmpty() && !secondConfigId.isEmpty();
    }, &sky, 8000), "configuration request ids observed");
    require(firstConfigId != secondConfigId, "configuration requests use independent ids");
    require(waitUntil([&] {
        bool first = false, second = false;
        for (const auto& r : groundLogs) {
            if (r.fields.value(QStringLiteral("event")) != QStringLiteral("ground_config_apply_result_context")) continue;
            const auto seq = r.fields.value(QStringLiteral("command_seq")).toUInt();
            if (seq == firstConfig) first = r.correlation_id == firstConfigId && r.session_id == sessionId;
            if (seq == secondConfig) second = r.correlation_id == secondConfigId && r.session_id == sessionId;
        }
        return first && second && legacyConfigResults.size() >= 2 && !reorderConfigContexts;
    }, &sky, 15000), "out-of-order configuration results preserve ids and real session without first ACK");
    require(wrongConfigContextInjected, "mismatched configuration context was injected");
    if (longSession) require(configV2Count >= 2, "SkyCore sends both long configuration results as v2");
    require(waitUntil([&] { return commandAcks.contains(secondConfig); }, &sky, 8000) &&
            commandAcks.value(secondConfig).config_result_context_expected.has_value() &&
            *commandAcks.value(secondConfig).config_result_context_expected &&
            commandAcks.value(secondConfig).correlation_id == secondConfigId &&
            commandAcks.value(secondConfig).session_id == sessionId,
            "actual SkyCore ACK announces contextual configuration result with matching identity");
    for (const auto& r : groundLogs)
        require(r.correlation_id != QStringLiteral("wrong-operation-id"), "mismatched configuration context is not attributed");
    require(!commandAcks.contains(firstConfig), "configuration result is associated even when its ACK is lost");
    const qsizetype afterConfigResult = groundLogs.size();
    require(!waitUntil([&] {
        for (qsizetype i = afterConfigResult; i < groundLogs.size(); ++i) {
            const auto& r = groundLogs[i];
            const auto event = r.fields.value(QStringLiteral("event")).toString();
            if (r.correlation_id == firstConfigId &&
                (event == QStringLiteral("ground_command_retry") || event == QStringLiteral("ground_command_timeout")))
                return true;
        }
        return false;
    }, &sky, 4000), "completed configuration does not retry or time out when its ACK stays lost");
    require(proxyGroundPeer, "configuration proxy is connected for late ACK");
    CommandAck wrongConfigAck;
    wrongConfigAck.command_id = CommandId::SetSkyConfig;
    wrongConfigAck.command_seq = firstConfig;
    wrongConfigAck.correlation_id = secondConfigId;
    wrongConfigAck.session_id = sessionId;
    wrongConfigAck.extended_ack_context_allowed = longSession;
    TelemetryCodec lateAckEncoder;
    proxyGroundPeer->write(lateAckEncoder.encodeFrame(MsgType::CommandAck,
        TelemetryCodec::serializeCommandAck(wrongConfigAck), 1, 1));
    require(!waitUntil([&] { return commandAcks.contains(firstConfig); }, &sky, 200),
            "late ACK with another operation identity is rejected");
    wrongConfigAck.correlation_id = firstConfigId;
    proxyGroundPeer->write(lateAckEncoder.encodeFrame(MsgType::CommandAck,
        TelemetryCodec::serializeCommandAck(wrongConfigAck), 2, 1));
    require(waitUntil([&] { return commandAcks.contains(firstConfig); }, &sky, 3000),
            "matching late configuration ACK remains deliverable");
    dropConfigAckSequence = 0;

    const quint16 invalidConfig = controller.sendCommand(CommandId::SetSkyConfig, QByteArrayLiteral("not-json"));
    require(invalidConfig && waitUntil([&] { return commandAcks.contains(invalidConfig); }, &sky, 8000),
            "invalid configuration receives actual SkyCore ACK");
    const auto invalidAck = commandAcks.value(invalidConfig);
    require(invalidAck.error_code == CommandErrorCode::ConfigInvalid &&
            invalidAck.config_result_context_expected.has_value() && !*invalidAck.config_result_context_expected &&
            !invalidAck.correlation_id.isEmpty() && invalidAck.session_id == sessionId,
            "invalid configuration ACK announces no contextual result with actual session");
    for (const auto& record : groundLogs)
        require(!(record.correlation_id == invalidAck.correlation_id &&
                  record.fields.value(QStringLiteral("event")) == QStringLiteral("ground_config_apply_result_context")),
                "invalid configuration does not receive a contextual result");

    const quint16 legacyConfig = controller.sendCommand(CommandId::SetSkyConfig, configPayload);
    legacyConfigAckSequence = legacyConfig;
    require(legacyConfig && waitUntil([&] { return commandAcks.contains(legacyConfig); }, &sky, 8000),
            "legacy-style configuration ACK received over real connection");
    const auto legacyConfigAck = commandAcks.value(legacyConfig);
    require(legacyConfigAckHadExpectation && !legacyConfigAck.config_result_context_expected.has_value() &&
            !legacyConfigAck.correlation_id.isEmpty() && legacyConfigAck.session_id == sessionId,
            "proxy strips only the expectation from an actual SkyCore ACK");
    require(waitUntil([&] {
        for (const auto& r : groundLogs)
            if (r.fields.value(QStringLiteral("event")) == QStringLiteral("ground_config_apply_result_context") &&
                r.fields.value(QStringLiteral("command_seq")).toUInt() == legacyConfig &&
                r.correlation_id == legacyConfigAck.correlation_id && r.session_id == sessionId) return true;
        return false;
    }, &sky, 8000), "legacy-style ACK still associates the real configuration result");
    legacyConfigAckSequence = 0;
    const auto request = controller.sendFpgaControl({{"op", "raw"}, {"enabled", true}});
    require(request != 0 && waitUntil([&] { return results.contains(request); }, &sky, 15000),
            "simulated FPGA typed response returns");
    const auto response = results.value(request);
    require(response.error_code == CommandErrorCode::Ok && !response.correlation_id.isEmpty(),
            "simulated FPGA typed result keeps generated correlation");
    const QString id = response.correlation_id;
    bool wireMatches = false;
    for (const auto& result : wireContextResults)
        if (result.request_id == request && result.correlation_id == id && result.session_id == sessionId)
            wireMatches = true;
    require(wireMatches, "cross-process device result carries the actual correlation and session on the wire");
    if (longSession) require(deviceV2Count > 0, "SkyCore sends long device result as v2");
    LogRecord groundSend, groundAck, groundResult, skyReceived, worker, driver, skyCompleted;
    require(waitUntil([&] {
        for (const auto& r : groundLogs) if (r.correlation_id == id) {
            const QString event = r.fields.value(QStringLiteral("event")).toString();
            if (event == QStringLiteral("ground_command_send")) groundSend = r;
            if (event == QStringLiteral("ground_command_ack")) groundAck = r;
            if (event == QStringLiteral("ground_device_operation_response")) groundResult = r;
        }
        for (const auto& r : skyLogs) if (r.correlation_id == id) {
            const QString event = r.fields.value(QStringLiteral("event")).toString();
            if (event == QStringLiteral("sky_command_received")) skyReceived = r;
            if (event == QStringLiteral("fpga_worker_started")) worker = r;
            if (event == QStringLiteral("fpga_raw_acquisition_result")) driver = r;
            if (event == QStringLiteral("sky_command_completed")) skyCompleted = r;
        }
        return groundSend.thread_id && groundAck.thread_id && groundResult.thread_id && skyReceived.thread_id &&
               worker.thread_id && driver.thread_id && skyCompleted.thread_id;
    }, &sky, 8000), "same generated correlation appears through ground, FPGA worker, driver, and typed response");
    require(skyReceived.process_id == skyPid && worker.process_id == skyPid && driver.process_id == skyPid &&
            groundSend.process_id == static_cast<quint64>(QCoreApplication::applicationPid()) &&
            worker.thread_id == driver.thread_id && worker.thread_id != skyReceived.thread_id,
            "evidence identifies separate Ground process and SkyCore FPGA worker thread");
    require(groundSend.session_id.isEmpty() && groundAck.session_id == sessionId &&
            groundResult.session_id == sessionId && response.session_id == sessionId &&
            skyReceived.session_id == sessionId && worker.session_id == sessionId &&
            driver.session_id == sessionId && skyCompleted.session_id == sessionId,
            "active session is carried through sky worker, driver, ACK and typed result");
    const auto show = [&](const char *stage, const LogRecord& record) {
        std::cout << "FPGA_CORRELATION " << stage << " pid=" << record.process_id
                  << " tid=" << record.thread_id << " id=" << id.toStdString() << '\n';
    };
    show("ground_send", groundSend); show("sky_receive", skyReceived); show("fpga_worker", worker);
    show("fpga_driver", driver); show("sky_complete", skyCompleted); show("ground_ack", groundAck);
    show("ground_typed", groundResult);
    std::cout << "FPGA_SESSION id=" << id.toStdString() << " session=" << sessionId.toStdString() << '\n';

    const quint16 pauseSequence = controller.sendCommand(CommandId::PauseRecording);
    require(pauseSequence != 0 && waitUntil([&] { return commandAcks.contains(pauseSequence); }, &sky, 15000),
            "recording pause returns ACK");
    require(commandAcks.value(pauseSequence).error_code == CommandErrorCode::Ok &&
            commandAcks.value(pauseSequence).session_id == sessionId,
            "pause retains the original session identity");
    const auto pausedRequest = controller.sendFpgaControl({{"op", "raw"}, {"enabled", false}});
    require(pausedRequest != 0 && waitUntil([&] { return results.contains(pausedRequest); }, &sky, 15000) &&
            results.value(pausedRequest).error_code == CommandErrorCode::Ok &&
            results.value(pausedRequest).session_id == sessionId,
            "operation while paused retains the original session");
    const quint16 resumeSequence = controller.sendCommand(CommandId::StartRecording);
    require(resumeSequence != 0 && waitUntil([&] { return commandAcks.contains(resumeSequence); }, &sky, 15000),
            "recording resume returns ACK");
    require(commandAcks.value(resumeSequence).error_code == CommandErrorCode::Ok &&
            commandAcks.value(resumeSequence).session_id == sessionId,
            "resume retains the original session identity");

    dropNextDeviceAck = true;
    const auto ackLostRequest = controller.sendFpgaControl({{"op", "raw"}, {"enabled", true}});
    require(ackLostRequest != 0 && waitUntil([&] { return deviceAckDropped; }, &sky, 15000),
            "proxy drops the FPGA operation ACK");
    require(waitUntil([&] { return results.contains(ackLostRequest); }, &sky, 15000),
            "contextual FPGA result completes despite its lost ACK");
    const auto ackLostResult = results.value(ackLostRequest);
    require(ackLostResult.error_code == CommandErrorCode::Ok && !ackLostResult.correlation_id.isEmpty() &&
            ackLostResult.correlation_id != id && ackLostResult.session_id == sessionId &&
            !commandAcks.contains(dropAckSequence),
            "result retains its operation and session without an ACK");
    const QString ackLostId = ackLostResult.correlation_id;
    dropAckSequence = 0;

    const quint16 stopSequence = controller.sendCommand(CommandId::StopRecording);
    require(stopSequence != 0 && waitUntil([&] { return commandAcks.contains(stopSequence); }, &sky, 15000),
            "recording stop returns ACK");
    require(commandAcks.value(stopSequence).error_code == CommandErrorCode::Ok &&
            commandAcks.value(stopSequence).session_id == sessionId,
            "recording stop keeps the ending session identity");
    const auto afterStopRequest = controller.sendFpgaControl({{"op", "raw"}, {"enabled", false}});
    require(afterStopRequest != 0 && waitUntil([&] { return results.contains(afterStopRequest); }, &sky, 15000),
            "post-session FPGA typed response returns");
    const auto afterStop = results.value(afterStopRequest);
    require(afterStop.error_code == CommandErrorCode::Ok && !afterStop.correlation_id.isEmpty() &&
            afterStop.correlation_id != id && afterStop.session_id.isEmpty(),
            "post-session FPGA result does not inherit the prior session");
    QSet<QString> postSessionEvents;
    bool postSessionEmpty = true;
    require(waitUntil([&] {
        for (const auto& record : skyLogs) {
            if (record.correlation_id != afterStop.correlation_id) continue;
            const QString event = record.fields.value(QStringLiteral("event")).toString();
            if (event == QStringLiteral("sky_command_received") || event == QStringLiteral("fpga_worker_started") ||
                event == QStringLiteral("fpga_raw_acquisition_result") || event == QStringLiteral("sky_command_completed")) {
                postSessionEvents.insert(event);
                postSessionEmpty = postSessionEmpty && record.session_id.isEmpty();
            }
        }
        for (const auto& record : groundLogs) {
            if (record.correlation_id != afterStop.correlation_id) continue;
            const QString event = record.fields.value(QStringLiteral("event")).toString();
            if (event == QStringLiteral("ground_command_ack") || event == QStringLiteral("ground_device_operation_response")) {
                postSessionEvents.insert(event);
                postSessionEmpty = postSessionEmpty && record.session_id.isEmpty();
            }
        }
        return postSessionEvents.size() == 6;
    }, &sky, 8000) && postSessionEmpty, "post-session chain has no stale session identity");

    // Batch a simulated asynchronous magnetic operation, its cancellation,
    // and an independent status request on the actual cross-process IPC. The
    // SkyCore handler dispatches all frames before its worker completion event.
    const QString magneticId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString cancelId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString independentId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    DeviceOperationRequest magnetic;
    magnetic.request_id = 81; magnetic.device_id = SkyDeviceId::Epsilon;
    magnetic.operation = DeviceOperation::CalibrateEpsilonMagnetic3D;
    magnetic.payload = TelemetryCodec::serializeEpsilonMaintenanceAction(EpsilonMaintenanceAction::Magnetic3D);
    CommandMessage magneticCommand;
    magneticCommand.command_id = CommandId::DeviceOperation; magneticCommand.command_seq = 62001;
    magneticCommand.correlation_id = magneticId;
    magneticCommand.payload = TelemetryCodec::serializeDeviceOperationRequest(magnetic);
    DeviceOperationRequest cancellation;
    cancellation.request_id = 82; cancellation.device_id = SkyDeviceId::Epsilon;
    cancellation.operation = DeviceOperation::CancelEpsilonMagneticCalibration;
    cancellation.payload = QByteArray::fromHex("51000000");
    CommandMessage cancelCommand;
    cancelCommand.command_id = CommandId::DeviceOperation; cancelCommand.command_seq = 62002;
    cancelCommand.correlation_id = cancelId;
    cancelCommand.payload = TelemetryCodec::serializeDeviceOperationRequest(cancellation);
    CommandMessage independent;
    independent.command_id = CommandId::RequestStatus; independent.command_seq = 62003;
    independent.correlation_id = independentId;
    CommandMessage legacy;
    legacy.command_id = CommandId::RequestStatus; legacy.command_seq = 62004;
    TelemetryCodec ipcEncoder;
    QByteArray batch;
    for (const auto& command : {magneticCommand, cancelCommand, independent, legacy})
        batch += ipcEncoder.encodeFrame(MsgType::Command, TelemetryCodec::serializeCommand(command), command.command_seq, 1);
    require(observer.write(batch) == batch.size(), "cross-process cancellation commands queued together");
    require(waitUntil([&] { return ipcAcks.contains(62001) && ipcAcks.contains(62002) &&
                                  ipcAcks.contains(62003) && ipcAcks.contains(62004); }, &sky),
            "cancelled, independent, and legacy requests all return ACK");
    require(ipcAcks.value(62002).error_code == CommandErrorCode::Ok &&
            ipcAcks.value(62002).correlation_id == cancelId &&
            ipcAcks.value(62001).error_code == CommandErrorCode::UnknownCommand &&
            ipcAcks.value(62001).correlation_id == magneticId &&
            ipcAcks.value(62003).error_code == CommandErrorCode::Ok &&
            ipcAcks.value(62003).correlation_id == independentId &&
            ipcAcks.value(62004).error_code == CommandErrorCode::Ok &&
            ipcAcks.value(62004).correlation_id.isEmpty(),
            "real IPC cancellation is accepted, unrelated request completes, and old message remains compatible");
    std::cout << "IPC_CANCEL operation_id=" << magneticId.toStdString() << " cancel_id=" << cancelId.toStdString()
              << " independent_id=" << independentId.toStdString() << " accepted=1 legacy=1\n";

    // Two operations and a status query are concurrently outstanding. The
    // first operation and query ACKs are filtered while the second completes.
    const qsizetype timeoutLogStart = groundLogs.size();
    const quint16 querySequence = controller.requestSkyConfig();
    require(querySequence != 0, "uncorrelated configuration query sent");
    dropQueryAckSequence = querySequence;
    const quint16 timedSequence = controller.sendCommand(CommandId::SetTelemetryRate,
                                                         TelemetryCodec::serializeRatePayload(3));
    dropAckSequence = timedSequence;
    const quint16 successfulSequence = controller.sendCommand(CommandId::SetWaveformRate,
                                                              TelemetryCodec::serializeRatePayload(4));
    require(timedSequence != successfulSequence, "concurrent commands use separate sequences");
    require(waitUntil([&] { return commandAcks.contains(successfulSequence); }, &sky),
            "unfiltered concurrent command completes");
    QString timedId, successfulId;
    for (const auto& record : groundLogs) {
        if (record.fields.value(QStringLiteral("event")) != QStringLiteral("ground_command_send")) continue;
        const auto sequence = record.fields.value(QStringLiteral("command_seq")).toUInt();
        if (sequence == timedSequence) timedId = record.correlation_id;
        if (sequence == successfulSequence) successfulId = record.correlation_id;
    }
    require(!timedId.isEmpty() && !successfulId.isEmpty() && timedId != successfulId &&
            commandAcks.value(successfulSequence).correlation_id == successfulId,
            "concurrent operations have distinct correlation IDs and matching ACK");
    LogRecord commandTimeout, commandWarning, queryWarning;
    qsizetype timeoutIndex = -1, warningIndex = -1;
    require(waitUntil([&] {
        for (qsizetype i = timeoutLogStart; i < groundLogs.size(); ++i) {
            const auto& record = groundLogs.at(i);
            const auto event = record.fields.value(QStringLiteral("event")).toString();
            const auto sequence = record.fields.value(QStringLiteral("command_seq")).toUInt();
            if (event == QStringLiteral("ground_command_timeout") && sequence == timedSequence) { commandTimeout = record; timeoutIndex = i; }
            if (event == QStringLiteral("telemetry_command_ack_timeout") && sequence == timedSequence) { commandWarning = record; warningIndex = i; }
            if (event == QStringLiteral("telemetry_command_ack_timeout") && sequence == querySequence) queryWarning = record;
        }
        return !commandTimeout.fields.isEmpty() && !commandWarning.fields.isEmpty() && !queryWarning.fields.isEmpty();
    }, &sky, 10000), "filtered command and query time out without affecting the successful command");
    require(timeoutIndex < warningIndex && commandTimeout.correlation_id == timedId && commandWarning.correlation_id == timedId &&
            commandWarning.session_id == commandTimeout.session_id &&
            commandWarning.fields.value(QStringLiteral("reason_code")) == QStringLiteral("TELEMETRY_COMMAND_ACK_TIMEOUT"),
            "operation timeout warning preserves the same correlation and session as the command timeout");
    require(queryWarning.correlation_id.isEmpty() && queryWarning.session_id.isEmpty(),
            "uncorrelated configuration query timeout does not inherit an operation context");
    dropQueryAckSequence = 0;
    std::cout << "FPGA_CONCURRENT timeout_id=" << timedId.toStdString()
              << " success_id=" << successfulId.toStdString() << '\n';

    // Start another isolated session and drop a typed result after its ACK.
    const quint16 restartSequence = controller.sendCommand(CommandId::StartRecording,
        QJsonDocument(calibrationSnapshot).toJson(QJsonDocument::Compact));
    require(restartSequence != 0 && waitUntil([&] { return commandAcks.contains(restartSequence); }, &sky, 15000) &&
            commandAcks.value(restartSequence).error_code == CommandErrorCode::Ok,
            "second isolated recording starts");
    const QString disconnectSession = commandAcks.value(restartSequence).session_id;
    require(!disconnectSession.isEmpty(), "second recording has a real session");
    const qsizetype logOffset = groundLogs.size();
    dropContextRequest = controller.sendFpgaControl({{"op", "raw"}, {"enabled", true}});
    require(dropContextRequest != 0, "operation for remote disconnect starts");
    quint16 disconnectedSequence = 0;
    QString disconnectedId;
    for (qsizetype i = logOffset; i < groundLogs.size(); ++i) {
        const auto& record = groundLogs.at(i);
        if (record.fields.value(QStringLiteral("event")) == QStringLiteral("ground_command_send") &&
            record.fields.value(QStringLiteral("command_id")).toUInt() == static_cast<quint16>(CommandId::DeviceOperation)) {
            disconnectedId = record.correlation_id;
            disconnectedSequence = static_cast<quint16>(record.fields.value(QStringLiteral("command_seq")).toUInt());
        }
    }
    require(!disconnectedId.isEmpty() && disconnectedId != timedId && disconnectedId != successfulId,
            "disconnect scenario has a fresh operation ID");
    require(waitUntil([&] { return commandAcks.contains(disconnectedSequence) && contextDropped; }, &sky, 15000) &&
            commandAcks.value(disconnectedSequence).session_id == disconnectSession && !results.contains(dropContextRequest),
            "proxy drops the typed result after an ACK with the active session");
    require(proxyGroundPeer, "proxy owns the test connection");
    proxyGroundPeer->abort();
    require(waitUntil([&] { return !open; }, &sky, 5000), "remote transport loss reaches Ground");
    bool disconnectLogged = false;
    bool falseTimeout = false, falseDisconnect = false;
    for (const auto& record : groundLogs) {
        if (record.fields.value(QStringLiteral("event")) == QStringLiteral("ground_command_disconnected") &&
            record.correlation_id == disconnectedId && record.session_id == disconnectSession) disconnectLogged = true;
        if (record.correlation_id == ackLostId) {
            falseTimeout |= record.fields.value(QStringLiteral("event")) == QStringLiteral("ground_command_timeout");
            falseDisconnect |= record.fields.value(QStringLiteral("event")) == QStringLiteral("ground_command_disconnected");
        }
        require(!(record.correlation_id == invalidAck.correlation_id &&
                  record.fields.value(QStringLiteral("event")) == QStringLiteral("ground_command_disconnected") &&
                  record.fields.value(QStringLiteral("stage")) == QStringLiteral("awaiting_config_result")),
                "invalid configuration with no result is not reported as awaiting one on disconnect");
    }
    require(disconnectLogged, "remote loss records the unfinished operation and its session");
    require(!falseTimeout && !falseDisconnect, "completed operation with lost ACK is no longer pending");
    CommandMessage stopAfterDisconnect;
    stopAfterDisconnect.command_id = CommandId::StopRecording;
    stopAfterDisconnect.command_seq = 62005;
    stopAfterDisconnect.correlation_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    stopAfterDisconnect.extended_ack_context_requested = true;
    const QByteArray stopFrame = ipcEncoder.encodeFrame(MsgType::Command,
        TelemetryCodec::serializeCommand(stopAfterDisconnect), stopAfterDisconnect.command_seq, 1);
    require(observer.write(stopFrame) == stopFrame.size() &&
            waitUntil([&] { return ipcAcks.contains(stopAfterDisconnect.command_seq); }, &sky, 15000) &&
            ipcAcks.value(stopAfterDisconnect.command_seq).error_code == CommandErrorCode::Ok,
            "isolated recording stops through IPC after telemetry loss");
    if (longSession)
        require(ipcAcks.value(stopAfterDisconnect.command_seq).extended_ack_context &&
                ipcAcks.value(stopAfterDisconnect.command_seq).session_id == disconnectSession,
                "IPC stop returns the actual long Session in an extended ACK");
    CommandMessage restartForShutdown;
    restartForShutdown.command_id = CommandId::StartRecording;
    restartForShutdown.command_seq = 62006;
    restartForShutdown.correlation_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    restartForShutdown.extended_ack_context_requested = true;
    restartForShutdown.payload = QJsonDocument(calibrationSnapshot).toJson(QJsonDocument::Compact);
    const auto restartFrame = ipcEncoder.encodeFrame(MsgType::Command,
        TelemetryCodec::serializeCommand(restartForShutdown), restartForShutdown.command_seq, 1);
    require(observer.write(restartFrame) == restartFrame.size() &&
            waitUntil([&] { return ipcAcks.contains(restartForShutdown.command_seq); }, &sky, 15000) &&
            ipcAcks.value(restartForShutdown.command_seq).error_code == CommandErrorCode::Ok,
            "isolated session restarts before IPC shutdown");
    const QString shutdownSession = ipcAcks.value(restartForShutdown.command_seq).session_id;
    require(!shutdownSession.isEmpty(), "restarted session has a real directory");
    if (longSession)
        require(ipcAcks.value(restartForShutdown.command_seq).extended_ack_context &&
                shutdownSession.toUtf8().size() > 4096 && QFileInfo(shutdownSession).isDir(),
                "IPC restart returns the real long Session in an extended ACK");
    CommandMessage pauseForShutdown;
    pauseForShutdown.command_id = CommandId::PauseRecording;
    pauseForShutdown.command_seq = 62007;
    pauseForShutdown.correlation_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    pauseForShutdown.extended_ack_context_requested = true;
    const auto pauseFrame = ipcEncoder.encodeFrame(MsgType::Command,
        TelemetryCodec::serializeCommand(pauseForShutdown), pauseForShutdown.command_seq, 1);
    require(observer.write(pauseFrame) == pauseFrame.size() &&
            waitUntil([&] { return ipcAcks.contains(pauseForShutdown.command_seq); }, &sky, 15000) &&
            ipcAcks.value(pauseForShutdown.command_seq).error_code == CommandErrorCode::Ok &&
            ipcAcks.value(pauseForShutdown.command_seq).session_id == shutdownSession,
            "IPC pause retains the active session");
    if (longSession)
        require(ipcAcks.value(pauseForShutdown.command_seq).extended_ack_context,
                "IPC pause returns an extended ACK for the long Session");
    CommandMessage shutdown;
    shutdown.command_id = CommandId::ShutdownCore;
    shutdown.command_seq = 62008;
    shutdown.correlation_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    shutdown.extended_ack_context_requested = true;
    const auto shutdownFrame = ipcEncoder.encodeFrame(MsgType::Command,
        TelemetryCodec::serializeCommand(shutdown), shutdown.command_seq, 1);
    require(observer.write(shutdownFrame) == shutdownFrame.size() &&
            waitUntil([&] { return ipcAcks.contains(shutdown.command_seq); }, &sky, 15000),
            "IPC shutdown returns an acknowledgement");
    require(ipcAcks.value(shutdown.command_seq).correlation_id == shutdown.correlation_id &&
            ipcAcks.value(shutdown.command_seq).session_id == shutdownSession,
            "IPC shutdown acknowledgement retains the paused session and correlation");
    if (longSession)
        require(ipcAcks.value(shutdown.command_seq).extended_ack_context,
                "IPC shutdown returns an extended ACK for the long Session");
    require(waitUntil([&] {
        for (const auto& record : skyLogs)
            if (record.fields.value(QStringLiteral("event")) == QStringLiteral("sky_ipc_shutdown_requested") &&
                record.correlation_id == shutdown.correlation_id && record.session_id == shutdownSession)
                return true;
        return false;
    }, &sky, 5000), "IPC shutdown log retains the paused session and correlation");
    observer.disconnectFromHost();
    stopSkyCore(sky);
    require(sky.state() == QProcess::NotRunning, "isolated FPGA simulator process stopped");
}

void unexpectedSkyExitE2e(QVector<VaporView::LogRecord>& groundLogs, bool activeSession)
{
    using namespace VaporView;
    QTemporaryDir isolated;
    require(isolated.isValid(), "unexpected exit fixture directory");
    const QString configPath = isolated.filePath(QStringLiteral("sky.json"));
    require(SkyConfig::defaults().saveToFile(configPath), "save unexpected exit config");
    const quint16 port = reserveLocalTcpPort();
    quint16 ipcPort = reserveLocalTcpPort();
    while (ipcPort == port) ipcPort = reserveLocalTcpPort();
    QProcess sky;
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("LOCALAPPDATA"), isolated.path());
    environment.insert(QStringLiteral("VAPORVIEW_SETTINGS_DIR"), isolated.path());
    sky.setProcessEnvironment(environment);
    sky.setProgram(skyCoreExecutablePath());
    sky.setArguments({QStringLiteral("--fpga-simulate-data"), QStringLiteral("--sky-config"), configPath,
        QStringLiteral("--telemetry-transport"), QStringLiteral("tcp"),
        QStringLiteral("--telemetry-host"), QStringLiteral("127.0.0.1"),
        QStringLiteral("--telemetry-tcp-port"), QString::number(port),
        QStringLiteral("--sky-wave-host"), QStringLiteral("127.0.0.1"),
        QStringLiteral("--sky-wave-port"), QStringLiteral("9"),
        QStringLiteral("--ipc-port"), QString::number(ipcPort)});
    sky.setProcessChannelMode(QProcess::MergedChannels);
    sky.start();
    require(sky.waitForStarted(5000), "unexpected exit SkyCore starts");
    QTcpSocket observer;
    require(waitUntil([&] {
        if (observer.state() == QAbstractSocket::UnconnectedState)
            observer.connectToHost(QHostAddress::LocalHost, ipcPort);
        return observer.state() == QAbstractSocket::ConnectedState;
    }, &sky), "unexpected exit SkyCore IPC becomes ready");

    QTcpServer proxy;
    require(proxy.listen(QHostAddress::LocalHost, 0), "unexpected exit proxy listens");
    quint16 heldSequence = 0;
    bool ackHeld = false;
    quint32 heldRequest = 0;
    bool resultHeld = false;
    QObject::connect(&proxy, &QTcpServer::newConnection, &proxy, [&] {
        auto *groundPeer = proxy.nextPendingConnection();
        auto *skyPeer = new QTcpSocket(&proxy);
        auto pending = std::make_shared<QByteArray>();
        auto decoder = std::make_shared<TelemetryCodec>();
        QObject::connect(groundPeer, &QTcpSocket::readyRead, &proxy, [groundPeer, skyPeer, pending] {
            const QByteArray bytes = groundPeer->readAll();
            if (skyPeer->state() == QAbstractSocket::ConnectedState) skyPeer->write(bytes);
            else pending->append(bytes);
        });
        QObject::connect(skyPeer, &QTcpSocket::connected, &proxy, [skyPeer, pending] {
            if (!pending->isEmpty()) { skyPeer->write(*pending); pending->clear(); }
        });
        QObject::connect(skyPeer, &QTcpSocket::readyRead, &proxy, [&, groundPeer, skyPeer, decoder] {
            TelemetryCodec encoder;
            for (const auto& frame : decoder->feedBytes(skyPeer->readAll())) {
                CommandAck ack;
                if (frame.type == MsgType::CommandAck && TelemetryCodec::parseCommandAck(frame.payload, ack) &&
                    heldSequence != 0 && ack.command_seq == heldSequence) {
                    ackHeld = true;
                    continue;
                }
                DeviceOperationResponse response;
                if (frame.type == MsgType::DeviceOperationResponseContext &&
                    TelemetryCodec::parseDeviceOperationResponseContext(frame.payload, response) &&
                    heldRequest != 0 && response.request_id == heldRequest) {
                    resultHeld = true;
                    continue;
                }
                groundPeer->write(encoder.encodeFrame(frame.type, frame.payload, frame.seq, frame.time_us, frame.flags));
            }
        });
        QObject::connect(skyPeer, &QTcpSocket::disconnected, groundPeer, &QTcpSocket::disconnectFromHost);
        QObject::connect(groundPeer, &QTcpSocket::disconnected, skyPeer, &QTcpSocket::disconnectFromHost);
        skyPeer->connectToHost(QHostAddress::LocalHost, port);
    });

    Ground::Devices::RemoteSkyController controller;
    bool open = false, ready = false;
    QHash<quint16, CommandAck> acks;
    QObject::connect(&controller, &Ground::Devices::RemoteSkyController::linkOpenChanged,
                     [&](bool value) { open = value; });
    QObject::connect(&controller, &Ground::Devices::RemoteSkyController::fpgaStatusUpdated,
                     [&](const QJsonObject& status) { ready = status.value(QStringLiteral("ready")).toBool(); });
    QObject::connect(&controller, &Ground::Devices::RemoteSkyController::commandAckReceived,
                     [&](const CommandAck& ack) { acks.insert(ack.command_seq, ack); });
    require(controller.openTcp(QStringLiteral("127.0.0.1"), proxy.serverPort()), "unexpected exit Ground connects");
    require(waitUntil([&] { return open && ready; }, &sky, 15000), "unexpected exit Ground and FPGA simulator are ready");
    QString completedId;
    const quint16 completed = controller.sendCommand(CommandId::SetTelemetryRate,
        TelemetryCodec::serializeRatePayload(2), &completedId);
    require(completed && waitUntil([&] { return acks.contains(completed); }, &sky) &&
            acks.value(completed).error_code == CommandErrorCode::Ok && !completedId.isEmpty(),
            "independent command completes before SkyCore exit");
    QString pendingId, expectedSession;
    if (activeSession) {
        IndependentCalibrationTest::Rig calibration;
        const auto calibrationFile = calibration.complete();
        calibration.service->selectFile(calibrationFile.filename);
        calibration.service->synchronize();
        QJsonObject snapshot;
        QString error;
        require(calibration.service->prepare(snapshot, &error), qPrintable(error));
        snapshot.remove(QStringLiteral("vcal_document"));
        const quint16 start = controller.sendCommand(CommandId::StartRecording,
            QJsonDocument(snapshot).toJson(QJsonDocument::Compact));
        require(start && waitUntil([&] { return acks.contains(start); }, &sky, 15000) &&
                acks.value(start).error_code == CommandErrorCode::Ok,
                "isolated session starts before unexpected exit");
        expectedSession = acks.value(start).session_id;
        require(!expectedSession.isEmpty() && QFileInfo(expectedSession).isDir(),
                "unexpected exit fixture has a real session");
        const qsizetype offset = groundLogs.size();
        heldRequest = controller.sendFpgaControl({{"op", "raw"}, {"enabled", true}});
        require(heldRequest != 0, "FPGA operation starts before unexpected exit");
        quint16 operationSequence = 0;
        for (qsizetype i = offset; i < groundLogs.size(); ++i) {
            const auto& record = groundLogs.at(i);
            if (record.fields.value(QStringLiteral("event")) == QStringLiteral("ground_command_send") &&
                record.fields.value(QStringLiteral("command_id")).toUInt() == static_cast<quint16>(CommandId::DeviceOperation)) {
                pendingId = record.correlation_id;
                operationSequence = static_cast<quint16>(record.fields.value(QStringLiteral("command_seq")).toUInt());
            }
        }
        require(operationSequence && !pendingId.isEmpty() && pendingId != completedId &&
                waitUntil([&] { return acks.contains(operationSequence) && resultHeld; }, &sky, 15000) &&
                acks.value(operationSequence).correlation_id == pendingId &&
                acks.value(operationSequence).session_id == expectedSession,
                "ACK confirms the real session while the typed result remains pending");
    } else {
        heldSequence = controller.sendCommand(CommandId::SetTelemetryRate,
            TelemetryCodec::serializeRatePayload(3), &pendingId);
        require(heldSequence && !pendingId.isEmpty() && pendingId != completedId &&
                waitUntil([&] { return ackHeld; }, &sky) && !acks.contains(heldSequence),
                "second command remains pending after its response is held");
    }
    sky.kill();
    require(sky.waitForFinished(5000) && sky.exitStatus() == QProcess::CrashExit,
            "test-owned SkyCore is terminated");
    require(waitUntil([&] { return !open; }, nullptr, 5000), "SkyCore termination reaches Ground");
    bool pendingDisconnected = false, completedDisconnected = false;
    for (const auto& record : groundLogs) {
        if (record.fields.value(QStringLiteral("event")) != QStringLiteral("ground_command_disconnected")) continue;
        if (record.correlation_id == pendingId && record.session_id == expectedSession) pendingDisconnected = true;
        if (record.correlation_id == completedId) completedDisconnected = true;
    }
    require(pendingDisconnected && !completedDisconnected,
            "SkyCore exit retains pending ID without reporting the completed operation");
    std::cout << "SKY_EXIT pending=" << pendingId.toStdString()
              << " completed=" << completedId.toStdString()
              << " active_session=" << activeSession << '\n';
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
    QVector<VaporView::LogRecord> groundLogs;
    VaporView::LogService logs(QStringLiteral("ground-e2e"), nullptr, tempDir.path(), tempDir.path());
    QObject::connect(&logs, &VaporView::LogService::recordPublished, &app,
                     [&](const VaporView::LogRecord& record) { groundLogs.push_back(record); });
    if (app.arguments().contains(QStringLiteral("--unexpected-sky-exit"))) {
        unwindFailures = true;
        try {
            unexpectedSkyExitE2e(groundLogs, false);
            unexpectedSkyExitE2e(groundLogs, true);
        } catch (const std::exception& error) {
            std::cerr << "Unexpected SkyCore exit test failed: " << error.what() << '\n';
            return 1;
        }
        return 0;
    }
    if (app.arguments().contains(QStringLiteral("--long-session-v2"))) {
        unwindFailures = true;
        try {
            fpgaCorrelationE2e(groundLogs, true);
        } catch (const LongPathUnavailable&) {
            return 77;
        } catch (const std::exception& error) {
            std::cerr << "Long Session test failed: " << error.what() << '\n';
            return 1;
        }
        return 0;
    }
    const QString configPath = tempDir.filePath(QStringLiteral("sky_config_e2e_test.json"));
    QString error;
    VaporView::SkyConfig config = testConfig();
    require(config.saveToFile(configPath, &error), "initial SkyConfig saved");

    const quint16 port = reserveLocalTcpPort();
    quint16 ipcPort = reserveLocalTcpPort();
    while (ipcPort == port) ipcPort = reserveLocalTcpPort();
    QProcess process;
    startSkyCore(process, port, configPath, ipcPort);
    QTcpSocket observer;
    VaporView::TelemetryCodec observerCodec;
    QVector<VaporView::LogRecord> skyLogs;
    QObject::connect(&observer, &QTcpSocket::readyRead, &app, [&]() {
        for (const auto& frame : observerCodec.feedBytes(observer.readAll()))
        {
            VaporView::LogRecord record;
            if (frame.type == VaporView::MsgType::LogEvent &&
                VaporView::TelemetryCodec::parseLogRecord(frame.payload, record)) skyLogs.push_back(record);
        }
    });
    require(waitUntil([&]() {
        if (observer.state() == QAbstractSocket::UnconnectedState)
            observer.connectToHost(QHostAddress::LocalHost, ipcPort);
        return observer.state() == QAbstractSocket::ConnectedState;
    }, &process), "isolated IPC log observer connects");

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
    QSet<quint16> internalSources;
    QObject::connect(&controller,&VaporView::Ground::Devices::RemoteSkyController::internalEnvironmentUpdated,
        [&](const VaporView::FpgaSensor::Reading& reading) {
            require(reading.validity.measurement,"remote internal environment typed reading valid");internalSources.insert(reading.source);
        });
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
    require(waitUntil([&]{return internalSources.contains(0x43)&&internalSources.contains(0x44);},&process),
        "SkyCore sends both independent internal devices over the same telemetry TCP link");
    require(waitUntil([&]() { return receivedConfig.contains(QStringLiteral("ai8_temperature_controller")); },
                      &process),
            "GetSkyConfig returns AI-8 section");
    require(receivedConfig.value(QStringLiteral("ptb")).toObject().value(QStringLiteral("source")).toString() ==
                QStringLiteral("ptb210"),
            "initial pressure source returned");
    require(receivedConfig.value(QStringLiteral("hmp")).toObject().value(QStringLiteral("source")).toString() ==
                QStringLiteral("hmp3"),
            "initial humidity source returned");
    require(!receivedConfig.value(QStringLiteral("epsilon")).toObject().contains(QStringLiteral("frequency_hz")),
            "GetSkyConfig omits EPSILON single frequency");
    require(receivedConfig.value(QStringLiteral("epsilon")).toObject()
                .value(QStringLiteral("packet_rates")).isArray(),
            "GetSkyConfig includes EPSILON packet-rate profile");
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
                       QStringLiteral("ptb210");
            }, &process),
            "GetSkyConfig migrates legacy BMP390 environment selection to PTB210");
    require(receivedConfig.value(QStringLiteral("hmp")).toObject().value(QStringLiteral("source")).toString() ==
                QStringLiteral("hmp3"),
            "GetSkyConfig migrates legacy SHT45 environment selection to HMP");
    require(receivedConfig.value(QStringLiteral("ptb")).toObject().value(QStringLiteral("baud")).toInt() == 9600 &&
                receivedConfig.value(QStringLiteral("hmp")).toObject().value(QStringLiteral("baud")).toInt() == 19200,
            "legacy internal sensor serial settings do not reach environmental instruments");
    require(receivedConfig.value(QStringLiteral("ai8_temperature_controller")).toObject()
                .value(QStringLiteral("slave_address")).toInt() == 7,
            "GetSkyConfig reflects AI-8 slave address after apply");
    require(!receivedConfig.value(QStringLiteral("epsilon")).toObject().contains(QStringLiteral("frequency_hz")),
            "post-apply GetSkyConfig still omits EPSILON single frequency");
    require(receivedConfig.value(QStringLiteral("epsilon")).toObject()
                .value(QStringLiteral("packet_rates")).isArray(),
            "post-apply GetSkyConfig preserves EPSILON packet-rate profile");
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
    seq=controller.sendCommand(VaporView::CommandId::DisconnectDevice,
        VaporView::TelemetryCodec::serializeDeviceCommand(VaporView::SkyDeviceId::Bmp390));
    require(waitUntil([&]{return acks.contains(seq)&&controller.deviceState(VaporView::SkyDeviceId::Bmp390)==VaporView::DeviceState::Disconnected;},&process),
        "individual internal bridge disconnect travels through SkyCore device commands");
    QString correlation;
    for (const auto& record : groundLogs)
        if (record.fields.value(QStringLiteral("event")) == QStringLiteral("ground_command_send") &&
            record.fields.value(QStringLiteral("command_seq")).toUInt() == seq) correlation = record.correlation_id;
    require(!correlation.isEmpty(), "Ground generates device operation correlation ID");
    require(waitUntil([&]() {
        bool received = false, completed = false, deviceStarted = false, deviceFinished = false, ack = false;
        for (const auto& record : skyLogs) if (record.correlation_id == correlation) {
            received |= record.fields.value(QStringLiteral("event")) == QStringLiteral("sky_command_received");
            completed |= record.fields.value(QStringLiteral("event")) == QStringLiteral("sky_command_completed");
            deviceStarted |= record.fields.value(QStringLiteral("event")) == QStringLiteral("sky_device_operation_started");
            deviceFinished |= record.fields.value(QStringLiteral("event")) == QStringLiteral("sky_device_operation_finished");
        }
        for (const auto& record : groundLogs) if (record.correlation_id == correlation)
            ack |= record.fields.value(QStringLiteral("event")) == QStringLiteral("ground_command_ack");
        return received && completed && deviceStarted && deviceFinished && ack &&
            acks.value(seq).correlation_id == correlation;
    }, &process), "Ground and SkyCore device command lifecycle retains correlation ID");
    require(controller.deviceState(VaporView::SkyDeviceId::Sht45)==VaporView::DeviceState::Connected
        &&(lastBasic.validity_flags&VaporView::BasicHasPressure),"BMP disconnect preserves independent SHT and environmental PTB data");
    seq=controller.sendCommand(VaporView::CommandId::ConnectDevice,
        VaporView::TelemetryCodec::serializeDeviceCommand(VaporView::SkyDeviceId::Bmp390));
    require(waitUntil([&]{return acks.contains(seq)&&controller.deviceState(VaporView::SkyDeviceId::Bmp390)==VaporView::DeviceState::Connected;},&process),
        "individual internal bridge reconnect uses its own retained Sky configuration");

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
                       lastStatus.rtcm_correction_last_receive_time_us > 0 &&
                       lastStatus.rtcm_observability_version == 1 &&
                       lastStatus.rtcm_link_stream_id != 0 &&
                       lastStatus.rtcm_link_frames_received > 0 &&
                       lastStatus.rtcm_link_frames_lost == 0;
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

    startSkyCore(process, port, configPath, ipcPort);
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
                       QStringLiteral("ptb210");
            }, &process),
            "Restarted SkyCore reloads saved pressure source");
    require(receivedConfig.value(QStringLiteral("hmp")).toObject().value(QStringLiteral("source")).toString() ==
                QStringLiteral("hmp3"),
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
    fpgaCorrelationE2e(groundLogs);

    std::cout << "sky_ground_simulation_e2e_test passed\n";
    return 0;
}
