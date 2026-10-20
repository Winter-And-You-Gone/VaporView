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
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
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
    int submitted=0;FpgaControlConfig requested;
    QObject::connect(&page,&FpgaControlPage::applyRequested,&page,[&](const FpgaControlConfig &v){++submitted;requested=v;});
    page.setConnectionState(true,false,"GP01");
    auto *rate=page.findChild<QDoubleSpinBox*>("fpgaAdcRate");rate->setValue(500000);
    page.setHardwareValues({{0x4014,1000000}});
    require(rate->value()==500000,"readback must not overwrite user edits");
    require(page.findChild<QLabel*>("fpgaAdcRateActual")->text().contains("1000000"),"actual rate independently displayed");
    apply->click();apply->click();require(submitted==1 && requested.adcRateHz==500000,"busy state blocks duplicate submission");
    page.setConnectionState(true,false);
    int wmsApplied=0,dliaApplied=0;
    QObject::connect(&page,&FpgaControlPage::applyWmsRequested,&page,[&](const FpgaControlConfig &){++wmsApplied;});
    QObject::connect(&page,&FpgaControlPage::applyDliaRequested,&page,[&](const FpgaControlConfig &){++dliaApplied;});
    auto *parameterTabs=page.findChild<QTabWidget*>("fpgaParameterTabs");
    auto *amplitude=page.findChild<QDoubleSpinBox*>("fpga0ScanAmplitude");
    auto *bias=page.findChild<QDoubleSpinBox*>("fpga0Bias");
    require(parameterTabs&&parameterTabs->count()==2&&parameterTabs->widget(0)->isAncestorOf(amplitude)
        &&parameterTabs->widget(1)->isAncestorOf(page.findChild<QDoubleSpinBox*>("fpga0Reference")),"WMS and DLIA fields occupy separate pages");
    require(amplitude->value()==0.5,"default scan amplitude displayed in volts");
    auto precise=page.configuration();precise.wms[0].bias=123456789;page.setConfiguration(precise);
    require(page.configuration().wms[0].bias==123456789,"unchanged voltage display preserves exact stored code");
    page.setHardwareValues({{0x2018,quint32(-536870912)}});
    require(page.findChild<QLabel*>("fpga0BiasActual")->text().contains("-2.500 V"),"device readback displays signed voltage");
    bias->setValue(-2.5);require(page.configuration().wms[0].bias==-536870912,"negative voltage converts to signed Q1.31");
    amplitude->setValue(10);require(page.configuration().wms[0].scanAmplitude==2147483647,"positive full scale clamps without signed overflow");
    page.findChild<QPushButton*>("fpgaApplyWms")->click();
    require(wmsApplied==1&&dliaApplied==0&&submitted==1,"WMS has independent apply signal");
    page.setConnectionState(true,false);page.findChild<QPushButton*>("fpgaApplyDlia")->click();
    require(wmsApplied==1&&dliaApplied==1&&submitted==1,"DLIA has independent apply signal");
    page.setConnectionState(true,false);
    int acquisitions=0,dacs=0;
    QObject::connect(&page,&FpgaControlPage::acquisitionRequested,&page,[&](bool){++acquisitions;});
    QObject::connect(&page,&FpgaControlPage::dacRequested,&page,[&](int,bool){++dacs;});
    page.findChild<QPushButton*>("fpgaAcquisitionStop")->click();require(acquisitions==1&&dacs==0,"stop acquisition never stops DAC");
    auto *wmsGrid=page.findChild<QGridLayout*>("fpgaWmsGrid");
    require(wmsGrid&&page.findChild<QDoubleSpinBox*>("fpga0Scan")->parentWidget()==page.findChild<QDoubleSpinBox*>("fpga1Scan")->parentWidget(),"WMS and DAC channels share one card");
    require(page.findChild<QLabel*>("fpga0ScanLabel")&&!page.findChild<QLabel*>("fpga1ScanLabel"),"both channel columns reuse one field label");
    for(const auto *field : {"Scan","PhaseMode","DacControl","DacSpi"}) {
        auto *firstInput=page.findChild<QDoubleSpinBox*>(QString("fpga0%1").arg(field));
        auto *secondInput=page.findChild<QDoubleSpinBox*>(QString("fpga1%1").arg(field));
        int firstRow,firstColumn,secondRow,secondColumn,rowSpan,columnSpan;
        wmsGrid->getItemPosition(wmsGrid->indexOf(firstInput),&firstRow,&firstColumn,&rowSpan,&columnSpan);
        wmsGrid->getItemPosition(wmsGrid->indexOf(secondInput),&secondRow,&secondColumn,&rowSpan,&columnSpan);
        require(firstRow==secondRow&&firstColumn==1&&secondColumn==3,"merged fields align on one row with independent inputs");
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
    require(page.findChild<QLabel*>("fpgaWaveSummary32")->text().startsWith("Waiting for waveform"),"unreceived waveform placeholder follows language changes");
    require(page.findChild<QLabel*>("fpgaSensor0043")->text().contains("Waiting for session calibration"),"cached sensor metadata rerenders in English");
    VaporView::FpgaWave::CompletedStream wave;wave.source=0x30;wave.message=0x1001;wave.format=2;wave.bytesPerPoint=24;wave.totalPoints=2;wave.rate=10000;wave.complete=true;
    double cyclePeak=0;bool peakPreview=false;int peakEvents=0;
    QObject::connect(&page,&FpgaControlPage::secondHarmonicPeakReady,&page,
        [&](quint16,quint32,quint64,double peak,bool preview){cyclePeak=peak;peakPreview=preview;++peakEvents;});
    auto appendWord=[](QByteArray &bytes,qint32 value){for(int i=0;i<4;++i)bytes.append(char(quint32(value)>>(8*i)));};
    for(qint32 value:{1,2,3,4,100,200,11,12,13,14,110,210})appendWord(wave.pointBytes,value);
    page.updateWaveform(wave);auto *plot=page.findChild<QWidget*>("fpgaWavePlot48");
    auto *secondPlot=page.findChild<QWidget*>("fpgaWaveSecondPlot48");
    require(!page.findChild<QComboBox*>("fpgaWaveComponent48")&&!page.findChild<QComboBox*>("fpgaWaveComponent49")&&secondPlot,"both DLIA channels display their data without component selectors");
    require(plot->property("sampleMaximum").toDouble()==110&&secondPlot->property("sampleMaximum").toDouble()==210,"H1 and H2 are displayed together by default");
    require(cyclePeak==2&&!peakPreview,"format2 publishes max(H2/H1), not max(H2) divided by max(H1)");
    const int beforeRerender=peakEvents;page.setLanguage(false);page.setLanguage(true);
    require(peakEvents==beforeRerender,"language rerender does not publish a new peak event");
    auto iq=wave;iq.format=1;iq.bytesPerPoint=16;iq.pointBytes.clear();
    for(qint32 value:{1,2,3,4,11,12,13,14})appendWord(iq.pointBytes,value);
    page.updateWaveform(iq);
    require(std::abs(cyclePeak-std::hypot(3.0,4.0)/std::hypot(1.0,2.0))<0.0001,"IQ format normalizes magnitudes at the same point before finding the peak");
    auto *i2=page.findChild<QWidget*>("fpgaWaveIqPlot48_0"),*q2=page.findChild<QWidget*>("fpgaWaveIqPlot48_1");
    require(plot->property("sampleMaximum").toDouble()==11&&secondPlot->property("sampleMaximum").toDouble()==12&&i2&&q2
        &&i2->property("sampleMaximum").toDouble()==13&&q2->property("sampleMaximum").toDouble()==14&&!i2->parentWidget()->isHidden(),"IQ-only format displays all four components without a selector");
    auto partialIq=iq;partialIq.partial=true;page.updateWaveform(partialIq);
    require(i2->property("sampleCount").toInt()==0&&q2->property("sampleCount").toInt()==0,"incomplete IQ cycles clear both component pairs");
    page.updateWaveform(wave);
    require(i2->parentWidget()->isHidden()&&i2->property("sampleCount").toInt()==0,"harmonic format hides and clears the IQ-only pair");
    auto partial=wave;partial.complete=false;partial.partial=true;partial.pointBytes.truncate(24);page.updateWaveform(partial);
    require(std::isnan(cyclePeak),"invalid DLIA publishes a trend gap instead of a peak");
    require(plot->property("sampleCount").toInt()==0&&secondPlot->property("sampleCount").toInt()==0&&page.findChild<QLabel*>("fpgaWaveSummary48")->text().contains("missing fragments"),"partial cycle does not draw a fabricated connected trace");
    page.updateWaveform(wave);
    auto preview=wave;preview.preview=true;preview.previewStride=1024;preview.totalPoints=2048;
    page.updateWaveform(preview);
    require(cyclePeak==2&&peakPreview,"normalized preview maximum is marked as a preview peak");
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
            if(source==0x30)normalizedSamples=samples.value(components.indexOf("H2/H1"));
        });
    page.updateWaveform(invalidReference);
    require(cyclePeak==0.5&&normalizedSamples.size()==3&&std::isnan(normalizedSamples[0])&&std::isnan(normalizedSamples[1])&&normalizedSamples[2]==0.5,
        "zero and negative H1 retain sample positions as gaps and cannot become peaks");
    invalidReference.totalPoints=2;invalidReference.pointBytes.truncate(16);page.updateWaveform(invalidReference);
    require(std::isnan(cyclePeak),"a cycle with no valid H1 has no normalized peak");
    using VaporView::FpgaWave::normalizedSecondHarmonic;
    require(std::isnan(normalizedSecondHarmonic(std::numeric_limits<double>::infinity(),1))
        &&std::isnan(normalizedSecondHarmonic(1,std::numeric_limits<double>::quiet_NaN())),"non-finite normalization inputs are invalid");
    auto zeroIq=iq;zeroIq.totalPoints=1;zeroIq.pointBytes.clear();
    for(qint32 value:{0,0,100,200})appendWord(zeroIq.pointBytes,value);
    page.updateWaveform(zeroIq);require(std::isnan(cyclePeak),"zero IQ reference cannot produce a normalized peak");
    page.updateWaveform(wave);
    auto volts=page.configuration();volts.frontend[0].calibrated=true;volts.frontend[0].voltsPerCode=0.001;page.setConfiguration(volts);
    VaporView::FpgaWave::CompletedStream adc;adc.source=0x20;adc.message=0x1000;adc.bytesPerPoint=4;adc.totalPoints=3;adc.rate=1000000;adc.complete=true;
    for(qint32 value:{0,100,200})appendWord(adc.pointBytes,value);page.updateWaveform(adc);
    auto *adcPlot=page.findChild<QWidget*>("fpgaWavePlot32");require(adcPlot->property("sampleMaximum").toDouble()==0.2,"calibrated subvolt waveform retains physical range");
    page.setHardwareValues({{0x50,3},{0x403c,1},{0x413c,1}});
    auto rawTail=adc;rawTail.complete=false;rawTail.partial=true;rawTail.pointBytes.truncate(4);
    page.updateWaveform(rawTail);
    require(adcPlot->property("sampleCount").toInt()==3&&adcPlot->property("sampleMaximum").toDouble()==0.2,"incomplete RAW preserves previous complete samples");
    require(page.findChild<QLabel*>("fpgaWaveSummary32")->text().contains("historical, not current"),"retained RAW never claims current measurement");
    page.setHardwareValues({{0x50,0},{0x403c,0},{0x413c,0}});
    require(page.findChild<QLabel*>("fpgaWaveSummary32")->text().contains("RAW upload stopped"),"readback explicitly labels stopped RAW");
    page.setLanguage(false);page.setLanguage(true);
    require(adcPlot->property("sampleCount").toInt()==3,"historical RAW survives rerender");
    page.updateWaveform(adc);
    page.setTheme(true);
    page.setTransportConnected(true);page.setConnectionState(true,false);
    VaporView::FpgaSensor::Reading live;live.source=0x40;live.pressurePa=101325;live.validity.measurement=true;page.updateSensor(live);page.updateWaveform(adc);
    require(!sensor->text().contains("historical"),"fresh connected measurement is live");
    page.setTransportConnected(false);page.setConnectionState(false,false,"Disconnected");
    require(sensor->text().contains("historical")&&page.findChild<QLabel*>("fpgaWaveSummary32")->text().contains("historical"),"transport-first disconnect marks cached sensor and waveform as historical");
    page.setTransportConnected(true);page.setConnectionState(false,false,"Version mismatch");
    require(page.findChild<QLabel*>("fpgaConnectionStatus")->text().contains("Writes disabled"),"opened incompatible USB is connected but explicitly forbids writes");
    page.setLanguage(false);page.setLanguage(true);
    require(sensor->text().contains("historical"),"language changes and reconnect cannot promote old cached measurement to live");
    page.setConnectionState(true,false);page.updateSensor(live);
    require(!sensor->text().contains("historical")&&page.findChild<QLabel*>("fpgaWaveSummary32")->text().contains("historical"),"new sensor sample refreshes only its source while old waveform remains historical");
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
    auto *advancedTabs=page.findChild<QTabWidget*>("fpgaTabs");const int previousTab=advancedTabs->currentIndex();advancedTabs->setCurrentIndex(3);
    sensorChoice->setCurrentIndex(0);app.processEvents();app.processEvents();const int shortForm=sensorForms->parentWidget()->height();
    sensorChoice->setCurrentIndex(5);app.processEvents();app.processEvents();
    require(sensorForms->parentWidget()->height()>shortForm,"advanced sensor card height follows the visible form rather than the longest hidden form");
    sensorChoice->setCurrentIndex(0);advancedTabs->setCurrentIndex(previousTab);app.processEvents();
    for(bool dark : {false,true}) for(bool english : {false,true}) for(int scale : {100,150}) {
        page.setLanguage(english); page.setTheme(dark,scale);
        QFont font=app.font();font.setPointSizeF(app.font().pointSizeF()*scale/100.0);page.setFont(font);
        auto *tabs=page.findChild<QTabWidget*>();
        for(int i=0;i<tabs->count();++i) {
            tabs->setCurrentIndex(i);
            for(int subtab=0;subtab<(i==0?parameterTabs->count():1);++subtab) {
                if(i==0)parameterTabs->setCurrentIndex(subtab);
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
                if(i==2)for(int channel=0;channel<2;++channel) {
                    auto *adc=page.findChild<QWidget*>(QString("fpgaWavePlot%1").arg(32+channel));
                    auto *h1=page.findChild<QWidget*>(QString("fpgaWavePlot%1").arg(48+channel));
                    auto *h2=page.findChild<QWidget*>(QString("fpgaWaveSecondPlot%1").arg(48+channel));
                    require(adc->height()==scale&&h1->height()==scale&&h2->height()==scale,"all three plots retain two-thirds height at each font scale");
                    require(h1->geometry().left()==h2->geometry().left()&&h1->width()==h2->width()
                        &&h1->geometry().bottom()<h2->geometry().top(),"harmonic plots fill the same column in vertical order");
                }
                if(i==1) {
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
    page.findChild<QTabWidget*>()->setCurrentIndex(2);
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
    require(adcPlot->property("sampleCount").toInt()>0&&page.findChild<QLabel*>("fpgaWaveSummary32")->text().contains("RAW upload stopped"),"RAW stop keeps previous cycle as history");
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
