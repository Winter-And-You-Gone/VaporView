#include "ground/devices/FpgaInstrumentService.h"
#include "Ai8TemperatureControllerCollector.h"
#include "Tec103GpioProtocol.h"
#include <QEventLoop>
#include <QPointer>
#include <QThread>
#include <QTimer>
#include <QtEndian>
#include <cmath>
#include <limits>

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
LocalTemperatureCommandResult readFpgaTec103Data(const FpgaInstrumentCall& call,int logicalChannel,bool full)
{
    const auto response=Tec103GpioProtocol::read(call,logicalChannel,full);
    LocalTemperatureCommandResult result;
    result.status=response.success?LocalTemperatureCommandStatus::Confirmed:LocalTemperatureCommandStatus::Rejected;
    result.latestData=response.data;result.message=response.message;result.unverified=response.unverified;
    return result;
}

LocalTemperatureCommandResult sendFpgaTemperatureCommand(const FpgaInstrumentCall& call,CommandId command,const TemperatureControllerCommand& payload)
{
    LocalTemperatureCommandResult result;result.status=LocalTemperatureCommandStatus::Rejected;
    if(command==CommandId::QueryDeviceStatus) {
        const auto first=Tec103GpioProtocol::read(call,1,true);
        const auto second=Tec103GpioProtocol::read(call,2,true);
        result.latestData.channels[0]=first.data.channels[0];result.latestData.channels[1]=second.data.channels[1];
        result.latestData.valid=first.data.valid || second.data.valid;
        result.latestData.internal_temperature_c=std::numeric_limits<double>::quiet_NaN();
        result.latestData.error_code=quint16(first.data.error_code | second.data.error_code);
        const bool both=first.success && second.success;
        result.latestData.device_address=both && first.data.device_address==second.data.device_address?first.data.device_address:-1;
        result.latestData.controller_mode=both && first.data.controller_mode==second.data.controller_mode?first.data.controller_mode:-1;
        result.latestData.overtemp_output_mode=both && first.data.overtemp_output_mode==second.data.overtemp_output_mode?first.data.overtemp_output_mode:-1;
        result.latestData.rs485_baud_index=-1;
        result.latestData.timestamp=first.data.timestamp>second.data.timestamp?first.data.timestamp:second.data.timestamp;
        if(both)result.status=LocalTemperatureCommandStatus::Confirmed;
        if(!both || !first.message.isEmpty() || !second.message.isEmpty())
            result.message=QStringLiteral("GPIO1：%1；GPIO2：%2。")
                .arg(first.message.isEmpty()?(first.success?QStringLiteral("读取完成"):QStringLiteral("读取失败")):first.message,
                     second.message.isEmpty()?(second.success?QStringLiteral("读取完成"):QStringLiteral("读取失败")):second.message);
        return result;
    }
    if(command==CommandId::SetTemperatureDeviceAddress || command==CommandId::SetTemperatureRs485Baud
        || command==CommandId::RestoreTemperatureFactoryDefaults) {
        result.message=QStringLiteral("GPIO TEC103 不提供实体站号、串口参数修改或恢复出厂；当前 ASCII 链路为 38400/8N1，RS485 参数读回命名与手册不一致，未发送维护命令。");
        return result;
    }
    if(command==CommandId::SetTemperatureSensorConfig) {
        result.message=QStringLiteral("TEC103/4370 的传感器参数与通用 RD105 模型存在单位差异；复合传感器及多项式写入尚未确认，未修改实体参数。");
        return result;
    }
    if(command==CommandId::SetTemperatureControllerMode || command==CommandId::SetTemperatureOvertempOutputMode) {
        result.message=QStringLiteral("当前通用参数入口没有选择物理 TEC103；两台控制器的 CONTMODE 和 OVERTTEMP 独立，不能作为共享参数修改，未发送。");
        return result;
    }
    QVector<Tec103GpioProtocol::Setting> settings;
    bool precision=true;
    const auto scaled=[&](double value,double scale,double minimum,double maximum) -> qint64 {
        if(!std::isfinite(value) || value<minimum || value>maximum) {precision=false;return 0;}
        const double raw=value*scale,rounded=std::round(raw);
        if(std::abs(raw-rounded)>0.000001)precision=false;
        return qint64(rounded);
    };
    switch(command) {
    case CommandId::SetTemperatureTarget:settings.append({"TC1:TG",scaled(payload.target_temperature_c,100000,-400,1000)});break;
    case CommandId::SetTemperatureOutputEnabled:settings.append({"TC1:ENABLE",payload.output_enabled?1:0});break;
    case CommandId::SetTemperatureOutputMode:settings.append({"TC1:MODE",payload.output_mode});break;
    case CommandId::SetTemperatureMaxOutputPercent:settings.append({"TC1:LIMITED",payload.max_output_percent});break;
    case CommandId::SetTemperaturePid:
        settings.append({"TC1:KP",payload.kp});settings.append({"TC1:KI",payload.ki});settings.append({"TC1:KD",payload.kd});break;
    case CommandId::SetTemperatureAutoPid:
        if(payload.auto_pid_mode>1) {result.message=QStringLiteral("TEC103 的 PID 模式 2 为预留功能，未发送。");return result;}
        settings.append({"TC1:AUTOPID",payload.auto_pid_mode});break;
    case CommandId::SetTemperatureOvertempUpper:settings.append({"TC1:OVERTEMPUP",scaled(payload.overtemp_upper_c,100000,-3000,5000)});break;
    case CommandId::SetTemperatureOvertempLower:settings.append({"TC1:OVERTEMPLOWER",scaled(payload.overtemp_lower_c,100000,-3000,5000)});break;
    case CommandId::SetTemperatureSlope:settings.append({"TC1:SPEED",scaled(payload.temperature_slope_c_per_s,1000,0,10)});break;
    case CommandId::SetTemperatureStartupDelay:settings.append({"TC1:STARTUPDELAY",payload.startup_delay_s});break;
    default:result.message=QStringLiteral("此温控命令尚未适配 GPIO TEC103，未发送。");return result;
    }
    if(!precision) {result.message=QStringLiteral("TEC103 参数超出范围或原生精度，未发送。");return result;}
    const auto write=Tec103GpioProtocol::writeVerified(call,int(payload.channel),settings);
    result.unverified=write.unverified;result.message=write.message;
    if(!write.success)return result;
    result.status=LocalTemperatureCommandStatus::Confirmed;
    const auto refresh=Tec103GpioProtocol::read(call,int(payload.channel),false);
    if(refresh.success)result.latestData=refresh.data;
    else result.message=QStringLiteral("TEC103 写入已独立读回确认，但状态刷新失败：%1").arg(refresh.message);
    return result;
}
}
