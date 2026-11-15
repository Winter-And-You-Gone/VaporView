#include "FpgaDeviceController.h"
#include "FpgaUsbSimulator.h"
#include "Tec103GpioProtocol.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QThread>
#include <QTimer>
#include <QtEndian>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <functional>

using namespace VaporView;
using namespace VaporView::Ground::Devices;
namespace {
void require(bool value,const char* message)
{
    if(!value) {std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}
}
void waitUntil(const std::function<bool()>& done,const char* message,int timeout=15000)
{
    QElapsedTimer timer;timer.start();
    while(!done() && timer.elapsed()<timeout) {
        QCoreApplication::processEvents(QEventLoop::AllEvents,10);QThread::msleep(1);
    }
    require(done(),message);
}
bool pumpUntil(const std::function<bool()>& done,int timeout)
{
    QElapsedTimer timer;timer.start();
    while(!done() && timer.elapsed()<timeout) {
        QCoreApplication::processEvents(QEventLoop::AllEvents,10);QThread::msleep(1);
    }
    return done();
}
std::shared_ptr<QJsonObject> traceSession(FpgaDeviceSession& session)
{
    auto trace=std::make_shared<QJsonObject>();
    auto parser=std::make_shared<FpgaVlp1::StreamParser>();
    QObject::connect(&session,&FpgaDeviceSession::usbBytesReceived,&session,
        [trace,parser](const QByteArray& bytes) {
            (*trace)["usb_read_chunks"]=trace->value("usb_read_chunks").toInt()+1;
            (*trace)["usb_received_bytes"]=trace->value("usb_received_bytes").toDouble()+bytes.size();
            auto prefix=QByteArray::fromHex(trace->value("usb_prefix_hex").toString().toLatin1());
            prefix.append(bytes.left(qMax<qsizetype>(0,512-prefix.size())));(*trace)["usb_prefix_hex"]=QString::fromLatin1(prefix.toHex());
            auto frames=trace->value("received_frames").toArray();
            for(const auto& frame:parser->feed(reinterpret_cast<const std::uint8_t*>(bytes.constData()),size_t(bytes.size()))) {
                frames.append(QJsonObject{{"sequence",double(frame.header.sequence)},{"source",frame.header.source},
                    {"message",frame.header.message},{"frame_type",int(frame.header.frameType)},
                    {"payload_hex",QString::fromLatin1(QByteArray(reinterpret_cast<const char*>(frame.payload.data()),qsizetype(frame.payload.size())).left(64).toHex())}});
                while(frames.size()>32)frames.removeAt(0);
            }
            (*trace)["received_frames"]=frames;(*trace)["parser_frame_count"]=double(parser->frameCount());
            (*trace)["parser_discarded_bytes"]=double(parser->discardedBytes());
            (*trace)["parser_pending_bytes"]=double(parser->pendingBytes());
        });
    QObject::connect(&session,&FpgaDeviceSession::commandSent,&session,[trace](const QByteArray& bytes) {
        (*trace)["command_transfers"]=trace->value("command_transfers").toInt()+1;
        auto transfers=trace->value("sent_transfers_hex").toArray();transfers.append(QString::fromLatin1(bytes.left(512).toHex()));
        while(transfers.size()>32)transfers.removeAt(0);(*trace)["sent_transfers_hex"]=transfers;
    });
    QObject::connect(&session,&FpgaDeviceSession::commandCompleted,&session,
        [trace](quint32 sequence,quint16 message,bool ok,quint32 status,const QByteArray&) {
            auto completions=trace->value("command_completions").toArray();
            completions.append(QJsonObject{{"sequence",double(sequence)},{"message",message},{"ok",ok},{"status",double(status)}});
            while(completions.size()>32)completions.removeAt(0);(*trace)["command_completions"]=completions;
        });
    QObject::connect(&session,&FpgaDeviceSession::stateChanged,&session,
        [trace](FpgaSessionState state,const QString& detail) {(*trace)["state"]=int(state);(*trace)["state_detail"]=detail;});
    QObject::connect(&session,&FpgaDeviceSession::errorOccurred,&session,
        [trace](const QString& detail) {auto errors=trace->value("errors").toArray();errors.append(detail);(*trace)["errors"]=errors;});
    return trace;
}
QJsonObject sessionTrace(const FpgaDeviceSession& session,const std::shared_ptr<QJsonObject>& trace)
{
    auto result=*trace;const auto diagnostic=session.transport()->diagnostic();
    result["state"]=int(session.state());result["state_detail"]=session.stateDetail();result["is_open"]=session.isOpen();
    result["transport"]=QJsonObject{{"state",int(diagnostic.state)},{"detail",diagnostic.detail},
        {"bytes_read",double(diagnostic.bytesRead)},{"bytes_written",double(diagnostic.bytesWritten)},
        {"fields",QJsonObject::fromVariantMap(diagnostic.fields)}};
    return result;
}
bool sessionCommand(FpgaDeviceSession& session,quint32 address,bool write,quint32& value,QString& error)
{
    bool done=false,success=false;quint32 sequence=0;
    const auto read=QObject::connect(&session,&FpgaDeviceSession::registerReadCompleted,&session,
        [&](quint32 current,quint32,const QVector<quint32>& values) {
            if(current==sequence && values.size()==1)value=values.front();
        });
    const auto complete=QObject::connect(&session,&FpgaDeviceSession::commandCompleted,&session,
        [&](quint32 current,quint16,bool ok,quint32 status,const QByteArray&) {
            if(current!=sequence)return;
            done=true;success=ok;
            if(!ok)error=QStringLiteral("Register 0x%1 status 0x%2").arg(address,4,16,QLatin1Char('0')).arg(status,8,16,QLatin1Char('0'));
        });
    sequence=write?session.writeRegisters(address,{value}):session.readRegisters(address,1);
    if(!sequence || !pumpUntil([&]{return done || !session.isOpen();},3500))
        error=QStringLiteral("Register 0x%1 timed out; no retry was requested").arg(address,4,16,QLatin1Char('0'));
    QObject::disconnect(read);QObject::disconnect(complete);
    if(!done && sequence)session.cancelCommands({sequence});
    return done && success;
}
bool writeAndRead(FpgaDeviceSession& session,quint32 address,quint32 value,QString& error)
{
    const auto expected=value;
    if(!sessionCommand(session,address,true,value,error) || !sessionCommand(session,address,false,value,error))return false;
    if(value==expected)return true;
    error=QStringLiteral("Register 0x%1 restore readback mismatch").arg(address,4,16,QLatin1Char('0'));return false;
}
QJsonObject registerDocument(const QMap<quint32,quint32>& values)
{
    QJsonObject result;
    for(auto i=values.cbegin();i!=values.cend();++i)result[QStringLiteral("0x%1").arg(i.key(),4,16,QLatin1Char('0'))]=double(i.value());
    return result;
}
QJsonObject temperatureDocument(const TemperatureControllerData& data)
{
    QJsonArray channels;
    for(int channel=0;channel<2;++channel) {
        const auto& value=data.channels[size_t(channel)];
        channels.append(QJsonObject{{"logical_channel",channel+1},{"internal_channel",1},
            {"target_c",value.target_temperature_c},{"measured_c",value.measured_temperature_c},
            {"output_percent",value.output_percent},{"output_enabled",value.output_enabled},
            {"resistance_ohm",value.sensor_resistance_ohm},{"kp",value.kp},{"ki",value.ki},{"kd",value.kd}});
    }
    return {{"valid",data.valid},{"channels",channels},{"error_code",int(data.error_code)},
        {"internal_temperature_c",data.internal_temperature_c},{"error_message",QString::fromStdString(data.error_message)}};
}

// Explicit manual entry point only. No default CTest invocation opens hardware.
// All instrument traffic comes from the production Controller's read-only poll.
int hardwareReadonly(const QString& locator,const QString& output,int expectedChannel)
{
    const auto path=QDir::cleanPath(QFileInfo(output).absoluteFilePath());
    if(expectedChannel<0 || expectedChannel>2 || locator.isEmpty() || output.isEmpty() || !path.startsWith("D:/Project/Test/",Qt::CaseInsensitive)
        || QFileInfo::exists(path)) {
        std::fprintf(stderr,"Hardware mode requires --locator and a new --output file under D:\\Project\\Test.\n");return 2;
    }
    QJsonObject document{{"mode","explicit_hardware_readonly"},{"locator",locator},
        {"instrument_parameter_writes",0},{"instrument_output_writes",0},{"legacy_6a_writes",0},
        {"expected_channel",expectedChannel}};
    const auto expected=[expectedChannel](int channel) {return expectedChannel==0 || expectedChannel==channel;};
    const auto validSample=[&](const TemperatureControllerData& value) {
        if(!value.valid)return false;
        for(int channel=1;channel<=2;++channel)
            if(expected(channel) && !std::isfinite(value.channels[size_t(channel-1)].measured_temperature_c))return false;
        return true;
    };
    QString error;QMap<quint32,quint32> original,restored;
    const QVector<quint32> otherControls{0x2004,0x2104,0x3004,0x3104,0x4004,0x4104,
        0x5004,0x5104,0x6004,0x6104,0x6204,0x6304,0x6404,0x6504,0x6604,0x6a04,0x6704};
    QVector<quint32> addresses{4};
    addresses+=otherControls;
    for(quint32 base:{0x6800u,0x6900u})
        for(quint32 offset:{0u,4u,8u,12u,0x10u,0x14u,0x20u,0x24u,0x28u,0x2cu,0x30u,0x34u,0x38u})addresses.append(base+offset);
    bool preflight=false,sampled=false,configurationRestored=true,expectedChannelsErrorFree=true;
    {
        // Read preconditions before constructing a Controller, whose normal
        // disconnect sends the existing motor-disable safety command.
        FpgaDeviceSession inspection(makeFpgaUsbHardwareTransport());
        const auto trace=traceSession(inspection);
        if(inspection.open(locator,&error)) {
            preflight=true;
            for(quint32 address:addresses) {
                quint32 value=0;
                if(!sessionCommand(inspection,address,false,value,error)){preflight=false;break;}
                original.insert(address,value);
            }
            if(preflight && (original.value(4)>1 || original.value(0x6704)!=0)) {
                preflight=false;error=QStringLiteral("Requires global_enable=0/1 and motor control=0; no state was changed.");
            }
            if(preflight && original.value(4)==0)for(quint32 control:otherControls) {
                if(original.value(control)!=0) {
                    preflight=false;error=QStringLiteral("Global enable is zero but another module control is enabled; no state was changed.");break;
                }
            }
            for(quint32 base:{0x6800u,0x6900u}) {
                if(!preflight)break;
                if(original.value(base)!=(base==0x6800?0x00480100u:0x00490100u)
                    || original.value(base+4)!=0 || (original.value(base+8)&9)
                    || original.value(base+0x20) || original.value(base+0x24)) {
                    preflight=false;error=QStringLiteral("Requires disabled, idle, empty LC UART bridges; no data was flushed.");
                }
            }
            document["preflight_session_diagnostics"]=sessionTrace(inspection,trace);
            inspection.close();
        }
        else document["preflight_session_diagnostics"]=sessionTrace(inspection,trace);
    }
    document["original_registers"]=registerDocument(original);
    if(preflight) {
        FpgaDeviceController controller(makeFpgaUsbHardwareTransport());
        FpgaDeviceSession* session=nullptr;
        bool started=false,cleanupDone=false;TemperatureControllerData data;int samples=0;
        std::array<QByteArray,2> uartCommands;
        QJsonArray observedQueries, exchanges;int instrumentWrites=0,legacyWrites=0;
        std::shared_ptr<QJsonObject> productionTrace;
        QObject::connect(&controller,&FpgaDeviceController::extensionCompleted,&controller,
            [&](const QJsonObject& response) {
                auto entry=response;
                if(!response.value("ok").toBool() && session && productionTrace)
                    entry["session_diagnostics"]=sessionTrace(*session,productionTrace);
                exchanges.append(entry);
            });
        QObject::connect(&controller,&FpgaDeviceController::rawCommand,&controller,
            [&](quint64,const QByteArray& bytes) {
                FpgaVlp1::StreamParser parser;
                for(const auto& command:parser.feed(reinterpret_cast<const std::uint8_t*>(bytes.constData()),size_t(bytes.size()))) {
                    if(command.header.message!=3 || command.payload.size()<12)continue;
                    const auto address=qFromLittleEndian<quint32>(command.payload.data());
                    if(address>=0x6a00 && address<0x6b00)++legacyWrites;
                    if(address!=0x6818 && address!=0x6918)continue;
                    auto& uart=uartCommands[(address-0x6818)/0x100];
                    const char byte=char(qFromLittleEndian<quint32>(command.payload.data()+8)&255);uart.append(byte);
                    if(byte!='@')continue;
                    observedQueries.append(QJsonObject{{"channel",int((address-0x6818)/0x100)+1},
                        {"command",QString::fromLatin1(uart)}});
                    if(!uart.endsWith("=?@") && uart!="DATADEMAND=1@" && uart!="INQUIRE=1@")++instrumentWrites;
                    uart.clear();
                }
            });
        QObject::connect(&controller,&FpgaDeviceController::temperatureDataUpdated,&controller,
            [&](const TemperatureControllerData& value) {
                data=value;
                if(validSample(value))++samples;
            });
        const auto cleanup=[&] {
            if(cleanupDone)return;
            cleanupDone=true;
            if(started && controller.ready()) {
                if(!pumpUntil([&]{return !controller.busy();},6500)) {
                    configurationRestored=false;error+=QStringLiteral(" Could not acquire controller for UART cleanup.");
                } else {
                    controller.setSensorEnabled(0x4a,false);
                    if(!pumpUntil([&]{return !controller.busy();},6500))configurationRestored=false;
                }
            }
            // Stop the production poll and close its sole USB owner before
            // restoring configuration through a fresh, sequential owner.
            controller.disconnectDevice();
            FpgaDeviceSession restoration(makeFpgaUsbHardwareTransport());
            const auto restoreTrace=traceSession(restoration);
            QString restoreOpenError;
            if(restoration.open(locator,&restoreOpenError)) {
                for(quint32 base:{0x6800u,0x6900u}) {
                    QString detail;
                    if(!writeAndRead(restoration,base+4,0,detail)
                        || !writeAndRead(restoration,base+0x10,original.value(base+0x10),detail)
                        || !writeAndRead(restoration,base+0x14,original.value(base+0x14),detail)
                        || !writeAndRead(restoration,base+4,original.value(base+4),detail)) {
                        configurationRestored=false;error+=' '+detail;
                    }
                }
                QString globalDetail;
                if(!writeAndRead(restoration,4,original.value(4),globalDetail)) {
                    configurationRestored=false;error+=' '+globalDetail;
                }
                for(quint32 address:addresses) {
                    quint32 value=0;QString detail;
                    if(sessionCommand(restoration,address,false,value,detail))restored.insert(address,value);
                    else {configurationRestored=false;error+=' '+detail;}
                }
                for(quint32 address:{4u,0x6704u,0x6804u,0x6810u,0x6814u,0x6904u,0x6910u,0x6914u})
                    if(restored.value(address)!=original.value(address))configurationRestored=false;
                for(quint32 control:otherControls)
                    if(!restored.contains(control) || restored.value(control)!=original.value(control))configurationRestored=false;
                // A deliberately unconnected UART may have a floating RX pin.
                // Observe its errors without treating them as configuration.
                for(quint32 base:{0x6800u,0x6900u})if(expected(base==0x6800?1:2))
                    for(quint32 offset:{12u,0x30u,0x34u,0x38u})
                        if(restored.value(base+offset)!=original.value(base+offset))expectedChannelsErrorFree=false;
                document["restore_session_diagnostics"]=sessionTrace(restoration,restoreTrace);
                restoration.close();
            } else {configurationRestored=false;error+=' '+restoreOpenError;
                document["restore_session_diagnostics"]=sessionTrace(restoration,restoreTrace);}
        };
        struct Cleanup {std::function<void()> action;~Cleanup(){if(action)action();}} guard{cleanup};
        try {
            controller.connectDevice(locator,"auto",FpgaDeviceController::ConnectMode::PreserveRunningModules);
            if(!pumpUntil([&]{return !controller.busy();},15000) || !controller.ready())
                error=QStringLiteral("Production Controller handshake failed.");
            else {
                session=controller.findChild<FpgaDeviceSession*>();
                if(!session)error=QStringLiteral("Production Controller session was unavailable.");
                else {
                    started=true;
                    productionTrace=traceSession(*session);
                    controller.connectSensor({{"op","sensor"},{"source",0x4a},{"enabled",true}});
                    sampled=pumpUntil([&]{return controller.temperatureConnected() && validSample(data) && samples>=2;},60000);
                    if(!sampled)error=QStringLiteral("Production Controller did not receive two valid samples from the expected channel(s).");
                    document["sample"]=temperatureDocument(data);document["samples"]=samples;
                    document["production_session_diagnostics"]=sessionTrace(*session,productionTrace);
                }
            }
        } catch(const std::exception& exception) {
            error=QString::fromUtf8(exception.what());sampled=false;
        } catch(...) {error=QStringLiteral("Unexpected hardware probe exception.");sampled=false;}
        cleanup();guard.action={};
        document["uart_queries"]=observedQueries;
        document["uart_exchanges"]=exchanges;
        document["instrument_parameter_writes"]=instrumentWrites;
        document["instrument_output_writes"]=instrumentWrites;
        document["legacy_6a_writes"]=legacyWrites;
        if(instrumentWrites || legacyWrites) {sampled=false;error+=QStringLiteral(" Unexpected instrument/legacy write observed.");}
        document["disconnect_motor_safety_original_control_zero"]=true;
        document["global_enable_restored"]=restored.contains(4) && restored.value(4)==original.value(4);
    }
    document["restored_registers"]=registerDocument(restored);
    document["preflight_ok"]=preflight;document["configuration_restored"]=configurationRestored;
    document["error_flags_cleared"]=false;document["counter_reset_performed"]=false;
    document["expected_channels_error_free"]=expectedChannelsErrorFree;
    document["ok"]=preflight && sampled && configurationRestored && expectedChannelsErrorFree;document["detail"]=error;
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if(!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(document).toJson())<0 || !file.commit()) {
        std::fprintf(stderr,"Could not save hardware probe result: %s\n",file.errorString().toUtf8().constData());return 1;
    }
    std::printf("Hardware read-only result: %s\n",path.toUtf8().constData());
    return document.value("ok").toBool()?0:1;
}
struct Fixture {
    FpgaDeviceController controller{makeFpgaUsbSimulator()};
    QVector<FpgaVlp1::Frame> commands;
    TemperatureControllerData data;
    int updates=0;
    Fixture()
    {
        QObject::connect(&controller,&FpgaDeviceController::rawCommand,&controller,
            [this](quint64,const QByteArray& bytes) {
                FpgaVlp1::StreamParser parser;
                const auto frames=parser.feed(reinterpret_cast<const std::uint8_t*>(bytes.constData()),size_t(bytes.size()));
                for(const auto& frame:frames)commands.append(frame);
            });
        QObject::connect(&controller,&FpgaDeviceController::temperatureDataUpdated,&controller,
            [this](const TemperatureControllerData& value) {data=value;++updates;});
        controller.connectDevice("simulation://fpga","simulator",
            FpgaDeviceController::ConnectMode::PreserveRunningModules);
        waitUntil([this]{return !controller.busy();},"controller handshake completed");
        require(controller.ready(),"controller accepts simulated FPGA handshake");
    }
    void connectTemperature()
    {
        controller.connectSensor({{"op","sensor"},{"source",0x4a},{"enabled",true}});
        waitUntil([this]{return controller.temperatureConnected() && updates>0;},
            "both simulated physical TEC103 ports complete identity and full protocol reads");
    }
    QJsonObject call(QJsonObject request)
    {
        static int ticket=0;
        const auto identity=QString::number(++ticket);
        request.insert("ticket",identity);
        QEventLoop loop;QTimer deadline;deadline.setSingleShot(true);
        QTimer retry;retry.setInterval(2);QJsonObject result;bool submitted=false;
        const auto observer=QObject::connect(&controller,&FpgaDeviceController::extensionCompleted,&loop,
            [&](const QJsonObject& response) {
                if(response.value("ticket").toString()==identity) {result=response;loop.quit();}
            });
        QObject::connect(&retry,&QTimer::timeout,&loop,[&] {
            if(submitted || controller.busy())return;
            submitted=true;retry.stop();controller.executeOperation(request);
        });
        QObject::connect(&deadline,&QTimer::timeout,&loop,&QEventLoop::quit);
        deadline.start(10000);retry.start();loop.exec();QObject::disconnect(observer);
        require(!result.isEmpty(),"production UART extension completed");return result;
    }
    int txBytes() const
    {
        int count=0;
        for(const auto& command:commands) {
            if(command.header.message!=3 || command.payload.size()<8)continue;
            const auto address=qFromLittleEndian<quint32>(command.payload.data());
            if(address==0x6818 || address==0x6918)++count;
        }
        return count;
    }
    void assertNoLegacyWrites() const
    {
        for(const auto& command:commands) {
            if(command.header.message!=3 || command.payload.size()<8)continue;
            const auto address=qFromLittleEndian<quint32>(command.payload.data());
            require(address<0x6a00 || address>=0x6b00,"GPIO integration never writes the legacy CON19 RD105 page");
        }
    }
};
void controllerLifecycleAndIndependentPhysicalPorts()
{
    Fixture f;f.connectTemperature();
    require(f.data.valid && std::abs(f.data.channels[0].measured_temperature_c-24.5)<0.0001
        && std::abs(f.data.channels[1].measured_temperature_c-26.5)<0.0001,
        "logical CH1/CH2 come from internal TC1 on independent UARTs and ignore TC2 zero placeholders");
    require(std::abs(f.data.channels[0].target_temperature_c-27.3)<0.0001
        && std::abs(f.data.channels[1].target_temperature_c-26.0)<0.0001,
        "independent instrument targets survive the two-port merge");
    require(!f.data.channels[0].output_enabled && !f.data.channels[1].output_enabled,
        "connecting and polling do not enable either instrument output");
    const int initialUpdates=f.updates;
    waitUntil([&]{return f.updates>initialUpdates;},"periodic polling delivers another typed sample",6000);
    const Tec103GpioProtocol::Call call=[&](const QJsonObject& request){return f.call(request);};
    auto changed=Tec103GpioProtocol::writeVerified(call,2,
        {{"TC1:TG",2810000},{"TC1:KP",321},{"TC1:KI",123},{"TC1:KD",7},{"TC1:ENABLE",1}});
    if(!changed.success || changed.unverified)std::fprintf(stderr,"TEC103 write failure: %s\n",changed.message.toUtf8().constData());
    require(changed.success && !changed.unverified,"target/PID/enable use native commands and independent readback");
    waitUntil([&]{return f.data.channels[1].kp==321 && f.data.channels[1].ki==123
        && f.data.channels[1].kd==7;},"parameter write invalidates polling cache and refreshes confirmed PID",10000);
    const int changedUpdates=f.updates;
    waitUntil([&]{return f.updates>changedUpdates;},"next polling cycle after parameter confirmation",6000);
    require(f.data.channels[1].kp==321 && f.data.channels[1].ki==123 && f.data.channels[1].kd==7,
        "the next poll never restores stale PID parameters");
    const auto readback=Tec103GpioProtocol::read(call,2,true);
    require(readback.success && std::abs(readback.data.channels[1].target_temperature_c-28.1)<0.0001
        && readback.data.channels[1].kp==321 && readback.data.channels[1].ki==123
        && readback.data.channels[1].kd==7 && readback.data.channels[1].output_enabled,
        "full production read confirms the simulated physical channel 2 changes");
    const auto untouched=Tec103GpioProtocol::read(call,1,false);
    require(untouched.success && std::abs(untouched.data.channels[0].target_temperature_c-27.3)<0.0001
        && !untouched.data.channels[0].output_enabled,"physical channel 1 remains independent");
    require(Tec103GpioProtocol::writeVerified(call,2,{{"TC1:ENABLE",0}}).success,
        "test restores simulated instrument output before disconnect");
    waitUntil([&]{return !f.controller.busy();},"controller is idle before disconnect");
    f.controller.setSensorEnabled(0x4a,false);
    waitUntil([&]{return !f.controller.busy();},"GPIO disconnect register sequence completes");
    require(!f.controller.temperatureConnected()
        && !(f.controller.hardwareValues().value(0x6804)&1)
        && !(f.controller.hardwareValues().value(0x6904)&1),"disconnect disables both raw bridges");
    const int bytesAtStop=f.txBytes();
    QElapsedTimer stopped;stopped.start();
    while(stopped.elapsed()<1300) {QCoreApplication::processEvents(QEventLoop::AllEvents,10);QThread::msleep(1);}
    require(f.txBytes()==bytesAtStop,"disconnect stops periodic UART instrument queries");
    f.assertNoLegacyWrites();f.controller.disconnectDevice();
}
}
int main(int argc,char** argv)
{
    QCoreApplication app(argc,argv);
    const auto arguments=app.arguments();
    if(arguments.contains("--hardware-readonly")) {
        const auto argument=[&](const QString& key) {
            const int index=arguments.indexOf(key);return index>=0 && index+1<arguments.size()?arguments[index+1]:QString();
        };
        const auto expected=argument("--expected-channel");
        bool validExpected=expected.isEmpty();
        const int channel=expected.isEmpty()?0:expected.toInt(&validExpected);
        if(!validExpected)return 2;
        return hardwareReadonly(argument("--locator"),argument("--output"),channel);
    }
    controllerLifecycleAndIndependentPhysicalPorts();
    std::puts("TEC103 GPIO controller tests passed");return 0;
}
