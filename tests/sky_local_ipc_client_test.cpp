#include "SkyLocalIpcClient.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QHostAddress>
#include <QJsonDocument>
#include <QTcpServer>
#include <QTcpSocket>
#include <cstdlib>
#include <iostream>

namespace VaporView {
class SkyLocalIpcClientTestAccess {
public:
    static void setNext(SkyLocalIpcClient& client, quint16 sequence) { client.next_command_seq_ = sequence; }
    static void reservePending(SkyLocalIpcClient& client, quint16 sequence) {
        CommandMessage command; command.command_seq = sequence; command.correlation_id = QStringLiteral("existing");
        client.pending_commands_.insert(sequence, command);
    }
    static void reserveResult(SkyLocalIpcClient& client, quint16 sequence) {
        CommandMessage command; command.command_seq = sequence; command.correlation_id = QStringLiteral("result");
        client.config_result_contexts_.insert(sequence, {command, true, true});
    }
    static bool hasPending(const SkyLocalIpcClient& client, quint16 sequence) { return client.pending_commands_.contains(sequence); }
    static void disableExtendedAck(SkyLocalIpcClient& client, quint16 sequence) {
        client.pending_commands_[sequence].extended_ack_context_requested = false;
    }
    static void disableExtendedResult(SkyLocalIpcClient& client, quint16 sequence) {
        client.pending_commands_[sequence].extended_result_context_requested = false;
    }
    static bool hasResult(const SkyLocalIpcClient& client, quint16 sequence) { return client.config_result_contexts_.contains(sequence); }
    static void remember(SkyLocalIpcClient& client, const CommandMessage& command, bool awaitingAck) {
        client.rememberConfigResultContext(command, awaitingAck, true);
    }
    static void fillPending(SkyLocalIpcClient& client) {
        for (quint32 i = 1; i <= 65535; ++i) reservePending(client, quint16(i));
    }
    static void clear(SkyLocalIpcClient& client) { client.pending_commands_.clear(); client.config_result_contexts_.clear(); }
};
}

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
bool waitUntil(Predicate predicate, int timeoutMs = 3000)
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
    }
    return predicate();
}

quint64 nowUs()
{
    return static_cast<quint64>(QDateTime::currentDateTimeUtc().toMSecsSinceEpoch()) * 1000ULL;
}

void testReentrantReconnectDuringCommandLog()
{
    using namespace VaporView;
    QTcpServer server;
    require(server.listen(QHostAddress::LocalHost, 0), "reentrant fixture listens");
    SkyLocalIpcClient client;
    client.connectToCore(QStringLiteral("127.0.0.1"), server.serverPort());
    require(waitUntil([&] { return client.isConnected() && server.hasPendingConnections(); }),
            "reentrant fixture connects");
    QTcpSocket *oldPeer = server.nextPendingConnection();
    require(oldPeer != nullptr, "reentrant fixture accepts first connection");

    bool switched = false;
    QString operationId;
    QObject::connect(&client, &SkyLocalIpcClient::logRecordGenerated, [&](const LogRecord& record) {
        if (switched || record.fields.value(QStringLiteral("event")) != QStringLiteral("sky_ipc_command_started")) return;
        switched = true;
        operationId = record.correlation_id;
        client.connectToCore(QStringLiteral("127.0.0.1"), server.serverPort());
        require(waitUntil([&] { return client.isConnected() && server.hasPendingConnections(); }),
                "log callback reconnects to a new connection");
    });
    require(client.connectDevice(SkyDeviceId::Epsilon) == 0,
            "command interrupted by reconnect reports it was not sent");
    require(switched && !operationId.isEmpty(), "business command log triggered reconnect");
    QTcpSocket *newPeer = server.nextPendingConnection();
    require(newPeer != nullptr, "reentrant fixture accepts new connection");
    TelemetryCodec decoder;
    QVector<TelemetryFrame> frames;
    waitUntil([&] {
        frames += decoder.feedBytes(newPeer->readAll());
        for (const auto& frame : frames) {
            CommandMessage command;
            if (frame.type == MsgType::Command && TelemetryCodec::parseCommand(frame.payload, command) &&
                command.correlation_id == operationId) return true;
        }
        return false;
    }, 150);
    for (const auto& frame : frames) {
        CommandMessage command;
        require(frame.type != MsgType::Command || !TelemetryCodec::parseCommand(frame.payload, command) ||
                command.correlation_id != operationId, "old command never reaches reconnected peer");
    }
    client.disconnectFromCore();
}

void testSequenceWrapAndExhaustion()
{
    using namespace VaporView;
    QTcpServer server;
    require(server.listen(QHostAddress::LocalHost, 0), "sequence fixture listens");
    SkyLocalIpcClient client;
    client.connectToCore(QStringLiteral("127.0.0.1"), server.serverPort());
    require(waitUntil([&] { return client.isConnected() && server.hasPendingConnections(); }), "sequence fixture connects");
    QTcpSocket *peer = server.nextPendingConnection();
    QElapsedTimer startup; startup.start();
    while (startup.elapsed() < 100) QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    peer->readAll();
    SkyLocalIpcClientTestAccess::reservePending(client, 65535);
    SkyLocalIpcClientTestAccess::reserveResult(client, 1);
    SkyLocalIpcClientTestAccess::setNext(client, 65535);
    require(client.requestStatus() == 2, "sequence wrap skips zero, pending and cached result");
    require(SkyLocalIpcClientTestAccess::hasPending(client, 65535) && SkyLocalIpcClientTestAccess::hasResult(client, 1),
            "query does not overwrite existing command or result");
    require(waitUntil([&] { return peer->bytesAvailable() > 0; }), "wrapped query reaches peer");
    TelemetryCodec decoder;
    const auto frames = decoder.feedBytes(peer->readAll());
    bool found = false;
    for (const auto& frame : frames) {
        CommandMessage command;
        if (frame.type == MsgType::Command && TelemetryCodec::parseCommand(frame.payload, command) && command.command_seq == 2)
            found = true;
    }
    require(found, "wire uses the nonzero unoccupied sequence");
    SkyLocalIpcClientTestAccess::fillPending(client);
    require(client.connectDevice(SkyDeviceId::Epsilon) == 0, "exhausted command space reports not sent");
    QElapsedTimer elapsed; elapsed.start();
    while (elapsed.elapsed() < 100) QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    require(peer->bytesAvailable() == 0, "exhausted command space sends no frame");
    require(SkyLocalIpcClientTestAccess::hasResult(client, 1), "exhaustion preserves cached result");
    SkyLocalIpcClientTestAccess::clear(client);
    client.disconnectFromCore();
}

void testReconnectDuringDisconnectLog()
{
    using namespace VaporView;
    QTcpServer server;
    require(server.listen(QHostAddress::LocalHost, 0), "disconnect reentry fixture listens");
    SkyLocalIpcClient client;
    QVector<bool> states;
    QObject::connect(&client, &SkyLocalIpcClient::connectedChanged, [&](bool connected) { states.push_back(connected); });
    client.connectToCore(QStringLiteral("127.0.0.1"), server.serverPort());
    require(waitUntil([&] { return client.isConnected() && server.hasPendingConnections(); }), "disconnect reentry connects");
    QTcpSocket *oldPeer = server.nextPendingConnection();
    require(client.connectDevice(SkyDeviceId::Epsilon) != 0 && client.connectDevice(SkyDeviceId::Ptb) != 0,
            "two pending operations established");
    int disconnectedLogs = 0;
    bool reconnected = false;
    QObject::connect(&client, &SkyLocalIpcClient::logRecordGenerated, [&](const LogRecord& record) {
        if (record.fields.value(QStringLiteral("event")) != QStringLiteral("sky_ipc_command_disconnected")) return;
        ++disconnectedLogs;
        if (reconnected) return;
        reconnected = true;
        client.connectToCore(QStringLiteral("127.0.0.1"), server.serverPort());
        require(waitUntil([&] { return client.isConnected() && server.hasPendingConnections(); }), "reconnect inside disconnect log");
    });
    oldPeer->disconnectFromHost();
    require(waitUntil([&] { return reconnected && disconnectedLogs == 2; }), "all original pending operations logged");
    require(client.isConnected() && !states.isEmpty() && states.last(), "old disconnect cannot overwrite new connected state");
    client.disconnectFromCore();
}

}  // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    testReentrantReconnectDuringCommandLog();
    testSequenceWrapAndExhaustion();
    {
        using namespace VaporView;
        SkyLocalIpcClient client;
        QVector<LogRecord> records;
        QObject::connect(&client, &SkyLocalIpcClient::logRecordGenerated, [&](const LogRecord& record) {
            records.push_back(record);
        });
        for (quint16 i = 1; i <= 257; ++i) {
            CommandMessage command;
            command.command_id = CommandId::SetSkyConfig;
            command.command_seq = i;
            command.correlation_id = QStringLiteral("cache-%1").arg(i);
            command.session_id = QStringLiteral("session-%1").arg(i);
            SkyLocalIpcClientTestAccess::remember(client, command, false);
        }
        require(records.size() == 1 && records[0].fields.value(QStringLiteral("event")) == QStringLiteral("sky_ipc_config_result_context_evicted") &&
                records[0].correlation_id == QStringLiteral("cache-%1").arg(records[0].fields.value(QStringLiteral("command_seq")).toUInt()) &&
                records[0].session_id == QStringLiteral("session-%1").arg(records[0].fields.value(QStringLiteral("command_seq")).toUInt()),
                "capacity eviction records the matching configuration context");
        require(waitUntil([&] {
            for (const auto& record : records)
                if (record.fields.value(QStringLiteral("event")) == QStringLiteral("sky_ipc_config_result_context_expired"))
                    return record.correlation_id == QStringLiteral("cache-%1").arg(record.fields.value(QStringLiteral("command_seq")).toUInt()) &&
                           record.session_id == QStringLiteral("session-%1").arg(record.fields.value(QStringLiteral("command_seq")).toUInt());
            return false;
        }, 35000), "configuration context timeout retains its session");
        SkyLocalIpcClientTestAccess::clear(client);
        records.clear();
        CommandMessage completed;
        completed.command_id = CommandId::SetSkyConfig;
        completed.command_seq = 500;
        completed.correlation_id = QStringLiteral("already-completed");
        SkyLocalIpcClientTestAccess::remember(client, completed, true);
        for (quint16 i = 1000; i < 1256; ++i) {
            completed.command_seq = i;
            SkyLocalIpcClientTestAccess::remember(client, completed, true);
        }
        require(records.isEmpty(), "eviction of completed results is not reported as a missing result");
    }
    testReconnectDuringDisconnectLog();

    QTcpServer server;
    require(server.listen(QHostAddress::LocalHost, 0), "listen localhost");

    QVector<VaporView::LogRecord> commandLogs;
    QVector<VaporView::LogRecord> configLogs;
    VaporView::SkyLocalIpcClient client;
    client.setAutoReconnectEnabled(true, 50);
    bool connected = false;
    bool ackReceived = false;
    int acknowledgementCount = 0;
    VaporView::CommandAck lastAcknowledgement;
    bool statusReceived = false;
    bool logReceived = false;
    bool dashboardDeviceDataReceived = false;
    bool localStructuredLogReceived = false;
    QObject::connect(&client, &VaporView::SkyLocalIpcClient::connectedChanged, [&](bool value) {
        connected = value;
    });
    QObject::connect(&client, &VaporView::SkyLocalIpcClient::logRecordGenerated,
                     [&](const VaporView::LogRecord& record) {
                         if (record.category == QStringLiteral("ipc.command")) commandLogs.push_back(record);
                         if (record.category == QStringLiteral("ipc.config")) configLogs.push_back(record);
                         if (record.source == QStringLiteral("SkyTui") &&
                             record.level == VaporView::LogLevel::Info &&
                             record.category == QStringLiteral("ipc.connection") &&
                             record.message == QStringLiteral("SkyCore IPC 已连接。") &&
                             record.fields.value(QStringLiteral("ui_visibility")).toString() ==
                                 QStringLiteral("details") &&
                             record.fields.value(QStringLiteral("event")).toString() ==
                                 QStringLiteral("sky_ipc_connected"))
                         {
                             localStructuredLogReceived = true;
                         }
                     });
    QObject::connect(&client, &VaporView::SkyLocalIpcClient::ackReceived, [&](const VaporView::CommandAck& ack) {
        ++acknowledgementCount;
        lastAcknowledgement = ack;
        ackReceived = ack.command_id == VaporView::CommandId::RequestStatus &&
                      ack.error_code == VaporView::CommandErrorCode::Ok;
    });
    QObject::connect(&client, &VaporView::SkyLocalIpcClient::statusReceived, [&](const VaporView::TelemetryStatus& status) {
        statusReceived = status.session_name == QStringLiteral("ipc-test");
    });
    QObject::connect(&client, &VaporView::SkyLocalIpcClient::logRecordReceived, [&](const VaporView::LogRecord& record) {
        logReceived = record.level == VaporView::LogLevel::Warning &&
                      record.source == QStringLiteral("SkyCore") &&
                      record.category == QStringLiteral("ipc") &&
                      record.message == QStringLiteral("IPC log test") &&
                      record.fields.value(QStringLiteral("event")).toString() ==
                          QStringLiteral("sky_ipc_test_log");
    });
    QObject::connect(&client, &VaporView::SkyLocalIpcClient::dashboardUpdated, [&]() {
        const VaporView::SkyDashboardSnapshot dashboard = client.dashboardSnapshot();
        dashboardDeviceDataReceived =
            dashboard.temperature_controller.valid &&
            std::abs(dashboard.temperature_controller.channels[0].target_temperature_c - 31.0) < 0.001 &&
            dashboard.ai8_temperature_controller.valid &&
            std::abs(dashboard.ai8_temperature_controller.measuredC[7] - 27.5) < 0.001;
    });

    require(client.connectDevice(VaporView::SkyDeviceId::Epsilon) == 0, "disconnected business command is not sent");
    require(client.disconnectDevice(VaporView::SkyDeviceId::Epsilon) == 0, "another disconnected business command is not sent");
    require(client.requestStatus() == 0, "disconnected status query is not sent");
    require(commandLogs.size() == 3, "disconnected commands each emit a diagnostic");
    require(commandLogs[0].fields.value(QStringLiteral("event")) == QStringLiteral("sky_ipc_command_send_skipped") &&
            commandLogs[0].fields.value(QStringLiteral("reason_code")) == QStringLiteral("SKY_IPC_NOT_CONNECTED") &&
            !commandLogs[0].correlation_id.isEmpty() && commandLogs[0].session_id.isEmpty(),
            "disconnected business diagnostic carries an operation id");
    require(!commandLogs[1].correlation_id.isEmpty() && commandLogs[1].correlation_id != commandLogs[0].correlation_id,
            "separate disconnected business commands have distinct ids");
    require(commandLogs[2].correlation_id.isEmpty() && commandLogs[2].session_id.isEmpty(),
            "disconnected status query does not acquire operation context");
    commandLogs.clear();

    client.connectToCore(QStringLiteral("127.0.0.1"), server.serverPort());
    require(waitUntil([&]() {
        return connected && server.hasPendingConnections() && localStructuredLogReceived;
    }), "client connects and reports structured local log");

    QTcpSocket *socket = server.nextPendingConnection();
    require(socket != nullptr, "accept client");

    VaporView::TelemetryCodec decoder;
    QVector<VaporView::TelemetryFrame> receivedFrames;
    require(waitUntil([&]() {
        const QByteArray bytes = socket->readAll();
        if (!bytes.isEmpty())
        {
            receivedFrames += decoder.feedBytes(bytes);
        }
        return !receivedFrames.isEmpty();
    }), "receive command frame");

    VaporView::CommandMessage request;
    bool sawRequestStatus = false;
    for (const VaporView::TelemetryFrame& frame : receivedFrames)
    {
        if (frame.type == VaporView::MsgType::Command &&
            VaporView::TelemetryCodec::parseCommand(frame.payload, request) &&
            request.command_id == VaporView::CommandId::RequestStatus)
        {
            sawRequestStatus = true;
            break;
        }
    }
    require(sawRequestStatus, "client sends RequestStatus command");

    VaporView::TelemetryCodec encoder;
    VaporView::CommandAck ack;
    ack.command_id = VaporView::CommandId::RequestStatus;
    ack.command_seq = request.command_seq;
    ack.error_code = VaporView::CommandErrorCode::Ok;
    socket->write(encoder.encodeFrame(VaporView::MsgType::CommandAck,
                                      VaporView::TelemetryCodec::serializeCommandAck(ack),
                                      1,
                                      nowUs()));

    VaporView::TelemetryStatus status;
    status.session_name = QStringLiteral("ipc-test");
    status.recording_state = 1;
    socket->write(encoder.encodeFrame(VaporView::MsgType::TelemetryStatus,
                                      VaporView::TelemetryCodec::serializeTelemetryStatus(status),
                                      2,
                                      nowUs()));
    VaporView::LogRecord logRecord;
    logRecord.timestamp_us = nowUs();
    logRecord.level = VaporView::LogLevel::Warning;
    logRecord.source = QStringLiteral("SkyCore");
    logRecord.category = QStringLiteral("ipc");
    logRecord.message = QStringLiteral("IPC log test");
    logRecord.fields.insert(QStringLiteral("event"), QStringLiteral("sky_ipc_test_log"));
    socket->write(encoder.encodeFrame(VaporView::MsgType::LogEvent,
                                      VaporView::TelemetryCodec::serializeLogRecord(logRecord),
                                      3,
                                      nowUs()));
    VaporView::TemperatureControllerData rd105;
    rd105.valid = true;
    rd105.channels[0].target_temperature_c = 31.0;
    rd105.channels[0].measured_temperature_c = 29.5;
    socket->write(encoder.encodeFrame(VaporView::MsgType::TemperatureControllerStatus,
                                      VaporView::TelemetryCodec::serializeTemperatureControllerStatus(rd105),
                                      4,
                                      nowUs()));
    VaporView::Ai8TemperatureControllerProtocol::LiveData ai8;
    ai8.valid = true;
    ai8.controlStatesValid = true;
    for (int index = 0; index < VaporView::Ai8TemperatureControllerProtocol::kChannelCount; ++index)
    {
        ai8.measuredC[static_cast<size_t>(index)] = 20.5 + index;
    }
    socket->write(encoder.encodeFrame(VaporView::MsgType::Ai8TemperatureControllerStatus,
                                      VaporView::TelemetryCodec::serializeAi8TemperatureControllerStatus(ai8),
                                      5,
                                      nowUs()));
    socket->flush();

    require(waitUntil([&]() { return ackReceived && statusReceived && logReceived && dashboardDeviceDataReceived; }),
            "client parses ack, status, log and temperature dashboard frames");

    const quint16 configSeqA = client.setConfig(VaporView::SkyConfig::defaults());
    const quint16 configSeqB = client.setConfig(VaporView::SkyConfig::defaults());
    require(configSeqA != 0 && configSeqB != 0 && configSeqA != configSeqB, "two config commands sent");
    VaporView::CommandMessage configA, configB;
    require(waitUntil([&]() {
        const auto frames = decoder.feedBytes(socket->readAll());
        for (const auto& frame : frames) {
            VaporView::CommandMessage command;
            if (frame.type != VaporView::MsgType::Command || !VaporView::TelemetryCodec::parseCommand(frame.payload, command)) continue;
            if (command.command_seq == configSeqA) configA = command;
            if (command.command_seq == configSeqB) configB = command;
        }
        return !configA.correlation_id.isEmpty() && !configB.correlation_id.isEmpty();
    }), "read both config requests");
    require(configA.config_result_context_requested && configB.config_result_context_requested &&
            configA.config_ack_session_omission_supported && configB.config_ack_session_omission_supported &&
            configA.correlation_id != configB.correlation_id, "config requests negotiate distinct contexts");
    VaporView::CommandAck configAck;
    configAck.command_id = VaporView::CommandId::SetSkyConfig;
    configAck.command_seq = configSeqA;
    configAck.correlation_id = configA.correlation_id;
    configAck.session_id = QStringLiteral("session-A");
    configAck.error_code = VaporView::CommandErrorCode::Ok;
    socket->write(encoder.encodeFrame(VaporView::MsgType::CommandAck,
        VaporView::TelemetryCodec::serializeCommandAck(configAck), 7, nowUs()));
    const QJsonObject resultA{{QStringLiteral("success"), true}, {QStringLiteral("correlation_id"), QStringLiteral("business-id")},
                              {QStringLiteral("session_id"), QStringLiteral("business-session")}};
    const QJsonObject resultB{{QStringLiteral("success"), false}, {QStringLiteral("command_seq"), QStringLiteral("business-sequence")}};
    socket->write(encoder.encodeFrame(VaporView::MsgType::SkyConfigApplyResult,
        QJsonDocument(resultA).toJson(QJsonDocument::Compact), 8, nowUs()));
    socket->write(encoder.encodeFrame(VaporView::MsgType::SkyConfigApplyResultContext,
        VaporView::TelemetryCodec::serializeSkyConfigApplyResultContext(resultB, QStringLiteral("wrong-id"), QStringLiteral("session-B"), configSeqB), 81, nowUs()));
    socket->write(encoder.encodeFrame(VaporView::MsgType::SkyConfigApplyResultContext,
        VaporView::TelemetryCodec::serializeSkyConfigApplyResultContext(resultA, configA.correlation_id, QStringLiteral("wrong-session"), configSeqA), 82, nowUs()));
    socket->write(encoder.encodeFrame(VaporView::MsgType::SkyConfigApplyResultContext,
        VaporView::TelemetryCodec::serializeSkyConfigApplyResultContext(resultB, configB.correlation_id, QStringLiteral("session-B"), configSeqB), 9, nowUs()));
    socket->write(encoder.encodeFrame(VaporView::MsgType::SkyConfigApplyResultContext,
        VaporView::TelemetryCodec::serializeSkyConfigApplyResultContext(resultA, configA.correlation_id, QStringLiteral("session-A"), configSeqA), 10, nowUs()));
    socket->flush();
    require(waitUntil([&]() { return configLogs.size() == 3; }), "legacy and reversed contextual results received");
    require(configLogs[0].fields.value(QStringLiteral("event")) == QStringLiteral("sky_ipc_config_apply_result_received") &&
            configLogs[0].correlation_id.isEmpty(), "legacy result does not infer context");
    require(configLogs[1].correlation_id == configB.correlation_id && configLogs[1].session_id == QStringLiteral("session-B") &&
            configLogs[1].fields.value(QStringLiteral("command_seq")).toUInt() == configSeqB &&
            QJsonDocument::fromJson(configLogs[1].fields.value(QStringLiteral("config_apply_result")).toString().toUtf8()).object() == resultB,
            "result without ACK retains its own context and business fields");
    require(configLogs[2].correlation_id == configA.correlation_id && configLogs[2].session_id == QStringLiteral("session-A") &&
            configLogs[2].fields.value(QStringLiteral("command_seq")).toUInt() == configSeqA &&
            QJsonDocument::fromJson(configLogs[2].fields.value(QStringLiteral("config_apply_result")).toString().toUtf8()).object() == resultA,
            "result after ACK keeps its own context without overwriting business fields");
    configAck.command_seq = configSeqB;
    configAck.correlation_id = configB.correlation_id;
    configAck.session_id = QStringLiteral("session-B");
    socket->write(encoder.encodeFrame(VaporView::MsgType::CommandAck,
        VaporView::TelemetryCodec::serializeCommandAck(configAck), 11, nowUs()));
    socket->flush();
    require(waitUntil([&]() {
        for (const auto& record : commandLogs)
            if (record.fields.value(QStringLiteral("event")) == QStringLiteral("sky_ipc_command_ack") &&
                record.correlation_id == configB.correlation_id && record.session_id == QStringLiteral("session-B")) return true;
        return false;
    }), "late ACK after config result keeps the operation identity");

    const quint16 configSeqLong = client.setConfig(VaporView::SkyConfig::defaults());
    VaporView::CommandMessage configLong;
    require(waitUntil([&]() {
        for (const auto& frame : decoder.feedBytes(socket->readAll())) {
            VaporView::CommandMessage command;
            if (frame.type == VaporView::MsgType::Command && VaporView::TelemetryCodec::parseCommand(frame.payload, command) &&
                command.command_seq == configSeqLong) configLong = command;
        }
        return !configLong.correlation_id.isEmpty();
    }), "read config request for long-session ACK");
    const QString longSession(1300, QLatin1Char('x'));
    VaporView::CommandAck longAck;
    longAck.command_id = VaporView::CommandId::SetSkyConfig;
    longAck.command_seq = configSeqLong;
    longAck.correlation_id = configLong.correlation_id;
    longAck.session_id = longSession;
    const QByteArray encodedLongAck = VaporView::TelemetryCodec::serializeCommandAck(longAck);
    VaporView::CommandAck parsedLongAck;
    require(VaporView::TelemetryCodec::parseCommandAck(encodedLongAck, parsedLongAck) &&
            parsedLongAck.correlation_id.isEmpty(), "long session produces legacy-format ACK");
    socket->write(encoder.encodeFrame(VaporView::MsgType::CommandAck, encodedLongAck, 111, nowUs()));
    socket->write(encoder.encodeFrame(VaporView::MsgType::SkyConfigApplyResultContext,
        VaporView::TelemetryCodec::serializeSkyConfigApplyResultContext(resultB, QStringLiteral("wrong-id"), longSession, configSeqLong), 112, nowUs()));
    socket->write(encoder.encodeFrame(VaporView::MsgType::SkyConfigApplyResultContext,
        VaporView::TelemetryCodec::serializeSkyConfigApplyResultContext(resultB, configLong.correlation_id, longSession, configSeqLong), 113, nowUs()));
    socket->flush();
    require(waitUntil([&]() { return configLogs.size() == 4; }), "result survives legacy ACK with long session");
    require(configLogs.last().correlation_id == configLong.correlation_id && configLogs.last().session_id == longSession,
            "long-session result preserves request identity and authoritative session");

    const quint16 configSeqC = client.setConfig(VaporView::SkyConfig::defaults());
    VaporView::CommandMessage configC;
    require(waitUntil([&]() {
        for (const auto& frame : decoder.feedBytes(socket->readAll())) {
            VaporView::CommandMessage command;
            if (frame.type == VaporView::MsgType::Command && VaporView::TelemetryCodec::parseCommand(frame.payload, command) &&
                command.command_seq == configSeqC) configC = command;
        }
        return !configC.correlation_id.isEmpty();
    }), "read config request before disconnect");
    socket->write(encoder.encodeFrame(VaporView::MsgType::SkyConfigApplyResultContext,
        VaporView::TelemetryCodec::serializeSkyConfigApplyResultContext(resultB, configC.correlation_id, QStringLiteral("session-C"), configSeqC), 12, nowUs()));
    socket->flush();
    require(waitUntil([&]() { return configLogs.size() == 5; }), "config result completes before its ACK");

    const quint16 awaitingSeq = client.setConfig(VaporView::SkyConfig::defaults());
    VaporView::CommandMessage awaitingConfig;
    require(waitUntil([&]() {
        for (const auto& frame : decoder.feedBytes(socket->readAll())) {
            VaporView::CommandMessage command;
            if (frame.type == VaporView::MsgType::Command && VaporView::TelemetryCodec::parseCommand(frame.payload, command) &&
                command.command_seq == awaitingSeq) awaitingConfig = command;
        }
        return !awaitingConfig.correlation_id.isEmpty();
    }), "read config waiting for result");
    configAck.command_seq = awaitingSeq;
    configAck.correlation_id = awaitingConfig.correlation_id;
    configAck.session_id = QStringLiteral("awaiting-session");
    configAck.error_code = VaporView::CommandErrorCode::ConfigInvalid;
    configAck.config_result_context_expected = true;
    socket->write(encoder.encodeFrame(VaporView::MsgType::CommandAck,
        VaporView::TelemetryCodec::serializeCommandAck(configAck), 120, nowUs()));
    socket->flush();
    require(waitUntil([&]() { return VaporView::SkyLocalIpcClientTestAccess::hasResult(client, awaitingSeq); }),
            "config ACK received before disconnect");

    const quint16 noResultSeq = client.setConfig(VaporView::SkyConfig::defaults());
    VaporView::CommandMessage noResultConfig;
    require(waitUntil([&]() {
        for (const auto& frame : decoder.feedBytes(socket->readAll())) {
            VaporView::CommandMessage command;
            if (frame.type == VaporView::MsgType::Command && VaporView::TelemetryCodec::parseCommand(frame.payload, command) &&
                command.command_seq == noResultSeq) noResultConfig = command;
        }
        return !noResultConfig.correlation_id.isEmpty();
    }), "read config that has no result");
    configAck.command_seq = noResultSeq;
    configAck.correlation_id = noResultConfig.correlation_id;
    configAck.config_result_context_expected = false;
    socket->write(encoder.encodeFrame(VaporView::MsgType::CommandAck,
        VaporView::TelemetryCodec::serializeCommandAck(configAck), 121, nowUs()));
    socket->flush();
    require(waitUntil([&]() {
        for (const auto& record : commandLogs)
            if (record.fields.value(QStringLiteral("event")) == QStringLiteral("sky_ipc_command_ack") &&
                record.correlation_id == noResultConfig.correlation_id) return true;
        return false;
    }) && !VaporView::SkyLocalIpcClientTestAccess::hasResult(client, noResultSeq),
            "explicit no-result ACK does not retain a waiting context");

    const auto readConfig = [&]() {
        VaporView::CommandMessage parsed;
        const quint16 sequence = client.setConfig(VaporView::SkyConfig::defaults());
        require(waitUntil([&]() {
            for (const auto& frame : decoder.feedBytes(socket->readAll())) {
                VaporView::CommandMessage command;
                if (frame.type == VaporView::MsgType::Command && VaporView::TelemetryCodec::parseCommand(frame.payload, command) &&
                    command.command_seq == sequence) parsed = command;
            }
            return !parsed.correlation_id.isEmpty();
        }), "read negotiated config request");
        require(parsed.config_ack_session_omission_supported && parsed.extended_ack_context_requested &&
                parsed.extended_result_context_requested,
                "config request negotiates extended ACK and result context");
        return parsed;
    };
    const auto lateConfig = readConfig();
    const int beforeLateLogs = configLogs.size();
    socket->write(encoder.encodeFrame(VaporView::MsgType::SkyConfigApplyResultContext,
        VaporView::TelemetryCodec::serializeSkyConfigApplyResultContext(resultB, lateConfig.correlation_id, longSession, lateConfig.command_seq), 130, nowUs()));
    socket->flush();
    require(waitUntil([&]() { return configLogs.size() == beforeLateLogs + 1; }), "long session sidecar arrives first");
    const int beforeLateAcks = acknowledgementCount;
    VaporView::CommandAck lateAck;
    lateAck.command_id = VaporView::CommandId::SetSkyConfig;
    lateAck.command_seq = lateConfig.command_seq;
    lateAck.session_id = longSession;
    lateAck.allow_session_id_omission = true;
    socket->write(encoder.encodeFrame(VaporView::MsgType::CommandAck, VaporView::TelemetryCodec::serializeCommandAck(lateAck), 131, nowUs()));
    lateAck.correlation_id = QStringLiteral("wrong-id");
    socket->write(encoder.encodeFrame(VaporView::MsgType::CommandAck, VaporView::TelemetryCodec::serializeCommandAck(lateAck), 132, nowUs()));
    socket->flush();
    QElapsedTimer lateMismatchWait;
    lateMismatchWait.start();
    while (lateMismatchWait.elapsed() < 100) QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    require(acknowledgementCount == beforeLateAcks, "late ACKs without matching identity are rejected");
    lateAck.correlation_id = lateConfig.correlation_id;
    socket->write(encoder.encodeFrame(VaporView::MsgType::CommandAck, VaporView::TelemetryCodec::serializeCommandAck(lateAck), 133, nowUs()));
    socket->flush();
    require(waitUntil([&]() { return acknowledgementCount == beforeLateAcks + 1; }) &&
            lastAcknowledgement.session_id_omitted && lastAcknowledgement.session_id == longSession,
            "late ACK recovers verified sidecar session");

    const auto earlyConfig = readConfig();
    lateAck.command_seq = earlyConfig.command_seq;
    lateAck.correlation_id = earlyConfig.correlation_id;
    socket->write(encoder.encodeFrame(VaporView::MsgType::CommandAck, VaporView::TelemetryCodec::serializeCommandAck(lateAck), 134, nowUs()));
    socket->flush();
    require(waitUntil([&]() { return acknowledgementCount == beforeLateAcks + 2; }) &&
            lastAcknowledgement.session_id_omitted && lastAcknowledgement.session_id.isEmpty(),
            "ACK-first omitted session stays unknown");
    const int beforeEarlyLogs = configLogs.size();
    socket->write(encoder.encodeFrame(VaporView::MsgType::SkyConfigApplyResultContext,
        VaporView::TelemetryCodec::serializeSkyConfigApplyResultContext(resultB, earlyConfig.correlation_id, longSession, earlyConfig.command_seq), 135, nowUs()));
    socket->flush();
    require(waitUntil([&]() { return configLogs.size() == beforeEarlyLogs + 1; }) &&
            configLogs.last().correlation_id == earlyConfig.correlation_id && configLogs.last().session_id == longSession,
            "ACK-first sidecar supplies authoritative session");

    const auto extendedA = readConfig();
    const auto extendedB = readConfig();
    const int beforeExtended = acknowledgementCount;
    VaporView::CommandAck extendedAck;
    extendedAck.command_id = VaporView::CommandId::SetSkyConfig;
    extendedAck.command_seq = extendedA.command_seq;
    extendedAck.correlation_id = extendedB.correlation_id;
    extendedAck.session_id = longSession;
    extendedAck.extended_ack_context_allowed = true;
    extendedAck.config_result_context_expected = false;
    socket->write(encoder.encodeFrame(VaporView::MsgType::CommandAck,
        VaporView::TelemetryCodec::serializeCommandAck(extendedAck), 136, nowUs()));
    socket->flush();
    QElapsedTimer extendedMismatchWait; extendedMismatchWait.start();
    while (extendedMismatchWait.elapsed() < 100) QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    require(acknowledgementCount == beforeExtended, "extended ACK cannot cross concurrent identities");
    extendedAck.command_seq = extendedB.command_seq;
    socket->write(encoder.encodeFrame(VaporView::MsgType::CommandAck,
        VaporView::TelemetryCodec::serializeCommandAck(extendedAck), 137, nowUs()));
    socket->flush();
    require(waitUntil([&]() { return acknowledgementCount == beforeExtended + 1; }) &&
            lastAcknowledgement.extended_ack_context && lastAcknowledgement.session_id == longSession &&
            lastAcknowledgement.correlation_id == extendedB.correlation_id,
            "extended ACK preserves long session without a result sidecar");
    VaporView::SkyLocalIpcClientTestAccess::disableExtendedAck(client, extendedA.command_seq);
    extendedAck.command_seq = extendedA.command_seq;
    extendedAck.correlation_id = extendedA.correlation_id;
    socket->write(encoder.encodeFrame(VaporView::MsgType::CommandAck,
        VaporView::TelemetryCodec::serializeCommandAck(extendedAck), 138, nowUs()));
    socket->flush();
    extendedMismatchWait.restart();
    while (extendedMismatchWait.elapsed() < 100) QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    require(acknowledgementCount == beforeExtended + 1, "unnegotiated extended ACK is rejected");
    extendedAck.extended_ack_context_allowed = false;
    extendedAck.session_id.clear();
    socket->write(encoder.encodeFrame(VaporView::MsgType::CommandAck,
        VaporView::TelemetryCodec::serializeCommandAck(extendedAck), 139, nowUs()));
    socket->flush();
    require(waitUntil([&]() { return acknowledgementCount == beforeExtended + 2; }) &&
            lastAcknowledgement.correlation_id == extendedA.correlation_id,
            "legacy ACK remains accepted for the pending operation");

    const auto extendedLate = readConfig();
    const int beforeExtendedLateLogs = configLogs.size();
    socket->write(encoder.encodeFrame(VaporView::MsgType::SkyConfigApplyResultContext,
        VaporView::TelemetryCodec::serializeSkyConfigApplyResultContext(
            resultB, extendedLate.correlation_id, longSession, extendedLate.command_seq), 140, nowUs()));
    socket->flush();
    require(waitUntil([&]() { return configLogs.size() == beforeExtendedLateLogs + 1; }),
            "long-session result arrives before extended ACK");
    extendedAck.command_seq = extendedLate.command_seq;
    extendedAck.correlation_id = QStringLiteral("wrong-id");
    extendedAck.session_id = longSession;
    extendedAck.extended_ack_context_allowed = true;
    socket->write(encoder.encodeFrame(VaporView::MsgType::CommandAck,
        VaporView::TelemetryCodec::serializeCommandAck(extendedAck), 141, nowUs()));
    socket->flush();
    extendedMismatchWait.restart();
    while (extendedMismatchWait.elapsed() < 100) QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    require(acknowledgementCount == beforeExtended + 2, "late extended ACK requires its original identity");
    extendedAck.correlation_id = extendedLate.correlation_id;
    socket->write(encoder.encodeFrame(VaporView::MsgType::CommandAck,
        VaporView::TelemetryCodec::serializeCommandAck(extendedAck), 142, nowUs()));
    socket->flush();
    require(waitUntil([&]() { return acknowledgementCount == beforeExtended + 3; }) &&
            lastAcknowledgement.extended_ack_context && lastAcknowledgement.session_id == longSession,
            "late extended ACK matches the previously verified result");

    const QString v2Session(5000, QLatin1Char('s'));
    const auto v2A = readConfig();
    const auto v2B = readConfig();
    const int beforeV2Logs = configLogs.size();
    const int beforeV2Acks = acknowledgementCount;
    const auto sendV2Result = [&](const VaporView::CommandMessage& command, const QString& id, const QString& session) {
        const auto payload = VaporView::TelemetryCodec::serializeSkyConfigApplyResultContext(
            resultB, id, session, command.command_seq, true);
        require(!payload.isEmpty(), "extended config result encodes");
        socket->write(encoder.encodeFrame(VaporView::MsgType::SkyConfigApplyResultContext, payload, 143, nowUs()));
        socket->flush();
    };
    const auto sendV2Ack = [&](const VaporView::CommandMessage& command) {
        VaporView::CommandAck ack; ack.command_id = VaporView::CommandId::SetSkyConfig;
        ack.command_seq = command.command_seq; ack.correlation_id = command.correlation_id;
        ack.session_id = v2Session; ack.config_result_context_expected = true;
        ack.extended_ack_context_allowed = true;
        socket->write(encoder.encodeFrame(VaporView::MsgType::CommandAck,
            VaporView::TelemetryCodec::serializeCommandAck(ack), 144, nowUs()));
        socket->flush();
    };
    sendV2Result(v2A, v2B.correlation_id, v2Session);
    VaporView::SkyLocalIpcClientTestAccess::disableExtendedResult(client, v2A.command_seq);
    sendV2Result(v2A, v2A.correlation_id, v2Session);
    extendedMismatchWait.restart();
    while (extendedMismatchWait.elapsed() < 100) QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    require(configLogs.size() == beforeV2Logs, "wrong or unnegotiated extended results do not consume pending config");
    sendV2Result(v2B, v2B.correlation_id, v2Session);
    require(waitUntil([&]() { return configLogs.size() == beforeV2Logs + 1; }) &&
            configLogs.last().correlation_id == v2B.correlation_id && configLogs.last().session_id == v2Session,
            "result-first extended context retains session");
    sendV2Ack(v2B);
    require(waitUntil([&]() { return acknowledgementCount == beforeV2Acks + 1; }) &&
            lastAcknowledgement.session_id == v2Session, "late ACK matches extended result");
    sendV2Ack(v2A);
    require(waitUntil([&]() { return acknowledgementCount == beforeV2Acks + 2; }), "ACK-first unnegotiated request remains pending result");
    sendV2Result(v2A, v2A.correlation_id, v2Session);
    extendedMismatchWait.restart();
    while (extendedMismatchWait.elapsed() < 100) QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    require(configLogs.size() == beforeV2Logs + 1, "ACK-first cache rejects unnegotiated result");
    const auto v2C = readConfig();
    sendV2Ack(v2C);
    require(waitUntil([&]() { return acknowledgementCount == beforeV2Acks + 3; }), "ACK-first extended config is accepted");
    sendV2Result(v2C, v2C.correlation_id, v2Session + QLatin1Char('x'));
    extendedMismatchWait.restart();
    while (extendedMismatchWait.elapsed() < 100) QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    require(configLogs.size() == beforeV2Logs + 1, "ACK-first result cannot change its session");
    sendV2Result(v2C, v2C.correlation_id, v2Session);
    require(waitUntil([&]() { return configLogs.size() == beforeV2Logs + 2; }) &&
            configLogs.last().correlation_id == v2C.correlation_id && configLogs.last().session_id == v2Session,
            "ACK-first result retains extended session");

    const quint16 shutdownSeq = client.requestCoreShutdown();
    require(shutdownSeq != 0, "client sends shutdown request");
    QVector<VaporView::TelemetryFrame> shutdownFrames;
    VaporView::CommandMessage shutdownCommand;
    require(waitUntil([&]() {
        const QByteArray bytes = socket->readAll();
        if (!bytes.isEmpty())
        {
            shutdownFrames += decoder.feedBytes(bytes);
        }
        for (const VaporView::TelemetryFrame& frame : shutdownFrames)
        {
            VaporView::CommandMessage command;
            if (frame.type == VaporView::MsgType::Command &&
                VaporView::TelemetryCodec::parseCommand(frame.payload, command) &&
                command.command_id == VaporView::CommandId::ShutdownCore &&
                command.command_seq == shutdownSeq)
            {
                shutdownCommand = command;
                return true;
            }
        }
        return false;
    }), "client sends ShutdownCore command");
    require(!shutdownCommand.correlation_id.isEmpty(), "business command carries correlation id");
    const int ackCountBeforeMismatch = acknowledgementCount;
    VaporView::CommandAck wrongLegacyAck;
    wrongLegacyAck.command_id = VaporView::CommandId::SaveSkyConfig;
    wrongLegacyAck.command_seq = shutdownCommand.command_seq;
    socket->write(encoder.encodeFrame(VaporView::MsgType::CommandAck,
                                      VaporView::TelemetryCodec::serializeCommandAck(wrongLegacyAck), 6, nowUs()));
    socket->flush();
    QElapsedTimer mismatchWait;
    mismatchWait.start();
    while (mismatchWait.elapsed() < 100) QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    require(acknowledgementCount == ackCountBeforeMismatch, "legacy ACK for a different command is ignored");
    VaporView::CommandAck shutdownAck;
    shutdownAck.command_id = shutdownCommand.command_id;
    shutdownAck.command_seq = shutdownCommand.command_seq;
    shutdownAck.correlation_id = shutdownCommand.correlation_id;
    shutdownAck.error_code = VaporView::CommandErrorCode::Ok;
    socket->write(encoder.encodeFrame(VaporView::MsgType::CommandAck,
                                      VaporView::TelemetryCodec::serializeCommandAck(shutdownAck), 6, nowUs()));
    socket->flush();
    require(waitUntil([&]() {
        for (const auto& record : commandLogs)
            if (record.fields.value(QStringLiteral("event")) == QStringLiteral("sky_ipc_command_ack") &&
                record.correlation_id == shutdownCommand.correlation_id) return true;
        return false;
    }), "ack retains operation correlation id");

    const quint16 pendingSeq = client.connectDevice(VaporView::SkyDeviceId::Bmp390);
    require(pendingSeq != 0, "pending operation sent before disconnect");
    QString pendingId;
    for (const auto& record : commandLogs)
        if (record.fields.value(QStringLiteral("event")) == QStringLiteral("sky_ipc_command_started") &&
            record.fields.value(QStringLiteral("command_seq")).toUInt() == pendingSeq)
            pendingId = record.correlation_id;
    require(!pendingId.isEmpty() && pendingId != shutdownCommand.correlation_id, "concurrent operations have distinct ids");

    socket->disconnectFromHost();
    require(waitUntil([&]() { return !connected; }), "client observes disconnected socket");
    bool foundDisconnected = false;
    for (const auto& record : commandLogs)
        if (record.fields.value(QStringLiteral("event")) == QStringLiteral("sky_ipc_command_disconnected") &&
            record.correlation_id == pendingId) foundDisconnected = true;
    require(foundDisconnected, "disconnect preserves pending operation id");
    bool foundAwaitingConfig = false;
    for (const auto& record : commandLogs)
        if (record.fields.value(QStringLiteral("event")) == QStringLiteral("sky_ipc_config_result_disconnected") &&
            record.correlation_id == awaitingConfig.correlation_id && record.session_id == QStringLiteral("awaiting-session"))
            foundAwaitingConfig = true;
    require(foundAwaitingConfig, "disconnect retains context for config awaiting its result");
    for (const auto& record : commandLogs)
        require(record.fields.value(QStringLiteral("event")) != QStringLiteral("sky_ipc_config_result_disconnected") ||
                record.correlation_id != noResultConfig.correlation_id,
                "disconnect does not report an explicitly absent result");
    for (const auto& record : commandLogs)
        require((record.fields.value(QStringLiteral("event")) != QStringLiteral("sky_ipc_command_disconnected") &&
                 record.fields.value(QStringLiteral("event")) != QStringLiteral("sky_ipc_config_result_disconnected")) ||
                record.correlation_id != configC.correlation_id,
                "completed config result is not reported disconnected when ACK was lost");
    require(waitUntil([&]() { return connected && server.hasPendingConnections(); }), "client reconnects after disconnect");
    QTcpSocket *reconnectedSocket = server.nextPendingConnection();
    require(reconnectedSocket != nullptr, "accept reconnected client");
    const auto configLogCountBeforeStaleResult = configLogs.size();
    reconnectedSocket->write(encoder.encodeFrame(VaporView::MsgType::SkyConfigApplyResultContext,
        VaporView::TelemetryCodec::serializeSkyConfigApplyResultContext(resultB, configC.correlation_id, QStringLiteral("session-C"), configSeqC), 13, nowUs()));
    reconnectedSocket->flush();
    QElapsedTimer oldResultWait;
    oldResultWait.start();
    while (oldResultWait.elapsed() < 100) QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    require(configLogs.size() == configLogCountBeforeStaleResult, "old result cannot be attributed after reconnect");

    client.setAutoReconnectEnabled(false);
    client.disconnectFromCore();
    require(waitUntil([&]() { return !connected; }), "client closes after reconnect");

    std::cout << "sky_local_ipc_client_test passed\n";
    return 0;
}
