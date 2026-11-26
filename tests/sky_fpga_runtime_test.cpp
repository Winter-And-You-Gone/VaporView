#include "SkyRuntime.h"
#include "LogService.h"
#include "FpgaTelemetry.h"
#include "FpgaUsbSimulator.h"
#include "sky_fpga_test_transport.h"
#include "FailingRecordingStorage.h"
#include "shared/session/CalibrationRuntime.h"
#include <QTemporaryDir>
#include <QTcpSocket>
#include <QDateTime>
#include <QMutex>
#include <QMutexLocker>
#include <cmath>
#include <limits>

namespace VaporView {
class SkyRuntimeEpsilonContextTestAccess {
public:
    static void setFpgaTimeout(SkyDeviceManager& manager, int timeoutMs) {
        manager.ensureFpgaBackend();
        manager.fpga_backend_->setOperationTimeoutForTesting(timeoutMs);
    }
    static void deliverAi8(SkyRuntime& runtime, const Ai8TemperatureControllerProtocol::LiveData& data,
                           DeviceState state) {
        runtime.device_manager_.setState(SkyDeviceId::Ai8TemperatureController, state);
        runtime.device_manager_.handleAi8TemperatureControllerData(data);
    }
    static void resendStaleAi8(SkyRuntime& runtime) {
        runtime.device_manager_.setState(SkyDeviceId::Ai8TemperatureController, DeviceState::Error);
        runtime.device_manager_.ai8_temperature_controller_status_.last_data_time_us = 1;
        runtime.sendAi8TemperatureControllerStatus();
    }
};
class SkyRuntimeStorageFailureTestAccess {
public:
    static void storage(SkyRuntime& runtime, const std::shared_ptr<RecordingStorage>& value) {
        runtime.session_recorder_.storage_ = value;
    }
    static SkySessionRecorder& recorder(SkyRuntime& runtime) { return runtime.session_recorder_; }
    static QString waveformFileName(SkyRuntime& runtime) {
        auto& recorder = runtime.session_recorder_;
        std::lock_guard<std::recursive_mutex> recordingLock(recorder.recording_mutex_);
        std::lock_guard<std::mutex> filesLock(recorder.files_mutex_);
        return recorder.waveform_raw_file_.fileName();
    }
    static void closeLog(SkyRuntime& runtime, bool event) {
        auto& recorder = runtime.session_recorder_;
        std::lock_guard<std::recursive_mutex> recordingLock(recorder.recording_mutex_);
        std::lock_guard<std::mutex> filesLock(recorder.files_mutex_);
        (event ? recorder.event_log_file_ : recorder.error_log_file_).close();
    }
    static bool reopenLog(SkyRuntime& runtime, bool event) {
        auto& recorder = runtime.session_recorder_;
        std::lock_guard<std::recursive_mutex> recordingLock(recorder.recording_mutex_);
        std::lock_guard<std::mutex> filesLock(recorder.files_mutex_);
        return (event ? recorder.event_log_file_ : recorder.error_log_file_).open(QIODevice::WriteOnly | QIODevice::Append);
    }
};
}

static void firstStartFpgaTest()
{
    QTemporaryDir directory;
    require(directory.isValid(), "isolated unconfigured SkyCore");
    SkyRuntimeOptions options;
    options.config_path = directory.filePath(QStringLiteral("sky_config.json"));
    options.telemetry_tcp_port = 0;
    SkyRuntime runtime(options);
    runtime.setFpgaTransportForTesting(std::make_unique<ScriptedTransport>());
    require(runtime.start(), "start SkyCore without a saved config or a ground client");
    const auto config = runtime.currentConfig();
    require(config.sensor_transport_path == SensorTransportPath::FpgaRelay && config.fpga.enabled,
            "first hardware startup selects the FPGA relay");
    waitUntil([&] { return runtime.currentFpgaStatus().value("ready").toBool(); },
              "first hardware startup completes the FPGA handshake without a ground command");
    runtime.stop();
}

static void fpgaAi8PageTest(FpgaSimulatorFirmware firmware)
{
    using namespace Ai8TemperatureControllerProtocol;
    QTemporaryDir directory;
    SkyRuntimeOptions options;
    options.config_path = directory.filePath("sky.json");
    options.telemetry_tcp_port = 0;
    SkyRuntime runtime(options);
    runtime.setFpgaTransportForTesting(makeFpgaUsbSimulator(nullptr, FpgaSimulatorStartup::RunningDemo, firmware));
    require(runtime.start(), "start FPGA AI-8 simulator");
    waitUntil([&] { return runtime.currentFpgaStatus().value("ready").toBool(); }, "FPGA ready for page command");
    quint16 seq = 0;
    const auto pageCommand = [&](DeviceOperation op, const PageData& page) {
        const DeviceOperationRequest request{++seq, SkyDeviceId::Ai8TemperatureController, op, TelemetryCodec::serializeAi8PageData(page)};
        CommandMessage cmd;
        cmd.command_id = CommandId::DeviceOperation;
        cmd.command_seq = seq;
        cmd.payload = TelemetryCodec::serializeDeviceOperationRequest(request);
        std::optional<SkyCommandResult> reply;
        runtime.submitCommand(cmd, [&](const SkyCommandResult& result) { reply = result; });
        if (firmware == FpgaSimulatorFirmware::HistoricalExtensions) require(!reply.has_value(), "page is asynchronous");
        waitUntil([&] { return reply.has_value(); }, "single AI-8 page command completes", 30000);
        const auto& response = reply->device_operation_response;
        require(reply->send_device_operation_response && response.request_id == seq && response.operation == op,
                "one correlated AI-8 page response");
        PageData confirmed;
        if (firmware == FpgaSimulatorFirmware::Rd105Integrated) {
            require(response.error_code == CommandErrorCode::UnknownCommand, "0x3B firmware rejects parameter pages");
        } else {
            require(response.error_code == CommandErrorCode::Ok, qPrintable(response.error_message));
            require(TelemetryCodec::parseAi8PageData(response.payload, confirmed), "confirmed page decodes");
        }
        return confirmed;
    };
    PageData page;
    page.selection.channel = 3;
    const auto original = pageCommand(DeviceOperation::ReadParameters, page);
    page.writeFields = writeFieldBit(WriteField::Setpoint);
    page.channel.setpointC = 32.1;
    const auto confirmed = pageCommand(DeviceOperation::WriteAi8ParameterChanges, page);
    if (firmware == FpgaSimulatorFirmware::HistoricalExtensions) {
        require(confirmed.selection.channel == 3 && confirmed.channel.setpointC == 32.1 &&
                confirmed.channel.proportionalBand == original.channel.proportionalBand, "partial SP write preserves PID and reads back CH3");
    }
    runtime.stop();
}

static void fpgaAutoSaveTest()
{
    QTemporaryDir directory;
    require(directory.isValid(), "isolated FPGA autosave directory");
    SkyRuntimeOptions options;
    options.config_path = directory.filePath("sky.json");
    options.telemetry_tcp_port = 0;
    options.simulate_fpga = true;
    const auto start = [](SkyRuntime& runtime) {
        bool connected = false;
        const auto observer = QObject::connect(&runtime, &SkyRuntime::logRecord, [&](const LogRecord& record) {
            if (record.fields.value("event") == "fpga_async_connection_result")
                connected = record.fields.value("result_code", -1).toInt() == int(CommandErrorCode::Ok);
        });
        require(runtime.start(), "start autosave simulator");
        waitUntil([&] { return connected; }, "simulator connection complete");
        QObject::disconnect(observer);
    };
    int sequence = 0;
    const auto apply = [&](SkyRuntime& runtime, const QJsonObject& control) {
        DeviceOperationRequest request;
        request.request_id = ++sequence;
        request.device_id = SkyDeviceId::Fpga;
        request.operation = DeviceOperation::FpgaControl;
        request.payload = FpgaRemote::encodeControl(control);
        CommandMessage command;
        command.command_id = CommandId::DeviceOperation;
        command.command_seq = sequence;
        command.payload = TelemetryCodec::serializeDeviceOperationRequest(request);
        bool complete = false;
        runtime.submitCommand(command, [&](const SkyCommandResult& result) {
            require(result.ack.error_code == CommandErrorCode::Ok, qPrintable(result.device_operation_response.error_message));
            complete = true;
        });
        waitUntil([&] { return complete; }, "remote configuration operation completes");
        SkyConfig disk;
        require(SkyConfig::loadFromFile(options.config_path, disk), "SkyCore automatically saves into configured path");
        require(disk.fpga.configuration.toJson() == runtime.currentFpgaStatus().value("configuration").toObject(),
            "status and reloaded disk configuration agree");
        return disk.fpga.configuration;
    };
    FpgaControlConfig started;
    {
        SkyRuntime runtime(options);
        start(runtime);
        auto desired = runtime.currentConfig().fpga.configuration;
        desired.ai8.pollIntervalMs = 456;
        desired.wms[1].scanMilliHz = 42000;
        const auto partial = apply(runtime, {{"op","configure"},{"section","ai8"},{"configuration",desired.toJson()}});
        require(partial.ai8.pollIntervalMs == 456 && partial.wms[1].scanMilliHz != 42000,
            "partial configure saves only applied section");
        desired.ai8.pollIntervalMs = 999;
        started = apply(runtime, {{"op","acquisition"},{"enabled",true},{"enableDacs",true},{"configuration",desired.toJson()}});
        require(started.wms[1].scanMilliHz == 42000 && started.ai8.pollIntervalMs == 456
            && started.dac[0].minCode == FpgaDac1Calibration::MinDacCode && started.dac[0].maxCode == FpgaDac1Calibration::MaxDacCode,
            "remote start persists actual WMS, normalized limits and previous AI8 settings");
        runtime.stop();
    }
    SkyRuntime runtime(options);
    start(runtime);
    require(runtime.currentConfig().fpga.configuration.toJson() == started.toJson(), "restart restores full persisted configuration");
    apply(runtime, {{"op","acquisition"},{"enabled",true},{"enableDacs",true}});
    const auto registers = runtime.currentFpgaStatus().value("registers").toObject();
    require(registers.value(QString::number(0x2110)).toInt() == 42000 && registers.value(QString::number(0x4104)).toInt() == 1,
        "legacy acquisition uses saved waveform on new hardware session");
    const auto sensor = apply(runtime, {{"op","sensor_config"},{"source",0x46},{"poll_ms",543}});
    require(sensor.ai8.pollIntervalMs == 543, "sensor readback is saved without a separate save command");
    runtime.stop();
}

static void ai8FaultStatusTest()
{
    namespace Ai8 = VaporView::Ai8TemperatureControllerProtocol;
    QTemporaryDir directory;
    require(directory.isValid(), "isolated AI8 fault runtime fixture");
    SkyRuntimeOptions options;
    options.config_path = directory.filePath(QStringLiteral("sky.json"));
    options.simulate_fpga = true;
    SkyRuntime runtime(options);
    QVector<Ai8::LiveData> received;
    QObject::connect(&runtime, &SkyRuntime::telemetryFrameReady, [&](MsgType type, const QByteArray& payload) {
        if (type != MsgType::Ai8TemperatureControllerStatus) return;
        Ai8::LiveData data;
        require(TelemetryCodec::parseAi8TemperatureControllerStatus(payload, data), "parse AI8 fault telemetry");
        received.append(data);
    });
    Ai8::LiveData fault;
    fault.measuredC.fill(std::numeric_limits<double>::quiet_NaN());
    fault.detailedChannel = 1;
    fault.controlRaw = 1;
    fault.alarmRaw = 1;
    fault.mainStatusValid = true;
    fault.mainStatusRaw = 0x214;
    SkyRuntimeEpsilonContextTestAccess::deliverAi8(runtime, fault, DeviceState::Error);
    require(received.size() == 1 && !received.last().valid &&
                std::isnan(received.last().measuredC[0]) && received.last().detailedChannel == 1 &&
                received.last().alarmRaw == 1 && received.last().mainStatusValid &&
                received.last().mainStatusRaw == 0x214,
            "an input fault is transmitted with confirmed metadata despite having no valid PV");
    fault.alarmRaw = 0;
    fault.mainStatusRaw = 0x100;
    SkyRuntimeEpsilonContextTestAccess::deliverAi8(runtime, fault, DeviceState::Error);
    require(received.size() == 2 && !received.last().valid && received.last().mainStatusRaw == 0x100,
            "an instrument system fault reaches remote diagnostics without a valid measurement");
    Ai8::LiveData unknown;
    unknown.measuredC.fill(std::numeric_limits<double>::quiet_NaN());
    SkyRuntimeEpsilonContextTestAccess::deliverAi8(runtime, unknown, DeviceState::Error);
    require(received.size() == 3 && !received.last().valid && !received.last().mainStatusValid &&
                !received.last().alarmStatusValid && !received.last().controlStatesValid &&
                received.last().detailedChannel == 0 &&
                received.last().controlStates[0] == Ai8::ChannelControlState::Unknown,
            "a failed unknown poll clears older telemetry without claiming confirmed alarm metadata");
    SkyRuntimeEpsilonContextTestAccess::deliverAi8(runtime, fault, DeviceState::Disconnected);
    SkyRuntimeEpsilonContextTestAccess::resendStaleAi8(runtime);
    require(received.size() == 3, "disconnected or stale AI8 snapshots are not transmitted");
}

class FailingFpgaTransport final : public FpgaUsbTransport {
public:
    bool open(const QString&) override { return false; }
    void close() override {}
    bool isOpen() const override { return false; }
    qint64 read(QByteArray&, qint64) override { return 0; }
    qint64 write(const QByteArray&) override { return -1; }
    FpgaUsbDiagnostic diagnostic() const override { return {}; }
};

static void asyncConnectionLogTest()
{
    for (int scenario = 0; scenario < 3; ++scenario) {
        SkyDeviceManager manager;
        auto config = SkyConfig::defaults();
        config.fpga.enabled = true;
        config.fpga.locator = QStringLiteral("mock");
        config.sensor_transport_path = SensorTransportPath::FpgaRelay;
        manager.loadConfig(config);
        if (scenario == 1) manager.setFpgaTransportForTesting(std::make_unique<FailingFpgaTransport>());
        else {
            auto transport = std::make_unique<ScriptedTransport>();
            if (scenario == 2) transport->holdResponses = true;
            manager.setFpgaTransportForTesting(std::move(transport));
        }
        if (scenario == 2) SkyRuntimeEpsilonContextTestAccess::setFpgaTimeout(manager, 50);
        const QString id = QStringLiteral("fpga-async-connect-%1").arg(scenario);
        const QString session = QStringLiteral("session-for-fpga-connect");
        QVector<LogRecord> records;
        QObject::connect(&manager, &SkyDeviceManager::logRecord, &manager, [&](const LogRecord& record) {
            if (record.fields.value(QStringLiteral("event")) == QStringLiteral("fpga_async_connection_result")) records.push_back(record);
        });
        CommandErrorCode accepted;
        require(manager.connectDevice(SkyDeviceId::Fpga, &accepted, id, session) && accepted == CommandErrorCode::Ok,
                "asynchronous FPGA connection retains scheduling behavior");
        waitUntil([&] { return !records.isEmpty(); }, "asynchronous FPGA connection emits result");
        require(records.size() == 1 && records.first().correlation_id == id && records.first().session_id == session,
                "FPGA connection result retains original context once");
        require((records.first().fields.value(QStringLiteral("result_code")).toInt() == int(CommandErrorCode::Ok)) == (scenario == 0),
                "FPGA connection result distinguishes success from failure");
        if (scenario == 2) require(records.first().fields.value(QStringLiteral("detail")).toString().contains(QStringLiteral("timed out")),
                                   "FPGA timeout retains real diagnostic detail");
        manager.shutdown();
    }
}

static QJsonObject calibrationSnapshot()
{
    const QString fileId = QStringLiteral("11111111-1111-4111-8111-111111111111");
    const QString fileHash(64, QLatin1Char('a'));
    QJsonObject channels;
    for (int wavelength : {816, 1368}) {
        channels[QString::number(wavelength)] = QJsonObject{
            {"wavelength", wavelength}, {"channel", wavelength == 816 ? 0x31 : 0x30},
            {"model", "linear"}, {"model_version", 1},
            {"parameter_id", "22222222-2222-4222-8222-222222222222"},
            {"a", 1.0}, {"b", 0.0}, {"r_min", 0.0}, {"r_max", 10.0},
            {"c_min", 0.0}, {"c_max", 10.0}, {"r_squared", 1.0}, {"rmse", 0.0},
            {"sample_count", 2}, {"concentration_count", 2}, {"unit", "ppmv"}};
    }
    QJsonObject runtime{{"schema_version", 1}, {"kind", "water_runtime_parameters"},
        {"file_id", fileId}, {"file_sha256", fileHash}, {"peak_algorithm", "pointwise_h2_over_h1_v1"},
        {"model", "validation_linear_v1"}, {"compensation", "unconfigured"},
        {"hardware_mapping", QJsonObject{{"816", 0x31}, {"1368", 0x30}}},
        {"task_id", "33333333-3333-4333-8333-333333333333"}, {"task_name", "storage test"},
        {"created_utc", "2026-01-01T00:00:00.000Z"}, {"device_identity", QJsonObject{}}, {"channels", channels}};
    runtime["sha256"] = QString::fromLatin1(Session::runtimeCalibrationDigest(runtime));
    return {{"kind", "vaporview_session_calibration"}, {"schema_version", 1},
        {"file_id", fileId}, {"file_sha256", fileHash}, {"runtime", runtime}};
}

static void sessionLogFailureContextTest()
{
    QTemporaryDir directory;
    require(directory.isValid(), "session log failure fixture directory");
    const auto configPath = directory.filePath(QStringLiteral("sky.json"));
    require(SkyConfig::defaults().saveToFile(configPath), "save session log failure config");
    QVector<LogRecord> failures;
    QMutex failuresMutex;
    VaporView::LogService logs(QStringLiteral("SkySessionLogFailureTest"), nullptr,
                               directory.path(), directory.path());
    SkyRuntimeOptions options;
    options.config_path = configPath;
    options.telemetry_tcp_port = 0;
    options.simulate_data = true;
    SkyRuntime runtime(options);
    // The constructor installs the session log sink. Starting the devices would
    // also produce unrelated log writes while this test holds a file closed.
    auto& recorder = SkyRuntimeStorageFailureTestAccess::recorder(runtime);
    QString error;
    require(recorder.start(directory.path(), QStringLiteral("session-log-test"), 0, &error),
            "start isolated session for log failure test");
    const QString session = recorder.sessionDirectory();
    require(!session.isEmpty(), "session log failure fixture has a session");
    QObject::connect(&logs, &VaporView::LogService::recordPublished, &runtime,
                     [&](const LogRecord& record) {
        if (record.fields.value(QStringLiteral("event")) == QStringLiteral("sky_session_log_append_failed")) {
            QMutexLocker locker(&failuresMutex);
            failures.push_back(record);
        }
    }, Qt::DirectConnection);

    for (int scenario = 0; scenario < 3; ++scenario) {
        const bool eventFile = scenario == 0;
        const QString id = QStringLiteral("session-log-failure-%1").arg(scenario);
        const QString recordSession = scenario == 2 ? QString() : session;
        SkyRuntimeStorageFailureTestAccess::closeLog(runtime, eventFile);
        qsizetype previous = 0;
        {
            QMutexLocker locker(&failuresMutex);
            previous = failures.size();
        }
        logs.publish(eventFile ? LogLevel::Info : LogLevel::Warning,
                     QStringLiteral("SkyCore"), QStringLiteral("test.session"),
                     QStringLiteral("session log failure fixture"), {}, id, recordSession);
        QCoreApplication::processEvents();
        QVector<LogRecord> observed;
        {
            QMutexLocker locker(&failuresMutex);
            observed = failures;
        }
        if (observed.size() != previous + 1) {
            std::fprintf(stderr, "scenario=%d previous=%lld current=%lld expected_id=%s\n",
                         scenario, static_cast<long long>(previous), static_cast<long long>(observed.size()),
                         qPrintable(id));
            for (qsizetype index = previous; index < observed.size(); ++index) {
                const auto& record = observed.at(index);
                std::fprintf(stderr, "diagnostic[%lld] correlation_id=%s session_id=%s source=%s category=%s event=%s thread_id=%llu\n",
                             static_cast<long long>(index), qPrintable(record.correlation_id),
                             qPrintable(record.session_id), qPrintable(record.source), qPrintable(record.category),
                             qPrintable(record.fields.value(QStringLiteral("event")).toString()),
                             static_cast<unsigned long long>(record.thread_id));
            }
        }
        require(observed.size() == previous + 1, "one diagnostic and no recursive sink failure");
        const auto& failure = observed.last();
        require(failure.correlation_id == id && failure.session_id == recordSession &&
                    failure.fields.value(QStringLiteral("session_sink_failure")).toBool() &&
                    failure.fields.value(QStringLiteral("event_ok")).toBool() == !eventFile &&
                    failure.fields.value(QStringLiteral("error_ok")).toBool() == eventFile,
                "session sink diagnostic retains context and actual failing file");
        require(SkyRuntimeStorageFailureTestAccess::reopenLog(runtime, eventFile),
                "restore isolated session log file");
    }
    require(runtime.stopRecording(&error), "stop isolated session after log failure test");
    runtime.stop();
}

static void storageFailureContextTest(bool stale, bool writerFailure = false, bool stopBeforeRecovery = false)
{
    QTemporaryDir directory; require(directory.isValid(), "storage failure fixture directory");
    auto config = SkyConfig::defaults();
    const auto configPath = directory.filePath("sky.json");
    require(config.saveToFile(configPath), "save storage failure configuration");
    SkyRuntimeOptions options; options.config_path = configPath; options.telemetry_tcp_port = 0;
    options.simulate_fpga = true;
    SkyRuntime runtime(options);
    auto storage = std::make_shared<FailingRecordingStorage>();
    SkyRuntimeStorageFailureTestAccess::storage(runtime, storage);
    QVector<LogRecord> records;
    QObject::connect(&runtime, &SkyRuntime::logRecord, [&records](const LogRecord& record) { records.push_back(record); });
    require(runtime.start(), "start storage failure runtime");
    // The ready status can arrive before the backend releases its connect request.
    waitUntil([&] {
        for (const auto& record : records)
            if (record.fields.value("event") == "fpga_async_connection_result")
                return record.fields.value("result_code", -1).toInt() == int(CommandErrorCode::Ok);
        return false;
    }, "storage failure fixture completes the FPGA connection");
    QString error;
    auto& recorder = SkyRuntimeStorageFailureTestAccess::recorder(runtime);
    const auto snapshot = calibrationSnapshot();
    const bool snapshotValid = recorder.setCalibrationSnapshot(snapshot, &error);
    require(snapshotValid, qPrintable(error));
    const bool sessionStarted = runtime.startRecording(&error);
    require(sessionStarted, qPrintable(error));
    const QString failedSession = recorder.sessionDirectory();
    if (writerFailure) {
        const QString waveformFile = SkyRuntimeStorageFailureTestAccess::waveformFileName(runtime);
        require(!waveformFile.isEmpty(), "waveform failure target is configured");
        storage->setFailWriteFile(waveformFile);
    }
    storage->failWrites = true;
    const quint64 now = quint64(QDateTime::currentMSecsSinceEpoch()) * 1000;
    if (writerFailure) {
        FpgaWave::CompletedStream stream;
        stream.source = 0x30; stream.message = 0x1001; stream.schema = 2; stream.format = 0;
        stream.totalPoints = 1; stream.bytesPerPoint = 8; stream.rate = 10000; stream.complete = true;
        FpgaWave::CompletedStream::DliaPoint point; point.hasHarmonics = true;
        stream.dliaPoints.append(point);
        require(recorder.recordFpgaHarmonics(now, stream), "writer accepts waveform before short write");
        QElapsedTimer clock; clock.start();
        while (!recorder.storageFailed() && clock.elapsed() < 10000) QThread::msleep(1);
    } else {
        WaveformFeature feature; feature.host_time_us = now;
        recorder.recordWaveformFeature(feature);
    }
    require(recorder.storageFailed(), "real short write marks storage failure");
    if (writerFailure) require(storage->shortWriteObserved.load(), "waveform write was shortened");
    storage->failWrites = false;
    if (stale) {
        runtime.stopRecording(&error);
        error.clear();
        const bool newSnapshotValid = recorder.setCalibrationSnapshot(snapshot, &error);
        require(newSnapshotValid, qPrintable(error));
        const bool newSessionStarted = runtime.startRecording(&error);
        require(newSessionStarted, qPrintable(error));
        const QString newSession = recorder.sessionDirectory();
        require(newSession != failedSession, "new session uses another directory");
        waitUntil([&] {
            for (const auto& r : records) if (r.fields.value("event") == "recording_data_write_failed") return true;
            return false;
        }, "queued old failure observed");
        require(recorder.isRecording() && recorder.sessionDirectory() == newSession,
                "old queued failure does not stop or pause new session");
        for (const auto& r : records) {
            if (r.fields.value("event") == "recording_data_write_failed")
                require(r.session_id == failedSession && !r.correlation_id.isEmpty(), "old failure keeps old context");
            require(r.fields.value("event") != "fpga_recording_storage_failed", "old callback never recovers new session");
        }
        runtime.stopRecording(&error);
    } else {
        if (stopBeforeRecovery) {
            require(!runtime.stopRecording(&error), "failed session stops as incomplete");
            require(!recorder.isRecording() && !recorder.isPaused(), "failed session is closed before recovery");
        }
        require(QMetaObject::invokeMethod(&runtime, "sendTelemetryStatus", Qt::DirectConnection), "timer runs before queued failure");
        waitUntil([&] {
            bool failed = false, finished = false;
            for (const auto& r : records) {
                failed |= r.fields.value("event") == "recording_data_write_failed";
                finished |= r.fields.value("event") == "fpga_storage_recovery_finished";
            }
            return failed && finished;
        }, "storage recovery callback completed");
        QString id; int stages = 0;
        for (const auto& r : records) {
            const auto event = r.fields.value("event").toString();
            if (event != "recording_data_write_failed" && event != "fpga_recording_storage_failed" &&
                event != "fpga_worker_started" && event != "fpga_storage_recovery_finished") continue;
            if (event == "fpga_worker_started" && r.session_id != failedSession) continue;
            if (stopBeforeRecovery && event == "fpga_recording_storage_failed")
                require(r.message.contains(QStringLiteral("会话已结束")) && !r.message.contains(QStringLiteral("会话已暂停")),
                        "recovery describes the already closed session accurately");
            if (id.isEmpty()) id = r.correlation_id;
            require(!id.isEmpty() && r.correlation_id == id && r.session_id == failedSession,
                    "failure, worker and callback retain one session context");
            ++stages;
        }
        require(stages >= 4, "all storage recovery stages observed");
        if (!stopBeforeRecovery) runtime.stopRecording(&error);
    }
    runtime.stop();
}

int main(int argc,char **argv)
{
    QCoreApplication app(argc,argv);
    ai8FaultStatusTest();
    if (app.arguments().contains("--ai8-fault-status")) return 0;
    if (app.arguments().contains("--storage-failure-context")) {
        sessionLogFailureContextTest();
        storageFailureContextTest(false);
        storageFailureContextTest(true);
        storageFailureContextTest(false, true);
        storageFailureContextTest(false, false, true);
        return 0;
    }
    if (!app.arguments().contains("--discovery-only")) {
        fpgaAi8PageTest(FpgaSimulatorFirmware::HistoricalExtensions);
        fpgaAi8PageTest(FpgaSimulatorFirmware::Rd105Integrated);
        fpgaAutoSaveTest();
        firstStartFpgaTest();
        asyncConnectionLogTest();
    }
    QTemporaryDir directory; require(directory.isValid(),"isolated config directory");
    auto config=SkyConfig::defaults(); config.fpga.enabled=true; config.fpga.locator="mock";
    const bool discoveryOnly=app.arguments().contains("--discovery-only");
    config.sensor_transport_path=discoveryOnly ? SensorTransportPath::DirectDevices : SensorTransportPath::FpgaRelay;
    config.epsilon_rtcm.enabled=true; config.epsilon_rtcm.forward_port="FORBIDDEN_DIRECT_SERIAL";
    auto file=directory.filePath("sky.json"); require(config.saveToFile(file),"save FPGA deployment configuration");
    SkyRuntimeOptions options; options.config_path=file; options.telemetry_tcp_port=0;
    SkyRuntime runtime(options);
    quint16 telemetryPort=0;int connectedPeers=0,disconnectedPeers=0;
    QObject::connect(&runtime,&SkyRuntime::logRecord,[&](const LogRecord& record) {
        const QString event=record.fields.value("event").toString();
        if(event=="telemetry_tcp_server_listening") telemetryPort=quint16(record.fields.value("local_port").toUInt());
        if(event=="telemetry_tcp_client_connected") ++connectedPeers;
        if(event=="telemetry_tcp_client_disconnected") ++disconnectedPeers;
    });
    auto mock=std::make_unique<ScriptedTransport>();auto *wire=mock.get();
    mock->regs[0x6004]=mock->regs[0x6404]=1;
    std::vector<std::uint8_t> pressure;append16(pressure,1);append16(pressure,1);append16(pressure,0x12);
    pressure.push_back(7);pressure.push_back(4);append32(pressure,101459000);
    const auto sensor=FpgaVlp1::buildFrame(FpgaVlp1::FrameType::Data,99,0x40,0x1100,pressure,true,3,100000000,0);
    mock->sensorWire=QByteArray(reinterpret_cast<const char*>(sensor.data()),qsizetype(sensor.size()));
    std::vector<std::uint8_t> humidity;append16(humidity,1);append16(humidity,3);
    for(auto entry:{std::pair<quint16,quint32>{0x10,22000},{0x11,33000}}) {
        append16(humidity,entry.first);humidity.push_back(entry.first==0x10?7:3);humidity.push_back(4);append32(humidity,entry.second);
    }
    append16(humidity,1);humidity.push_back(3);humidity.push_back(4);append32(humidity,0);
    const auto sht=FpgaVlp1::buildFrame(FpgaVlp1::FrameType::Data,100,0x44,0x1100,humidity,true,3,100000000,0);
    mock->sensorWire.append(reinterpret_cast<const char*>(sht.data()),qsizetype(sht.size()));
    bool internalReceived=false;
    bool pressureReceived=false,fpgaSensorReceived=false;
    QObject::connect(&runtime,&SkyRuntime::telemetryFrameReady,[&](MsgType type,const QByteArray& bytes) {
        if(type==MsgType::TelemetryBasic) { TelemetryBasic data;
            if(TelemetryCodec::parseBasicTelemetry(bytes,data)) require(!(data.validity_flags&(BasicHasTemperature|BasicHasHumidity)),"SHT45 never becomes environmental HC2A telemetry");
            if(TelemetryCodec::parseBasicTelemetry(bytes,data) && (data.validity_flags&BasicHasPressure))
                pressureReceived=std::abs(data.pressure_hpa-1014.59)<0.02;
        } else if(type==MsgType::FpgaSensor) { FpgaSensor::Reading reading;
            if(FpgaRemote::parseSensor(bytes,reading)) {
                fpgaSensorReceived |= reading.source==0x40 && reading.pressurePa.has_value();
                internalReceived |= reading.source==0x44 && reading.temperatureC && reading.humidityPct;
            }
        }
    });
    runtime.setFpgaTransportForTesting(std::move(mock));
    require(runtime.start(),"start FPGA-only SkyCore");
    if(!discoveryOnly) {
        waitUntil([&]{return runtime.currentFpgaStatus().value("ready").toBool();},"Sky FPGA handshake ready");
        require(runtime.currentFpgaStatus().value("locator")=="mock" && runtime.currentFpgaStatus().value("usb").isObject(),
                "remote calibration receives stable USB connection identity");
    }
    DeviceOperationRequest discoveryRequest;discoveryRequest.request_id=100;discoveryRequest.device_id=SkyDeviceId::Fpga;
    discoveryRequest.operation=DeviceOperation::FpgaControl;discoveryRequest.payload=FpgaRemote::encodeControl({{"op","discover"}});
    CommandMessage discoveryCommand;discoveryCommand.command_id=CommandId::DeviceOperation;discoveryCommand.command_seq=100;
    discoveryCommand.payload=TelemetryCodec::serializeDeviceOperationRequest(discoveryRequest);
    bool discoveryCompleted=false;
    runtime.submitCommand(discoveryCommand,[&](const SkyCommandResult& result) {
        QJsonObject status;
        require(result.ack.error_code==CommandErrorCode::Ok && FpgaRemote::parseStatus(result.device_operation_response.payload,status),"runtime serializes USB discovery response");
        const auto devices=status.value("usb_discovery").toObject().value("devices").toArray();
        require(devices.size()==1 && devices.first().toObject().value("locator").toString()=="mock","runtime preserves Sky USB candidates");
        discoveryCompleted=true;
    },"discovery-client");
    waitUntil([&]{return discoveryCompleted;},"runtime discovery completed");
    if(discoveryOnly) {
        require(wire->openCount.load()==0 && wire->commands.isEmpty(),"runtime discovery never opens USB or sends VLP1");
        require(!runtime.currentFpgaStatus().contains("usb_discovery"),"device paths are absent from periodic Sky status");
        runtime.stop();return 0;
    }
    require(wire->openCount.load()==1,"startup owns one USB receive loop");
    DeviceOperationRequest operation;operation.request_id=7;operation.device_id=SkyDeviceId::Fpga;operation.operation=DeviceOperation::FpgaControl;
    operation.payload=FpgaRemote::encodeControl({{"op","temperature"},{"celsius",26.0}});
    CommandMessage command;command.command_id=CommandId::DeviceOperation;command.command_seq=12;
    command.payload=TelemetryCodec::serializeDeviceOperationRequest(operation);
    int completions=0;
    auto finished=[&](const SkyCommandResult& result) {
        require(result.ack.error_code==CommandErrorCode::Ok,"temperature readback succeeds");
        require(result.device_operation_response.request_id==7,"logical request ID retained");++completions;
    };
    runtime.submitCommand(command,finished,"ipc-A");
    auto retransmission=command;retransmission.command_seq=13;
    runtime.submitCommand(retransmission,[&](const SkyCommandResult& result) {
        require(result.ack.command_seq==13,"pending retransmission ACK uses its new sequence");finished(result);
    },"ipc-A");
    require(completions==0,"SkyCore ACK follows hardware final result");
    auto conflicting=operation;conflicting.payload=FpgaRemote::encodeControl({{"op","temperature"},{"celsius",27.0}});
    auto conflictCommand=command;conflictCommand.command_seq=14;
    conflictCommand.payload=TelemetryCodec::serializeDeviceOperationRequest(conflicting);
    int conflicts=0;
    auto conflict=[&](const SkyCommandResult& result) {
        require(result.ack.error_code==CommandErrorCode::InvalidPayload && result.ack.command_seq==14,
            "same request ID with changed payload rejected with current ACK sequence");++conflicts;
    };
    runtime.submitCommand(conflictCommand,conflict,"ipc-A");
    require(conflicts==1,"pending request payload conflicts are rejected before Busy handling");
    bool rejected=false;
    runtime.submitCommand(command,[&](const SkyCommandResult& result) {
        require(result.ack.error_code==CommandErrorCode::DeviceOperationBusy,"competing client scope receives Busy"); rejected=true;
    },"telemetry");
    require(rejected,"concurrent clients cannot submit second USB transaction");
    CommandMessage status;status.command_id=CommandId::RequestStatus;bool statusServed=false;
    runtime.submitCommand(status,[&](const SkyCommandResult& result){statusServed=result.ack.error_code==CommandErrorCode::Ok;});
    require(statusServed,"status stays responsive during hardware transaction");
    waitUntil([&]{return completions==2;},"duplicates complete from one operation");
    retransmission.command_seq=15;
    runtime.submitCommand(retransmission,[&](const SkyCommandResult& result) {
        require(result.ack.command_seq==15,"cached retransmission ACK uses latest sequence");finished(result);
    },"ipc-A");require(completions==3,"completed retry with new sequence reuses result cache");
    runtime.submitCommand(conflictCommand,conflict,"ipc-A");require(conflicts==2,"completed request ID cannot be reused with changed payload");
    require(wire->temperatureWrites.load()==1,"retransmitted requests do not repeat physical writes");
    waitUntil([&]{return pressureReceived && fpgaSensorReceived && internalReceived;},"FPGA pressure reaches both standardized telemetry and diagnostic sensor message");
    CommandErrorCode error;
    SkyDeviceManager manager;manager.loadConfig(config);
    auto selectionTransport=std::make_unique<ScriptedTransport>();auto *selectionWire=selectionTransport.get();
    manager.setFpgaTransportForTesting(std::move(selectionTransport));
    bool selectionConnected=false;
    manager.submitFpgaControl({{"op","connect"}},[&](CommandErrorCode code,QString,QJsonObject document) {
        require(code==CommandErrorCode::Ok&&document.value("ready").toBool(),"source selection test opens FPGA");selectionConnected=true;
    });
    waitUntil([&]{return selectionConnected;},"source selection FPGA ready");
    bool settingsApplied=false;
    manager.submitFpgaControl({{"op","sensor"},{"source",0x46},{"enabled",true},{"poll_ms",500}},[&](CommandErrorCode code,QString,QJsonObject) {
        require(code==CommandErrorCode::Ok&&manager.config().fpga.configuration.ai8.pollIntervalMs==500,
            "Sky configuration model retains confirmed AI8 polling for later source changes");settingsApplied=true;
    });
    waitUntil([&]{return settingsApplied;},"manager sensor configuration synchronized");
    config.fpga.configuration.ai8.pollIntervalMs=500;
    auto selected=config;selected.fpga.configuration.pressureSource=0x43;
    require(manager.applyConfig(selected).json.value("success").toBool(),"pressure source selection applies");
    require(manager.fpgaStatus().value("ready").toBool()&&selectionWire->openCount.load()==1&&
        manager.config().fpga.configuration.pressureSource==0x40,
        "legacy pressure selection is ignored and preserves the active FPGA USB session");
    require(!manager.receiveRtcmCorrectionData("rtcm",&error) && error==CommandErrorCode::UnknownCommand,"FPGA topology refuses unsupported independent RTCM serial path");
    require(!manager.connectDevice(SkyDeviceId::Epsilon,&error),"FPGA topology blocks direct sensor collector");
    require(telemetryPort!=0,"ephemeral telemetry port reported");
    QTcpSocket radio;TelemetryCodec radioCodec;int acknowledgements=0;
    QSet<quint16> radioConnectionSuccesses,radioConnectionFailures;
    QObject::connect(&radio,&QTcpSocket::readyRead,[&] {
        for(const auto& frame:radioCodec.feedBytes(radio.readAll())) if(frame.type==MsgType::LogEvent) {
            LogRecord record;require(TelemetryCodec::parseLogRecord(frame.payload,record),"parse actual radio connection log");
            require(record.source=="SkyCore"&&record.fields.value("event")=="fpga_device_connection_result",
                "only structured per-device FPGA results are forwarded through this log path");
            const auto source=quint16(record.fields.value("fpga_source").toUInt());
            if(record.fields.value("result")=="succeeded")radioConnectionSuccesses.insert(source);
            if(record.fields.value("result")=="failed")radioConnectionFailures.insert(source);
        } else if(frame.type==MsgType::CommandAck) {
            CommandAck ack;require(TelemetryCodec::parseCommandAck(frame.payload,ack),"parse actual radio ACK");
            require(ack.error_code==CommandErrorCode::Ok,"reconnected radio request executes instead of conflicting with old cache");
            require(ack.command_seq==21,"actual radio ACK sequence retained");++acknowledgements;
        }
    });
    radio.connectToHost("127.0.0.1",telemetryPort);
    waitUntil([&]{return connectedPeers==1;},"radio connection accepted");
    operation.request_id=1;operation.payload=FpgaRemote::encodeControl({{"op","temperature"},{"celsius",28.0}});
    command.command_seq=21;command.payload=TelemetryCodec::serializeDeviceOperationRequest(operation);
    TelemetryCodec encoder;
    radio.write(encoder.encodeFrame(MsgType::Command,TelemetryCodec::serializeCommand(command),1,1));
    waitUntil([&]{return acknowledgements==1;},"first radio controller request completes");
    radio.disconnectFromHost();waitUntil([&]{return disconnectedPeers==1;},"radio disconnect resets stream generation");
    radioCodec.reset();radio.connectToHost("127.0.0.1",telemetryPort);
    waitUntil([&]{return connectedPeers==2;},"new radio controller connects");
    operation.payload=FpgaRemote::encodeControl({{"op","temperature"},{"celsius",29.0}});
    command.payload=TelemetryCodec::serializeDeviceOperationRequest(operation);
    radio.write(encoder.encodeFrame(MsgType::Command,TelemetryCodec::serializeCommand(command),1,1));
    waitUntil([&]{return acknowledgements==2;},"new link generation may restart logical request ID at one");
    require(wire->temperatureWrites.load()==3,"new link generation executes fresh request instead of cached previous controller result");
    operation.request_id=2;operation.payload=FpgaRemote::encodeControl({{"op","connect"},{"backend","auto"},{"locator","mock"}});
    command.payload=TelemetryCodec::serializeDeviceOperationRequest(operation);
    radio.write(encoder.encodeFrame(MsgType::Command,TelemetryCodec::serializeCommand(command),1,1));
    waitUntil([&]{return acknowledgements==3&&radioConnectionSuccesses.contains(0x40)&&radioConnectionSuccesses.contains(0x44);},
        "unified FPGA reconnect sends independent PTB and SHT success logs through actual TCP");
    operation.request_id=3;operation.payload=FpgaRemote::encodeControl({{"op","sensor"},{"source",0x45},{"enabled",true}});
    command.payload=TelemetryCodec::serializeDeviceOperationRequest(operation);
    radio.write(encoder.encodeFrame(MsgType::Command,TelemetryCodec::serializeCommand(command),1,1));
    waitUntil([&]{return acknowledgements==4&&radioConnectionFailures.contains(0x45);},
        "missing sensor data sends a device-specific failure through actual TCP",11000);
    radio.disconnectFromHost();waitUntil([&]{return disconnectedPeers==2;},"test radio socket cleaned up");
    runtime.stop();
    auto producerClosed=std::make_shared<std::atomic_bool>(false);
    auto stoppedMock=std::make_unique<ScriptedTransport>();stoppedMock->destroyedClosed=producerClosed;
    runtime.setFpgaTransportForTesting(std::move(stoppedMock));
    operation.request_id=500;
    operation.payload=FpgaRemote::encodeControl({{"op","connect"},{"backend","auto"},{"locator","mock"}});
    command.payload=TelemetryCodec::serializeDeviceOperationRequest(operation);
    runtime.submitCommand(command,[](const SkyCommandResult&) {},"ipc-after-stop");
    waitUntil([&]{return runtime.currentFpgaStatus().value("ready").toBool();},
        "post-stop IPC fixture starts a USB producer before testing repeated-stop teardown");
    runtime.stop();
    require(producerClosed->load(),"stop joins and closes USB producer even when runtime was already stopped");
    auto cancelledMock=std::make_unique<ScriptedTransport>();
    QPointer<ScriptedTransport> cancelledProducer(cancelledMock.get());
    runtime.setFpgaTransportForTesting(std::move(cancelledMock));
    operation.request_id=501;command.payload=TelemetryCodec::serializeDeviceOperationRequest(operation);
    runtime.submitCommand(command,[](const SkyCommandResult&) {},"queued-ipc-after-stop");
    runtime.stop();
    require(!cancelledProducer,"immediate repeated stop destroys a producer even if its queued open was cancelled");
    QCoreApplication::processEvents();
    require(!runtime.currentFpgaStatus().value("connected").toBool() && !cancelledProducer,
        "a cancelled post-stop request cannot open the USB producer later");
    return 0;
}
