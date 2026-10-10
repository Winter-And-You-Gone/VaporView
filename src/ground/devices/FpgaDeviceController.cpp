#include "FpgaDeviceController.h"
#include "shared/session/FpgaSessionArchive.h"

#include <QDateTime>
#include <QFileInfo>
#include <QThread>
#include <cmath>

namespace VaporView::Ground::Devices
{
namespace {
constexpr quint32 kBases[] = {0x2000,0x2100,0x3000,0x3100,0x4000,0x4100,0x5000,0x5100,
                              0x6000,0x6200,0x6300,0x6400,0x6500,0x6600};
QString hex(quint32 value) { return QStringLiteral("0x%1").arg(value, 8, 16, QLatin1Char('0')); }
}

quint64 FpgaDeviceController::nowUs() { return quint64(QDateTime::currentMSecsSinceEpoch()) * 1000; }

FpgaDeviceController::FpgaDeviceController(std::unique_ptr<FpgaUsbTransport> transport, QObject *parent)
    : QObject(parent), injected_(bool(transport))
{
    qRegisterMetaType<FpgaControlConfig>();
    qRegisterMetaType<FpgaSensor::AdaptedMeasurements>();
    qRegisterMetaType<LogRecord>();
    presentationTimer_.setParent(this); statusTimer_.setParent(this); rawTimer_.setParent(this);
    presentationTimer_.setInterval(50);
    statusTimer_.setInterval(2000);
    rawTimer_.setSingleShot(true);
    connect(&presentationTimer_, &QTimer::timeout, this, &FpgaDeviceController::flushPresentation);
    connect(&statusTimer_, &QTimer::timeout, this, [this] { if (ready_ && !busy_) refresh(); });
    connect(&rawTimer_, &QTimer::timeout, this, [this] { setRawEnabled(false); });
    if (transport) installSession(std::move(transport));
}

FpgaDeviceController::~FpgaDeviceController() { disconnectDevice(); }

void FpgaDeviceController::installSession(std::unique_ptr<FpgaUsbTransport> transport)
{
    delete session_;
    session_ = new FpgaDeviceSession(std::move(transport), this);
    connect(session_, &FpgaDeviceSession::registerReadCompleted, this,
        [this](quint32, quint32 address, const QVector<quint32>& values) {
            for (int i=0; i<values.size(); ++i) registers_[address + quint32(4*i)] = values[i];
            if (registers_.contains(0x661c)) adapter_.setAi8Channel(int(registers_.value(0x661c)));
        });
    connect(session_, &FpgaDeviceSession::commandCompleted, this,
        [this](quint32 seq, quint16, bool success, quint32 status, const QByteArray&) { completed(seq, success, status); });
    connect(session_, &FpgaDeviceSession::commandTimedOut, this,
        [this](quint32, quint16) { fail(QStringLiteral("命令超时，执行结果未知；请刷新读回状态，写操作未自动重发。")); });
    connect(session_, &FpgaDeviceSession::errorOccurred, this, [this](const QString& message) {
        if (!session_->isOpen()) { ready_ = false; emit transportConnectionChanged(false); }
        fail(message);
    });
    connect(session_, &FpgaDeviceSession::sensorReadingReceived, this, &FpgaDeviceController::acceptReading);
    connect(session_, &FpgaDeviceSession::waveformCompleted, this,
        [this](const FpgaWave::CompletedStream& stream) {
            latestWaves_[stream.source] = stream;
            if (!stream.complete) report(QStringLiteral("source %1 cycle %2：周期不完整，保留原始分片证据。")
                .arg(hex(stream.source)).arg(stream.cycleId));
        });
    connect(session_, &FpgaDeviceSession::rawFrameReceived, this,
        [this](quint32, quint16 source, quint16 msg, const QByteArray& bytes) {
            if (replaying_) return;
            const bool wave = msg==0x1000 || msg==0x1001;
            if (!wave || (msg==0x1000 && configuration_.recordRaw) ||
                (msg==0x1001 && configuration_.recordDlia))
                if (wave || source < 0x40 || source > 0x46 || configuration_.recordSensors)
                    emit rawFrame(nowUs(), bytes);
        });
    connect(session_, &FpgaDeviceSession::usbBytesReceived, this,
        [this](const QByteArray& bytes) { if (!replaying_) emit rawUsbBytes(nowUs(), bytes); });
    connect(session_, &FpgaDeviceSession::commandSent, this,
        [this](const QByteArray& bytes) { if (!replaying_) emit rawCommand(nowUs(), bytes); });
}

void FpgaDeviceController::connectDevice(const QString& locator, const QString& backend)
{
    disconnectDevice();
    if (!injected_) installSession(makeFpgaUsbHardwareTransport(backend));
    QString error;
    if (!session_->open(locator, &error)) { fail(error); return; }
    emit transportConnectionChanged(true);
    adapter_.reset();
    presentationTimer_.start(); statusTimer_.start();
    emit snapshot(nowUs(), {{"event","connected"},{"backend",backend},{"locator",locator},
        {"usb", QJsonObject::fromVariantMap(session_->transport()->diagnostic().fields)}});
    begin(QStringLiteral("核验协议和模块版本"));
    steps_.push_back({Kind::Ping}); steps_.push_back({Kind::Capabilities});
    readAll(); steps_.push_back({Kind::CheckVersions}); advance();
}

void FpgaDeviceController::disconnectDevice()
{
    presentationTimer_.stop(); statusTimer_.stop(); rawTimer_.stop();
    if (session_) session_->close();
    flushPresentation();
    emit transportConnectionChanged(false);
    steps_.clear(); current_.reset(); pendingSequence_ = 0;
    ready_ = busy_ = replaying_ = drainWaveforms_ = false;
    registers_.clear(); adapter_.reset(); latestReadings_.clear(); latestWaves_.clear(); lastReadingMs_.clear();
    dirtyReadings_.clear(); expiredReadings_.clear();
    emit snapshot(nowUs(), {{"event","disconnected"}});
    emit hardwareValuesChanged(registers_);
    emit connectionChanged(false,false,QStringLiteral("USB 未连接"));
}

bool FpgaDeviceController::begin(const QString& label)
{
    if (busy_ || !session_ || !session_->isOpen()) { report(QStringLiteral("当前无法执行：") + label); return false; }
    busy_=true; operation_=label; detail_=label; steps_.clear(); current_.reset();
    emit connectionChanged(ready_,true,label); return true;
}

void FpgaDeviceController::read(quint32 address, quint16 count)
{ Step s; s.kind=Kind::Read; s.address=address; s.count=count; steps_.push_back(s); }
void FpgaDeviceController::write(quint32 address, quint32 value)
{ Step s; s.kind=Kind::Write; s.address=address; s.values={value}; steps_.push_back(s); }
void FpgaDeviceController::verify(quint32 address, quint32 value)
{ Step s; s.kind=Kind::Wait; s.address=address; s.mask=0xffffffff; s.expected=value; steps_.push_back(s); }
void FpgaDeviceController::waitFor(quint32 address, quint32 mask, quint32 expected)
{ Step s; s.kind=Kind::Wait; s.address=address; s.mask=mask; s.expected=expected; steps_.push_back(s); }
void FpgaDeviceController::commit(quint16 source, quint32 base)
{ Step s; s.kind=Kind::Commit; s.source=source; steps_.push_back(s); waitFor(base+8, 4, 0); read(base+8,2); }

void FpgaDeviceController::readAll()
{
    read(0,5);
    read(0x0020,9); // Ends at 0040; 0044 is write-only watchdog feeding.
    read(0x0048,3);
    read(0x0110,3);
    read(0x1000,17);
    read(0x6100,4); read(0x6700,4); // Diagnostic only; never start HMP or motor.
    read(0x7000,4); read(0x7014); read(0x701c,6); read(0x7038,4);
    read(0x8000,4); read(0x8010,12);
    for (quint32 base : kBases) read(base,4);
    for (quint32 base : {0x2000u,0x2100u}) { read(base+0x10,15); }
    for (quint32 base : {0x3000u,0x3100u}) { read(base+0x10,14); }
    for (quint32 base : {0x4000u,0x4100u}) { read(base+0x10,12); }
    for (quint32 base : {0x5000u,0x5100u}) { read(base+0x10,18); }
    read(0x6610,22);
}

bool FpgaDeviceController::enableUpload()
{
    // A disabled USB transmitter cannot reliably acknowledge its own recovery.
    // Preserve the evidence and require the board's USB path to be restored.
    if (!registers_.contains(0x8004) || !(registers_.value(0x8004)&1)) {
        ready_=false;
        fail(QStringLiteral("USB 上传通路未启用；无法安全确认恢复，未自动写入或重试。请恢复下位机 USB 通路后重新连接。"));
        return false;
    }
    for (quint32 address : {0x002cu,0x0030u}) { write(address,0xffffffffu); verify(address,0xffffffffu); }
    for (quint32 address : {0x7004u,0x8004u}) { write(address,1); verify(address,1); }
    return true;
}

bool FpgaDeviceController::versionsMatch(QString *detail) const
{
    const QMap<quint32,quint32> expected{{0x4000,0x00200101},{0x4100,0x00210101},
        {0x4018,0x00011800},{0x4118,0x00011800},{0x5000,0x00300101},{0x5100,0x00310101},
        {0x5024,0x00040101},{0x5124,0x00040101},{0x6600,0x00460200}};
    const auto cap = session_->capabilities();
    if (!registers_.contains(0x8004) || !(registers_.value(0x8004)&1)) {
        if(detail)*detail=QStringLiteral("USB 上传通路未启用，保留只读诊断并禁止写操作；不能盲目写入恢复。请恢复下位机通路后重新连接。");
        return false;
    }
    if (!cap.valid || cap.protocolVersion!=0x100 || cap.timeClockHz!=100000000 || cap.maxBulkPayload!=8192) {
        if (detail) *detail=QStringLiteral("VLP1 能力或时钟不匹配，保留读取并禁用写操作。"); return false;
    }
    for (auto i=expected.cbegin(); i!=expected.cend(); ++i) if (!registers_.contains(i.key()) || registers_.value(i.key())!=i.value()) {
        if (detail) *detail=QStringLiteral("模块版本不匹配：%1 实际 %2，期望 %3；已禁用写操作。")
            .arg(hex(i.key()),hex(registers_.value(i.key())),hex(i.value())); return false;
    }
    return true;
}

void FpgaDeviceController::advance()
{
    if (!busy_ || current_) return;
    if (steps_.empty()) {
        if (drainWaveforms_) { session_->flushWaveforms(); flushPresentation(); drainWaveforms_ = false; }
        busy_=false; emit hardwareValuesChanged(registers_);
        emit connectionChanged(ready_,false,operation_+QStringLiteral("完成"));
        emit snapshot(nowUs(),{{"event","operation_completed"},{"operation",operation_},{"configuration",configuration_.toJson()}});
        return;
    }
    current_=steps_.front(); steps_.pop_front();
    const auto& s=*current_;
    if (s.kind==Kind::CheckVersions) {
        QString reason; ready_=versionsMatch(&reason);
        if (!ready_) { fail(reason); return; }
        current_.reset(); advance(); return;
    }
    if (s.kind==Kind::CheckTemperature) {
        const quint32 commandStatus=registers_.value(0x664c);
        const quint32 requestedReadback=registers_.value(0x6654);
        if (registers_.value(0x6648)!=temperatureBaseline_+1u ||
            (commandStatus&0x1ffff)!=0x10000 ||
            qint16(requestedReadback&0xffff)!=temperatureTarget_ ||
            qint16(requestedReadback>>16)!=temperatureTarget_) {
            fail(QStringLiteral("AI8 ACK 已收到，但设温确认计数、状态或读回值未匹配。")); return;
        }
        current_.reset(); advance(); return;
    }
    switch (s.kind) {
    case Kind::Read: case Kind::Wait:
        if (s.kind==Kind::Wait) waitTimer_.start();
        pendingSequence_=session_->readRegisters(s.address,s.count); break;
    case Kind::Write: pendingSequence_=session_->writeRegisters(s.address,s.values); break;
    case Kind::Commit: pendingSequence_=session_->commitConfig(quint64(1)<<(s.source&63),s.source); break;
    case Kind::Ping: pendingSequence_=session_->ping(QByteArrayLiteral("VaporView")); break;
    case Kind::Capabilities: pendingSequence_=session_->requestCapabilities(); break;
    case Kind::Sensor: pendingSequence_=session_->sensorAction(s.address,s.expected,s.source); break;
    case Kind::CheckVersions: case Kind::CheckTemperature: break;
    }
    if (!pendingSequence_) fail(QStringLiteral("命令未发送。"));
}

void FpgaDeviceController::completed(quint32 seq, bool success, quint32 status)
{
    if (!current_ || seq!=pendingSequence_) return;
    if (!success) { fail(QStringLiteral("%1失败：VLP status=%2；请查看模块状态和错误。") .arg(operation_).arg(status)); return; }
    if (current_->kind==Kind::Wait &&
        (!registers_.contains(current_->address) || (registers_.value(current_->address)&current_->mask)!=current_->expected)) {
        if (waitTimer_.elapsed()>=10000) { fail(QStringLiteral("读回等待超时：")+hex(current_->address)); return; }
        QTimer::singleShot(50,this,[this] {
            if (current_ && current_->kind==Kind::Wait && busy_)
                pendingSequence_=session_->readRegisters(current_->address,1);
        }); return;
    }
    if (current_->label==QStringLiteral("temperature_baseline"))
        temperatureBaseline_=registers_.value(0x6648);
    current_.reset(); pendingSequence_=0;
    QTimer::singleShot(0,this,&FpgaDeviceController::advance);
}

void FpgaDeviceController::fail(const QString& detail)
{
    steps_.clear(); current_.reset(); pendingSequence_=0; busy_=false; drainWaveforms_=false;
    if (session_) session_->cancelQueuedCommands();
    detail_=detail; report(detail, LogLevel::Error);
    emit connectionChanged(ready_,false,detail);
    emit snapshot(nowUs(),{{"event","operation_failed"},{"detail",detail}});
}

void FpgaDeviceController::report(const QString& detail, LogLevel level)
{
    LogRecord record;
    record.level=level; record.source=QStringLiteral("Ground"); record.category=QStringLiteral("device.fpga");
    record.message=detail;
    record.fields={{QStringLiteral("event"),QStringLiteral("fpga_operation_diagnostic")},
        {QStringLiteral("operation"),operation_},{QStringLiteral("ready"),ready_},{QStringLiteral("busy"),busy_}};
    if (level>=LogLevel::Error) record.fields.insert(QStringLiteral("error_code"),QStringLiteral("FPGA_OPERATION_FAILED"));
    emit logRecordGenerated(record);
}

void FpgaDeviceController::setConfiguration(const FpgaControlConfig& config)
{ configuration_=config; }
void FpgaDeviceController::snapshotConfiguration()
{
    QJsonObject actual;
    for (auto i=registers_.cbegin(); i!=registers_.cend(); ++i) actual[hex(i.key())]=double(i.value());
    emit snapshot(nowUs(),{{"event","config"},{"configuration",configuration_.toJson()},
        {"hardware_registers",actual},{"ready",ready_},{"usb_connected",session_ && session_->isOpen()},
        {"usb",session_?QJsonObject::fromVariantMap(session_->transport()->diagnostic().fields):QJsonObject{}}});
}

void FpgaDeviceController::refresh()
{
    if (!begin(QStringLiteral("读取实际状态"))) return;
    readAll(); steps_.push_back({Kind::CheckVersions}); advance();
}

void FpgaDeviceController::stopAndDrain()
{
    drainWaveforms_ = true;
    for (quint32 b : {0x4000u,0x4100u}) write(b+4,0);
    for (quint32 b : {0x4000u,0x4100u}) { waitFor(b+8,0xd,0); waitFor(b+0x20,0xffffffff,0); }
    for (quint32 b : {0x5000u,0x5100u}) write(b+4,0);
    for (quint32 b : {0x5000u,0x5100u}) { waitFor(b+8,5,0); waitFor(b+0x3c,0xffffffff,0); }
}

void FpgaDeviceController::applyConfiguration(const FpgaControlConfig& config)
{
    if (busy_) { report(QStringLiteral("配置操作被拒绝：已有事务正在执行。")); return; }
    if (!ready_) { report(QStringLiteral("请先连接并通过模块版本核验。")); return; }
    if (config.adcRateHz<1000 || config.adcRateHz>1000000 ||
        config.dlia[0].outputRateHz==0 || config.dlia[1].outputRateHz==0 ||
        config.dlia[0].outputRateHz>config.adcRateHz/8 || config.dlia[1].outputRateHz>config.adcRateHz/8) {
        fail(QStringLiteral("采样率或 DLIA 输出率超出当前双 AD4630 接口范围。")); return;
    }
    if (config.ai8.slaveAddress<1 || config.ai8.slaveAddress>80 || config.ai8.channel<1 || config.ai8.channel>8
        || config.ai8.pollIntervalMs<1 || config.ai8.pollIntervalMs>3600000
        || config.ai8.timeoutMs<1 || config.ai8.timeoutMs>65535 || config.ai8.retryLimit>3) {
        fail(QStringLiteral("AI8 地址、通道、轮询、超时或重试参数超出协议范围。")); return;
    }
    for (int ch=0; ch<2; ++ch) {
        const auto& w=config.wms[ch]; const auto& d=config.dlia[ch]; const auto& dac=config.dac[ch];
        const auto& f=config.frontend[ch];
        if (!w.scanMilliHz || !w.sineMilliHz || !w.updateRateHz || w.phaseMode>2
            || !d.referenceMilliHz || d.mode>3 || dac.deviceCtrl>0x3ff || dac.clearCode>0xfffff
            || dac.minCode>dac.maxCode || dac.maxCode>0xfffff || !dac.spiClockHz || dac.spiClockHz>20000000
            || !std::isfinite(f.voltsPerCode) || f.voltsPerCode<=0 || (f.polarity!=1 && f.polarity!=-1)) {
            fail(QStringLiteral("通道 %1 的波形、DAC、解调或前端标定参数不合法。").arg(ch)); return;
        }
    }
    if (!begin(QStringLiteral("提交并核对配置"))) return;
    configuration_=config; stopAndDrain();
    const bool ai8Changed=registers_.value(0x6614)!=config.ai8.slaveAddress ||
        registers_.value(0x661c)!=config.ai8.channel || registers_.value(0x662c)!=config.ai8.pollIntervalMs
        || registers_.value(0x6660)!=config.ai8.timeoutMs || registers_.value(0x6664)!=config.ai8.retryLimit;
    if (ai8Changed && (registers_.value(0x6604)&1)) {
        fail(QStringLiteral("AI8 地址、通道或轮询配置只能在禁用时修改；请先禁用 AI8。")); return;
    }
    for (int ch=0; ch<2; ++ch) {
        const auto &dac=config.dac[ch]; const quint32 db=0x3000+quint32(ch)*0x100;
        const QMap<quint32,quint32> dacWords{{0x10,dac.deviceCtrl},{0x14,dac.clearCode},
            {0x18,dac.minCode},{0x1c,dac.maxCode},{0x24,dac.spiClockHz},{0x34,dac.gain},{0x38,quint32(dac.offset)}};
        for (auto i=dacWords.cbegin();i!=dacWords.cend();++i) { write(db+i.key(),i.value()); verify(db+i.key(),i.value()); }
        const auto &w=config.wms[ch]; const quint32 b=0x2000+quint32(ch)*0x100;
        const quint32 vals[]={w.scanMilliHz,quint32(w.scanAmplitude),quint32(w.bias),w.sineMilliHz,
            quint32(w.sineAmplitude),w.phase,w.updateRateHz};
        for (quint32 i=0;i<7;++i) { write(b+0x10+i*4,vals[i]); verify(b+0x10+i*4,vals[i]); }
        write(b+0x40,w.phaseMode); verify(b+0x40,w.phaseMode);
        // WMS COMMIT coordinates the DAC and restores the existing WMS enable.
        commit(quint16(0x10+ch),b); read(b+0x2c,1);
    }
    if (ai8Changed) {
        write(0x6610,19200); write(0x6618,0);
        write(0x6614,config.ai8.slaveAddress); verify(0x6614,config.ai8.slaveAddress);
        write(0x661c,config.ai8.channel); verify(0x661c,config.ai8.channel);
        write(0x662c,config.ai8.pollIntervalMs); verify(0x662c,config.ai8.pollIntervalMs);
        write(0x6660,config.ai8.timeoutMs); verify(0x6660,config.ai8.timeoutMs);
        write(0x6664,config.ai8.retryLimit); verify(0x6664,config.ai8.retryLimit);
    }
    write(0x4010,config.adcRateHz); verify(0x4010,config.adcRateHz);
    write(0x401c,0); write(0x411c,0);
    commit(0x20,0x4000); read(0x4014); read(0x4114);
    for (int ch=0; ch<2; ++ch) {
        const auto &d=config.dlia[ch]; const quint32 b=0x5000+quint32(ch)*0x100;
        const quint32 vals[]={d.referenceMilliHz,d.phase1,d.phase2,d.outputRateHz,d.mode};
        for (quint32 i=0;i<5;++i) { write(b+0x10+i*4,vals[i]); verify(b+0x10+i*4,vals[i]); }
        commit(quint16(0x30+ch),b); read(b+0x4c,2);
    }
    readAll(); advance();
}

void FpgaDeviceController::setAcquisition(bool enable)
{
    if (!ready_ || !begin(enable?QStringLiteral("启动采集"):QStringLiteral("停止并排空采集"))) return;
    if (enable) {
        if (!enableUpload()) return;
        // WMS supplies cycle labels even with independent DLIA references.
        write(0x0004,registers_.value(4)|1u);
        for (quint32 b:{0x2000u,0x2100u}) { write(b+4,1); verify(b+4,1); }
        for (quint32 b:{0x5000u,0x5100u}) { write(b+4,1); waitFor(b+8,7,3); }
        for (quint32 b:{0x4000u,0x4100u}) { write(b+4,1); waitFor(b+8,5,1); }
    } else {
        rawTimer_.stop(); stopAndDrain();
        write(0x403c,0); write(0x413c,0); write(0x0050,0);
    }
    readAll(); advance();
}

void FpgaDeviceController::setWaveform(int channel, bool enable)
{
    if (channel<0 || channel>1 || !ready_ || !begin(QStringLiteral("独立控制 WMS"))) return;
    const quint32 b=0x2000+quint32(channel)*0x100;
    if (enable) write(4,registers_.value(4)|1u);
    write(b+4,enable?1:0); verify(b+4,enable?1:0); read(b+8,2); advance();
}

void FpgaDeviceController::setDac(int channel, bool enable)
{
    if (channel<0 || channel>1 || !ready_ || !begin(QStringLiteral("独立控制 DAC"))) return;
    const quint32 b=0x3000+quint32(channel)*0x100;
    if (enable) write(4,registers_.value(4)|1u);
    write(b+4,enable?1:0); verify(b+4,enable?1:0); read(b+8,2); advance();
}

void FpgaDeviceController::setSensorEnabled(quint16 source, bool enable)
{
    if (!ready_ || source<0x40 || source>0x46 || source==0x41 || !begin(QStringLiteral("设置传感器状态"))) return;
    if (enable && !enableUpload()) return;
    if (source==0x43 && enable) {
        if (registers_.value(0x6304)&1) { fail(QStringLiteral("修改 BMP 地址前请先禁用 BMP390。")); return; }
        write(0x6310,0x77); verify(0x6310,0x77);
    }
    Step s; s.kind=Kind::Sensor; s.address=1; s.source=source; s.expected=enable?1:0; steps_.push_back(s);
    readAll(); advance();
}

void FpgaDeviceController::setRawEnabled(bool enable)
{
    if (!ready_) return;
    if (busy_) { if (!enable) QTimer::singleShot(100,this,[this]{ setRawEnabled(false); }); return; }
    if (!begin(QStringLiteral("设置 RAW 诊断窗口"))) return;
    write(0x0050,enable?3:0);
    verify(0x0050,enable?3:0);
    for (quint32 b:{0x4000u,0x4100u}) { write(b+0x3c,enable?1:0); verify(b+0x3c,enable?1:0); }
    if (enable) rawTimer_.start(qBound(1,configuration_.rawWindowSeconds,60)*1000);
    else rawTimer_.stop();
    readAll(); advance();
}

void FpgaDeviceController::setTemperature(double celsius)
{
    if (busy_) { report(QStringLiteral("设温被拒绝：已有事务正在执行。")); return; }
    const double raw=celsius*10;
    if (!std::isfinite(celsius) || celsius < -999 || celsius > 2147.4 ||
        std::abs(raw-std::round(raw))>1e-6) { fail(QStringLiteral("AI8 温度必须为 0.1℃ 的整数倍，并位于设备允许范围。")); return; }
    if (!ready_ || !begin(QStringLiteral("AI8 设温与确认"))) return;
    temperatureTarget_=qint16(std::llround(raw));
    Step baseline; baseline.kind=Kind::Read; baseline.address=0x6648; baseline.label=QStringLiteral("temperature_baseline");
    steps_.push_back(baseline);
    Step s; s.kind=Kind::Sensor; s.address=17; s.source=0x46; s.expected=quint32(qint32(std::llround(celsius*1000000)));
    steps_.push_back(s); read(0x6648,2); read(0x6654);
    steps_.push_back({Kind::CheckTemperature}); readAll(); advance();
}

void FpgaDeviceController::acceptReading(const FpgaSensor::Reading& reading)
{
    latestReadings_[reading.source]=adapter_.accept(reading);
    lastReadingMs_[reading.source]=QDateTime::currentMSecsSinceEpoch();
    dirtyReadings_.insert(reading.source); expiredReadings_.remove(reading.source);
}

void FpgaDeviceController::flushPresentation()
{
    const qint64 now=QDateTime::currentMSecsSinceEpoch();
    for (auto i=latestReadings_.begin(); i!=latestReadings_.end(); ++i) {
        const bool expired=!replaying_ && now-lastReadingMs_.value(i.key())>5000;
        if (!dirtyReadings_.contains(i.key()) && (!expired || expiredReadings_.contains(i.key()))) continue;
        auto value=i.value();
        if (expired) {
            value.reading.validity.measurement=false; value.reading.validity.deviceOnline=false;
            if (value.epsilon) value.epsilon->valid=false;
            value.attitudeValid=value.navigationValid=value.utcValid=false;
            if (value.ai8Live) value.ai8Live->valid=false;
            expiredReadings_.insert(i.key());
        }
        emit measurementUpdated(value);
    }
    dirtyReadings_.clear();
    for (const auto& wave:latestWaves_) emit waveformUpdated(wave);
    latestWaves_.clear();
}

void FpgaDeviceController::replaySession(const QString& directory)
{
    if (session_ && session_->isOpen()) { report(QStringLiteral("离线回放前请先断开 USB。")); return; }
    disconnectDevice();
    installSession(makeFpgaUsbReplayTransport());
    replaying_=true;
    emit connectionChanged(false,true,QStringLiteral("正在解析离线 FPGA 会话"));
    QString error;
    QElapsedTimer presentation; presentation.start();
    const bool ok=VaporView::Session::FpgaSessionArchive::replay(directory,
        [this,&presentation](quint64, const FpgaVlp1::Frame& frame) {
            QByteArray bytes(reinterpret_cast<const char*>(frame.bytes.data()),qsizetype(frame.bytes.size()));
            while (bytes.size()%4) bytes.append(char(0));
            session_->ingestReplayBytes(bytes);
            if (presentation.elapsed()>=50) { flushPresentation(); presentation.restart(); }
            if (QThread::currentThread()->isInterruptionRequested()) return false;
            return true;
        }, &error, [this](quint64, const QJsonObject&) {
            session_->flushWaveforms(); flushPresentation();
            adapter_.reset(); latestReadings_.clear(); latestWaves_.clear();
            dirtyReadings_.clear(); expiredReadings_.clear();
            session_->close();
        }, [] { return QThread::currentThread()->isInterruptionRequested(); });
    session_->flushWaveforms(); flushPresentation(); replaying_=false;
    emit connectionChanged(false,false,ok?QStringLiteral("离线会话已载入；USB 未连接"):error);
    emit replayFinished(ok,ok?directory:error);
}

void FpgaDeviceController::exportSession(const QString& directory, const QString& output)
{
    if (busy_ || replaying_ || (session_ && session_->isOpen())) {
        report(QStringLiteral("导出前请完成当前事务并断开 USB，确保接收循环不被文件解析阻塞。")); return;
    }
    using Archive=VaporView::Session::FpgaSessionArchive;
    const QString extension=QFileInfo(output).suffix().toLower();
    const auto format=extension=="bin"?Archive::ExportFormat::Bin:
        extension=="csv"?Archive::ExportFormat::Csv:Archive::ExportFormat::Json;
    QString error;
    const bool ok=Archive::exportTo(directory,output,format,&error,
        [] { return QThread::currentThread()->isInterruptionRequested(); });
    report(ok?QStringLiteral("FPGA 会话已导出：")+output:error,ok?LogLevel::Info:LogLevel::Error);
}
}
