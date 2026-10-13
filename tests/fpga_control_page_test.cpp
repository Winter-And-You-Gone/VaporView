#include "test_settings_sandbox.h"
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
#include <QTabWidget>
#include <cstdio>
#include <cstdlib>
#include <cmath>

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
    require(cyclePeak==210&&!peakPreview,"format2 publishes the raw H2 cycle maximum");
    const int beforeRerender=peakEvents;page.setLanguage(false);page.setLanguage(true);
    require(peakEvents==beforeRerender,"language rerender does not publish a new peak event");
    auto iq=wave;iq.format=1;iq.bytesPerPoint=16;iq.pointBytes.clear();
    for(qint32 value:{1,2,3,4,11,12,13,14})appendWord(iq.pointBytes,value);
    page.updateWaveform(iq);
    require(std::abs(cyclePeak-std::hypot(13.0,14.0))<0.0001,"IQ format publishes the unnormalized second harmonic magnitude peak");
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
    require(cyclePeak==210&&peakPreview,"preview maximum is marked as a preview peak");
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
        for(int i=0;i<tabs->count();++i) {
            tabs->setCurrentIndex(i);
            for(int subtab=0;subtab<(i==0?parameterTabs->count():1);++subtab) {
                if(i==0)parameterTabs->setCurrentIndex(subtab);
                app.processEvents();
                auto *scroll=qobject_cast<QScrollArea*>(tabs->widget(i));
                require(scroll&&scroll->widgetResizable(),"all tabs remain scrollable at large fonts");require(scroll->viewport()->width()>0,"tab viewport remains visible");
                for(const auto *name : {"fpgaDliaChannels","fpgaWaveChannels"}) {
                    auto *row=page.findChild<QHBoxLayout*>(name);
                    require(row&&row->count()==2,"each channel row contains two cards");
                    auto *first=row->itemAt(0)->widget(),*second=row->itemAt(1)->widget();
                    if(!first->isVisibleTo(&page))continue;
                    const QRect firstRect(first->mapTo(&page,QPoint()),first->size()),secondRect(second->mapTo(&page,QPoint()),second->size());
                    require(firstRect.top()==secondRect.top()&&firstRect.right()<secondRect.left(),"channel cards remain side by side across themes, languages and font scales");
                    require(qAbs(firstRect.width()-secondRect.width())<=1,"channel cards retain equal widths across themes, languages and font scales");
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
                    auto *grid=page.findChild<QGridLayout*>("fpgaSensorGrid");
                    require(grid&&grid->count()==5&&grid->rowCount()==2&&grid->columnCount()==3&&!grid->itemAtPosition(1,2),"five ordinary sensors occupy three columns in two rows");
                    const int columnWidth=grid->itemAtPosition(0,0)->widget()->width();
                    for(int row=0;row<2;++row) {
                        int previousRight=-1;
                        for(int column=0;column<3;++column) {
                            auto *item=grid->itemAtPosition(row,column);if(!item)continue;auto *card=item->widget();
                            const QRect rect(card->mapTo(&page,QPoint()),card->size());
                            require(qAbs(card->width()-columnWidth)<=1&&rect.left()>previousRight,"sensor cards use equal compact widths without overlap");previousRight=rect.right();
                        }
                    }
                    auto *ai8Card=page.findChild<QLabel*>("fpgaSensor0046")->parentWidget()->parentWidget();
                    auto *address=page.findChild<QDoubleSpinBox*>("fpgaAi8Address");
                    require(address->parentWidget()->parentWidget()==ai8Card&&page.findChild<QPushButton*>("fpgaSetTemperature")->parentWidget()->parentWidget()==ai8Card,"AI8 reading, polling and temperature controls share one card");
                    const QRect ai8Rect(ai8Card->mapTo(&page,QPoint()),ai8Card->size());
                    auto *lastSensor=grid->itemAtPosition(1,0)->widget();
                    require(ai8Rect.top()>lastSensor->mapTo(&page,QPoint()).y()+lastSensor->height()&&ai8Rect.width()>2*columnWidth,"AI8 occupies its own full-width row below the sensor grid");
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
    page.setLanguage(true);
    for(int channel=0;channel<2;++channel) {
        for(const auto *field : {"Update","PhaseMode","DacClear","DacMin","DacMax","DacSpi"})
            require(page.findChild<QLabel*>(QString("fpga%1%2Actual").arg(channel).arg(field))->text()!=QStringLiteral("—"),"simulation covers all previously missing editable WMS / DAC readbacks");
        for(quint32 address : {0x2008u,0x200cu,0x3008u,0x300cu,0x203cu,0x2044u,0x3028u,0x302cu,0x400cu,0x4024u,0x5034u,0x5048u,0x5050u})
            require(page.findChild<QLabel*>(QString("fpgaRegister%1").arg(address+channel*0x100,4,16,QChar('0')))->text()!=QStringLiteral("—"),"simulation covers named status, error and counter readbacks on both channels");
        require(page.findChild<QLabel*>(QString("fpga%1DacSpiActual").arg(channel))->text().contains("= 16666666 "),"simulated actual SPI rate accounts for the integer divider");
    }
    auto *writes0=page.findChild<QLabel*>("fpgaRegister3028"),*writes1=page.findChild<QLabel*>("fpgaRegister3128");
    page.simulateOperation({{"op","dac"},{"channel",0},{"enabled",false}});
    const auto stoppedWrites=writes0->text(),runningWrites=writes1->text();
    page.setUiTestState(true,false,false,1100);
    require(writes0->text()==stoppedWrites&&writes1->text()!=runningWrites,"DAC write counters stop independently for a disabled channel");
    page.simulateOperation({{"op","dac"},{"channel",0},{"enabled",true}});
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
