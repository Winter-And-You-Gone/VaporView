#include "SkyRuntime.h"
#include "SkyLocalIpcServer.h"
#include "LogService.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTcpSocket>
#include <QPointer>
#include <cstdlib>
#include <iostream>

namespace VaporView {
class TestRunningEpsilonCollector : public EpsilonCollector {
public:
    TestRunningEpsilonCollector() { running_.store(true); }
};
class SkyRuntimeEpsilonContextTestAccess {
public:
    static SkyDeviceManager& manager(SkyRuntime& runtime) { return runtime.device_manager_; }
    static bool startSession(SkyRuntime& runtime, const QString& directory, QString *error) {
        return runtime.session_recorder_.start(directory, QStringLiteral("test"), 0, error);
    }
    static void injectFailedReopen(SkyDeviceManager& manager) {
        manager.simulate_data_ = false;
        manager.config_.epsilon.port = QStringLiteral("COM_VAPORVIEW_CONTEXT_TEST_DOES_NOT_EXIST");
        manager.epsilon_ = std::make_shared<TestRunningEpsilonCollector>();
    }
};
class SkyRuntimeSerialDetectionTestAccess {
public:
    static void setPorts(SkyRuntime& runtime, std::function<QStringList()> ports) {
        runtime.serial_port_detection_ports_for_testing_ = std::move(ports);
    }
    static void join(SkyRuntime& runtime) {
        if (runtime.serial_port_detection_thread_.joinable()) runtime.serial_port_detection_thread_.join();
    }
    static bool running(const SkyRuntime& runtime) { return runtime.serial_port_detection_in_progress_.load(); }
    static bool deviceWorkerJoinable(const SkyRuntime& runtime) { return runtime.device_command_thread_.joinable(); }
};
}

static void require(bool value, const char *message)
{
    if (!value) { std::cerr << message << '\n'; std::exit(1); }
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir directory;
    QVector<VaporView::LogRecord> commandLogs;
    VaporView::LogService logs(QStringLiteral("command-test"), nullptr, directory.path(), directory.path());
    QObject::connect(&logs, &VaporView::LogService::recordPublished, &app,
                     [&](const VaporView::LogRecord& record) { commandLogs.push_back(record); });
    for (int scenario = 0; scenario < 4; ++scenario) {
        QTemporaryDir isolated;
        VaporView::SkyRuntimeOptions options;
        options.simulate_data = true;
        options.telemetry_tcp_port = 0;
        options.config_path = isolated.filePath(QStringLiteral("sky.json"));
        require(VaporView::SkyConfig::defaults().saveToFile(options.config_path), "save shutdown session fixture");
        VaporView::SkyRuntime runtime(options);
        require(runtime.start(), "start shutdown session fixture");
        QString session;
        if (scenario != 2) {
            QString error;
            require(VaporView::SkyRuntimeEpsilonContextTestAccess::startSession(runtime, isolated.path(), &error),
                    "start actual shutdown session");
            session = runtime.activeSessionId();
            require(!session.isEmpty(), "shutdown fixture has actual session");
            if (scenario == 1) require(runtime.pauseRecording(&error), "pause shutdown session");
        }
        commandLogs.clear();
        if (scenario == 3) {
            const VaporView::ScopedLogContext caller({QStringLiteral("shutdown-request"), QStringLiteral("stale-session")});
            runtime.stop();
        } else {
            runtime.stop();
        }
        int shutdownRecords = 0;
        for (const auto& record : commandLogs) {
            const auto event = record.fields.value(QStringLiteral("event")).toString();
            if (event == QStringLiteral("sky_recording_stop_requested") &&
                record.fields.value(QStringLiteral("reason_code")) == QStringLiteral("APPLICATION_SHUTDOWN")) {
                ++shutdownRecords;
                require(record.session_id == session && !record.correlation_id.isEmpty(),
                        "shutdown recording log captures actual session and operation");
                if (scenario == 3)
                    require(record.correlation_id == QStringLiteral("shutdown-request"), "shutdown retains caller correlation");
            }
            if (scenario != 3 && event == QStringLiteral("sky_runtime_stopped"))
                require(record.correlation_id.isEmpty() && record.session_id.isEmpty(), "shutdown context does not leak");
        }
        require(shutdownRecords == (scenario == 2 ? 0 : 1), "only open sessions receive shutdown recording log");
    }
    {
        QTemporaryDir isolated;
        const QString firstRoot = isolated.filePath(QStringLiteral("first"));
        const QString secondRoot = isolated.filePath(QStringLiteral("second"));
        require(QDir().mkpath(firstRoot) && QDir().mkpath(secondRoot), "create isolated session roots");
        VaporView::SkyRuntimeOptions options;
        options.simulate_data = true;
        options.telemetry_tcp_port = 0;
        options.config_path = isolated.filePath(QStringLiteral("sky.json"));
        require(VaporView::SkyConfig::defaults().saveToFile(options.config_path), "save delayed log fixture");
        VaporView::SkyRuntime runtime(options);
        require(runtime.start(), "start delayed log fixture");
        QString error;
        require(VaporView::SkyRuntimeEpsilonContextTestAccess::startSession(runtime, firstRoot, &error), "start first session");
        const QString first = runtime.activeSessionId();
        bool oldDelivered = false;
        const auto observer = QObject::connect(&logs, &VaporView::LogService::recordPublished, &app,
            [&](const VaporView::LogRecord& record) {
                if (record.message == QStringLiteral("old-session-delayed-marker")) oldDelivered = true;
            });
        std::thread worker([&] {
            logs.publish(VaporView::LogLevel::Warning, QStringLiteral("SkyCore"), QStringLiteral("test"),
                         QStringLiteral("old-session-delayed-marker"), {}, QStringLiteral("old-operation"), first);
            logs.publish(VaporView::LogLevel::Warning, QStringLiteral("SkyCore"), QStringLiteral("test"),
                         QStringLiteral("unbound-delayed-marker"));
        });
        worker.join();
        require(runtime.stopRecording(&error), "stop first session before queued delivery");
        require(VaporView::SkyRuntimeEpsilonContextTestAccess::startSession(runtime, secondRoot, &error), "start second session");
        const QString second = runtime.activeSessionId();
        require(!first.isEmpty() && !second.isEmpty() && first != second, "session identities differ");
        require(!oldDelivered, "worker record remains queued until second session starts");
        QCoreApplication::processEvents();
        require(oldDelivered, "queued worker record was delivered");
        QObject::disconnect(observer);
        logs.publish(VaporView::LogLevel::Warning, QStringLiteral("SkyCore"), QStringLiteral("test"),
                     QStringLiteral("current-session-marker"), {}, QStringLiteral("current-operation"), second);
        require(runtime.stopRecording(&error), "stop second session");
        for (const QString& name : {QStringLiteral("/logs/event_log.csv"), QStringLiteral("/logs/error_log.txt")}) {
            QFile file(second + name);
            require(file.open(QIODevice::ReadOnly), "open second session log");
            const QByteArray contents = file.readAll();
            require(!contents.contains("old-session-delayed-marker"), "old session record not written to new session");
            require(contents.contains("unbound-delayed-marker") && contents.contains("current-session-marker"),
                    "unbound and current session records retain existing behavior");
        }
        runtime.stop();
    }
    {
        QTemporaryDir isolated;
        VaporView::SkyRuntimeOptions options;
        options.simulate_data = true;
        options.telemetry_tcp_port = 0;
        options.config_path = isolated.filePath(QStringLiteral("sky.json"));
        auto config = VaporView::SkyConfig::defaults();
        config.epsilon.port.clear(); config.ptb.port.clear(); config.hc2a.port.clear();
        config.lidar.port.clear(); config.temperature_controller.port.clear();
        config.ai8_temperature_controller.port.clear();
        require(config.saveToFile(options.config_path), "save detection fixture");
        VaporView::SkyRuntime runtime(options);
        auto calls = std::make_shared<std::atomic_int>(0);
        auto release = std::make_shared<std::atomic_bool>(false);
        VaporView::SkyRuntimeSerialDetectionTestAccess::setPorts(runtime, [calls, release] {
            if (++*calls == 2) {
                const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds(5);
                while (!release->load() && std::chrono::steady_clock::now() < limit)
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            return QStringList{};
        });
        require(runtime.start(), "start detection fixture");
        VaporView::CommandMessage first;
        first.command_id = VaporView::CommandId::AutoDetectSerialPorts;
        first.correlation_id = QStringLiteral("detection-before-stop");
        VaporView::EpsilonMainAntennaLeverArmOperation lever;
        VaporView::DeviceOperationRequest request;
        request.request_id = 891;
        request.device_id = VaporView::SkyDeviceId::Epsilon;
        request.operation = VaporView::DeviceOperation::ConfigureEpsilonMainAntennaLeverArm;
        request.payload = VaporView::TelemetryCodec::serializeEpsilonMainAntennaLeverArmOperation(lever);
        VaporView::CommandMessage business;
        business.command_id = VaporView::CommandId::DeviceOperation;
        business.command_seq = 891;
        business.correlation_id = QStringLiteral("stop-reentry-business");
        business.payload = VaporView::TelemetryCodec::serializeDeviceOperationRequest(request);
        bool reentrantRejected = false;
        bool businessRejected = false;
        bool directBusinessRejected = false;
        QObject::connect(&runtime, &VaporView::SkyRuntime::logRecord, &app,
                         [&](const VaporView::LogRecord& record) {
            if (record.fields.value(QStringLiteral("event")) != QStringLiteral("serial_port_detection_interrupted")) return;
            const auto error = runtime.executeCommand(first).ack.error_code;
            const auto directResult = runtime.executeCommand(business);
            directBusinessRejected = directResult.ack.error_code == VaporView::CommandErrorCode::DeviceOperationBusy &&
                directResult.send_device_operation_response && directResult.device_operation_response.request_id == request.request_id;
            runtime.submitCommand(business, [&](const VaporView::SkyCommandResult& result) {
                businessRejected = result.ack.error_code == VaporView::CommandErrorCode::DeviceOperationBusy &&
                    result.ack.correlation_id == business.correlation_id && result.ack.session_id.isEmpty() &&
                    result.send_device_operation_response && result.device_operation_response.request_id == request.request_id &&
                    result.device_operation_response.error_code == VaporView::CommandErrorCode::DeviceOperationBusy &&
                    result.device_operation_response.correlation_id == business.correlation_id;
            });
            const bool started = runtime.start();
            runtime.stop();
            reentrantRejected = error != VaporView::CommandErrorCode::Ok && !started;
        }, Qt::DirectConnection);
        require(runtime.executeCommand(first).ack.error_code == VaporView::CommandErrorCode::Ok,
                "first detection starts");
        VaporView::SkyRuntimeSerialDetectionTestAccess::join(runtime);
        runtime.stop();
        require(reentrantRejected, "runtime rejects detection and startup reentry while stopping");
        require(businessRejected && directBusinessRejected, "runtime rejects direct and submitted business commands while stopping");
        require(!VaporView::SkyRuntimeSerialDetectionTestAccess::deviceWorkerJoinable(runtime),
                "stopping reentry does not start a device worker");
        require(runtime.start(), "restart detection fixture");
        VaporView::CommandMessage second = first;
        second.correlation_id = QStringLiteral("detection-after-restart");
        require(runtime.executeCommand(second).ack.error_code == VaporView::CommandErrorCode::Ok,
                "second detection starts");
        QCoreApplication::processEvents();
        const bool stillRunning = VaporView::SkyRuntimeSerialDetectionTestAccess::running(runtime);
        release->store(true);
        require(stillRunning, "stale detection completion cannot clear newer detection");
        QElapsedTimer timeout; timeout.start();
        while (VaporView::SkyRuntimeSerialDetectionTestAccess::running(runtime) && timeout.elapsed() < 5000)
            QCoreApplication::processEvents();
        require(!VaporView::SkyRuntimeSerialDetectionTestAccess::running(runtime), "new detection completes");
        bool interrupted = false, completed = false, staleCompleted = false;
        bool rejectedReceived = false, rejectedCompleted = false, rejectedWorker = false;
        for (const auto& record : commandLogs) {
            const auto event = record.fields.value(QStringLiteral("event")).toString();
            if (record.correlation_id == business.correlation_id) {
                rejectedReceived |= event == QStringLiteral("sky_command_received");
                rejectedCompleted |= event == QStringLiteral("sky_command_completed");
                rejectedWorker |= event == QStringLiteral("sky_device_worker_started");
            }
            interrupted |= event == QStringLiteral("serial_port_detection_interrupted") && record.correlation_id == first.correlation_id;
            completed |= event == QStringLiteral("serial_port_detection_completed") && record.correlation_id == second.correlation_id;
            staleCompleted |= event == QStringLiteral("serial_port_detection_completed") && record.correlation_id == first.correlation_id;
        }
        require(interrupted && completed && !staleCompleted, "detection lifecycle keeps each operation identity");
        require(rejectedReceived && rejectedCompleted && !rejectedWorker, "rejected command retains logs without starting a worker");
        runtime.stop();
    }
    for (int scenario = 0; scenario < 2; ++scenario) {
        QTemporaryDir isolated;
        VaporView::SkyRuntimeOptions options;
        options.simulate_data = true;
        options.telemetry_tcp_port = 0;
        options.config_path = isolated.filePath(QStringLiteral("sky.json"));
        require(VaporView::SkyConfig::defaults().saveToFile(options.config_path), "save command reentry fixture");
        VaporView::SkyRuntime runtime(options);
        require(runtime.start(), "start command reentry fixture");
        VaporView::EpsilonMainAntennaLeverArmOperation lever;
        VaporView::DeviceOperationRequest request;
        request.request_id = 892 + scenario;
        request.device_id = VaporView::SkyDeviceId::Epsilon;
        request.operation = VaporView::DeviceOperation::ConfigureEpsilonMainAntennaLeverArm;
        request.payload = VaporView::TelemetryCodec::serializeEpsilonMainAntennaLeverArmOperation(lever);
        VaporView::CommandMessage command;
        command.command_id = VaporView::CommandId::DeviceOperation;
        command.command_seq = request.request_id;
        command.correlation_id = QStringLiteral("stop-from-command-log-%1").arg(scenario);
        command.payload = VaporView::TelemetryCodec::serializeDeviceOperationRequest(request);
        const auto trigger = scenario == 0 ? QStringLiteral("sky_command_received")
                                           : QStringLiteral("sky_device_operation_started");
        bool stopped = false;
        bool restarted = false;
        const auto connection = QObject::connect(&logs, &VaporView::LogService::recordPublished, &runtime,
            [&](const VaporView::LogRecord& record) {
                if (stopped || record.correlation_id != command.correlation_id ||
                    record.fields.value(QStringLiteral("event")) != trigger) return;
                stopped = true;
                runtime.stop();
                if (scenario == 1) restarted = runtime.start();
            }, Qt::DirectConnection);
        bool rejected = false;
        runtime.submitCommand(command, [&](const VaporView::SkyCommandResult& result) {
            rejected = result.ack.error_code == VaporView::CommandErrorCode::DeviceOperationBusy &&
                result.ack.correlation_id == command.correlation_id && result.ack.session_id.isEmpty() &&
                result.send_device_operation_response && result.device_operation_response.request_id == request.request_id &&
                result.device_operation_response.error_code == VaporView::CommandErrorCode::DeviceOperationBusy &&
                result.device_operation_response.correlation_id == command.correlation_id;
        });
        QObject::disconnect(connection);
        require(stopped && rejected && (scenario == 0 || restarted), "stop from command logging rejects stale request");
        require(!VaporView::SkyRuntimeSerialDetectionTestAccess::deviceWorkerJoinable(runtime),
                "stale request cannot start device worker after stop or restart");
        bool received = false, completed = false, worker = false;
        for (const auto& record : commandLogs) {
            if (record.correlation_id != command.correlation_id) continue;
            const auto event = record.fields.value(QStringLiteral("event")).toString();
            received |= event == QStringLiteral("sky_command_received");
            completed |= event == QStringLiteral("sky_command_completed");
            worker |= event == QStringLiteral("sky_device_worker_started");
        }
        require(received && completed && !worker, "stale request keeps logs and never starts worker");
        if (scenario == 0) require(runtime.start(), "restart after command log stop");
        VaporView::CommandMessage status;
        status.command_id = VaporView::CommandId::RequestStatus;
        require(runtime.executeCommand(status).ack.error_code == VaporView::CommandErrorCode::Ok,
                "restarted runtime accepts new command");
        runtime.stop();
    }
    {
        QTemporaryDir isolated;
        VaporView::SkyRuntimeOptions testOptions;
        testOptions.simulate_data = true;
        testOptions.telemetry_tcp_port = 0;
        testOptions.config_path = isolated.filePath(QStringLiteral("sky.json"));
        require(VaporView::SkyConfig::defaults().saveToFile(testOptions.config_path), "save context fixture");
        VaporView::SkyRuntime testRuntime(testOptions);
        require(testRuntime.start(), "start simulated EPSILON context fixture");
        auto& manager = VaporView::SkyRuntimeEpsilonContextTestAccess::manager(testRuntime);
        require(manager.status(VaporView::SkyDeviceId::Epsilon).state == VaporView::DeviceState::Connected,
                "simulated EPSILON is connected by runtime startup");
        VaporView::SkyRuntimeEpsilonContextTestAccess::injectFailedReopen(manager);
        QString observedId, observedSession;
        bool observed = false;
        QObject::connect(&manager, &VaporView::SkyDeviceManager::deviceStatusChanged, &app,
            [&](VaporView::SkyDeviceId id, const VaporView::DeviceStatusItem& status) {
                if (id != VaporView::SkyDeviceId::Epsilon || status.state != VaporView::DeviceState::Error) return;
                const auto *context = VaporView::currentLogContext();
                observed = true;
                if (context) { observedId = context->correlationId; observedSession = context->sessionId; }
            }, Qt::DirectConnection);
        VaporView::EpsilonMainAntennaLeverArmOperation lever;
        VaporView::DeviceOperationRequest request;
        request.request_id = 890;
        request.device_id = VaporView::SkyDeviceId::Epsilon;
        request.operation = VaporView::DeviceOperation::ConfigureEpsilonMainAntennaLeverArm;
        request.payload = VaporView::TelemetryCodec::serializeEpsilonMainAntennaLeverArmOperation(lever);
        VaporView::CommandMessage command;
        command.command_id = VaporView::CommandId::DeviceOperation;
        command.command_seq = 890;
        command.correlation_id = QStringLiteral("epsilon-completion-context");
        command.payload = VaporView::TelemetryCodec::serializeDeviceOperationRequest(request);
        bool done = false;
        testRuntime.submitCommand(command, [&](const VaporView::SkyCommandResult& result) {
            require(result.ack.error_code == VaporView::CommandErrorCode::DeviceConnectFailed,
                    "invalid EPSILON test port fails to reopen");
            done = true;
        });
        QElapsedTimer timeout; timeout.start();
        while (!done && timeout.elapsed() < 5000) QCoreApplication::processEvents();
        require(done && observed && observedId == command.correlation_id && observedSession.isEmpty(),
                "EPSILON completion status retains request context without inventing a session");
        require(VaporView::currentLogContext() == nullptr, "EPSILON completion restores prior context");
        testRuntime.stop();
    }
    {
        QTemporaryDir isolated;
        VaporView::SkyRuntimeOptions options;
        options.simulate_data = true;
        options.telemetry_tcp_port = 0;
        options.config_path = isolated.filePath(QStringLiteral("sky.json"));
        require(VaporView::SkyConfig::defaults().saveToFile(options.config_path), "save interrupted operation fixture");
        VaporView::SkyRuntime runtime(options);
        require(runtime.start(), "start interrupted operation fixture");
        QString error;
        require(VaporView::SkyRuntimeEpsilonContextTestAccess::startSession(runtime, isolated.path(), &error),
                "create real session for interrupted operation");
        const QString session = runtime.activeSessionId();
        require(!session.isEmpty(), "interrupted operation fixture has a session");
        VaporView::EpsilonPacketRatesOperation rates;
        rates.output_rate_hz = 100;
        rates.callback_rate_hz = 20;
        rates.packet_rates = {{0x5C, 10}};
        VaporView::DeviceOperationRequest request;
        request.request_id = 893;
        request.device_id = VaporView::SkyDeviceId::Epsilon;
        request.operation = VaporView::DeviceOperation::ConfigureEpsilonPacketRates;
        request.payload = VaporView::TelemetryCodec::serializeEpsilonPacketRatesOperation(rates);
        VaporView::CommandMessage command;
        command.command_id = VaporView::CommandId::DeviceOperation;
        command.command_seq = 893;
        command.correlation_id = QStringLiteral("interrupted-epsilon-operation");
        command.payload = VaporView::TelemetryCodec::serializeDeviceOperationRequest(request);
        auto coalesced = command;
        coalesced.correlation_id = QStringLiteral("coalesced-epsilon-operation");
        bool oldCallback = false;
        bool rejectedDuringStop = false;
        int interrupted = 0, coalescedInterrupted = 0;
        const auto connection = QObject::connect(&logs, &VaporView::LogService::recordPublished, &runtime,
            [&](const VaporView::LogRecord& record) {
                if (record.fields.value(QStringLiteral("event")) != QStringLiteral("sky_device_result_discarded")) return;
                if (record.correlation_id == command.correlation_id) ++interrupted;
                if (record.correlation_id == coalesced.correlation_id) ++coalescedInterrupted;
                require((record.correlation_id == command.correlation_id || record.correlation_id == coalesced.correlation_id) &&
                            record.session_id == session,
                        "interrupted operation retains original context");
                runtime.stop();
                runtime.submitCommand(command, [&](const VaporView::SkyCommandResult& result) {
                    rejectedDuringStop = result.ack.error_code == VaporView::CommandErrorCode::DeviceOperationBusy;
                });
            }, Qt::DirectConnection);
        runtime.submitCommand(command, [&](const VaporView::SkyCommandResult&) { oldCallback = true; });
        runtime.submitCommand(coalesced, [&](const VaporView::SkyCommandResult&) { oldCallback = true; });
        runtime.submitCommand(coalesced, [&](const VaporView::SkyCommandResult&) { oldCallback = true; });
        runtime.stop();
        QObject::disconnect(connection);
        require(interrupted == 1 && coalescedInterrupted == 1 && rejectedDuringStop && !oldCallback,
                "stop reports each distinct coalesced operation once and retains existing callback behavior");
        require(runtime.start(), "restart after interrupted operation");
        command.command_seq++;
        command.correlation_id = QStringLiteral("independent-epsilon-operation");
        bool completed = false;
        runtime.submitCommand(command, [&](const VaporView::SkyCommandResult& result) {
            completed = result.ack.error_code == VaporView::CommandErrorCode::Ok &&
                result.ack.correlation_id == command.correlation_id && result.ack.session_id.isEmpty();
        });
        QElapsedTimer deadline; deadline.start();
        while (!completed && deadline.elapsed() < 5000) QCoreApplication::processEvents();
        require(completed && !oldCallback, "restarted operation has independent context and no stale callback");
        runtime.stop();
    }
    {
        VaporView::SkyRuntimeOptions testOptions;
        testOptions.simulate_data = true;
        testOptions.telemetry_tcp_port = 0;
        testOptions.config_path = directory.filePath(QStringLiteral("invalid-apply-sky.json"));
        require(VaporView::SkyConfig::defaults().saveToFile(testOptions.config_path), "save invalid apply fixture");
        VaporView::SkyRuntime testRuntime(testOptions);
        require(testRuntime.start(), "start invalid apply fixture");
        auto badConfig = testRuntime.currentConfig();
        badConfig.epsilon.packet_rates[0xfe] = 1;
        VaporView::CommandMessage command;
        command.command_id = VaporView::CommandId::SetSkyConfig;
        command.command_seq = 701;
        command.correlation_id = QStringLiteral("invalid-apply-result");
        command.config_result_context_requested = true;
        command.payload = QJsonDocument(badConfig.toJson()).toJson(QJsonDocument::Compact);
        VaporView::SkyCommandResult result;
        testRuntime.submitCommand(command, [&](const VaporView::SkyCommandResult& value) { result = value; });
        require(result.ack.error_code == VaporView::CommandErrorCode::ConfigInvalid &&
                result.send_config_apply_result && !result.config_apply_result_context.isEmpty() &&
                result.ack.config_result_context_expected.value_or(false) && !result.ack.allow_session_id_omission,
                "invalid applied packet rate still announces its contextual result");
        command.config_ack_session_omission_supported = true;
        testRuntime.submitCommand(command, [&](const VaporView::SkyCommandResult& value) { result = value; });
        require(result.ack.error_code == VaporView::CommandErrorCode::ConfigInvalid &&
                result.ack.allow_session_id_omission && result.ack.correlation_id == command.correlation_id,
                "config ACK omission is negotiated and does not alter retry identity");
        testRuntime.stop();
    }
    VaporView::SkyRuntimeOptions options;
    options.simulate_data = true;
    options.telemetry_tcp_port = 0;
    options.config_path = directory.filePath(QStringLiteral("sky.json"));
    auto config = VaporView::SkyConfig::defaults();
    config.temperature_controller.enabled = true;
    require(config.saveToFile(options.config_path), "save isolated enabled temperature fixture");
    VaporView::SkyRuntime runtime(options);
    require(runtime.start(), "start isolated simulated Core");
    VaporView::CommandMessage invalidConfig;
    invalidConfig.command_id = VaporView::CommandId::SetSkyConfig;
    invalidConfig.command_seq = 700;
    invalidConfig.correlation_id = QStringLiteral("invalid-config-correlation");
    invalidConfig.session_id = QStringLiteral("expired-session");
    invalidConfig.payload = QByteArrayLiteral("not-json");
    require(runtime.executeCommand(invalidConfig).ack.error_code == VaporView::CommandErrorCode::ConfigInvalid,
            "invalid configuration is rejected");
    invalidConfig.config_result_context_requested = true;
    invalidConfig.extended_ack_context_requested = true;
    VaporView::SkyCommandResult invalidResult;
    runtime.submitCommand(invalidConfig, [&](const VaporView::SkyCommandResult& result) { invalidResult = result; });
    require(invalidResult.ack.config_result_context_expected.has_value() &&
            !*invalidResult.ack.config_result_context_expected && !invalidResult.send_config_apply_result &&
            invalidResult.ack.extended_ack_context_allowed,
            "invalid config announces that no contextual result follows");
    bool foundInvalidConfig = false;
    for (const auto& record : commandLogs)
        if (record.fields.value(QStringLiteral("event")) == QStringLiteral("sky_config_apply_rejected"))
            foundInvalidConfig = record.correlation_id == invalidConfig.correlation_id && record.session_id.isEmpty();
    require(foundInvalidConfig, "configuration rejection keeps operation context without stale session");
    invalidConfig.correlation_id.clear();
    runtime.executeCommand(invalidConfig);
    require(commandLogs.last().fields.value(QStringLiteral("event")) == QStringLiteral("sky_config_apply_rejected") &&
            commandLogs.last().correlation_id.isEmpty() && commandLogs.last().session_id.isEmpty(),
            "subsequent unrelated runtime log does not inherit operation context");
    VaporView::CommandMessage configRetry;
    configRetry.command_id = VaporView::CommandId::SetSkyConfig;
    configRetry.command_seq = 64000;
    configRetry.payload = QJsonDocument(runtime.currentConfig().toJson()).toJson(QJsonDocument::Compact);
    configRetry.correlation_id = QStringLiteral("config-retry-first");
    VaporView::SkyCommandResult firstConfigResult;
    runtime.submitCommand(configRetry, [&](const VaporView::SkyCommandResult& result) { firstConfigResult = result; });
    require(firstConfigResult.ack.error_code == VaporView::CommandErrorCode::Ok &&
            firstConfigResult.send_config_apply_result && firstConfigResult.config_apply_result_context.isEmpty(),
            "legacy configuration command completes without contextual frame");
    configRetry.correlation_id = QStringLiteral("config-retry-second");
    configRetry.config_result_context_requested = true;
    configRetry.extended_ack_context_requested = true;
    configRetry.extended_result_context_requested = true;
    VaporView::SkyCommandResult retriedConfigResult;
    runtime.submitCommand(configRetry, [&](const VaporView::SkyCommandResult& result) { retriedConfigResult = result; });
    require(retriedConfigResult.ack.error_code == VaporView::CommandErrorCode::Ok &&
            retriedConfigResult.config_apply_result == firstConfigResult.config_apply_result &&
            !retriedConfigResult.config_apply_result_context.isEmpty() &&
            retriedConfigResult.ack.config_result_context_expected.value_or(false) &&
            !firstConfigResult.ack.extended_ack_context_allowed && retriedConfigResult.ack.extended_ack_context_allowed,
            "capability-only retry reuses configuration result with current context");
    QJsonObject retriedDocument;
    QString retriedId, retriedSession;
    quint16 retriedSequence = 0;
    require(VaporView::TelemetryCodec::parseSkyConfigApplyResultContext(retriedConfigResult.config_apply_result_context,
                retriedDocument, retriedId, retriedSession, retriedSequence) &&
            retriedId == configRetry.correlation_id && retriedSession.isEmpty() &&
            retriedSequence == configRetry.command_seq && retriedDocument == firstConfigResult.config_apply_result,
            "cached result carries retry context without changing business result");
    {
        auto badOptions = options;
        badOptions.config_path = directory.path();
        VaporView::SkyRuntime badRuntime(badOptions);
        VaporView::CommandMessage save;
        save.command_id = VaporView::CommandId::SaveSkyConfig;
        save.correlation_id = QStringLiteral("save-config-correlation");
        require(badRuntime.executeCommand(save).ack.error_code == VaporView::CommandErrorCode::ConfigSaveFailed,
                "saving configuration to a directory fails");
        bool foundSaveFailure = false;
        for (const auto& record : commandLogs)
            if (record.fields.value(QStringLiteral("event")) == QStringLiteral("sky_config_save_failed"))
                foundSaveFailure = record.correlation_id == save.correlation_id && record.session_id.isEmpty();
        require(foundSaveFailure, "configuration save failure keeps operation context");
    }
    runtime.connectDevice(VaporView::SkyDeviceId::Epsilon);
    VaporView::CommandMessage disconnect;
    disconnect.command_id = VaporView::CommandId::DisconnectDevice;
    disconnect.command_seq = 701;
    disconnect.correlation_id = QStringLiteral("disconnect-correlation");
    disconnect.payload = VaporView::TelemetryCodec::serializeDeviceCommand(VaporView::SkyDeviceId::Epsilon);
    runtime.submitCommand(disconnect, [](const VaporView::SkyCommandResult& result) {
        require(result.ack.error_code == VaporView::CommandErrorCode::Ok, "disconnect succeeds");
    });
    bool foundDisconnect = false;
    for (const auto& record : commandLogs)
        if (record.fields.value(QStringLiteral("event")) == QStringLiteral("device_disconnected") &&
            record.fields.value(QStringLiteral("device_id")) == VaporView::skyDeviceIdName(VaporView::SkyDeviceId::Epsilon) &&
            record.correlation_id == disconnect.correlation_id && record.session_id.isEmpty())
            foundDisconnect = true;
    require(foundDisconnect, "device disconnect log retains command correlation");
    runtime.connectDevice(VaporView::SkyDeviceId::Epsilon);
    VaporView::CommandMessage disconnectAll;
    disconnectAll.command_id = VaporView::CommandId::DisconnectAllDevices;
    disconnectAll.command_seq = 702;
    disconnectAll.correlation_id = QStringLiteral("disconnect-all-correlation");
    runtime.submitCommand(disconnectAll, [](const VaporView::SkyCommandResult& result) {
        require(result.ack.error_code == VaporView::CommandErrorCode::Ok, "disconnect all succeeds");
    });
    bool foundDisconnectAll = false;
    for (const auto& record : commandLogs)
        if (record.fields.value(QStringLiteral("event")) == QStringLiteral("device_disconnected") &&
            record.correlation_id == disconnectAll.correlation_id && record.session_id.isEmpty())
            foundDisconnectAll = true;
    require(foundDisconnectAll, "disconnect all logs retain command correlation");
    runtime.connectDevice(VaporView::SkyDeviceId::Epsilon);
    VaporView::EpsilonPacketRatesOperation rates;
    rates.output_rate_hz = 100;
    rates.callback_rate_hz = 20;
    rates.packet_rates = {{0x5C, 10}};
    VaporView::DeviceOperationRequest operation;
    operation.request_id = 77;
    operation.device_id = VaporView::SkyDeviceId::Epsilon;
    operation.operation = VaporView::DeviceOperation::ConfigureEpsilonPacketRates;
    operation.payload = VaporView::TelemetryCodec::serializeEpsilonPacketRatesOperation(rates);
    VaporView::CommandMessage command;
    command.command_id = VaporView::CommandId::DeviceOperation;
    command.command_seq = 13;
    command.payload = VaporView::TelemetryCodec::serializeDeviceOperationRequest(operation);
    command.correlation_id = QStringLiteral("async-test-correlation");
    command.session_id = QStringLiteral("test-session");
    int completions = 0;
    auto completed = [&](const VaporView::SkyCommandResult& result) {
        require(result.ack.error_code == VaporView::CommandErrorCode::Ok, "configuration completed successfully");
        require(result.ack.correlation_id == command.correlation_id && result.ack.session_id.isEmpty(),
                "async result carries request context");
        require(!result.ack.typed_response_context_supported,
                "legacy device requests retain the legacy response format");
        require(result.device_operation_response.request_id == 77, "result retains logical request identity");
        ++completions;
    };
    for (int repeat = 0; repeat < 4; ++repeat) runtime.submitCommand(command, completed);
    require(completions == 0, "device transaction never completes synchronously on the Core loop");
    auto competing = command;
    competing.command_seq++;
    runtime.submitCommand(competing, [&](const VaporView::SkyCommandResult& result) {
        require(result.ack.error_code == VaporView::CommandErrorCode::DeviceOperationBusy,
                "competing device transaction is rejected while collector is owned by worker");
    });
    VaporView::CommandMessage status;
    status.command_id = VaporView::CommandId::RequestStatus;
    bool statusCompleted = false;
    runtime.submitCommand(status, [&](const VaporView::SkyCommandResult& result) {
        statusCompleted = result.ack.error_code == VaporView::CommandErrorCode::Ok;
    });
    require(statusCompleted, "Core serves status while transaction is in flight");
    QElapsedTimer deadline;
    deadline.start();
    while (completions != 4 && deadline.elapsed() < 3000) QCoreApplication::processEvents();
    require(completions == 4, "duplicate requests share one completion");
    int receivedLogs = 0, completedLogs = 0, workerStarted = 0, workerFinished = 0;
    for (const auto& record : commandLogs)
    {
        if (record.correlation_id != command.correlation_id || !record.session_id.isEmpty() ||
            record.fields.value(QStringLiteral("command_seq")).toUInt() != command.command_seq)
            continue;
        if (record.fields.value(QStringLiteral("event")) == QStringLiteral("sky_command_received")) ++receivedLogs;
        if (record.fields.value(QStringLiteral("event")) == QStringLiteral("sky_command_completed")) ++completedLogs;
        if (record.fields.value(QStringLiteral("event")) == QStringLiteral("sky_device_worker_started")) ++workerStarted;
        if (record.fields.value(QStringLiteral("event")) == QStringLiteral("sky_device_worker_finished")) ++workerFinished;
    }
    require(receivedLogs == 4 && completedLogs == 4, "async duplicate completions preserve explicit command context");
    require(workerStarted == 1 && workerFinished == 1, "device worker preserves context without duplicate hardware execution");
    runtime.submitCommand(command, completed);
    require(completions == 5, "completed retry immediately reuses cached result");
    auto alternateContext = command;
    alternateContext.correlation_id = QStringLiteral("alternate-diagnostics");
    alternateContext.session_id.clear();
    alternateContext.typed_response_context_requested = true;
    runtime.submitCommand(alternateContext, [&](const VaporView::SkyCommandResult& result) {
        require(result.ack.error_code == VaporView::CommandErrorCode::Ok &&
                result.ack.correlation_id == alternateContext.correlation_id && result.ack.session_id.isEmpty() &&
                result.ack.typed_response_context_supported &&
                result.device_operation_response.correlation_id == alternateContext.correlation_id,
                "cached legacy completion negotiates the current caller's response context");
        ++completions;
    });
    require(completions == 6, "diagnostic metadata does not change command deduplication identity");
    auto escapedContext = alternateContext;
    escapedContext.correlation_id = QString(200, QChar(1));
    bool escapedContextCompleted = false;
    runtime.submitCommand(escapedContext, [&](const VaporView::SkyCommandResult& result) {
        require(!VaporView::TelemetryCodec::serializeDeviceOperationResponseContext(result.device_operation_response).isEmpty() &&
                !result.ack.typed_response_context_supported,
                "encodable result falls back when escaped ACK metadata exceeds its size limit");
        escapedContextCompleted = true;
    });
    require(escapedContextCompleted, "ACK metadata size fallback preserves cached completion");
    operation.request_id = 78;
    operation.operation = VaporView::DeviceOperation::ReadEpsilonSettings;
    operation.payload = VaporView::TelemetryCodec::serializeEpsilonSettingsRead(VaporView::EpsilonSettingsGroup::Installation);
    command.command_seq = 15;
    command.payload = VaporView::TelemetryCodec::serializeDeviceOperationRequest(operation);
    bool settingsCompleted = false;
    runtime.submitCommand(command, [&](const VaporView::SkyCommandResult& result) {
        require(result.ack.error_code == VaporView::CommandErrorCode::UnknownCommand,
                "simulation cannot report fabricated hardware parameter values");
        settingsCompleted = true;
    });
    VaporView::CommandMessage startRecording;
    startRecording.command_id = VaporView::CommandId::StartRecording;
    require(runtime.executeCommand(startRecording).ack.error_code == VaporView::CommandErrorCode::DeviceOperationBusy,
            "recording cannot start while a settings transaction owns the collector");
    QString recordingError;
    require(!runtime.startRecording(&recordingError), "direct recording API observes settings ownership");
    deadline.restart();
    while (!settingsCompleted && deadline.elapsed() < 3000) QCoreApplication::processEvents();
    require(settingsCompleted, "unsupported settings transaction releases collector ownership");

    operation.request_id = 79;
    operation.operation = VaporView::DeviceOperation::CalibrateEpsilonLevel;
    operation.payload = VaporView::TelemetryCodec::serializeEpsilonMaintenanceAction(VaporView::EpsilonMaintenanceAction::Level);
    command.command_seq = 16;
    command.payload = VaporView::TelemetryCodec::serializeDeviceOperationRequest(operation);
    const auto calibrationResult = runtime.executeCommand(command);
    require(calibrationResult.ack.error_code == VaporView::CommandErrorCode::UnknownCommand,
            "Core rejects calibration without a completed maintenance workflow");
    command.command_seq = 17;
    operation.request_id = 80;
    command.payload = VaporView::TelemetryCodec::serializeDeviceOperationRequest(operation);
    bool maintenanceCompleted = false;
    runtime.submitCommand(command, [&](const VaporView::SkyCommandResult& result) {
        require(result.ack.error_code == VaporView::CommandErrorCode::UnknownCommand,
                "asynchronous simulation also refuses fabricated calibration ACK");
        maintenanceCompleted = true;
    });
    require(runtime.executeCommand(startRecording).ack.error_code == VaporView::CommandErrorCode::DeviceOperationBusy,
            "recording cannot start while maintenance owns the collector");
    deadline.restart();
    while (!maintenanceCompleted && deadline.elapsed() < 3000) QCoreApplication::processEvents();
    require(maintenanceCompleted, "unsupported maintenance releases collector ownership");
    operation.operation = VaporView::DeviceOperation::CalibrateEpsilonMagnetic3D;
    operation.payload = VaporView::TelemetryCodec::serializeEpsilonMaintenanceAction(VaporView::EpsilonMaintenanceAction::Magnetic3D);
    operation.request_id = 81;
    command.command_seq = 18;
    command.payload = VaporView::TelemetryCodec::serializeDeviceOperationRequest(operation);
    bool magneticCompleted = false;
    runtime.submitCommand(command, [&](const VaporView::SkyCommandResult& result) {
        require(result.ack.error_code == VaporView::CommandErrorCode::UnknownCommand,
                "simulation cannot invent magnetic calibration completion");
        VaporView::EpsilonMaintenanceResult maintenance;
        require(VaporView::TelemetryCodec::parseEpsilonMaintenanceResult(result.device_operation_response.payload, maintenance) &&
                maintenance.action == VaporView::EpsilonMaintenanceAction::Magnetic3D,
                "unsupported magnetic result matches requested action");
        magneticCompleted = true;
    });
    VaporView::DeviceOperationRequest cancel;
    cancel.request_id = 82;
    cancel.device_id = VaporView::SkyDeviceId::Epsilon;
    cancel.operation = VaporView::DeviceOperation::CancelEpsilonMagneticCalibration;
    cancel.payload = QByteArray::fromHex("51000000");
    auto cancelCommand = command;
    cancelCommand.command_seq = 19;
    cancelCommand.payload = VaporView::TelemetryCodec::serializeDeviceOperationRequest(cancel);
    bool cancelAccepted = false;
    runtime.submitCommand(cancelCommand, [&](const VaporView::SkyCommandResult& result) {
        cancelAccepted = result.ack.error_code == VaporView::CommandErrorCode::Ok;
    });
    require(cancelAccepted && !magneticCompleted, "cancel is accepted before busy check without claiming calibration completion");
    deadline.restart();
    while (!magneticCompleted && deadline.elapsed() < 3000) QCoreApplication::processEvents();
    require(magneticCompleted, "magnetic worker final result releases ownership after cancel");

    VaporView::CommandMessage enable;
    enable.command_id = VaporView::CommandId::EnableWaveformStreaming;
    enable.command_seq = 100;
    auto apply = [&](const VaporView::CommandMessage& request, const QByteArray& client = "retry-client") {
        VaporView::CommandErrorCode error = VaporView::CommandErrorCode::InternalError;
        runtime.submitCommand(request, [&](const VaporView::SkyCommandResult& result) {
            error = result.ack.error_code;
        }, client);
        return error;
    };
    require(apply(enable) == VaporView::CommandErrorCode::Ok && runtime.waveformStreamingEnabled(),
            "ordinary mutating command executes once");
    auto disable = enable;
    disable.command_id = VaporView::CommandId::DisableWaveformStreaming;
    disable.command_seq = 101;
    require(apply(disable) == VaporView::CommandErrorCode::Ok && !runtime.waveformStreamingEnabled(),
            "new identity executes a later command");
    require(apply(enable) == VaporView::CommandErrorCode::Ok && !runtime.waveformStreamingEnabled(),
            "lost ACK retry reuses result without repeating the earlier side effect");
    disable.command_seq = enable.command_seq;
    require(apply(disable) == VaporView::CommandErrorCode::InvalidPayload,
            "one command identity cannot be reused for a different payload");
    require(apply(enable, "other-client") == VaporView::CommandErrorCode::Ok && runtime.waveformStreamingEnabled(),
            "independent clients do not share command identities");

    runtime.connectDevice(VaporView::SkyDeviceId::TemperatureController);
    VaporView::TemperatureControllerCommand temperature;
    temperature.channel = 1;
    temperature.target_temperature_c = 20;
    VaporView::CommandMessage temperatureCommand;
    temperatureCommand.command_id = VaporView::CommandId::SetTemperatureTarget;
    temperatureCommand.command_seq = 110;
    temperatureCommand.payload = VaporView::TelemetryCodec::serializeTemperatureControllerCommand(temperature);
    bool temperatureCompleted = false;
    runtime.submitCommand(temperatureCommand, [&](const VaporView::SkyCommandResult& result) {
        require(result.ack.error_code == VaporView::CommandErrorCode::Ok, "temperature transaction completes");
        temperatureCompleted = true;
    });
    require(!temperatureCompleted, "temperature control yields the Core event loop");
    statusCompleted = false;
    runtime.submitCommand(status, [&](const VaporView::SkyCommandResult& result) {
        statusCompleted = result.ack.error_code == VaporView::CommandErrorCode::Ok;
    });
    require(statusCompleted, "status remains available during temperature control");
    deadline.restart();
    while (!temperatureCompleted && deadline.elapsed() < 3000) QCoreApplication::processEvents();
    require(temperatureCompleted, "temperature completion returns on the Core event loop");

    VaporView::CommandMessage staleSession;
    staleSession.command_id = VaporView::CommandId::RequestStatus;
    staleSession.command_seq = 602;
    staleSession.correlation_id = QStringLiteral("stale-context-test");
    staleSession.session_id = QStringLiteral("stale-external-session");
    runtime.submitCommand(staleSession, [&](const VaporView::SkyCommandResult& result) {
        require(result.ack.session_id.isEmpty(), "inactive recorder ignores stale external session");
    });

    {
        using namespace VaporView;
        SkyLocalIpcServer compatibilityServer(&runtime);
        require(compatibilityServer.listen(QStringLiteral("127.0.0.1"), 0), "listen compatibility IPC server");
        QTcpSocket client;
        client.connectToHost(QStringLiteral("127.0.0.1"), compatibilityServer.serverPort());
        deadline.restart();
        while (client.state() != QAbstractSocket::ConnectedState && deadline.elapsed() < 3000)
            QCoreApplication::processEvents();
        require(client.state() == QAbstractSocket::ConnectedState, "compatibility IPC client connects");
        TelemetryCodec encoder, decoder;
        QHash<quint16, CommandAck> acknowledgements;
        QVector<TelemetryFrame> results;
        QObject::connect(&client, &QTcpSocket::readyRead, &app, [&] {
            for (const auto& frame : decoder.feedBytes(client.readAll())) {
                CommandAck ack;
                if (frame.type == MsgType::CommandAck && TelemetryCodec::parseCommandAck(frame.payload, ack))
                    acknowledgements.insert(ack.command_seq, ack);
                if (frame.type == MsgType::DeviceOperationResponse || frame.type == MsgType::DeviceOperationResponseContext)
                    results.push_back(frame);
            }
        });
        for (int index = 0; index < 4; ++index) {
            DeviceOperationRequest cancel;
            cancel.request_id = 900 + index;
            cancel.device_id = SkyDeviceId::Epsilon;
            cancel.operation = DeviceOperation::CancelEpsilonMagneticCalibration;
            cancel.payload = QByteArray::fromHex("01000000");
            CommandMessage request;
            request.command_id = CommandId::DeviceOperation;
            request.command_seq = 61000 + index;
            request.payload = TelemetryCodec::serializeDeviceOperationRequest(cancel);
            request.typed_response_context_requested = index != 0;
            if (index == 2) request.correlation_id = QString(257, QLatin1Char('x'));
            else if (index != 3) request.correlation_id = QStringLiteral("ipc-operation-%1").arg(index);
            client.write(encoder.encodeFrame(MsgType::Command, TelemetryCodec::serializeCommand(request), request.command_seq, 1));
        }
        deadline.restart();
        while ((acknowledgements.size() < 4 || results.size() < 4) && deadline.elapsed() < 3000)
            QCoreApplication::processEvents();
        require(acknowledgements.size() == 4 && results.size() == 4, "each compatibility request yields one ACK and result");
        for (int index = 0; index < 4; ++index) {
            const bool contextual = index == 1;
            require(acknowledgements.value(61000 + index).typed_response_context_supported == contextual,
                    "IPC ACK negotiates contextual results only for encodable identified requests");
            DeviceOperationResponse result;
            const auto& frame = results.at(index);
            require(frame.type == (contextual ? MsgType::DeviceOperationResponseContext : MsgType::DeviceOperationResponse) &&
                    (contextual ? TelemetryCodec::parseDeviceOperationResponseContext(frame.payload, result)
                                : TelemetryCodec::parseDeviceOperationResponse(frame.payload, result)) &&
                    result.request_id == static_cast<quint32>(900 + index) &&
                    result.error_code == CommandErrorCode::UnknownCommand &&
                    (contextual ? result.correlation_id == QStringLiteral("ipc-operation-1") : result.correlation_id.isEmpty()),
                    "IPC results preserve negotiation, operation identity and legacy format");
        }
        client.disconnectFromHost();
        compatibilityServer.close();
    }
    VaporView::SkyLocalIpcServer ipc(&runtime);
    require(ipc.listen(QStringLiteral("127.0.0.1"), 0), "listen isolated IPC server");
    QTcpSocket slowClient;
    slowClient.setReadBufferSize(1);
    slowClient.connectToHost(QStringLiteral("127.0.0.1"), ipc.serverPort());
    deadline.restart();
    while (ipc.findChildren<QTcpSocket*>().isEmpty() && deadline.elapsed() < 3000)
        QCoreApplication::processEvents();
    const auto peers = ipc.findChildren<QTcpSocket*>();
    require(peers.size() == 1, "slow IPC client is accepted");
    QPointer<QTcpSocket> acceptedPeer = peers.front();
    VaporView::DownsampledWaveform waveform;
    waveform.samples.fill(1.0f, 8192);
    waveform.original_point_count = waveform.downsampled_point_count = waveform.samples.size();
    const QByteArray waveformPayload = VaporView::TelemetryCodec::serializeDownsampledWaveform(waveform);
    for (int frame = 0; frame < 512; ++frame)
    {
        runtime.telemetryFrameReady(VaporView::MsgType::WaveformDownsampled, waveformPayload);
        require(acceptedPeer && acceptedPeer->bytesToWrite() <= 4 * 1024 * 1024,
                "IPC burst cannot grow the pending write queue past its limit");
    }
    deadline.restart();
    while (acceptedPeer && acceptedPeer->state() != QAbstractSocket::UnconnectedState && deadline.elapsed() < 3000)
        QCoreApplication::processEvents();
    require(!acceptedPeer || acceptedPeer->state() == QAbstractSocket::UnconnectedState,
            "slow IPC client is disconnected outside the broadcast traversal");
    require(runtime.isRunning(), "slow IPC peer cannot stop the Core");
    statusCompleted = false;
    runtime.submitCommand(status, [&](const VaporView::SkyCommandResult& result) {
        statusCompleted = result.ack.error_code == VaporView::CommandErrorCode::Ok;
    });
    require(statusCompleted, "Core remains responsive after IPC backpressure");
    ipc.close();
    runtime.stop();
    QCoreApplication::processEvents();
    require(runtime.start(), "restart Core with a new command lifetime");
    runtime.setWaveformStreamingEnabled(false);
    require(apply(enable) == VaporView::CommandErrorCode::Ok && runtime.waveformStreamingEnabled(),
            "same identity after Core restart cannot reuse an old cached result");
    runtime.stop();
}
