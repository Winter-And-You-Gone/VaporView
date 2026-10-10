#include "FpgaControlPage.h"
#include "shared/theme/SingleLevelPopupComboBox.h"
#include "shared/theme/AppTheme.h"
#include "shared/theme/TopLevelCardStyle.h"
#include "VisualTextLabel.h"
#include "LabelTextSelection.h"

#include <QAbstractButton>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFormLayout>
#include <QGridLayout>
#include <QFrame>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QStyleOptionButton>
#include <QSvgRenderer>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QtEndian>
#include <algorithm>
#include <limits>
#include <type_traits>

namespace {
class FpgaCheckBox final : public QCheckBox {
public:
    explicit FpgaCheckBox(QWidget *parent) : QCheckBox(parent) {
        setStyleSheet(QStringLiteral("QCheckBox::indicator, QCheckBox::indicator:checked, QCheckBox::indicator:hover { background: transparent; border: none; image: none; }"));
    }
protected:
    void paintEvent(QPaintEvent *event) override {
        QCheckBox::paintEvent(event);
        QStyleOptionButton option; initStyleOption(&option);
        const QRect indicator=style()->subElementRect(QStyle::SE_CheckBoxIndicator,&option,this);
        const bool dark=VaporView::isDarkThemePalette(palette());
        QColor color=VaporView::appThemeColor(isChecked()?VaporView::AppThemeColor::Primary:VaporView::AppThemeColor::TextSecondary,dark);
        if (!isEnabled()) color.setAlphaF(0.6f);
        QPainter painter(this); painter.setRenderHint(QPainter::Antialiasing);
        painter.translate(indicator.topLeft()); painter.scale(indicator.width()/20.0,indicator.height()/20.0);
        painter.setPen(QPen(color,1.6,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
        painter.setBrush(VaporView::appThemeColor(isChecked()?VaporView::AppThemeColor::PrimarySubtle:VaporView::AppThemeColor::Surface,dark));
        painter.drawRoundedRect(QRectF(1,1,18,18),3,3);
        if (isChecked()) {
            QPainterPath check; check.moveTo(5,10); check.lineTo(8,13); check.lineTo(15,6); painter.drawPath(check);
        }
    }
};

class SamplePlot final : public QWidget {
public:
    QVector<double> samples;
    explicit SamplePlot(QWidget *parent) : QWidget(parent) { setMinimumHeight(150); setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding); }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this); p.fillRect(rect(), palette().base());
        const QRectF area = QRectF(rect()).adjusted(12,12,-12,-12);
        p.setPen(palette().mid().color()); p.drawRect(area);
        if (samples.size() < 2) return;
        const auto mm = std::minmax_element(samples.cbegin(), samples.cend());
        const double low = *mm.first, extent = *mm.second - low;
        const double span = extent > 0 ? extent : 1.0;
        QPainterPath path;
        for (int i=0; i<samples.size(); ++i) {
            const QPointF point(area.left()+area.width()*i/(samples.size()-1), area.bottom()-area.height()*(samples[i]-low)/span);
            if (!i) path.moveTo(point); else path.lineTo(point);
        }
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(VaporView::appThemeColor(VaporView::AppThemeColor::PlotSeriesWaveBlue,
            VaporView::isDarkThemePalette(palette())),1.5)); p.drawPath(path);
    }
};
}

QString FpgaControlPage::text(const char *zh, const char *en) const { return QString::fromUtf8(english_ ? en : zh); }
void FpgaControlPage::label(QWidget *w, const QString &zh, const QString &en) {
    w->setProperty("fpgaZh",zh); w->setProperty("fpgaEn",en);
    if (auto *l=qobject_cast<QLabel*>(w)) l->setText(english_?en:zh);
    if (auto *b=qobject_cast<QAbstractButton*>(w)) {
        b->setText(english_?en:zh);
        const auto metrics=b->fontMetrics();
        b->setMinimumWidth(std::max(metrics.horizontalAdvance(zh),metrics.horizontalAdvance(en))+2*metrics.height());
    }
}

FpgaControlPage::FpgaControlPage(QWidget *parent) : QWidget(parent) {
    measurementClock_.start();
    setObjectName("fpgaControlPage");
    setAttribute(Qt::WA_StyledBackground, true);
    auto *root=new QVBoxLayout(this); root->setContentsMargins(18,4,5,8); root->setSpacing(12);
    auto card = [this](QWidget *parent, QVBoxLayout *layout, const QString &zh, const QString &en, const QString &iconName) {
        auto *frame = new QFrame(parent);
        frame->setObjectName(QStringLiteral("fpgaSectionCard"));
        VaporView::configureTopLevelCard(frame);
        auto *cardLayout = new QVBoxLayout(frame);
        cardLayout->setContentsMargins(1,0,1,1); cardLayout->setSpacing(0);
        auto *header = new QWidget(frame); header->setObjectName(QStringLiteral("sectionTitleBar"));
        auto *headerLayout = new QHBoxLayout(header); headerLayout->setContentsMargins(10,2,10,2); headerLayout->setSpacing(6);
        auto *icon = new QLabel(header); icon->setObjectName(QStringLiteral("fpgaCardIcon"));
        icon->setProperty("fpgaIconName",iconName); icon->setFixedSize(20,20);
        headerLayout->addWidget(icon,0,Qt::AlignVCenter);
        auto *title = new VaporView::VisualTextLabel(header); title->setObjectName(QStringLiteral("sectionTitleLabel"));
        VaporView::configureSelectableCardTitle(title); label(title,zh,en);
        headerLayout->addWidget(title,1); cardLayout->addWidget(header);
        auto *body = new QWidget(frame); body->setObjectName(QStringLiteral("fpgaCardBody"));
        body->setAttribute(Qt::WA_StyledBackground,true); cardLayout->addWidget(body);
        layout->addWidget(frame); return body;
    };
    auto *connection=card(this,root,QStringLiteral("FPGA 连接"),QStringLiteral("FPGA connection"),QStringLiteral("cpu"));
    auto *cl=new QGridLayout(connection);
    locator_=new QLineEdit(connection); locator_->setObjectName("fpgaLocator"); locator_->setPlaceholderText("USB device path (optional)");
    backend_=new VaporView::SingleLevelPopupComboBox(connection); backend_->setObjectName("fpgaBackend");
    backend_->addItem(QStringLiteral("Auto"),QStringLiteral("auto"));backend_->addItem(QStringLiteral("Cypress"),QStringLiteral("cypress"));backend_->addItem(QStringLiteral("WinUSB"),QStringLiteral("winusb"));
    connect_=new QPushButton(connection); label(connect_,QStringLiteral("连接"),QStringLiteral("Connect")); connect_->setObjectName("fpgaConnect");
    disconnect_=new QPushButton(connection);disconnect_->setObjectName("fpgaDisconnect"); label(disconnect_,QStringLiteral("断开"),QStringLiteral("Disconnect"));
    status_=new QLabel(connection); status_->setWordWrap(true); status_->setObjectName("fpgaConnectionStatus");
    cl->addWidget(locator_,0,0); cl->addWidget(backend_,0,1);cl->addWidget(connect_,0,2); cl->addWidget(disconnect_,0,3); cl->addWidget(status_,1,0,1,4); cl->setColumnStretch(0,1);
    connect(connect_,&QAbstractButton::clicked,this,[this]{ lockOperation(); emit connectRequested(locator_->text(),backend_->currentData().toString()); });
    connect(disconnect_,&QAbstractButton::clicked,this,[this]{ lockOperation(); emit disconnectRequested(); });
    tabs_=new QTabWidget(this); tabs_->setObjectName(QStringLiteral("fpgaTabs")); root->addWidget(tabs_,1);
    auto tab=[this](const QString &zh,const QString &en) {
        auto *scroll=new QScrollArea(tabs_); scroll->setObjectName(QStringLiteral("mainCardsScrollArea")); scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame);
        auto *body=new QWidget(scroll); scroll->setWidget(body); scroll->setProperty("fpgaZh",zh); scroll->setProperty("fpgaEn",en);
        tabs_->addTab(scroll,zh); return body;
    };
    auto *parameters=tab(QStringLiteral("参数与采集"),QStringLiteral("Parameters & acquisition")); auto *pl=new QVBoxLayout(parameters);
    auto *actions=new QGridLayout; pl->addLayout(actions);
    auto action=[this](QGridLayout *layout,int row,int col,const char *zh,const char *en,const char *name,std::function<void()> fn) {
        auto *b=new QPushButton(this); label(b,QString::fromUtf8(zh),QString::fromUtf8(en)); b->setObjectName(QString::fromLatin1(name));
        layout->addWidget(b,row,col); onlineControls_.append(b); connect(b,&QPushButton::clicked,this,[this,fn]{ lockOperation(); fn(); }); return b;
    };
    action(actions,0,0,"提交参数并核验","Apply & verify","fpgaApply",[this]{emit applyRequested(configuration());});
    refresh_=action(actions,0,1,"刷新读回","Refresh readback","fpgaRefresh",[this]{emit refreshRequested();});
    action(actions,0,2,"开始采集","Start acquisition","fpgaAcquisitionStart",[this]{emit acquisitionRequested(true);});
    action(actions,0,3,"停止采集","Stop acquisition","fpgaAcquisitionStop",[this]{emit acquisitionRequested(false);});
    auto *hint=new QLabel(parameters); label(hint,QStringLiteral("采集与模拟输出独立控制。停止 DAC 可能保留最后码，不保证归零。"),QStringLiteral("Acquisition and analog output are independent. Stopping DAC may hold its last code.")); hint->setWordWrap(true); pl->addWidget(hint);
    auto *shared=card(parameters,pl,QStringLiteral("共同采样时序"),QStringLiteral("Shared sampling"),QStringLiteral("timer")); auto *sl=new QGridLayout(shared);
    auto field=[this](QGridLayout *layout,int row,const char *zh,const char *en,const QString &name,quint32 address,double min,double max,int decimals,
                      std::function<double(const FpgaControlConfig&)> read,std::function<void(FpgaControlConfig&,double)> write) {
        auto *title=new QLabel(this); label(title,QString::fromUtf8(zh),QString::fromUtf8(en));
        auto *input=new QDoubleSpinBox(this); input->setObjectName(name); input->setRange(min,max); input->setDecimals(decimals); input->setKeyboardTracking(false);
        input->setAccessibleName(QString::fromUtf8(zh)); title->setBuddy(input);
        auto *actual=new QLabel(QStringLiteral("—"),this); actual->setObjectName(name+"Actual"); actual->setProperty("fpgaSecondaryText",true); actual->setWordWrap(true);
        layout->addWidget(title,row,0); layout->addWidget(input,row,1); layout->addWidget(actual,row,2);
        layout->setColumnStretch(2,1);
        fields_.append({input,read,write}); if (address) hardwareLabels_[address]=actual;
        connect(input,&QDoubleSpinBox::valueChanged,this,[this]{emit configurationChanged(configuration());});
    };
    field(sl,0,"请求采样率 / Hz","Requested sampling / Hz","fpgaAdcRate",0x4014,1,1000000,0,[](const auto &c){return c.adcRateHz;},[](auto &c,double v){c.adcRateHz=quint32(v);});
    for(int channel=0;channel<2;++channel) {
        auto *group=card(parameters,pl,QStringLiteral("通道 %1").arg(channel+1),QStringLiteral("Channel %1").arg(channel+1),QStringLiteral("audio-waveform"));
        auto *grid=new QGridLayout(group);
        const quint32 w=0x2000+channel*0x100,d=0x5000+channel*0x100;
        auto *buttons=new QGridLayout; grid->addLayout(buttons,0,0,1,3);
        action(buttons,0,0,"扫描开","Scan on","",[this,channel]{emit waveformRequested(channel,true);});
        action(buttons,0,1,"扫描关","Scan off","",[this,channel]{emit waveformRequested(channel,false);});
        action(buttons,0,2,"DAC 开","DAC on","",[this,channel]{emit dacRequested(channel,true);});
        action(buttons,0,3,"DAC 关","DAC off","",[this,channel]{emit dacRequested(channel,false);});
        int row=1;
        auto wf=[&,channel](const char *zh,const char *en,const char *name,quint32 offset,double minimum,double maximum,auto member,double scale=1.0) {
            field(grid,row++,zh,en,QString("fpga%1%2").arg(channel).arg(name),w+offset,minimum,maximum,scale==1.0?0:3,
                [channel,member,scale](const auto &c){return double(c.wms[channel].*member)/scale;},
                [channel,member,scale](auto &c,double v){c.wms[channel].*member=static_cast<std::remove_reference_t<decltype(c.wms[channel].*member)>>(v*scale);});
        };
        wf("扫描频率 / Hz","Scan / Hz","Scan",0x10,0.001,1000,&FpgaControlConfig::Wms::scanMilliHz,1000);
        wf("调制频率 / Hz","Modulation / Hz","Sine",0x1c,0.001,100000,&FpgaControlConfig::Wms::sineMilliHz,1000);
        wf("请求更新率 / Hz","Requested update / Hz","Update",0x2c,1,1000000,&FpgaControlConfig::Wms::updateRateHz);
        wf("扫描幅度 / Q1.31","Scan amplitude / Q1.31","ScanAmplitude",0x14,-2147483648.0,2147483647.0,&FpgaControlConfig::Wms::scanAmplitude);
        wf("调制幅度 / Q1.31","Modulation amplitude / Q1.31","SineAmplitude",0x20,-2147483648.0,2147483647.0,&FpgaControlConfig::Wms::sineAmplitude);
        wf("偏置 / Q1.31","Bias / Q1.31","Bias",0x18,-2147483648.0,2147483647.0,&FpgaControlConfig::Wms::bias);
        wf("扫描相位 / 整圈码","Scan phase / turn code","Phase",0x24,0,4294967295.0,&FpgaControlConfig::Wms::phase);
        wf("相位模式","Phase mode","PhaseMode",0x40,0,2,&FpgaControlConfig::Wms::phaseMode);
        auto df=[&,channel](const char *zh,const char *en,const char *name,quint32 offset,double minimum,double maximum,auto member,double scale=1.0) {
            field(grid,row++,zh,en,QString("fpga%1%2").arg(channel).arg(name),d+offset,minimum,maximum,scale==1.0?0:3,
                [channel,member,scale](const auto &c){return double(c.dlia[channel].*member)/scale;},
                [channel,member,scale](auto &c,double v){c.dlia[channel].*member=static_cast<std::remove_reference_t<decltype(c.dlia[channel].*member)>>(v*scale);});
        };
        df("解调参考 / Hz","Demodulation reference / Hz","Reference",0x10,0.001,100000,&FpgaControlConfig::Dlia::referenceMilliHz,1000);
        df("1f 相位 / 整圈码","1f phase / turn code","Phase1",0x14,0,4294967295.0,&FpgaControlConfig::Dlia::phase1);
        df("2f 相位校正 / 整圈码","2f phase correction / turn code","Phase2",0x18,0,4294967295.0,&FpgaControlConfig::Dlia::phase2);
        df("解调输出率 / Hz","Demodulation output / Hz","Output",0x4c,1,1000000,&FpgaControlConfig::Dlia::outputRateHz);
        df("解调模式 (3: 板上参考)","Demodulation mode (3: onboard reference)","Mode",0x20,0,7,&FpgaControlConfig::Dlia::mode);
        auto dacf=[&,channel](const char *zh,const char *en,const char *name,quint32 offset,double minimum,double maximum,auto member) {
            field(grid,row++,zh,en,QString("fpga%1Dac%2").arg(channel).arg(name),quint32(0x3000+channel*0x100)+offset,minimum,maximum,0,
                [channel,member](const auto &c){return double(c.dac[channel].*member);},
                [channel,member](auto &c,double v){c.dac[channel].*member=static_cast<std::remove_reference_t<decltype(c.dac[channel].*member)>>(v);});
        };
        dacf("DAC 控制码","DAC control code","Control",0x10,0,1023,&FpgaControlConfig::Dac::deviceCtrl);
        dacf("DAC 清除码","DAC clear code","Clear",0x14,0,1048575,&FpgaControlConfig::Dac::clearCode);
        dacf("DAC 下限码","DAC minimum code","Min",0x18,0,1048575,&FpgaControlConfig::Dac::minCode);
        dacf("DAC 上限码","DAC maximum code","Max",0x1c,0,1048575,&FpgaControlConfig::Dac::maxCode);
        dacf("请求 SPI 频率 / Hz","Requested SPI clock / Hz","Spi",0x3c,1,20000000,&FpgaControlConfig::Dac::spiClockHz);
        dacf("DAC 增益 / UQ2.30","DAC gain / UQ2.30","Gain",0x34,0,4294967295.0,&FpgaControlConfig::Dac::gain);
        dacf("DAC 偏移码","DAC offset code","Offset",0x38,-2147483648.0,2147483647.0,&FpgaControlConfig::Dac::offset);
        for(quint32 base : {w,quint32(0x3000+channel*0x100),quint32(0x4000+channel*0x100),d}) {
            const QString module=base==w?"WMS":base==d?"DLIA":base<0x4000?"DAC":"ADC";
            for(quint32 offset : {quint32(8),quint32(12)}) {
                auto *l=new QLabel(module+QStringLiteral(" · —"),group); l->setProperty("fpgaModule",module);
                grid->addWidget(l,++row,0,1,3);hardwareLabels_[base+offset]=l;
            }
        }
        for(quint32 address : {w+0x3c,w+0x44,quint32(0x3028+channel*0x100),quint32(0x302c+channel*0x100),quint32(0x4024+channel*0x100),d+0x34,d+0x48,d+0x50}) {
            auto *l=new QLabel(QStringLiteral("—"),group);grid->addWidget(l,++row,0,1,3);hardwareLabels_[address]=l;
        }
    }
    pl->addStretch();
    auto *sensors=tab(QStringLiteral("传感器"),QStringLiteral("Sensors")); auto *sensorLayout=new QVBoxLayout(sensors);
    auto *form=new QFormLayout; sensorLayout->addLayout(form);
    pressure_=new VaporView::SingleLevelPopupComboBox(sensors); pressure_->setObjectName("fpgaPressureSource"); pressure_->addItem("PTB210",0x40); pressure_->addItem("BMP390",0x43);
    auto *pressureLabel=new QLabel(sensors); label(pressureLabel,QStringLiteral("气压来源"),QStringLiteral("Pressure source")); pressureLabel->setBuddy(pressure_); form->addRow(pressureLabel,pressure_);
    connect(pressure_,&QComboBox::currentIndexChanged,this,[this]{emit pressureSourceChanged(quint16(pressure_->currentData().toUInt()));emit configurationChanged(configuration());});
    const char *names[]={"PTB210","EPSILON","BMP390","SHT45","TFA1500-L","AI8"}; const quint16 sources[]={0x40,0x42,0x43,0x44,0x45,0x46};
    for(int i=0;i<6;++i) {
        auto *g=card(sensors,sensorLayout,QString::fromLatin1(names[i]),QString::fromLatin1(names[i]),QStringLiteral("activity")); auto *grid=new QGridLayout(g);
        auto *l=new QLabel(g); l->setObjectName(QString("fpgaSensor%1").arg(sources[i],4,16,QChar('0'))); l->setWordWrap(true); sensorLabels_[sources[i]]=l;
        grid->addWidget(l,0,0,1,2); const auto source=sources[i];
        action(grid,1,0,"启用","Enable","",[this,source]{emit sensorEnableRequested(source,true);});
        action(grid,1,1,"停用","Disable","",[this,source]{emit sensorEnableRequested(source,false);});
    }
    auto *tempGrid=new QGridLayout; sensorLayout->addLayout(tempGrid); auto *temperature=new QDoubleSpinBox(sensors); temperature->setRange(-999.0,2147.4);temperature->setDecimals(1);temperature->setSingleStep(0.1); temperature->setSuffix(" °C"); tempGrid->addWidget(temperature,0,0);
    auto *ai8config=card(sensors,sensorLayout,QStringLiteral("AI8 轮询配置（停用时提交）"),QStringLiteral("AI8 polling configuration (apply while disabled)"),QStringLiteral("sliders-vertical"));auto *ai8grid=new QGridLayout(ai8config);
    field(ai8grid,0,"仪表地址","Instrument address","fpgaAi8Address",0x6614,1,80,0,[](const auto &c){return c.ai8.slaveAddress;},[](auto &c,double v){c.ai8.slaveAddress=quint32(v);});
    field(ai8grid,1,"仪表通道","Instrument channel","fpgaAi8Channel",0x661c,1,8,0,[](const auto &c){return c.ai8.channel;},[](auto &c,double v){c.ai8.channel=quint32(v);});
    field(ai8grid,2,"轮询间隔 / ms","Polling interval / ms","fpgaAi8Interval",0x662c,1,3600000,0,[](const auto &c){return c.ai8.pollIntervalMs;},[](auto &c,double v){c.ai8.pollIntervalMs=quint32(v);});
    field(ai8grid,3,"通信超时 / ms","Communication timeout / ms","fpgaAi8Timeout",0x6660,1,65535,0,[](const auto &c){return c.ai8.timeoutMs;},[](auto &c,double v){c.ai8.timeoutMs=quint32(v);});
    field(ai8grid,4,"重试上限","Retry limit","fpgaAi8Retry",0x6664,0,3,0,[](const auto &c){return c.ai8.retryLimit;},[](auto &c,double v){c.ai8.retryLimit=quint32(v);});
    auto *ai8hint=new QLabel(ai8config);label(ai8hint,QStringLiteral("共享总线固定 19200 / 8N1。保留仪表报警与超时；不会自动修改仪表参数。"),QStringLiteral("Shared bus: 19200 / 8N1. Instrument alarms and timeouts are retained; instrument parameters are not automatically changed."));ai8hint->setWordWrap(true);ai8grid->addWidget(ai8hint,5,0,1,3);
    action(tempGrid,0,1,"设置 AI8 温度并读回","Set AI8 temperature & verify","fpgaSetTemperature",[this,temperature]{emit setTemperatureRequested(temperature->value());}); sensorLayout->addStretch();
    auto *waves=tab(QStringLiteral("波形"),QStringLiteral("Waveforms")); auto *waveLayout=new QVBoxLayout(waves);
    auto *waveHint=new QLabel(waves); label(waveHint,QStringLiteral("ADC 未确认前端校准时显示原码；解调显示数字幅值，不代表气体浓度。"),QStringLiteral("ADC displays raw codes until frontend calibration is confirmed. Demodulation displays digital amplitude, not concentration.")); waveHint->setWordWrap(true); waveLayout->addWidget(waveHint);
    for(int i=0;i<2;++i) {
        auto *g=card(waves,waveLayout,QString("ADC %1 / DLIA %1").arg(i),QString("ADC %1 / DLIA %1").arg(i),QStringLiteral("audio-waveform")); auto *l=new QVBoxLayout(g);
        auto *frontend=new QGridLayout; l->addLayout(frontend);
        field(frontend,0,"电压系数 / V·code⁻¹","Voltage scale / V per code",QString("fpga%1Scale").arg(i),0,0.000000000001,100,12,[i](const auto &c){return c.frontend[i].voltsPerCode;},[i](auto &c,double v){c.frontend[i].voltsPerCode=v;});
        field(frontend,1,"极性 (+1 / -1)","Polarity (+1 / -1)",QString("fpga%1Polarity").arg(i),0,-1,1,0,[i](const auto &c){return c.frontend[i].polarity;},[i](auto &c,double v){c.frontend[i].polarity=v<0?-1:1;});
        auto *cal=new FpgaCheckBox(g); label(cal,QStringLiteral("已确认本路前端校准"),QStringLiteral("Frontend calibration confirmed")); cal->setObjectName(QString("fpga%1Calibrated").arg(i)); l->addWidget(cal);
        connect(cal,&QCheckBox::toggled,this,[this,i](bool v){config_.frontend[i].calibrated=v;emit configurationChanged(configuration());});
        for(quint16 source : {quint16(0x20+i),quint16(0x30+i)}) {
            auto *summary=new QLabel(g);summary->setObjectName(QString("fpgaWaveSummary%1").arg(source));summary->setWordWrap(true);waveLabels_[source]=summary;l->addWidget(summary);
            if(source>=0x30) {
                auto *selector=new VaporView::SingleLevelPopupComboBox(g);selector->setObjectName(QString("fpgaWaveComponent%1").arg(source));selector->setAccessibleName(QStringLiteral("DLIA component"));
                selector->addItem("H1",0);selector->addItem("H2",1);waveComponents_[source]=selector;l->addWidget(selector);
                connect(selector,&QComboBox::currentIndexChanged,this,[this,source]{if(latestWaves_.contains(source)){rerendering_=true;updateWaveform(latestWaves_.value(source));rerendering_=false;}});
            }
            auto *plot=new SamplePlot(g);plot->setObjectName(QString("fpgaWavePlot%1").arg(source));plots_[source]=plot;l->addWidget(plot);
        }
    }
    auto *recordingTab=tab(QStringLiteral("记录与诊断"),QStringLiteral("Recording & diagnostics")); auto *recordingLayout=new QVBoxLayout(recordingTab);
    auto *recording=card(recordingTab,recordingLayout,QStringLiteral("采集记录"),QStringLiteral("Acquisition recording"),QStringLiteral("scroll-text")); auto *rl=new QVBoxLayout(recording);
    recordSensors_=new FpgaCheckBox(recording); label(recordSensors_,QStringLiteral("记录传感器"),QStringLiteral("Record sensors")); recordDlia_=new FpgaCheckBox(recording); label(recordDlia_,QStringLiteral("记录解调数据"),QStringLiteral("Record demodulation")); recordRaw_=new FpgaCheckBox(recording); label(recordRaw_,QStringLiteral("记录 RAW（高吞吐）"),QStringLiteral("Record RAW (high throughput)")); rl->addWidget(recordSensors_); rl->addWidget(recordDlia_); rl->addWidget(recordRaw_);
    for(auto *c : {recordSensors_,recordDlia_,recordRaw_}) connect(c,&QCheckBox::toggled,this,[this]{emit configurationChanged(configuration());});
    auto *rawGrid=new QGridLayout; rl->addLayout(rawGrid);
    field(rawGrid,0,"RAW 诊断窗口 / 秒","RAW diagnostic window / s","fpgaRawSeconds",0,1,60,0,[](const auto &c){return c.rawWindowSeconds;},[](auto &c,double v){c.rawWindowSeconds=int(v);});
    action(rawGrid,1,0,"开始短窗 RAW","Start bounded RAW","fpgaRawStart",[this]{emit rawRequested(true);}); action(rawGrid,1,1,"停止 RAW","Stop RAW","fpgaRawStop",[this]{emit rawRequested(false);});
    record_=new QPushButton(recording); record_->setObjectName("fpgaRecord"); rl->addWidget(record_); connect(record_,&QAbstractButton::clicked,this,[this]{emit recordingRequested(!recording_);});
    recordStatus_=new QLabel(recording); recordStatus_->setWordWrap(true); rl->addWidget(recordStatus_);
    auto *files=new QHBoxLayout; rl->addLayout(files);
    auto *replay=new QPushButton(recording);replay->setObjectName("fpgaReplay"); label(replay,QStringLiteral("打开离线记录"),QStringLiteral("Open offline recording")); files->addWidget(replay);
    auto *exportButton=new QPushButton(recording);exportButton->setObjectName("fpgaExport"); label(exportButton,QStringLiteral("导出记录"),QStringLiteral("Export recording")); files->addWidget(exportButton);
    connect(replay,&QPushButton::clicked,this,[this]{emit replayRequested({});});
    connect(exportButton,&QPushButton::clicked,this,[this]{emit exportRequested({});});
    auto *diagnosticCard=card(recordingTab,recordingLayout,QStringLiteral("诊断日志"),QStringLiteral("Diagnostics"),QStringLiteral("list-filter"));
    auto *diagnosticLayout=new QVBoxLayout(diagnosticCard);
    diagnostics_=new QPlainTextEdit(diagnosticCard); diagnostics_->setObjectName("fpgaDiagnostics"); diagnostics_->setReadOnly(true); diagnostics_->setMaximumBlockCount(2000); diagnostics_->setMinimumHeight(180); diagnosticLayout->addWidget(diagnostics_,1);
    hint->setProperty("fpgaSecondaryText",true); waveHint->setProperty("fpgaSecondaryText",true); ai8hint->setProperty("fpgaSecondaryText",true);
    status_->setProperty("fpgaSecondaryText",true); recordStatus_->setProperty("fpgaSecondaryText",true);
    for (auto *body : findChildren<QWidget*>(QStringLiteral("fpgaCardBody"))) {
        body->layout()->setContentsMargins(12,10,12,12); body->layout()->setSpacing(8);
    }
    for (auto *body : {parameters,sensors,waves,recordingTab}) {
        body->layout()->setContentsMargins(5,12,5,8); body->layout()->setSpacing(12);
    }
    for (auto *button : findChildren<QPushButton*>()) button->setFocusPolicy(Qt::TabFocus);
    for (auto *button : {disconnect_,refresh_}) button->setProperty("fpgaSecondaryAction",true);
    replay->setProperty("fpgaSecondaryAction",true); exportButton->setProperty("fpgaSecondaryAction",true);
    for (const char *name : {"fpgaAcquisitionStop","fpgaRawStop"}) findChild<QPushButton*>(QString::fromLatin1(name))->setProperty("fpgaSecondaryAction",true);
    setConfiguration(config_); setLanguage(false);setTheme(false); setConnectionState(false,false); setRecordingState(false);
    auto *freshnessTimer = new QTimer(this);
    freshnessTimer->setInterval(1000);
    connect(freshnessTimer, &QTimer::timeout, this, [this] {
        if (!connected_) return;
        bool changed = false;
        const auto now = measurementClock_.elapsed();
        for (auto it = readingTimes_.cbegin(); it != readingTimes_.cend(); ++it)
            if (now-it.value() >= 4000 && !historicalReadings_.contains(it.key())) { historicalReadings_.insert(it.key()); changed = true; }
        for (auto it = waveTimes_.cbegin(); it != waveTimes_.cend(); ++it)
            if (now-it.value() >= 4000 && !historicalWaves_.contains(it.key())) { historicalWaves_.insert(it.key()); changed = true; }
        if (changed) refreshMeasurements();
    });
    freshnessTimer->start();
}

FpgaControlConfig FpgaControlPage::configuration() const {
    auto c=config_; for(const auto &f:fields_)f.write(c,f.input->value());
    c.backend=backend_->currentData().toString();c.pressureSource=quint16(pressure_->currentData().toUInt()); c.recordSensors=recordSensors_->isChecked();c.recordDlia=recordDlia_->isChecked();c.recordRaw=recordRaw_->isChecked();return c;
}
void FpgaControlPage::setConfiguration(const FpgaControlConfig &c) {
    config_=c; for(const auto &f:fields_){const QSignalBlocker block(f.input);f.input->setValue(f.read(c));}
    {const QSignalBlocker block(backend_);backend_->setCurrentIndex(backend_->findData(c.backend));}
    {const QSignalBlocker block(pressure_);pressure_->setCurrentIndex(pressure_->findData(c.pressureSource));}
    const QSignalBlocker bs(recordSensors_),bd(recordDlia_),br(recordRaw_);
    recordSensors_->setChecked(c.recordSensors);recordDlia_->setChecked(c.recordDlia);recordRaw_->setChecked(c.recordRaw);
    for(int i=0;i<2;++i){auto *cal=findChild<QCheckBox*>(QString("fpga%1Calibrated").arg(i));const QSignalBlocker b(cal);cal->setChecked(c.frontend[i].calibrated);}
}
void FpgaControlPage::lockOperation(){busy_=true;updateActions();}
void FpgaControlPage::updateActions(){connect_->setEnabled(!connected_&&!busy_);disconnect_->setEnabled(connected_&&!busy_);for(auto *w:onlineControls_)w->setEnabled(ready_&&!busy_);refresh_->setEnabled(connected_&&!busy_);record_->setEnabled(ready_||recording_);}
void FpgaControlPage::setTransportConnected(bool connected){
    const bool lost=connected_&&!connected;connected_=connected;
    if(!connected_)ready_=false;
    if(lost){
        for(auto it=latestReadings_.cbegin();it!=latestReadings_.cend();++it)historicalReadings_.insert(it.key());
        for(auto it=latestWaves_.cbegin();it!=latestWaves_.cend();++it)historicalWaves_.insert(it.key());
        for(auto *l:hardwareLabels_)l->setText(text("历史值 · ","Historical · ")+l->text());
    }
    refreshMeasurements();setConnectionState(ready_,busy_,connectionDetail_);
}
void FpgaControlPage::setConnectionState(bool ready,bool busy,const QString &detail){
    const bool lost=ready_&&!ready;ready_=ready;if(ready_)connected_=true;busy_=busy;connectionDetail_=detail;
    status_->setText(ready?text("已连接","Connected"):connected_?text("已连接未就绪 · 禁止写操作","Connected, not ready · Writes disabled"):text("离线","Offline"));
    status_->setText(status_->text()+(busy?text(" · 正在处理"," · Working"):QString())+(detail.isEmpty()?QString():" · "+detail));
    if(lost){for(auto it=latestReadings_.cbegin();it!=latestReadings_.cend();++it)historicalReadings_.insert(it.key());for(auto it=latestWaves_.cbegin();it!=latestWaves_.cend();++it)historicalWaves_.insert(it.key());refreshMeasurements();}
    updateActions();
}
void FpgaControlPage::setHardwareValues(const QMap<quint32,quint32> &v){
    for(auto it=v.cbegin();it!=v.cend();++it)if(auto *l=hardwareLabels_.value(it.key())) {
        const auto module=l->property("fpgaModule").toString();
        if((it.key()&0xff)==8 && !module.isEmpty()) {
            l->setText(QString("%1 · %2 %3 · %4 %5 · %6 %7 · %8 %9").arg(module)
                .arg(text("启用","Enabled")).arg(bool(it.value()&1)).arg(text("就绪","Ready")).arg(bool(it.value()&(module=="ADC"?0x10:2)))
                .arg(text("待生效","Pending")).arg(bool(it.value()&4)).arg(text("错误","Error")).arg(bool(it.value()&0x80)));
        } else l->setText(QString("%1 0x%2 = %3 (0x%4)").arg(module).arg(it.key(),4,16,QChar('0')).arg(it.value()).arg(it.value(),8,16,QChar('0')));
    }
}
void FpgaControlPage::updateSensor(const VaporView::FpgaSensor::Reading &r){
    auto *l=sensorLabels_.value(r.source);if(!l)return;latestReadings_[r.source]=r;if(!rerendering_&&connected_)historicalReadings_.remove(r.source);QStringList parts;
    if (!rerendering_) readingTimes_[r.source] = measurementClock_.elapsed();
    if(r.pressurePa)parts<<QString::number(*r.pressurePa,'f',3)+" Pa";if(r.temperatureC)parts<<QString::number(*r.temperatureC,'f',2)+" °C";
    if(r.humidityPct)parts<<QString::number(*r.humidityPct,'f',2)+" %RH";if(r.distanceMm)parts<<QString::number(*r.distanceMm)+" mm";
    if(r.ai8PvRaw)parts<<"PV "+QString::number(*r.ai8PvRaw*0.1,'f',1)+" °C";if(r.ai8SpRaw)parts<<"SP "+QString::number(*r.ai8SpRaw*0.1,'f',1)+" °C";
    if(r.ai8Alarm)parts<<text("报警 ","Alarm ")+QString::number(*r.ai8Alarm);if(r.ai8Host)parts<<"host "+QString::number(*r.ai8Host);
    if(r.epsilonMessageId)parts<<"FDILink "+QString::number(*r.epsilonMessageId,16);
    if(r.kind==VaporView::FpgaSensor::SensorKind::Epsilon)parts<<text("通信包有效不代表 GNSS 定位或 UTC 有效","Valid communication does not imply valid GNSS position or UTC");
    if(r.kind==VaporView::FpgaSensor::SensorKind::Bmp390&&!r.pressurePa)parts<<text(r.bmpCalibration.isEmpty()?"等待本会话校准，未生成气压值":"需主机补偿原码，未生成气压值",r.bmpCalibration.isEmpty()?"Waiting for session calibration; pressure unavailable":"Raw codes require host compensation; pressure unavailable");
    if(r.validity.continuityLoss)parts<<text("采样连续性中断","Sampling continuity lost");
    if(!r.validity.deviceOnline)parts<<text("设备离线 / 历史值","Device offline / historical value");
    parts<<text(r.validity.measurement?"测量有效":"测量无效",r.validity.measurement?"Valid measurement":"Invalid measurement");
    parts<<QString("flags 0x%1 · tick %2").arg(r.flags,0,16).arg(r.timestamp);
    if(!connected_||historicalReadings_.contains(r.source))parts.prepend(text("离线记录 / 历史值","Offline / historical"));l->setText(parts.join(" · "));
}
void FpgaControlPage::updateWaveform(const VaporView::FpgaWave::CompletedStream &s){
    auto *plot=static_cast<SamplePlot*>(plots_.value(s.source));auto *l=waveLabels_.value(s.source);if(!plot||!l)return;
    latestWaves_[s.source]=s;if(!rerendering_&&connected_)historicalWaves_.remove(s.source);
    if (!rerendering_) waveTimes_[s.source] = measurementClock_.elapsed();
    const auto historyPrefix=!connected_||historicalWaves_.contains(s.source)?text("离线记录 / 历史值 · ","Offline / historical · "):QString();
    const bool adc=s.source<0x30;const int channel=adc?s.source-0x20:s.source-0x30;const auto c=configuration();const bool volts=adc&&c.frontend[channel].calibrated;
    int component=0;
    if(!adc) {
        auto *selector=waveComponents_.value(s.source);const QSignalBlocker blocker(selector);
        const QStringList names=s.format==0?QStringList{"H1","H2"}:s.format==1?QStringList{"I1","Q1","I2","Q2"}:QStringList{"I1","Q1","I2","Q2","H1","H2"};
        const auto selected=selector->currentText();selector->clear();for(int i=0;i<names.size();++i)selector->addItem(names[i],i);
        selector->setCurrentIndex(qMax(0,names.indexOf(selected)));component=selector->currentData().toInt();
    }
    const quint32 expectedBytes=adc?4u:s.format==0?8u:s.format==1?16u:s.format==2?24u:0u;
    if(!s.complete||s.partial||s.overflow||s.continuityError) {
        plot->samples.clear();plot->setProperty("sampleCount",0);plot->update();
        l->setText(historyPrefix+text("周期缺片或质量异常；不连接缺失区间。可用原始点保留在记录中。","Cycle has missing fragments or quality faults; gaps are not joined. Available raw points remain in the recording."));return;
    }
    const quint32 availablePoints = expectedBytes ? quint32(s.pointBytes.size() / expectedBytes) : 0;
    const quint32 displayPoints = s.preview ? availablePoints : s.totalPoints;
    if(!expectedBytes||s.bytesPerPoint!=expectedBytes||!displayPoints
        || s.pointBytes.size() % expectedBytes != 0 || displayPoints > availablePoints
        || (s.preview && (!s.previewStride || displayPoints > 64 || quint64(displayPoints-1)*s.previewStride >= s.totalPoints))) {
        plot->samples.clear();plot->setProperty("sampleCount",0);plot->update();l->setText(historyPrefix+text("波形格式或长度无效；保留原始记录供诊断","Invalid waveform format or length; raw recording retained for diagnostics"));return;
    }
    plot->samples.clear();double maxH2=0;const int stride=qMax(1,int(displayPoints)/1000);const int components=adc?1:int(s.bytesPerPoint/4);
    for(int point=0;point<int(displayPoints);++point){const int offset=point*components*4;if(offset+components*4>s.pointBytes.size())break;
        const double value=adc&&s.format==1?double(qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(s.pointBytes.constData()+offset))):double(qFromLittleEndian<qint32>(reinterpret_cast<const uchar*>(s.pointBytes.constData()+offset+component*4)));
        if(!adc&&s.format==0&&components>=2){const qint32 h2=qFromLittleEndian<qint32>(reinterpret_cast<const uchar*>(s.pointBytes.constData()+offset+4));maxH2=qMax(maxH2,double(h2));}
        if(point%stride==0)plot->samples.append(volts?value*c.frontend[channel].voltsPerCode*c.frontend[channel].polarity:double(value));
    }
    const bool valid=s.complete&&!s.partial&&!s.overflow&&!s.continuityError;
    l->setText(QString("%1 · %2 · %3 Hz · %4 %5 · %6%7").arg(adc?QString("ADC %1").arg(channel):QString("DLIA %1").arg(channel))
        .arg(s.totalPoints).arg(s.rate).arg(text("点","points")).arg(volts?"V":text("原码 / 数字幅值","raw / digital amplitude"))
        .arg(text(valid?"完整周期":"异常 / 不完整周期",valid?"Complete cycle":"Invalid / partial cycle"))
        .arg(!adc&&s.format==0&&!s.preview?text(" · 本周期 2f 最大值 "," · Cycle 2f maximum ")+QString::number(maxH2):QString()));
    if(s.preview)l->setText(QString("%1 · %2/%3 %4 · %5 %6 · %7 Hz · %8").arg(text("天空端波形预览","Sky waveform preview"))
        .arg(displayPoints).arg(s.totalPoints).arg(text("点","points")).arg(text("抽稀间隔","stride")).arg(s.previewStride).arg(s.rate)
        .arg(volts?"V":text("原码 / 数字幅值","raw / digital amplitude")));
    if(!adc)l->setText(l->text()+" · "+waveComponents_.value(s.source)->currentText());
    l->setText(historyPrefix+l->text());
    plot->setProperty("sampleCount",plot->samples.size());plot->setProperty("previewStride",s.preview?s.previewStride:1);if(!plot->samples.isEmpty()){const auto mm=std::minmax_element(plot->samples.cbegin(),plot->samples.cend());plot->setProperty("sampleMinimum",*mm.first);plot->setProperty("sampleMaximum",*mm.second);}plot->update();
}
void FpgaControlPage::appendDiagnostic(const QString &s){diagnostics_->appendPlainText(s);}
void FpgaControlPage::setLanguage(bool en){english_=en;for(auto *w:findChildren<QWidget*>())if(w->property("fpgaZh").isValid())label(w,w->property("fpgaZh").toString(),w->property("fpgaEn").toString());
    for(int i=0;i<tabs_->count();++i)tabs_->setTabText(i,tabs_->widget(i)->property(en?"fpgaEn":"fpgaZh").toString());
    for(auto it=sensorLabels_.cbegin();it!=sensorLabels_.cend();++it)if(!latestReadings_.contains(it.key()))it.value()->setText(text("等待有效样本","Waiting for valid sample"));
    for(auto it=waveLabels_.cbegin();it!=waveLabels_.cend();++it)if(!latestWaves_.contains(it.key()))it.value()->setText(text("等待波形 · ADC 原码 / 解调数字幅值","Waiting for waveform · ADC raw / demodulation digital amplitude"));
    refreshMeasurements();
    locator_->setPlaceholderText(text("USB 设备路径（可选）","USB device path (optional)"));setConnectionState(ready_,busy_,connectionDetail_);setRecordingState(recording_,recordingDetail_);
}
void FpgaControlPage::refreshMeasurements(){rerendering_=true;const auto readings=latestReadings_;for(const auto &reading:readings)updateSensor(reading);const auto waves=latestWaves_;for(const auto &wave:waves)updateWaveform(wave);rerendering_=false;}
void FpgaControlPage::setTheme(bool dark,int fontScalePercent){
    using namespace VaporView;setProperty(kAppDarkThemeProperty,dark);setPalette(appThemePalette(dark,palette()));
    const qreal scale=qBound(60,fontScalePercent,180)/100.0;
    const auto pixels=[scale](int value){return qRound(value*scale);};
    setStyleSheet(applyAppThemeTokens(QStringLiteral(
        "QWidget#fpgaControlPage { background-color: @vv-window; }"
        "QWidget#fpgaControlPage QFrame#fpgaSectionCard { background-color: @vv-surface-raised; border: 1px solid @vv-border; border-radius: 12px; }"
        "QWidget#fpgaControlPage QFrame#fpgaSectionCard > QWidget#sectionTitleBar { background-color: @vv-surface-raised; border: none; border-bottom: 1px solid @vv-border; border-top-left-radius: 11px; border-top-right-radius: 11px; min-height: %1px; max-height: %1px; }"
        "QWidget#fpgaControlPage QWidget#fpgaCardBody { background-color: @vv-surface-raised; border: none; border-bottom-left-radius: 11px; border-bottom-right-radius: 11px; }"
        "QWidget#fpgaControlPage QLabel { background-color: transparent; border: none; color: @vv-text; }"
        "QWidget#fpgaControlPage QLabel#sectionTitleLabel { font-size: %2px; font-weight: 700; padding: 0; margin: 0; }"
        "QWidget#fpgaControlPage QLabel[fpgaSecondaryText=\"true\"] { color: @vv-text-secondary; font-weight: 400; }"
        "QWidget#fpgaControlPage QTabWidget::pane { border: none; background-color: @vv-window; }"
        "QWidget#fpgaControlPage QTabBar::tab { background-color: @vv-surface-alt; color: @vv-text; border: 1px solid transparent; border-radius: 6px; padding: 0 %3px; margin-right: 4px; min-height: %4px; font-weight: 600; }"
        "QWidget#fpgaControlPage QTabBar::tab:hover { background-color: @vv-primary-subtle; color: @vv-primary; }"
        "QWidget#fpgaControlPage QTabBar::tab:selected { background-color: @vv-primary; color: @vv-white; }"
        "QWidget#fpgaControlPage QTabBar::tab:focus { border-color: @vv-focus; }"
        "QWidget#fpgaControlPage QScrollArea, QWidget#fpgaControlPage QScrollArea > QWidget, QWidget#fpgaControlPage QScrollArea > QWidget > QWidget { background-color: @vv-window; border: none; }"
        "QWidget#fpgaControlPage QPushButton[fpgaSecondaryAction=\"true\"] { background-color: @vv-surface-alt; color: @vv-text; border: 1px solid @vv-border; }"
        "QWidget#fpgaControlPage QPushButton[fpgaSecondaryAction=\"true\"]:hover { background-color: @vv-primary-subtle; color: @vv-primary; border-color: @vv-primary; }"
        "QWidget#fpgaControlPage QPushButton[fpgaSecondaryAction=\"true\"]:focus { border-color: @vv-focus; }"
        "QWidget#fpgaControlPage QPushButton[fpgaSecondaryAction=\"true\"]:disabled { background-color: @vv-surface-alt; color: @vv-text-muted; border-color: @vv-border; }")
        .arg(pixels(40)).arg(pixels(16)).arg(pixels(12)).arg(pixels(36)),dark));
    for (auto *icon : findChildren<QLabel*>(QStringLiteral("fpgaCardIcon"))) {
        const int size=pixels(20); icon->setFixedSize(size,size);
        QFile file(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("resources/lucide/%1.svg").arg(icon->property("fpgaIconName").toString())));
        if (!file.open(QIODevice::ReadOnly)) continue;
        QByteArray svg=file.readAll(); svg.replace("currentColor",appThemeColorName(AppThemeColor::TextTitle,dark).toUtf8());
        const qreal dpr=icon->devicePixelRatioF(); QPixmap pixmap(qRound(size*dpr),qRound(size*dpr)); pixmap.setDevicePixelRatio(dpr); pixmap.fill(Qt::transparent);
        QSvgRenderer renderer(svg); QPainter painter(&pixmap); renderer.render(&painter,QRectF(0,0,size,size)); painter.end(); icon->setPixmap(pixmap);
    }
    updateTopLevelCardShadows(this,scale);update();
}
void FpgaControlPage::setRecordingState(bool active,const QString &detail){recording_=active;recordingDetail_=detail;record_->setText(text(active?"停止记录":"开始记录",active?"Stop recording":"Start recording"));recordStatus_->setText(text(active?"正在记录":"未记录",active?"Recording":"Not recording")+(detail.isEmpty()?QString():" · "+detail));updateActions();}
