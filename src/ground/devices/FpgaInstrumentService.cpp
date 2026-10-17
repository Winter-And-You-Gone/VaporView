#include "ground/devices/FpgaInstrumentService.h"
#include "Ai8TemperatureControllerCollector.h"
#include "TemperatureControllerProtocol.h"
#include <QEventLoop>
#include <QPointer>
#include <QThread>
#include <QTimer>
#include <QtEndian>

namespace VaporView::Ground::Devices {
FpgaInstrumentCall makeFpgaInstrumentCall(QObject* owner,FpgaInstrumentSubmit submit)
{
    QPointer<QObject> guard(owner);
    return [guard,submit=std::move(submit)](const QJsonObject& request) -> QJsonObject {
        if(!guard || QThread::currentThread()==guard->thread())return {{"ok",false},{"detail","FPGA instrument calls require a worker thread"}};
        if(QThread::currentThread()->isInterruptionRequested())return {{"ok",false},{"detail","Instrument worker stopped"}};
        QEventLoop loop;QTimer timer;timer.setSingleShot(true);QTimer interruption;
        QJsonObject result{{"ok",false},{"detail",QStringLiteral("设备事务等待超时，执行结果未知；未自动重试。")}};
        QPointer<QEventLoop> receiver(&loop);
        QObject::connect(&timer,&QTimer::timeout,&loop,&QEventLoop::quit);
        QObject::connect(&interruption,&QTimer::timeout,&loop,[&loop,guard]{if(!guard || QThread::currentThread()->isInterruptionRequested())loop.quit();});interruption.start(100);
        QMetaObject::invokeMethod(guard,[guard,submit,request,receiver,&result] {
            if(!guard || !receiver)return;
            submit(request,[receiver,&result](const QJsonObject& reply) {
                if(receiver)QMetaObject::invokeMethod(receiver,[receiver,reply,&result] {
                    if(receiver) {result=reply;receiver->quit();}
                },Qt::QueuedConnection);
            });
        },Qt::QueuedConnection);
        timer.start(30000);loop.exec();return result;
    };
}
namespace {
QJsonObject parameter(const FpgaInstrumentCall& call,quint16 source,quint16 address,int count,int action,const QByteArray& data={})
{
    return call({{"op","instrument"},{"source",source},{"address",address},{"count",count},{"action",action},{"data",QString::fromLatin1(data.toBase64())}});
}
void ai8Backend(Ai8TemperatureControllerCollector& collector,const FpgaInstrumentCall& call,QString& error)
{
    collector.setRegisterBackend(
        [call,&error](quint16 address,quint16 count,std::vector<quint16>& values) {
            values.clear();
            for(quint16 i=0;i<count;++i) {
                const auto result=parameter(call,0x46,quint16(address+i),1,1);
                const auto data=QByteArray::fromBase64(result.value("data").toString().toLatin1());
                if(!result.value("ok").toBool() || !result.value("verified").toBool() || data.size()!=2) {error=result.value("detail").toString();return false;}
                values.push_back(qFromBigEndian<quint16>(data.constData()));
            }
            return true;
        },
        [call](quint16 address,quint16 value,QString* error) {
            char bytes[2];qToBigEndian(value,bytes);
            const auto result=parameter(call,0x46,address,1,2,QByteArray(bytes,2));
            if(error)*error=result.value("detail").toString();
            return result.value("ok").toBool() && result.value("verified").toBool();
        });
}
}
LocalAi8OperationResult readFpgaAi8Page(const FpgaInstrumentCall& call,Ai8TemperatureControllerProtocol::Page page,
    const Ai8TemperatureControllerProtocol::Selection& selection)
{
    LocalAi8OperationResult result;Ai8TemperatureControllerCollector collector;
    ai8Backend(collector,call,result.message);
    result.success=collector.readPage(page,selection,result.data,&result.message);return result;
}
LocalAi8OperationResult writeFpgaAi8Page(const FpgaInstrumentCall& call,const Ai8TemperatureControllerProtocol::PageData& data)
{
    LocalAi8OperationResult result;Ai8TemperatureControllerCollector collector;
    ai8Backend(collector,call,result.message);
    result.success=collector.writePage(data,&result.message);
    if(result.success)result.success=collector.readPage(data.page,data.selection,result.data,&result.message);
    return result;
}
LocalTemperatureCommandResult sendFpgaTemperatureCommand(const FpgaInstrumentCall& call,CommandId command,const TemperatureControllerCommand& payload)
{
    LocalTemperatureCommandResult result;
    TemperatureControllerCollector collector;
    collector.setRegisterBackend(
        [call](quint16 address,quint16 count,std::vector<quint16>& values) {
            const auto result=parameter(call,0x48,address,count,1);
            const auto data=QByteArray::fromBase64(result.value("data").toString().toLatin1());
            if(!result.value("ok").toBool() || data.size()!=count*2)return false;
            values.clear();for(int i=0;i<count;++i)values.push_back(qFromBigEndian<quint16>(data.constData()+i*2));return true;
        },
        [call,&result](quint16 address,const std::vector<quint16>& values) {
            QByteArray data;for(auto v:values) {char word[2];qToBigEndian(v,word);data.append(word,2);}
            const auto reply=parameter(call,0x48,address,int(values.size()),address==0?3:2,data);
            result.message=reply.value("detail").toString();result.unverified=reply.value("unverified").toBool();
            if(result.unverified)result.message=QStringLiteral("RD105 恢复出厂收到写 ACK，最终状态未验证；请按恢复后的通信参数重新连接。");
            // A factory-reset write ACK has no final-state verification.
            return reply.value("ok").toBool() && reply.value("verified").toBool();
        });
    const quint8 ch=payload.channel?payload.channel:1;
    bool ok=false;
    switch(command) {
    case CommandId::QueryDeviceStatus:ok=collector.readConfiguration(result.latestData,ch);break;
    case CommandId::SetTemperatureTarget:ok=collector.setTargetTemperature(ch,payload.target_temperature_c);break;
    case CommandId::SetTemperatureOutputEnabled:ok=collector.setOutputEnabled(ch,payload.output_enabled);break;
    case CommandId::SetTemperatureOutputMode:ok=collector.setOutputMode(ch,payload.output_mode);break;
    case CommandId::SetTemperatureMaxOutputPercent:ok=collector.setMaxOutputPercent(ch,payload.max_output_percent);break;
    case CommandId::SetTemperaturePid:ok=collector.setPid(ch,payload.kp,payload.ki,payload.kd);break;
    case CommandId::SetTemperatureAutoPid:ok=collector.setAutoPid(ch,payload.auto_pid_mode);break;
    case CommandId::SetTemperatureOvertempUpper:ok=collector.setOvertempUpper(ch,payload.overtemp_upper_c);break;
    case CommandId::SetTemperatureOvertempLower:ok=collector.setOvertempLower(ch,payload.overtemp_lower_c);break;
    case CommandId::SetTemperatureSlope:ok=collector.setTemperatureSlope(ch,payload.temperature_slope_c_per_s);break;
    case CommandId::SetTemperatureStartupDelay:ok=collector.setStartupDelay(ch,payload.startup_delay_s);break;
    case CommandId::SetTemperatureSensorConfig:ok=collector.setSensorConfig(ch,payload.sensor_model,payload.ntc_b,payload.ntc_r0,payload.pt_r0,payload.pt_a,payload.pt_b,payload.pt_c,payload.polynomial_mantissas,payload.polynomial_exponents);break;
    case CommandId::SetTemperatureControllerMode:ok=collector.setControllerMode(payload.controller_mode);break;
    case CommandId::SetTemperatureDeviceAddress:ok=collector.setDeviceAddress(payload.device_address);break;
    case CommandId::SetTemperatureRs485Baud:ok=collector.setRs485BaudIndex(payload.rs485_baud_index);break;
    case CommandId::SetTemperatureOvertempOutputMode:ok=collector.setOvertempOutputMode(payload.overtemp_output_mode);break;
    case CommandId::RestoreTemperatureFactoryDefaults:ok=collector.restoreFactoryDefaults();break;
    default:break;
    }
    result.status=ok?LocalTemperatureCommandStatus::Confirmed:LocalTemperatureCommandStatus::Rejected;
    return result;
}
}
