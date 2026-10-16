#include "FpgaDeviceController.h"
#include "FpgaUsbSimulator.h"
#include "FpgaTelemetry.h"
#include "FpgaAdvancedParameters.h"
#include "ppk/ObservationStore.h"
#include "shared/session/FpgaSessionArchive.h"

#include <QDateTime>
#include <QFileInfo>
#include <QThread>
#include <QtEndian>
#include <cmath>
#include <utility>

namespace VaporView::Ground::Devices
{
namespace {
constexpr quint32 kBases[] = {0x2000,0x2100,0x3000,0x3100,0x4000,0x4100,0x5000,0x5100,
                              0x6000,0x6100,0x6200,0x6300,0x6400,0x6500,0x6600};
QString hex(quint32 value) { return QStringLiteral("0x%1").arg(value, 8, 16, QLatin1Char('0')); }
}

quint64 FpgaDeviceController::nowUs() { return quint64(QDateTime::currentMSecsSinceEpoch()) * 1000; }

FpgaDeviceController::FpgaDeviceController(std::unique_ptr<FpgaUsbTransport> transport, QObject *parent)
    : QObject(parent), injected_(bool(transport))
{
    observations_=Ppk::RawSatelliteAssembler([this](const std::string& event,const std::string& reason,quint8 receiver,quint64 utc,int received,int expected) {
        if(event=="epsilon_raw_satellite_epoch_completed")return;
        LogRecord record;record.source=QStringLiteral("Ground");record.category=QStringLiteral("device.fpga");record.level=LogLevel::Warning;
        record.message=QStringLiteral("EPSILON 原始观测：%1，接收 %2/%3 包。").arg(QString::fromStdString(reason)).arg(received).arg(expected);
        record.fields={{"event","fpga_firmware_event"},{"observation_event",QString::fromStdString(event)},{"receiver",receiver},{"utc_ns",QString::number(utc)}};emit logRecordGenerated(record);
    });
    qRegisterMetaType<FpgaControlConfig>();
    qRegisterMetaType<FpgaSensor::AdaptedMeasurements>();
    qRegisterMetaType<LogRecord>();
    presentationTimer_.setParent(this); statusTimer_.setParent(this);watchdogTimer_.setParent(this);
    presentationTimer_.setInterval(50);
    statusTimer_.setInterval(2000);
    connect(&watchdogTimer_,&QTimer::timeout,this,[this] {
        if(ready_ && connected())session_->writeRegisters(0x0044,{1});
    });
    connect(&presentationTimer_, &QTimer::timeout, this, &FpgaDeviceController::flushPresentation);
    connect(&statusTimer_, &QTimer::timeout, this, [this] {
        if (ready_ && !busy_ && begin(QStringLiteral("读取实际状态"), true)) {
            readAll(); steps_.push_back({Kind::CheckVersions}); advance();
        }
    });
    if (transport) installSession(std::move(transport));
}

FpgaDeviceController::~FpgaDeviceController() { disconnectDevice(); }

QJsonObject FpgaDeviceController::discoverDevices(const QString& backend) const
{
    if (injected_) return session_->transport()->discoverDevices();
    if ((backend.isEmpty() ? configuration_.backend : backend) == QStringLiteral("simulator"))
        return makeFpgaUsbSimulator()->discoverDevices();
    return discoverFpgaUsbDevices();
}

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
    connect(session_, &FpgaDeviceSession::frameReceived, this,
        [this](quint8 type,quint32 sequence,quint16 source,quint16 msg,quint64 tick,quint32,const QByteArray& payload) {
            if(!replaying_ && !connectionStartTick_ && type==2 && source==1 && msg==0xfe
                && current_ && current_->kind==Kind::Ping && sequence==pendingSequence_
                && payload.size()>=4 && qFromLittleEndian<quint32>(payload.constData())==0) {
                // The reply was generated during this handshake, unlike queued
                // events. Subtract the entire host elapsed time conservatively so
                // a new fault during connection is not mistaken for old backlog.
                const quint64 elapsedTicks=quint64(connectionClock_.nsecsElapsed()/10);
                connectionStartTick_=tick>elapsedTicks?tick-elapsedTicks:0;
                const auto pending=std::exchange(pendingManagement_,{});
                for(const auto& queued:pending)publishManagementEvent(queued);
            }
            const auto event=FpgaRemote::decodeManagementEvent(type,source,msg,tick,payload);
            if(event.isEmpty())return;
            if(!replaying_ && !connectionStartTick_) {pendingManagement_.append(event);return;}
            publishManagementEvent(event);
        });
    connect(session_, &FpgaDeviceSession::sensorReadingReceived, this, &FpgaDeviceController::acceptReading);
    connect(session_, &FpgaDeviceSession::waveformCompleted, this,
        [this](const FpgaWave::CompletedStream& stream) {
            if (recoveringConnection_) return;
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

void FpgaDeviceController::publishManagementEvent(const QJsonObject& event)
{
    const auto tick=event.value("tick").toString().toULongLong();
    if(!replaying_ && (!connectionStartTick_ || tick<*connectionStartTick_))return;
    const auto msg=quint16(event.value("message").toInt());
    const auto source=quint16(event.value("source").toInt());
    const auto key=QString::number(msg,16)+(msg==0x1401||msg==0x1402?QStringLiteral(":")+QString::number(source,16)+QStringLiteral(":")+QString::number(event.value("words").toArray()[0].toInt(),16):QString());
    const auto old=management_.value(key).toObject().value("words").toArray();
    const auto words=event.value("words").toArray();
    management_[key]=event;emit managementUpdated(management_);
    const bool dropped=msg==0x1400 && old.size()==7 && (words[4].toDouble()>old[4].toDouble() || words[5].toDouble()>old[5].toDouble());
    if(msg==0x1402 || msg==0x1300 || dropped) {
        if(!replaying_ && msg==0x1402 && startupWarningDeadlines_.contains(source))
            startupWarnings_[source]=event;
        else logManagementEvent(event);
    }
    if(!replaying_)emit snapshot(nowUs(),{{"event","firmware_event"},{"data",event}});
}

void FpgaDeviceController::logManagementEvent(const QJsonObject& event, bool afterStartup)
{
    const auto msg=quint16(event.value("message").toInt());
    const auto source=quint16(event.value("source").toInt());
    const auto words=event.value("words").toArray();
    LogRecord record;record.source=QStringLiteral("Ground");record.category=QStringLiteral("device.fpga");
    record.level=msg==0x1300?LogLevel::Info:LogLevel::Warning;
    record.message=msg==0x1402?QStringLiteral("FPGA 模块 %1 新错误 %2，累计 %3。")
        .arg(hex(quint32(words[0].toDouble())),hex(quint32(words[2].toDouble())),hex(quint32(words[1].toDouble())))
        :msg==0x1300?QStringLiteral("FPGA 电机运动 %1 结束：位置 %2，剩余 %3，错误 %4。")
        .arg(words[0].toDouble()).arg(qint32(quint32(words[1].toDouble()))).arg(words[2].toDouble()).arg(hex(quint32(words[3].toDouble())))
        :QStringLiteral("FPGA 上传发生丢包：事件 %1，生产端合计 %2。注意两个计数分别统计。")
        .arg(words[4].toDouble()).arg(words[5].toDouble());
    record.fields={{"event","fpga_firmware_event"},{"message_id",msg},{"source_id",source},{"tick",event.value("tick").toString()},{"payload",event.toVariantMap()},{"ui_visibility","attention"}};
    if(afterStartup) {
        record.message=QStringLiteral("启动等待 3 秒后仍异常；启动期间记录：")+record.message;
        record.fields.insert(QStringLiteral("startup_grace_ms"),3000);
    }
    emit logRecordGenerated(record);
}

void FpgaDeviceController::connectDevice(const QString& locator, const QString& backend, ConnectMode mode)
{
    disconnectDevice();
    if (!injected_) {
        // A fresh simulator opened from a device row represents an idle board.
        // Full demo connections retain their already-running initial scene.
        if (backend == QStringLiteral("simulator") && mode == ConnectMode::PreserveRunningModules)
            installSession(makeFpgaUsbSimulator(nullptr, FpgaSimulatorStartup::Idle));
        else installSession(makeFpgaUsbHardwareTransport(backend));
    }
    QString error;
    connectionClock_.start();
    if (!session_->open(locator, &error)) { fail(error); return; }
    emit transportConnectionChanged(true);
    adapter_.reset();observations_.flush();
    recoveringConnection_=true;
    emit snapshot(nowUs(), {{"event","connected"},{"backend",backend},{"locator",locator},
        {"usb", QJsonObject::fromVariantMap(session_->transport()->diagnostic().fields)}});
    begin(QStringLiteral("核验协议和模块版本"));
    steps_.push_back({Kind::Ping}); steps_.push_back({Kind::Capabilities});
    readAll(); steps_.push_back({Kind::CheckVersions});
    // A device-row action opens the shared transport without restarting other
    // sensors or acquisition. Explicit full connections retain recovery.
    if (mode == ConnectMode::RecoverEnabledModules) steps_.push_back({Kind::RecoverConnection});
    advance();
}

void FpgaDeviceController::disconnectDevice()
{
    ++operationGeneration_;
    connectionStartTick_.reset();pendingManagement_.clear();connectionClock_.invalidate();
    startupWarningDeadlines_.clear();startupWarnings_.clear();
    observations_.flush();
    presentationTimer_.stop(); statusTimer_.stop();watchdogTimer_.stop();
    pendingConnections_.clear(); operationConnections_.clear();
    rawOperation_.reset();
    if (session_) session_->close();
    flushPresentation();
    emit transportConnectionChanged(false);
    steps_.clear(); current_.reset(); pendingSequence_ = 0;
    ready_ = busy_ = replaying_ = drainWaveforms_ = backgroundRefresh_ = recoveringConnection_ = connectionRecoveryFailed_ = false;
    registers_.clear();management_={};emit managementUpdated(management_); adapter_.reset(); latestReadings_.clear(); latestWaves_.clear(); lastReadingMs_.clear();
    dirtyReadings_.clear(); expiredReadings_.clear();
    emit snapshot(nowUs(), {{"event","disconnected"}});
    emit hardwareValuesChanged(registers_);
    emit connectionChanged(false,false,QStringLiteral("USB 未连接"));
}

bool FpgaDeviceController::begin(const QString& label, bool background)
{
    if ((busy_ && !backgroundRefresh_) || !session_ || !session_->isOpen()) { report(QStringLiteral("当前无法执行：") + label); return false; }
    // A foreground operation replaces the remaining background reads. The session
    // still serializes it behind any read already on the wire.
    busy_=true; backgroundRefresh_=background; operation_=label;
    ++operationGeneration_;
    steps_.clear(); current_.reset(); pendingSequence_=0;pendingSensorParameters_={};
    operationConnections_.clear();
    if (!background) { detail_=label; emit connectionChanged(ready_,true,label); }
    return true;
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
    read(0x6110,15); read(0x6710,10); read(0x6700,4); // Motor remains idle unless explicitly requested.
    read(0x7000,4); read(0x7014); read(0x701c,6); read(0x7038,4);
    read(0x8000,4); read(0x8010,12);
    for (quint32 base : kBases) read(base,4);
    for (quint32 base : {0x2000u,0x2100u}) { read(base+0x10,15); }
    for (quint32 base : {0x3000u,0x3100u}) { read(base+0x10,14); }
    for (quint32 base : {0x4000u,0x4100u}) { read(base+0x10,12); }
    for (quint32 base : {0x5000u,0x5100u}) { read(base+0x10,18); }
    for(const auto& spec:FpgaRemote::sensorParameters())read(spec.address);
    read(0x0040);read(0x7010);read(0x7018);read(0x7034);
    read(0x6010,3); // PTB baud, serial format and polling interval.
    read(0x6210);   // EPSILON baud.
    read(0x6314);   // BMP polling interval.
    read(0x6414);   // SHT polling interval.
    read(0x6510);   // TFA baud.
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
        if(recoveringConnection_) {
            recoveringConnection_=false;
            presentationTimer_.start();statusTimer_.start();
        }
        busy_=false; emit hardwareValuesChanged(registers_);
        if (rawOperation_) {
            reportRawResult(*rawOperation_,true,QStringLiteral("寄存器读回确认。"));
            rawOperation_.reset();
        }
        if (!backgroundRefresh_) emit connectionChanged(ready_,false,operation_+QStringLiteral("完成"));
        backgroundRefresh_=false;
        emit snapshot(nowUs(),{{"event","operation_completed"},{"operation",operation_},{"configuration",configuration_.toJson()}});
        return;
    }
    current_=steps_.front(); steps_.pop_front();
    if(current_->label==QStringLiteral("hmp_confirmation"))current_->expected=hmpCompensationBaseline_+1u;
    const auto& s=*current_;
    if (s.kind==Kind::ConfirmConnection) {
        confirmConnection(s.source); current_.reset(); advance(); return;
    }
    if (s.kind==Kind::PrepareSensorConfig) {
        const auto source=s.source; const auto values=s.values;
        current_.reset(); prepareSensorConfiguration(source,values[0],values[1],values[2]!=0); advance(); return;
    }
    if (s.kind==Kind::Delay) {
        const auto generation=operationGeneration_;
        QTimer::singleShot(int(s.expected),this,[this,generation] {
            if (generation==operationGeneration_ && current_ && current_->kind==Kind::Delay) {current_.reset();advance();}
        }); return;
    }
    if(s.kind==Kind::FinishSensorConfig) {
        if(s.source==0x46)configuration_.ai8.pollIntervalMs=registers_.value(0x662c);
        for(auto it=pendingSensorParameters_.begin();it!=pendingSensorParameters_.end();++it)configuration_.sensorParameters[s.source][it.key()]=it.value();
        emit sensorConfigurationApplied(s.source,registers_,pendingSensorParameters_);
        current_.reset();advance();return;
    }
    if (s.kind==Kind::CheckVersions) {
        if(connectionRecoveryFailed_) {fail(QStringLiteral("连接恢复未完成，请排查后重新连接。"));return;}
        QString reason; ready_=versionsMatch(&reason);
        if (!ready_) { fail(reason); return; }
        current_.reset(); advance(); return;
    }
    if (s.kind==Kind::RecoverConnection) {
        current_.reset();recoverConnection();advance();return;
    }
    if (s.kind==Kind::FinishRawStop) {
        session_->flushWaveforms(0x20);session_->flushWaveforms(0x21);flushPresentation();
        report(QStringLiteral("RAW 已停止上传；保留最后完整波形作为历史值，ADC/DLIA 采集状态未改变。"),LogLevel::Info);
        current_.reset();advance();return;
    }
    if(s.kind==Kind::FinishWatchdog) {
        if(s.expected)watchdogTimer_.start(1000);else watchdogTimer_.stop();
        current_.reset();advance();return;
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
    case Kind::Management: pendingSequence_=session_->managementAction(quint16(s.address),s.source,s.values); break;
    case Kind::Read: case Kind::Wait:
        if (s.kind==Kind::Wait) waitTimer_.start();
        pendingSequence_=session_->readRegisters(s.address,s.count); break;
    case Kind::Write:
        if (s.label==QStringLiteral("sensor_parameter")) registers_.remove(s.address);
        pendingSequence_=session_->writeRegisters(s.address,s.values); break;
    case Kind::Commit: pendingSequence_=session_->commitConfig(quint64(1)<<(s.source&63),s.source); break;
    case Kind::Ping: pendingSequence_=session_->ping(QByteArrayLiteral("VaporView")); break;
    case Kind::Capabilities: pendingSequence_=session_->requestCapabilities(); break;
    case Kind::Sensor:
        if (s.address==1) {
            latestReadings_.remove(s.source); lastReadingMs_.remove(s.source);
            if (!s.expected && !operationConnections_.contains(s.source)) {
                pendingConnections_.remove(s.source);
                startupWarningDeadlines_.remove(s.source);startupWarnings_.remove(s.source);
            }
        }
        pendingSequence_=session_->sensorAction(s.address,s.expected,s.source); break;
    case Kind::Delay: case Kind::PrepareSensorConfig: case Kind::FinishSensorConfig: case Kind::CheckVersions: case Kind::RecoverConnection: case Kind::FinishRawStop: case Kind::CheckTemperature: case Kind::ConfirmConnection: case Kind::FinishWatchdog: break;
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
    if(current_->label==QStringLiteral("hmp_baseline"))hmpCompensationBaseline_=registers_.value(0x6148);
    current_.reset(); pendingSequence_=0;
    QTimer::singleShot(0,this,&FpgaDeviceController::advance);
}

void FpgaDeviceController::fail(const QString& detail)
{
    if (rawOperation_) {
        reportRawResult(*rawOperation_,false,detail);
        rawOperation_.reset();
    }
    const auto affected=operationConnections_;
    for (quint16 source:affected) {
        if (pendingConnections_.contains(source)) reportConnection(source,QStringLiteral("failed"),QStringLiteral("TRANSACTION_FAILED"),detail);
        pendingConnections_.remove(source);
        startupWarningDeadlines_.remove(source);startupWarnings_.remove(source);
    }
    operationConnections_.clear();
    if(recoveringConnection_) {ready_=false;recoveringConnection_=false;connectionRecoveryFailed_=true;}
    steps_.clear(); current_.reset(); pendingSequence_=0; busy_=false; drainWaveforms_=false; backgroundRefresh_=false;
    if (session_) session_->cancelQueuedCommands();
    detail_=detail; report(detail, LogLevel::Error);
    emit hardwareValuesChanged(registers_);
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

void FpgaDeviceController::reportRawResult(bool enabled, bool succeeded, const QString& detail)
{
    LogRecord record;
    record.source=QStringLiteral("Ground"); record.category=QStringLiteral("device.fpga");
    record.level=succeeded?LogLevel::Info:LogLevel::Warning;
    record.message=QStringLiteral("原始信号：")+(enabled?QStringLiteral("开始采集"):QStringLiteral("停止采集"))+
        (succeeded?QStringLiteral("成功，"):QStringLiteral("失败，"))+detail;
    record.fields={{QStringLiteral("event"),QStringLiteral("fpga_raw_acquisition_result")},
        {QStringLiteral("enabled"),enabled},{QStringLiteral("result"),succeeded?QStringLiteral("succeeded"):QStringLiteral("failed")},
        {QStringLiteral("reason"),detail},{QStringLiteral("ui_visibility"),QStringLiteral("attention")}};
    emit logRecordGenerated(record);
}

void FpgaDeviceController::trackConnection(quint16 source)
{
    pendingConnections_[source]=0; operationConnections_.insert(source);
    if(source>=0x40 && source<=0x46) {
        startupWarningDeadlines_[source]=-1;
        startupWarnings_.remove(source);
    }
}

void FpgaDeviceController::reportConnection(quint16 source, const QString& result, const QString& reasonCode, const QString& detail)
{
    const QMap<quint16,QString> names{{0,QStringLiteral("ADC / DLIA")},{0x40,QStringLiteral("PTB210")},
        {0x41,QStringLiteral("HMP3")},{0x42,QStringLiteral("EPSILON")},{0x43,QStringLiteral("BMP390")},{0x44,QStringLiteral("SHT45")},
        {0x45,QStringLiteral("TFA1500-L")},{0x46,QStringLiteral("AI-8288D92J0")}};
    LogRecord record; record.source=QStringLiteral("Ground"); record.category=QStringLiteral("device.fpga");
    record.level=(result==QStringLiteral("failed") || result==QStringLiteral("warning"))?LogLevel::Warning:LogLevel::Info;
    record.message=names.value(source)+QStringLiteral(" 经 FPGA ")+
        (result==QStringLiteral("succeeded")?QStringLiteral("连接成功："):result==QStringLiteral("failed")?
         QStringLiteral("连接失败："):QStringLiteral("通道已启用："))+detail;
    record.fields={{QStringLiteral("event"),QStringLiteral("fpga_device_connection_result")},
        {QStringLiteral("device"),names.value(source)},{QStringLiteral("fpga_source"),source},
        {QStringLiteral("result"),result},{QStringLiteral("reason_code"),reasonCode},
        {QStringLiteral("reason"),detail},{QStringLiteral("ui_visibility"),QStringLiteral("attention")}};
    if(source) {
        const quint32 base=0x6000+(source-0x40)*0x100;
        record.fields.insert(QStringLiteral("module_status"),registers_.value(base+8));
        record.fields.insert(QStringLiteral("module_error"),registers_.value(base+12));
    }
    emit logRecordGenerated(record);
}

void FpgaDeviceController::confirmConnection(quint16 source)
{
    if(!pendingConnections_.contains(source))return;
    if(!source) {
        reportConnection(source,QStringLiteral("succeeded"),QStringLiteral("CONTROL_READBACK"),
            QStringLiteral("采集 CONTROL 读回确认；波形上传由 RAW 开关独立控制。"));
        pendingConnections_.remove(source); operationConnections_.remove(source); return;
    }
    const quint32 pollAddress=source==0x40?0x6018:source==0x41?0x611c:source==0x43?0x6314:source==0x44?0x6414:source==0x46?0x662c:0;
    const qint64 timeout=qMax<qint64>(8000,3*qint64(registers_.value(pollAddress))+5000);
    pendingConnections_[source]=QDateTime::currentMSecsSinceEpoch()+timeout;
    startupWarningDeadlines_[source]=connectionClock_.elapsed()+3000;
    reportConnection(source,QStringLiteral("waiting"),QStringLiteral("CHANNEL_ENABLED"),QStringLiteral("读回确认，等待设备有效数据。"));
}

void FpgaDeviceController::checkConnectionResults()
{
    const auto pending=pendingConnections_;
    const auto now=QDateTime::currentMSecsSinceEpoch();
    for(auto i=pending.cbegin();i!=pending.cend();++i) {
        if(!i.value())continue;
        const auto measurements=latestReadings_.value(i.key());
        const auto& reading=measurements.reading;
        const bool ai8Valid=reading.kind!=FpgaSensor::SensorKind::Ai8 || (measurements.ai8 && measurements.ai8->measurementValid);
        if(reading.validity.structure && reading.validity.crc && reading.validity.deviceOnline && reading.validity.measurement
            && ai8Valid && now-lastReadingMs_.value(i.key())<=5000) {
            reportConnection(i.key(),QStringLiteral("succeeded"),QStringLiteral("VALID_SAMPLE"),QStringLiteral("已收到设备有效数据。"));
        } else if(now>=i.value()) {
            reportConnection(i.key(),QStringLiteral("failed"),QStringLiteral("NO_VALID_SAMPLE"),
                QStringLiteral("等待有效数据超时；通道已启用，请检查设备供电、接线及通信参数。"));
        } else continue;
        pendingConnections_.remove(i.key()); operationConnections_.remove(i.key());
    }
}

void FpgaDeviceController::checkStartupWarnings()
{
    if(replaying_ || recoveringConnection_ || !connectionClock_.isValid())return;
    const auto deadlines=startupWarningDeadlines_;
    for(auto i=deadlines.cbegin();i!=deadlines.cend();++i) {
        if(i.value()<0 || connectionClock_.elapsed()<i.value())continue;
        const auto source=i.key();
        startupWarningDeadlines_.remove(source);
        const auto event=startupWarnings_.take(source);
        const auto sample=latestReadings_.value(source);
        const auto& validity=sample.reading.validity;
        // A latched error does not prove the current measurement is bad. TFA
        // emits no distance frame for invalid returns, so require a recent one.
        const bool fresh=lastReadingMs_.contains(source)
            && QDateTime::currentMSecsSinceEpoch()-lastReadingMs_.value(source)<=(source==0x45?1000:5000);
        const bool healthy=fresh && validity.structure && validity.crc && validity.deviceOnline && validity.measurement
            && (!sample.ai8 || (sample.ai8->measurementValid && !sample.ai8->alarmActive))
            && sample.reading.hmpDeviceError.value_or(0)==0;
        const auto words=event.value("words").toArray();
        const quint32 errors=words.size()==7?quint32(words[1].toDouble()):0;
        const quint32 base=0x6000+(source-0x40)*0x100;
        const bool hardwareFault=(errors & registers_.value(base+12) & 0x30)!=0;
        const bool newerSample=event.isEmpty() || sample.reading.timestamp>=event.value("tick").toString().toULongLong();
        if(healthy && newerSample && !hardwareFault)continue;
        if(!event.isEmpty())logManagementEvent(event,true);
        else reportConnection(source,QStringLiteral("warning"),QStringLiteral("STARTUP_UNHEALTHY"),
            QStringLiteral("启动等待 3 秒后仍未获得有效测量或设备仍在报警，请检查设备状态。"));
    }
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

void FpgaDeviceController::resetAcquisition()
{
    write(0x4004,2); waitFor(0x4008,0x92,0x12);
    write(0x4104,2);
    for (quint32 b:{0x5000u,0x5100u}) write(b+4,2);
    // Soft reset preserves shadow settings. Commit those actual settings,
    // rather than replacing the user's parameters with host defaults.
    commit(0x20,0x4000);
    for (quint32 b:{0x4000u,0x4100u}) waitFor(b+8,0x96,0x12);
    for (quint32 b:{0x5000u,0x5100u}) {
        commit(quint16(0x30+(b-0x5000)/0x100),b);
        verify(b+0xc,0);
    }
}

void FpgaDeviceController::recoverConnection()
{
    // Use device state, never host defaults. This recovery runs once after the
    // version check, not on the periodic readback path.
    const auto saved=registers_;
    operation_=QStringLiteral("连接后恢复采集与传感器");
    emit connectionChanged(false,true,operation_);
    snapshotConfiguration();
    for(quint16 source:{0x40,0x41,0x42,0x43,0x44,0x45,0x46})
        if(saved.value(0x6004+(source-0x40)*0x100)&1)trackConnection(source);
    for(quint32 control:{0x4004u,0x4104u,0x5004u,0x5104u})
        if(saved.value(control)&1) {trackConnection(0);break;}
    if(!enableUpload())return;
    stopAndDrain();resetAcquisition();
    for(quint32 b:{0x6000u,0x6100u,0x6200u,0x6300u,0x6400u,0x6500u,0x6600u}) {
        if(saved.value(b+4)&1) {
            Step stop;stop.kind=Kind::Sensor;stop.address=1;stop.source=quint16(0x40+(b-0x6000)/0x100);stop.expected=0;
            steps_.push_back(stop);verify(b+4,0);
            // PTB reports BUSY even in ST_DISABLED. TFA must finish its
            // serial stop/quiet sequence before accepting a new start.
            const quint32 stoppedMask=b==0x6500?13:(b==0x6300||b==0x6400)?9:1;
            waitFor(b+8,stoppedMask,0);
        }
        if(saved.value(b+0xc)) {
            report(QStringLiteral("连接恢复：模块 %1 清除已记录的历史错误 %2。").arg(hex(b),hex(saved.value(b+0xc))),LogLevel::Info);
            write(b+0xc,saved.value(b+0xc));read(b+0xc);
        }
        if(saved.value(b+4)&1) {
            Step start;start.kind=Kind::Sensor;start.address=1;start.source=quint16(0x40+(b-0x6000)/0x100);start.expected=1;
            steps_.push_back(start);verify(b+4,1);
            if(b==0x6500)waitFor(b+8,13,1);
            Step confirmed;confirmed.kind=Kind::ConfirmConnection;confirmed.source=start.source;steps_.push_back(confirmed);
        }
    }
    for(quint32 b:{0x4000u,0x4100u}) {write(b+0x3c,saved.value(b+0x3c));verify(b+0x3c,saved.value(b+0x3c));}
    for(quint32 b:{0x5000u,0x5100u,0x4000u,0x4100u}) {
        write(b+4,saved.value(b+4));verify(b+4,saved.value(b+4));
    }
    readAll();steps_.push_back({Kind::CheckVersions});
    if(pendingConnections_.contains(0)) {Step confirmed;confirmed.kind=Kind::ConfirmConnection;confirmed.source=0;steps_.push_back(confirmed);}
}

void FpgaDeviceController::applyConfiguration(const FpgaControlConfig& config, const QString& section)
{
    if (busy_ && !backgroundRefresh_) { report(QStringLiteral("配置操作被拒绝：已有事务正在执行。")); return; }
    if (!ready_) { report(QStringLiteral("请先连接并通过模块版本核验。")); return; }
    const bool wms=section=="all" || section=="wms";
    const bool dlia=section=="all" || section=="dlia";
    const bool acquisition=section=="all" || section=="acquisition";
    if (!wms && !dlia && !acquisition) { fail(QStringLiteral("未知参数分区。")); return; }
    const quint32 adcRate=acquisition?config.adcRateHz:registers_.value(0x4014);
    if ((acquisition && (adcRate<1000 || adcRate>1000000)) ||
        (dlia && (config.dlia[0].outputRateHz==0 || config.dlia[1].outputRateHz==0 ||
        config.dlia[0].outputRateHz>adcRate/8 || config.dlia[1].outputRateHz>adcRate/8))) {
        fail(QStringLiteral("采样率或 DLIA 输出率超出当前双 AD4630 接口范围。")); return;
    }
    if (acquisition && !dlia && (registers_.value(0x504c)>adcRate/8 || registers_.value(0x514c)>adcRate/8)) {
        fail(QStringLiteral("请先降低 DLIA 输出率，再降低 ADC 采样率。")); return;
    }
    if (acquisition && (config.ai8.slaveAddress<1 || config.ai8.slaveAddress>80 || config.ai8.channel<1 || config.ai8.channel>8
        || config.ai8.pollIntervalMs<1 || config.ai8.pollIntervalMs>3600000
        || config.ai8.timeoutMs<1 || config.ai8.timeoutMs>65535 || config.ai8.retryLimit>3)) {
        fail(QStringLiteral("AI8 地址、通道、轮询、超时或重试参数超出协议范围。")); return;
    }
    for (int ch=0; ch<2; ++ch) {
        const auto& w=config.wms[ch]; const auto& d=config.dlia[ch]; const auto& dac=config.dac[ch];
        const auto& f=config.frontend[ch];
        if ((wms && (!w.scanMilliHz || !w.sineMilliHz || !w.updateRateHz || w.phaseMode>2
            || dac.deviceCtrl>0x3ff || dac.clearCode>0xfffff
            || dac.minCode>dac.maxCode || dac.maxCode>0xfffff || !dac.spiClockHz || dac.spiClockHz>20000000))
            || (dlia && (!d.referenceMilliHz || d.mode>7 || !(d.mode&2) || d.fragmentPoints<1 || d.fragmentPoints>256))
            || (acquisition && (!std::isfinite(f.voltsPerCode) || f.voltsPerCode<=0 || (f.polarity!=1 && f.polarity!=-1)))) {
            fail(QStringLiteral("通道 %1 的波形、DAC、解调或前端标定参数不合法。").arg(ch)); return;
        }
    }
    if(acquisition && (config.adcCnvTicks<2 || config.adcCnvTicks>20 || config.adcBusyTimeoutTicks<40 || config.adcBusyTimeoutTicks>1000000)) {
        fail(QStringLiteral("ADC CNV 脉宽或 BUSY 超时超出范围。"));return;
    }
    if (!begin(QStringLiteral("提交并核对配置"))) return;
    configuration_=config; stopAndDrain();
    const bool ai8Changed=acquisition && (registers_.value(0x6614)!=config.ai8.slaveAddress ||
        registers_.value(0x661c)!=config.ai8.channel || registers_.value(0x662c)!=config.ai8.pollIntervalMs
        || registers_.value(0x6660)!=config.ai8.timeoutMs || registers_.value(0x6664)!=config.ai8.retryLimit);
    if (ai8Changed && (registers_.value(0x6604)&1)) {
        fail(QStringLiteral("AI8 地址、通道或轮询配置只能在禁用时修改；请先禁用 AI8。")); return;
    }
    if (wms) for (int ch=0; ch<2; ++ch) {
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
    if (acquisition) {
        write(0x4010,config.adcRateHz); verify(0x4010,config.adcRateHz);
        for(int ch=0;ch<2;++ch) {const quint32 a=0x401c+ch*0x100;write(a,config.adcExpectedPoints[ch]);verify(a,config.adcExpectedPoints[ch]);}
        write(0x4030,config.adcCnvTicks);verify(0x4030,config.adcCnvTicks);
        write(0x4034,config.adcBusyTimeoutTicks);verify(0x4034,config.adcBusyTimeoutTicks);
        commit(0x20,0x4000); read(0x4014); read(0x4114);
    }
    if (dlia) for (int ch=0; ch<2; ++ch) {
        const auto &d=config.dlia[ch]; const quint32 b=0x5000+quint32(ch)*0x100;
        const quint32 vals[]={d.referenceMilliHz,d.phase1,d.phase2,d.outputRateHz,d.mode};
        for (quint32 i=0;i<5;++i) { write(b+0x10+i*4,vals[i]); verify(b+0x10+i*4,vals[i]); }
        write(b+0x40,d.fragmentPoints);verify(b+0x40,d.fragmentPoints);
        commit(quint16(0x30+ch),b); read(b+0x4c,2);
    }
    readAll(); advance();
}

void FpgaDeviceController::setAcquisition(bool enable)
{
    if (!ready_ || !begin(enable?QStringLiteral("启动采集"):QStringLiteral("停止并排空采集"))) return;
    if (enable) {
        trackConnection(0);
        if (!enableUpload()) return;
        // DLIA stop latches cycle_partial; enable alone cannot clear it. Reset
        // only after both ADC producers and DLIA consumers have drained. ADC0
        // reset also reinitializes the shared PHY, so both lanes must be idle.
        stopAndDrain();
        resetAcquisition();
        // WMS supplies cycle labels even with independent DLIA references.
        write(0x0004,registers_.value(4)|1u);
        for (quint32 b:{0x2000u,0x2100u}) { write(b+4,1); verify(b+4,1); }
        for (quint32 b:{0x5000u,0x5100u}) { write(b+4,1); waitFor(b+8,7,3); }
        for (quint32 b:{0x4000u,0x4100u}) { write(b+4,1); waitFor(b+8,5,1); }
    } else {
        pendingConnections_.remove(0);
        stopAndDrain();
        write(0x403c,0); write(0x413c,0); write(0x0050,0);
    }
    readAll();
    if(enable) {Step confirmed;confirmed.kind=Kind::ConfirmConnection;confirmed.source=0;steps_.push_back(confirmed);}
    advance();
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
    if (!ready_ || source<0x40 || source>0x46 || !begin(QStringLiteral("设置传感器状态"))) return;
    if(enable)trackConnection(source);
    if (source==0x43 && enable && (registers_.value(0x6304)&1)) {
        fail(QStringLiteral("修改 BMP 地址前请先禁用 BMP390。"));return;
    }
    if (!appendSensorEnable(source,enable)) return;
    readAll(); advance();
}

bool FpgaDeviceController::appendSensorEnable(quint16 source, bool enable)
{
    if (enable && !enableUpload()) return false;
    if (source==0x40 && enable && registers_.value(0x600c)) {
        // PTB firmware marks every new pressure sample invalid while any old
        // error is latched. Preserve evidence before acknowledging those bits;
        // new communication failures remain visible and invalidate new data.
        const quint32 errors=registers_.value(0x600c);
        snapshotConfiguration();
        report(QStringLiteral("重新启用 PTB210：清除已记录的历史锁存错误 %1，后续按新数据有效位判断。").arg(hex(errors)),LogLevel::Info);
        write(0x600c,errors);read(0x600c);
    }
    if (source==0x43 && enable) {
        const auto address=quint32(pendingSensorParameters_.value("address").toDouble(configuration_.sensorParameters.value(source).value("address").toDouble(0x77)));
        write(0x6310,address); verify(0x6310,address);
    }
    adapter_.resetRate(source);
    if (latestReadings_.contains(source)) latestReadings_[source].sampleRateHz=0.0;
    Step s; s.kind=Kind::Sensor; s.address=1; s.source=source; s.expected=enable?1:0; steps_.push_back(s);
    verify(0x6004+(source-0x40)*0x100,enable?1:0);
    if(enable) {Step confirmed;confirmed.kind=Kind::ConfirmConnection;confirmed.source=source;steps_.push_back(confirmed);}
    return true;
}

void FpgaDeviceController::connectSensor(const QJsonObject& request)
{
    if (busy_ && !backgroundRefresh_) {report(QStringLiteral("连接被拒绝：已有事务正在执行。"));return;}
    if (request.value("op")!="sensor" || !request.value("enabled").toBool() || !FpgaRemote::validateControl(request)) {
        fail(QStringLiteral("传感器连接参数或范围无效。"));return;
    }
    const auto source=quint16(request.value("source").toInt());
    if (!request.contains("baud") && !request.contains("poll_ms") && !request.contains("parameters") && configuration_.sensorParameters.value(source).isEmpty()) {
        setSensorEnabled(quint16(request.value("source").toInt()),true);return;
    }
    auto settings=request;settings["op"]="sensor_config";settings.remove("enabled");
    if(!settings.contains("parameters") && !configuration_.sensorParameters.value(source).isEmpty())settings["parameters"]=configuration_.sensorParameters.value(source);
    startSensorConfiguration(settings,true);
}

void FpgaDeviceController::configureSensor(const QJsonObject& request)
{
    startSensorConfiguration(request,false);
}

void FpgaDeviceController::startSensorConfiguration(const QJsonObject& request, bool connectAfter)
{
    if (busy_ && !backgroundRefresh_) {report(QStringLiteral("参数提交被拒绝：已有事务正在执行。"));return;}
    if (request.value("op").toString()!=QStringLiteral("sensor_config") || !FpgaRemote::validateControl(request)) {
        fail(QStringLiteral("传感器参数或范围无效。"));return;
    }
    if (!ready_ || !begin(QStringLiteral("配置传感器参数并读回"))) return;
    const auto source=quint16(request.value("source").toInt());
    pendingSensorParameters_=request.value("parameters").toObject();
    if(connectAfter)trackConnection(source);
    const quint32 base=0x6000+(source-0x40)*0x100;
    // Decide from a fresh hardware read, including state-dependent constraints.
    read(base+4,3);
    if(source==0x40)read(0x6010,4);
    if(source==0x44)read(0x6414,3);
    if(source==0x45)read(0x6510,2);
    Step prepare;prepare.kind=Kind::PrepareSensorConfig;prepare.source=source;
    prepare.values={quint32(request.value("baud").toInt()),quint32(request.value("poll_ms").toInt()),connectAfter?1u:0u};
    steps_.push_back(prepare);advance();
}

void FpgaDeviceController::prepareSensorConfiguration(quint16 source, quint32 baud, quint32 pollMs, bool connectAfter)
{
    const quint32 base=0x6000+(source-0x40)*0x100;
    const bool enabled=registers_.value(base+4)&1;
    if(source==0x40 && pollMs && quint32(pendingSensorParameters_.value("mode").toDouble(registers_.value(0x601c)))!=0) {
        fail(QStringLiteral("PTB 连续输出模式不使用轮询间隔；未修改参数。"));return;
    }
    const auto heater=quint32(pendingSensorParameters_.value("heater").toDouble(registers_.value(0x641c)));
    if(source==0x44 && heater && (pollMs?pollMs:registers_.value(0x6414))<(heater&1?12000u:1300u)) {
        fail(QStringLiteral("SHT45 当前加热模式要求更长的轮询间隔；未修改参数。"));return;
    }
    if(source==0x45 && quint32(pendingSensorParameters_.value("mode").toDouble(registers_.value(0x6514)))==2 && (baud?baud:registers_.value(0x6510))<500000) {
        fail(QStringLiteral("TFA 高频模式要求波特率不低于 500000；未修改参数。"));return;
    }
    snapshotConfiguration();
    const auto sensor=[&](bool on) {
        Step s;s.kind=Kind::Sensor;s.source=source;s.address=1;s.expected=on?1:0;steps_.push_back(s);
        verify(base+4,on?1:0);
    };
    if(enabled)sensor(false);
    // PTB BUSY remains set even in ST_DISABLED. Allow its current UART byte
    // and optional continuous-output stop CR to finish at the old baud.
    if(source==0x40) {Step delay;delay.kind=Kind::Delay;delay.expected=50;steps_.push_back(delay);}
    else waitFor(base+8,source==0x45?13u:source==0x42?1u:9u,0);
    const auto parameter=[&](quint32 address,quint32 value) {
        Step s;s.kind=Kind::Write;s.address=address;s.values={value};s.label=QStringLiteral("sensor_parameter");steps_.push_back(s);
        verify(address,value);
    };
    const bool modeBeforeBaud=source==0x45 && pendingSensorParameters_.contains("mode") && pendingSensorParameters_.value("mode").toInt()!=2;
    if(modeBeforeBaud)parameter(0x6514,quint32(pendingSensorParameters_.value("mode").toInt()));
    if(baud)parameter(base+0x10,baud);
    if(pollMs)parameter(base+(source==0x40?0x18:source==0x41?0x1c:source==0x46?0x2c:0x14),pollMs);
    for(const auto& spec:FpgaRemote::sensorParameters())if(spec.source==source && pendingSensorParameters_.contains(spec.key) && !(modeBeforeBaud && QString::fromLatin1(spec.key)=="mode"))parameter(spec.address,quint32(pendingSensorParameters_.value(spec.key).toDouble()));
    if(source==0x43 && !pendingSensorParameters_.isEmpty())commit(source,base);
    if(source==0x45)waitFor(base+8,13,0);
    if(connectAfter) {
        if(!appendSensorEnable(source,true))return;
    } else if(enabled) {
        sensor(true);
        if(source==0x45)waitFor(base+8,13,1);
    }
    adapter_.resetRate(source);
    if(latestReadings_.contains(source))latestReadings_[source].sampleRateHz=0;
    readAll();
    Step finish;finish.kind=Kind::FinishSensorConfig;finish.source=source;steps_.push_back(finish);
}

void FpgaDeviceController::executeOperation(const QJsonObject& request)
{
    if(busy_ && !backgroundRefresh_) {report(QStringLiteral("FPGA 操作被拒绝：已有事务正在执行。"));return;}
    if(!FpgaRemote::validateControl(request)) {fail(QStringLiteral("FPGA 操作参数无效。"));return;}
    if(!ready_ || !begin(QStringLiteral("FPGA 高级控制与读回")))return;
    snapshotConfiguration();
    const auto op=request.value("op").toString();
    const quint32 action=quint32(request.value("action").toDouble()),value=quint32(qint64(request.value("value").toDouble()));
    const quint16 source=quint16(request.value("source").toInt());
    auto command=[&](quint16 message,quint16 target,QVector<quint32> words){Step s;s.kind=Kind::Management;s.address=message;s.source=target;s.values=words;steps_.push_back(s);};
    if(op=="time")command(13,2,{action,value});
    else if(op=="time_tag") {
        for(auto pair:{qMakePair(0x1038u,quint32(request.value("low").toDouble())),qMakePair(0x103cu,quint32(request.value("high").toDouble()))}){write(pair.first,pair.second);verify(pair.first,pair.second);}
    } else if(op=="motor")command(12,0x47,{action,value});
    else if(op=="motor_config") {
        waitFor(0x6708,8,0);write(0x6704,0);verify(0x6704,0);
        for(auto pair:{qMakePair(0x6714u,quint32(request.value("frequency").toDouble())),qMakePair(0x6718u,quint32(request.value("high_ticks").toDouble())),qMakePair(0x671cu,quint32(request.value("dir_ticks").toDouble())),qMakePair(0x6730u,quint32(request.value("polarity").toDouble()))}) {write(pair.first,pair.second);verify(pair.first,pair.second);}
    } else if(op=="channel_acquisition") {
        const int channel=request.value("channel").toInt();const quint32 mask=1u<<(16+channel);
        if(request.value("enabled").toBool() && !enableUpload())return;
        command(request.value("enabled").toBool()?6:7,1,{0,mask,0,0});
        if(!request.value("enabled").toBool())drainWaveforms_=true;
    } else if(op=="sensor_action") {
        if(source==0x41 && action==16) {
            Step baseline;baseline.kind=Kind::Read;baseline.address=0x6148;baseline.label=QStringLiteral("hmp_baseline");steps_.push_back(baseline);
        }
        Step s;s.kind=Kind::Sensor;s.address=action;s.source=source;s.expected=value;steps_.push_back(s);
        if(source==0x41 && action==16) {waitFor(0x6148,0xffffffff,0);steps_.back().label=QStringLiteral("hmp_confirmation");verify(0x6140,value);}
    } else if(op=="maintenance") {
        if(action==8 || action==9) {const quint64 mask=quint64(1)<<(source&63);command(quint16(action),source,{quint32(mask),quint32(mask>>32)});}
    } else if(op=="stream_config") {
        for(auto pair:{qMakePair(0x7010u,quint32(request.value("mode").toDouble())),qMakePair(0x7018u,quint32(request.value("high_water").toDouble())),qMakePair(0x7034u,quint32(request.value("burst").toDouble()))}) {write(pair.first,pair.second);verify(pair.first,pair.second);}
    } else if(op=="watchdog") {
        write(0x0040,value);verify(0x0040,value);write(0x0044,1);
        Step finish;finish.kind=Kind::FinishWatchdog;finish.expected=value;steps_.push_back(finish);
    }
    readAll();advance();
}

void FpgaDeviceController::setRawEnabled(bool enable)
{
    if (!ready_) {
        reportRawResult(enable,false,QStringLiteral("FPGA 未就绪。"));
        return;
    }
    if (busy_ && !backgroundRefresh_) {
        if (!enable) QTimer::singleShot(100,this,[this]{ setRawEnabled(false); });
        else reportRawResult(true,false,QStringLiteral("当前有其它 FPGA 操作正在执行。"));
        return;
    }
    if (!begin(enable?QStringLiteral("启动 RAW 上传"):QStringLiteral("停止 RAW 上传"))) {
        reportRawResult(enable,false,QStringLiteral("当前无法执行 FPGA 操作。"));
        return;
    }
    rawOperation_=enable;
    if(enable) {write(0x0050,3);verify(0x0050,3);}
    for (quint32 b:{0x4000u,0x4100u}) { write(b+0x3c,enable?1:0); verify(b+0x3c,enable?1:0); }
    if(!enable) {
        // Stop RAW producers first. Masking the framer finalizes its partial
        // tail; only then can its buffered-sample count reach zero.
        write(0x0050,0);verify(0x0050,0);
        for(quint32 a:{0x7020u,0x7024u})waitFor(a,0xffffffff,0);
    }
    readAll();if(!enable)steps_.push_back({Kind::FinishRawStop});advance();
}

void FpgaDeviceController::setTemperature(double celsius)
{
    if (busy_ && !backgroundRefresh_) { report(QStringLiteral("设温被拒绝：已有事务正在执行。")); return; }
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
    if(reading.source==0x42 && reading.epsilonMessageId==Ppk::kMsgRawSatellite) {
        Ppk::RawSatellitePacket packet;std::string reason;
        if(!reading.validity.structure || !reading.validity.crc || (reading.flags&0x18) ||
            !Ppk::decodeRawSatellitePayload(reinterpret_cast<const std::uint8_t*>(reading.epsilonData.constData()),
                size_t(reading.epsilonData.size()),packet,reason)) {
            observations_.flush();
            LogRecord record;record.source=QStringLiteral("Ground");record.category=QStringLiteral("device.fpga");record.level=LogLevel::Warning;
            record.message=QStringLiteral("EPSILON 原始观测包无效：")+(reason.empty()?QStringLiteral("结构、CRC 或连续性校验失败。"):QString::fromStdString(reason));
            record.fields={{"event","fpga_firmware_event"},{"observation_event","invalid_packet"}};emit logRecordGenerated(record);return;
        }
        packet.epoch.hostTimestampUs=nowUs();
        if(auto epoch=observations_.accept(std::move(packet)))
            if(!replaying_)emit observationEpochReceived(Ppk::encodeEpoch(*epoch));
        return; // Observations must not replace coalesced navigation state.
    }
    latestReadings_[reading.source]=adapter_.accept(reading);
    lastReadingMs_[reading.source]=QDateTime::currentMSecsSinceEpoch();
    dirtyReadings_.insert(reading.source); expiredReadings_.remove(reading.source);
}

void FpgaDeviceController::flushPresentation()
{
    checkStartupWarnings();
    checkConnectionResults();
    const qint64 now=QDateTime::currentMSecsSinceEpoch();
    for (auto i=latestReadings_.begin(); i!=latestReadings_.end(); ++i) {
        const bool expired=!replaying_ && now-lastReadingMs_.value(i.key())>5000;
        if (!dirtyReadings_.contains(i.key()) && (!expired || expiredReadings_.contains(i.key()))) continue;
        auto value=i.value();
        if (expired) {
            value.sampleRateHz=0.0;
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
