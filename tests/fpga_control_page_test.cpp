#include "test_settings_sandbox.h"
#include "FpgaDeviceController.h"
#include "FpgaVlp1.h"
#include "FpgaHarmonics.h"
#include <QtEndian>
#include <QElapsedTimer>
#include <QThread>
#include "ground/widgets/FpgaControlPage.h"
#include "shared/theme/AppTheme.h"
#include <QApplication>
#include <QBoxLayout>
#include <QStyleFactory>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFrame>
#include <QGridLayout>
#include <QJsonArray>
#include <QLabel>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSemaphore>
#include <QPlainTextEdit>
#include <QTabWidget>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <limits>

namespace {
void require(bool value, const char *message) {
    if (!value) { std::fprintf(stderr,"FAIL: %s\n",message); std::exit(1); }
}
void motorQueuedCancellation()
{
    FpgaControlPage page;page.setUiTestState(true);
    const auto wait=[](const std::function<bool()>& done,const char* message) {
        QElapsedTimer timer;timer.start();
        while(!done() && timer.elapsed()<4000) {QCoreApplication::processEvents();QThread::msleep(1);}
        require(done(),message);
    };
    page.simulateOperation({{"op","connect"}});
    wait([&]{return page.uiTestReady()&&!page.uiTestBusy();},"queued cancellation fixture connects its isolated protocol simulator");
    auto* controller=page.uiTestController();auto* progress=page.findChild<QLabel*>("fpgaMotorProgress");
    int movesSent=0;
    QObject::connect(controller,&VaporView::Ground::Devices::FpgaDeviceController::rawCommand,&page,
        [&](quint64,const QByteArray& bytes) {
            VaporView::FpgaVlp1::StreamParser parser;
            for(const auto& frame:parser.feed(reinterpret_cast<const std::uint8_t*>(bytes.constData()),size_t(bytes.size())))
                if(frame.header.message==12 && frame.header.source==0x47 && frame.payload.size()>=4
                    && qFromLittleEndian<quint32>(frame.payload.data())==1)++movesSent;
        },Qt::QueuedConnection);
    const QJsonObject move{{"op","motor"},{"action",1},{"value",40}};
    const QJsonObject stop{{"op","motor"},{"action",2},{"value",0}};
    for(bool cancelWithStop:{false,true}) {
        QSemaphore entered,released;
        QMetaObject::invokeMethod(controller,[&] {entered.release();released.acquire();},Qt::QueuedConnection);
        require(entered.tryAcquire(1,1000),"simulated USB worker is held before the unsent MOVE");
        page.simulateOperation(move);
        require(progress->text().contains(QStringLiteral("等待指令结果")),"queued MOVE enters the pending UI state");
        if(cancelWithStop)page.simulateOperation(stop);
        else page.cancelPendingUiTestOperations();
        released.release();
        if(cancelWithStop) {
            wait([&]{return !page.uiTestBusy()&&progress->text().contains(QStringLiteral("已停止"));},
                "new STOP finishes without an older cancelled MOVE overwriting its result");
        } else {
            wait([&]{return progress->text().contains(QStringLiteral("执行结果未确认"));},
                "cancelled unsent MOVE receives a terminal result instead of remaining pending forever");
            page.simulateOperation(stop);
            wait([&]{return !page.uiTestBusy()&&progress->text().contains(QStringLiteral("已停止"));},
                "explicit STOP restores controls after a cancelled pending MOVE");
        }
        require(movesSent==0,"cancelled unsent MOVEs never reach the FPGA transport");
    }
    page.setUiTestState(false);
}
void motorMotionGuard() {
    FpgaControlPage page;page.setConnectionState(true,false);
    QMap<quint32,quint32> values{{0x6700,0x00470200},{0x6708,0x63},{0x673c,0x21},{0x6734,10},{0x6714,100},{0x6728,0},{0x670c,0}};
    page.setHardwareValues(values);
    auto *run=page.findChild<QPushButton*>("fpgaMotorRun"),*stop=page.findChild<QPushButton*>("fpgaMotorStop"),*disable=page.findChild<QPushButton*>("fpgaMotorDisable");
    auto *steps=page.findChild<QDoubleSpinBox*>("fpgaMotorSteps"),*frequency=page.findChild<QDoubleSpinBox*>("fpgaMotorFrequency");
    auto *progress=page.findChild<QLabel*>("fpgaMotorProgress");
    QVector<QJsonObject> requests;int heartbeats=0;
    QObject::connect(&page,&FpgaControlPage::operationRequested,&page,[&](const QJsonObject& request){requests.append(request);});
    QObject::connect(&page,&FpgaControlPage::motorHeartbeatRequested,&page,[&]{++heartbeats;});
    const auto processFor=[](int ms) {QElapsedTimer clock;clock.start();while(clock.elapsed()<ms){QCoreApplication::processEvents();QThread::msleep(1);}};
    const auto event=[](quint32 id,quint64 tick) {return QJsonObject{{"1300",QJsonObject{{"message",0x1300},{"source",0x47},{"tick",QString::number(tick)},{"words",QJsonArray{double(id),0,0,0,0,0,0}}}}};};
    const auto idle=[&](quint32 id) {values[0x6734]=id;values[0x6708]=0x63;values[0x673c]=0x21;values[0x6728]=0;page.setHardwareValues(values);};
    const auto moving=[&](quint32 id) {values[0x6734]=id;values[0x6708]=0x69;values[0x673c]=0x23;values[0x6728]=50;page.setHardwareValues(values);};
    page.setManagementState(event(10,100));frequency->setValue(20);steps->setValue(100);
    page.findChild<QPushButton*>("fpgaMotorEnable")->click();page.setConnectionState(true,false);
    run->click();run->click();const auto first=requests.last();
    require(requests.size()==2&&progress->text().contains("1.0 s"),"MOVE locks immediately and estimates from actual frequency rather than an unapplied edit");
    page.setConnectionState(true,false);idle(10);page.setManagementState(event(10,100));
    require(!run->isEnabled()&&!steps->isEnabled()&&!frequency->isEnabled(),"command completion and cached idle/event cannot unlock an unconfirmed move");
    for(const char *name:{"fpgaMotorRun","fpgaMotorEnable","fpgaMotorConfigure","fpgaMotorZero"})page.findChild<QPushButton*>(QString::fromLatin1(name))->click();
    require(requests.size()==2&&stop->isEnabled()&&disable->isEnabled(),"movement blocks repeated motor commands while keeping both safety actions available");
    auto *module=page.findChild<QComboBox*>("fpgaMaintenanceModule");module->setCurrentIndex(module->findData(0x47));
    page.findChild<QPushButton*>("fpgaModuleReset")->click();
    require(requests.size()==2&&!page.findChild<QPushButton*>("fpgaModuleClearErrors")->isEnabled(),"maintenance cannot bypass the motor movement guard");
    module->setCurrentIndex(module->findData(0x40));
    require(page.findChild<QPushButton*>("fpgaModuleReset")->isEnabled(),"movement does not lock maintenance for other modules");
    page.finishMotorOperation(first,true);const auto initialProgress=progress->text();const int beforeHeartbeats=heartbeats;
    processFor(350);
    require(progress->text()!=initialProgress&&requests.size()==2&&heartbeats>beforeHeartbeats,"local estimated progress advances without querying or sending new operations, while heartbeats continue");
    page.setLanguage(true);require(progress->text().contains("Estimated")&&!run->isEnabled(),"language change preserves the active movement and translates its estimate");
    moving(11);processFor(850);
    require(progress->text().contains("waiting for completion")&&!run->isEnabled(),"zero estimated time cannot confirm completion or permit a second move");
    idle(10);require(!run->isEnabled(),"a stale pre-move idle snapshot cannot complete the current motion");
    idle(11);require(run->isEnabled()&&progress->text().contains("Device confirmed"),"matching new motion id and idle readback confirm the move");

    steps->setValue(-100);run->click();const auto rejected=requests.last();page.setConnectionState(true,false);
    page.finishMotorOperation({{"op","motor"},{"action",3},{"value",0}},true);
    require(!run->isEnabled(),"an unrelated operation result cannot finish the pending move");
    page.finishMotorOperation(rejected,false);page.setLanguage(false);page.setLanguage(true);
    require(progress->text().contains("unconfirmed")&&!run->isEnabled(),"failure and language rerender retain the uncertain execution guard");
    idle(11);require(!run->isEnabled()&&progress->text().contains("unconfirmed"),"cached idle with the old motion id cannot release an uncertain failed command");
    stop->click();page.finishMotorOperation(requests.last(),true);page.setConnectionState(true,false);
    require(run->isEnabled()&&progress->text().contains("Stopped"),"explicit confirmed STOP restores control after an uncertain failed MOVE");
    run->click();const auto shortMove=requests.last();page.setConnectionState(true,false);
    page.setManagementState(event(10,100));require(!run->isEnabled(),"cached old completion cannot finish a newer move");
    page.setManagementState(event(12,300));require(!run->isEnabled(),"a short move completion arriving before its command result remains pending");
    page.finishMotorOperation(shortMove,true);require(run->isEnabled(),"matching fresh completion evidence is retained until command success");

    idle(12);steps->setValue(1000);run->click();page.finishMotorOperation(requests.last(),true);page.setConnectionState(true,false);moving(13);
    stop->click();const auto failedStop=requests.last();page.finishMotorOperation(failedStop,false);page.setConnectionState(true,false);
    require(!run->isEnabled()&&progress->text().contains("unconfirmed"),"failed STOP does not claim motion has completed");
    stop->click();page.finishMotorOperation(requests.last(),true);page.setConnectionState(true,false);
    require(progress->text().contains("moving")&&!run->isEnabled(),"a confirmed STOP cannot override hardware that still reports busy");
    idle(13);require(progress->text().contains("Stopped")&&run->isEnabled(),"STOP is distinct from pulse completion and releases the movement guard after idle");
    const int holdingHeartbeats=heartbeats;processFor(250);require(heartbeats>holdingHeartbeats,"STOP retains the existing owner's holding heartbeat");
    run->click();page.finishMotorOperation(requests.last(),true);page.setConnectionState(true,false);
    disable->click();page.finishMotorOperation(requests.last(),true);page.setConnectionState(true,false);
    const int disabledHeartbeats=heartbeats;processFor(250);
    require(progress->text().contains("disabled")&&heartbeats==disabledHeartbeats,"Disable clears the estimate and ends heartbeat ownership");
    run->click();page.setConnectionState(false,false,"Not ready");
    require(progress->text().contains("unconfirmed")&&!run->isEnabled(),"loss of readiness stops the estimate without asserting a motion end");
    page.setTransportConnected(false);page.setConnectionState(true,false);idle(13);
    require(run->isEnabled()&&!progress->text().contains("Estimated"),"disconnect clears the old local estimate before a fresh connection");
    moving(14);require(progress->text().contains("progress is unknown")&&!run->isEnabled(),"pre-existing device motion is locked and never given an invented estimate");idle(14);
}
}
int main(int argc,char **argv) {
    QApplication app(argc,argv);
#ifdef Q_OS_WIN
    auto *windowsStyle=QStyleFactory::create("windows11");
    require(windowsStyle,"Windows 11 style is available for font regression");
    QApplication::setStyle(windowsStyle);
#endif
    VaporView::installButtonFocusStyle();
    VaporView::setAppStyleSheet(VaporView::startupAppThemeStyleSheet(false));
    motorQueuedCancellation();
    motorMotionGuard();
    {
        FpgaControlPage page;
        page.setConnectionState(true,false);
        auto *sos=page.findChild<QPushButton*>("fpga0SosApply");
        auto *pps=page.findChild<QPushButton*>("fpga0TriggerApply");
        require(sos && pps && !sos->isEnabled() && !pps->isEnabled(),"new firmware controls require API readback");
        page.setHardwareValues({{0x50f0,0x534f5301},{0x2084,0x50505301},{0x5024,0x80000003},{0x5064,3},{0x50ec,1250}});
        require(sos->isEnabled() && pps->isEnabled(),"SOS and PPS enabled only after API probe");
        require(page.findChild<QLabel*>("fpga0SosStatus")->text().contains("1250"),"active SOS cutoff displayed separately from builtin");
        QJsonObject requested;
        const auto observer=QObject::connect(&page,&FpgaControlPage::operationRequested,&page,[&](const QJsonObject& request){requested=request;});
        page.findChild<QDoubleSpinBox*>("fpga0SosOrder")->setValue(7);
        page.findChild<QDoubleSpinBox*>("fpga0SosHz")->setValue(1250);sos->click();
        require(requested.value("op")=="dlia_sos" && requested.value("order").toInt()==7 && requested.value("hz").toInt()==1250,"UI sends selected filter design");
        QObject::disconnect(observer);page.setHardwareValues({});page.setConnectionState(false,false);
    }
    FpgaControlPage page;
    auto *requiredHarmonics=page.findChild<QCheckBox*>("fpgaRecordHarmonics");
    require(requiredHarmonics && requiredHarmonics->isChecked() && !requiredHarmonics->isEnabled(),"H1/H2 recording is required");
    auto legacyRecordingOptions=page.configuration();legacyRecordingOptions.recordRaw=true;legacyRecordingOptions.recordDlia=false;
    page.setConfiguration(legacyRecordingOptions);
    require(page.configuration().recordDlia && !page.configuration().recordRaw,"legacy options cannot disable H1/H2 or enable raw archive writes");
    auto *devices=page.findChild<QComboBox*>("fpgaLocator");
    auto *scan=page.findChild<QPushButton*>("fpgaDiscover");
    auto *connect=page.findChild<QPushButton*>("fpgaConnect");
    auto *disconnect=page.findChild<QPushButton*>("fpgaDisconnect");
    auto *backend=page.findChild<QComboBox*>("fpgaBackend");
    auto *discoveryStatus=page.findChild<QLabel*>("fpgaDiscoveryStatus");
    auto *connectionStatus=page.findChild<QLabel*>("fpgaConnectionStatus");
    const QJsonObject first{{"locator","board-a"},{"label","GP01 A"},{"driver","WinUSB"},{"backend","winusb"},{"available",true}};
    auto second=first;second["locator"]="board-b";second["label"]="GP01 B";
    page.setUsbDiscoveryResult({{"devices",QJsonArray{}},{"error",""}});
    require(!connect->isEnabled(),"no USB devices cannot connect");
    page.setUsbDiscoveryResult({{"devices",QJsonArray{first,second}},{"error",""}});
    require(devices->count()==3 && devices->currentIndex()==0 && !connect->isEnabled(),"multiple boards require an explicit selection");
    devices->setCurrentIndex(2);require(connect->isEnabled(),"enumerated board can be selected");
    page.setUsbDiscoveryResult({{"devices",QJsonArray{second,first}},{"error",""}});
    require(devices->currentData().toString()=="board-b","refresh preserves device by path rather than index");
    QString selected;
    QObject::connect(&page,&FpgaControlPage::connectRequested,&page,[&](const QString &path,const QString&){selected=path;});
    connect->click();require(selected=="board-b","connect uses hidden enumerated path");page.setConnectionState(false,false);
    auto unavailable=first;unavailable["available"]=false;
    page.setUsbDiscoveryResult({{"devices",QJsonArray{unavailable}},{"error",""}});
    require(!connect->isEnabled(),"detected device without driver backend cannot claim connectability");
    page.setUsbDiscoveryResult({{"devices",QJsonArray{first}},{"error",""}});
    require(devices->currentData().toString()=="board-a" && connect->isEnabled(),"single device selected automatically");
    auto backendConfig=page.configuration();backendConfig.backend="cypress";page.setConfiguration(backendConfig);
    require(!connect->isEnabled(),"saved backend incompatible with selected driver cannot connect");
    backendConfig.backend="auto";page.setConfiguration(backendConfig);require(connect->isEnabled(),"auto backend restores selected device connection");
    int discoveries=0;QObject::connect(&page,&FpgaControlPage::discoverDevicesRequested,&page,[&]{++discoveries;});
    scan->click();scan->click();require(discoveries==1 && !connect->isEnabled(),"pending enumeration blocks duplicates and connection");
    page.setUsbDiscoveryResult({{"devices",QJsonArray{first}},{"error",""}});
    page.setUsbDiscoveryHost(true,false);require(devices->count()==1&&!scan->isEnabled()&&!connect->isEnabled(),"switch to disconnected Sky clears Local candidates");
    page.setUsbDiscoveryHost(false);page.setUsbDiscoveryResult({{"devices",QJsonArray{first}},{"error",""}});
    auto c=page.configuration();
    require(c.pressureSource==0x40 && c.backend=="auto","safe PTB210 / auto defaults");
    require(c.adcRateHz==1000000 && c.dlia[0].outputRateHz==10000 && c.dlia[1].mode==3,"shared ADC / DLIA defaults");
    require(c.wms[0].scanMilliHz==100000 && c.wms[1].scanAmplitude==107374182,"WMS units and defaults");
    require(c.dac[0].deviceCtrl==0x312 && c.dac[1].spiClockHz==20000000 && c.ai8.slaveAddress==1 && c.ai8.channel==1,"DAC / AI8 safe defaults");
    require(!c.ai8.scan && !page.findChild<QCheckBox*>("fpgaAi8Scan")->isChecked(),"AI8 defaults to the firmware's selected single channel");
    auto oldAi8=c.toJson();auto ai8=oldAi8.value("ai8").toObject();ai8.remove("scan");oldAi8["ai8"]=ai8;
    require(!FpgaControlConfig::fromJson(oldAi8).ai8.scan,"configuration without a scan preference defaults to the selected channel");
    ai8["scan"]=false;oldAi8["ai8"]=ai8;
    require(!FpgaControlConfig::fromJson(oldAi8).ai8.scan,"explicit single-channel preference remains supported");
    auto oldProbe=c.toJson();oldProbe["sensorParameters"]=QJsonObject{{"65",QJsonObject{{"address",240},{"pressure",1013.25}}}};
    const auto migratedProbe=FpgaControlConfig::fromJson(oldProbe).sensorParameters.value(0x41);
    require(migratedProbe.value("address")==99 && !migratedProbe.contains("pressure"),"obsolete humidity address and pressure presets cannot reach HC2A");
    require(c.recordSensors && c.recordDlia && !c.recordRaw,"RAW recording off / sensor and DLIA recording");
    require(!page.findChild<QDoubleSpinBox*>("fpgaRawSeconds")&&!c.toJson().contains("rawWindowSeconds"),"RAW has no duration setting or persisted timer policy");
    auto legacy=c.toJson();legacy.insert("rawWindowSeconds",1);
    require(FpgaControlConfig::fromJson(legacy).toJson()==c.toJson(),"legacy RAW duration is ignored when restoring configuration");
    require(!c.frontend[0].calibrated && !c.frontend[1].calibrated,"unconfirmed frontend must retain raw codes");
    c.wms[1].phase=0xffffffffU;c.frontend[1].polarity=-1;c.frontend[1].calibrated=true;c.backend="winusb";
    const auto restored=FpgaControlConfig::fromJson(c.toJson());
    require(restored.wms[1].phase==0xffffffffU && restored.frontend[1].polarity==-1 && restored.backend=="winusb","configuration JSON retains unsigned phase and calibration");
    page.setConfiguration(c);require(page.configuration().wms[1].phase==0xffffffffU,"editor retains whole-turn unsigned phase");
    auto *apply=page.findChild<QPushButton*>("fpgaApply");
    require(apply&&!apply->isEnabled(),"offline cannot control hardware");
    auto *sensor=page.findChild<QLabel*>("fpgaSensor0040");
    require(sensor && !sensor->text().contains("Pa"),"no invented online pressure");
    page.setTransportConnected(true);page.setConnectionState(false,false,"Version mismatch");
    require(page.findChild<QPushButton*>("fpgaRefresh")->isEnabled()&&page.findChild<QPushButton*>("fpgaDisconnect")->isEnabled()&&!apply->isEnabled(),"incompatible connected hardware retains readback and disconnect while writes stay forbidden");
    page.setTransportConnected(false);require(page.findChild<QPushButton*>("fpgaConnect")->isEnabled(),"closed transport can reconnect");
    auto *bmpAddress=page.findChild<QDoubleSpinBox*>("fpgaSensorParameter6310");require(bmpAddress,"advanced sensor address editor exists");
    page.setConnectionState(true,false);page.setHardwareValues({{0x6304,1},{0x6310,119}});require(!bmpAddress->isEnabled(),"connected sensor parameters locked");
    page.setHardwareValues({{0x6304,0},{0x6310,119}});require(bmpAddress->isEnabled(),"disconnected sensor parameters editable");bmpAddress->setValue(118);require(page.configuration().sensorParameters.value(0x43).value("address")==118,"advanced edit stages desired settings");
    page.setHardwareValues({{0x6304,0},{0x6310,119}});require(bmpAddress->value()==118,"readback preserves staged settings");
    QJsonObject advancedRequest;QObject::connect(&page,&FpgaControlPage::operationRequested,&page,[&](const QJsonObject& request){advancedRequest=request;});
    page.findChild<QPushButton*>("fpgaTimeRearm")->click();require(advancedRequest.value("op")=="time" && advancedRequest.value("action")==2,"time control emits typed operation");page.setConnectionState(true,false);
    require(!page.findChild<QPushButton*>("fpgaTimeTag")&&!page.findChild<QPushButton*>("fpgaHc2aSetPressure"),"UTC tag writes and historical pressure compensation have no control entry");
    auto* hc2aAddress=page.findChild<QDoubleSpinBox*>("fpgaSensorParameter6114");
    require(hc2aAddress && hc2aAddress->minimum()==0 && hc2aAddress->maximum()==99,"HC2A address editor includes discovery and address zero");
    page.setHardwareValues({{0x1060,5},{0x104c,7},{0x1050,123},{0x1058,123456789},{0x6800,0x00480100},{0x6900,0x00490100}});
    require(page.findChild<QLabel*>("fpgaPpsCalibrationStatus")->text().contains(QStringLiteral("未确认"))
        && !page.findChild<QLabel*>("fpgaPpsCalibrationStatus")->text().contains("123456789"),"qualified PPS without UTC anchor never displays an absolute time");
    auto* lcStatus=page.findChild<QLabel*>("fpgaLcStatus");
    require(lcStatus && lcStatus->text().contains(QStringLiteral("LC0 模块匹配：1"))
        && lcStatus->text().contains(QStringLiteral("LC1 模块匹配：1"))
        && lcStatus->text().contains(QStringLiteral("TEC103"))
        && lcStatus->text().contains(QStringLiteral("CON11 5/7"))
        && lcStatus->text().contains(QStringLiteral("6/8"))
        && lcStatus->text().contains(QStringLiteral("38400 / 8N1"))
        && lcStatus->text().contains(QStringLiteral("内部 TC1"))
        && lcStatus->text().contains(QStringLiteral("激光温控页")),
        "LC status maps both real UART bridge IDs to the TEC103 wiring, protocol and temperature page");
    page.setHardwareValues({{0x6800,0x00480100},{0x6900,0}});
    require(lcStatus->text().contains(QStringLiteral("LC0 模块匹配：1"))
        && lcStatus->text().contains(QStringLiteral("LC1 模块匹配：0")),
        "LC status reports each bridge ID independently without inventing instrument identity");
    require(!page.property("tec103Connected").toBool(),
        "matching raw UART bridge IDs alone never confirm a TEC103 instrument connection");
    auto* motorFrequency=page.findChild<QDoubleSpinBox*>("fpgaMotorFrequency");auto* motorSteps=page.findChild<QDoubleSpinBox*>("fpgaMotorSteps");
    require(motorFrequency->minimum()==20 && motorFrequency->maximum()==400 && motorSteps->minimum()==-5000 && motorSteps->maximum()==5000
        && motorSteps->value()==40 && motorSteps->decimals()==0
        && !page.findChild<QDoubleSpinBox*>("fpgaMotorHigh") && !page.findChild<QDoubleSpinBox*>("fpgaMotorPolarity"),"DM422 exposes bounded motion and fixed timing/polarity");
    for(int pulses:{-5000,5000}) {
        const quint32 motionId=pulses<0?0:1;
        page.setHardwareValues({{0x6734,motionId},{0x6714,100},{0x6708,2},{0x673c,0}});
        motorSteps->setValue(pulses);page.findChild<QPushButton*>("fpgaMotorRun")->click();
        require(advancedRequest.value("op")=="motor" && advancedRequest.value("action")==1 && advancedRequest.value("value").toInt()==pulses,
            "DM422 run submits both signed 5000-pulse bounds without clamping");
        page.finishMotorOperation(advancedRequest,true);page.setHardwareValues({{0x6734,motionId+1},{0x6708,2},{0x673c,0}});page.setConnectionState(true,false);
    }
    motorSteps->setValue(40);
    page.setHardwareValues({{0x6708,0x61},{0x673c,8}});
    require(page.findChild<QLabel*>("fpgaMotorSafetyStatus")->text() == QStringLiteral("请求使能 1 · 实际使能 1 · 心跳存活 1 · 故障 1"),
        "motor status reports all four live safety fields");
    page.setHardwareValues({{0x6708,2},{0x673c,0}});
    int heartbeats=0;QObject::connect(&page,&FpgaControlPage::motorHeartbeatRequested,&page,[&]{++heartbeats;});
    page.findChild<QPushButton*>("fpgaMotorEnable")->click();page.setConnectionState(true,true);
    QElapsedTimer leaseTime;leaseTime.start();while(leaseTime.elapsed()<250){QCoreApplication::processEvents();QThread::msleep(1);}
    require(heartbeats>0 && page.findChild<QPushButton*>("fpgaMotorStop")->isEnabled(),"live host lease continues while ordinary operations are busy and STOP remains enabled");
    page.findChild<QPushButton*>("fpgaMotorStop")->click();const int stoppedHeartbeats=heartbeats;
    leaseTime.restart();while(leaseTime.elapsed()<250){QCoreApplication::processEvents();QThread::msleep(1);}
    require(heartbeats>stoppedHeartbeats && advancedRequest.value("action")==2,"normal STOP keeps the owner's holding heartbeat");
    page.findChild<QPushButton*>("fpgaMotorDisable")->click();const int disabledHeartbeats=heartbeats;
    leaseTime.restart();while(leaseTime.elapsed()<250){QCoreApplication::processEvents();QThread::msleep(1);}
    require(heartbeats==disabledHeartbeats,"Disable ends host heartbeat ownership immediately");page.setConnectionState(true,false);
    int submitted=0;FpgaControlConfig requested;QString requestedSection;
    QObject::connect(&page,&FpgaControlPage::applyRequested,&page,[&](const FpgaControlConfig &v,const QString &section){++submitted;requested=v;requestedSection=section;});
    page.setConnectionState(true,false,"GP01");
    auto *rate=page.findChild<QDoubleSpinBox*>("fpgaAdcRate");rate->setValue(500000);
    page.setHardwareValues({{0x4014,1000000}});
    require(rate->value()==500000,"readback must not overwrite user edits");
    require(page.findChild<QLabel*>("fpgaAdcRateActual")->text().contains("1000000"),"actual rate independently displayed");
    apply->click();apply->click();require(submitted==1 && requestedSection=="adc" && requested.adcRateHz==500000,"busy state blocks duplicate ADC submission");
    auto *applyAi8=page.findChild<QPushButton*>("fpgaApplyAi8");
    page.setConnectionState(true,false);page.setHardwareValues({{0x6604,1}});
    require(apply->isEnabled()&&!applyAi8->isEnabled(),"active AI8 locks only its own apply button");
    page.setHardwareValues({{0x6604,0}});applyAi8->click();
    require(submitted==2 && requestedSection=="ai8","AI8 submits its own section");
    page.setConnectionState(true,false);
    int wmsApplied=0,dliaApplied=0;
    FpgaControlConfig requestedWms;
    QObject::connect(&page,&FpgaControlPage::applyWmsRequested,&page,[&](const FpgaControlConfig &config){
        ++wmsApplied;
        requestedWms=config;
    });
    QObject::connect(&page,&FpgaControlPage::applyDliaRequested,&page,[&](const FpgaControlConfig &){++dliaApplied;});
    auto *parameterTabs=page.findChild<QTabWidget*>("fpgaParameterTabs");
    auto *amplitude=page.findChild<QDoubleSpinBox*>("fpga0ScanAmplitude");
    auto *amplitude2=page.findChild<QDoubleSpinBox*>("fpga1ScanAmplitude");
    auto *bias=page.findChild<QDoubleSpinBox*>("fpga0Bias");
    auto *modulation2=page.findChild<QDoubleSpinBox*>("fpga1SineAmplitude");
    require(parameterTabs&&parameterTabs->count()==2&&parameterTabs->widget(0)->isAncestorOf(amplitude)
        &&parameterTabs->widget(1)->isAncestorOf(page.findChild<QDoubleSpinBox*>("fpga0Reference")),"WMS and DLIA fields occupy separate pages");
    require(amplitude->value()==0.475&&amplitude2->value()==0.498&&bias->value()==4.121,"scan and bias defaults use each channel calibration");
    auto precise=page.configuration();
    precise.wms[0].bias=123456789;
    precise.wms[0].scanAmplitude=-13579111;
    precise.wms[0].sineAmplitude=3456789;
    precise.wms[1].bias=-123456789;
    page.setConfiguration(FpgaControlConfig::fromJson(precise.toJson()));
    bias->stepUp(); bias->stepDown();
    require(page.configuration().wms[0].bias==123456789 && page.configuration().wms[1].bias==-123456789
        && page.configuration().wms[0].scanAmplitude==-13579111 && page.configuration().wms[0].sineAmplitude==3456789,
        "loading a preset and returning to the same displayed volts preserves its exact signed codes");
    page.setHardwareValues({{0x2014,215311728},{0x2114,215746451},{0x2018,245455370},
        {0x2118,quint32(-536870912)},{0x2120,43149290}});
    require(page.findChild<QLabel*>("fpga0ScanAmplitudeActual")->text().contains("0.952 Vpp")
        &&page.findChild<QLabel*>("fpga1ScanAmplitudeActual")->text().contains("1.000 Vpp")
        &&page.findChild<QLabel*>("fpga0BiasActual")->text().contains("4.121 V")
        &&page.findChild<QLabel*>("fpga1BiasActual")->text().contains("-1.244 V")
        &&page.findChild<QLabel*>("fpga1SineAmplitudeActual")->text().contains("0.200 Vpp"),
        "device readback applies the corresponding channel calibration to all voltage fields");
    auto *estimate=page.findChild<QLabel*>("fpga0BiasActual");
    auto *target=page.findChild<QLabel*>("fpgaDac1TargetRange");
    auto *modulation=page.findChild<QDoubleSpinBox*>("fpga0SineAmplitude");
    require(estimate->text().contains(QStringLiteral("寄存器估算")) && estimate->toolTip().contains(QStringLiteral("示波器")),
        "the estimate identifies its register source and how to measure the physical output");
    require(amplitude->maximum()==5.0&&amplitude2->minimum()==-9.954,"scan inputs use calibrated signed peak-to-peak volts");
    require(bias->minimum()==0 && bias->maximum()==5 && bias->singleStep()==0.01,"entire rated DC range is editable in small steps");
    auto *editor=bias->findChild<QLineEdit*>();
    require(editor,"bias has a keyboard editor");
    bias->selectAll();
    for (const auto ch : QStringLiteral("2.000")) {
        QKeyEvent key(QEvent::KeyPress,ch.unicode(),Qt::NoModifier,QString(ch));
        QApplication::sendEvent(bias,&key);
    }
    QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);
    QApplication::sendEvent(bias,&enter);
    require(bias->value()==2.0,"typing and committing 2.000 V works");
    bias->stepUp(); require(bias->value()==2.01,"arrow steps by 10 mV"); bias->stepDown();
    amplitude->setValue(2.0); modulation->setValue(0.2);
    const auto mixed=page.configuration();
    require(mixed.wms[0].scanAmplitude==VaporView::FpgaDac1Calibration::scanCode(1.0)
        &&mixed.wms[0].sineAmplitude==VaporView::FpgaDac1Calibration::modulationCode(0.1)
        &&target->text().contains("0.900") && target->text().contains("3.100"),
        "entered driver volts give the requested mixed waveform");
    auto *applyWms=page.findChild<QPushButton*>("fpgaApplyWms");
    applyWms->click();
    require(wmsApplied==1 && requestedWms.wms[0].bias==mixed.wms[0].bias,"the 0.900 to 3.100 V example can be submitted");
    page.setConnectionState(true,false);
    page.setHardwareValues({{0x2018,245455370}});
    require(std::abs(bias->value()-2.0)<1e-9,"register readings never replace the editable target");
    require(estimate->text().contains("4.121 V"),"refreshed register estimate shows the independently read bias");
    page.setLanguage(true);
    require(estimate->text().contains("Register estimate") && target->text().contains("target waveform range"),"English gives equivalent feedback");
    page.setLanguage(false);
    for (double rail : {0.0,5.0}) {
        const int before=wmsApplied;
        bias->setValue(rail); amplitude->setValue(0.0); modulation->setValue(0.0);
        applyWms->click();
        require(wmsApplied==before+1 && requestedWms.wms[0].bias==VaporView::FpgaDac1Calibration::biasCode(rail),
            "pure DC can be submitted at either rail");
        page.setConnectionState(true,false);
        modulation->setValue(-0.001);
        require(target->text().contains(QStringLiteral("超出驱动板")) && target->text().contains(rail==0?"-0.0005":"5.0005"),
            "a half-millivolt violation remains visible for either rail and either polarity");
        applyWms->click();
        require(wmsApplied==before+1,"rail violation cannot reach the controller");
    }
    auto *laserMin = page.findChild<QDoubleSpinBox*>("fpga0DacMin");
    auto *laserMax = page.findChild<QDoubleSpinBox*>("fpga0DacMax");
    require(laserMin&&laserMin->minimum()==VaporView::FpgaDac1Calibration::MinDacCode&&laserMin->maximum()==laserMin->minimum()
        &&laserMax&&laserMax->maximum()==VaporView::FpgaDac1Calibration::MaxDacCode&&laserMax->maximum()==laserMax->minimum(),
        "the page uses the same fixed 20-bit driver limits as the controller");
    amplitude2->setValue(amplitude2->minimum());
    require(page.configuration().wms[1].scanAmplitude==std::numeric_limits<qint32>::min(),
        "channel 2 rounded voltage endpoint clamps at signed Q1.31 limit");
    const int accepted=wmsApplied;
    amplitude->setValue(0.560);
    bias->setValue(4.050);
    modulation->setValue(2.000);
    applyWms->click();
    require(wmsApplied==accepted,"invalid combined extrema cannot be submitted from the page");
    modulation->setValue(0);
    amplitude2->setValue(1.000);
    modulation2->setValue(0.200);
    applyWms->click();
    require(wmsApplied==accepted+1&&dliaApplied==0&&submitted==2,"WMS has independent apply signal");
    require(requestedWms.wms[0].scanAmplitude==VaporView::FpgaDac1Calibration::scanCode(0.280)
        &&requestedWms.wms[0].bias==VaporView::FpgaDac1Calibration::biasCode(4.050)&&requestedWms.wms[0].sineAmplitude==0
        &&requestedWms.wms[1].scanAmplitude==215746451&&requestedWms.wms[1].sineAmplitude==43149290,
        "applying volts emits the calibrated pure sawtooth for channel 1 and preserves channel 2 scaling");
    page.setConnectionState(true,false);page.findChild<QPushButton*>("fpgaApplyDlia")->click();
    require(wmsApplied==accepted+1&&dliaApplied==1&&submitted==2,"DLIA has independent apply signal");
    page.setConnectionState(true,false);
    int acquisitions=0,dacs=0;
    QObject::connect(&page,&FpgaControlPage::acquisitionRequested,&page,[&](bool){++acquisitions;});
    QObject::connect(&page,&FpgaControlPage::dacRequested,&page,[&](int,bool){++dacs;});
    page.findChild<QPushButton*>("fpgaAcquisitionStop")->click();require(acquisitions==1&&dacs==0,"stop acquisition never stops DAC");
    require(page.findChild<QDoubleSpinBox*>("fpga0Scan")->parentWidget()!=page.findChild<QDoubleSpinBox*>("fpga1Scan")->parentWidget(),"independent WMS channel cards");
    for (int ch=0; ch<2; ++ch) {
        auto *more=page.findChild<QPushButton*>(QString("fpga%1MoreParameters").arg(ch));
        auto *advanced=page.findChild<QDoubleSpinBox*>(QString("fpga%1Phase").arg(ch))->parentWidget();
        require(more && advanced->isHidden(),"advanced parameters start collapsed");
        more->click();require(!advanced->isHidden(),"more parameters expands the channel");
        more->click();require(advanced->isHidden(),"more parameters collapses the channel");
    }
    const auto beforeChannelEdit=page.configuration();
    page.findChild<QDoubleSpinBox*>("fpga1Scan")->setValue(123);
    require(page.configuration().wms[0].scanMilliHz==beforeChannelEdit.wms[0].scanMilliHz&&page.configuration().wms[1].scanMilliHz==123000,"merged editor preserves channel independence");
    page.setConfiguration(beforeChannelEdit);
    QList<QPair<int,bool>> scanCommands,dacCommands;
    QObject::connect(&page,&FpgaControlPage::waveformRequested,&page,[&](int channel,bool enabled){scanCommands.append({channel,enabled});});
    QObject::connect(&page,&FpgaControlPage::dacRequested,&page,[&](int channel,bool enabled){dacCommands.append({channel,enabled});});
    for(int channel=0;channel<2;++channel)for(const auto *suffix : {"ScanOn","ScanOff","DacOn","DacOff"}) {
        page.setConnectionState(true,false);page.findChild<QPushButton*>(QString("fpga%1%2").arg(channel).arg(suffix))->click();
    }
    const QList<QPair<int,bool>> expectedCommands{{0,true},{0,false},{1,true},{1,false}};
    require(scanCommands==expectedCommands&&dacCommands==expectedCommands,"merged card actions address each channel separately");
    page.setConnectionState(false,false,"Replay");require(!apply->isEnabled(),"offline replay never enables hardware commands");
    int replays=0,exports=0;QObject::connect(&page,&FpgaControlPage::replayRequested,&page,[&](const QString &path){++replays;require(path.isEmpty(),"MainWindow chooses complete offline session directory");});
    QObject::connect(&page,&FpgaControlPage::exportRequested,&page,[&](const QString &path){++exports;require(path.isEmpty(),"MainWindow chooses session and export output");});
    page.findChild<QPushButton*>("fpgaReplay")->click();page.findChild<QPushButton*>("fpgaExport")->click();require(replays==1&&exports==1,"replay and export remain available offline");
    VaporView::FpgaSensor::Reading epsilon;epsilon.source=0x42;epsilon.kind=VaporView::FpgaSensor::SensorKind::Epsilon;epsilon.validity.measurement=true;page.updateSensor(epsilon);
    require(page.findChild<QLabel*>("fpgaSensor0042")->text().contains("UTC"),"communication validity never claims valid navigation or UTC");
    VaporView::FpgaSensor::Reading bmp;bmp.source=0x43;bmp.kind=VaporView::FpgaSensor::SensorKind::Bmp390;page.updateSensor(bmp);
    require(!page.findChild<QLabel*>("fpgaSensor0043")->text().contains(" Pa"),"BMP raw data without calibration never becomes pressure");
    page.setLanguage(true);require(page.findChild<QLabel*>("fpgaSensor0040")->text()=="Waiting for valid sample","unreceived sensor placeholder follows language changes");
    require(page.findChild<QLabel*>("fpgaWaveSummary32")->text().contains("FPGA not ready"),"waveform unavailable reason follows language changes");
    require(page.findChild<QLabel*>("fpgaSensor0043")->text().contains("Waiting for session calibration"),"cached sensor metadata rerenders in English");
    VaporView::FpgaWave::CompletedStream wave;wave.source=0x30;wave.message=0x1001;wave.format=2;wave.bytesPerPoint=24;wave.totalPoints=2;wave.rate=10000;wave.complete=true;
    double cyclePeak=0;bool peakPreview=false;int peakEvents=0;
    QObject::connect(&page,&FpgaControlPage::secondHarmonicPeakReady,&page,
        [&](quint16,quint32,quint64,double peak,bool preview){cyclePeak=peak;peakPreview=preview;++peakEvents;});
    auto appendWord=[](QByteArray &bytes,qint32 value){for(int i=0;i<4;++i)bytes.append(char(quint32(value)>>(8*i)));};
    for(qint32 value:{1,2,3,4,100,200,11,12,13,14,110,210})appendWord(wave.pointBytes,value);
    page.setConnectionState(true,false);
    page.updateWaveform(wave);auto *plot=page.findChild<QWidget*>("fpgaWavePlot48");
    auto *secondPlot=page.findChild<QWidget*>("fpgaWaveSecondPlot48");
    require(!page.findChild<QComboBox*>("fpgaWaveComponent48")&&!page.findChild<QComboBox*>("fpgaWaveComponent49")&&secondPlot,"both DLIA channels display their data without component selectors");
    require(plot->property("sampleMaximum").toDouble()==110&&secondPlot->property("sampleMaximum").toDouble()==210,"H1 and H2 are displayed together by default");
    require(std::abs(cyclePeak-210.0/110)<1e-9&&!peakPreview,"format2 publishes cycle peak ratio");
    auto *triggerMode = page.findChild<QCheckBox*>("fpgaWaveformTrigger");
    require(triggerMode && !triggerMode->isChecked(), "physical scan origin is the default");
    require(page.findChild<QLabel*>("fpgaWaveSummary48")->text().contains("FPGA scan origin"),
            "DLIA uses the scan origin on initial display");
    triggerMode->click();
    auto shifted = wave;
    shifted.format=0; shifted.bytesPerPoint=8; shifted.totalPoints=9;
    for (int peak : {2, 7}) {
        shifted.pointBytes.clear();
        for (int i=0; i<9; ++i) {
            appendWord(shifted.pointBytes, i==(peak+1)%9 ? 9 : 1);
            appendWord(shifted.pointBytes, i==peak ? 12 : 0);
        }
        page.updateWaveform(shifted);
        const auto hasTraceAtPeak=[](QWidget *widget, int position) {
            const QImage image=widget->grab().toImage();
            const int x=qRound((12.0+(widget->width()-24)*position/8.0)*image.devicePixelRatio());
            const int y=qRound(12*image.devicePixelRatio());
            for (int dy=0;dy<=3;++dy) for (int dx=-2;dx<=2;++dx) {
                const QColor color=image.pixelColor(x+dx,y+dy);
                if (color.blue()>color.red()+40) return true;
            }
            return false;
        };
        require(hasTraceAtPeak(secondPlot,4) && hasTraceAtPeak(plot,5),
            "DLIA0 centers shifted H2 peaks and keeps the H1 peak one sample later");
        if (peak==2) {
            // Display positions 1 and 2 straddle the original last/first samples.
            for (QWidget *trace : {plot, secondPlot}) {
                const QImage image=trace->grab().toImage();
                const int x=qRound((12.0+(trace->width()-24)*1.5/8)*image.devicePixelRatio());
                const int y=qRound((trace->height()-12)*image.devicePixelRatio());
                bool connected=false;
                for (int dy=-2; dy<=2; ++dy) {
                    const QColor color=image.pixelColor(x,y+dy);
                    connected |= color.blue()>color.red()+40;
                }
                require(connected,"both aligned harmonics draw through the original cycle boundary");
            }
        }
    }
    // Swap which H2 lobe is tallest while the whole sweep moves by three samples.
    auto multi = shifted;
    multi.totalPoints = 11;
    for (int shift : {0, 3, 0, 3}) {
        multi.pointBytes.clear();
        for (int i=0; i<11; ++i) {
            const int phase=(i+11-shift)%11;
            appendWord(multi.pointBytes, phase==3 ? 20 : phase==6 ? 5 : 0);
            appendWord(multi.pointBytes, phase==3 ? 100 : phase==6 ? (shift ? 101 : 99) : 0);
        }
        page.updateWaveform(multi);
        const QImage image=plot->grab().toImage();
        const QColor center=image.pixelColor(qRound(plot->width()*image.devicePixelRatio()/2), qRound(13*image.devicePixelRatio()));
        require(center.blue()>center.red()+40,"switching H2 peak heights keeps the same H1 lobe centered");
    }
    // A clearly taller lobe becomes the center even while the two traces still correlate.
    for (quint16 source : {0x30,0x31}) {
        auto boundary=multi; boundary.source=source;
        auto *h2Plot=page.findChild<QWidget*>(QString("fpgaWaveSecondPlot%1").arg(source));
        for (int edgeHeight : {96, 115}) {
            boundary.pointBytes.clear();
            for (int i=0; i<11; ++i) {
                appendWord(boundary.pointBytes, i==4 ? 20 : 0);
                appendWord(boundary.pointBytes, i==5 ? 100 : i==0 ? edgeHeight : 0);
            }
            page.updateWaveform(boundary);
        }
        const QImage image=h2Plot->grab().toImage();
        const auto color=image.pixelColor(qRound(h2Plot->width()*image.devicePixelRatio()/2), qRound(13*image.devicePixelRatio()));
        require(color.blue()>color.red()+40,"dominant H2 peak at the cycle boundary is centered on either channel");
    }
    const auto triggeredImage=secondPlot->grab().toImage();
    multi.pointBytes.fill(0);
    page.updateWaveform(multi);
    require(page.findChild<QLabel*>("fpgaWaveSummary48")->text().contains("showing previous sweep")
            && secondPlot->grab().toImage()==triggeredImage,"unmatched cycle explicitly holds the last trigger");
    page.updateWaveform(wave);
    const int beforeRerender=peakEvents;page.setLanguage(false);page.setLanguage(true);
    require(peakEvents==beforeRerender,"language rerender does not publish a new peak event");
    auto iq=wave;iq.format=1;iq.bytesPerPoint=16;iq.pointBytes.clear();
    for(qint32 value:{1,2,3,4,11,12,13,14})appendWord(iq.pointBytes,value);
    page.updateWaveform(iq);
    require(std::abs(cyclePeak-std::hypot(13.0,14.0)/std::hypot(11.0,12.0))<0.0001,"IQ format normalizes cycle peak magnitudes");
    auto *i2=page.findChild<QWidget*>("fpgaWaveIqPlot48_0"),*q2=page.findChild<QWidget*>("fpgaWaveIqPlot48_1");
    require(plot->property("sampleMaximum").toDouble()==11&&secondPlot->property("sampleMaximum").toDouble()==12&&i2&&q2
        &&i2->property("sampleMaximum").toDouble()==13&&q2->property("sampleMaximum").toDouble()==14&&!i2->parentWidget()->isHidden(),"IQ-only format displays all four components without a selector");
    auto partialIq=iq;partialIq.partial=true;page.updateWaveform(partialIq);
    require(i2->property("sampleCount").toInt()==0&&q2->property("sampleCount").toInt()==0,"incomplete IQ cycles clear both component pairs");
    page.updateWaveform(wave);
    require(i2->parentWidget()->isHidden()&&i2->property("sampleCount").toInt()==0,"harmonic format hides and clears the IQ-only pair");
    auto partial=wave;partial.complete=false;partial.partial=true;partial.pointBytes.truncate(24);page.updateWaveform(partial);
    require(std::isnan(cyclePeak),"invalid DLIA publishes a trend gap instead of a peak");
    require(plot->property("sampleCount").toInt()==0&&secondPlot->property("sampleCount").toInt()==0&&page.findChild<QLabel*>("fpgaWaveSummary48")->text().contains("No live waveform"),"partial cycle does not draw a fabricated connected trace");
    page.updateWaveform(wave);
    auto preview=wave;preview.preview=true;preview.previewStride=1024;preview.totalPoints=2048;
    page.updateWaveform(preview);
    require(std::abs(cyclePeak-210.0/110)<1e-9&&peakPreview,"normalized preview maximum is marked as a preview peak");
    require(plot->property("sampleCount").toInt()==2 && plot->property("previewStride").toInt()==1024,
        "bounded preview displays actual received points while preserving original stride");
    const auto previewLabel=page.findChild<QLabel*>("fpgaWaveSummary48")->text();
    require(previewLabel.contains("Sky waveform preview") && previewLabel.contains("2/2048") && !previewLabel.contains("Complete cycle"),
        "preview cannot claim a full cycle or original-length sample buffer");
    page.updateWaveform(wave);
    auto invalidReference=wave;invalidReference.format=0;invalidReference.bytesPerPoint=8;invalidReference.totalPoints=3;invalidReference.pointBytes.clear();
    for(qint32 value:{0,9999,-1,9999,4,2})appendWord(invalidReference.pointBytes,value);
    QVector<double> normalizedSamples;
    QObject::connect(&page,&FpgaControlPage::waveformDisplayUpdated,&page,
        [&](quint16 source,const QVector<QVector<double>>& samples,const QStringList& components,const QString&,quint32,bool) {
            if(source==0x30)normalizedSamples=samples.value(components.indexOf("H2/max(H1)"));
        });
    page.updateWaveform(invalidReference);
    require(cyclePeak==9999.0/4 && normalizedSamples==QVector<double>({9999.0/4,9999.0/4,0.5}),
        "cycle reference preserves H2 shape even at local H1 minima");
    invalidReference.totalPoints=2;invalidReference.pointBytes.truncate(16);page.updateWaveform(invalidReference);
    require(std::isnan(cyclePeak),"a cycle with no valid H1 has no normalized peak");
    using VaporView::FpgaWave::normalizedSecondHarmonic;
    require(std::isnan(normalizedSecondHarmonic(std::numeric_limits<double>::infinity(),1))
        &&std::isnan(normalizedSecondHarmonic(1,std::numeric_limits<double>::quiet_NaN())),"non-finite normalization inputs are invalid");
    auto zeroIq=iq;zeroIq.totalPoints=1;zeroIq.pointBytes.clear();
    for(qint32 value:{0,0,100,200})appendWord(zeroIq.pointBytes,value);
    page.updateWaveform(zeroIq);require(std::isnan(cyclePeak),"zero IQ reference cannot produce a normalized peak");
    page.updateWaveform(wave);
    {
        FpgaControlPage dacPage;
        dacPage.setLanguage(true);
        dacPage.setConnectionState(true,false);
        auto *captureStatus = dacPage.findChild<QLabel*>("fpgaCaptureStatus");
        dacPage.setHardwareValues({{4,1},{0x3004,1},{0x3008,0x1b},{0x3104,0},{0x3108,0x12}});
        require(captureStatus->text().contains("DAC1: on and ready") && captureStatus->text().contains("DAC2: off"),
                "capture status reports each DAC from its hardware readback");
        dacPage.setHardwareValues({{4,0},{0x3004,1},{0x3008,0x13},{0x3104,1},{0x3108,0x93}});
        require(captureStatus->text().contains("DAC1: on, not ready") && captureStatus->text().contains("DAC2: fault"),
                "capture status distinguishes global disable and DAC fault");
        dacPage.setTransportConnected(false);
        dacPage.setConnectionState(true,false);
        require(captureStatus->text().contains("DAC1: awaiting readback"), "reconnection needs fresh DAC state");
    }
    auto volts=page.configuration();volts.frontend[0].calibrated=true;volts.frontend[0].voltsPerCode=0.001;page.setConfiguration(volts);
    VaporView::FpgaWave::CompletedStream adc;adc.source=0x20;adc.message=0x1000;adc.bytesPerPoint=4;adc.totalPoints=3;adc.rate=1000000;adc.complete=true;
    for(qint32 value:{0,100,200})appendWord(adc.pointBytes,value);page.updateWaveform(adc);
    auto *adcPlot=page.findChild<QWidget*>("fpgaWavePlot32");require(adcPlot->property("sampleMaximum").toDouble()==0.2,"calibrated subvolt waveform retains physical range");
    page.setHardwareValues({{0x50,3},{0x403c,1},{0x413c,1}});
    // A modulated ramp has a single scan reset. Move it across the frame and across the decimation grid.
    for (int channel=0; channel<2; ++channel) {
        auto scan=adc;
        scan.source+=channel; scan.format=channel; scan.totalPoints=10000;
        auto *rawPlot=page.findChild<QWidget*>(QString("fpgaWavePlot%1").arg(scan.source));
        auto *summary=page.findChild<QLabel*>(QString("fpgaWaveSummary%1").arg(scan.source));
        QImage aligned;
        for (int edge : {0, 7683, 9999}) {
            scan.pointBytes.clear();
            for (int i=0; i<10000; ++i) {
                const int phase=(i+10000-edge)%10000;
                const int carrier=qRound(2000*std::sin(2*3.141592653589793*phase/50));
                appendWord(scan.pointBytes, (channel ? 20000 : 0) - phase + carrier);
            }
            page.updateWaveform(scan);
            require(summary->text().contains("Scan edge aligned"),"detect the scan reset under modulation");
            const auto image=rawPlot->grab().toImage();
            if (aligned.isNull()) aligned=image;
            else require(image==aligned,"raw trace starts after the reset even for a sub-stride frame shift");
        }
        scan.pointBytes.clear();
        for (int i=0;i<10000;++i) appendWord(scan.pointBytes,2000+(i%50)*20);
        page.updateWaveform(scan);
        require(!summary->text().contains("Scan edge aligned"),"repeated carrier edges alone do not trigger a scan");
    }
    triggerMode->click();
    require(page.findChild<QLabel*>("fpgaWaveSummary33")->text().contains("FPGA scan origin") &&
            page.findChild<QLabel*>("fpgaWaveSummary48")->text().contains("FPGA scan origin"),
            "switching back refreshes cached raw and harmonic plots together");
    auto rawTail=adc;rawTail.complete=false;rawTail.partial=true;rawTail.pointBytes.truncate(4);
    page.updateWaveform(rawTail);
    require(adcPlot->property("sampleCount").toInt()==0,"incomplete RAW clears the live plot instead of reusing an older cycle");
    require(page.findChild<QLabel*>("fpgaWaveSummary32")->text().contains("No live waveform"),"incomplete RAW is explicitly shown as unavailable");
    page.setHardwareValues({{0x50,0},{0x403c,0},{0x413c,0}});
    require(page.findChild<QLabel*>("fpgaWaveSummary32")->text().contains("No live waveform"),"stopped RAW is explicitly shown as unavailable");
    page.setLanguage(false);page.setLanguage(true);
    require(adcPlot->property("sampleCount").toInt()==0,"stopped RAW stays cleared after rerender");
    page.updateWaveform(adc);
    require(adcPlot->property("sampleCount").toInt()==0,"late RAW after stop stays hidden");
    page.updateWaveform(adc, true);
    require(adcPlot->property("sampleCount").toInt()==3 && page.findChild<QLabel*>("fpgaWaveSummary32")->text().contains("Historical replay"),"explicit replay displays recorded ADC with its origin");
    page.setHardwareValues({{0x50,3},{0x403c,1},{0x413c,1}});
    require(adcPlot->property("sampleCount").toInt()==0,"enabling RAW waits for the next frame");
    auto flat=adc;flat.pointBytes.fill(0);page.updateWaveform(flat);
    require(adcPlot->property("sampleCount").toInt()==3 && adcPlot->property("sampleMaximum").toDouble()==0,"a complete flat signal is displayed");
    auto invalid=adc;invalid.pointBytes.append(4,char(0));page.updateWaveform(invalid);
    require(adcPlot->property("sampleCount").toInt()==0,"a malformed length clears the previous frame");
    page.setTheme(true);
    page.setTransportConnected(true);page.setConnectionState(true,false);
    VaporView::FpgaSensor::Reading live;live.source=0x40;live.pressurePa=101325;live.validity.measurement=true;page.updateSensor(live);page.updateWaveform(adc);
    require(!sensor->text().contains("historical"),"fresh connected measurement is live");
    require(adcPlot->property("sampleCount").toInt()==3,"live ADC frame received before disconnect");
    page.setTransportConnected(false);page.setConnectionState(false,false,"Disconnected");
    page.updateWaveform(adc);
    require(adcPlot->property("sampleCount").toInt()==0,"disconnected late frame cannot repaint the waveform");
    require(sensor->text().contains("historical")&&page.findChild<QLabel*>("fpgaWaveSummary32")->text().contains("No live waveform"),"transport-first disconnect clears the live waveform while retaining sensor history status");
    page.setTransportConnected(true);page.setConnectionState(false,false,"Version mismatch");
    require(page.findChild<QLabel*>("fpgaConnectionStatus")->text().contains("Writes disabled"),"opened incompatible USB is connected but explicitly forbids writes");
    page.setLanguage(false);page.setLanguage(true);
    require(sensor->text().contains("historical"),"language changes and reconnect cannot promote old cached measurement to live");
    page.setConnectionState(true,false);page.updateSensor(live);
    const auto waveformStatusAfterSensor = page.findChild<QLabel*>("fpgaWaveSummary32")->text();
    require(!sensor->text().contains("historical") &&
        (waveformStatusAfterSensor.contains("No live waveform") || waveformStatusAfterSensor.contains("Waiting for waveform")),
        "new sensor sample refreshes only its source while the waveform waits for a new frame");
    quint16 requestedSensor=0;bool requestedEnabled=false;
    QObject::connect(&page,&FpgaControlPage::sensorEnableRequested,&page,[&](quint16 source,bool enabled){requestedSensor=source;requestedEnabled=enabled;});
    for(quint16 source:{0x40,0x42,0x43,0x44,0x45})for(bool enabled:{true,false}) {
        page.setConnectionState(true,false);
        auto *button=page.findChild<QPushButton*>(QString(enabled?"fpgaSensorEnable%1":"fpgaSensorDisable%1").arg(source,4,16,QChar('0')));
        require(button&&button->isEnabled(),"each merged device row retains its enabled control");button->click();
        require(requestedSensor==source&&requestedEnabled==enabled,"merged rows retain per-device enable and disable routing");
    }
    page.setConnectionState(true,false);
    page.resize(720,540);page.show();app.processEvents();
    auto *sensorChoice=page.findChild<QComboBox*>("fpgaAdvancedSensor");auto *sensorForms=page.findChild<QWidget*>("fpgaAdvancedSensorStack");
    auto *advancedTabs=page.findChild<QTabWidget*>("fpgaTabs");const int previousTab=advancedTabs->currentIndex();advancedTabs->setCurrentIndex(4);
    require(advancedTabs->widget(0)->isAncestorOf(page.findChild<QWidget*>("fpgaRawStart"))
        && advancedTabs->widget(0)->isAncestorOf(page.findChild<QWidget*>("fpgaWavePlot32"))
        && advancedTabs->widget(1)->isAncestorOf(rate) && advancedTabs->widget(1)->isAncestorOf(apply)
        && advancedTabs->widget(1)->isAncestorOf(page.findChild<QWidget*>("fpgaTimeSetEdge"))
        && advancedTabs->widget(3)->isAncestorOf(applyAi8),"sampling, shared settings and AI8 have dedicated locations");
    sensorChoice->setCurrentIndex(0);app.processEvents();app.processEvents();const int shortForm=sensorForms->parentWidget()->height();
    sensorChoice->setCurrentIndex(3);app.processEvents();app.processEvents();
    require(sensorForms->parentWidget()->height()>shortForm,"advanced sensor card height follows the visible form rather than the longest hidden form");
    sensorChoice->setCurrentIndex(5);app.processEvents();app.processEvents();
    require(sensorForms->parentWidget()->height()<=shortForm,"SDFM300 receive-only form no longer reserves space for withdrawn TFA controls");
    sensorChoice->setCurrentIndex(0);advancedTabs->setCurrentIndex(previousTab);app.processEvents();
    for(bool dark : {false,true}) for(bool english : {false,true}) for(int scale : {100,150}) {
        page.setLanguage(english); page.setTheme(dark,scale);
        QFont font=app.font();font.setPointSizeF(app.font().pointSizeF()*scale/100.0);page.setFont(font);
        auto *tabs=page.findChild<QTabWidget*>();
        for(int i=0;i<tabs->count();++i) {
            tabs->setCurrentIndex(i);
            const bool channelSettings=tabs->widget(i)->isAncestorOf(parameterTabs);
            for(int subtab=0;subtab<(channelSettings?parameterTabs->count():1);++subtab) {
                if(channelSettings)parameterTabs->setCurrentIndex(subtab);
                app.processEvents();
                auto *scroll=qobject_cast<QScrollArea*>(tabs->widget(i));
                require(scroll&&scroll->widgetResizable(),"all tabs remain scrollable at large fonts");require(scroll->viewport()->width()>0,"tab viewport remains visible");
                for(const auto *name : {"fpgaDliaChannels","fpgaWaveChannels","fpgaRecordingCards"}) {
                    auto *row=page.findChild<QHBoxLayout*>(name);
                    require(row&&row->count()==2,"each paired row contains two cards");
                    auto *first=row->itemAt(0)->widget(),*second=row->itemAt(1)->widget();
                    if(!first->isVisibleTo(&page))continue;
                    const QRect firstRect(first->mapTo(&page,QPoint()),first->size()),secondRect(second->mapTo(&page,QPoint()),second->size());
                    require(firstRect.top()==secondRect.top()&&firstRect.bottom()==secondRect.bottom()&&firstRect.right()<secondRect.left(),"paired cards remain side by side with equal heights across themes, languages and font scales");
                    require(qAbs(firstRect.width()-secondRect.width())<=1,"channel cards retain equal widths across themes, languages and font scales");
                    if(QString::fromLatin1(name)=="fpgaRecordingCards") {
                        auto *management=page.findChild<QScrollArea*>("fpgaManagementScroll");
                        auto *diagnostics=page.findChild<QPlainTextEdit*>("fpgaDiagnostics");
                        require(management&&diagnostics,"diagnostic status and history use scrollable text views");
                        page.setManagementState({});diagnostics->clear();app.processEvents();
                        const int initialHeight=second->height();
                        QJsonObject state;
                        for(int n=0;n<40;++n)state[QString::number(n)]=QJsonObject{
                            {"message",0x1401},{"words",QJsonArray{0x40+n,0x93,0x44,1,0,0,0}}};
                        page.setManagementState(state);
                        for(int n=0;n<300;++n)page.appendDiagnostic(QString("Diagnostic %1: sensor status and acquisition history").arg(n));
                        app.processEvents();
                        require(first->height()==initialHeight&&second->height()==initialHeight,
                            "module status and log growth cannot stretch either recording card");
                        require(management->verticalScrollBar()->maximum()>0&&diagnostics->verticalScrollBar()->maximum()>0,
                            "overflowing diagnostic contents scroll inside the fixed-height card");
                        management->verticalScrollBar()->setValue(5);
                        state["extra"]=QJsonObject{{"message",0x1401},{"words",QJsonArray{0x80,0x93,0,1,0,0,0}}};
                        page.setManagementState(state);app.processEvents();
                        require(management->verticalScrollBar()->value()==5,"status refresh preserves the reader's scroll position");
                        page.setManagementState({});diagnostics->clear();app.processEvents();
                    }
                }
                if(i==0)for(int channel=0;channel<2;++channel) {
                    auto *adc=page.findChild<QWidget*>(QString("fpgaWavePlot%1").arg(32+channel));
                    auto *h1=page.findChild<QWidget*>(QString("fpgaWavePlot%1").arg(48+channel));
                    auto *h2=page.findChild<QWidget*>(QString("fpgaWaveSecondPlot%1").arg(48+channel));
                    require(adc->height()==scale&&h1->height()==scale&&h2->height()==scale,"all three plots retain two-thirds height at each font scale");
                    require(h1->geometry().left()==h2->geometry().left()&&h1->width()==h2->width()
                        &&h1->geometry().bottom()<h2->geometry().top(),"harmonic plots fill the same column in vertical order");
                }
                if(i==3) {
                    require(!page.findChild<QComboBox*>("fpgaPressureSource") && !page.findChild<QComboBox*>("fpgaHumiditySource"),
                        "independent sensor card has no source selectors");
                    auto *grid=page.findChild<QGridLayout*>("fpgaSensorGrid");
                    require(grid&&grid->count()==28&&grid->rowCount()==7&&grid->columnCount()==4,"seven ordinary sensors include independent RD105");
                    auto *sensorBody=grid->parentWidget();
                    int previousBottom=-1;
                    for(int row=0;row<7;++row) {
                        int previousRight=-1;
                        const auto *reading=qobject_cast<QLabel*>(grid->itemAtPosition(row,1)->widget());
                        require(reading&&reading->parentWidget()==sensorBody&&!reading->wordWrap(),"sensor readings stay on one line in the shared card");
                        const int center=reading->geometry().center().y();
                        for(int column=0;column<4;++column) {
                            auto *widget=grid->itemAtPosition(row,column)->widget();const QRect rect=widget->geometry();
                            require(widget->parentWidget()==sensorBody&&rect.left()>previousRight&&qAbs(rect.center().y()-center)<=1,
                                "sensor names, readings and controls align in one row without overlap");
                            require(rect.top()>previousBottom,"device rows do not overlap at large fonts");previousRight=rect.right();
                        }
                        previousBottom=grid->itemAtPosition(row,3)->widget()->geometry().bottom();
                    }
                    auto *ai8Card=page.findChild<QLabel*>("fpgaSensor0046")->parentWidget()->parentWidget();
                    auto *address=page.findChild<QDoubleSpinBox*>("fpgaAi8Address");
                    require(address->parentWidget()->parentWidget()==ai8Card&&page.findChild<QPushButton*>("fpgaSetTemperature")->parentWidget()->parentWidget()==ai8Card,"AI8 reading, polling and temperature controls share one card");
                    const QRect ai8Rect(ai8Card->mapTo(&page,QPoint()),ai8Card->size());
                    auto *sensorCard=sensorBody->parentWidget();const QRect sensorRect(sensorCard->mapTo(&page,QPoint()),sensorCard->size());
                    require(ai8Rect.top()>sensorRect.bottom()&&ai8Rect.left()==sensorRect.left()&&ai8Rect.width()==sensorRect.width(),
                        "AI8 retains its separate full-width card below the combined sensors");
                }
            }
        }
        int previousRight=-1;
        for(auto *widget : QList<QWidget*>{devices,backend,scan,connect,disconnect}) {
            const QRect geometry(widget->mapTo(devices->parentWidget(),QPoint()),widget->size());
            require(qAbs(geometry.center().y()-devices->geometry().center().y())<=1,"FPGA connection controls share one row");
            require(geometry.left()>previousRight&&geometry.right()<devices->parentWidget()->width(),"connection row stays ordered without overlap or overflow");
            previousRight=geometry.right();
        }
        auto *connectionHeader=connectionStatus->parentWidget();
        require(connectionHeader->objectName()=="sectionTitleBar"&&discoveryStatus->parentWidget()==connectionHeader,"both connection status labels occupy the card title bar");
        auto *connectionTitle=connectionHeader->findChild<QLabel*>("sectionTitleLabel");
        previousRight=connectionTitle->geometry().right();
        for(auto *status : {discoveryStatus,connectionStatus}) {
            require(qAbs(status->geometry().center().y()-connectionTitle->geometry().center().y())<=1,"status text is vertically centered with the card title");
            require(status->geometry().left()>previousRight&&status->geometry().right()<connectionHeader->width(),"title-bar status labels stay ordered without overlap or overflow");
            previousRight=status->geometry().right();
        }
        require(backend->width()<devices->width(),"device selector receives more available width than backend");
        for(auto *button : {scan,connect,disconnect}) {
            require(button->text().isEmpty()&&!button->icon().isNull(),"connection actions render only their icons");
            require(button->width()==button->height()&&button->width()>=40,"icon actions retain square clickable targets");
            require(!button->toolTip().isEmpty()&&button->accessibleName()==button->toolTip(),"icon actions retain translated tooltips and accessible names");
            button->setAttribute(Qt::WA_UnderMouse,false);button->clearFocus();
            auto *body=button->parentWidget();
            const QPoint blankPoint=button->mapTo(body,QPoint(4,button->height()/2));
            const auto pixel=[](const QImage &image,const QPoint &point) {
                return image.pixelColor(qRound(point.x()*image.devicePixelRatio()),qRound(point.y()*image.devicePixelRatio())).rgb();
            };
            const bool enabled=button->isEnabled();
            for(bool active : {false,true}) {
                button->setEnabled(active);
                const auto idle=body->grab().toImage();
                require(pixel(idle,blankPoint)==pixel(idle,QPoint(1,blankPoint.y())),"idle and disabled line-icon actions have transparent backgrounds");
            }
            button->setDown(true);
            const auto pressed=body->grab().toImage();
            require(pixel(pressed,blankPoint)==VaporView::appThemeColor(VaporView::AppThemeColor::SurfaceAlt,dark).rgb(),"pressing a line-icon action highlights its background");
            button->setDown(false);button->setEnabled(enabled);
        }
        const auto disconnectIcon=disconnect->icon().pixmap(disconnect->iconSize(),QIcon::Normal).toImage();
        bool redStroke=false;
        for(int y=0;y<disconnectIcon.height();++y)for(int x=0;x<disconnectIcon.width();++x) {
            const auto color=disconnectIcon.pixelColor(x,y);
            redStroke|=color.alpha()>200&&color.rgb()==VaporView::appThemeColor(VaporView::AppThemeColor::ToolbarRed,dark).rgb();
        }
        require(redStroke,"disconnect line icon uses the theme's red action color");
        require(scan->toolTip()==(english?QStringLiteral("Refresh devices"):QStringLiteral("刷新设备")),"switching language keeps icon-only refresh action localized");
        require(discoveryStatus->toolTip()==discoveryStatus->text()&&connectionStatus->toolTip()==connectionStatus->text(),"connection status tooltips preserve complete text");
        require(devices->toolTip().startsWith(devices->currentText()+"\n")&&devices->toolTip().contains("04B4:00F1"),"compact device selector retains its full name and existing connection guidance");
        for(auto *card : page.findChildren<QFrame*>("fpgaSectionCard")) {
            require(card->property("vaporViewTopLevelCard").toBool(),"FPGA sections use shared card shadow and radius behavior");
            auto *header=card->findChild<QWidget*>("sectionTitleBar");
            auto *title=header->findChild<QLabel*>("sectionTitleLabel");
            require(header->height()>=title->fontMetrics().height()+4,"scaled section titles fit inside their headers");
            require(title->textInteractionFlags().testFlag(Qt::TextSelectableByKeyboard)&&title->cursor().shape()==Qt::IBeamCursor,"card titles preserve shared selection and copying behavior");
        }
    }
    page.setFont(app.font());page.setTheme(false);page.setLanguage(false);
    page.findChild<QTabWidget*>("fpgaTabs")->setCurrentIndex(0);
    auto pair=wave;pair.format=0;pair.bytesPerPoint=8;pair.pointBytes.clear();
    for(qint32 value:{10,20,30,40})appendWord(pair.pointBytes,value);
    page.updateWaveform(pair);app.processEvents();
    require(plot->property("sampleMaximum").toDouble()==30&&secondPlot->property("sampleMaximum").toDouble()==40,"format0 renders both harmonics together");
    require(plot->mapTo(&page,QPoint()).x()==secondPlot->mapTo(&page,QPoint()).x()
        &&plot->mapTo(&page,QPoint()).y()+plot->height()<secondPlot->mapTo(&page,QPoint()).y()
        &&plot->height()==100&&secondPlot->height()==100,"harmonics are stacked with compact equal-height plots");
    const auto beforeSimulation=page.configuration().toJson();
    page.setUiTestState(true,false,false,1000);
    auto *simulation=page.uiTestController();
    require(simulation&&simulation->thread()!=QThread::currentThread()&&!page.uiTestConnected()&&page.findChild<QPushButton*>("fpgaConnect")->isEnabled(),"UI test starts disconnected with an idle board");
    require(plot->property("sampleCount").toInt()==0,"entering UI test does not manufacture a waveform");
    const auto wait=[&](auto done,const char *message,int timeout=12000) {
        QElapsedTimer timer;timer.start();
        while(!done()&&timer.elapsed()<timeout){app.processEvents(QEventLoop::AllEvents,10);QThread::msleep(1);}
        require(done(),message);
    };
    int wireCommands=0;
    QObject::connect(simulation,&VaporView::Ground::Devices::FpgaDeviceController::rawCommand,&page,[&]{++wireCommands;});
    const auto operation=[&](const QJsonObject& request) {
        const int before=wireCommands;page.simulateOperation(request);
        wait([&]{return !page.uiTestBusy()&&(request.value("op")!="connect"||page.uiTestReady());},"UI operation waits for actual command responses and readback");
        if(request.value("op")!="disconnect")require(wireCommands>before,"UI operation traverses real VLP1 wire commands");
    };
    operation({{"op","connect"}});
    require(page.uiTestReady(),"UI test completes the real controller handshake");
    FpgaControlConfig wireConfig;wireConfig.backend="simulator";page.setConfiguration(wireConfig);
    operation({{"op","configure"},{"section","all"},{"configuration",wireConfig.toJson()}});
    for(quint32 base:{0x2000u,0x3000u,0x4000u,0x5000u,0x6000u,0x6100u,0x6200u,0x6300u,0x6400u,0x6500u,0x6600u})
        require(page.uiTestHardwareValues().value(base+4)==0,"opening FPGA does not enable any device");
    {
        const auto route=QObject::connect(&page,&FpgaControlPage::operationRequested,&page,[&](const QJsonObject& request){page.simulateOperation(request);});
        const auto heartbeat=QObject::connect(&page,&FpgaControlPage::motorHeartbeatRequested,&page,[&]{page.simulateOperation({{"op","motor_heartbeat"}});});
        int moves=0;
        const auto commands=QObject::connect(simulation,&VaporView::Ground::Devices::FpgaDeviceController::rawCommand,&page,[&](quint64,const QByteArray& bytes) {
            VaporView::FpgaVlp1::StreamParser parser;
            for(const auto& frame:parser.feed(reinterpret_cast<const std::uint8_t*>(bytes.constData()),bytes.size()))
                if(frame.header.source==0x47&&frame.header.message==12&&frame.payload.size()==8&&qFromLittleEndian<quint32>(frame.payload.data())==1)++moves;
        });
        operation({{"op","motor_config"},{"frequency",20}});
        page.findChild<QPushButton*>("fpgaMotorEnable")->click();
        wait([&]{return !page.uiTestBusy()&&page.uiTestHardwareValues().value(0x6704)==1;},"simulated motor enable completes through actual controller results");
        motorSteps->setValue(40);auto *run=page.findChild<QPushButton*>("fpgaMotorRun");run->click();run->click();
        wait([&]{return !page.uiTestBusy()&&page.findChild<QLabel*>("fpgaMotorProgress")->text().contains(QStringLiteral("预计进度"));},"simulated MOVE acknowledgement enters the local running estimate");
        require(moves==1&&!run->isEnabled(),"UI simulator sends one MOVE and retains the physical movement guard after the command completes");
        QElapsedTimer clock;clock.start();while(clock.elapsed()<250){app.processEvents();QThread::msleep(1);}
        require(!page.uiTestBusy(),"simulated heartbeat forwarding does not fabricate a foreground busy transaction");
        wait([&]{return run->isEnabled();},"natural simulated idle readback confirms completion without a new query",5000);
        require(moves==1&&!(page.uiTestHardwareValues().value(0x673c)&8),"host heartbeats keep the estimated two-second motion alive without retries");
        page.findChild<QPushButton*>("fpgaMotorDisable")->click();wait([&]{return !page.uiTestBusy();},"simulated motor is disabled before restoring the test fixture");
        QObject::disconnect(commands);QObject::disconnect(heartbeat);QObject::disconnect(route);
    }
    operation({{"op","sensor"},{"source",0x40},{"enabled",true}});
    page.setLanguage(true);
    wait([&]{return sensor->text().contains("Valid measurement");},"sensor display comes from decoded protocol data");
    require(page.uiTestHardwareValues().value(0x6404)==0,"single device startup preserves other channels");
    operation({{"op","sensor"},{"source",0x40},{"enabled",false}});
    operation({{"op","sensor_config"},{"source",0x40},{"baud",4800}});
    require(page.uiTestHardwareValues().value(0x6010)==4800,"UI settings are committed and read back through the protocol");
    operation({{"op","disconnect"}});
    operation({{"op","connect"},{"recover",false}});
    require(page.uiTestHardwareValues().value(0x6010)==4800&&page.uiTestHardwareValues().value(0x6004)==0,"USB reconnect preserves configuration and stopped device state");
    operation({{"op","sensor"},{"source",0x40},{"enabled",true}});
    operation({{"op","dac"},{"channel",0},{"enabled",true}});
    operation({{"op","acquisition"},{"enabled",true}});
    QSet<quint32> rawCycles;bool wireGap=false;
    QObject::connect(simulation,&VaporView::Ground::Devices::FpgaDeviceController::rawUsbBytes,&page,[&,parser=VaporView::FpgaVlp1::StreamParser{}](quint64,const QByteArray& bytes) mutable {
        for(const auto& f:parser.feed(reinterpret_cast<const std::uint8_t*>(bytes.constData()),bytes.size()))
            if(f.header.source==0x20&&f.header.message==0x1000&&f.payload.size()>=28&&!rawCycles.contains(f.header.cycleId)) {
                rawCycles.insert(f.header.cycleId);wireGap|=qFromLittleEndian<quint16>(f.payload.data()+16)>0;
            }
    });
    operation({{"op","raw"},{"enabled",true}});
    wait([&]{return adcPlot->property("sampleCount").toInt()>0&&plot->property("sampleCount").toInt()>0;},"ADC and DLIA use the production parser and assembler");
    bool incompleteRaw=false;
    QObject::connect(simulation,&VaporView::Ground::Devices::FpgaDeviceController::waveformUpdated,&page,[&](const auto& wave) {
        if(wave.source==0x20&&!wave.complete)incompleteRaw=true;
    });
    page.simulateOperation({{"op","missing_cycle"}});
    wait([&]{return wireGap;},"fault injection drops an actual first fragment from a multi-fragment RAW cycle");
    operation({{"op","raw"},{"enabled",false}});
    wait([&]{return incompleteRaw;},"normal stop/drain flushes the incomplete cycle through the production assembler");
    operation({{"op","raw"},{"enabled",true}});
    page.simulateOperation({{"op","latch_dlia"}});
    wait([&]{return plot->property("sampleCount").toInt()==0;},"protocol fault clears invalid DLIA display");
    operation({{"op","acquisition"},{"enabled",false}});
    operation({{"op","acquisition"},{"enabled",true}});
    wait([&]{return plot->property("sampleCount").toInt()>0;},"normal stop/reset/start sequence clears DLIA latch");
    operation({{"op","raw"},{"enabled",true}});
    wait([&]{return adcPlot->property("sampleCount").toInt()>0;},"RAW restarts through normal control");
    operation({{"op","raw"},{"enabled",false}});
    require(adcPlot->property("sampleCount").toInt()==0&&page.findChild<QLabel*>("fpgaWaveSummary32")->text().contains("No live waveform"),"RAW stop clears the live plot");
    page.setUiTestState(true,false,true,75000);
    wait([&]{return sensor->text().contains("Invalid measurement");},"partial fault enters through wire validity flags");
    page.setUiTestState(true,false,false,75100);
    wait([&]{return sensor->text().contains("Valid measurement");},"removing a fault restores actual decoded measurements");
    page.setUiTestState(true,true,false,75200);
    wait([&]{return sensor->text().contains("historical",Qt::CaseInsensitive);},"stalled wire data expires through the normal sample freshness timer",7000);
    operation({{"op","refresh"}});
    require(page.uiTestReady(),"stalled measurements do not fake a USB disconnection");
    page.setUiTestState(true,false,false,75300);
    wait([&]{return sensor->text().contains("Valid measurement")&&!sensor->text().contains("historical",Qt::CaseInsensitive);},"new wire samples recover from a data stall");
    operation({{"op","disconnect"}});
    operation({{"op","connect"},{"recover",false}});
    require(page.uiTestHardwareValues().value(0x6004)==1,"reconnect preserves a running sensor");
    page.setUiTestState(false);
    require(!page.uiTestController()&&page.configuration().toJson()==beforeSimulation&&adcPlot->property("sampleCount").toInt()==0,"leaving UI test destroys its isolated controller, restores configuration and clears data");
    { FpgaControlPage pending;pending.setUiTestState(true);pending.simulateOperation({{"op","connect"}}); }
    app.processEvents(); // Pending worker results must not outlive their page.

    page.close();std::puts("FPGA control page tests passed");return 0;
}
