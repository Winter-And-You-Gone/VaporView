#include "test_settings_sandbox.h"
#include "ground/widgets/FpgaControlPage.h"
#include "shared/theme/AppTheme.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFrame>
#include <QJsonArray>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QTabWidget>
#include <cstdio>
#include <cstdlib>

namespace {
int invalidFontWarnings=0;
void captureFontWarning(QtMsgType, const QMessageLogContext&, const QString& message) {
    if(message.contains("QFont::setPointSize")&&message.contains("<= 0"))++invalidFontWarnings;
}

void require(bool value, const char *message) {
    if (!value) { std::fprintf(stderr,"FAIL: %s\n",message); std::exit(1); }
}
}
int main(int argc,char **argv) {
    QApplication app(argc,argv);
    VaporView::installButtonFocusStyle();
    FpgaControlPage page;
    auto *devices=page.findChild<QComboBox*>("fpgaLocator");
    auto *scan=page.findChild<QPushButton*>("fpgaDiscover");
    auto *connect=page.findChild<QPushButton*>("fpgaConnect");
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
    require(c.recordSensors && c.recordDlia && !c.recordRaw && c.rawWindowSeconds==10,"bounded RAW off / sensor and DLIA recording");
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
    int submitted=0;FpgaControlConfig requested;
    QObject::connect(&page,&FpgaControlPage::applyRequested,&page,[&](const FpgaControlConfig &v){++submitted;requested=v;});
    page.setConnectionState(true,false,"GP01");
    auto *rate=page.findChild<QDoubleSpinBox*>("fpgaAdcRate");rate->setValue(500000);
    page.setHardwareValues({{0x4014,1000000}});
    require(rate->value()==500000,"readback must not overwrite user edits");
    require(page.findChild<QLabel*>("fpgaAdcRateActual")->text().contains("1000000"),"actual rate independently displayed");
    apply->click();apply->click();require(submitted==1 && requested.adcRateHz==500000,"busy state blocks duplicate submission");
    page.setConnectionState(true,false);
    int acquisitions=0,dacs=0;
    QObject::connect(&page,&FpgaControlPage::acquisitionRequested,&page,[&](bool){++acquisitions;});
    QObject::connect(&page,&FpgaControlPage::dacRequested,&page,[&](int,bool){++dacs;});
    page.findChild<QPushButton*>("fpgaAcquisitionStop")->click();require(acquisitions==1&&dacs==0,"stop acquisition never stops DAC");
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
    auto appendWord=[](QByteArray &bytes,qint32 value){for(int i=0;i<4;++i)bytes.append(char(quint32(value)>>(8*i)));};
    for(qint32 value:{1,2,3,4,100,200,11,12,13,14,110,210})appendWord(wave.pointBytes,value);
    page.updateWaveform(wave);auto *component=page.findChild<QComboBox*>("fpgaWaveComponent48");auto *plot=page.findChild<QWidget*>("fpgaWavePlot48");
    require(component&&component->count()==6,"format2 offers all IQ and harmonic components");
    const double expectedMax[]={11,12,13,14,110,210};for(int i=0;i<6;++i){component->setCurrentIndex(i);require(plot->property("sampleCount").toInt()==2&&plot->property("sampleMaximum").toDouble()==expectedMax[i],"component selection displays chosen physical field");}
    auto partial=wave;partial.complete=false;partial.partial=true;partial.pointBytes.truncate(24);page.updateWaveform(partial);
    require(plot->property("sampleCount").toInt()==0&&page.findChild<QLabel*>("fpgaWaveSummary48")->text().contains("missing fragments"),"partial cycle does not draw a fabricated connected trace");
    page.updateWaveform(wave);
    auto preview=wave;preview.preview=true;preview.previewStride=1024;preview.totalPoints=2048;
    page.updateWaveform(preview);
    require(plot->property("sampleCount").toInt()==2 && plot->property("previewStride").toInt()==1024,
        "bounded preview displays actual received points while preserving original stride");
    const auto previewLabel=page.findChild<QLabel*>("fpgaWaveSummary48")->text();
    require(previewLabel.contains("Sky waveform preview") && previewLabel.contains("2/2048") && !previewLabel.contains("Complete cycle"),
        "preview cannot claim a full cycle or original-length sample buffer");
    page.updateWaveform(wave);
    auto volts=page.configuration();volts.frontend[0].calibrated=true;volts.frontend[0].voltsPerCode=0.001;page.setConfiguration(volts);
    VaporView::FpgaWave::CompletedStream adc;adc.source=0x20;adc.message=0x1000;adc.bytesPerPoint=4;adc.totalPoints=3;adc.rate=1000000;adc.complete=true;
    for(qint32 value:{0,100,200})appendWord(adc.pointBytes,value);page.updateWaveform(adc);
    auto *adcPlot=page.findChild<QWidget*>("fpgaWavePlot32");require(adcPlot->property("sampleMaximum").toDouble()==0.2,"calibrated subvolt waveform retains physical range");
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
    page.resize(720,540);page.show();app.processEvents();
    for(bool dark : {false,true}) for(bool english : {false,true}) for(int scale : {100,150}) {
        page.setLanguage(english); page.setTheme(dark,scale);
        QFont font=app.font();font.setPointSizeF(app.font().pointSizeF()*scale/100.0);page.setFont(font);
        auto *tabs=page.findChild<QTabWidget*>();
        for(int i=0;i<tabs->count();++i){tabs->setCurrentIndex(i);app.processEvents();auto *scroll=qobject_cast<QScrollArea*>(tabs->widget(i));require(scroll&&scroll->widgetResizable(),"all tabs remain scrollable at large fonts");require(scroll->viewport()->width()>0,"tab viewport remains visible");}
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
    require(component,"DLIA component selector exists");
    const auto previousHandler=qInstallMessageHandler(captureFontWarning);
    const auto originalFont=component->font();
    const auto originalTheme=app.property(VaporView::kAppDarkThemeProperty);
    for(bool dark:{false,true}) for(int pixels:{16,24}) {
        app.setProperty(VaporView::kAppDarkThemeProperty,dark);
        page.setTheme(dark,pixels==16?100:150);
        QFont pixelFont=originalFont;pixelFont.setPixelSize(pixels);component->setFont(pixelFont);
        for(int i=0;i<3;++i){component->showPopup();app.processEvents();component->hidePopup();app.processEvents();}
        require(component->font().pixelSize()==pixels,"popup workaround preserves pixel font size and scaling");
    }
    qInstallMessageHandler(previousHandler);
    require(invalidFontWarnings==0,"opening H1/H2 popup with pixel font never sets invalid point size");
    app.setProperty(VaporView::kAppDarkThemeProperty,originalTheme);
    component->setFont(originalFont);page.setTheme(false);
    page.close();std::puts("FPGA control page tests passed");return 0;
}
