#include "FpgaDeviceController.h"
#include "FpgaExtensions.h"
#include "FpgaTelemetry.h"
#include "FpgaDliaSos.h"
#include <QJsonArray>
#include <QtEndian>
#include <cmath>

namespace VaporView::Ground::Devices {
void FpgaDeviceController::probe(quint32 address, quint32 api)
{
    read(address);steps_.back().label=QStringLiteral("optional_api");
    callback([this,address,api] {
        if(registers_.value(address)!=api)return;
        auto remaining=std::move(steps_);steps_.clear();
        const quint32 base=address&~255u;
        if(api==FpgaDliaSos::Api)read(base+0x60,40);
        else read(base+0x4c,16);
        for(auto& step:remaining)steps_.push_back(std::move(step));
    });
}

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
    if(bits&FpgaExtensions::Rd105) {read(0x6a00,20);read(0x6a84,4);}
    if(bits&FpgaExtensions::EpsilonMailbox)read(0x6244,7);
    if(bits&FpgaExtensions::DliaCutoff) {read(0x5058,2);read(0x5158,2);}
    for(quint32 base:{0x5000u,0x5100u})probe(base+0xf0,FpgaDliaSos::Api);
    for(quint32 base:{0x2000u,0x2100u})probe(base+0x84,FpgaDliaSos::PpsApi);
    for(auto& step:remaining)steps_.push_back(std::move(step));
}

void FpgaDeviceController::executeExtension(const QJsonObject& request, bool polling)
{
    const auto reject=[this,&request](const QString& detail) {
        emit extensionCompleted({{"op",request.value("op")},{"ticket",request.value("ticket")},{"ok",false},{"detail",detail}});
        report(detail);
    };
    if(!FpgaRemote::validateControl(request)) {reject(QStringLiteral("扩展操作参数无效。"));return;}
    if(!ready_ || busy()) {reject(QStringLiteral("FPGA 未就绪或已有事务正在执行。"));return;}
    const auto op=request.value("op").toString();
    const quint16 source=quint16(request.value("source").toInt());
    if(source==0x4a && (op=="instrument" || op=="device_temperature")) {
        reject(QStringLiteral("现场 TEC103 使用 LC0/LC1 GPIO UART；旧 6A RS485 温控事务已停用。"));return;
    }
    const quint32 required=(op=="uart_exchange" || op=="dlia_sos" || op=="wms_trigger" || op=="pps_snapshot")?0:op=="epsilon_mailbox"?FpgaExtensions::EpsilonMailbox:
        op=="dlia_cutoff"?FpgaExtensions::DliaCutoff:op=="ai8_cache"?FpgaExtensions::Ai8Scan:
        source==0x4a?FpgaExtensions::Rd105:op=="instrument"?FpgaExtensions::Ai8Parameters:
        request.value("channel").toInt()?FpgaExtensions::Ai8Scan:0;
    if(required && !(extensions()&required)) {reject(QStringLiteral("当前固件未报告此扩展能力；请更新下位机后重新连接。"));return;}
    const int channel=request.value("channel").toInt();
    if((op=="dlia_sos" && registers_.value(0x50f0+channel*0x100)!=FpgaDliaSos::Api)
        || ((op=="wms_trigger" || op=="pps_snapshot") && registers_.value(0x2084+channel*0x100)!=FpgaDliaSos::PpsApi)) {
        reject(QStringLiteral("当前通道未确认 SOS / PPS API；未发送新寄存器写入。"));return;
    }
    const QString label=op=="uart_exchange"?QStringLiteral("TEC103 GPIO 原生通信"):
        op=="device_temperature"?(source==0x4a?QStringLiteral("RD105 设温与确认"):QStringLiteral("AI8 设温与确认")):
        op=="instrument"?(source==0x4a?QStringLiteral("RD105 参数事务与回读"):QStringLiteral("AI8 参数事务与回读")):
        op=="epsilon_mailbox"?QStringLiteral("EPSILON 命令与 RTCM 转发"):op=="ai8_cache"?QStringLiteral("AI8 通道缓存读回"):
        op=="dlia_sos"?QStringLiteral("停止采集并提交 SOS 滤波器"):
        op=="wms_trigger"?QStringLiteral("停止采集并设置 PPS 触发"):op=="pps_snapshot"?QStringLiteral("读取 PPS 扫描快照"):QStringLiteral("DLIA 截止频率配置");
    if(!begin(label,false,!polling))return;
    tec103PollExchange_=polling;
    extensionRequest_=request;extensionResult_={{"op",op},{"ticket",request.value("ticket")},{"source",source},{"verified",false}};
    if(request.contains("channel"))extensionResult_["channel"]=request.value("channel");
    if(op=="uart_exchange") {
        const auto data=QByteArray::fromBase64(request.value("data").toString().toLatin1());
        if(!data.contains("=?@") && data!="INQUIRE=1@" && data!="DATADEMAND=1@") {
            const int channel=request.value("channel").toInt();
            if(channel>=1 && channel<=2) {
                tec103Identified_[size_t(channel-1)]=false;++tec103Revision_[size_t(channel-1)];
            }
        }
        steps_.push_back({Kind::UartExchange});
    } else if(op=="instrument") {
        const quint32 base=source==0x4a?0x6a00:0x6600;
        if(!(registers_.value(base+4)&1)) {fail(QStringLiteral("参数操作前请先启用对应设备。"));return;}
        const bool rd=source==0x4a;
        const quint32 counter=base+(rd?0x90:0x94),status=base+0x8c;
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
                || (state&0x3ffff)!=0x20000u || code!=0) {
                fail(QStringLiteral("设备参数事务未通过：状态 0x%1；执行结果可能未知，未自动重试。").arg(state,8,16,QLatin1Char('0')));return;
            }
            QByteArray actual;
            if(rd)for(int i=0;i<8;++i) {char word[4];qToLittleEndian(registers_.value(base+0xe0+quint32(i*4)),word);actual.append(word,4);}
            else {const auto raw=registers_.value(base+0x90);actual.append(char(raw>>8));actual.append(char(raw));}
            actual.truncate(count*2);
            extensionResult_["data"]=QString::fromLatin1(actual.toBase64());
            extensionResult_["verified"]=true;
            if(operation==2 && actual!=data) {fail(QStringLiteral("参数实际读回值与写入值不同。"));return;}
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
    } else if(op=="wms_trigger") {
        const quint32 base=0x2000+channel*0x100;
        write(base+4,0);verify(base+4,0);waitFor(base+0x54,2,0);waitFor(base+8,4,0);
        stopAndDrain();
        const quint32 mode=request.value("mode").toInt(),idle=quint32(qint64(request.value("idle").toDouble()));
        extensionResult_["mode"]=double(mode);extensionResult_["idle_q31"]=double(qint32(idle));
        write(base+0x4c,mode);verify(base+0x4c,mode);write(base+0x50,idle);verify(base+0x50,idle);
        read(base+0x54,14);
        callback([this]{extensionResult_["verified"]=true;});
    } else if(op=="pps_snapshot") {
        const quint32 base=0x2000+channel*0x100;
        read(base+0x54,14);read(base+0x30);
        auto before=std::make_shared<QMap<quint32,quint32>>();
        callback([this,before]{*before=registers_;});
        read(base+0x54,14);read(base+0x30);
        callback([this,base,before,channel] {
            bool coherent=!(registers_.value(base+0x54)&2) && !(before->value(base+0x54)&2);
            for(quint32 offset:{0x30u,0x58u,0x60u,0x7cu,0x80u})
                coherent&=before->value(base+offset)==registers_.value(base+offset);
            const auto tick=[this,base](quint32 offset){return (quint64(registers_.value(base+offset+4))<<32)|registers_.value(base+offset);};
            if(!coherent) {fail(QStringLiteral("扫描正在变化，未取得一致快照；请在扫描间隙重新读取。"));return;}
            extensionResult_["channel"]=channel;extensionResult_["pps_sequence"]=double(registers_.value(base+0x60));
            extensionResult_["status"]=double(registers_.value(base+0x54));
            extensionResult_["accepted"]=double(registers_.value(base+0x58));
            extensionResult_["completed"]=double(registers_.value(base+0x7c));extensionResult_["aborted_count"]=double(registers_.value(base+0x80));
            extensionResult_["cycle_id"]=double(registers_.value(base+0x30));
            extensionResult_["pps_tick"]=QString::number(tick(0x64));
            extensionResult_["start_tick"]=QString::number(tick(0x6c));extensionResult_["end_tick"]=QString::number(tick(0x74));
            extensionResult_["window_valid"]=registers_.value(base+0x58)!=0 && tick(0x6c)>=tick(0x64) && tick(0x74)>=tick(0x6c);
            extensionResult_["aborted"]=bool(registers_.value(base+0x54)&128);
            extensionResult_["verified"]=true;
        });
    } else if(op=="dlia_sos") {
        const quint32 base=0x5000+channel*0x100;
        stopAndDrain();read(base+0xfc);read(base+0xf8);read(base+0xc);
        callback([this,base,request] {
            const auto fs=registers_.value(base+0xfc),fc=quint32(request.value("hz").toDouble());
            const int order=request.value("order").toInt();
            const auto words=FpgaDliaSos::design(order,fs,fc);
            if(words.size()!=30) {fail(QStringLiteral("截止频率须小于实际 ADC 采样率的一半，且量化后的 SOS 必须稳定。"));return;}
            if(registers_.value(base+0xc)&0x110) {fail(QStringLiteral("DLIA 有配置或采样率错误，请先检查并清除已记录的错误。"));return;}
            const auto generation=registers_.value(base+0xf8);
            extensionResult_["sample_rate_hz"]=double(fs);extensionResult_["cutoff_hz"]=double(fc);extensionResult_["order"]=order;
            QJsonArray bank;for(auto word:words)bank.append(double(word));extensionResult_["words_q46"]=bank;
            write(base+0x60,order);verify(base+0x60,order);
            write(base+0xe0,fs);verify(base+0xe0,fs);write(base+0xe4,fc);verify(base+0xe4,fc);
            for(int i=0;i<30;++i)write(base+0x68+i*4,words[i]);
            for(int i=0;i<30;++i)verify(base+0x68+i*4,words[i]);
            waitFor(base+0xf4,0x38,0x38);
            commit(quint16(0x30+(base-0x5000)/0x100),base);
            verify(base+0xf8,generation+1u);verify(base+0x64,order);
            verify(base+0xe8,fs);verify(base+0xec,fc);verify(base+0xfc,fs);
            verify(base+0x24,0x80000000u|quint32(order));waitFor(base+0xf4,5,1);read(base+0xc);
            callback([this,base,generation] {
                if(registers_.value(base+0xc)&0x110) {fail(QStringLiteral("SOS 提交后出现配置或采样率错误。"));return;}
                extensionResult_["generation"]=double(generation+1u);extensionResult_["verified"]=true;
            });
        });
    } else if(op=="dlia_cutoff") {
        const quint32 base=0x5000+quint32(request.value("channel").toInt())*0x100;
        const quint32 hz=quint32(request.value("hz").toInt());
        if(hz!=1000 && registers_.value(0x4014)!=1000000) {fail(QStringLiteral("非默认截止频率要求 ADC 实际采样率为固件编译标称值 1 MHz。"));return;}
        stopAndDrain();write(base+0x58,hz);verify(base+0x58,hz);commit(quint16(0x30+(base-0x5000)/0x100),base);verify(base+0x5c,hz);read(base+0x24);
        callback([this,base,hz]{configuration_.dlia[(base-0x5000)/0x100].cutoffHz=hz;extensionResult_["verified"]=true;});
    } else if(op=="device_temperature") {
        const double c=request.value("celsius").toDouble();
        const quint32 base=source==0x4a?0x6a00:0x6600;
        if(!(registers_.value(base+4)&1)) {fail(QStringLiteral("设温前请先启用对应设备。"));return;}
        const quint32 target=quint32(qint32(std::llround(c*1000000)));
        read(base+0x48);callback([this,base]{extensionResult_["baseline"]=double(registers_.value(base+0x48));});
        if(source==0x46 && request.contains("channel")) {write(0x666c,quint32(request.value("channel").toInt()));verify(0x666c,quint32(request.value("channel").toInt()));}
        Step s;s.kind=Kind::Sensor;s.address=17;s.source=source;s.expected=target;steps_.push_back(s);
        callback([this,base,target,source] {
            // Completion is the verified device counter, not the VLP write ACK.
            waitFor(base+0x48,0xffffffff,quint32(extensionResult_.value("baseline").toDouble())+1u);
            if(source==0x4a)verify(0x6a40,target);
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
