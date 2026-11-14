#include "SkyFpgaBackend.h"
#include "FpgaUsbSimulator.h"
#include "sky_fpga_test_transport.h"

void selectiveConnection()
{
    auto mock=std::make_unique<ScriptedTransport>();auto *wire=mock.get();
    mock->regs[0x6004]=1;mock->regs[0x6304]=0;mock->regs[0x4004]=1;mock->regs[0x600c]=5;
    SkyFpgaBackend backend(std::move(mock));
    bool done=false;
    backend.submit({{"op","connect"},{"backend","auto"},{"locator","mock"},{"recover",false}},
        [&](CommandErrorCode code,QString,QJsonObject status) {
            require(code==CommandErrorCode::Ok&&status.value("ready").toBool(),"Sky selective connection verifies readiness");done=true;
        });
    waitUntil([&]{return done;},"Sky selective connection completes");
    require(!wire->commands.isEmpty(),"Sky selective handshake reaches the USB worker");
    for(const auto &command:wire->commands)
        require(command.message==0xfe||command.message==1||command.message==2,"Sky forwards selective policy without any recovery writes");
    require(wire->regs.value(0x6004)==1&&wire->regs.value(0x6304)==0&&wire->regs.value(0x4004)==1&&wire->regs.value(0x600c)==5,
        "Sky selective connection preserves running modules, stopped modules and errors");
    backend.shutdown();
}

void diagnosticReadback()
{
    auto mock=std::make_unique<ScriptedTransport>();auto *wire=mock.get();
    mock->regs[0x5024]=0;
    SkyFpgaBackend backend(std::move(mock));
    const auto operation=[&](const QJsonObject& request,CommandErrorCode expected) {
        bool done=false;QJsonObject result;
        backend.submit(request,[&](CommandErrorCode code,QString,QJsonObject status) {
            require(code==expected,"diagnostic operation returns the expected final result");result=status;done=true;
        });
        waitUntil([&]{return done;},"diagnostic operation completes");return result;
    };
    operation({{"op","refresh"}},CommandErrorCode::DeviceNotConnected);
    auto status=operation({{"op","connect"},{"backend","auto"},{"locator","mock"}},CommandErrorCode::ConfigApplyFailed);
    require(status.value("connected").toBool()&&!status.value("ready").toBool(),"failed handshake retains the USB connection for read-only diagnostics");
    wire->clear();wire->regs[0x6010]=9600;
    status=operation({{"op","refresh"}},CommandErrorCode::ConfigApplyFailed);
    require(status.value("registers").toObject().value(QString::number(0x6010)).toInt()==9600,
        "Sky refresh reads fresh registers after a failed handshake, just like local refresh");
    require(!status.value("ready").toBool()&&!wire->commands.isEmpty(),"diagnostic readback cannot bypass failed connection recovery");
    for(const auto& command:wire->commands)require(command.message==2,"diagnostic refresh sends only register reads");
    const auto commandCount=wire->commands.size();
    operation({{"op","sensor_config"},{"source",0x40},{"baud",4800}},CommandErrorCode::DeviceNotConnected);
    require(wire->commands.size()==commandCount,"unready Sky controller still blocks parameter writes");
    wire->regs[0x5024]=0x00040101;
    status=operation({{"op","refresh"}},CommandErrorCode::ConfigApplyFailed);
    require(!status.value("ready").toBool()&&wire->openCount.load()==1,"refresh never reconnects or silently resumes failed recovery");
    operation({{"op","disconnect"}},CommandErrorCode::Ok);
    status=operation({{"op","connect"},{"backend","auto"},{"locator","mock"}},CommandErrorCode::Ok);
    require(status.value("ready").toBool(),"explicit reconnect performs complete recovery before enabling writes");
    backend.shutdown();
}

void timeoutObserverIsolation()
{
    auto mock = std::make_unique<ScriptedTransport>();
    auto *wire = mock.get();
    QVector<LogRecord> records;
    QObject receiver;
    SkyFpgaBackend backend(std::move(mock));
    QObject::connect(&backend, &SkyFpgaBackend::logRecord, &receiver,
                     [&](const LogRecord& record) { records.push_back(record); });
    bool connected = false;
    backend.submit({{"op","connect"},{"backend","auto"},{"locator","mock"}},
                   [&](CommandErrorCode code, QString, QJsonObject) { connected = code == CommandErrorCode::Ok; });
    waitUntil([&] { return connected; }, "timeout test connects simulator");
    wire->holdResponses = true;
    backend.setOperationTimeoutForTesting(50);
    int timedOutCallbacks = 0;
    backend.submit({{"op","refresh"}}, [&](CommandErrorCode code, QString, QJsonObject) {
        require(code == CommandErrorCode::ConfigApplyFailed, "stalled FPGA operation times out");
        ++timedOutCallbacks;
    }, QStringLiteral("expired-operation"));
    waitUntil([&] { return timedOutCallbacks == 1; }, "timed out callback returns once");
    waitUntil([&] { return !backend.statusDocument().value("connected").toBool(); },
              "timed out operation disconnects worker");
    wire->holdResponses = false;
    backend.setOperationTimeoutForTesting(120000);
    int nextCallbacks = 0;
    backend.submit({{"op","connect"},{"backend","auto"},{"locator","mock"}},
                   [&](CommandErrorCode code, QString, QJsonObject) {
                       require(code == CommandErrorCode::Ok, "new operation succeeds after timeout cleanup");
                       ++nextCallbacks;
                   }, QStringLiteral("next-operation"));
    waitUntil([&] { return nextCallbacks == 1; }, "new operation completes after prior timeout");
    QCoreApplication::processEvents();
    require(timedOutCallbacks == 1, "stale observer cannot complete expired callback again");
    bool newWorkerSeen = false;
    for (const auto& record : records)
        if (record.fields.value("event") == "fpga_worker_started" && record.correlation_id == "next-operation")
            newWorkerSeen = true;
    require(newWorkerSeen, "new worker uses its own context after timeout");
    backend.shutdown();
}

void statusListenerMayDestroyBackend()
{
    auto mock = std::make_unique<ScriptedTransport>();
    auto *backend = new SkyFpgaBackend(std::move(mock));
    QPointer<SkyFpgaBackend> guard(backend);
    QObject receiver;
    int completions = 0;
    QObject::connect(backend, &SkyFpgaBackend::statusChanged, &receiver,
                     [&guard](const QJsonObject& status) {
                         if (guard && status.contains(QStringLiteral("operation_result")))
                             delete guard.data();
                     });
    backend->submit({{"op", "connect"}, {"backend", "auto"}, {"locator", "mock"}},
                    [&](CommandErrorCode, QString, QJsonObject) { ++completions; },
                    QStringLiteral("destroy-during-status"));
    waitUntil([&] { return !guard; }, "direct status listener destroys backend during completion");
    require(completions == 1, "destruction completes pending operation exactly once");
    QCoreApplication::processEvents();
    require(completions == 1, "late completion cannot access destroyed backend");
}

void motorSafetyDoesNotCompleteAnotherOperation()
{
    auto mock=std::make_unique<ScriptedTransport>();auto* wire=mock.get();
    SkyFpgaBackend backend(std::move(mock));QObject receiver;
    int reads=0,heartbeats=0,stops=0;
    QObject::connect(&backend,&SkyFpgaBackend::rawCommand,&receiver,[&](quint64,const QByteArray& bytes) {
        FpgaVlp1::StreamParser parser;
        for(const auto& frame:parser.feed(reinterpret_cast<const std::uint8_t*>(bytes.constData()),bytes.size())) {
            if(frame.header.message==2)++reads;
            if(frame.header.message==12&&frame.header.source==0x47) {
                const auto action=word(frame.payload,0);
                if(action==5)++heartbeats;
                if(action==2)++stops;
            }
        }
    });
    const auto finish=[&](const QJsonObject& request) {
        bool done=false;
        backend.submit(request,[&](CommandErrorCode code,QString,QJsonObject) {
            require(code==CommandErrorCode::Ok,"motor safety setup completes");done=true;
        });
        waitUntil([&]{return done;},"motor safety setup final response");
    };
    const auto settle=[] {
        QElapsedTimer clock;clock.start();
        waitUntil([&]{return clock.elapsed()>=80;},"deliver queued safety ACKs");
    };
    finish({{"op","connect"},{"backend","auto"},{"locator","mock"}});
    finish({{"op","motor"},{"action",4},{"value",1}});

    wire->holdRegularResponses=true;reads=0;
    int refreshCompletions=0,heartbeatCompletions=0;
    backend.submit({{"op","refresh"}},[&](CommandErrorCode code,QString,QJsonObject) {
        require(code==CommandErrorCode::Ok,"ordinary refresh finishes from its own register responses");++refreshCompletions;
    });
    waitUntil([&]{return reads>0;},"ordinary register request remains pending");
    const int priorHeartbeats=heartbeats;
    backend.submit({{"op","motor_heartbeat"}},[&](CommandErrorCode code,QString,QJsonObject status) {
        require(code==CommandErrorCode::Ok&&status.isEmpty(),"heartbeat acknowledges dispatch without fabricating an operation readback");++heartbeatCompletions;
    });
    waitUntil([&]{return heartbeats>priorHeartbeats;},"heartbeat bypasses a pending ordinary register request");
    settle();
    require(heartbeatCompletions==1&&refreshCompletions==0,"heartbeat USB ACK cannot finish the pending ordinary operation");
    bool busyRejected=false;
    backend.submit({{"op","refresh"}},[&](CommandErrorCode code,QString,QJsonObject) {
        busyRejected=code==CommandErrorCode::DeviceOperationBusy;
    });
    require(busyRejected,"parallel heartbeat preserves ordinary operation ownership");
    wire->holdRegularResponses=false;
    waitUntil([&]{return refreshCompletions==1;},"ordinary readback resumes after heartbeat ACK");

    wire->holdRegularResponses=true;reads=0;
    int interruptedCompletions=0,stopCompletions=0;
    backend.submit({{"op","refresh"}},[&](CommandErrorCode code,QString error,QJsonObject) {
        require(code==CommandErrorCode::ConfigApplyFailed&&error.contains("interrupted"),"STOP explicitly interrupts the previous operation once");++interruptedCompletions;
    });
    waitUntil([&]{return reads>0;},"another register request is held before STOP");
    backend.submit({{"op","motor"},{"action",2},{"value",0}},[&](CommandErrorCode code,QString,QJsonObject) {
        require(code==CommandErrorCode::Ok,"STOP completes only after its own acknowledgement and fresh readback");++stopCompletions;
    });
    waitUntil([&]{return stops==1;},"STOP reaches USB without waiting for the held request");
    settle();
    require(interruptedCompletions==1&&stopCompletions==0,"old interruption and priority STOP ACK do not impersonate STOP final readback");
    wire->holdRegularResponses=false;
    waitUntil([&]{return stopCompletions==1;},"STOP final readback completes independently");
    settle();
    require(interruptedCompletions==1&&stopCompletions==1,"late prior responses cannot complete either operation twice");
    backend.shutdown();
}

void acquisitionCycleStatistics()
{
    SkyFpgaBackend backend(makeFpgaUsbSimulator());
    const auto operation = [&](const QJsonObject& request) {
        bool done = false;
        backend.submit(request, [&](CommandErrorCode code, QString error, QJsonObject) {
            if(code != CommandErrorCode::Ok) fprintf(stderr,"Operation %s: %s\n",request.value("op").toString().toUtf8().constData(),error.toUtf8().constData());
            require(code == CommandErrorCode::Ok, "simulated acquisition operation succeeds"); done = true;
        });
        waitUntil([&] { return done; }, "simulated acquisition operation completes");
    };
    operation({{"op","connect"},{"backend","auto"},{"locator",""}});
    operation({{"op","acquisition"},{"enabled",true}});
    operation({{"op","raw"},{"enabled",true}});
    waitUntil([&] { return backend.statusDocument().value("acquisition_cycles").toObject()
        .value("count").toString().toULongLong() > 0; }, "full simulated cycles are counted before preview thinning");
    const auto stats = backend.statusDocument().value("acquisition_cycles").toObject();
    const double expected = stats.value("count").toString().toULongLong() * 1000.0 / stats.value("window_ms").toDouble();
    require(stats.value("scope") == "dlia0+dlia1" && std::abs(expected - stats.value("rate_hz").toDouble()) < 0.0001,
        "cycle frequency uses reported count and actual time window");
    operation({{"op","acquisition"},{"enabled",false}});
    waitUntil([&] { const auto stats = backend.statusDocument().value("acquisition_cycles").toObject();
        return stats.contains("count") && stats.value("count").toString() == "0" && stats.value("rate_hz").toDouble() == 0;
    }, "stopped acquisition reports zero rather than retaining a previous cycle frequency");
    operation({{"op","disconnect"}});
    require(!backend.statusDocument().contains("acquisition_cycles"), "USB disconnect clears cycle statistics");
    backend.shutdown();
}

int main(int argc,char **argv)
{
    QCoreApplication app(argc,argv);
    acquisitionCycleStatistics();
    selectiveConnection();
    diagnosticReadback();
    timeoutObserverIsolation();
    statusListenerMayDestroyBackend();
    motorSafetyDoesNotCompleteAnotherOperation();
    auto mock=std::make_unique<ScriptedTransport>(); auto *wire=mock.get(); mock->failTemperature=true;mock->failWriteAddress=0x2010;
    QVector<LogRecord> operationLogs;
    SkyFpgaBackend backend(std::move(mock));
    QObject::connect(&backend,&SkyFpgaBackend::logRecord,&app,[&](const LogRecord& record) {
        require(record.source=="SkyCore","shared controller diagnostics identify actual Sky execution owner");
        operationLogs.push_back(record);
    });
    int completions=0;
    bool discovered=false;
    backend.submit({{"op","discover"}},[&](CommandErrorCode code,QString,QJsonObject status) {
        require(code==CommandErrorCode::Ok && !status.value("connected").toBool(),"discovery works before USB connection");
        const auto devices=status.value("usb_discovery").toObject().value("devices").toArray();
        require(devices.size()==1 && devices.first().toObject().value("locator").toString()=="mock","discovery returns worker's device list");
        discovered=true;
    });
    waitUntil([&]{return discovered;},"read-only discovery completed");
    require(wire->openCount.load()==0 && wire->commands.isEmpty(),"discovery never opens USB or sends physical commands");
    require(!backend.statusDocument().contains("usb_discovery"),"paths are returned only on request, not periodic telemetry");
    backend.submit({{"op","connect"},{"backend","auto"},{"locator","mock"}},
        [&](CommandErrorCode code,QString,QJsonObject status) {
            require(code==CommandErrorCode::Ok && status.value("ready").toBool(),"handshake waits for verified versions"); ++completions;
        });
    require(completions==0,"queued USB connect cannot report success");
    backend.submit({{"op","refresh"}}, [&](CommandErrorCode code,QString,QJsonObject) {
        require(code==CommandErrorCode::DeviceOperationBusy,"second client cannot own active handshake");
    });
    waitUntil([&]{return completions==1;},"asynchronous handshake finished");
    require(wire->openCount.load()==1,"single USB owner opens once");
    const auto sensorRegisters=backend.statusDocument().value("registers").toObject();
    require(sensorRegisters.value(QString::number(0x6010)).toInt()==4800
        &&sensorRegisters.value(QString::number(0x6210)).toInt()==460800
        &&sensorRegisters.value(QString::number(0x6510)).toInt()==115200
        &&sensorRegisters.value(QString::number(0x6414)).toInt()==2000,
        "Sky status forwards actual sensor baud and polling register readbacks to ground");
    bool settingsDone=false;
    backend.submit({{"op","sensor"},{"source",0x40},{"enabled",true},{"baud",9600},{"poll_ms",250}},
        [&](CommandErrorCode code,QString,QJsonObject status) {
            const auto values=status.value("registers").toObject();
            require(code==CommandErrorCode::Ok && values.value(QString::number(0x6010)).toInt()==9600
                && values.value(QString::number(0x6018)).toInt()==250 && values.value(QString::number(0x6004)).toInt()==1,
                "Sky connects only after staged baud and polling have verified readbacks");settingsDone=true;
        });
    require(!settingsDone,"parameter submission is not success at enqueue");
    waitUntil([&]{return settingsDone;},"remote parameter write and readback completed");
    settingsDone=false;
    backend.submit({{"op","sensor"},{"source",0x41},{"enabled",true},{"poll_ms",500},{"parameters",QJsonObject{{"address",42}}}},
        [&](CommandErrorCode code,QString,QJsonObject status) {
            const auto values=status.value("registers").toObject();
            const auto config=FpgaControlConfig::fromJson(status.value("configuration").toObject());
            require(code==CommandErrorCode::Ok && values.value(QString::number(0x6114)).toInt()==42
                && values.value(QString::number(0x611c)).toInt()==500 && values.value(QString::number(0x6104)).toInt()==1
                && config.sensorParameters.value(0x41).value("address").toInt()==42,
                "Sky HC2A connects after advanced parameter verification and synchronizes confirmed configuration");settingsDone=true;
        });
    waitUntil([&]{return settingsDone;},"remote HC2A advanced configuration completed");
    settingsDone=false;
    backend.submit({{"op","sensor"},{"source",0x46},{"enabled",true},{"poll_ms",500}},
        [&](CommandErrorCode code,QString,QJsonObject status) {
            require(code==CommandErrorCode::Ok && FpgaControlConfig::fromJson(status.value("configuration").toObject()).ai8.pollIntervalMs==500,
                "confirmed AI8 polling stays synchronized with the FPGA configuration page");settingsDone=true;
        });
    waitUntil([&]{return settingsDone;},"AI8 final parameter write completed");
    const auto payload=QByteArray::fromHex("0100010013000304c0d40100");
    for(int i=0;i<=1000;++i){
        const auto frame=FpgaVlp1::buildFrame(FpgaVlp1::FrameType::Data,i+1,0x45,0x1100,
            std::vector<std::uint8_t>(payload.begin(),payload.end()),true,1,quint64(i)*100000);
        wire->sensorWire.append(reinterpret_cast<const char*>(frame.data()),frame.size());
    }
    backend.submit({{"op","temperature"},{"celsius",25.0}},[&](CommandErrorCode code,QString error,QJsonObject) {
        require(code==CommandErrorCode::ConfigApplyFailed && !error.isEmpty(),"hardware failure delivered after FPGA response"); ++completions;
    }, QStringLiteral("fpga-operation-test"), QStringLiteral("test-session"));
    require(completions==1,"temperature cannot complete at enqueue");
    waitUntil([&]{return completions==2;},"failed temperature final callback");
    require(wire->temperatureWrites.load()==1,"failed write never automatically retries");
    waitUntil([&] { for (const auto& log : operationLogs)
        if (log.fields.value(QStringLiteral("event")).toString() == QStringLiteral("fpga_operation_diagnostic") &&
            log.correlation_id == QStringLiteral("fpga-operation-test") && log.session_id == QStringLiteral("test-session") && log.thread_id != 0) return true;
        return false; }, "worker driver failure preserves operation context and worker thread");
    bool started = false;
    for (const auto& log : operationLogs)
        if (log.fields.value(QStringLiteral("event")).toString() == QStringLiteral("fpga_worker_started") &&
            log.correlation_id == QStringLiteral("fpga-operation-test")) started = true;
    require(started, "queued FPGA worker starts with captured operation context");
    waitUntil([&]{return backend.statusDocument().value("sensor_rates_hz").toObject().value("69").toDouble()==1000.0;},
        "Sky status retains raw sensor frequency despite presentation coalescing");
    const auto confirmedConfiguration=backend.statusDocument().value("configuration").toObject();
    FpgaControlConfig requested;requested.wms[0].scanMilliHz+=1000;
    backend.submit({{"op","configure"},{"configuration",requested.toJson()}},[&](CommandErrorCode code,QString error,QJsonObject status) {
        require(code==CommandErrorCode::ConfigApplyFailed && !error.isEmpty(),"partial configuration failure is explicit");
        require(status.value("configuration").toObject()==confirmedConfiguration,"failed requested config never becomes externally confirmed config");
        require(!status.value("registers").toObject().isEmpty() && !status.value("detail").toString().isEmpty(),"partial operation retains hardware readback evidence and failure detail");
        ++completions;
    });
    require(backend.statusDocument().value("configuration").toObject()==confirmedConfiguration,"queued configuration remains unconfirmed");
    waitUntil([&]{return completions==3;},"configuration failure final result");
    backend.submit({{"op","write_register"},{"address",1}},[&](CommandErrorCode code,QString,QJsonObject) {
        require(code==CommandErrorCode::InvalidPayload,"arbitrary register request rejected"); ++completions;
    });
    backend.submit({{"op","disconnect"}},[&](CommandErrorCode code,QString,QJsonObject status) {
        require(code==CommandErrorCode::Ok && !status.value("connected").toBool(),"disconnect final result clears USB state"); ++completions;
        require(status.value("sensor_rates_hz").toObject().isEmpty(),"Sky USB disconnect clears cached sensor rates");
    });
    waitUntil([&]{return completions==5;},"disconnect callback returned");
    backend.shutdown();
    return 0;
}
