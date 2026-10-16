#include "FpgaControlPage.h"
#include "FpgaDeviceController.h"
#include "FpgaUsbSimulator.h"
#include "FpgaTelemetry.h"
#include "FpgaAdvancedParameters.h"
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
#include <QGridLayout>
#include <QJsonArray>
#include <QShowEvent>
#include <QFrame>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QStandardItemModel>
#include <QStyleOptionButton>
#include <QSvgRenderer>
#include <QTabWidget>
#include <cstring>
#include <QTimer>
#include <QThread>
#include <QPointer>
#include <QVBoxLayout>
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <limits>
#include <type_traits>

namespace {
class ConnectionStatusLabel final : public QLabel {
public:
    explicit ConnectionStatusLabel(QWidget *parent) : QLabel(parent) {
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.setPen(palette().color(QPalette::WindowText));
        painter.drawText(contentsRect(), alignment(), fontMetrics().elidedText(text(), Qt::ElideRight, contentsRect().width()));
    }
};

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
    explicit SamplePlot(QWidget *parent) : QWidget(parent) { setProperty("fpgaFixedPlot",true); setFixedHeight(100); setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed); }
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
        if (b->property("fpgaIconOnly").toBool()) {
            b->setText(QString()); b->setToolTip(english_?en:zh); b->setAccessibleName(english_?en:zh);
            return;
        }
        b->setText(english_?en:zh);
        const auto metrics=b->fontMetrics();
        b->setMinimumWidth(std::max(metrics.horizontalAdvance(zh),metrics.horizontalAdvance(en))+2*metrics.height());
    }
}

FpgaControlPage::FpgaControlPage(QWidget *parent) : QWidget(parent) {
    measurementClock_.start();
    setObjectName("fpgaControlPage");
    setAttribute(Qt::WA_StyledBackground, true);
    auto *root=new QVBoxLayout(this); root->setContentsMargins(0,4,0,8); root->setSpacing(12);
    auto card = [this](QWidget *parent, QBoxLayout *layout, const QString &zh, const QString &en, const QString &iconName) {
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
        if(layout)layout->addWidget(frame); return body;
    };
    auto *connectionContainer=new QWidget(this); connectionContainer->setObjectName("fpgaConnectionContainer");
    auto *connectionLayout=new QVBoxLayout(connectionContainer); connectionLayout->setContentsMargins(18,0,13,0); connectionLayout->setProperty("fpgaCardOuterMargins",true); root->addWidget(connectionContainer);
    auto *connection=card(connectionContainer,connectionLayout,QStringLiteral("FPGA 连接"),QStringLiteral("FPGA connection"),QStringLiteral("cpu"));
    auto *connectionHeader=connection->parentWidget()->findChild<QWidget*>(QStringLiteral("sectionTitleBar"),Qt::FindDirectChildrenOnly);
    auto *connectionHeaderLayout=qobject_cast<QHBoxLayout*>(connectionHeader->layout());
    connectionHeaderLayout->setStretch(1,0);
    auto *cl=new QHBoxLayout(connection);
    locator_=new VaporView::SingleLevelPopupComboBox(connection); locator_->setObjectName("fpgaLocator");
    locator_->setMinimumContentsLength(12); locator_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    locator_->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Fixed);
    auto connectionAction=[this,connection](const char *name,const char *icon,const char *zh,const char *en) {
        auto *button=new QPushButton(connection); button->setObjectName(QString::fromLatin1(name));
        button->setFocusPolicy(Qt::TabFocus);
        button->setProperty("fpgaIconOnly",true); button->setProperty("fpgaIconName",QString::fromLatin1(icon));
        label(button,QString::fromUtf8(zh),QString::fromUtf8(en)); return button;
    };
    discover_=connectionAction("fpgaDiscover","refresh-cw","刷新设备","Refresh devices");
    discoveryStatus_=new ConnectionStatusLabel(connectionHeader); discoveryStatus_->setObjectName("fpgaDiscoveryStatus");
    backend_=new VaporView::SingleLevelPopupComboBox(connection); backend_->setObjectName("fpgaBackend");
    backend_->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Fixed);
    backend_->addItem(QStringLiteral("Auto"),QStringLiteral("auto"));backend_->addItem(QStringLiteral("Cypress"),QStringLiteral("cypress"));backend_->addItem(QStringLiteral("WinUSB"),QStringLiteral("winusb"));
    backend_->addItem(QStringLiteral("模拟器"),QStringLiteral("simulator"));
    connect_=connectionAction("fpgaConnect","link","连接","Connect");
    disconnect_=connectionAction("fpgaDisconnect","unlink","断开","Disconnect");
    status_=new ConnectionStatusLabel(connectionHeader); status_->setObjectName("fpgaConnectionStatus");
    cl->addWidget(locator_,3); cl->addWidget(backend_,1); cl->addWidget(discover_); cl->addWidget(connect_); cl->addWidget(disconnect_);
    connectionHeaderLayout->addSpacing(12); connectionHeaderLayout->addWidget(discoveryStatus_,1); connectionHeaderLayout->addWidget(status_,2);
    setTabOrder(locator_,backend_); setTabOrder(backend_,discover_); setTabOrder(discover_,connect_); setTabOrder(connect_,disconnect_);
    connect(discover_,&QAbstractButton::clicked,this,&FpgaControlPage::requestUsbDiscovery);
    connect(locator_,&QComboBox::currentIndexChanged,this,[this]{updateActions();});
    connect(backend_,&QComboBox::currentIndexChanged,this,[this]{
        emit configurationChanged(configuration());
        if(!usbRemote_&&!busy_&&!connected_) {
            // A backend change supersedes an outstanding discovery result.
            ++usbDiscoveryGeneration_;usbDiscovering_=false;usbDiscovery_={};
            requestUsbDiscovery();
        }
        else updateActions();
    });
    connect(connect_,&QAbstractButton::clicked,this,[this]{ lockOperation(); emit connectRequested(locator_->currentData().toString(),backend_->currentData().toString()); });
    connect(disconnect_,&QAbstractButton::clicked,this,[this]{ lockOperation(); emit disconnectRequested(); });
    tabs_=new QTabWidget(this); tabs_->setObjectName(QStringLiteral("fpgaTabs")); root->addWidget(tabs_,1);
    auto tab=[this](const QString &zh,const QString &en) {
        auto *scroll=new QScrollArea(tabs_); scroll->setObjectName(QStringLiteral("mainCardsScrollArea")); scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame);
        scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
        auto *body=new QWidget(scroll); scroll->setWidget(body); scroll->setProperty("fpgaZh",zh); scroll->setProperty("fpgaEn",en);
        tabs_->addTab(scroll,zh); return body;
    };
    auto *parameters=tab(QStringLiteral("参数与采集"),QStringLiteral("Parameters & acquisition")); auto *pl=new QVBoxLayout(parameters);
    auto *actions=new QGridLayout; pl->addLayout(actions);
    auto action=[this](QGridLayout *layout,int row,int col,const char *zh,const char *en,const QString &name,std::function<void()> fn) {
        auto *b=new QPushButton(this); label(b,QString::fromUtf8(zh),QString::fromUtf8(en)); b->setObjectName(name);
        layout->addWidget(b,row,col); onlineControls_.append(b); connect(b,&QPushButton::clicked,this,[this,fn]{ lockOperation(); fn(); }); return b;
    };
    action(actions,0,0,"应用采样与 AI8 参数","Apply sampling & AI8","fpgaApply",[this]{emit applyRequested(configuration());});
    refresh_=action(actions,0,1,"刷新读回","Refresh readback","fpgaRefresh",[this]{emit refreshRequested();});
    action(actions,0,2,"开始采集","Start acquisition","fpgaAcquisitionStart",[this]{emit acquisitionRequested(true);});
    action(actions,0,3,"停止采集","Stop acquisition","fpgaAcquisitionStop",[this]{emit acquisitionRequested(false);});
    auto *hint=new QLabel(parameters); label(hint,QStringLiteral("采集与模拟输出独立控制。停止 DAC 可能保留最后码，不保证归零。"),QStringLiteral("Acquisition and analog output are independent. Stopping DAC may hold its last code.")); hint->setWordWrap(true); pl->addWidget(hint);
    auto *shared=card(parameters,pl,QStringLiteral("共同采样时序"),QStringLiteral("Shared sampling"),QStringLiteral("timer")); auto *sl=new QGridLayout(shared);
    auto field=[this](QGridLayout *layout,int row,const char *zh,const char *en,const QString &name,quint32 address,double min,double max,int decimals,
                      std::function<double(const FpgaControlConfig&)> read,std::function<void(FpgaControlConfig&,double)> write,int column=0) {
        auto *input=new QDoubleSpinBox(this); input->setObjectName(name); input->setRange(min,max); input->setDecimals(decimals); input->setKeyboardTracking(false);
        input->setAccessibleName(QString::fromUtf8(zh));
        if(column==0) {
            auto *title=new QLabel(this);title->setObjectName(name+"Label");title->setWordWrap(true);label(title,QString::fromUtf8(zh),QString::fromUtf8(en));title->setBuddy(input);layout->addWidget(title,row,0);
        }
        auto *actual=new QLabel(QStringLiteral("—"),this); actual->setObjectName(name+"Actual"); actual->setProperty("fpgaSecondaryText",true); actual->setWordWrap(true);
        layout->addWidget(input,row,column+1); layout->addWidget(actual,row,column+2);
        layout->setColumnStretch(column+2,1);
        fields_.append({input,read,write}); if (address) hardwareLabels_[address]=actual;
        connect(input,&QDoubleSpinBox::valueChanged,this,[this]{emit configurationChanged(configuration());});
    };
    field(sl,0,"请求采样率 / Hz","Requested sampling / Hz","fpgaAdcRate",0x4014,1,1000000,0,[](const auto &c){return c.adcRateHz;},[](auto &c,double v){c.adcRateHz=quint32(v);});
    field(sl,1,"CNV 脉宽 / 10 ns","CNV width / 10 ns","fpgaAdcCnvTicks",0x4030,2,20,0,[](const auto& c){return c.adcCnvTicks;},[](auto& c,double v){c.adcCnvTicks=quint32(v);});
    field(sl,2,"BUSY 超时 / 10 ns","BUSY timeout / 10 ns","fpgaAdcBusyTimeout",0x4034,40,1000000,0,[](const auto& c){return c.adcBusyTimeoutTicks;},[](auto& c,double v){c.adcBusyTimeoutTicks=quint32(v);});
    for(int ch=0;ch<2;++ch)field(sl,3+ch,ch?"ADC1 周期点数 (0 关闭校验)":"ADC0 周期点数 (0 关闭校验)",ch?"ADC1 points (0 disables check)":"ADC0 points (0 disables check)",QString("fpgaAdcExpected%1").arg(ch),0x401c+ch*0x100,0,4294967295.0,0,[ch](const auto& c){return c.adcExpectedPoints[ch];},[ch](auto& c,double v){c.adcExpectedPoints[ch]=quint32(v);});
    auto *parameterTabs=new QTabWidget(parameters);parameterTabs->setObjectName("fpgaParameterTabs");pl->addWidget(parameterTabs);
    auto *wmsPage=new QWidget(parameterTabs),*dliaPage=new QWidget(parameterTabs);
    parameterTabs->addTab(wmsPage,"WMS / DAC");parameterTabs->addTab(dliaPage,"DLIA");
    auto *wmsLayout=new QVBoxLayout(wmsPage),*dliaLayout=new QVBoxLayout(dliaPage);
    for(auto *layout : {wmsLayout,dliaLayout}) { const auto margins=layout->contentsMargins(); layout->setContentsMargins(0,margins.top(),0,margins.bottom()); }
    auto *wmsActions=new QGridLayout;wmsLayout->addLayout(wmsActions);
    action(wmsActions,0,0,"应用 WMS / DAC 参数","Apply WMS / DAC","fpgaApplyWms",[this]{emit applyWmsRequested(configuration());});
    auto *dliaActions=new QGridLayout;dliaLayout->addLayout(dliaActions);
    action(dliaActions,0,0,"应用 DLIA 参数","Apply DLIA","fpgaApplyDlia",[this]{emit applyDliaRequested(configuration());});
    auto *voltageHint=new QLabel(wmsPage);voltageHint->setWordWrap(true);
    label(voltageHint,QStringLiteral("幅度为峰值，偏置为直流电压；按 DAC ±10 V 标称量程、增益 1 / 偏移 0 换算，非实测值。应用参数后需重新开始采集。"),QStringLiteral("Amplitudes are peak voltages; bias is DC. Nominal DAC ±10 V, unity gain / zero offset; not measured voltage. Restart acquisition after applying."));wmsLayout->addWidget(voltageHint);
    auto *dliaHint=new QLabel(dliaPage);dliaHint->setWordWrap(true);label(dliaHint,QStringLiteral("仅提交解调参数，保留 WMS / DAC 设置。应用后需重新开始采集。"),QStringLiteral("Apply demodulation settings only; preserve WMS / DAC. Restart acquisition after applying."));dliaLayout->addWidget(dliaHint);
    auto *wmsGroup=card(wmsPage,wmsLayout,QStringLiteral("WMS / DAC"),QStringLiteral("WMS / DAC"),QStringLiteral("audio-waveform"));
    auto *wmsGrid=new QGridLayout(wmsGroup);wmsGrid->setObjectName("fpgaWmsGrid");
    auto *dliaChannels=new QHBoxLayout;dliaChannels->setObjectName("fpgaDliaChannels");dliaChannels->setSpacing(12);dliaLayout->addLayout(dliaChannels);
    auto readback=[this](QGridLayout *layout,int row,int column,int span,quint32 address,const QString &zh,const QString &en,const QString &module=QString()) {
        if(column==1) {auto *title=new QLabel(this);title->setWordWrap(true);label(title,zh,en);layout->addWidget(title,row,0);}
        auto *actual=new QLabel(QStringLiteral("—"),this);actual->setObjectName(QString("fpgaRegister%1").arg(address,4,16,QChar('0')));
        actual->setProperty("fpgaSecondaryText",true);actual->setProperty("fpgaModule",module);actual->setWordWrap(true);
        layout->addWidget(actual,row,column,1,span);hardwareLabels_[address]=actual;
    };
    for(int channel=0;channel<2;++channel) {
        auto *group=wmsGroup;auto *grid=wmsGrid;
        const quint32 w=0x2000+channel*0x100,d=0x5000+channel*0x100;
        auto *channelTitle=new QLabel(group);label(channelTitle,QStringLiteral("通道 %1").arg(channel+1),QStringLiteral("Channel %1").arg(channel+1));grid->addWidget(channelTitle,0,1+channel*2,1,2);
        auto *buttons=new QGridLayout; grid->addLayout(buttons,1,1+channel*2,1,2);
        action(buttons,0,0,"扫描开","Scan on",QString("fpga%1ScanOn").arg(channel),[this,channel]{emit waveformRequested(channel,true);});
        action(buttons,0,1,"扫描关","Scan off",QString("fpga%1ScanOff").arg(channel),[this,channel]{emit waveformRequested(channel,false);});
        action(buttons,0,2,"DAC 开","DAC on",QString("fpga%1DacOn").arg(channel),[this,channel]{emit dacRequested(channel,true);});
        action(buttons,0,3,"DAC 关","DAC off",QString("fpga%1DacOff").arg(channel),[this,channel]{emit dacRequested(channel,false);});
        int row=2;
        auto wf=[&,channel](const char *zh,const char *en,const char *name,quint32 offset,double minimum,double maximum,auto member,double scale=1.0) {
            field(grid,row++,zh,en,QString("fpga%1%2").arg(channel).arg(name),w+offset,minimum,maximum,scale==1.0?0:3,
                [channel,member,scale](const auto &c){return double(c.wms[channel].*member)/scale;},
                [channel,member,scale](auto &c,double v){
                    using Value=std::remove_reference_t<decltype(c.wms[channel].*member)>;
                    // Keep exact saved Q1.31 codes when the millivolt display is unchanged.
                    if(scale>1000 && std::abs(v-std::round(double(c.wms[channel].*member)/scale*1000)/1000)<1e-9)return;
                    c.wms[channel].*member=static_cast<Value>(std::clamp(std::round(v*scale),
                        double(std::numeric_limits<Value>::lowest()),double(std::numeric_limits<Value>::max())));
                },channel*2);
        };
        wf("扫描频率 / Hz","Scan / Hz","Scan",0x10,0.001,1000,&FpgaControlConfig::Wms::scanMilliHz,1000);
        wf("调制频率 / Hz","Modulation / Hz","Sine",0x1c,0.001,100000,&FpgaControlConfig::Wms::sineMilliHz,1000);
        wf("请求更新率 / Hz","Requested update / Hz","Update",0x2c,1,1000000,&FpgaControlConfig::Wms::updateRateHz);
        wf("扫描峰值 / V","Scan peak / V","ScanAmplitude",0x14,-10.0,10.0-10.0/2147483648.0,&FpgaControlConfig::Wms::scanAmplitude,2147483648.0/10.0);
        wf("调制峰值 / V","Modulation peak / V","SineAmplitude",0x20,-10.0,10.0-10.0/2147483648.0,&FpgaControlConfig::Wms::sineAmplitude,2147483648.0/10.0);
        wf("直流偏置 / V","DC bias / V","Bias",0x18,-10.0,10.0-10.0/2147483648.0,&FpgaControlConfig::Wms::bias,2147483648.0/10.0);
        wf("扫描相位 / 整圈码","Scan phase / turn code","Phase",0x24,0,4294967295.0,&FpgaControlConfig::Wms::phase);
        wf("相位模式","Phase mode","PhaseMode",0x40,0,2,&FpgaControlConfig::Wms::phaseMode);
        auto *dliaGroup=card(dliaPage,dliaChannels,QStringLiteral("DLIA · 通道 %1").arg(channel+1),QStringLiteral("DLIA · Channel %1").arg(channel+1),QStringLiteral("activity"));
        auto *dliaGrid=new QGridLayout(dliaGroup);int dliaRow=0;
        auto df=[&,channel](const char *zh,const char *en,const char *name,quint32 offset,double minimum,double maximum,auto member,double scale=1.0) {
            field(dliaGrid,dliaRow++,zh,en,QString("fpga%1%2").arg(channel).arg(name),d+offset,minimum,maximum,scale==1.0?0:3,
                [channel,member,scale](const auto &c){return double(c.dlia[channel].*member)/scale;},
                [channel,member,scale](auto &c,double v){c.dlia[channel].*member=static_cast<std::remove_reference_t<decltype(c.dlia[channel].*member)>>(v*scale);});
        };
        df("解调参考 / Hz","Demodulation reference / Hz","Reference",0x10,0.001,100000,&FpgaControlConfig::Dlia::referenceMilliHz,1000);
        df("1f 相位 / 整圈码","1f phase / turn code","Phase1",0x14,0,4294967295.0,&FpgaControlConfig::Dlia::phase1);
        df("2f 相位校正 / 整圈码","2f phase correction / turn code","Phase2",0x18,0,4294967295.0,&FpgaControlConfig::Dlia::phase2);
        df("解调输出率 / Hz","Demodulation output / Hz","Output",0x4c,1,1000000,&FpgaControlConfig::Dlia::outputRateHz);
        df("解调模式 (2/3；6/7 旁路低通)","Mode (2/3; 6/7 bypass LPF)","Mode",0x20,0,7,&FpgaControlConfig::Dlia::mode);
        df("上传分片点数","Fragment points","FragmentPoints",0x40,1,256,&FpgaControlConfig::Dlia::fragmentPoints);
        auto dacf=[&,channel](const char *zh,const char *en,const char *name,quint32 offset,double minimum,double maximum,auto member) {
            field(grid,row++,zh,en,QString("fpga%1Dac%2").arg(channel).arg(name),quint32(0x3000+channel*0x100)+offset,minimum,maximum,0,
                [channel,member](const auto &c){return double(c.dac[channel].*member);},
                [channel,member](auto &c,double v){c.dac[channel].*member=static_cast<std::remove_reference_t<decltype(c.dac[channel].*member)>>(v);},channel*2);
        };
        dacf("DAC 控制码","DAC control code","Control",0x10,0,1023,&FpgaControlConfig::Dac::deviceCtrl);
        dacf("DAC 清除码","DAC clear code","Clear",0x14,0,1048575,&FpgaControlConfig::Dac::clearCode);
        dacf("DAC 下限码","DAC minimum code","Min",0x18,0,1048575,&FpgaControlConfig::Dac::minCode);
        dacf("DAC 上限码","DAC maximum code","Max",0x1c,0,1048575,&FpgaControlConfig::Dac::maxCode);
        dacf("请求 SPI 频率 / Hz","Requested SPI clock / Hz","Spi",0x3c,1,20000000,&FpgaControlConfig::Dac::spiClockHz);
        dacf("DAC 增益 / UQ2.30","DAC gain / UQ2.30","Gain",0x34,0,4294967295.0,&FpgaControlConfig::Dac::gain);
        dacf("DAC 偏移码","DAC offset code","Offset",0x38,-2147483648.0,2147483647.0,&FpgaControlConfig::Dac::offset);
        const quint32 dac=0x3000+channel*0x100;
        readback(grid,row++,1+channel*2,2,w+8,QStringLiteral("WMS 状态"),QStringLiteral("WMS status"),"WMS");
        readback(grid,row++,1+channel*2,2,w+12,QStringLiteral("WMS 错误寄存器"),QStringLiteral("WMS error register"));
        readback(grid,row++,1+channel*2,2,dac+8,QStringLiteral("DAC 状态"),QStringLiteral("DAC status"),"DAC");
        readback(grid,row++,1+channel*2,2,dac+12,QStringLiteral("DAC 错误寄存器"),QStringLiteral("DAC error register"));
        readback(grid,row++,1+channel*2,2,w+0x3c,QStringLiteral("波形饱和次数"),QStringLiteral("Waveform saturation count"));
        readback(grid,row++,1+channel*2,2,w+0x44,QStringLiteral("更新超限次数"),QStringLiteral("Update overrun count"));
        readback(grid,row++,1+channel*2,2,dac+0x28,QStringLiteral("DAC 写入次数"),QStringLiteral("DAC write count"));
        readback(grid,row++,1+channel*2,2,dac+0x2c,QStringLiteral("SPI 错误次数"),QStringLiteral("SPI error count"));
        for(quint32 base : {quint32(0x4000+channel*0x100),d}) {
            const QString module=base==d?"DLIA":"ADC";
            readback(dliaGrid,++dliaRow,1,2,base+8,module+QStringLiteral(" 状态"),module+" status",module);
            readback(dliaGrid,++dliaRow,1,2,base+12,module+QStringLiteral(" 错误寄存器"),module+" error register");
        }
        readback(dliaGrid,++dliaRow,1,2,0x4024+channel*0x100,QStringLiteral("ADC 丢样次数"),QStringLiteral("ADC sample drop count"));
        readback(dliaGrid,++dliaRow,1,2,d+0x34,QStringLiteral("滤波饱和次数"),QStringLiteral("Filter saturation count"));
        readback(dliaGrid,++dliaRow,1,2,d+0x48,QStringLiteral("帧容量丢弃次数"),QStringLiteral("Frame capacity drop count"));
        readback(dliaGrid,++dliaRow,1,2,d+0x50,QStringLiteral("生效抽取系数"),QStringLiteral("Active decimation factor"));
    }
    dliaChannels->setStretch(0,1);dliaChannels->setStretch(1,1);
    wmsLayout->addStretch();dliaLayout->addStretch();
    pl->addStretch();
    auto *sensors=tab(QStringLiteral("传感器"),QStringLiteral("Sensors")); auto *sensorLayout=new QVBoxLayout(sensors);
    auto *sensorCard=card(sensors,sensorLayout,QStringLiteral("传感器状态"),QStringLiteral("Sensor status"),QStringLiteral("activity"));
    auto *sensorGrid=new QGridLayout(sensorCard);sensorGrid->setObjectName("fpgaSensorGrid");sensorGrid->setSpacing(12);sensorGrid->setColumnStretch(1,1);
    const char *names[]={"PTB210","HMP3","EPSILON","BMP390","SHT45","TFA1500-L"}; const quint16 sources[]={0x40,0x41,0x42,0x43,0x44,0x45};
    for(int i=0;i<6;++i) {
        const auto source=sources[i]; const auto suffix=QString::number(source,16).rightJustified(4,QChar('0'));
        auto *name=new QLabel(QString::fromLatin1(names[i]),sensorCard); name->setObjectName("fpgaSensorName"+suffix);
        auto nameFont=name->font(); nameFont.setBold(true); name->setFont(nameFont); name->setAlignment(Qt::AlignLeft|Qt::AlignVCenter);
        sensorGrid->addWidget(name,i,0);
        auto *l=new ConnectionStatusLabel(sensorCard); l->setObjectName("fpgaSensor"+suffix); sensorLabels_[source]=l;
        sensorGrid->addWidget(l,i,1);
        action(sensorGrid,i,2,"启用","Enable","fpgaSensorEnable"+suffix,[this,source]{emit sensorEnableRequested(source,true);});
        action(sensorGrid,i,3,"停用","Disable","fpgaSensorDisable"+suffix,[this,source]{emit sensorEnableRequested(source,false);});
    }
    auto *ai8config=card(sensors,sensorLayout,QStringLiteral("AI8 轮询配置（停用时提交）"),QStringLiteral("AI8 polling configuration (apply while disabled)"),QStringLiteral("sliders-vertical"));
    auto *ai8layout=new QHBoxLayout(ai8config);auto *ai8controls=new QVBoxLayout;auto *ai8grid=new QGridLayout;
    ai8layout->addLayout(ai8controls,1);ai8layout->addLayout(ai8grid,2);
    auto *ai8reading=new QLabel(ai8config);ai8reading->setObjectName("fpgaSensor0046");ai8reading->setWordWrap(true);sensorLabels_[0x46]=ai8reading;ai8controls->addWidget(ai8reading);
    auto *ai8buttons=new QGridLayout;ai8controls->addLayout(ai8buttons);
    action(ai8buttons,0,0,"启用","Enable","",[this]{emit sensorEnableRequested(0x46,true);});
    action(ai8buttons,0,1,"停用","Disable","",[this]{emit sensorEnableRequested(0x46,false);});
    auto *tempGrid=new QGridLayout;ai8controls->addLayout(tempGrid);auto *temperature=new QDoubleSpinBox(ai8config);temperature->setRange(-999.0,2147.4);temperature->setDecimals(1);temperature->setSingleStep(0.1);temperature->setSuffix(" °C");tempGrid->addWidget(temperature,0,0,1,2);
    field(ai8grid,0,"仪表地址","Instrument address","fpgaAi8Address",0x6614,1,80,0,[](const auto &c){return c.ai8.slaveAddress;},[](auto &c,double v){c.ai8.slaveAddress=quint32(v);});
    field(ai8grid,1,"仪表通道","Instrument channel","fpgaAi8Channel",0x661c,1,8,0,[](const auto &c){return c.ai8.channel;},[](auto &c,double v){c.ai8.channel=quint32(v);});
    field(ai8grid,2,"轮询间隔 / ms","Polling interval / ms","fpgaAi8Interval",0x662c,1,3600000,0,[](const auto &c){return c.ai8.pollIntervalMs;},[](auto &c,double v){c.ai8.pollIntervalMs=quint32(v);});
    field(ai8grid,3,"通信超时 / ms","Communication timeout / ms","fpgaAi8Timeout",0x6660,1,65535,0,[](const auto &c){return c.ai8.timeoutMs;},[](auto &c,double v){c.ai8.timeoutMs=quint32(v);});
    field(ai8grid,4,"重试上限","Retry limit","fpgaAi8Retry",0x6664,0,3,0,[](const auto &c){return c.ai8.retryLimit;},[](auto &c,double v){c.ai8.retryLimit=quint32(v);});
    auto *ai8hint=new QLabel(ai8config);label(ai8hint,QStringLiteral("共享总线固定 19200 / 8N1。保留仪表报警与超时；不会自动修改仪表参数。"),QStringLiteral("Shared bus: 19200 / 8N1. Instrument alarms and timeouts are retained; instrument parameters are not automatically changed."));ai8hint->setWordWrap(true);ai8grid->addWidget(ai8hint,5,0,1,3);
    action(tempGrid,1,0,"设置 AI8 温度并读回","Set AI8 temperature & verify","fpgaSetTemperature",[this,temperature]{emit setTemperatureRequested(temperature->value());});ai8controls->addStretch();sensorLayout->addStretch();
    auto *waves=tab(QStringLiteral("波形"),QStringLiteral("Waveforms")); auto *waveLayout=new QVBoxLayout(waves);
    auto *waveHint=new QLabel(waves); label(waveHint,QStringLiteral("ADC 未确认前端校准时显示原码；解调显示数字幅值，不代表气体浓度。"),QStringLiteral("ADC displays raw codes until frontend calibration is confirmed. Demodulation displays digital amplitude, not concentration.")); waveHint->setWordWrap(true); waveLayout->addWidget(waveHint);
    auto *faults=new QHBoxLayout;waveLayout->addLayout(faults);
    for(const auto &fault:QList<QPair<QString,QString>>{{"missing_cycle",QStringLiteral("模拟单周期丢片")},{"latch_dlia",QStringLiteral("模拟 DLIA 异常锁存")}}) {
        auto *button=new QPushButton(waves);button->setObjectName("fpgaSimulate_"+fault.first);
        label(button,fault.second,fault.first=="missing_cycle"?"Simulate one missing fragment":"Simulate latched DLIA fault");
        button->setProperty("fpgaSimulationControl",true);button->hide();faults->addWidget(button);
        connect(button,&QPushButton::clicked,this,[this,op=fault.first]{simulateOperation({{"op",op}});});
    }
    auto *waveChannels=new QHBoxLayout;waveChannels->setObjectName("fpgaWaveChannels");waveChannels->setSpacing(12);waveLayout->addLayout(waveChannels);
    for(int i=0;i<2;++i) {
        auto *g=card(waves,waveChannels,QString("ADC %1 / DLIA %1").arg(i),QString("ADC %1 / DLIA %1").arg(i),QStringLiteral("audio-waveform")); auto *l=new QVBoxLayout(g);
        auto *frontend=new QGridLayout; l->addLayout(frontend);
        field(frontend,0,"电压系数 / V·code⁻¹","Voltage scale / V per code",QString("fpga%1Scale").arg(i),0,0.000000000001,100,12,[i](const auto &c){return c.frontend[i].voltsPerCode;},[i](auto &c,double v){c.frontend[i].voltsPerCode=v;});
        field(frontend,1,"极性 (+1 / -1)","Polarity (+1 / -1)",QString("fpga%1Polarity").arg(i),0,-1,1,0,[i](const auto &c){return c.frontend[i].polarity;},[i](auto &c,double v){c.frontend[i].polarity=v<0?-1:1;});
        auto *cal=new FpgaCheckBox(g); label(cal,QStringLiteral("已确认本路前端校准"),QStringLiteral("Frontend calibration confirmed")); cal->setObjectName(QString("fpga%1Calibrated").arg(i)); l->addWidget(cal);
        connect(cal,&QCheckBox::toggled,this,[this,i](bool v){config_.frontend[i].calibrated=v;emit configurationChanged(configuration());});
        for(quint16 source : {quint16(0x20+i),quint16(0x30+i)}) {
            auto *summary=new QLabel(g);summary->setObjectName(QString("fpgaWaveSummary%1").arg(source));summary->setWordWrap(true);waveLabels_[source]=summary;l->addWidget(summary);
            auto *plot=new SamplePlot(g);plot->setObjectName(QString("fpgaWavePlot%1").arg(source));plots_[source]=plot;
            if(source>=0x30) {
                auto *pair=new QGridLayout;l->addLayout(pair);
                auto *firstTitle=new QLabel("H1",g);auto *secondTitle=new QLabel("H2",g);
                firstPlotTitles_[source]=firstTitle;secondPlotTitles_[source]=secondTitle;
                auto *second=new SamplePlot(g);second->setObjectName(QString("fpgaWaveSecondPlot%1").arg(source));secondPlots_[source]=second;
                pair->addWidget(firstTitle,0,0);pair->addWidget(plot,1,0);
                pair->addWidget(secondTitle,2,0);pair->addWidget(second,3,0);
                pair->setColumnStretch(0,1);
                auto *iqPanel=new QWidget(g);iqPanels_[source]=iqPanel;l->addWidget(iqPanel);iqPanel->hide();
                auto *iqPair=new QGridLayout(iqPanel);iqPair->setContentsMargins(0,0,0,0);
                for(int component=0;component<2;++component) {
                    auto *title=new QLabel(component==0?"I2":"Q2",iqPanel);iqPair->addWidget(title,0,component);
                    auto *iqPlot=new SamplePlot(iqPanel);iqPlot->setObjectName(QString("fpgaWaveIqPlot%1_%2").arg(source).arg(component));
                    iqPlots_[source].append(iqPlot);iqPair->addWidget(iqPlot,1,component);iqPair->setColumnStretch(component,1);
                }
            } else l->addWidget(plot);
        }
    }
    waveChannels->setStretch(0,1);waveChannels->setStretch(1,1);
    auto *advanced=tab(QStringLiteral("高级控制"),QStringLiteral("Advanced controls"));auto *advancedLayout=new QVBoxLayout(advanced);
    auto *advancedSensors=card(advanced,advancedLayout,QStringLiteral("传感器参数（断开后编辑，连接时应用）"),QStringLiteral("Sensor parameters (edit offline; apply on connect)"),QStringLiteral("sliders-horizontal"));
    auto *asl=new QVBoxLayout(advancedSensors);auto *sensorChoice=new VaporView::SingleLevelPopupComboBox(advancedSensors);sensorChoice->setObjectName("fpgaAdvancedSensor");asl->addWidget(sensorChoice);
    auto *sensorStack=new QWidget(advancedSensors);sensorStack->setObjectName("fpgaAdvancedSensorStack");asl->addWidget(sensorStack);
    auto *formsLayout=new QVBoxLayout(sensorStack);formsLayout->setContentsMargins(0,0,0,0);QList<QWidget*> sensorForms;
    for(int index=0;index<6;++index) {
        const auto source=sources[index];sensorChoice->addItem(QString::fromLatin1(names[index]),source);
        auto *body=new QWidget(sensorStack);formsLayout->addWidget(body);sensorForms.push_back(body);body->setVisible(index==0);
        auto *grid=new QGridLayout(body);int row=0;
        for(const auto& spec:VaporView::FpgaRemote::sensorParameters())if(spec.source==source) {
            auto *title=new QLabel(QString::fromUtf8(spec.label),body);title->setWordWrap(true);grid->addWidget(title,row,0);
            auto *input=new QDoubleSpinBox(body);input->setObjectName(QString("fpgaSensorParameter%1").arg(spec.address,4,16,QChar('0')));input->setDecimals(0);input->setRange(spec.minimum,spec.maximum);input->setValue(spec.fallback);input->setKeyboardTracking(false);grid->addWidget(input,row,1);sensorParameterInputs_[spec.address]=input;
            auto *actual=new QLabel(body);actual->setWordWrap(true);grid->addWidget(actual,row++,2);hardwareLabels_[spec.address]=actual;
            connect(input,&QDoubleSpinBox::valueChanged,this,[this,spec](double value){config_.sensorParameters[spec.source][spec.key]=value;emit configurationChanged(configuration());});
        }
        grid->setColumnStretch(2,1);grid->setRowStretch(row,1);
    }
    connect(sensorChoice,&QComboBox::currentIndexChanged,sensorStack,[sensorStack,sensorForms](int current) {
        for(int i=0;i<sensorForms.size();++i)sensorForms[i]->setVisible(i==current);
        sensorStack->updateGeometry();
    });
    auto number=[this](QGridLayout *grid,int row,const char *title,const char *name,double low,double high,double value,int decimals=0) {
        auto *label=new QLabel(QString::fromUtf8(title),this);label->setWordWrap(true);grid->addWidget(label,row,0);
        auto *spin=new QDoubleSpinBox(this);spin->setObjectName(QString::fromLatin1(name));spin->setRange(low,high);spin->setDecimals(decimals);spin->setValue(value);spin->setKeyboardTracking(false);grid->addWidget(spin,row,1);return spin;
    };
    auto operation=[this,&action](QGridLayout *grid,int row,int col,const char *zh,const char *en,const char *name,std::function<QJsonObject()> request) {
        return action(grid,row,col,zh,en,QString::fromLatin1(name),[this,request]{emit operationRequested(request());});
    };
    struct ActualField { quint32 address; const char *zh; const char *en; };
    auto readbacks=[&readback](QGridLayout *grid,int row,std::initializer_list<ActualField> fields) {
        for(const auto& field:fields)readback(grid,row++,1,2,field.address,QString::fromUtf8(field.zh),QString::fromUtf8(field.en));
    };
    auto *lanes=card(advanced,advancedLayout,QStringLiteral("分通道采集"),QStringLiteral("Acquisition per channel"),QStringLiteral("activity"));auto *laneGrid=new QGridLayout(lanes);
    for(int ch=0;ch<2;++ch)for(int enable=0;enable<2;++enable)operation(laneGrid,ch,enable,ch?(enable?"通道 1 开始":"通道 1 停止"):(enable?"通道 0 开始":"通道 0 停止"),enable?"Start channel":"Stop channel",qPrintable(QString("fpgaChannel%1%2").arg(ch).arg(enable)),[ch,enable]{return QJsonObject{{"op","channel_acquisition"},{"channel",ch},{"enabled",bool(enable)}};});
    auto *time=card(advanced,advancedLayout,QStringLiteral("时间同步"),QStringLiteral("Time synchronization"),QStringLiteral("timer"));auto *tg=new QGridLayout(time);
    auto *edge=number(tg,0,"同步沿 (0 上升 / 1 下降)","fpgaTimeEdge",0,1,0);auto *timeout=number(tg,1,"同步超时 / ms","fpgaTimeTimeout",1,3600000,2000);
    operation(tg,0,2,"设置同步沿","Set edge","fpgaTimeSetEdge",[edge]{return QJsonObject{{"op","time"},{"action",3},{"value",edge->value()}};});
    operation(tg,1,2,"设置同步超时","Set timeout","fpgaTimeSetTimeout",[timeout]{return QJsonObject{{"op","time"},{"action",4},{"value",timeout->value()}};});
    operation(tg,2,0,"启用同步","Enable sync","fpgaTimeEnable",[]{return QJsonObject{{"op","time"},{"action",1},{"value",1}};});operation(tg,2,1,"停用同步","Disable sync","fpgaTimeDisable",[]{return QJsonObject{{"op","time"},{"action",1},{"value",0}};});operation(tg,2,2,"重新捕获同步沿","Rearm sync","fpgaTimeRearm",[]{return QJsonObject{{"op","time"},{"action",2},{"value",0}};});
    auto *tagLow=number(tg,3,"GNSS 标签低 32 位","fpgaTimeTagLow",0,4294967295.0,0);auto *tagHigh=number(tg,4,"GNSS 标签高 32 位","fpgaTimeTagHigh",0,4294967295.0,0);operation(tg,4,2,"写入 GNSS 标签","Write GNSS tag","fpgaTimeTag",[tagLow,tagHigh]{return QJsonObject{{"op","time_tag"},{"low",tagLow->value()},{"high",tagHigh->value()}};});
    readbacks(tg,5,{{0x1004,"同步控制","Sync control"},{0x1008,"同步状态","Sync status"},{0x100c,"同步错误","Sync errors"},
        {0x1010,"当前同步沿","Actual sync edge"},{0x1014,"当前同步超时","Actual sync timeout"},{0x1038,"当前 GNSS 标签低位","Actual GNSS tag low"},
        {0x103c,"当前 GNSS 标签高位","Actual GNSS tag high"},{0x1040,"GNSS 标签有效","GNSS tag valid"}});
    auto *motor=card(advanced,advancedLayout,QStringLiteral("步进电机（仅显式运行）"),QStringLiteral("Stepper motor (explicit run only)"),QStringLiteral("settings"));auto *mg=new QGridLayout(motor);
    auto *frequency=number(mg,0,"步进频率 / Hz","fpgaMotorFrequency",1,196078,1000);auto *high=number(mg,1,"高电平时间 / 10 ns","fpgaMotorHigh",255,4294967295.0,255);auto *dir=number(mg,2,"方向建立时间 / 10 ns","fpgaMotorDir",505,4294967295.0,505);auto *polarity=number(mg,3,"极性掩码 (STEP / DIR / EN)","fpgaMotorPolarity",0,7,0);
    operation(mg,3,2,"应用电机参数并停用","Configure and disable","fpgaMotorConfigure",[=]{return QJsonObject{{"op","motor_config"},{"frequency",frequency->value()},{"high_ticks",high->value()},{"dir_ticks",dir->value()},{"polarity",polarity->value()}};});
    auto *steps=number(mg,4,"相对运动脉冲数 (负数反向)","fpgaMotorSteps",-2147483648.0,2147483647.0,0);
    operation(mg,5,0,"启用电机","Enable motor","fpgaMotorEnable",[]{return QJsonObject{{"op","motor"},{"action",4},{"value",1}};});operation(mg,5,1,"运行相对运动","Run relative move","fpgaMotorRun",[steps]{return QJsonObject{{"op","motor"},{"action",1},{"value",steps->value()}};});operation(mg,5,2,"停止电机","Stop motor","fpgaMotorStop",[]{return QJsonObject{{"op","motor"},{"action",2},{"value",0}};});operation(mg,6,0,"停用电机","Disable motor","fpgaMotorDisable",[]{return QJsonObject{{"op","motor"},{"action",4},{"value",0}};});operation(mg,6,1,"位置归零","Zero position","fpgaMotorZero",[]{return QJsonObject{{"op","motor"},{"action",3},{"value",0}};});
    readbacks(mg,7,{{0x6704,"电机控制","Motor control"},{0x6708,"电机状态","Motor status"},{0x670c,"电机错误","Motor errors"},
        {0x6714,"当前步进频率","Actual step frequency"},{0x6718,"当前高电平时间","Actual high time"},{0x671c,"当前方向建立时间","Actual direction setup"},
        {0x6724,"电机位置原码","Motor position raw"},{0x6728,"剩余脉冲","Remaining pulses"},{0x6730,"当前极性","Actual polarity"}});
    auto *maintenance=card(advanced,advancedLayout,QStringLiteral("设备维护与上传"),QStringLiteral("Maintenance and upload"),QStringLiteral("settings"));auto *maintenanceGrid=new QGridLayout(maintenance);
    auto *module=new VaporView::SingleLevelPopupComboBox(maintenance);module->setObjectName("fpgaMaintenanceModule");maintenanceGrid->addWidget(module,0,0,1,2);
    for(auto pair:{qMakePair("时间同步",2),qMakePair("WMS0 / DAC0",0x10),qMakePair("WMS1 / DAC1",0x11),qMakePair("ADC0",0x20),qMakePair("ADC1",0x21),qMakePair("DLIA0",0x30),qMakePair("DLIA1",0x31),qMakePair("PTB210",0x40),qMakePair("HMP3",0x41),qMakePair("EPSILON",0x42),qMakePair("BMP390",0x43),qMakePair("SHT45",0x44),qMakePair("TFA1500-L",0x45),qMakePair("AI8",0x46),qMakePair("步进电机",0x47)})module->addItem(QString::fromUtf8(pair.first),pair.second);
    operation(maintenanceGrid,1,0,"复位所选模块","Reset selected module","fpgaModuleReset",[module]{return QJsonObject{{"op","maintenance"},{"source",module->currentData().toInt()},{"action",8},{"value",0}};});operation(maintenanceGrid,1,1,"清除所选模块错误","Clear selected errors","fpgaModuleClearErrors",[module]{return QJsonObject{{"op","maintenance"},{"source",module->currentData().toInt()},{"action",9},{"value",0}};});
    operation(maintenanceGrid,2,0,"清空所选传感器 FIFO","Clear sensor FIFO","fpgaSensorClearFifo",[module]{return QJsonObject{{"op","sensor_action"},{"source",module->currentData().toInt()},{"action",3},{"value",0}};});
    auto *pressure=number(maintenanceGrid,3,"HMP 压力补偿 / hPa","fpgaHmpPressure",0.01,2000,1013.25,2);operation(maintenanceGrid,3,2,"写入并确认 HMP 补偿","Write HMP compensation","fpgaHmpSetPressure",[pressure]{float value=float(pressure->value());quint32 bits;std::memcpy(&bits,&value,4);return QJsonObject{{"op","sensor_action"},{"source",0x41},{"action",16},{"value",double(bits)}};});
    auto *tfaCommand=number(maintenanceGrid,4,"TFA LF 命令 (1 连续 / 2 单次 / 3 停止 / 4 版本)","fpgaTfaCommand",1,4,2);operation(maintenanceGrid,4,2,"发送 LF 命令","Send LF command","fpgaTfaSendCommand",[tfaCommand]{return QJsonObject{{"op","sensor_action"},{"source",0x45},{"action",33},{"value",tfaCommand->value()}};});
    auto *watchdog=number(maintenanceGrid,5,"看门狗超时 / ms (0 关闭，启用至少 30000)","fpgaWatchdogTimeout",0,3600000,0);operation(maintenanceGrid,5,2,"设置看门狗","Set watchdog","fpgaWatchdogSet",[watchdog]{return QJsonObject{{"op","watchdog"},{"value",watchdog->value()}};});
    auto *arbiter=number(maintenanceGrid,6,"仲裁模式 (0 优先级 / 1 轮转)","fpgaArbiter",0,1,0);auto *highWater=number(maintenanceGrid,7,"上传高水位 / 字节","fpgaHighWater",1,4294967295.0,4096);auto *burst=number(maintenanceGrid,8,"高优先级突发上限","fpgaHighBurst",1,65535,8);operation(maintenanceGrid,8,2,"应用上传参数","Apply stream settings","fpgaStreamConfigure",[=]{return QJsonObject{{"op","stream_config"},{"mode",arbiter->value()},{"high_water",highWater->value()},{"burst",burst->value()}};});
    readbacks(maintenanceGrid,9,{{0x0040,"当前看门狗超时","Actual watchdog timeout"},{0x6140,"HMP 补偿读回原码","HMP compensation raw"},
        {0x6148,"HMP 补偿确认次数","HMP compensation confirmations"},{0x7010,"当前仲裁模式","Actual arbitration mode"},
        {0x7018,"当前高水位","Actual high-water mark"},{0x7034,"当前突发上限","Actual burst limit"}});
    advancedLayout->addStretch();
    auto *recordingTab=tab(QStringLiteral("记录与诊断"),QStringLiteral("Recording & diagnostics")); auto *recordingLayout=new QVBoxLayout(recordingTab);
    auto *recordingCards=new QHBoxLayout;recordingCards->setObjectName("fpgaRecordingCards");recordingCards->setSpacing(12);recordingLayout->addLayout(recordingCards);
    auto *recording=card(recordingTab,recordingCards,QStringLiteral("采集记录"),QStringLiteral("Acquisition recording"),QStringLiteral("scroll-text")); auto *rl=new QVBoxLayout(recording);
    recordSensors_=new FpgaCheckBox(recording); label(recordSensors_,QStringLiteral("记录传感器"),QStringLiteral("Record sensors")); recordDlia_=new FpgaCheckBox(recording); label(recordDlia_,QStringLiteral("记录解调数据"),QStringLiteral("Record demodulation")); recordRaw_=new FpgaCheckBox(recording); label(recordRaw_,QStringLiteral("记录 RAW（高吞吐）"),QStringLiteral("Record RAW (high throughput)")); rl->addWidget(recordSensors_); rl->addWidget(recordDlia_); rl->addWidget(recordRaw_);
    for(auto *c : {recordSensors_,recordDlia_,recordRaw_}) connect(c,&QCheckBox::toggled,this,[this]{emit configurationChanged(configuration());});
    auto *rawGrid=new QGridLayout; rl->addLayout(rawGrid);
    action(rawGrid,0,0,"开始 RAW","Start RAW","fpgaRawStart",[this]{emit rawRequested(true);}); action(rawGrid,0,1,"停止 RAW","Stop RAW","fpgaRawStop",[this]{emit rawRequested(false);});
    record_=new QPushButton(recording); record_->setObjectName("fpgaRecord"); rl->addWidget(record_); connect(record_,&QAbstractButton::clicked,this,[this]{emit recordingRequested(!recording_);});
    recordStatus_=new QLabel(recording); recordStatus_->setWordWrap(true); rl->addWidget(recordStatus_);
    auto *files=new QHBoxLayout; rl->addLayout(files);
    auto *replay=new QPushButton(recording);replay->setObjectName("fpgaReplay"); label(replay,QStringLiteral("打开离线记录"),QStringLiteral("Open offline recording")); files->addWidget(replay);
    auto *exportButton=new QPushButton(recording);exportButton->setObjectName("fpgaExport"); label(exportButton,QStringLiteral("导出记录"),QStringLiteral("Export recording")); files->addWidget(exportButton);
    connect(replay,&QPushButton::clicked,this,[this]{emit replayRequested({});});
    connect(exportButton,&QPushButton::clicked,this,[this]{emit exportRequested({});});
    auto *diagnosticCard=card(recordingTab,recordingCards,QStringLiteral("诊断日志"),QStringLiteral("Diagnostics"),QStringLiteral("list-filter"));
    auto *diagnosticLayout=new QVBoxLayout(diagnosticCard);
    auto *managementScroll=new QScrollArea(diagnosticCard);managementScroll->setObjectName("fpgaManagementScroll");managementScroll->setWidgetResizable(true);managementScroll->setFrameShape(QFrame::NoFrame);
    managementStatus_=new QLabel(managementScroll);managementStatus_->setObjectName("fpgaManagementState");managementStatus_->setWordWrap(true);managementStatus_->setAlignment(Qt::AlignLeft|Qt::AlignTop);managementScroll->setWidget(managementStatus_);diagnosticLayout->addWidget(managementScroll);
    diagnostics_=new QPlainTextEdit(diagnosticCard); diagnostics_->setObjectName("fpgaDiagnostics"); diagnostics_->setReadOnly(true); diagnostics_->setMaximumBlockCount(2000); diagnostics_->setMinimumHeight(180); diagnosticLayout->addWidget(diagnostics_,1);
    recordingCards->setStretch(0,1);recordingCards->setStretch(1,1);recordingLayout->addStretch();
    hint->setProperty("fpgaSecondaryText",true); waveHint->setProperty("fpgaSecondaryText",true); ai8hint->setProperty("fpgaSecondaryText",true);
    status_->setProperty("fpgaSecondaryText",true); recordStatus_->setProperty("fpgaSecondaryText",true);
    for (auto *body : findChildren<QWidget*>(QStringLiteral("fpgaCardBody"))) {
        body->layout()->setContentsMargins(12,10,12,12); body->layout()->setSpacing(8);
    }
    for (auto *body : {parameters,sensors,waves,advanced,recordingTab}) {
        body->layout()->setProperty("fpgaCardOuterMargins",true);
        body->layout()->setContentsMargins(18,12,5,8); body->layout()->setSpacing(12);
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
    c.backend=backend_->currentData().toString();c.pressureSource=0x40; c.recordSensors=recordSensors_->isChecked();c.recordDlia=recordDlia_->isChecked();c.recordRaw=recordRaw_->isChecked();return c;
}
void FpgaControlPage::setConfiguration(const FpgaControlConfig &c) {
    config_=c;
    for(const auto& spec:VaporView::FpgaRemote::sensorParameters())if(auto *input=sensorParameterInputs_.value(spec.address)){const QSignalBlocker block(input);input->setValue(c.sensorParameters.value(spec.source).value(spec.key).toDouble(hardwareValues_.value(spec.address,spec.fallback)));}
    for(const auto &f:fields_){const QSignalBlocker block(f.input);f.input->setValue(f.read(c));}
    {const QSignalBlocker block(backend_);backend_->setCurrentIndex(backend_->findData(c.backend));}
    const QSignalBlocker bs(recordSensors_),bd(recordDlia_),br(recordRaw_);
    recordSensors_->setChecked(c.recordSensors);recordDlia_->setChecked(c.recordDlia);recordRaw_->setChecked(c.recordRaw);
    for(int i=0;i<2;++i){auto *cal=findChild<QCheckBox*>(QString("fpga%1Calibrated").arg(i));const QSignalBlocker b(cal);cal->setChecked(c.frontend[i].calibrated);}
    updateActions();
}
void FpgaControlPage::lockOperation(){busy_=true;updateActions();}
void FpgaControlPage::updateActions(){
    for(const auto& spec:VaporView::FpgaRemote::sensorParameters())if(auto *input=sensorParameterInputs_.value(spec.address))input->setEnabled(!busy_ && (!connected_ || (hardwareValues_.contains(0x6004+(spec.source-0x40)*0x100) && !(hardwareValues_.value(0x6004+(spec.source-0x40)*0x100)&1))));
    locator_->setToolTip(locator_->currentText()+"\n"+(backend_->currentData().toString()==QStringLiteral("simulator")
        ?text("本地 VLP1 协议模拟器，不打开实体 USB。","Local VLP1 protocol simulator; no physical USB access.")
        :text("选择 GP01 FX3（04B4:00F1），插拔后点击刷新设备。连接时核验固件身份。","Select GP01 FX3 (04B4:00F1); refresh after plugging/unplugging. Firmware identity is verified on connect.")));
    auto selected=locator_->currentData(Qt::UserRole+1).toJsonObject();
    const auto devices=usbDiscovery_.value("devices").toArray();
    if(locator_->currentIndex()==0 && devices.size()==1) selected=devices.first().toObject();
    const auto backend=backend_->currentData().toString();
    const bool usable=property("fpgaUiTestMode").toBool() || ((!usbRemote_||backend!="simulator")&&selected.value("available").toBool()&&!selected.value("locator").toString().isEmpty()
        &&(backend=="auto"||backend==selected.value("backend").toString())&&usbDiscovery_.value("error").toString().isEmpty());
    connect_->setEnabled(!connected_&&!busy_&&!usbDiscovering_&&usbHostAvailable_&&usable);
    locator_->setEnabled(!connected_&&!busy_&&!usbDiscovering_&&usbHostAvailable_);
    backend_->setEnabled(!connected_&&!busy_&&!usbDiscovering_);
    discover_->setEnabled(!busy_&&!usbDiscovering_&&usbHostAvailable_);
    disconnect_->setEnabled(connected_&&!busy_);for(auto *w:onlineControls_)w->setEnabled(ready_&&!busy_);refresh_->setEnabled(connected_&&!busy_);record_->setEnabled(!property("fpgaUiTestMode").toBool()&&(ready_||recording_));
}
void FpgaControlPage::showEvent(QShowEvent *event){
    QWidget::showEvent(event);
    if(usbHostAvailable_&&!usbDiscovering_&&!busy_&&!connected_)requestUsbDiscovery();
}
void FpgaControlPage::requestUsbDiscovery(){
    if(!usbHostAvailable_||usbDiscovering_||busy_)return;
    ++usbDiscoveryGeneration_;usbDiscovering_=true;
    discoveryStatus_->setText(text("正在查找…","Searching…"));discoveryStatus_->setToolTip(discoveryStatus_->text());updateActions();emit discoverDevicesRequested();
}
void FpgaControlPage::setUsbDiscoveryHost(bool remote,bool available){
    if(usbRemote_==remote&&usbHostAvailable_==available)return;
    usbRemote_=remote;usbHostAvailable_=available;usbDiscovering_=false;++usbDiscoveryGeneration_;usbDiscovery_={};
    if(auto *model=qobject_cast<QStandardItemModel*>(backend_->model()))
        model->item(backend_->findData(QStringLiteral("simulator")))->setEnabled(!remote);
    rebuildUsbDevices();
    if(isVisible()&&available&&!busy_)requestUsbDiscovery();
}
void FpgaControlPage::setUsbDiscoveryResult(const QJsonObject &result){
    usbDiscovery_=result;usbDiscovering_=false;rebuildUsbDevices();
}
void FpgaControlPage::rebuildUsbDevices(){
    const auto previous=locator_->currentData().toString();
    const QSignalBlocker block(locator_);locator_->clear();
    locator_->addItem(text("自动选择唯一 GP01 设备","Select the only GP01 device automatically"),QString());
    const auto devices=usbDiscovery_.value("devices").toArray();
    for(const auto &value:devices){
        const auto device=value.toObject();
        QString title=device.value("label").toString()+" · "+device.value("driver").toString();
        if(!device.value("available").toBool())title+=text("（驱动后端不可用）"," (driver backend unavailable)");
        locator_->addItem(title,device.value("locator").toString());
        const int index=locator_->count()-1;locator_->setItemData(index,device,Qt::UserRole+1);
        locator_->setItemData(index,title+"\n"+device.value("locator").toString(),Qt::ToolTipRole);
    }
    int index=previous.isEmpty()?-1:locator_->findData(previous);
    if(index<0)index=devices.size()==1?1:0;
    locator_->setCurrentIndex(index);
    const bool simulated=backend_->currentData().toString()==QStringLiteral("simulator");
    const QString host=text(simulated?"本地模拟器":usbRemote_?"天空端 USB":"本机 USB",
                            simulated?"Local simulator":usbRemote_?"Sky USB":"Local USB");
    const auto error=usbDiscovery_.value("error").toString();
    discoveryStatus_->setText(!usbHostAvailable_?text("请先连接 SkyCore","Connect to SkyCore first")
        :!error.isEmpty()?host+" · "+error:!usbDiscovery_.contains("devices")?host+text(" · 等待枚举"," · Awaiting discovery")
        :devices.isEmpty()?host+text(" · 未发现 04B4:00F1"," · No 04B4:00F1 found")
        :host+text(" · %1 台设备"," · %1 device(s)").arg(devices.size()));
    discoveryStatus_->setToolTip(discoveryStatus_->text());
    updateActions();
}
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
    if(!usbRemote_&&backend_->currentData().toString()==QStringLiteral("simulator"))
        status_->setText(text("协议模拟 · ","Protocol simulation · ")+status_->text());
    status_->setText(status_->text()+(busy?text(" · 正在处理"," · Working"):QString())+(detail.isEmpty()?QString():" · "+detail));
    status_->setToolTip(status_->text());
    if(lost){for(auto it=latestReadings_.cbegin();it!=latestReadings_.cend();++it)historicalReadings_.insert(it.key());for(auto it=latestWaves_.cbegin();it!=latestWaves_.cend();++it)historicalWaves_.insert(it.key());refreshMeasurements();}
    updateActions();
}
void FpgaControlPage::setHardwareValues(const QMap<quint32,quint32> &v){
    hardwareValues_=v;
    for(const auto& spec:VaporView::FpgaRemote::sensorParameters())if(!config_.sensorParameters.value(spec.source).contains(spec.key) && v.contains(spec.address))if(auto *input=sensorParameterInputs_.value(spec.address)){const QSignalBlocker block(input);input->setValue(v.value(spec.address));}
    updateActions();
    bool rawChanged=false;
    for(int ch=0;ch<2;++ch)if(v.contains(0x403c+ch*0x100)&&v.contains(0x50)) {
        const quint16 source=0x20+ch;
        const bool enabled=(v.value(0x403c+ch*0x100)&1)&&(v.value(0x50)&(1u<<ch));
        rawChanged|=!rawEnabled_.contains(source)||rawEnabled_.value(source)!=enabled;
        rawEnabled_[source]=enabled;
    }
    if(rawChanged)refreshMeasurements();
    for(auto it=v.cbegin();it!=v.cend();++it)if(auto *l=hardwareLabels_.value(it.key())) {
        const auto module=l->property("fpgaModule").toString();
        const quint32 base=it.key()&~0xffu,offset=it.key()&0xff;
        if ((base==0x2000||base==0x2100) && (offset==0x14||offset==0x18||offset==0x20)) {
            l->setText(text("设备读回：","Readback: ")+QString::number(double(qint32(it.value()))*10.0/2147483648.0,'f',3)+" V");
        } else if((it.key()&0xff)==8 && !module.isEmpty()) {
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
    if(r.source!=0x46)l->setToolTip(l->text());
}
void FpgaControlPage::updateWaveform(const VaporView::FpgaWave::CompletedStream &s){
    auto *plot=static_cast<SamplePlot*>(plots_.value(s.source));auto *l=waveLabels_.value(s.source);if(!plot||!l)return;
    latestWaves_[s.source]=s;if(!rerendering_&&connected_)historicalWaves_.remove(s.source);
    if (!rerendering_) waveTimes_[s.source] = measurementClock_.elapsed();
    const auto historyPrefix=!connected_||historicalWaves_.contains(s.source)?text("离线记录 / 历史值 · ","Offline / historical · "):QString();
    const bool adc=s.source<0x30;const int channel=adc?s.source-0x20:s.source-0x30;const auto c=configuration();const bool volts=adc&&c.frontend[channel].calibrated;
    auto *second=static_cast<SamplePlot*>(secondPlots_.value(s.source));
    const int component=!adc&&s.format==2?4:0;
    const QString pairName=s.format==1?QStringLiteral("I1 / Q1"):QStringLiteral("H1 / H2");
    if(!adc) {
        const auto titles=pairName.split(" / ");
        firstPlotTitles_.value(s.source)->setText(titles.value(0));secondPlotTitles_.value(s.source)->setText(titles.value(1));
        iqPanels_.value(s.source)->setVisible(s.format==1);
    }
    QVector<SamplePlot*> activePlots{plot};if(second)activePlots.append(second);
    for(auto *iq:iqPlots_.value(s.source))activePlots.append(static_cast<SamplePlot*>(iq));
    double maxH2 = std::numeric_limits<double>::quiet_NaN();
    const auto publishDisplay = [&](quint32 totalPoints = 0) {
        QVector<QVector<double>> samples;
        for (auto *p : activePlots) samples.append(p->samples);
        const QStringList components = adc ? QStringList{QStringLiteral("ADC")}
            : s.format == 1 ? QStringList{QStringLiteral("I1"), QStringLiteral("Q1"), QStringLiteral("I2"), QStringLiteral("Q2")}
                            : QStringList{QStringLiteral("H1"), QStringLiteral("H2")};
        emit waveformDisplayUpdated(s.source, samples, components, l->text(), totalPoints ? totalPoints : s.totalPoints, s.preview);
        if (!adc && !rerendering_)
            emit secondHarmonicPeakReady(s.source, s.cycleId, s.timestamp, maxH2, s.preview);
    };
    auto retainRaw=[&](const QString &reason){
        if(!adc||!lastCompleteRaw_.contains(s.source))return false;
        const bool wasRerendering=rerendering_;rerendering_=true;
        const auto savedLatest=latestWaves_.value(s.source);
        updateWaveform(lastCompleteRaw_.value(s.source));
        latestWaves_[s.source]=savedLatest;rerendering_=wasRerendering;
        l->setText(reason+text(" · 显示上一完整 RAW 周期（历史值，非当前测量） · "," · Showing previous complete RAW cycle (historical, not current) · ")+l->text());
        publishDisplay(lastCompleteRaw_.value(s.source).totalPoints);
        return true;
    };
    auto clearPlots=[&]{for(auto *p:activePlots){p->samples.clear();p->setProperty("sampleCount",0);p->update();}};
    const quint32 expectedBytes=adc?4u:s.format==0?8u:s.format==1?16u:s.format==2?24u:0u;
    if(!s.complete||s.partial||s.overflow||s.continuityError) {
        if(retainRaw(text("最近 RAW 周期不完整或质量异常","Latest RAW cycle incomplete or faulty")))return;
        clearPlots();
        l->setText(historyPrefix+text("周期缺片或质量异常；不连接缺失区间。可用原始点保留在记录中。","Cycle has missing fragments or quality faults; gaps are not joined. Available raw points remain in the recording."));publishDisplay();return;
    }
    const quint32 availablePoints = expectedBytes ? quint32(s.pointBytes.size() / expectedBytes) : 0;
    const quint32 displayPoints = s.preview ? availablePoints : s.totalPoints;
    if(!expectedBytes||s.bytesPerPoint!=expectedBytes||!displayPoints
        || s.pointBytes.size() % expectedBytes != 0 || displayPoints > availablePoints
        || (s.preview && (!s.previewStride || displayPoints > 64 || quint64(displayPoints-1)*s.previewStride >= s.totalPoints))) {
        if(retainRaw(text("最近 RAW 格式或长度无效","Latest RAW format or length invalid")))return;
        clearPlots();l->setText(historyPrefix+text("波形格式或长度无效；保留原始记录供诊断","Invalid waveform format or length; raw recording retained for diagnostics"));publishDisplay();return;
    }
    if(adc)lastCompleteRaw_[s.source]=s;
    for(auto *p:activePlots)p->samples.clear();const int stride=qMax(1,int(displayPoints)/1000);const int components=adc?1:int(s.bytesPerPoint/4);
    for(int point=0;point<int(displayPoints);++point){const int offset=point*components*4;if(offset+components*4>s.pointBytes.size())break;
        const double value=adc&&s.format==1?double(qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(s.pointBytes.constData()+offset))):double(qFromLittleEndian<qint32>(reinterpret_cast<const uchar*>(s.pointBytes.constData()+offset+component*4)));
        if (!adc) {
            const auto read = [&](int index) { return double(qFromLittleEndian<qint32>(reinterpret_cast<const uchar*>(s.pointBytes.constData()+offset+index*4))); };
            const double h2 = s.format == 1 ? std::hypot(read(2), read(3)) : read(component+1);
            maxH2 = std::isfinite(maxH2) ? qMax(maxH2, h2) : h2;
        }
        if(point%stride==0) {
            plot->samples.append(volts?value*c.frontend[channel].voltsPerCode*c.frontend[channel].polarity:double(value));
            if(second)second->samples.append(double(qFromLittleEndian<qint32>(reinterpret_cast<const uchar*>(s.pointBytes.constData()+offset+(component+1)*4))));
            if(!adc&&s.format==1)for(int iq=0;iq<2;++iq)
                activePlots[iq+2]->samples.append(double(qFromLittleEndian<qint32>(reinterpret_cast<const uchar*>(s.pointBytes.constData()+offset+(iq+2)*4))));
        }
    }
    const bool valid=s.complete&&!s.partial&&!s.overflow&&!s.continuityError;
    l->setText(QString("%1 · %2 · %3 Hz · %4 %5 · %6%7").arg(adc?QString("ADC %1").arg(channel):QString("DLIA %1").arg(channel))
        .arg(s.totalPoints).arg(s.rate).arg(text("点","points")).arg(volts?"V":text("原码 / 数字幅值","raw / digital amplitude"))
        .arg(text(valid?"完整周期":"异常 / 不完整周期",valid?"Complete cycle":"Invalid / partial cycle"))
        .arg(!adc&&s.format==0&&!s.preview?text(" · 本周期 2f 最大值 "," · Cycle 2f maximum ")+QString::number(maxH2):QString()));
    if(s.preview)l->setText(QString("%1 · %2/%3 %4 · %5 %6 · %7 Hz · %8").arg(text("天空端波形预览","Sky waveform preview"))
        .arg(displayPoints).arg(s.totalPoints).arg(text("点","points")).arg(text("抽稀间隔","stride")).arg(s.previewStride).arg(s.rate)
        .arg(volts?"V":text("原码 / 数字幅值","raw / digital amplitude")));
    if(!adc)l->setText(l->text()+" · "+(s.format==1?QStringLiteral("I1 / Q1 / I2 / Q2"):pairName));
    l->setText(historyPrefix+l->text());
    if(adc&&rawEnabled_.contains(s.source)&&!rawEnabled_.value(s.source))
        l->setText(text("RAW 上传已停止 · 最后完整周期（历史值） · ","RAW upload stopped · Last complete cycle (historical) · ")+l->text());
    for(auto *p:activePlots){p->setProperty("sampleCount",p->samples.size());p->setProperty("previewStride",s.preview?s.previewStride:1);if(!p->samples.isEmpty()){const auto mm=std::minmax_element(p->samples.cbegin(),p->samples.cend());p->setProperty("sampleMinimum",*mm.first);p->setProperty("sampleMaximum",*mm.second);}p->update();}
    publishDisplay();
}
void FpgaControlPage::setManagementState(const QJsonObject& state)
{
    QStringList lines;
    for(auto it=state.begin();it!=state.end();++it) {
        const auto o=it.value().toObject();const auto w=o.value("words").toArray();if(w.size()!=7)continue;
        const int msg=o.value("message").toInt();
        if(msg==0x1400)lines<<text("系统状态 %1；错误汇总 %2；事件丢失 %3；生产端丢失 %4","Status %1; errors %2; event drops %3; producer drops %4")
            .arg(w[0].toDouble()).arg(w[1].toDouble()).arg(w[4].toDouble()).arg(w[5].toDouble());
        if(msg==0x1201)lines<<text("同步序号 %1；GNSS 标签有效 %2","Sync sequence %1; GNSS tag valid %2").arg(w[0].toDouble()).arg(w[5].toDouble());
        if(msg==0x1300)lines<<text("电机位置 %1；剩余 %2；错误 %3","Motor position %1; remaining %2; error %3")
            .arg(qint32(quint32(w[1].toDouble()))).arg(w[2].toDouble()).arg(w[3].toDouble());
        if(msg==0x1401)lines<<text("模块页 0x%1：状态 0x%2；错误 0x%3；存在 %4","Module page 0x%1: status 0x%2; errors 0x%3; present %4")
            .arg(quint32(w[0].toDouble()),2,16,QChar('0')).arg(quint32(w[1].toDouble()),8,16,QChar('0'))
            .arg(quint32(w[2].toDouble()),8,16,QChar('0')).arg(w[3].toDouble());
        if(msg==0x1402)lines<<text("模块页 0x%1：新增错误 0x%2；累计 0x%3","Module page 0x%1: new errors 0x%2; accumulated 0x%3")
            .arg(quint32(w[0].toDouble()),2,16,QChar('0')).arg(quint32(w[2].toDouble()),8,16,QChar('0'))
            .arg(quint32(w[1].toDouble()),8,16,QChar('0'));
    }
    managementStatus_->setText(lines.join(QChar('\n')));
}
void FpgaControlPage::appendDiagnostic(const QString &s){diagnostics_->appendPlainText(s);}
void FpgaControlPage::setLanguage(bool en){english_=en;for(auto *w:findChildren<QWidget*>())if(w->property("fpgaZh").isValid())label(w,w->property("fpgaZh").toString(),w->property("fpgaEn").toString());
    backend_->setItemText(backend_->findData(QStringLiteral("simulator")),text("模拟器","Simulator"));
    for(int i=0;i<tabs_->count();++i)tabs_->setTabText(i,tabs_->widget(i)->property(en?"fpgaEn":"fpgaZh").toString());
    for(auto it=sensorLabels_.cbegin();it!=sensorLabels_.cend();++it)if(!latestReadings_.contains(it.key()))it.value()->setText(text("等待有效样本","Waiting for valid sample"));
    for(auto it=waveLabels_.cbegin();it!=waveLabels_.cend();++it)if(!latestWaves_.contains(it.key()))it.value()->setText(text("等待波形 · ADC 原码 / 解调数字幅值","Waiting for waveform · ADC raw / demodulation digital amplitude"));
    refreshMeasurements();
    if(!usbDiscovering_)rebuildUsbDevices();setConnectionState(ready_,busy_,connectionDetail_);setRecordingState(recording_,recordingDetail_);
}
void FpgaControlPage::refreshMeasurements(){rerendering_=true;const auto readings=latestReadings_;for(const auto &reading:readings)updateSensor(reading);const auto waves=latestWaves_;for(const auto &wave:waves)updateWaveform(wave);rerendering_=false;}
void FpgaControlPage::setTheme(bool dark,int fontScalePercent){
    using namespace VaporView;setProperty(kAppDarkThemeProperty,dark);setPalette(appThemePalette(dark,palette()));
    const qreal scale=qBound(60,fontScalePercent,180)/100.0;
    const auto pixels=[scale](int value){return qRound(value*scale);};
    findChild<QScrollArea*>("fpgaManagementScroll")->setFixedHeight(pixels(120));
    for(auto *widget:findChildren<QWidget*>())if(widget->property("fpgaFixedPlot").toBool())widget->setFixedHeight(pixels(100));
    // Match the home card inset and reserve the same scrollbar rail above the tabs.
    const int shadowInset=int(std::ceil(kTopLevelCardShadowBlurRadius*scale*0.6))+1;
    findChild<QWidget*>("fpgaConnectionContainer")->layout()->setContentsMargins(pixels(18),0,shadowInset+pixels(8),0);
    for(int i=0;i<tabs_->count();++i) {
        auto *scroll=qobject_cast<QScrollArea*>(tabs_->widget(i));
        scroll->widget()->layout()->setContentsMargins(pixels(18),pixels(12),shadowInset,pixels(8));
    }
    setStyleSheet(applyAppThemeTokens(QStringLiteral(
        "QWidget#fpgaControlPage { background-color: @vv-window; }"
        "QWidget#fpgaControlPage QFrame#fpgaSectionCard { background-color: @vv-surface-raised; border: 1px solid @vv-border; border-radius: 12px; }"
        "QWidget#fpgaControlPage QFrame#fpgaSectionCard > QWidget#sectionTitleBar { background-color: @vv-surface-raised; border: none; border-bottom: 1px solid @vv-border; border-top-left-radius: 11px; border-top-right-radius: 11px; min-height: %1px; max-height: %1px; }"
        "QWidget#fpgaControlPage QWidget#fpgaCardBody { background-color: @vv-surface-raised; border: none; border-bottom-left-radius: 11px; border-bottom-right-radius: 11px; }"
        "QWidget#fpgaControlPage QLabel { background-color: transparent; border: none; color: @vv-text; }"
        "QWidget#fpgaControlPage QLabel#sectionTitleLabel { font-size: %2px; font-weight: 700; padding: 0; margin: 0; }"
        "QWidget#fpgaControlPage QLabel[fpgaSecondaryText=\"true\"] { color: @vv-text-secondary; font-weight: 400; }"
        "QWidget#fpgaControlPage QTabWidget::pane { border: none; background-color: @vv-window; }"
        "QWidget#fpgaControlPage QTabWidget#fpgaTabs::tab-bar { left: %7px; }"
        "QWidget#fpgaControlPage QTabBar::tab { background-color: @vv-surface-alt; color: @vv-text; border: 1px solid transparent; border-radius: 6px; padding: 0 %3px; margin-right: 4px; min-height: %4px; font-weight: 600; }"
        "QWidget#fpgaControlPage QTabBar::tab:hover { background-color: @vv-primary-subtle; color: @vv-primary; }"
        "QWidget#fpgaControlPage QTabBar::tab:selected { background-color: @vv-primary; color: @vv-white; }"
        "QWidget#fpgaControlPage QTabBar::tab:focus { border-color: @vv-focus; }"
        "QWidget#fpgaControlPage QScrollArea, QWidget#fpgaControlPage QScrollArea > QWidget, QWidget#fpgaControlPage QScrollArea > QWidget > QWidget { background-color: @vv-window; border: none; }"
        "QWidget#fpgaControlPage QScrollArea#mainCardsScrollArea QScrollBar:vertical { width: %6px; }"
        "QWidget#fpgaControlPage QPushButton[fpgaSecondaryAction=\"true\"] { background-color: @vv-surface-alt; color: @vv-text; border: 1px solid @vv-border; }"
        "QWidget#fpgaControlPage QPushButton[fpgaSecondaryAction=\"true\"]:hover { background-color: @vv-primary-subtle; color: @vv-primary; border-color: @vv-primary; }"
        "QWidget#fpgaControlPage QPushButton[fpgaSecondaryAction=\"true\"]:focus { border-color: @vv-focus; }"
        "QWidget#fpgaControlPage QPushButton[fpgaSecondaryAction=\"true\"]:disabled { background-color: @vv-surface-alt; color: @vv-text-muted; border-color: @vv-border; }"
        "QWidget#fpgaControlPage QPushButton[fpgaIconOnly=\"true\"] { background-color: transparent; border: 1px solid transparent; border-radius: 7px; padding: 0; min-width: %5px; max-width: %5px; min-height: %5px; max-height: %5px; }"
        "QWidget#fpgaControlPage QPushButton[fpgaIconOnly=\"true\"]:hover, QWidget#fpgaControlPage QPushButton[fpgaIconOnly=\"true\"]:pressed { background-color: @vv-surface-alt; border-color: transparent; }"
        "QWidget#fpgaControlPage QPushButton[fpgaIconOnly=\"true\"]:focus { border-color: @vv-focus; }"
        "QWidget#fpgaControlPage QPushButton[fpgaIconOnly=\"true\"]:disabled { background-color: transparent; border-color: transparent; }")
        .arg(pixels(40)).arg(pixels(16)).arg(pixels(12)).arg(pixels(36)).arg(pixels(40)-2).arg(pixels(8)).arg(pixels(18)),dark));
    locator_->setMinimumWidth(pixels(200)); locator_->setMaximumWidth(QWIDGETSIZE_MAX); locator_->setFixedHeight(pixels(40));
    backend_->setMinimumWidth(pixels(104)); backend_->setMaximumWidth(QWIDGETSIZE_MAX); backend_->setFixedHeight(pixels(40));
    for (auto *button : {discover_,connect_,disconnect_}) {
        button->setFixedSize(pixels(40),pixels(40)); button->setIconSize(QSize(pixels(20),pixels(20)));
        QFile file(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("resources/lucide/%1.svg").arg(button->property("fpgaIconName").toString())));
        if (!file.open(QIODevice::ReadOnly)) continue;
        const auto color=button==connect_?AppThemeColor::HomeDeviceSuccess:button==disconnect_?AppThemeColor::ToolbarRed:AppThemeColor::ToolbarBlue;
        QByteArray svg=file.readAll(); svg.replace("currentColor",appThemeColorName(color,dark).toUtf8());
        const qreal dpr=button->devicePixelRatioF(); const int size=pixels(20);
        QPixmap pixmap(qRound(size*dpr),qRound(size*dpr)); pixmap.setDevicePixelRatio(dpr); pixmap.fill(Qt::transparent);
        QSvgRenderer renderer(svg); QPainter painter(&pixmap); renderer.render(&painter,QRectF(0,0,size,size)); painter.end(); button->setIcon(QIcon(pixmap));
    }
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
void FpgaControlPage::setSensorTransportPath(VaporView::SensorTransportPath path)
{
    sensorTransportPath_ = path;
    setProperty("sensorTransportPath", VaporView::sensorTransportPathToString(path));
}

FpgaControlPage::~FpgaControlPage() { stopUiTestController(); }

VaporView::Ground::Devices::FpgaDeviceController* FpgaControlPage::uiTestController() const
{ return simulation_.controller; }

void FpgaControlPage::stopUiTestController()
{
    simulation_.receiver.reset(); // Discard queued results before restoring the view.
    if(simulation_.worker) {
        QMetaObject::invokeMethod(simulation_.controller,&VaporView::Ground::Devices::FpgaDeviceController::disconnectDevice,Qt::BlockingQueuedConnection);
        simulation_.worker->quit();simulation_.worker->wait();simulation_.worker.reset();
    }
    simulation_.controller=nullptr;simulation_.transport=nullptr;
}

void FpgaControlPage::simulateOperation(const QJsonObject &request)
{
    if(!property("fpgaUiTestMode").toBool() || !simulation_.controller)return;
    auto *controller=simulation_.controller;
    auto *transport=simulation_.transport;
    const auto op=request.value("op").toString();
    if(op=="discover") {
        QPointer<QObject> receiver(simulation_.receiver.get());
        QMetaObject::invokeMethod(controller,[this,controller,receiver] {
            const auto result=controller->discoverDevices();
            if(receiver)QMetaObject::invokeMethod(receiver,[this,result]{setUsbDiscoveryResult(result);},Qt::QueuedConnection);
        },Qt::QueuedConnection);
        return;
    }
    if(op!="missing_cycle"&&op!="latch_dlia")lockOperation();
    const auto config=configuration();
    QMetaObject::invokeMethod(controller,[controller,transport,request,op,config] {
        const bool enabled=request.value("enabled").toBool();
        if(op=="missing_cycle"||op=="latch_dlia")transport->injectFault(op);
        else if(op=="connect") {
            controller->setConfiguration(config);
            controller->connectDevice("simulation://fpga","simulator",request.value("recover").toBool(true)
                ? VaporView::Ground::Devices::FpgaDeviceController::ConnectMode::RecoverEnabledModules
                : VaporView::Ground::Devices::FpgaDeviceController::ConnectMode::PreserveRunningModules);
        }
        else if(op=="disconnect")controller->disconnectDevice();
        else if(op=="refresh")controller->refresh();
        else if(op=="configure")controller->applyConfiguration(FpgaControlConfig::fromJson(request.value("configuration").toObject()),request.value("section").toString("all"));
        else if(op=="sensor") {
            if(enabled)controller->connectSensor(request);
            else controller->setSensorEnabled(quint16(request.value("source").toInt()),false);
        }
        else if(op=="sensor_config")controller->configureSensor(request);
        else if(op=="acquisition")controller->setAcquisition(enabled);
        else if(op=="raw")controller->setRawEnabled(enabled);
        else if(op=="wms")controller->setWaveform(request.value("channel").toInt(),enabled);
        else if(op=="dac")controller->setDac(request.value("channel").toInt(),enabled);
        else if(op=="temperature")controller->setTemperature(request.value("celsius").toDouble());
        else controller->executeOperation(request);
    },Qt::QueuedConnection);
}

void FpgaControlPage::setUiTestState(bool enabled, bool dataStalled, bool partialFailure,
                                      qint64 elapsedMs, VaporView::SensorTransportPath path)
{
    setSensorTransportPath(path);
    const bool wasEnabled=property("fpgaUiTestMode").toBool();
    if(enabled&&!wasEnabled) {
        clearMeasurements();
        simulation_=Simulation{};simulation_.saved=configuration();
        simulation_.savedUsb=usbDiscovery_;simulation_.savedUsbRemote=usbRemote_;simulation_.savedUsbAvailable=usbHostAvailable_;
        auto transport=VaporView::Ground::Devices::makeFpgaUsbSimulator();
        const auto discovery=transport->discoverDevices();
        simulation_.transport=transport.get();
        simulation_.receiver=std::make_unique<QObject>();
        simulation_.worker=std::make_unique<QThread>();
        simulation_.controller=new VaporView::Ground::Devices::FpgaDeviceController(std::move(transport));
        auto *controller=simulation_.controller;
        simulation_.desired=configuration();controller->setConfiguration(configuration());
        auto *receiver=simulation_.receiver.get();
        const auto publish=[this] {emit simulationStateChanged(ready_,busy_,hardwareValues_,configuration().pressureSource);};
        connect(controller,&VaporView::Ground::Devices::FpgaDeviceController::transportConnectionChanged,receiver,[this,publish](bool connected) {
            setTransportConnected(connected);publish();
        });
        connect(controller,&VaporView::Ground::Devices::FpgaDeviceController::connectionChanged,receiver,[this,publish](bool ready,bool busy,const QString& detail) {
            setConnectionState(ready,busy,text("界面测试 · VLP1 协议模拟器 · ","UI test · VLP1 protocol simulator · ")+detail);publish();
        });
        connect(controller,&VaporView::Ground::Devices::FpgaDeviceController::hardwareValuesChanged,receiver,[this,publish](const auto& values) {setHardwareValues(values);publish();});
        connect(controller,&VaporView::Ground::Devices::FpgaDeviceController::managementUpdated,receiver,[this](const auto& state){setManagementState(state);});
        connect(controller,&VaporView::Ground::Devices::FpgaDeviceController::measurementUpdated,receiver,[this](const auto& m) {
            updateSensor(m.reading);emit simulationMeasurementsUpdated(m);
        });
        connect(controller,&VaporView::Ground::Devices::FpgaDeviceController::waveformUpdated,receiver,[this](const auto& wave){updateWaveform(wave);emit simulationWaveformUpdated(wave);});
        connect(controller,&VaporView::Ground::Devices::FpgaDeviceController::sensorConfigurationApplied,receiver,[this](quint16 source,const QMap<quint32,quint32>& values) {
            if(source==0x46) {auto config=configuration();config.ai8.pollIntervalMs=values.value(0x662c);setConfiguration(config);}
            emit simulationSensorConfigurationApplied(source,values);
        });
        connect(controller,&VaporView::Ground::Devices::FpgaDeviceController::logRecordGenerated,receiver,[this](const auto& record) {
            appendDiagnostic(record.message);if(record.level>=VaporView::LogLevel::Error)emit simulationOperationFailed();
        });
        connect(simulation_.worker.get(),&QThread::finished,controller,&QObject::deleteLater);
        controller->moveToThread(simulation_.worker.get());
        simulation_.worker->setObjectName(QStringLiteral("fpgaUiTestUsbWorker"));simulation_.worker->start();
        setUsbDiscoveryHost(false,true);setUsbDiscoveryResult(discovery);
        setTransportConnected(false);setConnectionState(false,false);setHardwareValues({});setRecordingState(false);
        for(auto *l:hardwareLabels_)l->setText(QStringLiteral("—"));
    }
    setProperty("fpgaUiTestMode", enabled);
    for(auto *w:findChildren<QWidget*>())if(w->property("fpgaSimulationControl").toBool())w->setVisible(enabled);
    if (!enabled)
    {
        if(wasEnabled){stopUiTestController();
            const QSignalBlocker blocker(this);setConfiguration(simulation_.saved);
            setUsbDiscoveryHost(simulation_.savedUsbRemote,simulation_.savedUsbAvailable);setUsbDiscoveryResult(simulation_.savedUsb);}
        clearMeasurements();
        setTransportConnected(false);
        setConnectionState(false, false);
        setRecordingState(false);
        return;
    }

    Q_UNUSED(elapsedMs);
    auto *controller=simulation_.controller;
    auto *transport=simulation_.transport;
    if(simulation_.stalled!=dataStalled||simulation_.partialFailure!=partialFailure) {
        simulation_.stalled=dataStalled;simulation_.partialFailure=partialFailure;
        QMetaObject::invokeMethod(controller,[transport,dataStalled,partialFailure]{transport->setScenario(dataStalled,partialFailure);},Qt::QueuedConnection);
    }
    const auto desired=configuration();
    if(simulation_.desired.toJson()!=desired.toJson()) {
        simulation_.desired=desired;
        QMetaObject::invokeMethod(controller,[controller,desired]{controller->setConfiguration(desired);},Qt::QueuedConnection);
    }
    if(!wasEnabled)emit simulationStateChanged(false,false,{},configuration().pressureSource);
    updateActions();
}

void FpgaControlPage::clearMeasurements()
{
    hardwareValues_.clear();
    latestReadings_.clear();
    latestWaves_.clear();
    historicalReadings_.clear();
    historicalWaves_.clear();
    readingTimes_.clear();
    waveTimes_.clear();lastCompleteRaw_.clear();rawEnabled_.clear();
    for (auto *label : sensorLabels_)
        label->setText(text("等待有效样本", "Waiting for valid sample"));
    for (auto *label : waveLabels_)
        label->setText(text("等待波形 · ADC 原码 / 解调数字幅值",
                            "Waiting for waveform · ADC raw / demodulation digital amplitude"));
    auto plotWidgets=plots_.values()+secondPlots_.values();
    for(const auto &pair:iqPlots_)plotWidgets.append(pair);
    for(auto *panel:iqPanels_)panel->hide();
    for(auto *title:firstPlotTitles_)title->setText("H1");
    for(auto *title:secondPlotTitles_)title->setText("H2");
    for (auto *plotWidget : plotWidgets)
    {
        auto *plot = static_cast<SamplePlot *>(plotWidget);
        plot->samples.clear();
        plot->setProperty("sampleCount", 0);
        plot->update();
    }
    for (auto *label : hardwareLabels_)
        label->setText(QStringLiteral("—"));
    if (diagnostics_)
        diagnostics_->clear();
}
