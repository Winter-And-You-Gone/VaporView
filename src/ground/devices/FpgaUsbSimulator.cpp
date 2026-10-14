#include "FpgaUsbSimulator.h"
#include "FpgaControlConfig.h"
#include "FpgaVlp1.h"

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
    if (base == 0x5000 || base == 0x5100) return offset <= 0x54;
    if (base == 0x6000) return offset <= 0x38;
    if (base == 0x6200 || base == 0x6400) return offset <= 0x3c;
    if (base == 0x6300) return offset <= 0x40;
    if (base == 0x6500) return offset <= 0x5c;
    if (base == 0x6600) return offset <= 0x64;
    if (base == 0x6100 || base == 0x6700) return offset <= 12;
    if (base == 0x7000) return offset <= 12 || offset == 0x14 ||
        (offset >= 0x1c && offset <= 0x30) || (offset >= 0x38 && offset <= 0x44);
    return base == 0x8000 && offset <= 0x3c;
}

class Simulator final : public FpgaUsbTransport
{
public:
    explicit Simulator(QObject *parent) : FpgaUsbTransport(parent) {}
    bool open(const QString &) override
    {
        close(); initialize(); opened_ = true; clock_.start();
        diagnostic_.state = FpgaUsbTransportState::Open;
        diagnostic_.detail = QStringLiteral("FPGA protocol simulator (no physical USB)");
        diagnostic_.fields = {{"mode", "simulation"}, {"serial", "SIMULATED-GP01"}};
        publishState(diagnostic_.state); return true;
    }
    void close() override
    {
        opened_ = false; incoming_.clear(); parser_ = FpgaVlp1::StreamParser{}; lastCycleMs_ = 0; cycle_ = sequence_ = 0;
        diagnostic_ = {}; publishState(FpgaUsbTransportState::Offline);
    }
    bool isOpen() const override { return opened_; }
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
            {0x6600,0x00460200},{0x0004,1},{0x7004,1},{0x8004,1},{0x002c,0xffffffff},{0x0030,0xffffffff},
            {0x6610,19200},{0x6614,1},{0x661c,1},{0x662c,1000},{0x6660,200},{0x6664,1},{0x6310,0x77}};
        regs_[0x6010] = 9600; regs_[0x6018] = 1000; regs_[0x6514] = 2;
        regs_[0x6210] = 921600; regs_[0x6510] = 500000;
        regs_[0x6314] = regs_[0x6414] = 1000;
        target_ = 250; measured_ = 235;
        const FpgaControlConfig defaults;
        for (quint32 b : {0x6000u,0x6200u,0x6300u,0x6400u,0x6500u,0x6600u}) {
            regs_[b + 4] = 1; regs_[b + 8] = 1;
        }
        for (int ch = 0; ch < 2; ++ch) {
            const quint32 offset = ch * 0x100;
            for (quint32 b : {0x2000u,0x3000u,0x4000u,0x5000u}) setControl(b + offset, 1);
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
        else if (address >= 0x6000 && address <= 0x660c && (address & 255) == 12) regs_[address] &= ~value;
        else regs_[address] = value;
    }
    void commit(quint16 source)
    {
        if (source == 0x20) regs_[0x4014] = regs_[0x4114] = regs_[0x4010];
        if (source >= 0x30 && source <= 0x31) {
            const auto base = 0x5000 + (source - 0x30) * 0x100;
            regs_[base + 0x4c] = regs_[base + 0x1c];
            regs_[base + 0x50] = regs_[0x4014] / qMax(1u, regs_[base + 0x1c]);
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
        else if (message == 1) { for (quint32 v : {0xffffffffu,0x100u,100000000u,8192u,4096u,1u}) put32(result,v); }
        else if (message == 2 || message == 3) {
            if (p.size() < 8) status = 2;
            else {
                const quint32 address = word(p,0); const auto count = qFromLittleEndian<quint16>(p.constData()+4);
                if (!count || count > 1000 || p.size() != (message == 2 ? 8 : 8 + count * 4)) status = 2;
                for (int i = 0; !status && i < count; ++i)
                    if (!readable(address + i * 4) || address + quint64(i) * 4 > 0xffff) status = 5;
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
        } else if (message == 0xb) {
            if (p.size() != 8 || f.header.source < 0x40 || f.header.source > 0x46) status = 2;
            else if (word(p,0) == 1 && word(p,4) <= 1) {
                const auto base = 0x6000 + (f.header.source - 0x40) * 0x100;
                regs_[base + 4] = regs_[base + 8] = word(p,4);
            } else if (word(p,0) == 17 && f.header.source == 0x46 && (regs_[0x6604] & 1)) {
                const qint32 microC = qint32(word(p,4));
                if (microC % 100000 || microC < -999000000 || microC > 2147400000) status = 14;
                else {
                    target_ = qint16(microC / 100000); ++regs_[0x6648]; regs_[0x664c] = 0x10000;
                    const auto raw = quint16(target_); regs_[0x6654] = (quint32(raw) << 16) | raw;
                }
            } else status = 14;
        } else status = 1;
        QByteArray response; put32(response,status); if (!status) response.append(result);
        enqueue(FpgaVlp1::FrameType::Response,f.header.sequence,f.header.source,message,response);
    }
    void data(quint16 source, quint16 message, const QByteArray &payload, quint64 ticks)
    { enqueue(FpgaVlp1::FrameType::Data,++sequence_,source,message,payload,ticks,3,cycle_); }
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
        const quint16 format = raw ? 0 : regs_[0x5020+offset] == 1 ? 1 : regs_[0x5020+offset] == 2 ? 0 : 2;
        const auto width = raw ? 4u : format == 0 ? 8u : format == 1 ? 16u : 24u;
        const auto perFragment = 8160/width;
        const auto fragments = (total+perFragment-1)/perFragment;
        for (quint32 index=0; index<fragments; ++index) {
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
        if (!(regs_[4]&1) || !(regs_[0x7004]&1) || !(regs_[0x8004]&1)) return;
        ++cycle_; const double t=elapsedMs/1000.; const quint64 ticks=quint64(elapsedMs)*100000;
        const auto streamed = [&](quint16 source) { const auto bit=source%64;
            return (regs_[bit<32?0x2c:0x30] & (1u<<(bit%32))) != 0; };
        const auto enabled = [&](quint16 source) {
            return (regs_[0x6004+(source-0x40)*0x100]&1) && streamed(source); };
        if(enabled(0x40)) {QByteArray p;put16(p,1);put16(p,1);tlv(p,0x12,7,scalar(quint32(101325000+std::sin(t)*28000)));data(0x40,0x1100,p,ticks);}
        if(enabled(0x43)) {QByteArray p;put16(p,1);put16(p,3);tlv(p,0x100,11,QByteArray::fromHex("986d4c4bf9fd1c4d1706019349005a03fa800f08f5"));
            tlv(p,0x101,3,scalar(quint32(6461000+std::sin(t)*1000)));tlv(p,0x102,3,scalar(8499712));data(0x43,0x1100,p,ticks);}
        if(enabled(0x42)) navigation(t,ticks);
        if(enabled(0x44)) {QByteArray p;put16(p,1);put16(p,3);tlv(p,0x10,7,scalar(quint32(23500+std::sin(t)*800)));
            tlv(p,0x11,3,scalar(quint32(47000+std::cos(t)*2000)));tlv(p,1,3,scalar(0));data(0x44,0x1100,p,ticks);}
        if(enabled(0x45)) {QByteArray p;put16(p,1);put16(p,1);tlv(p,0x13,3,scalar(quint32(120000+std::sin(t)*2400)));data(0x45,0x1100,p,ticks);}
        if(enabled(0x46)) {
            measured_ += (target_-measured_)*0.02;
            QByteArray p;put16(p,2);put16(p,13);
            const quint16 tags[]={0x460,0x461,0x462,0x463,0x464,0x465,0x466,0x467,1,2,4,0x14,0x15};
            const qint32 values[]={qint32(std::lround(measured_)),target_,target_,500,0,1,0, qint32(regs_[0x664c]),1,0,qint32(cycle_),target_*100000,qint32(measured_*100000)};
            for(int i=0;i<13;++i)tlv(p,tags[i],i<3||i>=11?7:3,scalar(quint32(values[i])));
            data(0x46,0x1100,p,ticks);
        }
        for(int ch=0;ch<2;++ch) {
            const quint32 offset=ch*0x100;
            if((regs_[0x2004+offset]&1)&&(regs_[0x3004+offset]&1))regs_[0x3028+offset]+=regs_[0x202c+offset]/10;
            if(!(regs_[0x4004+offset]&1)||!(regs_[0x2004+offset]&1))continue;
            if((regs_[0x5004+offset]&1)&&regs_[0x5020+offset]&&streamed(quint16(0x30+ch)))waveform(ch,false,t,ticks);
            if((regs_[0x403c+offset]&1)&&(regs_[0x50]&(1u<<ch))&&streamed(quint16(0x20+ch)))waveform(ch,true,t,ticks);
        }
    }
    bool opened_ = false;
    FpgaUsbDiagnostic diagnostic_;
    FpgaVlp1::StreamParser parser_;
    QMap<quint32,quint32> regs_;
    QByteArray incoming_;
    QElapsedTimer clock_;
    qint64 lastCycleMs_ = 0;
    quint32 sequence_ = 0, cycle_ = 0;
    qint16 target_ = 250;
    double measured_ = 235;
};
}
std::unique_ptr<FpgaUsbTransport> makeFpgaUsbSimulator(QObject *parent)
{ return std::make_unique<Simulator>(parent); }
}
