#include "FpgaDeviceController.h"
#include "FpgaExtensions.h"
#include "FpgaTelemetry.h"
#include <QtEndian>
#include <cmath>

namespace VaporView::Ground::Devices {
void FpgaDeviceController::callback(std::function<void()> action)
{
    Step step;step.kind=Kind::Callback;step.callback=std::move(action);steps_.push_back(std::move(step));
}

void FpgaDeviceController::readExtensions()
{
    // Capabilities arrive during the handshake, after readAll was queued.
    auto remaining=std::move(steps_);steps_.clear();
    const auto bits=extensions();emit capabilitiesUpdated(bits);
    if(bits&FpgaExtensions::Ai8Scan) {read(0x6668,2);read(0x6698);read(0x66a0,6);read(0x66fc);}
    if(bits&FpgaExtensions::Ai8Parameters)read(0x6684,5);
    if(bits&FpgaExtensions::Rd105) {read(0x6800,19);read(0x6884,4);}
    if(bits&FpgaExtensions::EpsilonMailbox)read(0x6244,7);
    if(bits&FpgaExtensions::DliaCutoff) {read(0x5058,2);read(0x5158,2);}
    for(auto& step:remaining)steps_.push_back(std::move(step));
}

void FpgaDeviceController::executeExtension(const QJsonObject& request)
{
    const auto reject=[this,&request](const QString& detail) {
        emit extensionCompleted({{"op",request.value("op")},{"ticket",request.value("ticket")},{"ok",false},{"detail",detail}});
        report(detail);
    };
    if(!FpgaRemote::validateControl(request)) {reject(QStringLiteral("扩展操作参数无效。"));return;}
    if(!ready_ || busy()) {reject(QStringLiteral("FPGA 未就绪或已有事务正在执行。"));return;}
    const auto op=request.value("op").toString();
    const quint16 source=quint16(request.value("source").toInt());
    const quint32 required=op=="epsilon_mailbox"?FpgaExtensions::EpsilonMailbox:
        op=="dlia_cutoff"?FpgaExtensions::DliaCutoff:(op=="ai8_scan" || op=="ai8_cache")?FpgaExtensions::Ai8Scan:
        source==0x48?FpgaExtensions::Rd105:op=="instrument"?FpgaExtensions::Ai8Parameters:
        request.value("channel").toInt()?FpgaExtensions::Ai8Scan:0;
    if(required && !(extensions()&required)) {reject(QStringLiteral("当前固件未报告此扩展能力；请更新下位机后重新连接。"));return;}
    const QString label=op=="device_temperature"?(source==0x48?QStringLiteral("RD105 设温与确认"):QStringLiteral("AI8 设温与确认")):
        op=="instrument"?(source==0x48?QStringLiteral("RD105 参数事务与回读"):QStringLiteral("AI8 参数事务与回读")):
        op=="epsilon_mailbox"?QStringLiteral("EPSILON 命令与 RTCM 转发"):op=="ai8_cache"?QStringLiteral("AI8 通道缓存读回"):
        op=="ai8_scan"?QStringLiteral("AI8 扫描配置"):QStringLiteral("DLIA 截止频率配置");
    if(!begin(label))return;
    extensionRequest_=request;extensionResult_={{"op",op},{"ticket",request.value("ticket")},{"source",source},{"verified",false}};
    if(op=="instrument") {
        const quint32 base=source==0x48?0x6800:0x6600;
        if(!(registers_.value(base+4)&1)) {fail(QStringLiteral("参数操作前请先启用对应设备。"));return;}
        const bool rd=source==0x48;
        const quint32 counter=base+(rd?0x90:0x94),status=base+(rd?0x8c:0x8c);
        const quint32 operation=quint32(request.value("action").toInt());
        const quint32 address=quint32(request.value("address").toInt());
        const auto data=QByteArray::fromBase64(request.value("data").toString().toLatin1());
        const int count=request.value("count").toInt(1);
        read(counter);callback([this,counter]{extensionResult_["baseline"]=double(registers_.value(counter));});
        write(base+0x84,address);
        if(rd) {
            write(base+0x88,quint32(count));
            if(operation!=1)for(int i=0;i<(data.size()+3)/4;++i) {
                auto word=data.mid(i*4,4).leftJustified(4,'\0');
                write(base+0xc0+quint32(i*4),qFromLittleEndian<quint32>(word.constData()));
            }
        } else if(operation==2)write(base+0x88,quint32(quint8(data[0]))*256+quint8(data[1]));
        write(base+0x80,operation);
        waitFor(status,0x30000,0x20000);read(counter);
        if(rd)read(base+0xe0,8);else read(base+0x90);
        callback([this,base,counter,status,operation,rd,count,data] {
            const quint32 state=registers_.value(status);
            const quint32 code=state&0xf;
            extensionResult_["status"]=double(state);extensionResult_["counter"]=double(registers_.value(counter));
            if(registers_.value(counter)!=quint32(extensionResult_.value("baseline").toDouble())+1u
                || (state&0x3ffff)!=(0x20000u+code) || (code!=0 && !(rd && operation==3 && code==6))) {
                fail(QStringLiteral("设备参数事务未通过：状态 0x%1；执行结果可能未知，未自动重试。").arg(state,8,16,QLatin1Char('0')));return;
            }
            QByteArray actual;
            if(rd)for(int i=0;i<8;++i) {char word[4];qToLittleEndian(registers_.value(base+0xe0+quint32(i*4)),word);actual.append(word,4);}
            else {const auto raw=registers_.value(base+0x90);actual.append(char(raw>>8));actual.append(char(raw));}
            actual.truncate(count*2);
            extensionResult_["data"]=QString::fromLatin1(actual.toBase64());
            extensionResult_["verified"]=code==0;
            extensionResult_["unverified"]=code==6;
            if(operation==2 && actual!=data) {fail(QStringLiteral("参数实际读回值与写入值不同。"));return;}
            if(code==6) {
                read(0x6804,3);
                report(QStringLiteral("RD105 恢复出厂收到写 ACK，最终状态未验证；轮询已停止，请按恢复后的通信参数重新连接。"),LogLevel::Warning);
            }
        });
    } else if(op=="epsilon_mailbox") {
        if(!(registers_.value(0x6204)&1)) {fail(QStringLiteral("EPSILON 命令或 RTCM 转发前请先启用 EPSILON。"));return;}
        const quint32 kind=quint32(request.value("kind").toInt());
        const auto data=QByteArray::fromBase64(request.value("data").toString().toLatin1());
        read(0x6254,2);callback([this] {
            extensionResult_["baseline"]=double(registers_.value(0x6254));
            extensionResult_["failure_baseline"]=double(registers_.value(0x6258));
        });
        write(0x6248,kind);write(0x6244,quint32(data.size()));
        write(0x6274,quint32(request.value("target").toInt()));
        write(0x6270,quint32(request.value("timeout_ms").toInt(2000)));
        if(request.contains("baud"))write(0x6278,quint32(request.value("baud").toInt()));
        for(int i=0;i<(data.size()+3)/4;++i) {
            auto word=data.mid(i*4,4).leftJustified(4,'\0');write(0x6268,qFromLittleEndian<quint32>(word.constData()));
        }
        verify(0x626c,quint32((data.size()+3)/4));write(0x6240,1);
        waitFor(0x624c,0x30000,0x20000);read(0x6250,4);read(0x627c);
        callback([this,kind,data] {
            const quint32 state=registers_.value(0x624c);
            extensionResult_["status"]=double(state);
            const bool counter=registers_.value(0x6254)==quint32(extensionResult_.value("baseline").toDouble())+1u;
            if(state!=0x20000 || !counter || registers_.value(0x6258)!=quint32(extensionResult_.value("failure_baseline").toDouble())) {
                fail(QStringLiteral("EPSILON 邮箱事务失败，状态 0x%1；未自动重试。").arg(state,8,16,QLatin1Char('0')));return;
            }
            extensionResult_["uart_bytes"]=double(registers_.value(0x6250));
            extensionResult_["rtcm_bytes"]=double(registers_.value(0x627c));
            if(kind==1) {extensionResult_["data"]=QString();extensionResult_["verified"]=false;return;}
            const quint32 size=registers_.value(0x625c);
            if(size>2048) {fail(QStringLiteral("EPSILON 响应长度超出协议范围。"));return;}
            auto response=std::make_shared<QByteArray>();
            for(quint32 i=0;i<size;i+=4) {
                write(0x6260,i);read(0x6264);
                callback([this,response]{char word[4];qToLittleEndian(registers_.value(0x6264),word);response->append(word,4);});
            }
            callback([this,response,size] {
                response->truncate(int(size));extensionResult_["data"]=QString::fromLatin1(response->toBase64());
                // The existing EPSILON command parser determines device ACK / readback.
                extensionResult_["verified"]=false;
            });
        });
    } else if(op=="ai8_cache") {
        const auto channel=quint32(request.value("channel").toInt());
        write(0x6698,channel);verify(0x6698,channel);read(0x66a0,6);
        callback([this,channel] {
            extensionResult_["channel"]=double(channel);extensionResult_["status"]=double(registers_.value(0x66a0));
            extensionResult_["pv_sp"]=double(registers_.value(0x66a4));extensionResult_["sv_op"]=double(registers_.value(0x66a8));
            extensionResult_["alarm_control_host"]=double(registers_.value(0x66ac));
            extensionResult_["tick"]=QString::number((quint64(registers_.value(0x66b4))<<32)|registers_.value(0x66b0));extensionResult_["verified"]=true;
        });
    } else if(op=="ai8_scan") {
        if(registers_.value(0x6604)&1) {fail(QStringLiteral("切换 AI8 扫描模式前请先断开 AI8。"));return;}
        const quint32 enable=request.value("enabled").toBool()?1:0;
        write(0x6668,enable);verify(0x6668,enable);
        callback([this,enable]{configuration_.ai8.scan=enable!=0;extensionResult_["verified"]=true;});
    } else if(op=="dlia_cutoff") {
        const quint32 base=0x5000+quint32(request.value("channel").toInt())*0x100;
        const quint32 hz=quint32(request.value("hz").toInt());
        if(hz!=1000 && registers_.value(0x4014)!=1000000) {fail(QStringLiteral("非默认截止频率要求 ADC 实际采样率为固件编译标称值 1 MHz。"));return;}
        stopAndDrain();write(base+0x58,hz);verify(base+0x58,hz);commit(quint16(0x30+(base-0x5000)/0x100),base);verify(base+0x5c,hz);
        callback([this,base,hz]{configuration_.dlia[(base-0x5000)/0x100].cutoffHz=hz;extensionResult_["verified"]=true;});
    } else if(op=="device_temperature") {
        const double c=request.value("celsius").toDouble();
        const quint32 base=source==0x48?0x6800:0x6600;
        if(!(registers_.value(base+4)&1)) {fail(QStringLiteral("设温前请先启用对应设备。"));return;}
        const quint32 target=quint32(qint32(std::llround(c*1000000)));
        read(base+0x48);callback([this,base]{extensionResult_["baseline"]=double(registers_.value(base+0x48));});
        if(source==0x46 && request.contains("channel")) {write(0x666c,quint32(request.value("channel").toInt()));verify(0x666c,quint32(request.value("channel").toInt()));}
        Step s;s.kind=Kind::Sensor;s.address=source==0x48?18:17;s.source=source;s.expected=target;steps_.push_back(s);
        callback([this,base,target,source] {
            // Completion is the verified device counter, not the VLP write ACK.
            waitFor(base+0x48,0xffffffff,quint32(extensionResult_.value("baseline").toDouble())+1u);
            if(source==0x48)verify(0x6840,target);
            else {read(0x664c);read(0x6654);callback([this,target] {
                const auto raw=quint16(qint32(target)/100000);
                if(registers_.value(0x664c)!=0x10000 || registers_.value(0x6654)!=(quint32(raw)<<16|raw))fail(QStringLiteral("AI8 设温回读不匹配。"));
            });}
            callback([this]{extensionResult_["verified"]=true;});
        });
    }
    advance();
}
}
