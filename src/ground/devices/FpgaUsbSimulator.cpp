#include "FpgaUsbSimulator.h"
#include "FpgaAdvancedParameters.h"
#include "FpgaControlConfig.h"
#include "FpgaVlp1.h"
#include "FpgaExtensions.h"
#include "EpsilonSettings.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QMap>
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace VaporView::Ground::Devices
{
namespace
{
constexpr double pi = 3.14159265358979323846;
void put16(QByteArray &b, quint16 v) { b.append(char(v)); b.append(char(v >> 8)); }
void put32(QByteArray &b, quint32 v) { for (int i = 0; i < 4; ++i) b.append(char(v >> (i * 8))); }
quint32 word(const QByteArray &b, int offset) { return qFromLittleEndian<quint32>(b.constData() + offset); }
template<class T> void at(QByteArray &b, int offset, T value)
{
    // FDILink uses IEEE754 and little-endian integer/float storage.
    auto bits = QByteArray(reinterpret_cast<const char *>(&value), sizeof(T));
#if Q_BYTE_ORDER == Q_BIG_ENDIAN
    std::reverse(bits.begin(), bits.end());
#endif
    std::memcpy(b.data() + offset, bits.constData(), sizeof(T));
}
QByteArray scalar(quint32 v) { QByteArray b; put32(b, v); return b; }
void tlv(QByteArray &b, quint16 tag, quint8 type, const QByteArray &v)
{
    put16(b, tag); b.append(char(type)); b.append(char(v.size())); b.append(v);
    while (b.size() % 4) b.append(char(0));
}
bool readable(quint32 a)
{
    if (a % 4) return false;
    if (a <= 0x10 || (a >= 0x20 && a <= 0x40) || (a >= 0x48 && a <= 0x50) ||
        (a >= 0x110 && a <= 0x118) || (a >= 0x1000 && a <= 0x1040)) return true;
    const auto base = a & ~255u, offset = a & 255;
    if (base == 0x2000 || base == 0x2100) return offset <= 0x48;
    if (base == 0x3000 || base == 0x3100) return offset <= 0x44;
    if (base == 0x4000 || base == 0x4100) return offset <= 0x3c;
    if (base == 0x5000 || base == 0x5100) return offset <= 0x5c;
    if (base == 0x6000) return offset <= 0x38;
    if (base == 0x6400) return offset <= 0x3c;
    if (base == 0x6200) return offset <= 0x7c;
    if (base == 0x6300) return offset <= 0x40;
    if (base == 0x6500) return offset <= 0x5c;
    if (base == 0x6600) return offset <= 0x6c || (offset>=0x80 && offset<=0xb4) || offset==0xfc;
    if (base == 0x6800) return offset <= 0x48 || (offset>=0x80 && offset<=0x90) || offset>=0xc0;
    if (base == 0x6100) return offset <= 0x48;
    if (base == 0x6700) return offset <= 0x34;
    if (base == 0x7000) return offset <= 12 || offset == 0x10 || offset == 0x14 || offset == 0x18 || offset == 0x34 ||
        (offset >= 0x1c && offset <= 0x30) || (offset >= 0x38 && offset <= 0x44);
    return base == 0x8000 && offset <= 0x3c;
}

class Simulator final : public FpgaUsbSimulator
{
public:
    Simulator(QObject *parent, FpgaSimulatorStartup startup) : FpgaUsbSimulator(parent), startup_(startup)
    { initialize(); clock_.start(); }
    bool open(const QString &) override
    {
        close(); opened_ = true;
        diagnostic_.state = FpgaUsbTransportState::Open;
        diagnostic_.detail = QStringLiteral("FPGA protocol simulator (no physical USB)");
        diagnostic_.fields = {{"mode", "simulation"}, {"serial", "SIMULATED-GP01"}};
        publishState(diagnostic_.state); return true;
    }
    void close() override
    {
        opened_ = false; incoming_.clear(); parser_ = FpgaVlp1::StreamParser{};
        diagnostic_ = {}; publishState(FpgaUsbTransportState::Offline);
    }
    bool isOpen() const override { return opened_; }
    void setScenario(bool stalled, bool partialFailure) override
    { stalled_ = stalled; partialFailure_ = partialFailure; }
    void injectFault(const QString& fault) override
    {
        if(fault=="legacy_extensions")extensions_=1;
        else if(fault=="parameter_timeout" || fault=="parameter_mismatch" || fault=="epsilon_error")fault_=fault;
        else if (fault == "missing_cycle") missingCycle_ = true;
        else if (fault == "latch_dlia") {
            for (quint32 base : {0x5000u,0x5100u}) regs_[base+12] |= 0x84;
        }
    }
    FpgaUsbDiagnostic diagnostic() const override { return diagnostic_; }
    QJsonObject discoverDevices() const override
    {
        return {{"devices", QJsonArray{QJsonObject{{"locator", "simulation://fpga"},
            {"label", "FPGA Protocol Simulator"}, {"backend", "simulator"},
            {"driver", "SIMULATED"}, {"available", true}}}}, {"error", ""}};
    }
    bool readDiagnostic(quint8 request, QByteArray &response) override
    {
        response.clear(); if (!opened_ || (request != 0xb0 && request != 0xb1)) return false;
        response = QByteArray(32, 0); response.replace(0, 4, QByteArrayLiteral("GP01")); return true;
    }
    qint64 read(QByteArray &out, qint64 maximum) override
    {
        out.clear(); if (!opened_) return -1;
        if (maximum <= 0) return 0;
        const auto elapsed = clock_.elapsed();
        completeExtensions(elapsed);
        if (elapsed - lastCycleMs_ >= 100 && incoming_.size() < 1024 * 1024) {
            lastCycleMs_ = elapsed; generate(elapsed);
        }
        // Deliberately split both headers and payloads across read calls.
        const auto count = std::min({maximum, qint64(incoming_.size()), qint64(503)});
        out = incoming_.left(count); incoming_.remove(0, count); diagnostic_.bytesRead += count;
        return count;
    }
    qint64 write(const QByteArray &bytes) override
    {
        if (!opened_) return -1;
        const auto frames = parser_.feed(reinterpret_cast<const std::uint8_t *>(bytes.constData()), bytes.size());
        for (const auto &frame : frames) respond(frame);
        diagnostic_.bytesWritten += bytes.size(); return bytes.size();
    }
private:
    void initialize()
    {
        regs_.clear();
        regs_ = {{0x4000,0x00200101},{0x4100,0x00210101},{0x4018,0x00011800},{0x4118,0x00011800},
            {0x5000,0x00300101},{0x5100,0x00310101},{0x5024,0x00040101},{0x5124,0x00040101},
            {0x6600,0x00460200},{0x0004,0},{0x7004,1},{0x8004,1},{0x002c,0xffffffff},{0x0030,0xffffffff},
            {0x6610,19200},{0x6614,1},{0x661c,1},{0x662c,1000},{0x6660,200},{0x6664,1},{0x6310,0x77}};
        regs_[0x6800]=0x00480103;regs_[0x6810]=19200;regs_[0x6814]=2;regs_[0x681c]=1;regs_[0x682c]=1000;
        regs_[0x66fc]=15;regs_[0x6698]=1;regs_[0x6270]=2000;regs_[0x6278]=115200;
        for(int ch=1;ch<=8;++ch) {ai8Registers_[quint16(ch-1)]=250+ch;ai8Registers_[quint16(0x600+ch-1)]=230+ch;ai8Registers_[quint16(0x480+ch-1)]=250+ch;}
        for(int i=0;i<8;++i)ai8Measured_[i]=230+i;
        ai8Registers_[0x840]=1;ai8Registers_[0x841]=2;
        for(quint16 channel:{1,2}) {const quint16 base=channel*0x1000;rd105Registers_[base]=38;rd105Registers_[base+1]=9632;rd105Registers_[base+2]=35;rd105Registers_[base+3]=60000;}
        rd105Registers_[2]=2;rd105Registers_[9]=1;
        for(const auto& spec:FpgaRemote::sensorParameters())regs_[spec.address]=spec.fallback;
        regs_[0x6714]=1000;regs_[0x6718]=255;regs_[0x671c]=505;regs_[0x6708]=2;
        regs_[0x6100]=0x00410101;regs_[0x6110]=19200;regs_[0x6114]=240;regs_[0x611c]=1000;regs_[0x6120]=3;
        regs_[0x6010] = 9600; regs_[0x6018] = 1000; regs_[0x6514] = 2;
        regs_[0x6210] = 921600; regs_[0x6510] = 500000;
        regs_[0x6314] = regs_[0x6414] = 1000;
        target_ = 250; measured_ = 235;
        const quint32 enabled = startup_ == FpgaSimulatorStartup::RunningDemo ? 1 : 0;
        regs_[4] = enabled;
        const FpgaControlConfig defaults;
        for (quint32 b : {0x6000u,0x6100u,0x6200u,0x6300u,0x6400u,0x6500u,0x6600u,0x6800u}) {
            regs_[b + 4] = enabled; regs_[b + 8] = enabled;
        }
        for (int ch = 0; ch < 2; ++ch) {
            const quint32 offset = ch * 0x100;
            for (quint32 b : {0x2000u,0x3000u,0x4000u,0x5000u}) setControl(b + offset, enabled);
            const auto &w = defaults.wms[ch];
            const quint32 values[] = {w.scanMilliHz,quint32(w.scanAmplitude),quint32(w.bias),w.sineMilliHz,
                quint32(w.sineAmplitude),w.phase,w.updateRateHz};
            for (int i = 0; i < 7; ++i) regs_[0x2010 + offset + i * 4] = values[i];
            regs_[0x2040 + offset] = w.phaseMode; regs_[0x202c + offset] = w.updateRateHz;
            const auto &dac = defaults.dac[ch];
            regs_[0x3010 + offset] = dac.deviceCtrl; regs_[0x3014 + offset] = dac.clearCode;
            regs_[0x301c + offset] = dac.maxCode; regs_[0x3024 + offset] = dac.spiClockHz;
            regs_[0x3034 + offset] = dac.gain; regs_[0x303c + offset] = 16666666;
            regs_[0x4010 + offset] = regs_[0x4014 + offset] = defaults.adcRateHz;
            regs_[0x5010 + offset] = defaults.dlia[ch].referenceMilliHz;
            regs_[0x501c + offset] = regs_[0x504c + offset] = defaults.dlia[ch].outputRateHz;
            regs_[0x5020 + offset] = defaults.dlia[ch].mode; regs_[0x5050 + offset] = 100;
            regs_[0x5058+offset]=regs_[0x505c+offset]=1000;
        }
    }
    void setControl(quint32 base, quint32 value)
    {
        regs_[base + 4] = value & 1;
        regs_[base + 8] = (value & 1) | (base >= 0x4000 && base <= 0x4100 ? 0x12 : 2);
        if (value & 2) regs_[base + 12] = 0;
    }
    void setRegister(quint32 address, quint32 value)
    {
        const auto base = address & ~255u;
        if (address >= 0x2000 && address <= 0x5104 && (address & 255) == 4) setControl(base, value);
        else if (address >= 0x6000 && address <= 0x680c && (address & 255) == 12) regs_[address] &= ~value;
        else regs_[address] = value;
        if(address==0x6680 || address==0x6880) {regs_[address+12]=0x10000;pendingParameters_[base]={value,clock_.elapsed()+30};}
        if(address==0x6244) {mailbox_.clear();regs_[0x626c]=0;}
        if(address==0x6268) {put32(mailbox_,value);++regs_[0x626c];}
        if(address==0x6260) {
            auto part=mailboxResponse_.mid(int(value),4).leftJustified(4,'\0');regs_[0x6264]=qFromLittleEndian<quint32>(part.constData());
        }
        if(address==0x6240) {regs_[0x624c]=0x10000;mailboxDue_=clock_.elapsed()+30;}
        if(address==0x6698)for(int i=0;i<6;++i)regs_[0x66a0+quint32(i*4)]=ai8Cache_[qBound(1,int(value),8)-1][i];
        if(address==0x103c)regs_[0x1040]=1;
    }
    void completeExtensions(qint64 now)
    {
        const auto pending=pendingParameters_;
        for(auto it=pending.begin();it!=pending.end();++it) {
            if(now<it.value().second)continue;
            const quint32 base=it.key(),op=it.value().first,address=regs_[base+0x84];
            const bool rd=base==0x6800;const int count=rd?int(regs_[base+0x88]):1;
            auto& instrument=rd?rd105Registers_:ai8Registers_;
            quint32 result=0;
            if(!(regs_[base+4]&1))result=5;
            else if(fault_=="parameter_timeout" || (rd && (regs_[0x6814]!=rd105Address_ || rd105Baud_!=19200))
                || (!rd && (regs_[0x6614]!=ai8Address_ || ai8Baud_!=19200)))result=op==1?2:1;
            if(result==0 && op!=1) {
                if(rd)for(int i=0;i<count;++i) {
                    const auto word=regs_[base+0xc0+quint32((i/2)*4)];
                    const int shift=(i%2)*16;
                    instrument[quint16(address+i)]=quint16(((word>>shift)&255)*256+((word>>(shift+8))&255));
                }
                else instrument[quint16(address)]=quint16(regs_[base+0x88]);
                if(rd && address==2) {rd105Address_=instrument.value(2);if(rd105Address_!=regs_[0x6814])result=2;}
                if(rd && address==9) {const auto code=instrument.value(9);rd105Baud_=code==1?19200:code==0?9600:38400;if(rd105Baud_!=19200)result=2;}
                if(!rd && address==0x840) {ai8Address_=instrument.value(0x840);if(ai8Address_!=regs_[0x6614])result=2;}
                if(!rd && address==0x841) {const auto code=instrument.value(0x841);ai8Baud_=code==2?19200:code==3?38400:9600;if(ai8Baud_!=19200)result=2;}
                if(!rd && address<8)instrument[quint16(0x480+address)]=instrument.value(quint16(address));
                if(fault_=="parameter_mismatch") {instrument[quint16(address)]^=1;result=3;}
                if(rd && op==3) {result=6;regs_[0x6804]=regs_[0x6808]=0;rd105Address_=1;rd105Baud_=9600;}
            }
            if(rd) {
                QByteArray data;
                for(int i=0;i<count;++i) {const auto value=instrument.value(quint16(address+i));data.append(char(value>>8));data.append(char(value));}
                data=data.leftJustified(32,'\0');
                for(int i=0;i<8;++i)regs_[base+0xe0+quint32(i*4)]=word(data,i*4);
            } else regs_[base+0x90]=instrument.value(quint16(address));
            regs_[base+0x8c]=0x20000|result|((result==1 || result==2)?0x10:0);
            ++regs_[base+(rd?0x90:0x94)];pendingParameters_.remove(base);
            fault_.clear();
        }
        if(mailboxDue_ && now>=mailboxDue_) {
            mailboxDue_=0;quint32 code=0;mailboxResponse_.clear();
            const auto data=mailbox_.left(int(regs_[0x6244]));
            if(!(regs_[0x6204]&1))code=4;
            else if(regs_[0x6248]==1) {
                if(!FpgaExtensions::rtcmFrame(data))code=3;
                else {regs_[0x627c]+=quint32(data.size());regs_[0x6250]+=quint32(data.size())+(regs_[0x6274]?0:8*quint32((data.size()+254)/255));}
            } else {
                const auto command=QString::fromLatin1(data).trimmed();const auto tokens=command.split(' ',Qt::SkipEmptyParts);
                mailboxResponse_="*#ERROR\r\n";
                if(fault_=="epsilon_error")fault_.clear();
                else if(command=="#fconfig") {epsilonConfig_=true;mailboxResponse_="*#OK\r\n";}
                else if(command=="#fdeconfig") {epsilonConfig_=false;mailboxResponse_="*#OK\r\n";}
                else if(epsilonConfig_) {
                    if(command=="#fsave")mailboxResponse_="*#OK\r\n";
                    else if(tokens.size()==3 && (tokens[0]=="#fparam" || tokens[0]=="#fdgnss") && tokens[1]=="get") {
                        const auto key=tokens[2];
                        auto& model=tokens[0]=="#fparam"?epsilonParameters_:epsilonDgnss_;
                        const bool known=tokens[0]=="#fparam"?bool(epsilonParameterDescriptor(key.toStdString())):bool(epsilonDgnssDescriptor(key.toStdString()));
                        const QString fallback=tokens[0]=="#fdgnss"?QStringLiteral("SIM"):key=="COMM_BAUD1"?"8":key=="COMM_STREAM_TYP1"?"1":key=="COMM_STREAM_TYP2"||key=="COMM_STREAM_TYP3"?"9":"0";
                        if(known)mailboxResponse_=(key+(tokens[0]=="#fparam"?" = ":" : ")+model.value(key,fallback)+"\r\n*#OK\r\n").toLatin1();
                    } else if(tokens.size()==4 && (tokens[0]=="#fparam" || tokens[0]=="#fdgnss") && tokens[1]=="set") {
                        const auto key=tokens[2].toStdString();
                        const auto* parameter=epsilonParameterDescriptor(key);const auto* dgnss=epsilonDgnssDescriptor(key);
                        if(tokens[0]=="#fparam"?parameter&&parameter->writable:dgnss&&dgnss->writable) {
                            auto& model=tokens[0]=="#fparam"?epsilonParameters_:epsilonDgnss_;model[tokens[2]]=tokens[3];mailboxResponse_="*#OK\r\n";
                        }
                    } else if(tokens.size()==3 && tokens[0]=="#fmsg") {
                        bool ok=false;const int id=tokens[1].toInt(&ok,16);if(ok){epsilonRates_[id]=tokens[2].toInt();mailboxResponse_="*#OK\r\n";}
                    } else if(command=="#fmsg") {
                        mailboxResponse_.clear();for(auto i=epsilonRates_.begin();i!=epsilonRates_.end();++i)
                            mailboxResponse_+=QStringLiteral("MSG_SIM [%1] %2 Hz\r\n").arg(i.key(),2,16,QLatin1Char('0')).arg(i.value()).toLatin1();
                        mailboxResponse_+="*#OK\r\n";
                    }
                }
                regs_[0x6250]+=quint32(data.size());
            }
            regs_[0x624c]=0x20000|code;regs_[0x625c]=quint32(mailboxResponse_.size());
            if(code)++regs_[0x6258];else ++regs_[0x6254];
        }
    }
    void commit(quint16 source)
    {
        if (source == 0x20) regs_[0x4014] = regs_[0x4114] = regs_[0x4010];
        if (source >= 0x30 && source <= 0x31) {
            const auto base = 0x5000 + (source - 0x30) * 0x100;
            regs_[base + 0x4c] = regs_[base + 0x1c];
            regs_[base + 0x50] = regs_[0x4014] / qMax(1u, regs_[base + 0x1c]);
            regs_[base+0x5c]=regs_[base+0x58];
        }
        if (source >= 0x10 && source <= 0x11) {
            const auto base = 0x2000 + (source - 0x10) * 0x100;
            const auto period = (100000000ull + qMax(1u, regs_[base + 0x28]) - 1) / qMax(1u, regs_[base + 0x28]);
            regs_[base + 0x2c] = quint32(100000000ull / period);
            const auto dac = base + 0x1000;
            const auto half = (100000000ull + 2ull * qMax(1u, regs_[dac + 0x24]) - 1) / (2ull * qMax(1u, regs_[dac + 0x24]));
            regs_[dac + 0x3c] = quint32(100000000ull / (2 * half));
        }
    }
    void enqueue(FpgaVlp1::FrameType type, quint32 sequence, quint16 source, quint16 message,
                 const QByteArray &payload, quint64 ticks = 0, quint32 flags = 0, quint32 cycle = 0)
    {
        const auto first = reinterpret_cast<const std::uint8_t *>(payload.constData());
        const auto wire = FpgaVlp1::buildFrame(type, sequence, source, message,
            std::vector<std::uint8_t>(first, first + payload.size()), true, flags, ticks, cycle);
        incoming_.append(reinterpret_cast<const char *>(wire.data()), qsizetype(wire.size()));
    }
    quint32 sensorWriteStatus(quint32 address, quint32 value) const
    {
        const auto base=address&~255u;
        const bool enabled=regs_.value(base+4)&1;
        if(address==0x6668 && (enabled || value>1))return 7;
        if(address==0x666c && value>8)return 7;
        if(address==0x6810 && (enabled || value!=19200))return 7;
        if((address==0x6814 || address==0x681c) && (enabled || value<1 || value>(address==0x6814?247u:2u)))return 7;
        if(address==0x5058 || address==0x5158) {
            if(!FpgaExtensions::cutoff(value) || (value!=1000 && regs_.value(0x4014)!=1000000))return 7;
        }
        if(address==0x6680 || address==0x6880) {
            if(!enabled || pendingParameters_.contains(base))return 6;
            if(value<1 || value>(base==0x6800?3u:2u))return 13;
            if(base==0x6600) {
                const auto a=regs_.value(0x6684);
                if(!((a<=0x4df) || (a>=0x600&&a<=0x6ef) || (a>=0x800&&a<=0x85f)))return 7;
                if(value==2 && ((a>=0x480&&a<0x800) || (a>=0x842&&a<=0x844) || a==0x851 || (a>=0x853&&a<=0x855)))return 7;
            }
            if(base==0x6800 && (regs_.value(0x6888)<1 || regs_.value(0x6888)>16))return 7;
            if(base==0x6800 && ((value==2 && !regs_.value(0x6884)) || (value==3 && (regs_.value(0x6884)!=0 || regs_.value(0x6888)!=1 || (regs_.value(0x68c0)&0xffff)!=0x0100))))return 7;
        }
        if(pendingParameters_.contains(base) && ((address&255)>=0x80))return 6;
        if(base==0x6200 && (address&255)>=0x40) {
            if(regs_.value(0x624c)&0x10000)return 6;
            if(address==0x6244 && (value<1 || value>1029))return 7;
            if(address==0x6248 && value>1)return 7;
            if(address==0x6274 && value>1)return 7;
            if(address==0x6270 && (value<100 || value>10000))return 7;
            if(address==0x6278 && (value<9600 || value>6250000))return 7;
            if(address==0x6268 && regs_.value(0x626c)>=(regs_.value(0x6244)+3)/4)return 7;
            if(address==0x6240 && (value!=1 || !enabled || regs_.value(0x626c)!=(regs_.value(0x6244)+3)/4 || (!regs_.value(0x6248) && regs_.value(0x6274))))return 7;
        }
        if(address==0x6010 || address==0x6210 || address==0x6510) {
            const quint32 minimum=address==0x6010?1200:address==0x6210?9600:115200;
            const quint32 maximum=address==0x6010?19200:6250000;
            if(value<minimum || value>maximum || (address==0x6510 && regs_.value(0x6514)==2 && value<500000))return 7;
            if(enabled && address!=0x6510)return 7;
        }
        if(address==0x6018 || address==0x6314 || address==0x6414 || address==0x662c) {
            if(value<1 || value>3600000)return 7;
            if(enabled && (address==0x6414 || address==0x662c))return 7;
            const auto heater=regs_.value(0x641c);
            if(address==0x6414 && heater && value<(heater&1?12000u:1300u))return 7;
        }
        return 0;
    }
    void respond(const FpgaVlp1::Frame &f)
    {
        if (f.header.frameType != FpgaVlp1::FrameType::Command) return;
        const QByteArray p(reinterpret_cast<const char *>(f.payload.data()), qsizetype(f.payload.size()));
        quint32 status = 0; QByteArray result;
        const auto message = f.header.message;
        if (message == 0xfe) result = p;
        else if (message == 1) { for (quint32 v : {0xffffffffu,0x100u,100000000u,8192u,4096u,extensions_}) put32(result,v); }
        else if (message == 2 || message == 3) {
            if (p.size() < 8) status = 2;
            else {
                const quint32 address = word(p,0); const auto count = qFromLittleEndian<quint16>(p.constData()+4);
                if (!count || count > 1000 || p.size() != (message == 2 ? 8 : 8 + count * 4)) status = 2;
                for (int i = 0; !status && i < count; ++i)
                    if ((!readable(address + i * 4) && !(message==3 && address+i*4==0x44)) || address + quint64(i) * 4 > 0xffff) status = 5;
                if (!status && message == 2) {
                    put32(result,address); put16(result,count); put16(result,0);
                    for (int i = 0; i < count; ++i) put32(result,regs_.value(address + i * 4));
                } else if (!status) {
                    for (int i = 0; i < count; ++i) {
                        const auto a = address + i * 4;
                        if (a >= 0x2000 && ((a & 255) == 0 || (a & 255) == 8)) { status = 5; break; }
                        status=sensorWriteStatus(a,word(p,8+i*4));if(status)break;
                    }
                    if (!status) for (int i = 0; i < count; ++i) setRegister(address + i * 4,word(p,8 + i * 4));
                }
            }
        } else if (message == 5) {
            if (p.size() != 8) status = 2;
            else {
                const quint64 mask = qFromLittleEndian<quint64>(p.constData());
                for (quint16 source : {0x10,0x11,0x12,0x13,0x20,0x21,0x30,0x31})
                    if (mask & (quint64(1) << source)) commit(source);
            }
        } else if (message == 4) {
            if (p.size() != 12 || !readable(word(p,0))) status = 5;
            else setRegister(word(p,0),(regs_.value(word(p,0)) & ~word(p,4)) | (word(p,8) & word(p,4)));
        } else if (message == 0xa) {
            if (p.size() != 16) status = 2;
            else { regs_[0x2c]=word(p,0);regs_[0x30]=word(p,4);regs_[0x50]=word(p,12)&3;
                regs_[0x403c]=regs_[0x50]&1;regs_[0x413c]=(regs_[0x50]>>1)&1; }
        } else if (message == 6 || message == 7) {
            if (p.size() != 16) status = 2;
            else {
                const quint64 mask=qFromLittleEndian<quint64>(p.constData());
                for(int ch=0;ch<2;++ch) if(mask & ((quint64(1)<<(0x10+ch)) |
                    (quint64(1)<<(0x20+ch)) | (quint64(1)<<(0x30+ch))))
                    for(quint32 base:{0x2000u,0x4000u,0x5000u})setControl(base+ch*0x100,message==6);
            }
        } else if(message==8 || message==9) {
            if(p.size()!=8)status=2;
            else {const auto source=f.header.source;const quint32 base=source==2?0x1000:source<0x20?0x2000+(source-0x10)*0x100:source<0x30?0x4000+(source-0x20)*0x100:source<0x40?0x5000+(source-0x30)*0x100:0x6000+(source-0x40)*0x100;regs_[base+12]=0;if(message==8)regs_[base+4]=0;}
        } else if(message==12 || message==13) {
            if(p.size()!=8)status=2;
            else {const auto action=word(p,0),value=word(p,4);
                if(message==13) {if(action==1)regs_[0x1004]=value;else if(action==2){regs_[0x1004]=1;regs_[0x100c]=0;}else if(action==3)regs_[0x1024]=value;else if(action==4)regs_[0x1028]=value;else if(action==5)regs_[0x100c]&=~value;else status=7;}
                else {if(action==4)regs_[0x6704]=value;else if(action==3)regs_[0x6724]=0;else if(action==2)regs_[0x6728]=0;else if(action==1){regs_[0x6724]+=value;++regs_[0x6734];QByteArray event;for(quint32 w:{1u,regs_[0x6734],regs_[0x6724],0u,0u,0u,0u,0u})put32(event,w);enqueue(FpgaVlp1::FrameType::Event,++sequence_,0x47,0x1300,event,quint64(clock_.elapsed())*100000,3);}else status=7;}
            }
        } else if (message == 0xb) {
            if (p.size() != 8 || !FpgaExtensions::sensor(f.header.source)) status = 2;
            else if (word(p,0) == 1 && word(p,4) <= 1) {
                const auto base = 0x6000 + (f.header.source - 0x40) * 0x100;
                regs_[base + 4] = regs_[base + 8] = word(p,4);
                if(word(p,4))lastSensorMs_.remove(f.header.source);
            } else if(word(p,0)==16 && f.header.source==0x41) {regs_[0x6134]=regs_[0x6140]=word(p,4);++regs_[0x6148];}
            else if(word(p,0)==3 || word(p,0)==4 || word(p,0)==5) {const auto base=0x6000+(f.header.source-0x40)*0x100;if(word(p,0)==5)regs_[base+12]&=~word(p,4);else if(word(p,0)==4){regs_[base+4]=0;regs_[base+12]=0;}}
            else if(word(p,0)==32 && f.header.source==0x45)regs_[0x6514]=word(p,4);
            else if(word(p,0)==33 && f.header.source==0x45)regs_[0x6518]=word(p,4);
            else if (word(p,0) == 17 && f.header.source == 0x46 && (regs_[0x6604] & 1)) {
                const qint32 microC = qint32(word(p,4));
                if (microC % 100000 || microC < -999000000 || microC > 2147400000) status = 14;
                else if(ai8Address_!=regs_[0x6614] || ai8Baud_!=19200) {regs_[0x664c]=0x10001;regs_[0x660c]|=0x20;}
                else {
                    const int channel=int(regs_.value(0x666c)?regs_.value(0x666c):regs_.value(0x661c));
                    target_ = qint16(microC / 100000);ai8Registers_[quint16(channel-1)]=ai8Registers_[quint16(0x480+channel-1)]=quint16(target_); ++regs_[0x6648]; regs_[0x664c] = 0x10000;
                    const auto raw = quint16(target_); regs_[0x6654] = (quint32(raw) << 16) | raw;
                }
            } else if(word(p,0)==18 && f.header.source==0x48 && (regs_[0x6804]&1)) {
                const qint32 target=qint32(word(p,4));
                if(target<-400000000 || target>1000000000 || target%10)status=7;
                else if(rd105Address_!=regs_[0x6814] || rd105Baud_!=19200)regs_[0x680c]|=0x20;
                else {const quint16 addr=quint16(regs_[0x681c]*0x1000);const quint32 raw=quint32(target/10);rd105Registers_[addr]=quint16(raw>>16);rd105Registers_[addr+1]=quint16(raw);regs_[0x6840]=quint32(target);++regs_[0x6848];}
            } else status = 14;
        } else status = 1;
        QByteArray response; put32(response,status); if (!status) response.append(result);
        enqueue(FpgaVlp1::FrameType::Response,f.header.sequence,f.header.source,message,response,quint64(clock_.elapsed())*100000);
    }
    void data(quint16 source, quint16 message, const QByteArray &payload, quint64 ticks)
    {
        quint32 flags = 3;
        if (partialFailure_ && (source == 0x40 || source == 0x44 || source == 0x45)) flags |= 1u<<4;
        if((source==0x48 && (rd105Address_!=regs_[0x6814] || rd105Baud_!=19200)) ||
            (source==0x46 && (ai8Address_!=regs_[0x6614] || ai8Baud_!=19200)))flags|=1u<<4;
        if (source >= 0x30 && source <= 0x31 && regs_.value(0x500c+(source-0x30)*0x100)) flags |= 1u<<5;
        enqueue(FpgaVlp1::FrameType::Data,++sequence_,source,message,payload,ticks,flags,cycle_);
    }
    void epsilon(quint8 id, const QByteArray &payload, quint64 ticks)
    {
        QByteArray f; f.append('\xfc'); f.append(char(id)); f.append(char(payload.size())); f.append(char(sequence_));
        quint8 crc8 = 0; for (char v : f) { crc8 ^= quint8(v); for (int i=0;i<8;++i) crc8 = crc8&1 ? quint8((crc8>>1)^0x8c) : quint8(crc8>>1); }
        f.append(char(crc8)); quint16 crc16 = 0;
        for (char v : payload) { crc16 ^= quint16(quint8(v))<<8; for (int i=0;i<8;++i) crc16 = crc16&0x8000 ? quint16((crc16<<1)^0x1021) : quint16(crc16<<1); }
        f.append(char(crc16>>8)); f.append(char(crc16)); f.append(payload); f.append('\xfd'); data(0x42,0x1200,f,ticks);
    }
    void navigation(double seconds, quint64 ticks)
    {
        const auto now = QDateTime::currentMSecsSinceEpoch();
        const double latitude = (22.3 + 0.0001*std::sin(seconds*0.05))*pi/180;
        const double longitude = (114.17 + 0.0001*std::cos(seconds*0.05))*pi/180;
        const float yaw = float(std::fmod(seconds*0.02,2*pi));
        QByteArray identity(40,0); at<quint32>(identity,0,0x53494d31); at<quint32>(identity,16,1);
        identity.replace(20,11,QByteArrayLiteral("EPSILON_SIM")); at<quint32>(identity,28,1);
        identity.replace(32,3,QByteArrayLiteral("SIM")); epsilon(0x39,identity,ticks);
        QByteArray state(102,0); at<quint16>(state,2,0x6f); at<quint32>(state,6,quint32(now/1000));
        at<quint32>(state,10,quint32(now%1000)*1000); at<double>(state,14,latitude); at<double>(state,22,longitude);
        at<double>(state,30,35); at<float>(state,38,0.5f); at<float>(state,58,-9.81f); at<float>(state,74,yaw);
        at<float>(state,90,0.03f); at<float>(state,94,0.03f); at<float>(state,98,0.05f); epsilon(0x50,state,ticks);
        QByteArray imu(56,0); at<float>(imu,8,0.02f); at<float>(imu,20,-9.81f); at<float>(imu,36,24);
        at<float>(imu,40,101325); at<float>(imu,44,24); at<quint64>(imu,48,ticks/100); epsilon(0x40,imu,ticks);
        QByteArray ahrs(48,0); at<float>(ahrs,8,0.02f); at<float>(ahrs,20,yaw);
        at<float>(ahrs,24,float(std::cos(yaw/2))); at<float>(ahrs,36,float(std::sin(yaw/2)));
        at<quint64>(ahrs,40,ticks/100); epsilon(0x41,ahrs,ticks);
        QByteArray ins(72,0); at<float>(ins,0,0.5f); at<float>(ins,24,float(seconds*0.5));
        at<float>(ins,36,0.5f); at<quint64>(ins,64,ticks/100); epsilon(0x42,ins,ticks);
        QByteArray filter(4,0); at<quint16>(filter,2,0x6f); epsilon(0x53,filter,ticks);
        QByteArray gnss(74,0); at<quint32>(gnss,0,quint32(now/1000)); at<quint32>(gnss,4,quint32(now%1000)*1000);
        at<double>(gnss,8,latitude); at<double>(gnss,16,longitude); at<double>(gnss,24,35);
        at<float>(gnss,32,0.5f); at<float>(gnss,44,0.03f); at<float>(gnss,48,0.03f); at<float>(gnss,52,0.05f);
        at<quint16>(gnss,72,0x136); epsilon(0x59,gnss,ticks);
        QByteArray sats(9,0); at<float>(sats,0,0.7f); at<float>(sats,4,1.1f); sats[8]=char(24); epsilon(0x5a,sats,ticks);
        QByteArray llh(32,0); at<double>(llh,0,latitude); at<double>(llh,8,longitude); at<double>(llh,16,35);
        at<float>(llh,24,0.03f); at<float>(llh,28,0.05f); epsilon(0x5c,llh,ticks);
        QByteArray euler(12,0); at<float>(euler,8,yaw); epsilon(0x63,euler,ticks);
        epsilon(0x64,ahrs.mid(24,16),ticks); epsilon(0x51,gnss.left(8),ticks);
        // WGS84 ECEF, kept on the same simulated trajectory as LLH.
        const double n = 6378137.0/std::sqrt(1-0.00669437999014*std::pow(std::sin(latitude),2));
        QByteArray ecef(24,0); at<double>(ecef,0,(n+35)*std::cos(latitude)*std::cos(longitude));
        at<double>(ecef,8,(n+35)*std::cos(latitude)*std::sin(longitude));
        at<double>(ecef,16,(n*(1-0.00669437999014)+35)*std::sin(latitude)); epsilon(0x5d,ecef,ticks);
    }
    void waveform(int ch, bool raw, double seconds, quint64 ticks)
    {
        const quint32 offset = ch*0x100;
        const auto rate = regs_.value((raw ? 0x4014 : 0x504c)+offset);
        const auto total = quint32(qBound<quint64>(32,quint64(rate)*1000/qMax(1u,regs_[0x2010+offset]),16384));
        const quint16 format = raw ? 0 : 0;
        const auto width = raw ? 4u : format == 0 ? 8u : format == 1 ? 16u : 24u;
        const auto perFragment = 8160/width;
        const auto fragments = (total+perFragment-1)/perFragment;
        for (quint32 index=0; index<fragments; ++index) {
            if (missingCycle_ && index == 0) continue;
            const quint32 first=index*perFragment, count=qMin(perFragment,total-first);
            QByteArray p; put16(p,2); put16(p,raw?24:format); put32(p,rate); put32(p,total);
            if (raw) put32(p,0);
            put16(p,quint16(index)); put16(p,quint16(fragments)); put32(p,first); put32(p,count);
            if (!raw) put32(p,width); put32(p,0);
            for (quint32 i=0;i<count;++i) {
                const double x=2*pi*(first+i)/total;
                const qint32 h1=qint32(200000+40000*std::sin(x+seconds)+ch*10000);
                const qint32 h2=qint32(30000+8000*std::cos(x*2+seconds));
                if (raw) {
                    const double phase=regs_[0x2024+offset]*(2*pi/4294967296.0);
                    const double ratio=double(regs_[0x201c+offset])/qMax(1u,regs_[0x2010+offset]);
                    const double code=(qint32(regs_[0x2018+offset])+
                        qint32(regs_[0x2014+offset])*std::sin(x+phase)+
                        qint32(regs_[0x2020+offset])*std::sin(ratio*x+seconds+phase))/256.0;
                    put32(p,quint32(qint32(std::llround(qBound(-8388608.0,code,8388607.0)))));
                }
                else { if(format!=0) {put32(p,quint32(h1));put32(p,0);put32(p,quint32(h2));put32(p,0);}
                    if(format!=1) {put32(p,quint32(h1));put32(p,quint32(h2));} }
            }
            data(quint16((raw?0x20:0x30)+ch),raw?0x1000:0x1001,p,ticks);
        }
    }
    void generate(qint64 elapsedMs)
    {
        if (stalled_ || !(regs_[0x7004]&1) || !(regs_[0x8004]&1)) return;
        ++cycle_; const double t=elapsedMs/1000.; const quint64 ticks=quint64(elapsedMs)*100000;
        const auto streamed = [&](quint16 source) { const auto bit=source%64;
            return (regs_[bit<32?0x2c:0x30] & (1u<<(bit%32))) != 0; };
        const auto enabled = [&](quint16 source) {
            if(!(regs_[0x6004+(source-0x40)*0x100]&1)||!streamed(source))return false;
            const quint32 poll=source==0x40?0x6018:source==0x41?0x611c:source==0x43?0x6314:source==0x44?0x6414:source==0x46?0x662c:source==0x48?0x682c:0;
            const qint64 interval=poll?qMax(1u,regs_.value(poll,1000)):100;
            if(lastSensorMs_.contains(source)&&elapsedMs-lastSensorMs_.value(source)<interval)return false;
            lastSensorMs_[source]=elapsedMs;return true; };
        if(enabled(0x40)) {QByteArray p;put16(p,1);put16(p,1);tlv(p,0x12,7,scalar(quint32(101325000+std::sin(t)*28000)));data(0x40,0x1100,p,ticks);}
        if(enabled(0x43)) {QByteArray p;put16(p,1);put16(p,3);tlv(p,0x100,11,QByteArray::fromHex("986d4c4bf9fd1c4d1706019349005a03fa800f08f5"));
            tlv(p,0x101,3,scalar(quint32(6461000+std::sin(t)*1000)));tlv(p,0x102,3,scalar(8499712));data(0x43,0x1100,p,ticks);}
        if(enabled(0x42)) navigation(t,ticks);
        if(enabled(0x44)) {QByteArray p;put16(p,1);put16(p,3);tlv(p,0x10,7,scalar(quint32(23500+std::sin(t)*800)));
            tlv(p,0x11,3,scalar(quint32(47000+std::cos(t)*2000)));tlv(p,1,3,scalar(0));data(0x44,0x1100,p,ticks);}
        if(enabled(0x45)) {QByteArray p;put16(p,1);put16(p,1);tlv(p,0x13,3,scalar(quint32(120000+std::sin(t)*2400)));data(0x45,0x1100,p,ticks);}
        if(enabled(0x41)) {
            QByteArray p;put16(p,1);put16(p,3);
            QByteArray value(4,0);at<float>(value,0,float(45+std::sin(t)));tlv(p,0x100,9,value);
            at<float>(value,0,float(23+std::cos(t)));tlv(p,0x101,9,value);tlv(p,2,3,scalar(0));
            data(0x41,0x1100,p,ticks);
        }
        if(enabled(0x46)) {
          for(int ch=regs_[0x6668]?1:int(regs_[0x661c]);ch<=(regs_[0x6668]?8:int(regs_[0x661c]));++ch) {
            target_=qint16(ai8Registers_.value(quint16(ch-1),250));
            measured_=ai8Measured_[ch-1]+(target_-ai8Measured_[ch-1])*0.02;ai8Measured_[ch-1]=measured_;
            ai8Registers_[quint16(0x600+ch-1)]=quint16(std::lround(measured_));
            QByteArray p;put16(p,regs_[0x6668]?3:2);put16(p,regs_[0x6668]?14:13);
            const quint16 tags[]={0x460,0x461,0x462,0x463,0x464,0x465,0x466,0x467,1,2,4,0x14,0x15};
            const qint32 values[]={qint32(std::lround(measured_)),target_,target_,500,0,1,0, qint32(regs_[0x664c]),1,0,qint32(cycle_),target_*100000,qint32(measured_*100000)};
            for(int i=0;i<13;++i)tlv(p,tags[i],i<3||i>=11?7:3,scalar(quint32(values[i])));
            ai8Cache_[ch-1]={{3u,quint32(quint16(values[0]))|(quint32(quint16(target_))<<16),500u|(quint32(quint16(target_))<<16),0x100u,quint32(ticks),quint32(ticks>>32)}};
            if(regs_.value(0x6698,1)==quint32(ch))for(int i=0;i<6;++i)regs_[0x66a0+quint32(i*4)]=ai8Cache_[ch-1][i];
            if(regs_[0x6668])tlv(p,0x468,3,scalar(quint32(ch)));
            data(0x46,0x1100,p,ticks);
          }
        }
        if(enabled(0x48)) {
            const auto address=quint16(regs_[0x681c]*0x1000);
            const qint32 target=qint32((quint32(rd105Registers_.value(address))<<16)|rd105Registers_.value(address+1))*10;
            const qint32 actual=23500000+qint32(std::sin(t)*300000);
            regs_[0x6824]=quint32(actual);regs_[0x6840]=quint32(target);++regs_[0x6844];
            QByteArray p;put16(p,1);put16(p,3);tlv(p,0x14,7,scalar(quint32(target)));tlv(p,0x15,7,scalar(quint32(actual)));tlv(p,2,3,scalar(0));data(0x48,0x1100,p,ticks);
        }
        for(int ch=0;ch<2;++ch) {
            const quint32 offset=ch*0x100;
            if (!(regs_[4]&1)) continue;
            if((regs_[0x2004+offset]&1)&&(regs_[0x3004+offset]&1))regs_[0x3028+offset]+=regs_[0x202c+offset]/10;
            if(!(regs_[0x4004+offset]&1)||!(regs_[0x2004+offset]&1))continue;
            if((regs_[0x5004+offset]&1)&&regs_[0x5020+offset]&&streamed(quint16(0x30+ch)))waveform(ch,false,t,ticks);
            if((regs_[0x403c+offset]&1)&&(regs_[0x50]&(1u<<ch))&&streamed(quint16(0x20+ch)))waveform(ch,true,t,ticks);
        }
        missingCycle_ = false;
    }
    const FpgaSimulatorStartup startup_;
    quint32 extensions_=0x3f;
    QString fault_;
    QMap<quint16,quint16> ai8Registers_,rd105Registers_;
    std::array<double,8> ai8Measured_{};
    std::array<std::array<quint32,6>,8> ai8Cache_{};
    quint32 rd105Address_=2,rd105Baud_=19200,ai8Address_=1,ai8Baud_=19200;
    QMap<quint32,QPair<quint32,qint64>> pendingParameters_;
    QByteArray mailbox_,mailboxResponse_;
    qint64 mailboxDue_=0;
    QMap<QString,QString> epsilonParameters_,epsilonDgnss_;
    QMap<int,int> epsilonRates_;
    bool epsilonConfig_=false;
    bool opened_ = false;
    bool stalled_ = false, partialFailure_ = false, missingCycle_ = false;
    FpgaUsbDiagnostic diagnostic_;
    FpgaVlp1::StreamParser parser_;
    QMap<quint32,quint32> regs_;
    QMap<quint16,qint64> lastSensorMs_;
    QByteArray incoming_;
    QElapsedTimer clock_;
    qint64 lastCycleMs_ = 0;
    quint32 sequence_ = 0, cycle_ = 0;
    qint16 target_ = 250;
    double measured_ = 235;
};
}
std::unique_ptr<FpgaUsbSimulator> makeFpgaUsbSimulator(QObject *parent, FpgaSimulatorStartup startup)
{ return std::make_unique<Simulator>(parent, startup); }
}
