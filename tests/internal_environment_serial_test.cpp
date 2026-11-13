#include "InternalEnvironmentSerialController.h"
#include "TelemetryCodec.h"
#include "data_collector.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPointer>
#include <QThread>
#include <cstdlib>
#include <iostream>
static void require(bool value,const char *message){if(!value){std::cerr<<message<<'\n';std::exit(1);}}
class TestPtbCollector : public VaporView::PtbCollector
{
public:
    using VaporView::PtbCollector::run;
};
namespace VaporView {
class LidarCollectorTestAccess : public LidarCollector
{
public:
    using LidarCollector::run;
    void setActiveProtocol(LidarProtocol protocol) { active_protocol_ = protocol; }
};
}
int main(int argc,char **argv)
{
    QCoreApplication app(argc,argv);
    using namespace VaporView;
    {
        TestPtbCollector ptb;
        QString sourceFile, sourceFunction;
        int sourceLine = 0;
        std::string structuredMessage;
        DataCollector::StructuredLogFields structuredFields;
        ptb.setLocatedStructuredLogCallback([&](LogLevel, const std::string&, const std::string& event,
                                                const std::string& message, DataCollector::StructuredLogFields fields,
                                                const char* file, const char* function, int line) {
            if (event == "ptb_continuous_start_failed") {
                sourceFile = QString::fromUtf8(file);
                sourceFunction = QString::fromUtf8(function);
                sourceLine = line;
                structuredMessage = message;
                structuredFields = std::move(fields);
            }
        });
        ptb.run();
        require(sourceFile.endsWith(QStringLiteral("data_collector.cpp")) &&
                !sourceFunction.isEmpty() && sourceLine > 0 &&
                structuredFields["error_code"] == "SERIAL_WRITE_FAILED",
                "collector failure preserves its source location and fields");
        const auto sourcePath = QFileInfo(QString::fromUtf8(__FILE__)).absoluteDir()
            .filePath(QStringLiteral("../src/sky/devices/data_collector.cpp"));
        QFile source(sourcePath);
        require(source.open(QIODevice::ReadOnly), "collector source is available for location verification");
        QByteArray sourceText;
        for (int i = 0; i < sourceLine; ++i) sourceText = source.readLine();
        require(sourceText.contains("logStructured(") &&
                source.readLine().contains("device.pressure.command") &&
                source.readLine().contains("ptb_continuous_start_failed"),
                "collector line identifies the actual PTB failure call");
        std::vector<std::string> fallbackMessages;
        ptb.setLogCallback([&](const std::string& message) { fallbackMessages.push_back(message); });
        ptb.setLocatedStructuredLogCallback({});
        ptb.run();
        bool foundFallback = false;
        for (const auto& message : fallbackMessages) foundFallback |= message == structuredMessage;
        require(foundFallback, "cleared structured callback falls back to the text log callback");
    }
    {
        TestPtbCollector ptb;
        QVector<LogContext> contexts;
        bool contextCleared = false;
        ptb.setStructuredLogCallback([&](LogLevel, const std::string&, const std::string& event,
                                         const std::string&, DataCollector::StructuredLogFields) {
            if (event == "ptb_continuous_start_failed") {
                const auto *context = currentLogContext();
                contexts.push_back(context ? *context : LogContext{});
            }
        });
        ptb.setStartupLogContext({QStringLiteral("ptb-connect"), QStringLiteral("ptb-session")});
        std::thread first([&] { ptb.run(); contextCleared = currentLogContext() == nullptr; });
        first.join();
        require(contexts.size() == 1 && contexts[0].correlationId == QStringLiteral("ptb-connect") &&
                contexts[0].sessionId == QStringLiteral("ptb-session") && contextCleared,
                "PTB startup failure keeps the connection context and clears its scope");
        std::thread later([&] { ptb.run(); contextCleared = currentLogContext() == nullptr; });
        later.join();
        require(contexts.size() == 2 && contexts[1].correlationId.isEmpty() &&
                contexts[1].sessionId.isEmpty() && contextCleared,
                "later PTB startup cannot inherit a completed connection context");
    }
    {
        LidarCollectorTestAccess lidar;
        QVector<LogContext> contexts;
        bool contextCleared = false;
        lidar.setActiveProtocol(LidarProtocol::TFA1500DistanceFrame);
        lidar.setStructuredLogCallback([&](LogLevel, const std::string&, const std::string& event,
                                           const std::string&, DataCollector::StructuredLogFields) {
            if (event == "lidar_distance_output_command_failed") {
                const auto *context = currentLogContext();
                contexts.push_back(context ? *context : LogContext{});
            }
        });
        lidar.setStartupLogContext({QStringLiteral("lidar-connect"), QStringLiteral("lidar-session")});
        std::thread worker([&] {
            lidar.run();
            contextCleared = currentLogContext() == nullptr;
            lidar.run();
            contextCleared = contextCleared && currentLogContext() == nullptr;
        });
        worker.join();
        require(contexts.size() == 2 && contexts[0].correlationId == QStringLiteral("lidar-connect") &&
                contexts[0].sessionId == QStringLiteral("lidar-session") &&
                contexts[1].correlationId.isEmpty() && contexts[1].sessionId.isEmpty() && contextCleared,
                "LiDAR startup failure keeps its connection context only for the first command");
    }
    SkyConfig config=SkyConfig::defaults();
    require(!config.bmp390.enabled&&!config.sht45.enabled&&config.bmp390.baud_rate==115200&&config.sht45.frequency_hz==1,
        "new bridges default to disabled optional devices with 115200 and 1 Hz");
    config.bmp390={true,"TEST-BMP",57600,2.0,{}};config.sht45={true,"TEST-SHT",115200,5.0,{}};
    SkyConfig decoded;QString error;
    require(SkyConfig::fromJson(config.toJson(),decoded,&error)&&decoded.bmp390==config.bmp390&&decoded.sht45==config.sht45,
        "bridge settings round trip independently from PTB/HC2A");
    auto changed=config;changed.bmp390.baud_rate=115200;
    require(config.diff(changed).bmp390_changed&&!config.diff(changed).ptb_changed&&!config.diff(changed).sht45_changed,
        "only the changed internal collector is reconfigured");
    auto invalid=config.toJson();auto bridge=invalid.value("sht45").toObject();bridge["frequency_hz"]=1.5;invalid["sht45"]=bridge;
    require(!SkyConfig::fromJson(invalid,decoded,&error)&&decoded.sht45==config.sht45,"unsupported fractional receive rate is rejected atomically");
    auto legacy=config.toJson();legacy.remove("bmp390");legacy.remove("sht45");
    require(SkyConfig::fromJson(legacy,decoded,&error)&&!decoded.bmp390.enabled&&!decoded.sht45.enabled,"old peer configuration leaves new bridges disabled");
    auto legacyHumidity = config.toJson();
    auto legacyHumiditySection = legacyHumidity.take("hc2a").toObject();
    legacyHumiditySection["source"] = "hmp3";
    legacyHumiditySection["port"] = "LEGACY-HUMIDITY";
    legacyHumidity["hmp"] = legacyHumiditySection;
    require(SkyConfig::fromJson(legacyHumidity, decoded, &error) && decoded.hc2a.port == "LEGACY-HUMIDITY" &&
            decoded.hc2a.source == "hc2a" && decoded.toJson().contains("hc2a") && !decoded.toJson().contains("hmp"),
            "legacy HMP JSON reads as HC2A while new configuration writes use HC2A");
    legacyHumidity["hc2a"] = config.toJson().value("hc2a");
    require(SkyConfig::fromJson(legacyHumidity, decoded, &error) && decoded.hc2a == config.hc2a,
            "explicit HC2A configuration wins over an old HMP alias");
    for(auto id:{SkyDeviceId::Bmp390,SkyDeviceId::Sht45}) {
        SkyDeviceId parsed;require(skyDeviceIdFromValue(quint8(id),parsed)&&parsed==id,"new device IDs decode independently");
        const auto payload=TelemetryCodec::serializeDeviceCommand(id);require(TelemetryCodec::parseDeviceCommand(payload,parsed)&&parsed==id,"direct device command preserves the bridge identity");
    }
    InternalEnvironmentSerialController controller;
    LogRecord failureRecord;
    const auto failureConnection = QObject::connect(&controller, &InternalEnvironmentSerialController::logRecordGenerated,
                     &controller, [&failureRecord](const LogRecord& record) { failureRecord = record; });
    controller.configure(SkyDeviceId::Bmp390,{true,"COM_VAPORVIEW_TEST_DOES_NOT_EXIST",0,1.0,{}});
    require(!controller.connectDevice(SkyDeviceId::Bmp390)&&!controller.active(),"invalid baud cannot start a bridge worker");
    require(failureRecord.fields.value("file").toString().endsWith("InternalEnvironmentSerialController.cpp") &&
            failureRecord.fields.value("function").toString().contains("connectDevice") &&
            failureRecord.fields.value("line").toInt() > 0,
            "serial failure carries its business call location before signal forwarding");
    QObject::disconnect(failureConnection);
    controller.configure(SkyDeviceId::Bmp390,{true,"COM_VAPORVIEW_TEST_DOES_NOT_EXIST",115200,1.0,{}});
    require(controller.connectDevice(SkyDeviceId::Bmp390)&&controller.state(SkyDeviceId::Bmp390)==DeviceState::Connecting,
        "opening serial is pending rather than reported connected");
    controller.disconnectDevice(SkyDeviceId::Bmp390);
    QElapsedTimer timer;timer.start();while(timer.elapsed()<100){QCoreApplication::processEvents();QThread::msleep(1);}
    require(controller.state(SkyDeviceId::Bmp390)==DeviceState::Disconnected&&!controller.active(),
        "cancelled worker joins and cannot restore stale connection state");
    QVector<LogRecord> records;
    const auto connection = QObject::connect(&controller, &InternalEnvironmentSerialController::logRecordGenerated,
        &controller, [&records](const LogRecord& record) { records.push_back(record); });
    controller.configure(SkyDeviceId::Bmp390,{true,"COM_VAPORVIEW_MISSING_BMP",115200,1.0,{}});
    controller.configure(SkyDeviceId::Sht45,{true,"COM_VAPORVIEW_MISSING_SHT",115200,1.0,{}});
    require(controller.connectDevice(SkyDeviceId::Bmp390,QStringLiteral("bmp-operation"),QStringLiteral("active-session")),
            "BMP worker accepts its correlation context");
    require(controller.connectDevice(SkyDeviceId::Sht45,QStringLiteral("sht-operation"),QStringLiteral("active-session")),
            "SHT worker accepts an independent correlation context");
    QElapsedTimer pending;pending.start();
    while ((controller.state(SkyDeviceId::Bmp390)==DeviceState::Connecting ||
            controller.state(SkyDeviceId::Sht45)==DeviceState::Connecting) && pending.elapsed()<5000) {
        QCoreApplication::processEvents();QThread::msleep(1);
    }
    QCoreApplication::processEvents();
    require(controller.state(SkyDeviceId::Bmp390)==DeviceState::Error && controller.state(SkyDeviceId::Sht45)==DeviceState::Error,
            "both absent serial bridges report asynchronous failure");
    for (auto id : {SkyDeviceId::Bmp390,SkyDeviceId::Sht45}) {
        const QString operation=id==SkyDeviceId::Bmp390?QStringLiteral("bmp-operation"):QStringLiteral("sht-operation");
        bool worker=false,failed=false;
        for (const auto& record : records) {
            if (record.fields.value(QStringLiteral("device_id")).toString()!=skyDeviceIdName(id)) continue;
            require(record.correlation_id==operation && record.session_id==QStringLiteral("active-session"),
                    "worker and callback keep independent context");
            worker|=record.fields.value(QStringLiteral("event")).toString()==QStringLiteral("internal_serial_worker_started");
            if (record.fields.value(QStringLiteral("event")).toString()==QStringLiteral("internal_serial_worker_started"))
                require(record.thread_id != 0 && record.thread_id != static_cast<quint64>(reinterpret_cast<quintptr>(QThread::currentThreadId())),
                        "worker record retains its originating thread");
            failed|=record.fields.value(QStringLiteral("event")).toString()==QStringLiteral("internal_serial_error");
        }
        require(worker&&failed,"worker start and asynchronous error retain the operation identity");
    }
    records.clear();
    require(controller.connectDevice(SkyDeviceId::Bmp390,QStringLiteral("cancelled-operation"),QStringLiteral("active-session")),
            "cancellable bridge operation starts");
    controller.disconnectDevice(SkyDeviceId::Bmp390);
    QCoreApplication::processEvents();
    bool cancelled=false;
    for (const auto& record:records) {
        const auto event=record.fields.value(QStringLiteral("event")).toString();
        if (event==QStringLiteral("internal_serial_connect_cancelled")) {
            cancelled=record.correlation_id==QStringLiteral("cancelled-operation") &&
                record.session_id==QStringLiteral("active-session");
        }
        require(event!=QStringLiteral("internal_serial_error"),"cancelled generation emits no stale failure callback");
    }
    require(cancelled,"cancelled operation retains its correlation context");
    QObject::disconnect(connection);

    int cancellationCount=0;
    const auto reentrantConnection=QObject::connect(&controller,&InternalEnvironmentSerialController::logRecordGenerated,
        &controller,[&](const LogRecord& record) {
            if(record.fields.value(QStringLiteral("event")).toString()!=QStringLiteral("internal_serial_connect_cancelled")) return;
            ++cancellationCount;
            require(record.correlation_id==QStringLiteral("reentrant-cancel") && record.session_id==QStringLiteral("active-session"),
                    "reentrant cancellation retains its original context");
            controller.disconnectDevice(SkyDeviceId::Bmp390);
        });
    require(controller.connectDevice(SkyDeviceId::Bmp390,QStringLiteral("reentrant-cancel"),QStringLiteral("active-session")),
            "reentrant cancellation operation starts");
    controller.disconnectDevice(SkyDeviceId::Bmp390);
    require(cancellationCount==1 && controller.state(SkyDeviceId::Bmp390)==DeviceState::Disconnected,
            "synchronous cancellation listener cannot recursively cancel the same operation");
    QObject::disconnect(reentrantConnection);

    {
        InternalEnvironmentSerialController bridge;
        bridge.configure(SkyDeviceId::Bmp390,{true,"COM_VAPORVIEW_MISSING_BMP",115200,1.0,{}});
        QVector<LogRecord> events;
        QObject::connect(&bridge,&InternalEnvironmentSerialController::logRecordGenerated,&bridge,
                         [&](const LogRecord& record){events.push_back(record);});
        bool replaced=false,replacementStarted=false;
        QObject::connect(&bridge,&InternalEnvironmentSerialController::stateChanged,&bridge,
            [&](SkyDeviceId id,DeviceState state) {
                if(id==SkyDeviceId::Bmp390 && state==DeviceState::Connecting && !replaced) {
                    replaced=true;
                    bridge.disconnectDevice(id);
                    replacementStarted=bridge.connectDevice(id,QStringLiteral("replacement"),QStringLiteral("session-b"));
                }
            });
        require(!bridge.connectDevice(SkyDeviceId::Bmp390,QStringLiteral("cancelled-before-worker"),QStringLiteral("session-a")) &&
                replaced && replacementStarted,"cancelled connection cannot overwrite a reentrant replacement worker");
        QElapsedTimer deadline;deadline.start();
        while(bridge.state(SkyDeviceId::Bmp390)==DeviceState::Connecting && deadline.elapsed()<5000) {
            QCoreApplication::processEvents();QThread::msleep(1);
        }
        QCoreApplication::processEvents();
        require(bridge.state(SkyDeviceId::Bmp390)==DeviceState::Error,"replacement worker completes independently");
        bool oldWorker=false,replacementWorker=false,cancellation=false;
        for(const auto& record:events) {
            const QString event=record.fields.value(QStringLiteral("event")).toString();
            if(event==QStringLiteral("internal_serial_worker_started")) {
                oldWorker|=record.correlation_id==QStringLiteral("cancelled-before-worker");
                replacementWorker|=record.correlation_id==QStringLiteral("replacement") && record.session_id==QStringLiteral("session-b");
            }
            if(event==QStringLiteral("internal_serial_connect_cancelled"))
                cancellation|=record.correlation_id==QStringLiteral("cancelled-before-worker") && record.session_id==QStringLiteral("session-a");
        }
        require(!oldWorker && replacementWorker && cancellation,"only the replacement worker starts with its own context");
    }
    {
        QPointer<InternalEnvironmentSerialController> bridge(new InternalEnvironmentSerialController);
        bridge->configure(SkyDeviceId::Bmp390,{true,"COM_VAPORVIEW_MISSING_BMP",115200,1.0,{}});
        QObject::connect(bridge,&InternalEnvironmentSerialController::stateChanged,&app,
            [&](SkyDeviceId,DeviceState state) {if(state==DeviceState::Connecting)delete bridge.data();});
        require(!bridge->connectDevice(SkyDeviceId::Bmp390,QStringLiteral("destroyed")) && !bridge,
                "connection stops when its state listener destroys the controller");
    }
}
