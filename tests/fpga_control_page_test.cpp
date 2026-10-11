#include "test_settings_sandbox.h"
#include "ground/widgets/FpgaControlPage.h"
#include "shared/theme/AppTheme.h"
#include <QApplication>
#include <QStyleFactory>
#include <QAbstractItemModel>
#include "shared/theme/SingleLevelPopupComboBox.h"
#include "shared/theme/SingleLevelPopupMenu.h"
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
#ifdef Q_OS_WIN
    auto *windowsStyle=QStyleFactory::create("windows11");
    require(windowsStyle,"Windows 11 style is available for font regression");
    QApplication::setStyle(windowsStyle);
#endif
    VaporView::installButtonFocusStyle();
    VaporView::setAppStyleSheet(VaporView::startupAppThemeStyleSheet(false));
    FpgaControlPage page;
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
    auto *secondPlot=page.findChild<QWidget*>("fpgaWaveSecondPlot48");
    require(component&&component->count()==3&&secondPlot,"format2 offers paired harmonics and IQ");
    require(plot->property("sampleMaximum").toDouble()==110&&secondPlot->property("sampleMaximum").toDouble()==210,"H1 and H2 are displayed together by default");
    const double expectedMax[]={110,11,13};const double secondMax[]={210,12,14};for(int i=0;i<3;++i){component->setCurrentIndex(i);require(plot->property("sampleCount").toInt()==2&&plot->property("sampleMaximum").toDouble()==expectedMax[i]&&secondPlot->property("sampleMaximum").toDouble()==secondMax[i],"paired plots display the correct independent fields");}
    auto iq=wave;iq.format=1;iq.bytesPerPoint=16;iq.pointBytes.clear();
    for(qint32 value:{1,2,3,4,11,12,13,14})appendWord(iq.pointBytes,value);
    page.updateWaveform(iq);component->setCurrentIndex(1);
    require(plot->property("sampleMaximum").toDouble()==13&&secondPlot->property("sampleMaximum").toDouble()==14,"IQ-only format retains paired I2 Q2 view");
    page.updateWaveform(wave);
    auto partial=wave;partial.complete=false;partial.partial=true;partial.pointBytes.truncate(24);page.updateWaveform(partial);
    require(plot->property("sampleCount").toInt()==0&&secondPlot->property("sampleCount").toInt()==0&&page.findChild<QLabel*>("fpgaWaveSummary48")->text().contains("missing fragments"),"partial cycle does not draw a fabricated connected trace");
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
    page.resize(720,540);page.show();app.processEvents();
    for(bool dark : {false,true}) for(bool english : {false,true}) for(int scale : {100,150}) {
        page.setLanguage(english); page.setTheme(dark,scale);
        QFont font=app.font();font.setPointSizeF(app.font().pointSizeF()*scale/100.0);page.setFont(font);
        auto *tabs=page.findChild<QTabWidget*>();
        for(int i=0;i<tabs->count();++i){tabs->setCurrentIndex(i);app.processEvents();auto *scroll=qobject_cast<QScrollArea*>(tabs->widget(i));require(scroll&&scroll->widgetResizable(),"all tabs remain scrollable at large fonts");require(scroll->viewport()->width()>0,"tab viewport remains visible");}
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
    require(component,"DLIA component selector exists");
    const auto previousHandler=qInstallMessageHandler(captureFontWarning);
    const auto originalFont=component->font();
    const auto originalTheme=app.property(VaporView::kAppDarkThemeProperty);
    for(bool dark:{false,true}) for(int pixels:{16,24}) {
        app.setProperty(VaporView::kAppDarkThemeProperty,dark);
        page.setTheme(dark,pixels==16?100:150);
        QFont pixelFont=originalFont;pixelFont.setPixelSize(pixels);component->setFont(pixelFont);
        page.updateWaveform(wave);
        int modelResets=0;
        const auto resetConnection=QObject::connect(component->model(),&QAbstractItemModel::modelReset,[&]{++modelResets;});
        for(int index:{0,1,0,1,0}){
            component->showPopup();app.processEvents();
            auto *menu=static_cast<VaporView::SingleLevelPopupComboBox*>(component)->popupMenu();
            menu->rows().at(index)->click();app.processEvents();component->hidePopup();app.processEvents();
            require(component->currentIndex()==index,"popup selects paired harmonic and IQ views");
        }
        QObject::disconnect(resetConnection);
        require(modelResets==0,"paired selection and live samples preserve the combo model");
        require(component->font().pixelSize()==pixels,"popup workaround preserves pixel font size and scaling");
    }
    qInstallMessageHandler(previousHandler);
    require(invalidFontWarnings==0,"opening H1/H2 popup with pixel font never sets invalid point size");
    app.setProperty(VaporView::kAppDarkThemeProperty,originalTheme);
    component->setFont(originalFont);page.setTheme(false);
    auto pair=wave;pair.format=0;pair.bytesPerPoint=8;pair.pointBytes.clear();
    for(qint32 value:{10,20,30,40})appendWord(pair.pointBytes,value);
    page.updateWaveform(pair);app.processEvents();
    require(component->isHidden(),"harmonic-only format needs no selector");
    require(plot->property("sampleMaximum").toDouble()==30&&secondPlot->property("sampleMaximum").toDouble()==40,"format0 renders both harmonics together");
    require(plot->mapTo(&page,QPoint()).x()<secondPlot->mapTo(&page,QPoint()).x()&&plot->height()==secondPlot->height(),"harmonics have side-by-side equal-height plots");
    const auto beforeSimulation=page.configuration().toJson();
    page.setUiTestState(true,false,false,1000);
    page.setLanguage(true);
    page.simulateOperation({{"op","raw"},{"enabled",true}});
    require(page.findChild<QWidget*>("fpgaWavePlot33")->property("sampleCount").toInt()>0,"simulator drives second ADC lane");
    require(page.findChild<QWidget*>("fpgaWaveSecondPlot49")->property("sampleCount").toInt()>0,"simulator drives second DLIA harmonic pair");
    require(page.findChild<QLabel*>("fpgaSensor0043")->text().contains("Pa"),"simulator includes BMP390");
    page.simulateOperation({{"op","missing_cycle"}});
    require(plot->property("sampleCount").toInt()==0&&adcPlot->property("sampleCount").toInt()>0,"missing fragment clears DLIA and retains last good RAW");
    page.setUiTestState(true,false,false,1200);
    require(plot->property("sampleCount").toInt()>0,"next simulated cycle recovers independently");
    page.simulateOperation({{"op","latch_dlia"}});
    page.setUiTestState(true,false,false,1300);
    require(plot->property("sampleCount").toInt()==0,"latched DLIA fault persists across cycles");
    page.simulateOperation({{"op","acquisition"},{"enabled",false}});
    page.simulateOperation({{"op","acquisition"},{"enabled",true}});
    require(plot->property("sampleCount").toInt()>0,"simulated restart clears DLIA latch");
    page.simulateOperation({{"op","sensor"},{"source",0x40},{"enabled",false}});
    require(sensor->text().contains("Invalid measurement"),"simulated sensor disable remains disabled on refresh");
    page.setUiTestState(true,false,false,1400);
    require(sensor->text().contains("Invalid measurement"),"simulation tick does not overwrite sensor control");
    page.simulateOperation({{"op","sensor"},{"source",0x40},{"enabled",true}});
    require(sensor->text().contains("Valid measurement"),"simulated sensor enable recovers readings");
    auto simulationConfig=page.configuration();simulationConfig.rawWindowSeconds=1;
    simulationConfig.wms[0].bias=10000000;page.setConfiguration(simulationConfig);
    page.simulateOperation({{"op","configure"},{"section","wms"},{"configuration",simulationConfig.toJson()}});
    page.simulateOperation({{"op","acquisition"},{"enabled",true}});
    page.simulateOperation({{"op","raw"},{"enabled",true}});
    page.setUiTestState(true,false,false,3000);
    require(adcPlot->property("sampleCount").toInt()>0&&page.findChild<QLabel*>("fpgaWaveSummary32")->text().contains("RAW upload stopped"),"simulated RAW expires with retained historical plot");
    page.simulateOperation({{"op","disconnect"}});
    page.setUiTestState(true,false,false,3200);
    require(page.findChild<QPushButton*>("fpgaConnect")->isEnabled(),"simulated disconnect survives ticks and permits reconnect");
    page.simulateOperation({{"op","connect"}});
    require(plot->property("sampleCount").toInt()>0,"simulated reconnect resumes acquisition");
    page.setUiTestState(false);
    require(page.configuration().toJson()==beforeSimulation&&adcPlot->property("sampleCount").toInt()==0,"leaving simulator restores configuration and clears synthetic data");
    page.close();std::puts("FPGA control page tests passed");return 0;
}
