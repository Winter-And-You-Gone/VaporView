#include "FpgaControlPage.h"
#include <QMessageBox>
#include "FpgaVirtualTcpTransport.h"
#include "FpgaDeviceController.h"
#include "FpgaUsbSimulator.h"
#include "FpgaTelemetry.h"
#include "FpgaAdvancedParameters.h"
#include "FpgaExtensions.h"
#include "FpgaDliaSos.h"
#include "FpgaHarmonics.h"
#include "ground/wave/WavePlotFrame.h"
#include "shared/theme/SingleLevelPopupComboBox.h"
#include "shared/theme/AppTheme.h"
#include "shared/theme/CheckedIndicator.h"
#include "shared/theme/TopLevelCardStyle.h"
#include "VisualTextLabel.h"
#include "shared/config/ApplicationConfig.h"
#include "shared/config/SettingsWriteBarrier.h"
#include "LabelTextSelection.h"

#include <QAbstractButton>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QCoreApplication>
#include <QDateTime>
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
#include <QSettings>
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
constexpr double WmsQ31Scale = 2147483648.0;
// Channel 2 retains the high-impedance connector calibration (CON28 pins 3–4).
constexpr double Wms2OutputFullScaleV = 4.9768690;
namespace Dac1 = VaporView::FpgaDac1Calibration;

void dispatchSimulationWhenIdle(QPointer<VaporView::Ground::Devices::FpgaDeviceController> controller,
    const std::shared_ptr<std::atomic<quint64>>& generation,quint64 expected,qint64 deadline,
    std::function<void()> action,std::function<void()> expired)
{
    if(!controller)return;
    if(generation->load(std::memory_order_acquire)!=expected) {expired();return;}
    if(!controller->connected() || QDateTime::currentMSecsSinceEpoch()>=deadline) {expired();return;}
    if(controller->busy()) {
        QTimer::singleShot(10,controller,[controller,generation,expected,deadline,
            action=std::move(action),expired=std::move(expired)]() mutable {
            dispatchSimulationWhenIdle(controller,generation,expected,deadline,std::move(action),std::move(expired));
        });return;
    }
    action();
}
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
        setAttribute(Qt::WA_Hover);
        setStyleSheet(QStringLiteral("QCheckBox::indicator, QCheckBox::indicator:checked, QCheckBox::indicator:hover { background: transparent; border: none; image: none; }"));
    }
protected:
    void paintEvent(QPaintEvent *event) override {
        QCheckBox::paintEvent(event);
        QStyleOptionButton option; initStyleOption(&option);
        const QRect indicator=style()->subElementRect(QStyle::SE_CheckBoxIndicator,&option,this);
        const bool dark=VaporView::isDarkThemePalette(palette());
        QColor color=VaporView::appThemeColor(VaporView::AppThemeColor::Primary,dark);
        if (!isEnabled()) color.setAlphaF(0.6f);
        QPainter painter(this);
        if (isEnabled() && (option.state & QStyle::State_MouseOver))
            VaporView::drawCheckedIndicatorHover(painter, indicator, VaporView::appThemeColor(VaporView::AppThemeColor::TitleBarHover, dark));
        if (isChecked()) VaporView::drawSquareCheckBig(painter,indicator,color);
        else VaporView::drawSquare(painter,indicator,color);
    }
};

class SamplePlot final : public QWidget {
public:
    QVector<double> samples;
    QVector<double> triggeredSamples;
    int displayOffset = 0;
    quint64 sampleStride = 1;
    QString emptyText = QStringLiteral("暂无数据");
    QElapsedTimer lastTrigger;
    quint32 triggerRate = 0, triggerPoints = 0;
    quint16 triggerFormat = 0;
    explicit SamplePlot(QWidget *parent) : QWidget(parent) { setProperty("fpgaFixedPlot",true); setFixedHeight(100); setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed); }
protected:
    void paintEvent(QPaintEvent *) override
    {
        using namespace VaporView::WavePlotFrame;
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const auto theme = plotThemeFor(this);
        painter.fillRect(rect(), theme.background);
        const auto &trace = triggeredSamples.isEmpty() ? samples : triggeredSamples;
        const auto metrics = painter.fontMetrics();
        qreal leftMargin = 6;
        qreal rightMargin = 6;
        int columns = 4;
        QStringList xLabels, yLabels;
        double low = 0, high = 0;
        if (!trace.isEmpty()) {
            const auto [minimum, maximum] = std::minmax_element(trace.cbegin(), trace.cend());
            low = *minimum;
            high = *maximum;
            if (low == high) {
                const double padding = std::max(1e-6, std::abs(low) * 0.05);
                low -= padding;
                high += padding;
            }
            const auto makeYLabels = [&](int precision) {
                QStringList labels;
                for (int i = 0; i < 4; ++i)
                    labels.append(QString::number(high - (high - low) * (i / 3.0), 'g', precision));
                return labels;
            };
            yLabels = makeYLabels(6);
            if (std::adjacent_find(yLabels.cbegin(), yLabels.cend()) != yLabels.cend())
                yLabels = makeYLabels(12);
            for (const auto &label : yLabels)
                leftMargin = std::max(leftMargin, qreal(metrics.horizontalAdvance(label) + 6));

            // Show indices in the capture, including points skipped by either decimator.
            const quint64 lastIndex = quint64(trace.size() - 1) * sampleStride;
            const QString endLabel = QString::number(lastIndex);
            rightMargin = std::max(rightMargin, metrics.horizontalAdvance(endLabel) / 2.0 + 2.0);
            const int labelSpacing = metrics.horizontalAdvance(endLabel + QStringLiteral(".75")) + 6;
            const int maxColumns = lastIndex < 4 ? std::max(1, int(lastIndex)) : 4;
            const qreal available = width() - leftMargin - rightMargin;
            columns = available >= maxColumns * labelSpacing ? maxColumns
                : (maxColumns >= 2 && available >= 2 * labelSpacing ? 2 : 1);
            for (int i = 0; i <= (lastIndex ? columns : 0); ++i)
                xLabels.append(QString::number(double(lastIndex) * i / columns, 'g', 12));
        }

        const QRectF area = QRectF(rect()).adjusted(leftMargin, 4, -rightMargin, -metrics.height() - 8);
        setProperty("plotAreaLeft", area.left());
        setProperty("plotAreaTop", area.top());
        setProperty("plotAreaRight", area.right());
        setProperty("plotAreaBottom", area.bottom());
        setProperty("xAxisTickLabels", xLabels);
        if (area.width() <= 1 || area.height() <= 1)
            return;
        drawGrid(painter, area, columns, theme);
        if (trace.isEmpty()) {
            painter.setPen(theme.mutedText);
            painter.drawText(area, Qt::AlignCenter, emptyText);
            return;
        }

        QPainterPath path;
        for (int i = 0; i < trace.size(); ++i) {
            const int index = (i + displayOffset) % trace.size();
            const qreal x = area.left() + area.width() * i / std::max<qsizetype>(1, trace.size() - 1);
            const qreal y = area.bottom() - area.height() * (trace.at(index) - low) / (high - low);
            if (i == 0)
                path.moveTo(x, y);
            else
                path.lineTo(x, y);
        }
        painter.setPen(QPen(VaporView::appThemeColor(VaporView::AppThemeColor::PlotSeriesWaveBlue,
            VaporView::isDarkThemePalette(palette())), 1.5));
        if (trace.size() == 1)
            painter.drawPoint(path.currentPosition());
        else
            painter.drawPath(path);

        painter.setPen(theme.text);
        const int yIntervals = int(yLabels.size()) - 1;
        for (int i = 0; i <= yIntervals; ++i) {
            const qreal y = area.top() + area.height() * i / yIntervals;
            const qreal top = i == 0 ? area.top() - 2
                : (i == yIntervals ? area.bottom() - metrics.height() + 2 : y - metrics.height() / 2.0);
            painter.drawText(QRectF(2, top, leftMargin - 4, metrics.height()),
                             Qt::AlignRight | Qt::AlignVCenter, yLabels.at(i));
        }
        for (int i = 0; i < xLabels.size(); ++i) {
            const qreal x = area.left() + area.width() * i / columns;
            const qreal labelWidth = metrics.horizontalAdvance(xLabels.at(i)) + 2;
            painter.drawText(QRectF(x - labelWidth / 2.0, area.bottom() + 2, labelWidth, metrics.height()),
                             Qt::AlignCenter, xLabels.at(i));
        }
    }
};
qint64 rawSample(const VaporView::FpgaWave::CompletedStream &stream, int index)
{
    const auto *word = stream.pointBytes.constData() + qsizetype(index) * 4;
    if (stream.format == 1) return qFromLittleEndian<quint32>(word);
    return qFromLittleEndian<qint32>(word);
}

// Locate the scan reset using the change in mean level across a modulation period.
// Treat the capture as circular so the same edge scores equally at either end.
int rawScanEdge(const VaporView::FpgaWave::CompletedStream &stream, quint32 modulationMilliHz)
{
    const int count = int(stream.totalPoints);
    if (count < 32 || !stream.rate || !modulationMilliHz) return -1;
    const int window = int(std::clamp(std::round(1000.0 * stream.rate / modulationMilliHz), 2.0, double(count / 16)));
    const auto sample = [&](int i) { return rawSample(stream, i); };
    qint64 left = 0, right = 0;
    for (int i = 0; i < window; ++i) {
        left += sample(count-window+i);
        right += sample(i);
    }
    QVector<qint64> changes(count);
    qint64 low = sample(0), high = low;
    int edge = 0;
    for (int i = 0; i < count; ++i) {
        changes[i] = std::abs(right-left);
        if (changes[i] > changes[edge]) edge = i;
        const qint64 value = sample(i);
        low = std::min(low, value);
        high = std::max(high, value);
        left += value - sample((i+count-window)%count);
        right += sample((i+window)%count) - value;
    }
    // A reset must stand out from the carrier, noise and other comparable edges.
    if (!changes[edge] || changes[edge] < 0.15 * window * (high-low)) return -1;
    for (int i = 0; i < count; ++i) {
        const int distance = std::abs(i-edge);
        if (std::min(distance, count-distance) > window && changes[i] > 0.8 * changes[edge]) return -1;
    }
    return edge;
}

// Score circular shifts of both harmonics against the last triggered sweep.
// Normalize the two components separately so a change in gain cannot choose the trigger.
int triggerDliaSweep(SamplePlot &h1, SamplePlot &h2, const VaporView::FpgaWave::CompletedStream &stream, bool &held)
{
    const int n = int(h2.samples.size());
    const bool tracking = h2.triggeredSamples.size() == n && h2.triggerRate == stream.rate &&
        h2.triggerPoints == stream.totalPoints && h2.triggerFormat == stream.format;
    int offset = 0;
    if (tracking) {
        QVector<double> scores(n, 0.0);
        int components = 0;
        const int step = std::max(1, (n + 95) / 96);
        for (const SamplePlot *plot : {&h1, &h2}) {
            const auto &reference = plot->triggeredSamples;
            const auto &current = plot->samples;
            for (int shift = 0; shift < n; ++shift) {
                double xSum=0, ySum=0, xx=0, yy=0, xy=0;
                int count=0;
                for (int i=0; i<n; i+=step) {
                    const double x=reference[(i+plot->displayOffset)%n], y=current[(i+shift)%n];
                    xSum+=x; ySum+=y; xx+=x*x; yy+=y*y; xy+=x*y; ++count;
                }
                const double energy = std::max(0.0, xx-xSum*xSum/count) * std::max(0.0, yy-ySum*ySum/count);
                if (energy>0) scores[shift] += (xy-xSum*ySum/count)/std::sqrt(energy);
            }
            if (*std::min_element(reference.cbegin(),reference.cend()) != *std::max_element(reference.cbegin(),reference.cend())) ++components;
        }
        // In a tie, retain the phase of the last sweep.
        offset = h2.displayOffset;
        for (int i=0; i<n; ++i) if (scores[i] > scores[offset]+1e-6) offset=i;
        if (components && scores[offset] >= 0.9*components) {
            // Keep the tracked lobe when two peaks are nearly equal, but anchor each
            // accepted sweep to a prominent H2 peak so small phase errors cannot accumulate.
            const auto &values = h2.samples;
            const auto [low, high] = std::minmax_element(values.cbegin(), values.cend());
            const double threshold = *high - 0.05 * (*high - *low);
            const int center = (offset + n/2) % n;
            for (int distance=0; distance<=n/2; ++distance) {
                for (int peak : {(center+distance)%n, (center-distance+n)%n}) {
                    if (values[peak] >= threshold && values[peak] >= values[(peak+n-1)%n]
                        && values[peak] >= values[(peak+1)%n]) return (peak+n-n/2)%n;
                }
            }
            return offset;
        }
        if (h2.lastTrigger.isValid() && h2.lastTrigger.elapsed()<1000) {
            held=true;
            return h2.displayOffset;
        }
    }
    const int peak=int(std::max_element(h2.samples.cbegin(),h2.samples.cend())-h2.samples.cbegin());
    return (peak+n-n/2)%n;
}

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

FpgaControlPage::FpgaControlPage(QWidget *parent, QVBoxLayout *motionLayout, QVBoxLayout *homeMotionLayout) : QWidget(parent) {
    motorHeartbeatTimer_.setParent(this);motorHeartbeatTimer_.setInterval(200);
    connect(&motorHeartbeatTimer_,&QTimer::timeout,this,[this] {
        if(motorLeaseRequested_ && connected_ && ready_)emit motorHeartbeatRequested();
        updateMotorProgress();
    });
    motorHeartbeatTimer_.start();
    measurementClock_.start();
    setObjectName("fpgaControlPage");
    setAttribute(Qt::WA_StyledBackground, true);
    auto *root=new QVBoxLayout(this); root->setContentsMargins(0,4,0,8); root->setSpacing(12);
    auto card = [this](QWidget *parent, QBoxLayout *layout, const QString &zh, const QString &en, const QString &iconName, bool topLevel = true, QHBoxLayout **titleRow = nullptr) {
        auto *frame = new QFrame(parent);
        frame->setObjectName(QStringLiteral("fpgaSectionCard"));
        if (topLevel) VaporView::configureTopLevelCard(frame);
        auto *cardLayout = new QVBoxLayout(frame);
        cardLayout->setContentsMargins(1,0,1,1); cardLayout->setSpacing(0);
        auto *header = new QWidget(frame); header->setObjectName(QStringLiteral("sectionTitleBar"));
        header->setProperty("fpgaMultilineHeader", titleRow != nullptr);
        auto *headerLayout = new QHBoxLayout;
        headerLayout->setContentsMargins(10,2,10,2); headerLayout->setSpacing(6);
        if (titleRow) {
            auto *rows = new QVBoxLayout(header);
            rows->setContentsMargins(0, 0, 0, 4);
            rows->setSpacing(0);
            rows->addLayout(headerLayout);
            *titleRow = headerLayout;
        } else {
            header->setLayout(headerLayout);
        }
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
    auto *waves=tab(QStringLiteral("采样"),QStringLiteral("Sampling"));
    auto *waveLayout=new QVBoxLayout(waves);
    auto *capture=card(waves,waveLayout,QStringLiteral("采集控制"),QStringLiteral("Acquisition control"),QStringLiteral("play"));
    auto *captureLayout=new QVBoxLayout(capture);
    auto *actions=new QGridLayout; captureLayout->addLayout(actions);
    auto action=[this](QGridLayout *layout,int row,int col,const char *zh,const char *en,const QString &name,std::function<void()> fn) {
        auto *b=new QPushButton(this); label(b,QString::fromUtf8(zh),QString::fromUtf8(en)); b->setObjectName(name);
        layout->addWidget(b,row,col); onlineControls_.append(b); connect(b,&QPushButton::clicked,this,[this,fn]{ lockOperation(); fn(); }); return b;
    };
    action(actions,0,0,"开始 H1/H2","Start H1/H2","fpgaAcquisitionStart",[this]{emit acquisitionRequested(true, linkDacsOnStart());});
    action(actions,0,1,"停止 H1/H2","Stop H1/H2","fpgaAcquisitionStop",[this]{emit acquisitionRequested(false);});
    action(actions,0,2,"开始原始波形","Start raw waveform","fpgaRawStart",[this]{emit rawRequested(true);});
    action(actions,0,3,"停止原始波形","Stop raw waveform","fpgaRawStop",[this]{emit rawRequested(false);});
    linkDacs_ = new FpgaCheckBox(capture);
    linkDacs_->setObjectName("fpgaLinkDacsOnStart");
    label(linkDacs_, QStringLiteral("开始 H1/H2 时开启双路 DAC"), QStringLiteral("Enable both DACs when starting H1/H2"));
    captureLayout->addWidget(linkDacs_);
    captureStatus_=new QLabel(capture); captureStatus_->setObjectName(QStringLiteral("fpgaCaptureStatus")); captureStatus_->setProperty("fpgaSecondaryText",true); captureStatus_->setWordWrap(true); captureLayout->addWidget(captureStatus_);
    auto *hint=new QLabel(capture); label(hint,QStringLiteral("先开始 H1/H2。需要查看原始波形时，再开始原始波形上传。"),QStringLiteral("Start H1/H2 first. To view raw waveforms, start raw waveform upload as well.")); hint->setWordWrap(true); captureLayout->addWidget(hint);
    auto *common=tab(QStringLiteral("公共参数"),QStringLiteral("Common settings"));
    auto *commonLayout=new QVBoxLayout(common);
    auto *shared=card(common,commonLayout,QStringLiteral("双路 ADC 采样设置"),QStringLiteral("Dual ADC sampling settings"),QStringLiteral("timer")); auto *sl=new QGridLayout(shared);
    auto field=[this](QGridLayout *layout,int row,const char *zh,const char *en,const QString &name,quint32 address,double min,double max,int decimals,
                      std::function<double(const FpgaControlConfig&)> read,std::function<void(FpgaControlConfig&,double)> write,int column=0) {
        auto *input=new QDoubleSpinBox(this); input->setObjectName(name); input->setDecimals(decimals); input->setRange(min,max); input->setKeyboardTracking(false);
        input->setAccessibleName(QString::fromUtf8(zh));
        if(column==0) {
            auto *title=new QLabel(this);title->setObjectName(name+"Label");title->setWordWrap(true);label(title,QString::fromUtf8(zh),QString::fromUtf8(en));title->setBuddy(input);layout->addWidget(title,row,0);
        }
        auto *actual=new QLabel(QStringLiteral("—"),this); actual->setObjectName(name+"Actual"); actual->setProperty("fpgaSecondaryText",true); actual->setWordWrap(true);
        layout->addWidget(input,row,column+1); layout->addWidget(actual,row,column+2);
        layout->setColumnStretch(column+2,1);
        fields_.append({input,read,write}); if (address) hardwareLabels_[address]=actual;
        connect(input,&QDoubleSpinBox::valueChanged,this,[this]{emit configurationChanged(configuration());});
        return input;
    };
    field(sl,0,"请求采样率 / Hz","Requested sampling / Hz","fpgaAdcRate",0x4014,1,1000000,0,[](const auto &c){return c.adcRateHz;},[](auto &c,double v){c.adcRateHz=quint32(v);});
    field(sl,1,"CNV 脉宽 / 10 ns","CNV width / 10 ns","fpgaAdcCnvTicks",0x4030,2,20,0,[](const auto& c){return c.adcCnvTicks;},[](auto& c,double v){c.adcCnvTicks=quint32(v);});
    field(sl,2,"BUSY 超时 / 10 ns","BUSY timeout / 10 ns","fpgaAdcBusyTimeout",0x4034,40,1000000,0,[](const auto& c){return c.adcBusyTimeoutTicks;},[](auto& c,double v){c.adcBusyTimeoutTicks=quint32(v);});
    for(int ch=0;ch<2;++ch)field(sl,3+ch,ch?"ADC1 周期点数 (0 关闭校验)":"ADC0 周期点数 (0 关闭校验)",ch?"ADC1 points (0 disables check)":"ADC0 points (0 disables check)",QString("fpgaAdcExpected%1").arg(ch),0x401c+ch*0x100,0,4294967295.0,0,[ch](const auto& c){return c.adcExpectedPoints[ch];},[ch](auto& c,double v){c.adcExpectedPoints[ch]=quint32(v);});
    auto *timingHint=new QLabel(shared);
    label(timingHint,QStringLiteral("两路 ADC 共用采样率、转换脉宽和超时。周期点数按通道校验。应用后需重新开始采集。"),
          QStringLiteral("Both ADCs share the sampling rate, conversion pulse width and timeout. Points per cycle are checked per channel. Restart acquisition after applying."));
    timingHint->setWordWrap(true);timingHint->setProperty("fpgaSecondaryText",true);sl->addWidget(timingHint,5,0,1,3);
    action(sl,6,0,"应用 ADC 参数","Apply ADC settings","fpgaApply",[this]{emit applyRequested(configuration(),"adc");});
    refresh_=action(sl,6,1,"刷新读回","Refresh readback","fpgaRefresh",[this]{emit refreshRequested();});
    auto *parameters=tab(QStringLiteral("通道参数"),QStringLiteral("Channel settings"));auto *pl=new QVBoxLayout(parameters);
    auto *parameterTabs=new QTabWidget(parameters);parameterTabs->setObjectName("fpgaParameterTabs");pl->addWidget(parameterTabs);
    auto *wmsPage=new QWidget(parameterTabs),*dliaPage=new QWidget(parameterTabs);
    parameterTabs->addTab(wmsPage,"WMS / DAC");parameterTabs->addTab(dliaPage,"DLIA");
    auto *wmsLayout=new QVBoxLayout(wmsPage),*dliaLayout=new QVBoxLayout(dliaPage);
    for(auto *layout : {wmsLayout,dliaLayout}) { const auto margins=layout->contentsMargins(); layout->setContentsMargins(0,margins.top(),0,margins.bottom()); }
    auto *wmsActions=new QGridLayout;wmsLayout->addLayout(wmsActions);
    action(wmsActions,0,0,"应用 WMS / DAC 参数","Apply WMS / DAC","fpgaApplyWms",[this] {
        const auto config = configuration();
        const auto &wave = config.wms[0];
        if (!Dac1::targetRange(wave.bias, wave.scanAmplitude, wave.sineAmplitude).withinInputRange()) {
            updateDac1Target(config);
            setConnectionState(ready_, false, dac1Target_->text());
            return;
        }
        emit applyWmsRequested(config);
    });
    restoreWms_ = new QPushButton(wmsPage);
    restoreWms_->setObjectName("fpgaRestoreWmsDefaults");
    label(restoreWms_, QStringLiteral("恢复出厂默认"), QStringLiteral("Restore defaults"));
    wmsActions->addWidget(restoreWms_, 0, 1);
    connect(restoreWms_, &QPushButton::clicked, this, [this] {
        if (QMessageBox::question(this, restoreWms_->text(),
            text("将双路 WMS / DAC 参数恢复为出厂默认值？设备就绪时会立即应用并保存，离线时仅恢复界面参数。",
                 "Restore factory settings for both WMS / DAC channels? When ready, the device will apply and save them immediately. Offline, only the form is updated."),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes || busy_) return;
        auto config = configuration();
        const auto defaults = FpgaControlConfig::factoryDefaults();
        config.wms = defaults.wms;
        config.dac = defaults.dac;
        setConfiguration(config);
        emit configurationChanged(config);
        if (ready_) { lockOperation(); emit applyWmsRequested(config); }
    });
    auto *dliaActions=new QGridLayout;dliaLayout->addLayout(dliaActions);
    action(dliaActions,0,0,"应用 DLIA 参数","Apply DLIA","fpgaApplyDlia",[this]{emit applyDliaRequested(configuration());});
    auto *voltageHint=new QLabel(wmsPage);voltageHint->setWordWrap(true);
    label(voltageHint,QStringLiteral("通道 1 填驱动板输入端的目标电压，通道 2 填 DAC 高阻输出端的目标电压。锯齿和正弦均填峰峰值，负号表示反相。应用后需重新开始采集。"),
        QStringLiteral("Enter target voltages at the driver input for channel 1 and the high-impedance DAC output for channel 2. Enter peak-to-peak amplitudes. A negative sign inverts the component. Restart acquisition after applying."));wmsLayout->addWidget(voltageHint);
    dac1Target_=new QLabel(wmsPage);dac1Target_->setObjectName("fpgaDac1TargetRange");
    dac1Target_->setWordWrap(true);wmsLayout->addWidget(dac1Target_);
    connect(this,&FpgaControlPage::configurationChanged,this,&FpgaControlPage::updateDac1Target);
    auto *dliaHint=new QLabel(dliaPage);dliaHint->setWordWrap(true);label(dliaHint,QStringLiteral("仅提交解调参数，保留 WMS / DAC 设置。应用后需重新开始采集。"),QStringLiteral("Apply demodulation settings only; preserve WMS / DAC. Restart acquisition after applying."));dliaLayout->addWidget(dliaHint);
    auto *wmsChannels = new QHBoxLayout;
    wmsChannels->setObjectName("fpgaWmsChannels");
    wmsChannels->setSpacing(12);
    wmsLayout->addLayout(wmsChannels);
    auto *dliaChannels=new QHBoxLayout;dliaChannels->setObjectName("fpgaDliaChannels");dliaChannels->setSpacing(12);dliaLayout->addLayout(dliaChannels);
    auto readback=[this](QGridLayout *layout,int row,int column,int span,quint32 address,const QString &zh,const QString &en,const QString &module=QString()) {
        if(column==1) {auto *title=new QLabel(this);title->setWordWrap(true);label(title,zh,en);layout->addWidget(title,row,0);}
        auto *actual=new QLabel(QStringLiteral("—"),this);actual->setObjectName(QString("fpgaRegister%1").arg(address,4,16,QChar('0')));
        actual->setProperty("fpgaSecondaryText",true);actual->setProperty("fpgaModule",module);actual->setWordWrap(true);
        layout->addWidget(actual,row,column,1,span);hardwareLabels_[address]=actual;
    };
    for(int channel=0;channel<2;++channel) {
        auto *group = card(wmsPage, wmsChannels, QStringLiteral("WMS / DAC · 通道 %1").arg(channel+1),
                           QStringLiteral("WMS / DAC · Channel %1").arg(channel+1), QStringLiteral("audio-waveform"));
        wmsChannels->setStretch(channel, 1);
        auto *grid = new QGridLayout(group);
        grid->setAlignment(Qt::AlignTop);
        const quint32 w=0x2000+channel*0x100,d=0x5000+channel*0x100;
        auto *buttons=new QGridLayout; grid->addLayout(buttons,0,0,1,3);
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
                });
        };
        wf("扫描频率 / Hz","Scan / Hz","Scan",0x10,0.001,1000,&FpgaControlConfig::Wms::scanMilliHz,1000);
        wf("调制频率 / Hz","Modulation / Hz","Sine",0x1c,0.001,100000,&FpgaControlConfig::Wms::sineMilliHz,1000);
        wf("请求更新率 / Hz","Requested update / Hz","Update",0x2c,1,1000000,&FpgaControlConfig::Wms::updateRateHz);
        if (channel == 0) {
            const auto voltage = [&](const char *zh, const char *en, const char *name, quint32 offset,
                                     double minimum, double maximum, qint32 FpgaControlConfig::Wms::*member,
                                     double (*toVolts)(qint32), qint32 (*toCode)(double)) {
                field(grid,row++,zh,en,QString("fpga0%1").arg(name),w+offset,minimum,maximum,3,
                    [member,toVolts](const FpgaControlConfig &c){ return toVolts(c.wms[0].*member); },
                    [member,toVolts,toCode](FpgaControlConfig &c,double value) {
                        auto &code = c.wms[0].*member;
                        const double displayed = std::round(toVolts(code)*1000)/1000;
                        if (std::abs(displayed - value) > 1e-9) code = toCode(value);
                    })->setSingleStep(0.010);
            };
            voltage("锯齿峰峰值 / Vpp","Sawtooth / Vpp","ScanAmplitude",0x14,-Dac1::MaxInputV,Dac1::MaxInputV,
                    &FpgaControlConfig::Wms::scanAmplitude,Dac1::scanVpp,Dac1::scanCodeFromVpp);
            voltage("正弦峰峰值 / Vpp","Sine / Vpp","SineAmplitude",0x20,-Dac1::MaxInputV,Dac1::MaxInputV,
                    &FpgaControlConfig::Wms::sineAmplitude,Dac1::modulationVpp,Dac1::modulationCodeFromVpp);
            voltage("直流偏置 / V","DC bias / V","Bias",0x18,Dac1::MinInputV,Dac1::MaxInputV,
                    &FpgaControlConfig::Wms::bias,Dac1::biasVolts,Dac1::biasCode);
        } else {
            const double codesPerVolt = WmsQ31Scale / Wms2OutputFullScaleV;
            const double maxV = std::numeric_limits<qint32>::max() / codesPerVolt;
            wf("锯齿峰峰值 / Vpp","Sawtooth / Vpp","ScanAmplitude",0x14,-2*Wms2OutputFullScaleV,2*maxV,&FpgaControlConfig::Wms::scanAmplitude,codesPerVolt/2);
            wf("正弦峰峰值 / Vpp","Sine / Vpp","SineAmplitude",0x20,-2*Wms2OutputFullScaleV,2*maxV,&FpgaControlConfig::Wms::sineAmplitude,codesPerVolt/2);
            wf("直流偏置 / V","DC bias / V","Bias",0x18,-Wms2OutputFullScaleV,maxV,&FpgaControlConfig::Wms::bias,codesPerVolt);
        }
        auto *more = new QPushButton(group);
        more->setObjectName(QString("fpga%1MoreParameters").arg(channel));
        label(more, QStringLiteral("更多参数"), QStringLiteral("More parameters"));
        more->setCheckable(true);
        grid->addWidget(more, row++, 0, 1, 3);
        auto *advanced = new QWidget(group);
        grid->addWidget(advanced, row, 0, 1, 3);
        advanced->hide();
        connect(more, &QPushButton::toggled, advanced, &QWidget::setVisible);
        grid = new QGridLayout(advanced);
        grid->setContentsMargins(0,0,0,0);
        row = 0;
        wf("扫描相位 / 整圈码","Scan phase / turn code","Phase",0x24,0,4294967295.0,&FpgaControlConfig::Wms::phase);
        wf("相位模式","Phase mode","PhaseMode",0x40,0,2,&FpgaControlConfig::Wms::phaseMode);
        auto *dliaGroup=card(dliaPage,dliaChannels,QStringLiteral("DLIA · 通道 %1").arg(channel+1),QStringLiteral("DLIA · Channel %1").arg(channel+1),QStringLiteral("chart-spline"));
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
        auto* cutoffTitle=new QLabel(dliaGroup);label(cutoffTitle,QStringLiteral("内置低通档位 / Hz"),QStringLiteral("Builtin LPF cutoff / Hz"));dliaGrid->addWidget(cutoffTitle,dliaRow,0);
        auto* cutoff=new VaporView::SingleLevelPopupComboBox(dliaGroup);cutoff_[channel]=cutoff;cutoff->setObjectName(QStringLiteral("fpga%1Cutoff").arg(channel));
        for(int hz:{100,250,500,1000,2000,5000,10000})cutoff->addItem(QString::number(hz),hz);
        cutoff->setCurrentIndex(cutoff->findData(1000));cutoff->setProperty("fpgaExtension",VaporView::FpgaExtensions::DliaCutoff);onlineControls_.append(cutoff);
        dliaGrid->addWidget(cutoff,dliaRow++,1);
        connect(cutoff,&QComboBox::currentIndexChanged,this,[this,channel,cutoff]{config_.dlia[channel].cutoffHz=cutoff->currentData().toUInt();emit configurationChanged(configuration());});
        readback(dliaGrid,dliaRow++,1,2,d+0x5c,QStringLiteral("生效截止频率 / Hz"),QStringLiteral("Active cutoff / Hz"));
        auto dacf=[&,channel](const char *zh,const char *en,const char *name,quint32 offset,double minimum,double maximum,auto member) {
            field(grid,row++,zh,en,QString("fpga%1Dac%2").arg(channel).arg(name),quint32(0x3000+channel*0x100)+offset,minimum,maximum,0,
                [channel,member](const auto &c){return double(c.dac[channel].*member);},
                [channel,member](auto &c,double v){c.dac[channel].*member=static_cast<std::remove_reference_t<decltype(c.dac[channel].*member)>>(v);});
        };
        const auto guardedDac = [&](const char *zh, const char *en, const char *name, quint32 offset,
                                    double minimum, double maximum, double calibrated, auto member) {
            dacf(zh,en,name,offset,channel ? minimum : calibrated,channel ? maximum : calibrated,member);
        };
        guardedDac("DAC 控制码","DAC control code","Control",0x10,0,1023,Dac1::DeviceControl,&FpgaControlConfig::Dac::deviceCtrl);
        guardedDac("DAC 清除码","DAC clear code","Clear",0x14,0,1048575,Dac1::ClearCode,&FpgaControlConfig::Dac::clearCode);
        guardedDac("DAC 下限码","DAC minimum code","Min",0x18,0,1048575,Dac1::MinDacCode,&FpgaControlConfig::Dac::minCode);
        guardedDac("DAC 上限码","DAC maximum code","Max",0x1c,0,1048575,Dac1::MaxDacCode,&FpgaControlConfig::Dac::maxCode);
        dacf("请求 SPI 频率 / Hz","Requested SPI clock / Hz","Spi",0x3c,1,20000000,&FpgaControlConfig::Dac::spiClockHz);
        guardedDac("DAC 增益 / UQ2.30","DAC gain / UQ2.30","Gain",0x34,0,4294967295.0,Dac1::UnityGain,&FpgaControlConfig::Dac::gain);
        guardedDac("DAC 偏移码","DAC offset code","Offset",0x38,-2147483648.0,2147483647.0,0,&FpgaControlConfig::Dac::offset);
        const quint32 dac=0x3000+channel*0x100;
        readback(grid,row++,1,2,w+8,QStringLiteral("WMS 状态"),QStringLiteral("WMS status"),"WMS");
        readback(grid,row++,1,2,w+12,QStringLiteral("WMS 错误寄存器"),QStringLiteral("WMS error register"));
        readback(grid,row++,1,2,dac+8,QStringLiteral("DAC 状态"),QStringLiteral("DAC status"),"DAC");
        readback(grid,row++,1,2,dac+12,QStringLiteral("DAC 错误寄存器"),QStringLiteral("DAC error register"));
        readback(grid,row++,1,2,w+0x3c,QStringLiteral("波形饱和次数"),QStringLiteral("Waveform saturation count"));
        readback(grid,row++,1,2,w+0x44,QStringLiteral("更新超限次数"),QStringLiteral("Update overrun count"));
        readback(grid,row++,1,2,dac+0x28,QStringLiteral("DAC 写入次数"),QStringLiteral("DAC write count"));
        readback(grid,row++,1,2,dac+0x2c,QStringLiteral("SPI 错误次数"),QStringLiteral("SPI error count"));
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
    auto *sensorCard=card(sensors,sensorLayout,QStringLiteral("传感器状态"),QStringLiteral("Sensor status"),QStringLiteral("gauge"));
    auto *sensorGrid=new QGridLayout(sensorCard);sensorGrid->setObjectName("fpgaSensorGrid");sensorGrid->setSpacing(12);sensorGrid->setColumnStretch(1,1);
    const char *names[]={"PTB210","HC2A","EPSILON","BMP390","SHT45","SDFM300","TEC103 × 2"}; const quint16 sources[]={0x40,0x41,0x42,0x43,0x44,0x45,0x4a};
    for(int i=0;i<7;++i) {
        const auto source=sources[i]; const auto suffix=QString::number(source,16).rightJustified(4,QChar('0'));
        auto *name=new QLabel(QString::fromLatin1(names[i]),sensorCard); name->setObjectName("fpgaSensorName"+suffix);
        auto nameFont=name->font(); nameFont.setBold(true); name->setFont(nameFont); name->setAlignment(Qt::AlignLeft|Qt::AlignVCenter);
        sensorGrid->addWidget(name,i,0);
        auto *l=new ConnectionStatusLabel(sensorCard); l->setObjectName("fpgaSensor"+suffix); sensorLabels_[source]=l;
        sensorGrid->addWidget(l,i,1);
        auto* enable=action(sensorGrid,i,2,"启用","Enable","fpgaSensorEnable"+suffix,[this,source]{emit sensorEnableRequested(source,true);});
        auto* disable=action(sensorGrid,i,3,"停用","Disable","fpgaSensorDisable"+suffix,[this,source]{emit sensorEnableRequested(source,false);});
        if(source==0x4a) {enable->setProperty("tec103BridgeControl",true);disable->setProperty("tec103BridgeControl",true);}
    }
    auto *ai8config=card(sensors,sensorLayout,QStringLiteral("AI8 轮询配置（停用时提交）"),QStringLiteral("AI8 polling configuration (apply while disabled)"),QStringLiteral("thermometer"));
    auto *ai8layout=new QHBoxLayout(ai8config);auto *ai8controls=new QVBoxLayout;auto *ai8grid=new QGridLayout;
    ai8layout->addLayout(ai8controls,1);ai8layout->addLayout(ai8grid,2);
    auto *ai8reading=new QLabel(ai8config);ai8reading->setObjectName("fpgaSensor0046");ai8reading->setWordWrap(true);sensorLabels_[0x46]=ai8reading;ai8controls->addWidget(ai8reading);
    auto *ai8buttons=new QGridLayout;ai8controls->addLayout(ai8buttons);
    action(ai8buttons,0,0,"启用","Enable","",[this]{emit sensorEnableRequested(0x46,true);});
    action(ai8buttons,0,1,"停用","Disable","",[this]{emit sensorEnableRequested(0x46,false);});
    action(ai8buttons,1,0,"应用 AI8 参数","Apply AI8 settings","fpgaApplyAi8",[this]{emit applyRequested(configuration(),"ai8");});
    auto *tempGrid=new QGridLayout;ai8controls->addLayout(tempGrid);auto *temperature=new QDoubleSpinBox(ai8config);temperature->setRange(-999.0,2147.4);temperature->setDecimals(1);temperature->setSingleStep(0.1);temperature->setSuffix(" °C");tempGrid->addWidget(temperature,0,0,1,2);
    field(ai8grid,0,"仪表地址","Instrument address","fpgaAi8Address",0x6614,1,80,0,[](const auto &c){return c.ai8.slaveAddress;},[](auto &c,double v){c.ai8.slaveAddress=quint32(v);});
    field(ai8grid,1,"仪表通道","Instrument channel","fpgaAi8Channel",0x661c,1,8,0,[](const auto &c){return c.ai8.channel;},[](auto &c,double v){c.ai8.channel=quint32(v);});
    field(ai8grid,2,"轮询间隔 / ms","Polling interval / ms","fpgaAi8Interval",0x662c,1,3600000,0,[](const auto &c){return c.ai8.pollIntervalMs;},[](auto &c,double v){c.ai8.pollIntervalMs=quint32(v);});
    field(ai8grid,3,"通信超时 / ms","Communication timeout / ms","fpgaAi8Timeout",0x6660,1,65535,0,[](const auto &c){return c.ai8.timeoutMs;},[](auto &c,double v){c.ai8.timeoutMs=quint32(v);});
    field(ai8grid,4,"重试上限","Retry limit","fpgaAi8Retry",0x6664,0,3,0,[](const auto &c){return c.ai8.retryLimit;},[](auto &c,double v){c.ai8.retryLimit=quint32(v);});
    ai8Scan_=new FpgaCheckBox(ai8config);label(ai8Scan_,QStringLiteral("扫描全部 8 通道（间隔表示整轮扫描周期）"),QStringLiteral("Scan all 8 channels (interval is a complete scan cycle)"));ai8Scan_->setObjectName("fpgaAi8Scan");ai8Scan_->setProperty("fpgaExtension",VaporView::FpgaExtensions::Ai8Scan);onlineControls_.append(ai8Scan_);ai8grid->addWidget(ai8Scan_,6,0,1,3);
    connect(ai8Scan_,&QCheckBox::toggled,this,[this](bool checked){config_.ai8.scan=checked;emit configurationChanged(configuration());});
    auto *ai8hint=new QLabel(ai8config);label(ai8hint,QStringLiteral("共享总线固定 19200 / 8N1。保留仪表报警与超时；不会自动修改仪表参数。"),QStringLiteral("Shared bus: 19200 / 8N1. Instrument alarms and timeouts are retained; instrument parameters are not automatically changed."));ai8hint->setWordWrap(true);ai8grid->addWidget(ai8hint,5,0,1,3);
    auto* runtimeChannel=new VaporView::SingleLevelPopupComboBox(ai8config);runtimeChannel->setObjectName("fpgaAi8RuntimeChannel");
    for(int i=1;i<=8;++i)runtimeChannel->addItem(QStringLiteral("CH%1").arg(i),i);
    runtimeChannel->setProperty("fpgaExtension",VaporView::FpgaExtensions::Ai8Scan);onlineControls_.append(runtimeChannel);tempGrid->addWidget(runtimeChannel,1,1);
    action(tempGrid,1,0,"设置 AI8 温度并读回","Set AI8 temperature & verify","fpgaSetTemperature",[this,temperature,runtimeChannel]{
        if(extensions_&VaporView::FpgaExtensions::Ai8Scan)emit operationRequested({{"op","device_temperature"},{"source",0x46},{"channel",runtimeChannel->currentData().toInt()},{"celsius",temperature->value()}});
        else emit setTemperatureRequested(temperature->value());
    });
    auto* cache=action(tempGrid,2,0,"读取通道缓存","Read channel cache","fpgaAi8ReadCache",[this,runtimeChannel]{emit operationRequested({{"op","ai8_cache"},{"channel",runtimeChannel->currentData().toInt()}});});cache->setProperty("fpgaExtension",VaporView::FpgaExtensions::Ai8Scan);
    readback(ai8grid,7,1,2,0x66a0,QStringLiteral("缓存在线 / 错误状态"),QStringLiteral("Cached online / error status"));
    readback(ai8grid,8,1,2,0x66a4,QStringLiteral("缓存 PV / SP 原码"),QStringLiteral("Cached PV / SP raw"));
    readback(ai8grid,9,1,2,0x66a8,QStringLiteral("缓存 OP / SV 原码"),QStringLiteral("Cached OP / SV raw"));
    readback(ai8grid,10,1,2,0x66ac,QStringLiteral("缓存设备状态"),QStringLiteral("Cached device status"));
    ai8controls->addStretch();sensorLayout->addStretch();
    waveformTrigger_ = new FpgaCheckBox(waves);
    waveformTrigger_->setObjectName("fpgaWaveformTrigger");
    label(waveformTrigger_, QStringLiteral("波形触发对齐（关闭时按 FPGA 扫描起点显示）"),
          QStringLiteral("Align by waveform trigger (off: FPGA scan origin)"));
    waveLayout->addWidget(waveformTrigger_);
    connect(waveformTrigger_, &QCheckBox::toggled, this, [this] { refreshMeasurements(); });
    auto *waveHint=new QLabel(waves); label(waveHint,QStringLiteral("横轴为采样点序号。ADC 未确认前端校准时显示原码；解调显示数字幅值，不代表气体浓度。"),QStringLiteral("The horizontal axis shows the sample index. ADC displays raw codes until frontend calibration is confirmed. Demodulation displays digital amplitude, not concentration.")); waveHint->setWordWrap(true); waveLayout->addWidget(waveHint);
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
    for(int index=0;index<7;++index) {
        const auto source=sources[index];sensorChoice->addItem(QString::fromLatin1(names[index]),source);
        auto *body=new QWidget(sensorStack);formsLayout->addWidget(body);sensorForms.push_back(body);body->setVisible(index==0);
        auto *grid=new QGridLayout(body);int row=0;
        if(source==0x4a) {
            auto* hint=new QLabel(body);hint->setWordWrap(true);
            label(hint,QStringLiteral("两台 TEC103 使用 GPIO TTL 38400 / 8N1，分别连接 LC0 和 LC1，每台内部通道均为 TC1。温度、输出与参数请使用激光温控页。"),
                QStringLiteral("Two TEC103 controllers use LC0 and LC1 GPIO TTL at 38400 / 8N1, internal TC1 on each. Use Laser temperature for readings, output and parameters."));
            grid->addWidget(hint,row++,0,1,3);
        }
        for(const auto& spec:VaporView::FpgaRemote::sensorParameters())if(spec.source==source && source!=0x4a) {
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
    auto number=[this](QGridLayout *grid,int row,const char *title,const char *name,double low,double high,double value,
                       int decimals=0,const char *englishTitle=nullptr) {
        auto *caption=new QLabel(QString::fromUtf8(title),this);caption->setWordWrap(true);grid->addWidget(caption,row,0);
        if (englishTitle) label(caption, QString::fromUtf8(title), QString::fromUtf8(englishTitle));
        auto *spin=new QDoubleSpinBox(this);spin->setObjectName(QString::fromLatin1(name));spin->setRange(low,high);spin->setDecimals(decimals);spin->setValue(value);spin->setKeyboardTracking(false);grid->addWidget(spin,row,1);return spin;
    };
    auto operation=[this](QGridLayout *grid,int row,int col,const char *zh,const char *en,const char *name,std::function<QJsonObject()> request) {
        auto *button=new QPushButton(this);label(button,QString::fromUtf8(zh),QString::fromUtf8(en));button->setObjectName(QString::fromLatin1(name));
        grid->addWidget(button,row,col);onlineControls_.append(button);
        button->setFocusPolicy(Qt::TabFocus);
        connect(button,&QPushButton::clicked,this,[this,request] {
            const auto command=request();if(!prepareMotorOperation(command))return;
            lockOperation();emit operationRequested(command);
        });return button;
    };
    struct ActualField { quint32 address; const char *zh; const char *en; };
    auto readbacks=[&readback](QGridLayout *grid,int row,std::initializer_list<ActualField> fields) {
        for(const auto& field:fields)readback(grid,row++,1,2,field.address,QString::fromUtf8(field.zh),QString::fromUtf8(field.en));
    };
    auto *lanes=card(advanced,advancedLayout,QStringLiteral("分通道采集"),QStringLiteral("Acquisition per channel"),QStringLiteral("audio-lines"));auto *laneGrid=new QGridLayout(lanes);
    for(int channel=0;channel<2;++channel) {
        auto *featureCard=card(advanced,advancedLayout,QStringLiteral("通道 %1 · SOS 滤波 / PPS 扫描").arg(channel+1),
            QStringLiteral("Channel %1 · SOS filter / PPS scan").arg(channel+1),QStringLiteral("funnel"));
        auto *grid=new QGridLayout(featureCard);
        auto *hint=new QLabel(featureCard);hint->setWordWrap(true);
        label(hint,QStringLiteral("应用前自动停止采集，完成后请手动开始。SOS 按实际 ADC 输入率设计；普通参数提交保留自定义滤波。等待电平为有符号码，不代表电压。"),
            QStringLiteral("Apply stops acquisition; start it manually afterward. SOS uses the actual ADC input rate and survives ordinary parameter commits. Idle level is a signed code, not volts."));
        grid->addWidget(hint,0,0,1,3);
        const auto name=[channel](const char* key){return QString("fpga%1%2").arg(channel).arg(key);};
        auto *order=number(grid,1,"Butterworth 阶数 (2–8)",qPrintable(name("SosOrder")),2,8,4);
        auto *hz=number(grid,2,"SOS 截止频率 / Hz",qPrintable(name("SosHz")),1,499999,1000);
        auto *sos=operation(grid,3,0,"应用 SOS 并回读","Apply SOS & verify",qPrintable(name("SosApply")),[channel,order,hz]{
            return QJsonObject{{"op","dlia_sos"},{"channel",channel},{"order",order->value()},{"hz",hz->value()}};
        });
        sos->setProperty("fpgaApiAddress",0x50f0+channel*0x100);sos->setProperty("fpgaApiValue",VaporView::FpgaDliaSos::Api);
        auto *builtin=operation(grid,3,1,"恢复 DLIA 页所选内置滤波","Restore builtin selected on DLIA page",qPrintable(name("SosBuiltin")),[this,channel]{
            return QJsonObject{{"op","dlia_cutoff"},{"channel",channel},{"hz",int(config_.dlia[channel].cutoffHz)}};
        });builtin->setProperty("fpgaExtension",VaporView::FpgaExtensions::DliaCutoff);
        auto *status=new QLabel(featureCard);status->setObjectName(name("SosStatus"));status->setWordWrap(true);grid->addWidget(status,4,0,1,3);
        auto *modeTitle=new QLabel(featureCard);label(modeTitle,QStringLiteral("扫描触发"),QStringLiteral("Scan trigger"));grid->addWidget(modeTitle,5,0);
        auto *mode=new VaporView::SingleLevelPopupComboBox(featureCard);mode->setObjectName(name("TriggerMode"));
        mode->addItem(QStringLiteral("连续扫描"),0);mode->addItem(QStringLiteral("PPS 单次扫描"),1);grid->addWidget(mode,5,1);
        auto *idle=number(grid,6,"等待电平 / signed Q1.31",qPrintable(name("IdleLevel")),-2147483648.0,2147483647.0,0);
        auto *trigger=operation(grid,7,0,"应用触发设置","Apply trigger",qPrintable(name("TriggerApply")),[channel,mode,idle]{
            return QJsonObject{{"op","wms_trigger"},{"channel",channel},{"mode",mode->currentData().toInt()},{"idle",idle->value()}};
        });
        auto *snapshot=operation(grid,7,1,"记录一致扫描快照","Record coherent scan snapshot",qPrintable(name("PpsSnapshot")),[channel]{return QJsonObject{{"op","pps_snapshot"},{"channel",channel}};});
        for(auto *button:{trigger,snapshot}) {button->setProperty("fpgaApiAddress",0x2084+channel*0x100);button->setProperty("fpgaApiValue",VaporView::FpgaDliaSos::PpsApi);}
        auto *pps=new QLabel(featureCard);pps->setObjectName(name("PpsStatus"));pps->setWordWrap(true);grid->addWidget(pps,8,0,1,3);
    }
    for(int ch=0;ch<2;++ch)for(int enable=0;enable<2;++enable)operation(laneGrid,ch,enable,ch?(enable?"通道 1 开始":"通道 1 停止"):(enable?"通道 0 开始":"通道 0 停止"),enable?"Start channel":"Stop channel",qPrintable(QString("fpgaChannel%1%2").arg(ch).arg(enable)),[ch,enable]{return QJsonObject{{"op","channel_acquisition"},{"channel",ch},{"enabled",bool(enable)}};});
    auto *time=card(common,commonLayout,QStringLiteral("时间同步"),QStringLiteral("Time synchronization"),QStringLiteral("clock-arrow-up"));auto *tg=new QGridLayout(time);
    commonLayout->addStretch();
    auto *edge=number(tg,0,"同步沿 (0 上升 / 1 下降)","fpgaTimeEdge",0,1,0);auto *timeout=number(tg,1,"同步超时 / ms","fpgaTimeTimeout",1,3600000,2000);
    operation(tg,0,2,"设置同步沿","Set edge","fpgaTimeSetEdge",[edge]{return QJsonObject{{"op","time"},{"action",3},{"value",edge->value()}};});
    operation(tg,1,2,"设置同步超时","Set timeout","fpgaTimeSetTimeout",[timeout]{return QJsonObject{{"op","time"},{"action",4},{"value",timeout->value()}};});
    operation(tg,2,0,"启用同步","Enable sync","fpgaTimeEnable",[]{return QJsonObject{{"op","time"},{"action",1},{"value",1}};});operation(tg,2,1,"停用同步","Disable sync","fpgaTimeDisable",[]{return QJsonObject{{"op","time"},{"action",1},{"value",0}};});operation(tg,2,2,"重新捕获同步沿","Rearm sync","fpgaTimeRearm",[]{return QJsonObject{{"op","time"},{"action",2},{"value",0}};});
    calibrationStatus_=new QLabel(time);calibrationStatus_->setObjectName("fpgaPpsCalibrationStatus");calibrationStatus_->setWordWrap(true);tg->addWidget(calibrationStatus_,3,0,1,3);
    readbacks(tg,4,{{0x1004,"同步控制","Sync control"},{0x1008,"同步状态","Sync status"},{0x100c,"同步错误","Sync errors"},
        {0x1010,"当前同步沿","Actual sync edge"},{0x1014,"当前同步超时","Actual sync timeout"},{0x1038,"当前 GNSS 标签低位","Actual GNSS tag low"},
        {0x103c,"当前 GNSS 标签高位","Actual GNSS tag high"},{0x1040,"串口 GNSS 标签有效（非 UTC 锚点）","Serial GNSS tag valid (not a UTC anchor)"},
        {0x1048,"PPS 实测 tick/s","Measured PPS ticks/s"},{0x104c,"PPS 序号","PPS sequence"},{0x1060,"原子校准状态","Atomic calibration status"},
        {0x1064,"不合格 PPS 间隔累计数","Rejected PPS interval count"}});
    auto addMotorCard = [&](QVBoxLayout *layout, bool topLevel) {
        QHBoxLayout *titleRow = nullptr;
        auto *body = card(layout->parentWidget(), layout, QStringLiteral("步进电机"), QStringLiteral("Stepper motor"),
                          QStringLiteral("cog"), topLevel, &titleRow);
        auto *frame = static_cast<QFrame *>(body->parentWidget());
        frame->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        auto *header = frame->findChild<QWidget *>(QStringLiteral("sectionTitleBar"), Qt::FindDirectChildrenOnly);
        auto *status = new QLabel(header);
        status->setObjectName("fpgaMotorSafetyStatus");
        status->setProperty("fpgaSecondaryText", true);
        status->setWordWrap(true);
        titleRow->setStretch(1, 0);
        titleRow->addWidget(status, 1);

        auto *bodyLayout = new QVBoxLayout(body);
        bodyLayout->setContentsMargins(0, 0, 0, 0);
        auto *controls = new QWidget(body);
        bodyLayout->addWidget(controls);
        auto *motorGrid = new QGridLayout(controls);
        motorGrid->setContentsMargins(12, 10, 12, 12);
        motorGrid->setSpacing(8);
        motorGrid->setColumnStretch(0, 1);
        motorGrid->setColumnStretch(1, 1);
        auto *frequency = number(motorGrid, 0, "步进频率 / Hz (20..400)", "fpgaMotorFrequency", 20, 400, 100,
                                 0, "Step frequency / Hz (20..400)");
        auto *steps = number(motorGrid, 1, "相对运动脉冲数 (±1..5000，正值移入，负值移出)", "fpgaMotorSteps", -5000, 5000, 40,
                             0, "Relative move (±1..5000 pulses, positive: move in, negative: move out)");
        operation(motorGrid, 2, 0, "应用电机参数并停用", "Configure and disable", "fpgaMotorConfigure",
                  [frequency] { return QJsonObject{{"op","motor_config"},{"frequency",frequency->value()}}; });
        operation(motorGrid, 2, 1, "启用电机", "Enable motor", "fpgaMotorEnable",
                  [] { return QJsonObject{{"op","motor"},{"action",4},{"value",1}}; });
        operation(motorGrid, 3, 0, "运行相对运动", "Run relative move", "fpgaMotorRun",
                  [steps] { return QJsonObject{{"op","motor"},{"action",1},{"value",steps->value()}}; });
        auto *stop = operation(motorGrid, 3, 1, "停止（保持使能）", "Stop (hold enabled)", "fpgaMotorStop",
                               [] { return QJsonObject{{"op","motor"},{"action",2},{"value",0}}; });
        stop->setProperty("fpgaSafetyControl", true);
        auto *disable = operation(motorGrid, 4, 0, "停用电机", "Disable motor", "fpgaMotorDisable",
                                  [] { return QJsonObject{{"op","motor"},{"action",4},{"value",0}}; });
        disable->setProperty("fpgaSafetyControl", true);
        operation(motorGrid, 4, 1, "软件脉冲位置清零", "Zero software pulse position", "fpgaMotorZero",
                  [] { return QJsonObject{{"op","motor"},{"action",3},{"value",0}}; });
        auto *progress = new QLabel(controls);
        progress->setObjectName("fpgaMotorProgress");
        progress->setWordWrap(true);
        progress->hide();
        motorGrid->addWidget(progress, 5, 0, 1, 2);
        motorViews_.append({frame, status, progress, frequency, steps});

        auto settings = VaporView::applicationConfigSettings();
        const auto presetInput = [&](int row, const char *zh, const char *en, const char *name,
                                     int lower, int upper, int fallback, const QString &key) {
            auto *input = number(motorGrid, row, zh, name, lower, upper, fallback, 0, en);
            bool ok = false;
            const int saved = settings.value(key, fallback).toInt(&ok);
            if (ok && saved >= lower && saved <= upper) input->setValue(saved);
            connect(input, &QDoubleSpinBox::valueChanged, this, [key](double value) {
                auto settings = VaporView::applicationConfigSettings();
                VaporView::setPersistentSetting(settings, key, int(value));
            });
            return input;
        };
        auto *presetTitle = new QLabel(controls);
        label(presetTitle, QStringLiteral("首页运动预设"), QStringLiteral("Home motion presets"));
        motorGrid->addWidget(presetTitle, 6, 0, 1, 2);
        auto *inFrequency = presetInput(7, "移入频率 / Hz", "Move-in frequency / Hz", "fpgaMotorInFrequency",
                                        20, 400, 200, QStringLiteral("Fpga/stepperInFrequency"));
        auto *inSteps = presetInput(8, "移入脉冲数", "Move-in pulses", "fpgaMotorInSteps",
                                    1, 5000, 2000, QStringLiteral("Fpga/stepperInSteps"));
        auto *outFrequency = presetInput(9, "移出频率 / Hz", "Move-out frequency / Hz", "fpgaMotorOutFrequency",
                                         20, 400, 200, QStringLiteral("Fpga/stepperOutFrequency"));
        auto *outSteps = presetInput(10, "移出脉冲数", "Move-out pulses", "fpgaMotorOutSteps",
                                     -5000, -1, -2000, QStringLiteral("Fpga/stepperOutSteps"));

        if (homeMotionLayout) {
            auto *homeCard = new QFrame(homeMotionLayout->parentWidget());
            homeCard->setObjectName(QStringLiteral("homeStepperMotorCard"));
            homeCard->setProperty("epsilonSubcard", true);
            homeCard->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
            auto *homeRow = new QHBoxLayout(homeCard);
            homeRow->setContentsMargins(2, 2, 2, 2);
            homeRow->setSpacing(2);
            auto *title = new QLabel(homeCard);
            title->setObjectName(QStringLiteral("epsilonSectionLabel"));
            title->setAlignment(Qt::AlignCenter);
            title->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Expanding);
            title->setContentsMargins(4, 0, 4, 0);
            label(title, QStringLiteral("步\n进\n电\n机"), QStringLiteral("Stepper\nmotor"));
            homeRow->addWidget(title);
            auto *homeBody = new QWidget(homeCard);
            homeBody->setObjectName(QStringLiteral("homeStepperMotorBody"));
            auto *grid = new QGridLayout(homeBody);
            grid->setContentsMargins(8, 4, 8, 6);
            grid->setSpacing(4);
            auto *homeStatus = new QLabel(homeBody);
            homeStatus->setObjectName(QStringLiteral("fpgaMotorSafetyStatus"));
            homeStatus->setWordWrap(true);
            auto *statusRow = new QHBoxLayout;
            statusRow->addWidget(homeStatus, 1);
            homeMotorState_ = new QLabel(homeBody);
            homeMotorState_->setObjectName(QStringLiteral("homeStepperMotorState"));
            statusRow->addWidget(homeMotorState_, 0, Qt::AlignRight | Qt::AlignTop);
            grid->addLayout(statusRow, 0, 0, 1, 3);
            auto *homeProgress = new QLabel(homeBody);
            homeProgress->setObjectName(QStringLiteral("fpgaMotorProgress"));
            homeProgress->setWordWrap(true);
            grid->addWidget(homeProgress, 1, 0, 1, 3);
            auto *homeStop = operation(grid, 2, 0, "停止并停用", "Stop and disable", "fpgaMotorDisable",
                                       [] { return QJsonObject{{"op","motor"},{"action",4},{"value",0}}; });
            homeStop->setProperty("fpgaSafetyControl", true);
            homeStop->hide();
            const auto move = [](QDoubleSpinBox *frequency, QDoubleSpinBox *steps) {
                return QJsonObject{{"op","motor"},{"action",1},{"value",int(steps->value())},
                                   {"frequency",int(frequency->value())}};
            };
            operation(grid, 3, 0, "移入 / 定标模式", "Move in / Calibration", "fpgaMotorMoveIn",
                      [=] { return move(inFrequency, inSteps); });
            operation(grid, 3, 1, "移出 / 遥测模式", "Move out / Telemetry", "fpgaMotorMoveOut",
                      [=] { return move(outFrequency, outSteps); });
            auto *configure = new QPushButton(homeBody);
            configure->setObjectName(QStringLiteral("homeStepperConfigure"));
            configure->setCursor(Qt::PointingHandCursor);
            label(configure, QStringLiteral("设备配置"), QStringLiteral("Settings"));
            connect(configure, &QPushButton::clicked, this, &FpgaControlPage::openDeviceConfigurationRequested);
            grid->addWidget(configure, 3, 2);
            grid->setColumnStretch(0, 1);
            grid->setColumnStretch(1, 1);
            homeRow->addWidget(homeBody, 1);
            homeMotionLayout->addWidget(homeCard, 1);
            motorViews_.append({homeCard, homeStatus, homeProgress, nullptr, nullptr});
            homeMotorStop_ = homeStop;
        }

        {
            auto *toggle = new QPushButton(header);
            toggle->setObjectName("fpgaMotorShowReadbacks");
            toggle->setCheckable(true);
            toggle->setCursor(Qt::PointingHandCursor);
            toggle->setFocusPolicy(Qt::TabFocus);
            label(toggle, QStringLiteral("查看寄存器"), QStringLiteral("Registers"));
            titleRow->addWidget(toggle, 0, Qt::AlignVCenter);

            auto *registers = new QWidget(body);
            auto *readbackGrid = new QGridLayout(registers);
            readbackGrid->setContentsMargins(12, 10, 12, 12);
            bodyLayout->addWidget(registers);
            registers->hide();
            connect(toggle, &QPushButton::toggled, body, [controls, registers](bool checked) {
                controls->setVisible(!checked);
                registers->setVisible(checked);
            });
            readbacks(readbackGrid,0,{{0x6704,"电机控制","Motor control"},{0x6708,"电机状态","Motor status"},{0x670c,"电机错误","Motor errors"},
                {0x6714,"当前步进频率","Actual step frequency"},{0x6718,"固定 50% 占空比半周期 / tick","Fixed 50% duty half-period / ticks"},{0x671c,"固定 10 ms 方向建立时间 / tick","Fixed 10 ms direction setup / ticks"},
                {0x6724,"输出脉冲软件位置（非机械位置）","Software pulse position (not mechanical position)"},{0x6728,"剩余脉冲","Remaining pulses"},{0x6730,"固定极性 4（EN 高为禁用）","Fixed polarity 4 (EN high disables)"},
                {0x6738,"真实心跳累计数","Real heartbeat count"},{0x673c,"DM422 核心状态","DM422 core status"},{0x6744,"失联超时 / ms","Heartbeat timeout / ms"}});
        }
    };
    addMotorCard(motionLayout ? motionLayout : advancedLayout, !motionLayout);
    auto *lc=card(advanced,advancedLayout,QStringLiteral("LC 双路 UART 状态"),QStringLiteral("LC UART status"),QStringLiteral("cable"));auto *lcGrid=new QGridLayout(lc);
    lcStatus_=new QLabel(lc);lcStatus_->setObjectName("fpgaLcStatus");lcStatus_->setWordWrap(true);lcGrid->addWidget(lcStatus_,0,0,1,3);
    readbacks(lcGrid,1,{{0x6800,"LC0 模块 ID","LC0 module ID"},{0x6808,"LC0 UART 状态","LC0 UART status"},{0x6828,"LC0 完成发送字节数","LC0 transmitted byte count"},{0x682c,"LC0 接收字节数","LC0 received byte count"},
        {0x6900,"LC1 模块 ID","LC1 module ID"},{0x6908,"LC1 UART 状态","LC1 UART status"},{0x6928,"LC1 完成发送字节数","LC1 transmitted byte count"},{0x692c,"LC1 接收字节数","LC1 received byte count"}});
    auto *maintenance=card(advanced,advancedLayout,QStringLiteral("设备维护与上传"),QStringLiteral("Maintenance and upload"),QStringLiteral("wrench"));auto *maintenanceGrid=new QGridLayout(maintenance);
    auto *module=new VaporView::SingleLevelPopupComboBox(maintenance);module->setObjectName("fpgaMaintenanceModule");maintenanceGrid->addWidget(module,0,0,1,2);
    for(auto pair:{qMakePair("时间同步",2),qMakePair("WMS0 / DAC0",0x10),qMakePair("WMS1 / DAC1",0x11),qMakePair("ADC0",0x20),qMakePair("ADC1",0x21),qMakePair("DLIA0",0x30),qMakePair("DLIA1",0x31),qMakePair("PTB210",0x40),qMakePair("HC2A",0x41),qMakePair("EPSILON",0x42),qMakePair("BMP390",0x43),qMakePair("SHT45",0x44),qMakePair("SDFM300",0x45),qMakePair("AI8",0x46),qMakePair("步进电机",0x47)})module->addItem(QString::fromUtf8(pair.first),pair.second);
    operation(maintenanceGrid,1,0,"复位所选模块","Reset selected module","fpgaModuleReset",[module]{return QJsonObject{{"op","maintenance"},{"source",module->currentData().toInt()},{"action",8},{"value",0}};});operation(maintenanceGrid,1,1,"清除所选模块错误","Clear selected errors","fpgaModuleClearErrors",[module]{return QJsonObject{{"op","maintenance"},{"source",module->currentData().toInt()},{"action",9},{"value",0}};});
    operation(maintenanceGrid,2,0,"清空所选传感器 FIFO","Clear sensor FIFO","fpgaSensorClearFifo",[module]{return QJsonObject{{"op","sensor_action"},{"source",module->currentData().toInt()},{"action",3},{"value",0}};});
    auto *watchdog=number(maintenanceGrid,5,"看门狗超时 / ms (0 关闭，启用至少 30000)","fpgaWatchdogTimeout",0,3600000,0);operation(maintenanceGrid,5,2,"设置看门狗","Set watchdog","fpgaWatchdogSet",[watchdog]{return QJsonObject{{"op","watchdog"},{"value",watchdog->value()}};});
    auto *arbiter=number(maintenanceGrid,6,"仲裁模式 (0 优先级 / 1 轮转)","fpgaArbiter",0,1,0);auto *highWater=number(maintenanceGrid,7,"上传高水位 / 字节","fpgaHighWater",1,4294967295.0,4096);auto *burst=number(maintenanceGrid,8,"高优先级突发上限","fpgaHighBurst",1,65535,8);operation(maintenanceGrid,8,2,"应用上传参数","Apply stream settings","fpgaStreamConfigure",[=]{return QJsonObject{{"op","stream_config"},{"mode",arbiter->value()},{"high_water",highWater->value()},{"burst",burst->value()}};});
    readbacks(maintenanceGrid,9,{{0x0040,"当前看门狗超时","Actual watchdog timeout"},{0x6134,"HC2A 探头报警字节","HC2A probe alarm byte"},{0x6140,"HC2A 最近解析报警","HC2A latest parsed alarm"},
        {0x6144,"HC2A 成功记录数","HC2A valid record count"},{0x6148,"HC2A 格式错误数","HC2A format error count"},{0x614c,"HC2A HCD2 诊断签名","HC2A HCD2 signature"},{0x615c,"HC2A 最近失败原因","HC2A latest failure reason"},{0x7010,"当前仲裁模式","Actual arbitration mode"},
        {0x7018,"当前高水位","Actual high-water mark"},{0x7034,"当前突发上限","Actual burst limit"}});
    advancedLayout->addStretch();
    auto *recordingTab=tab(QStringLiteral("记录与诊断"),QStringLiteral("Recording & diagnostics")); auto *recordingLayout=new QVBoxLayout(recordingTab);
    auto *recordingCards=new QHBoxLayout;recordingCards->setObjectName("fpgaRecordingCards");recordingCards->setSpacing(12);recordingLayout->addLayout(recordingCards);
    auto *recording=card(recordingTab,recordingCards,QStringLiteral("采集记录"),QStringLiteral("Acquisition recording"),QStringLiteral("pencil")); auto *rl=new QVBoxLayout(recording);
    recordSensors_=new FpgaCheckBox(recording); label(recordSensors_,QStringLiteral("记录传感器"),QStringLiteral("Record sensors"));
    recordDlia_=new FpgaCheckBox(recording); recordDlia_->setObjectName("fpgaRecordHarmonics");
    label(recordDlia_,QStringLiteral("保存 H1/H2（必选）"),QStringLiteral("Save H1/H2 (required)"));
    recordDlia_->setChecked(true); recordDlia_->setEnabled(false);
    recordDlia_->setToolTip(text("完整 H1/H2 和归一化峰值写入 Session；归一化波形在查看时计算。", "Full H1/H2 and normalized peaks are recorded in the session; normalized waveforms are calculated when viewed."));
    recordDlia_->setVisible(false);
    rl->addWidget(recordSensors_);
    auto *recordingHint=new QLabel(recording); recordingHint->setObjectName(QStringLiteral("fpgaRecordingHint")); recordingHint->setProperty("fpgaSecondaryText",true); recordingHint->setWordWrap(true);
    label(recordingHint,QStringLiteral("H1/H2 会随 FPGA Session 自动保存。此处只控制 Session 记录。"),QStringLiteral("H1/H2 is saved automatically with the FPGA Session. This section controls the Session only.")); rl->addWidget(recordingHint);
    for(auto *c : {recordSensors_}) connect(c,&QCheckBox::toggled,this,[this]{emit configurationChanged(configuration());});
    record_=new QPushButton(recording); record_->setObjectName("fpgaRecord"); rl->addWidget(record_); connect(record_,&QAbstractButton::clicked,this,[this]{emit recordingRequested(!recording_);});
    recordStatus_=new QLabel(recording); recordStatus_->setObjectName("fpgaRecordingStatus"); recordStatus_->setWordWrap(true); rl->addWidget(recordStatus_);
    auto *files=new QHBoxLayout; rl->addLayout(files);
    auto *replay=new QPushButton(recording);replay->setObjectName("fpgaReplay"); label(replay,QStringLiteral("打开离线记录"),QStringLiteral("Open offline recording")); files->addWidget(replay);
    auto *exportButton=new QPushButton(recording);exportButton->setObjectName("fpgaExport"); label(exportButton,QStringLiteral("导出记录"),QStringLiteral("Export recording")); files->addWidget(exportButton);
    connect(replay,&QPushButton::clicked,this,[this]{emit replayRequested({});});
    connect(exportButton,&QPushButton::clicked,this,[this]{emit exportRequested({});});
    auto *diagnosticCard=card(recordingTab,recordingCards,QStringLiteral("诊断日志"),QStringLiteral("Diagnostics"),QStringLiteral("scroll-text"));
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
    for (auto *body : {waves,common,parameters,sensors,advanced,recordingTab}) {
        body->layout()->setProperty("fpgaCardOuterMargins",true);
        body->layout()->setContentsMargins(18,12,5,8); body->layout()->setSpacing(12);
    }
    for (auto *button : findChildren<QPushButton*>()) button->setFocusPolicy(Qt::TabFocus);
    for (auto *button : {disconnect_,refresh_}) button->setProperty("fpgaSecondaryAction",true);
    replay->setProperty("fpgaSecondaryAction",true); exportButton->setProperty("fpgaSecondaryAction",true);
    for (const char *name : {"fpgaAcquisitionStop","fpgaRawStop"}) findChild<QPushButton*>(QString::fromLatin1(name))->setProperty("fpgaSecondaryAction",true);
    connect(module,&QComboBox::currentIndexChanged,this,[this]{updateActions();});
    setConfiguration(config_);setExtensions(0); setLanguage(false);setTheme(false); setConnectionState(false,false); setRecordingState(false);
    auto *freshnessTimer = new QTimer(this);
    freshnessTimer->setInterval(1000);
    connect(freshnessTimer, &QTimer::timeout, this, [this] {
        if (!connected_) return;
        bool changed = false;
        const auto now = measurementClock_.elapsed();
        for (auto it = readingTimes_.cbegin(); it != readingTimes_.cend(); ++it)
            if (now-it.value() >= 4000 && !historicalReadings_.contains(it.key())) { historicalReadings_.insert(it.key()); changed = true; }
        QVector<quint16> staleWaves;
        for (auto it = waveTimes_.cbegin(); it != waveTimes_.cend(); ++it)
            if (now-it.value() >= 4000) staleWaves.append(it.key());
        for (const quint16 source : staleWaves) {
            clearWaveformDisplay(source, "无实时波形 · 数据停止更新", "No live waveform · data stopped");
            changed = true;
        }
        if (changed) refreshMeasurements();
    });
    freshnessTimer->start();
}

FpgaControlConfig FpgaControlPage::configuration() const {
    auto c=config_; for(const auto &f:fields_)f.write(c,f.input->value());
    c.backend=backend_->currentData().toString();c.pressureSource=0x40; c.recordSensors=recordSensors_->isChecked();c.recordDlia=true;c.recordRaw=false;return c;
}
void FpgaControlPage::updateDac1Target(const FpgaControlConfig &config) {
    const auto &wave = config.wms[0];
    const auto range = Dac1::targetRange(wave.bias,wave.scanAmplitude,wave.sineAmplitude);
    const bool allowed = range.withinInputRange();
    // A millivolt Vpp step can move an extremum by half a millivolt.
    const auto format = [allowed](double v) {
        if (allowed && std::abs(v) < 0.0005) v = 0;
        return QString::number(v, 'f', allowed ? 3 : 4);
    };
    QString message = text("DAC1 目标波形范围 %1 至 %2 V。","DAC1 target waveform range %1 to %2 V.")
        .arg(format(range.lowV),format(range.highV));
    if (!allowed)
        message += text(" 超出驱动板 0–5 V 输入范围，请调整偏置或峰峰值。"," Outside the 0–5 V driver input range. Adjust the bias or amplitudes.");
    dac1Target_->setText(message);
}
void FpgaControlPage::setConfiguration(const FpgaControlConfig &c) {
    config_=c;
    for(int i=0;i<2;++i)if(cutoff_[i]) {const QSignalBlocker block(cutoff_[i]);cutoff_[i]->setCurrentIndex(cutoff_[i]->findData(c.dlia[i].cutoffHz));}
    if(ai8Scan_) {const QSignalBlocker block(ai8Scan_);ai8Scan_->setChecked(c.ai8.scan && (extensions_&VaporView::FpgaExtensions::Ai8Scan));}
    if(auto* channel=findChild<QComboBox*>("fpgaAi8RuntimeChannel")) {const QSignalBlocker block(channel);channel->setCurrentIndex(channel->findData(c.ai8.channel));}
    for(const auto& spec:VaporView::FpgaRemote::sensorParameters())if(auto *input=sensorParameterInputs_.value(spec.address)){const QSignalBlocker block(input);input->setValue(c.sensorParameters.value(spec.source).value(spec.key).toDouble(hardwareValues_.value(spec.address,spec.fallback)));}
    for(const auto &f:fields_){const QSignalBlocker block(f.input);f.input->setValue(f.read(c));}
    if (backend_->currentData().toString() != QLatin1String(VaporView::Ground::Devices::FpgaVirtualTcpBackend)) {
        const QSignalBlocker block(backend_);
        backend_->setCurrentIndex(backend_->findData(c.backend));
    }
    const QSignalBlocker bs(recordSensors_),bd(recordDlia_);
    recordSensors_->setChecked(c.recordSensors);recordDlia_->setChecked(true);
    for(int i=0;i<2;++i){auto *cal=findChild<QCheckBox*>(QString("fpga%1Calibrated").arg(i));const QSignalBlocker b(cal);cal->setChecked(c.frontend[i].calibrated);}
    updateDac1Target(configuration());
    updateActions();
}
void FpgaControlPage::lockOperation(){busy_=true;updateActions();}
bool FpgaControlPage::motorBusy() const {
    return connected_ && ((hardwareValues_.value(0x6708)&8) || (hardwareValues_.value(0x673c)&2));
}
bool FpgaControlPage::motorLocked() const { return motorPhase_!=MotorPhase::Idle || motorBusy(); }
bool FpgaControlPage::prepareMotorOperation(const QJsonObject& request) {
    const auto op=request.value("op").toString();const int action=request.value("action").toInt();
    const bool safety=op=="motor" && (action==2 || (action==4 && request.value("value").toInt()==0));
    if(motorLocked()&&!safety && (op=="motor" || op=="motor_config" ||
        ((op=="maintenance" || op=="sensor_action") && request.value("source").toInt()==0x47)))return false;
    if(op=="motor_config" || (safety&&action==4))motorLeaseRequested_=false;
    if(op!="motor")return true;
    if(action==4 && request.value("value").toInt()==1)motorLeaseRequested_=true;
    if(safety)motorPendingOperation_=request;
    if(action==1) {
        if (request.contains("frequency")) motorLeaseRequested_ = true;
        motorPendingOperation_=request;motorPhase_=MotorPhase::Pending;
        motorHasBaselineId_=hardwareValues_.contains(0x6734);motorBaselineId_=hardwareValues_.value(0x6734);
        motorStartEventTick_=motorLastEventTick_;motorSawBusy_=motorCompletionKnown_=motorCompletionFault_=false;
        const auto actualFrequency=hardwareValues_.value(0x6714,100);
        const double frequency = request.value("frequency").toDouble(actualFrequency>=20&&actualFrequency<=400?actualFrequency:100);
        motorEstimatedMs_=10+qint64(std::ceil(std::abs(request.value("value").toDouble())*1000/qMax(1.0,frequency)));
        motorMoveClock_.start();motorResultZh_.clear();motorResultEn_.clear();updateMotorProgress();
    }
    return true;
}
void FpgaControlPage::endMotorEstimate(const char *zh,const char *en) {
    motorPhase_=MotorPhase::Idle;motorPendingOperation_={};motorMoveClock_.invalidate();
    motorResultZh_=QString::fromUtf8(zh);motorResultEn_=QString::fromUtf8(en);
    updateMotorProgress();updateActions();
}
void FpgaControlPage::finishMotorOperation(const QJsonObject& request,bool success) {
    if(motorPendingOperation_.isEmpty() || request!=motorPendingOperation_)return;
    motorPendingOperation_={};
    if(!success || !ready_) {
        if (request.contains("frequency")) motorLeaseRequested_ = false;
        motorPhase_=MotorPhase::Unconfirmed;motorMoveClock_.invalidate();updateMotorProgress();updateActions();return;
    }
    const int action=request.value("action").toInt();
    if(action==2)endMotorEstimate("已停止；保持使能。","Stopped; motor remains enabled.");
    else if(action==4)endMotorEstimate("电机已停用。","Motor disabled.");
    else {
        motorPhase_=MotorPhase::Running;
        if(motorCompletionKnown_)endMotorEstimate(motorCompletionFault_?"运动已结束；设备报告故障或未完成，请检查状态。":"设备已确认运动结束（脉冲输出完成）。",
            motorCompletionFault_?"Motion ended with a fault or remaining pulses; check device status.":"Device confirmed motion ended (pulse output complete).");
        else {updateMotorProgress();updateActions();}
    }
}
void FpgaControlPage::updateMotorReadback() {
    if(motorPhase_==MotorPhase::Idle || (!hardwareValues_.contains(0x6708)&&!hardwareValues_.contains(0x673c)))return;
    const bool busy=motorBusy();
    const bool matchingId=motorHasBaselineId_ && hardwareValues_.contains(0x6734) && hardwareValues_.value(0x6734)==quint32(motorBaselineId_+1);
    if(busy && (matchingId || !motorHasBaselineId_))motorSawBusy_=true;
    if(!busy && (matchingId || (!motorHasBaselineId_&&motorSawBusy_))) {
        motorCompletionKnown_=true;
        motorCompletionFault_=(hardwareValues_.value(0x673c)&8) || hardwareValues_.value(0x670c) || hardwareValues_.value(0x6728);
        if(motorPendingOperation_.isEmpty())endMotorEstimate(motorCompletionFault_?"运动已结束；设备报告故障或未完成，请检查状态。":"设备已确认运动结束（脉冲输出完成）。",
            motorCompletionFault_?"Motion ended with a fault or remaining pulses; check device status.":"Device confirmed motion ended (pulse output complete).");
    }
}
void FpgaControlPage::updateMotorProgress() {
    QString message;
    if (motorPhase_ == MotorPhase::Pending) {
        message = text("已提交运动，等待指令结果；预计用时 %1 s。", "Move submitted; awaiting command result. Estimated duration: %1 s.")
            .arg(motorEstimatedMs_ / 1000.0, 0, 'f', 1);
    } else if (motorPhase_ == MotorPhase::Unconfirmed) {
        message = text("指令失败，执行结果未确认；请等待本次运动结束状态，或显式停止/停用后确认。",
                       "Command failed; execution is unconfirmed. Await this move's completion status, or explicitly stop/disable and confirm.");
    } else if (motorPhase_ == MotorPhase::Running) {
        const qint64 remaining = qMax<qint64>(0, motorEstimatedMs_ - motorMoveClock_.elapsed());
        if (!remaining) {
            message = text("预计已到时，等待完成确认；尚不能确认实际运动结束。",
                           "Estimated time elapsed; waiting for completion confirmation. Actual motion end is unconfirmed.");
        } else {
            message = text("预计进度 %1% · 预计剩余 %2 s（本地计时，不代表机械到位）。",
                           "Estimated progress %1% · Estimated remaining %2 s (local timer; mechanical position is unconfirmed).")
                .arg(qBound<qint64>(0LL, 100 * motorMoveClock_.elapsed() / qMax<qint64>(1, motorEstimatedMs_), 100LL))
                .arg(remaining / 1000.0, 0, 'f', 1);
        }
    } else if (motorBusy()) {
        message = text("设备运动中，进度未知；未建立本地运动估算。", "Device is moving; progress is unknown. No local estimate is available.");
    } else {
        message = english_ ? motorResultEn_ : motorResultZh_;
    }
    if (homeMotorState_) {
        const QString state = !ready_ ? text("未连接", "Offline")
            : message.isEmpty() ? text("空闲", "Idle") : QString();
        if (homeMotorState_->text() != state) homeMotorState_->setText(state);
        if (homeMotorState_->isHidden() != state.isEmpty()) homeMotorState_->setVisible(!state.isEmpty());
    }
    for (const auto &view : motorViews_) {
        if (view.progress->text() != message) view.progress->setText(message);
        if (view.progress->isHidden() != message.isEmpty()) view.progress->setVisible(!message.isEmpty());
    }
}
void FpgaControlPage::updateActions(){
    for(const auto& spec:VaporView::FpgaRemote::sensorParameters())if(auto *input=sensorParameterInputs_.value(spec.address))input->setEnabled(!busy_ && (!connected_ || (hardwareValues_.contains(0x6004+(spec.source-0x40)*0x100) && !(hardwareValues_.value(0x6004+(spec.source-0x40)*0x100)&1))));
    const auto backend = backend_->currentData().toString();
    const bool virtualTcp = backend == QLatin1String(VaporView::Ground::Devices::FpgaVirtualTcpBackend);
    locator_->setToolTip(locator_->currentText()+"\n"+(virtualTcp
        ? text("虚拟 FPGA 的本机回环端点。", "Local loopback endpoint for the virtual FPGA.")
        : backend == QStringLiteral("simulator")
        ?text("本地 VLP1 协议模拟器，不打开实体 USB。","Local VLP1 protocol simulator; no physical USB access.")
        :text("选择 GP01 FX3（04B4:00F1），插拔后点击刷新设备。连接时核验固件身份。","Select GP01 FX3 (04B4:00F1); refresh after plugging/unplugging. Firmware identity is verified on connect.")));
    auto selected=locator_->currentData(Qt::UserRole+1).toJsonObject();
    const auto devices=usbDiscovery_.value("devices").toArray();
    if(locator_->currentIndex()==0 && devices.size()==1) selected=devices.first().toObject();
    const bool usable=property("fpgaUiTestMode").toBool() || ((!usbRemote_||backend!="simulator")&&selected.value("available").toBool()&&!selected.value("locator").toString().isEmpty()
        &&(backend=="auto"||backend==selected.value("backend").toString())&&usbDiscovery_.value("error").toString().isEmpty());
    connect_->setEnabled(!connected_&&!busy_&&!usbDiscovering_&&usbHostAvailable_&&usable);
    locator_->setEnabled(!connected_&&!busy_&&!usbDiscovering_&&usbHostAvailable_);
    backend_->setEnabled(!virtualTcp && !connected_ && !busy_ && !usbDiscovering_);
    restoreWms_->setEnabled(!busy_);
    discover_->setEnabled(!busy_&&!usbDiscovering_&&usbHostAvailable_);
    disconnect_->setEnabled(connected_&&!busy_);for(auto *w:onlineControls_) {
        const quint32 bit=w->property("fpgaExtension").toUInt();const bool safety=w->property("fpgaSafetyControl").toBool();
        const auto apiAddress=w->property("fpgaApiAddress").toUInt();
        const bool api=!apiAddress || hardwareValues_.value(apiAddress)==w->property("fpgaApiValue").toUInt();
        const bool tec103=!w->property("tec103BridgeControl").toBool()
            || (hardwareValues_.value(0x6800)==0x00480100 && hardwareValues_.value(0x6900)==0x00490100);
        w->setEnabled((safety?connected_&&(ready_||hardwareValues_.value(0x6700)==0x00470200):ready_&&!busy_)&&(!bit || (extensions_&bit))&&tec103&&api);
    }refresh_->setEnabled(connected_&&!busy_);record_->setEnabled(!property("fpgaUiTestMode").toBool()&&(ready_||recording_));
    const bool motorBlocked=motorLocked();
    for (const auto &view : motorViews_) {
        for (const char *name : {"fpgaMotorRun", "fpgaMotorEnable", "fpgaMotorConfigure", "fpgaMotorZero",
                                 "fpgaMotorMoveIn", "fpgaMotorMoveOut"}) {
            if (auto *button = view.card->findChild<QPushButton *>(QString::fromLatin1(name)))
                button->setEnabled(button->isEnabled() && !motorBlocked);
        }
        if (view.frequency) view.frequency->setEnabled(!motorBlocked && !busy_);
        if (view.steps) view.steps->setEnabled(!motorBlocked && !busy_);
    }
    if (homeMotorStop_)
        homeMotorStop_->setVisible(homeMotorStop_->isEnabled() && (motorBlocked || (hardwareValues_.value(0x6708) & 1)));
    if(auto *module=findChild<QComboBox*>("fpgaMaintenanceModule"))if(module->currentData().toInt()==0x47 && motorBlocked)
        for(const char *name:{"fpgaModuleReset","fpgaModuleClearErrors","fpgaSensorClearFifo"})
            if(auto *button=findChild<QPushButton*>(QString::fromLatin1(name)))button->setEnabled(false);
    if(ai8Scan_)ai8Scan_->setEnabled(ready_&&!busy_&&(extensions_&VaporView::FpgaExtensions::Ai8Scan)&&!(hardwareValues_.value(0x6604)&1));
    if(auto *applyAi8=findChild<QPushButton*>("fpgaApplyAi8"))
        applyAi8->setEnabled(ready_ && !busy_ && hardwareValues_.contains(0x6604) && !(hardwareValues_.value(0x6604)&1));
}
void FpgaControlPage::updateCaptureStatus(){
    if(!captureStatus_)return;
    if(!connected_) {
        captureStatus_->setText(text("采集状态：未连接","Capture: offline"));
        captureStatus_->setToolTip(captureStatus_->text());
        return;
    }
    bool acquiring=true;
    for(quint32 control:{0x4004u,0x4104u,0x5004u,0x5104u})
        acquiring &= hardwareValues_.contains(control) && (hardwareValues_.value(control)&1);
    const bool raw=rawEnabled_.value(0x20,false)||rawEnabled_.value(0x21,false);
    const auto dacState = [this](quint32 base) {
        if (!ready_ || !hardwareValues_.contains(base+4) || !hardwareValues_.contains(base+8))
            return text("等待读回", "awaiting readback");
        const quint32 status = hardwareValues_.value(base+8);
        if (status & 0x80) return text("故障", "fault");
        if (!(hardwareValues_.value(base+4) & 1)) return text("已关闭", "off");
        if (!hardwareValues_.contains(4)) return text("已开启，就绪未知", "on, readiness unknown");
        const bool operational = (hardwareValues_.value(4) & 1) && (status & 7) == 3;
        return operational ? text("已开启并就绪", "on and ready") : text("已开启，未就绪", "on, not ready");
    };
    captureStatus_->setText(text("H1/H2：%1 · 原始波形：%2\nDAC1：%3 · DAC2：%4", "H1/H2: %1 · Raw waveform: %2\nDAC1: %3 · DAC2: %4")
        .arg(acquiring?text("运行中","running"):text("已停止","stopped"))
        .arg(raw?text("上传中","uploading"):text("已停止","stopped")).arg(dacState(0x3000), dacState(0x3100)));
    captureStatus_->setToolTip(captureStatus_->text());
}
void FpgaControlPage::setExtensions(quint32 bits) {
    extensions_=bits;
    if(ai8Scan_) {const QSignalBlocker block(ai8Scan_);ai8Scan_->setChecked(config_.ai8.scan && (bits&VaporView::FpgaExtensions::Ai8Scan));}
    updateActions();
}
void FpgaControlPage::setTemperatureConnectionState(bool connected,const QString& detail,bool identifying)
{
    setProperty("tec103Connected",connected);
    setProperty("tec103Identifying",identifying);
    setProperty("tec103ConnectionDetail",detail);
    if(auto* label=sensorLabels_.value(0x4a)) {
        label->setText((connected?text("TEC103 已连接","TEC103 connected")
            :identifying ? text("TEC103 连接中","Connecting TEC103") : text("TEC103 未连接","TEC103 disconnected"))+(detail.isEmpty()?QString():QStringLiteral(" · ")+detail));
        label->setToolTip(label->text());
    }
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
void FpgaControlPage::setVirtualTcpMode()
{
    const QSignalBlocker blocker(backend_);
    backend_->addItem(QStringLiteral("Virtual TCP"), QString::fromLatin1(VaporView::Ground::Devices::FpgaVirtualTcpBackend));
    backend_->setCurrentIndex(backend_->count() - 1);
    rebuildUsbDevices();
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
    const bool virtualTcp = backend_->currentData().toString() == QLatin1String(VaporView::Ground::Devices::FpgaVirtualTcpBackend);
    const QString host = virtualTcp ? text("虚拟 FPGA TCP", "Virtual FPGA TCP")
        : text(simulated?"本地模拟器":usbRemote_?"天空端 USB":"本机 USB",
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
    if (connected_ == connected) return;
    const bool lost=connected_&&!connected;connected_=connected;
    if(!connected_)ready_=false;
    if(lost){
        motorLastEventTick_=0;
        if(motorPhase_!=MotorPhase::Idle || !motorPendingOperation_.isEmpty())
            endMotorEstimate("连接已断开，运动状态未确认。","Disconnected; motion state is unconfirmed.");
        for(auto it=latestReadings_.cbegin();it!=latestReadings_.cend();++it)historicalReadings_.insert(it.key());
        rawEnabled_.clear();
        for (quint32 address : {0x3004u,0x3008u,0x3104u,0x3108u}) hardwareValues_.remove(address);
        const auto sources = plots_.keys();
        for(const quint16 source : sources) {
            clearWaveformDisplay(source, "无实时波形 · FPGA 连接已断开", "No live waveform · FPGA disconnected");
        }
        for(auto *l:hardwareLabels_)l->setText(text("历史值 · ","Historical · ")+l->text());
    }
    refreshMeasurements();setConnectionState(ready_,busy_,connectionDetail_);
}
void FpgaControlPage::setConnectionState(bool ready,bool busy,const QString &detail){
    const bool lost=ready_&&!ready;ready_=ready;if(ready_)connected_=true;busy_=busy;connectionDetail_=detail;
    if(!ready_)motorLeaseRequested_=false;
    if(!ready_ && motorPhase_!=MotorPhase::Idle) {motorPhase_=MotorPhase::Unconfirmed;motorMoveClock_.invalidate();}
    status_->setText(ready?text("已连接","Connected"):connected_?text("已连接未就绪 · 禁止写操作","Connected, not ready · Writes disabled"):text("离线","Offline"));
    if (backend_->currentData().toString() == QLatin1String(VaporView::Ground::Devices::FpgaVirtualTcpBackend))
        status_->setText(text("虚拟 FPGA TCP · ", "Virtual FPGA TCP · ") + status_->text());
    if(!usbRemote_&&backend_->currentData().toString()==QStringLiteral("simulator"))
        status_->setText(text("协议模拟 · ","Protocol simulation · ")+status_->text());
    status_->setText(status_->text()+(busy?text(" · 正在处理"," · Working"):QString())+(detail.isEmpty()?QString():" · "+detail));
    status_->setToolTip(status_->text());
    updateMotorProgress();
    updateCaptureStatus();
    if(lost){
        for(auto it=latestReadings_.cbegin();it!=latestReadings_.cend();++it)historicalReadings_.insert(it.key());
        const auto sources = plots_.keys();
        for(const quint16 source : sources) {
            clearWaveformDisplay(source, "无实时波形 · FPGA 未就绪", "No live waveform · FPGA not ready");
        }
        refreshMeasurements();
    }
    updateActions();
}
bool FpgaControlPage::linkDacsOnStart() const
{
    return linkDacs_->isChecked();
}

void FpgaControlPage::setHardwareValues(const QMap<quint32,quint32> &v){
    hardwareValues_=v;
    for(int ch=0;ch<2;++ch) {
        const quint32 d=0x5000+ch*0x100,w=0x2000+ch*0x100;
        if(auto *label=findChild<QLabel*>(QString("fpga%1SosStatus").arg(ch))) {
            const bool supported=v.value(d+0xf0)==VaporView::FpgaDliaSos::Api,custom=v.value(d+0x24)&0x80000000u;
            label->setText(!supported?text("当前固件未确认 SOS API","SOS API not confirmed"):
                custom?text("已生效：%1 阶 SOS · Fc %2 Hz · 设计 Fs %3 Hz / 实际 %4 Hz · 代次 %5",
                    "Active: order %1 SOS · Fc %2 Hz · Design Fs %3 Hz / actual %4 Hz · Generation %5")
                    .arg(v.value(d+0x64)).arg(v.value(d+0xec)).arg(v.value(d+0xe8)).arg(v.value(d+0xfc)).arg(v.value(d+0xf8)):
                text("已生效：内置滤波 %1 Hz · 实际 ADC %2 Hz","Active: builtin %1 Hz · Actual ADC %2 Hz").arg(v.value(d+0x5c)).arg(v.value(d+0xfc)));
        }
        if(auto *label=findChild<QLabel*>(QString("fpga%1PpsStatus").arg(ch))) {
            const auto state=v.value(w+0x54);
            label->setText(v.value(w+0x84)!=VaporView::FpgaDliaSos::PpsApi?text("当前固件未确认 PPS 扫描 API","PPS scan API not confirmed"):
                text("实际模式 %1 · 等待码 %2 · 已就绪 %3 / 扫描忙 %4 / PPS 合格 %5\n接受 %6 · 漏接 %7 · 不合格 %8 · 完成 %9 · 中止 %10 · PPS 序号 %11\n完成计数不代表 USB 帧完整；快照写入当前会话记录。",
                     "Mode %1 · Idle code %2 · Armed %3 / Busy %4 / PPS qualified %5\nAccepted %6 · Missed %7 · Rejected %8 · Completed %9 · Aborted %10 · PPS sequence %11\nCompletion is not USB frame integrity; snapshots are saved with the current session.")
                .arg(v.value(w+0x4c)).arg(qint32(v.value(w+0x50))).arg(bool(state&1)).arg(bool(state&2)).arg(bool(state&64))
                .arg(v.value(w+0x58)).arg(v.value(w+0x5c)).arg(v.value(w+0x88)).arg(v.value(w+0x7c)).arg(v.value(w+0x80)).arg(v.value(w+0x60)));
        }
    }
    if(!rerendering_)updateMotorReadback();
    updateMotorProgress();
    const quint32 calibration=v.value(0x1060);
    const bool frequencyValid=calibration&1,utcValid=frequencyValid&&(calibration&2);
    const auto ppsTick=(quint64(v.value(0x1054))<<32)|v.value(0x1050);
    const auto utcUs=(quint64(v.value(0x105c))<<32)|v.value(0x1058);
    calibrationStatus_->setText(text("PPS 沿新鲜：%1 · 频率校准有效：%2 · UTC 锚点确认：%3\nPPS #%4 · tick %5 · UTC / µs：%6\n仅展示下位机状态，不自动建立 UTC 对应关系。",
        "Recent PPS edge: %1 · Frequency calibrated: %2 · UTC anchor confirmed: %3\nPPS #%4 · tick %5 · UTC / µs: %6\nRead-only firmware status; UTC correspondence is never inferred.")
        .arg(bool(calibration&4)).arg(frequencyValid).arg(utcValid).arg(v.value(0x104c)).arg(ppsTick).arg(utcValid?QString::number(utcUs):text("未确认","Unconfirmed")));
    const QString motorStatus = text("请求使能 %1 · 实际使能 %2 · 心跳存活 %3 · 故障 %4",
        "Requested enable %1 · Actual enable %2 · Heartbeat alive %3 · Fault %4")
        .arg(bool(v.value(0x6708)&1)).arg(bool(v.value(0x6708)&0x40)).arg(bool(v.value(0x6708)&0x20)).arg(bool(v.value(0x673c)&8));
    for (const auto &view : motorViews_)
        if (view.status->text() != motorStatus) view.status->setText(motorStatus);
    lcStatus_->setText(text("LC0 模块匹配：%1 · LC1 模块匹配：%2\nTEC103 原位：CON11 5/7；遥测：6/8。TTL 38400 / 8N1，每台使用内部 TC1；参数与温度在激光温控页显示。",
        "LC0 module matches: %1 · LC1 module matches: %2\nTEC103 in-situ: CON11 5/7; remote: 6/8. TTL 38400 / 8N1, internal TC1 on each controller. See Laser temperature for readings and parameters.")
        .arg(v.value(0x6800)==0x00480100).arg(v.value(0x6900)==0x00490100));
    for(const auto& spec:VaporView::FpgaRemote::sensorParameters())if(!config_.sensorParameters.value(spec.source).contains(spec.key) && v.contains(spec.address))if(auto *input=sensorParameterInputs_.value(spec.address)){const QSignalBlocker block(input);input->setValue(v.value(spec.address));}
    updateActions();
    for(int ch=0;ch<2;++ch)if(v.contains(0x403c+ch*0x100)&&v.contains(0x50)) {
        const quint16 source=0x20+ch;
        const bool enabled=(v.value(0x403c+ch*0x100)&1)&&(v.value(0x50)&(1u<<ch));
        if (rawEnabled_.contains(source) && rawEnabled_.value(source) == enabled) continue;
        const bool restarting = enabled && rawEnabled_.contains(source);
        rawEnabled_[source]=enabled;
        if (restarting)
            clearWaveformDisplay(source, "等待新的原始波形", "Waiting for new raw waveform");
        else if (!enabled)
            clearWaveformDisplay(source, "无实时波形 · 原始波形上传已停止", "No live waveform · raw upload stopped");
    }
    updateCaptureStatus();
    for(auto it=v.cbegin();it!=v.cend();++it)if(auto *l=hardwareLabels_.value(it.key())) {
        const auto module=l->property("fpgaModule").toString();
        const quint32 base=it.key()&~0xffu,offset=it.key()&0xff;
        if((base==0x5000 || base==0x5100) && offset==0x5c && (v.value(base+0x24)&0x80000000u)) {
            l->setText(text("实际：自定义 %1 阶 SOS，%2 Hz；内置档位仅在高级控制显式恢复时应用。",
                "Active: order %1 SOS, %2 Hz; restore the selected builtin explicitly in Advanced controls.")
                .arg(v.value(base+0x64)).arg(v.value(base+0xec)));continue;
        }
        if ((base==0x2000||base==0x2100) && (offset==0x14||offset==0x18||offset==0x20)) {
            const qint32 code = qint32(it.value());
            const bool bias = offset == 0x18;
            double volts = (bias ? 1 : 2) * double(code) * Wms2OutputFullScaleV / WmsQ31Scale;
            if (base == 0x2000) {
                if (bias) volts = Dac1::biasVolts(code);
                else if (offset == 0x14) volts = Dac1::scanVpp(code);
                else volts = Dac1::modulationVpp(code);
            }
            l->setText(text("寄存器估算 %1 %2","Register estimate %1 %2").arg(volts,0,'f',3).arg(bias ? "V" : "Vpp"));
            l->setToolTip(text("按标定公式将 FPGA 的 WMS 参数寄存器码换算为电压。实际模拟电压请在对应输出端用示波器测量。",
                "Voltage calculated from the FPGA WMS parameter register using the calibration. Measure the analog voltage at the corresponding output with an oscilloscope."));
        } else if((it.key()&0xff)==8 && !module.isEmpty()) {
            l->setText(QString("%1 · %2 %3 · %4 %5 · %6 %7 · %8 %9").arg(module)
                .arg(text("启用","Enabled")).arg(bool(it.value()&1)).arg(text("就绪","Ready")).arg(bool(it.value()&(module=="ADC"?0x10:2)))
                .arg(text("待生效","Pending")).arg(bool(it.value()&4)).arg(text("错误","Error")).arg(bool(it.value()&0x80)));
        } else if ((base >= 0x2000 && base <= 0x2100) || (base >= 0x3000 && base <= 0x3100) ||
                   (base >= 0x5000 && base <= 0x5100)) {
            l->setText(text("实际 %1", "Actual %1").arg(it.value()));
        } else l->setText(QString("%1 0x%2 = %3 (0x%4)").arg(module).arg(it.key(),4,16,QChar('0')).arg(it.value()).arg(it.value(),8,16,QChar('0')));
    }
}
void FpgaControlPage::updateSensor(const VaporView::FpgaSensor::Reading &r){
    if(r.source==0x4a && hardwareValues_.value(0x6800)==0x00480100)return;
    auto *l=sensorLabels_.value(r.source);if(!l)return;latestReadings_[r.source]=r;if(!rerendering_&&connected_)historicalReadings_.remove(r.source);QStringList parts;
    if (!rerendering_) readingTimes_[r.source] = measurementClock_.elapsed();
    if(r.pressurePa)parts<<QString::number(*r.pressurePa,'f',3)+" Pa";if(r.temperatureC)parts<<QString::number(*r.temperatureC,'f',2)+" °C";
    if(r.humidityPct)parts<<QString::number(*r.humidityPct,'f',2)+" %RH";if(r.distanceMm)parts<<QString::number(*r.distanceMm)+" mm";
    if(r.ai8Channel)parts<<QStringLiteral("CH%1").arg(*r.ai8Channel);
    if(r.rd105TargetMicroC)parts<<QStringLiteral("SP %1 °C").arg(*r.rd105TargetMicroC/1000000.0,0,'f',5);
    if(r.rd105ActualMicroC)parts<<QStringLiteral("PV %1 °C").arg(*r.rd105ActualMicroC/1000000.0,0,'f',5);
    if(r.ai8PvRaw)parts<<"PV "+QString::number(*r.ai8PvRaw*0.1,'f',1)+" °C";if(r.ai8SpRaw)parts<<"SP "+QString::number(*r.ai8SpRaw*0.1,'f',1)+" °C";
    if(r.ai8Alarm)parts<<text("报警 ","Alarm ")+QString::number(*r.ai8Alarm);if(r.ai8Host)parts<<"host "+QString::number(*r.ai8Host);
    if(r.hc2aDeviceError)parts<<text("HC2A 探头报警 ","HC2A probe alarm ")+QString::number(*r.hc2aDeviceError);
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
void FpgaControlPage::clearWaveformDisplay(quint16 source, const QString &zh, const QString &en)
{
    latestWaves_.remove(source);
    waveTimes_.remove(source);
    QVector<SamplePlot*> plots;
    if (auto *plot = static_cast<SamplePlot*>(plots_.value(source))) plots.append(plot);
    if (auto *plot = static_cast<SamplePlot*>(secondPlots_.value(source))) plots.append(plot);
    for (auto *plot : iqPlots_.value(source)) plots.append(static_cast<SamplePlot*>(plot));
    for (auto *plot : plots) {
        plot->samples.clear();
        plot->triggeredSamples.clear();
        plot->displayOffset = 0;
        plot->setProperty("sampleCount", 0);
        plot->setProperty("sampleMinimum", QVariant());
        plot->setProperty("sampleMaximum", QVariant());
        plot->setProperty("previewStride", 1);
        plot->update();
    }
    if (auto *panel = iqPanels_.value(source)) panel->setVisible(false);
    auto *summary = waveLabels_.value(source);
    label(summary, zh, en);
    summary->setToolTip(summary->text());
    emit waveformDisplayUpdated(source, {}, {}, summary->text(), 0, false);
}

void FpgaControlPage::updateWaveform(const VaporView::FpgaWave::CompletedStream &s, bool replay){
    auto *plot=static_cast<SamplePlot*>(plots_.value(s.source));auto *l=waveLabels_.value(s.source);if(!plot||!l)return;
    if (!replay && (!ready_ || !rawEnabled_.value(s.source, true))) return;
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
    double maxNormalizedH2 = std::numeric_limits<double>::quiet_NaN();
    QVector<double> normalizedH2;
    const auto publishDisplay = [&]() {
        QVector<QVector<double>> samples;
        QStringList components = adc ? QStringList{QStringLiteral("ADC")}
            : s.format == 1 ? QStringList{QStringLiteral("I1"), QStringLiteral("Q1"), QStringLiteral("I2"), QStringLiteral("Q2")}
                            : QStringList{QStringLiteral("H1"), QStringLiteral("H2")};
        for (int i = 0; i < components.size(); ++i) samples.append(activePlots[i]->samples);
        if (!adc) {
            samples.append(normalizedH2);
            components.append(QStringLiteral("H2/max(H1)"));
        }
        emit waveformDisplayUpdated(s.source, samples, components, l->text(), s.totalPoints, s.preview);
        if (!adc && !rerendering_)
            emit secondHarmonicPeakReady(s.source, s.cycleId, s.timestamp,
                maxNormalizedH2, s.preview);
    };
    const quint32 expectedBytes=adc?(s.format<=1?4u:0u):s.format==0?8u:s.format==1?16u:s.format==2?24u:0u;
    const quint32 availablePoints = expectedBytes ? quint32(s.pointBytes.size() / expectedBytes) : 0;
    const quint32 displayPoints = s.preview ? availablePoints : s.totalPoints;
    const bool incomplete = !s.complete || s.partial || s.overflow || s.continuityError;
    if (incomplete || !expectedBytes || s.bytesPerPoint != expectedBytes || !displayPoints
        || s.pointBytes.size() % expectedBytes != 0 || displayPoints != availablePoints
        || (s.preview && (!s.previewStride || displayPoints > 64 || quint64(displayPoints-1)*s.previewStride >= s.totalPoints))) {
        const QString zh = replay ? QStringLiteral("历史回放 · ") : QStringLiteral("无实时波形 · ");
        const QString en = replay ? QStringLiteral("Historical replay · ") : QStringLiteral("No live waveform · ");
        clearWaveformDisplay(s.source, zh + (incomplete ? "当前周期缺片或质量异常" : "当前波形格式或长度无效"),
            en + (incomplete ? "current cycle incomplete or faulty" : "current waveform format or length invalid"));
        if (!adc && !rerendering_)
            emit secondHarmonicPeakReady(s.source, s.cycleId, s.timestamp,
                std::numeric_limits<double>::quiet_NaN(), false);
        return;
    }
    if (!rerendering_) {
        latestWaves_[s.source] = s;
        if (replay) waveTimes_.remove(s.source);
        else waveTimes_[s.source] = measurementClock_.elapsed();
    }
    for(auto *p:activePlots)p->samples.clear();const int stride=qMax(1,int(displayPoints)/1000);const int components=adc?1:int(s.bytesPerPoint/4);
    double referenceH1 = 0;
    for(int point=0;point<int(displayPoints);++point){const int offset=point*components*4;if(offset+components*4>s.pointBytes.size())break;
        const double value=adc?double(rawSample(s,point)):double(qFromLittleEndian<qint32>(reinterpret_cast<const uchar*>(s.pointBytes.constData()+offset+component*4)));
        if (!adc) {
            const auto read = [&](int index) { return double(qFromLittleEndian<qint32>(reinterpret_cast<const uchar*>(s.pointBytes.constData()+offset+index*4))); };
            const double h1 = s.format == 1 ? std::hypot(read(0), read(1)) : read(component);
            const double h2 = s.format == 1 ? std::hypot(read(2), read(3)) : read(component+1);
            maxH2 = std::isfinite(maxH2) ? qMax(maxH2, h2) : h2;
            referenceH1 = std::max(referenceH1, h1);
            if (point % stride == 0) normalizedH2.append(h2);
        }
        if(point%stride==0) {
            plot->samples.append(volts?value*c.frontend[channel].voltsPerCode*c.frontend[channel].polarity:double(value));
            if(second)second->samples.append(double(qFromLittleEndian<qint32>(reinterpret_cast<const uchar*>(s.pointBytes.constData()+offset+(component+1)*4))));
            if(!adc&&s.format==1)for(int iq=0;iq<2;++iq)
                activePlots[iq+2]->samples.append(double(qFromLittleEndian<qint32>(reinterpret_cast<const uchar*>(s.pointBytes.constData()+offset+(iq+2)*4))));
        }
    }
    int rawEdge = -1;
    if (adc) {
        plot->triggeredSamples.clear();
        if (waveformTrigger_->isChecked() && !s.preview) {
            const quint32 modulation = hardwareValues_.value(0x201c+channel*0x100, c.wms[channel].sineMilliHz);
            rawEdge = rawScanEdge(s, modulation);
        }
        if (rawEdge > 0) {
            // Rotate before display decimation so sub-stride shifts have the same origin.
            for (int i=0; i<int(displayPoints); i+=stride) {
                const double value = double(rawSample(s, (rawEdge+i)%displayPoints));
                plot->triggeredSamples.append(volts ? value*c.frontend[channel].voltsPerCode*c.frontend[channel].polarity : value);
            }
        }
    }
    bool heldSweep = false;
    if (!adc) {
        for (double &sample : normalizedH2)
            sample = VaporView::FpgaWave::normalizedSecondHarmonic(referenceH1, sample);
        maxNormalizedH2 = VaporView::FpgaWave::normalizedSecondHarmonic(referenceH1, maxH2);
    }
    const bool trigger = waveformTrigger_->isChecked() && !adc && s.format != 1 && !s.preview && second->samples.size() > 2;
    if (trigger) {
        const int offset = triggerDliaSweep(*plot, *second, s, heldSweep);
        if (!heldSweep) {
            plot->triggeredSamples = plot->samples;
            second->triggeredSamples = second->samples;
            plot->displayOffset = second->displayOffset = offset;
            second->triggerRate = s.rate;
            second->triggerPoints = s.totalPoints;
            second->triggerFormat = s.format;
            second->lastTrigger.start();
        }
    } else if (second) {
        plot->triggeredSamples.clear(); second->triggeredSamples.clear();
        plot->displayOffset = second->displayOffset = 0;
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
    if (!waveformTrigger_->isChecked())
        l->setText(l->text() + text(" · FPGA 扫描起点", " · FPGA scan origin"));
    if (rawEdge >= 0) l->setText(l->text() + text(" · 跳变沿起点", " · Scan edge aligned"));
    if (trigger)
        l->setText(l->text() + (heldSweep ? text(" · 等待相似周期，显示上次触发", " · Waiting for trigger, showing previous sweep")
                                          : text(" · H1/H2 波形触发", " · H1/H2 waveform trigger")));
    if (replay) l->setText(text("历史回放 · ", "Historical replay · ") + l->text());
    l->setToolTip(l->text());
    for (auto *p : activePlots)
        p->sampleStride = quint64(stride) * (s.preview ? s.previewStride : 1);
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
        if(msg==0x1300) {
            lines<<text("电机位置 %1；剩余 %2；错误 %3","Motor position %1; remaining %2; error %3")
                .arg(qint32(quint32(w[1].toDouble()))).arg(w[2].toDouble()).arg(w[3].toDouble());
            const auto tick=o.value("tick").toString().toULongLong();
            if(motorPhase_!=MotorPhase::Idle && tick>motorStartEventTick_ && tick>motorLastEventTick_
                && o.value("source").toInt()==0x47 && motorHasBaselineId_ && quint32(w[0].toDouble())==quint32(motorBaselineId_+1)) {
                motorCompletionKnown_=true;motorCompletionFault_=w[2].toDouble()!=0 || w[3].toDouble()!=0;
                if(motorPendingOperation_.isEmpty())endMotorEstimate(motorCompletionFault_?"运动已结束；设备报告故障或未完成，请检查状态。":"设备已确认运动结束（脉冲输出完成）。",
                    motorCompletionFault_?"Motion ended with a fault or remaining pulses; check device status.":"Device confirmed motion ended (pulse output complete).");
            }
            motorLastEventTick_=qMax(motorLastEventTick_,tick);
        }
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
void FpgaControlPage::setLanguage(bool en){
    english_ = en;
    const auto retranslate = [this](QWidget *root) {
        for (auto *widget : root->findChildren<QWidget *>())
            if (widget->property("fpgaZh").isValid())
                label(widget, widget->property("fpgaZh").toString(), widget->property("fpgaEn").toString());
    };
    retranslate(this);
    const auto emptyText = text("暂无数据", "No data");
    for (auto *widget : findChildren<QWidget *>()) {
        if (widget->property("fpgaFixedPlot").toBool()) {
            static_cast<SamplePlot *>(widget)->emptyText = emptyText;
            widget->update();
        }
    }
    updateDac1Target(configuration());
    for (const auto &view : motorViews_)
        if (!isAncestorOf(view.card)) retranslate(view.card);
    const bool rendering=rerendering_;rerendering_=true;setHardwareValues(hardwareValues_);rerendering_=rendering;updateMotorProgress();
    backend_->setItemText(backend_->findData(QStringLiteral("simulator")),text("模拟器","Simulator"));
    for(int i=0;i<tabs_->count();++i)tabs_->setTabText(i,tabs_->widget(i)->property(en?"fpgaEn":"fpgaZh").toString());
    for(auto it=sensorLabels_.cbegin();it!=sensorLabels_.cend();++it)if(!latestReadings_.contains(it.key()))it.value()->setText(text("等待有效样本","Waiting for valid sample"));
    for(auto it=waveLabels_.cbegin();it!=waveLabels_.cend();++it) {
        if (latestWaves_.contains(it.key())) continue;
        auto *summary = it.value();
        if (!summary->property("fpgaZh").isValid())
            label(summary, "等待波形", "Waiting for waveform");
        summary->setToolTip(summary->text());
        emit waveformDisplayUpdated(it.key(), {}, {}, summary->text(), 0, false);
    }
    refreshMeasurements();
    if(!usbDiscovering_)rebuildUsbDevices();setConnectionState(ready_,busy_,connectionDetail_);setRecordingState(recording_,recordingDetail_);
}
void FpgaControlPage::refreshMeasurements(){rerendering_=true;const auto readings=latestReadings_;for(const auto &reading:readings)updateSensor(reading);const auto waves=latestWaves_;for(const auto &wave:waves)updateWaveform(wave,!waveTimes_.contains(wave.source));rerendering_=false;}
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
    const QString cardStyle = applyAppThemeTokens(QStringLiteral(
        "QFrame#fpgaSectionCard { background-color: @vv-surface-raised; border: 1px solid @vv-border; border-radius: 12px; }"
        "QFrame#fpgaSectionCard > QWidget#sectionTitleBar { background-color: @vv-surface-raised; border: none; border-bottom: 1px solid @vv-border; border-top-left-radius: 11px; border-top-right-radius: 11px; }"
        "QFrame#fpgaSectionCard > QWidget#sectionTitleBar[fpgaMultilineHeader=false] { min-height: %1px; max-height: %1px; }"
        "QFrame#fpgaSectionCard > QWidget#sectionTitleBar[fpgaMultilineHeader=true] { min-height: %1px; }"
        "QWidget#fpgaCardBody { background-color: @vv-surface-raised; border: none; border-bottom-left-radius: 11px; border-bottom-right-radius: 11px; }"
        "QLabel { background-color: transparent; border: none; color: @vv-text; }"
        "QLabel#sectionTitleLabel { font-size: %2px; font-weight: 700; padding: 0; margin: 0; }"
        "QLabel[fpgaSecondaryText=\"true\"] { color: @vv-text-secondary; font-weight: 400; }"
        "QPushButton#fpgaMotorShowReadbacks { background-color: @vv-surface-alt; color: @vv-text; border: 1px solid @vv-border; border-radius: 6px; padding: 0 %4px; min-height: %3px; max-height: %3px; }"
        "QPushButton#fpgaMotorShowReadbacks:hover { background-color: @vv-surface; color: @vv-primary; }"
        "QPushButton#fpgaMotorShowReadbacks:checked { background-color: @vv-primary-subtle; color: @vv-primary; border-color: @vv-primary; }"
        "QPushButton#fpgaMotorShowReadbacks:focus { border-color: @vv-primary; }")
        .arg(pixels(40)).arg(pixels(16)).arg(pixels(28)).arg(pixels(10)), dark);
    for (const auto &view : motorViews_) {
        if (view.frequency && !isAncestorOf(view.card)) {
            view.card->setPalette(appThemePalette(dark, view.card->palette()));
            view.card->setStyleSheet(cardStyle);
        }
    }
    setStyleSheet(cardStyle + applyAppThemeTokens(QStringLiteral(
        "QWidget#fpgaControlPage { background-color: @vv-window; }"
        "QWidget#fpgaControlPage QTabWidget::pane { border: none; background-color: @vv-window; }"
        "QWidget#fpgaControlPage QTabWidget#fpgaTabs::tab-bar { left: %5px; }"
        "QWidget#fpgaControlPage QTabBar::tab { background-color: @vv-surface-alt; color: @vv-text; border: 1px solid transparent; border-radius: 6px; padding: 0 %1px; margin-right: 4px; min-height: %2px; font-weight: 600; }"
        "QWidget#fpgaControlPage QTabBar::tab:hover { background-color: @vv-primary-subtle; color: @vv-primary; }"
        "QWidget#fpgaControlPage QTabBar::tab:selected { background-color: @vv-primary; color: @vv-white; }"
        "QWidget#fpgaControlPage QTabBar::tab:focus { border-color: @vv-focus; }"
        "QWidget#fpgaControlPage QScrollArea, QWidget#fpgaControlPage QScrollArea > QWidget, QWidget#fpgaControlPage QScrollArea > QWidget > QWidget { background-color: @vv-window; border: none; }"
        "QWidget#fpgaControlPage QScrollArea#mainCardsScrollArea QScrollBar:vertical { width: %4px; }"
        "QWidget#fpgaControlPage QPushButton[fpgaSecondaryAction=\"true\"] { background-color: @vv-surface-alt; color: @vv-text; border: 1px solid @vv-border; }"
        "QWidget#fpgaControlPage QPushButton[fpgaSecondaryAction=\"true\"]:hover { background-color: @vv-primary-subtle; color: @vv-primary; border-color: @vv-primary; }"
        "QWidget#fpgaControlPage QPushButton[fpgaSecondaryAction=\"true\"]:focus { border-color: @vv-focus; }"
        "QWidget#fpgaControlPage QPushButton[fpgaSecondaryAction=\"true\"]:disabled { background-color: @vv-surface-alt; color: @vv-text-muted; border-color: @vv-border; }"
        "QWidget#fpgaControlPage QPushButton[fpgaIconOnly=\"true\"] { background-color: transparent; border: 1px solid transparent; border-radius: 7px; padding: 0; min-width: %3px; max-width: %3px; min-height: %3px; max-height: %3px; }"
        "QWidget#fpgaControlPage QPushButton[fpgaIconOnly=\"true\"]:hover, QWidget#fpgaControlPage QPushButton[fpgaIconOnly=\"true\"]:pressed { background-color: @vv-surface-alt; border-color: transparent; }"
        "QWidget#fpgaControlPage QPushButton[fpgaIconOnly=\"true\"]:focus { border-color: @vv-focus; }"
        "QWidget#fpgaControlPage QPushButton[fpgaIconOnly=\"true\"]:disabled { background-color: transparent; border-color: transparent; }")
        .arg(pixels(12)).arg(pixels(36)).arg(pixels(40)-2).arg(pixels(8)).arg(pixels(18)),dark));
    locator_->setMinimumWidth(pixels(200)); locator_->setMaximumWidth(QWIDGETSIZE_MAX); locator_->setFixedHeight(pixels(40));
    backend_->setMinimumWidth(pixels(104)); backend_->setMaximumWidth(QWIDGETSIZE_MAX); backend_->setFixedHeight(pixels(40));
    for (auto *button : {discover_,connect_,disconnect_}) {
        button->setFixedSize(pixels(40),pixels(40)); button->setIconSize(QSize(pixels(20),pixels(20)));
        QFile file(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("resources/lucide/%1.svg").arg(button->property("fpgaIconName").toString())));
        if (!file.open(QIODevice::ReadOnly)) continue;
        const auto color=AppThemeColor::Primary;
        QByteArray svg=file.readAll(); svg.replace("currentColor",appThemeColorName(color,dark).toUtf8());
        const qreal dpr=button->devicePixelRatioF(); const int size=pixels(20);
        QPixmap pixmap(qRound(size*dpr),qRound(size*dpr)); pixmap.setDevicePixelRatio(dpr); pixmap.fill(Qt::transparent);
        QSvgRenderer renderer(svg); QPainter painter(&pixmap); renderer.render(&painter,QRectF(0,0,size,size)); painter.end(); button->setIcon(QIcon(pixmap));
    }
    const QString iconObjectName = QStringLiteral("fpgaCardIcon");
    auto icons = findChildren<QLabel *>(iconObjectName);
    for (const auto &view : motorViews_)
        if (!isAncestorOf(view.card)) icons.append(view.card->findChildren<QLabel *>(iconObjectName));
    for (auto *icon : icons) {
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
    cancelPendingUiTestOperations();
    simulation_.receiver.reset(); // Discard queued results before restoring the view.
    if(simulation_.worker) {
        QMetaObject::invokeMethod(simulation_.controller,&VaporView::Ground::Devices::FpgaDeviceController::disconnectDevice,Qt::BlockingQueuedConnection);
        simulation_.worker->quit();simulation_.worker->wait();simulation_.worker.reset();
    }
    simulation_.controller=nullptr;simulation_.transport=nullptr;
}

void FpgaControlPage::cancelPendingUiTestOperations()
{
    simulation_.dispatchGeneration->fetch_add(1,std::memory_order_acq_rel);
}

void FpgaControlPage::simulateOperation(const QJsonObject &request)
{
    if(!property("fpgaUiTestMode").toBool() || !simulation_.controller)return;
    auto *controller=simulation_.controller;
    auto *transport=simulation_.transport;
    const auto op=request.value("op").toString();
    const bool motorStop=op=="motor" && (request.value("action").toInt()==2
        || (request.value("action").toInt()==4 && request.value("value").toInt()==0));
    if(op=="disconnect" || motorStop)cancelPendingUiTestOperations();
    if(op=="discover") {
        QPointer<QObject> receiver(simulation_.receiver.get());
        QMetaObject::invokeMethod(controller,[this,controller,receiver] {
            const auto result=controller->discoverDevices();
            if(receiver)QMetaObject::invokeMethod(receiver,[this,result]{setUsbDiscoveryResult(result);},Qt::QueuedConnection);
        },Qt::QueuedConnection);
        return;
    }
    if((op=="motor" || op=="motor_config" || op=="maintenance" || op=="sensor_action")
        && request!=motorPendingOperation_ && !prepareMotorOperation(request))return;
    if(op!="missing_cycle"&&op!="latch_dlia"&&op!="motor_heartbeat")lockOperation();
    const auto config=configuration();
    const auto motorGeneration=op=="motor"?++simulation_.motorGeneration:0;
    QPointer<QObject> receiver(simulation_.receiver.get());
    auto action=[this,controller,transport,request,op,config,receiver,motorGeneration] {
        if(op=="motor") {
            const int action=request.value("action").toInt();
            const bool safety=action==2 || (action==4 && request.value("value").toInt()==0);
            const auto finish=[this,receiver,request,motorGeneration](bool success) {
                if(receiver)QMetaObject::invokeMethod(receiver,[this,request,success,motorGeneration] {
                    if(motorGeneration==simulation_.motorGeneration)finishMotorOperation(request,success);
                },Qt::QueuedConnection);
            };
            if(!VaporView::FpgaRemote::validateControl(request) || (safety?!controller->connected():!controller->ready()||controller->busy())) {finish(false);return;}
            auto observer=std::make_shared<QMetaObject::Connection>();auto done=std::make_shared<bool>(false),dispatching=std::make_shared<bool>(true);
            *observer=connect(controller,&VaporView::Ground::Devices::FpgaDeviceController::snapshot,controller,
                [observer,done,dispatching,finish,safety](quint64,const QJsonObject& document) {
                    const auto event=document.value("event").toString();
                    if(event!="operation_completed"&&event!="operation_failed")return;
                    if(safety&&*dispatching&&document.value("interrupted_by_motor_stop").toBool())return;
                    if(event=="operation_completed" && document.value("operation").toString()!=(safety?QStringLiteral("DM422 停止与状态读回"):QStringLiteral("FPGA 高级控制与读回")))return;
                    QObject::disconnect(*observer);*done=true;finish(event=="operation_completed");
                });
            controller->executeOperation(request);
            *dispatching=false;
            if(!controller->busy()&&!*done){QObject::disconnect(*observer);finish(false);}
            return;
        }
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
        else if(op=="acquisition")controller->setAcquisition(enabled, request.value("enableDacs").toBool(), config);
        else if(op=="raw")controller->setRawEnabled(enabled);
        else if(op=="wms")controller->setWaveform(request.value("channel").toInt(),enabled);
        else if(op=="dac")controller->setDac(request.value("channel").toInt(),enabled);
        else if(op=="temperature")controller->setTemperature(request.value("celsius").toDouble());
        else controller->executeOperation(request);
    };
    const auto generation=simulation_.dispatchGeneration;
    const auto expected=generation->load(std::memory_order_acquire);
    const auto deadline=QDateTime::currentMSecsSinceEpoch()+10000;
    const auto expired=[this,receiver,generation,expected,request,motorGeneration] {
        if(receiver)QMetaObject::invokeMethod(receiver,[this,receiver,generation,expected,request,motorGeneration] {
            if(!receiver)return;
            if(generation->load(std::memory_order_acquire)!=expected) {
                if(request.value("op")=="motor" && motorGeneration==simulation_.motorGeneration)
                    finishMotorOperation(request,false);
                return;
            }
            cancelPendingUiTestOperations();setConnectionState(ready_,false,QStringLiteral("等待 FPGA 空闲已取消或超时；操作未发送。"));
            if(request.value("op")=="motor")finishMotorOperation(request,false);
            emit simulationOperationFailed(false);
        },Qt::QueuedConnection);
    };
    const bool safety=op=="disconnect" || op=="connect" || op=="motor_heartbeat" || motorStop;
    QMetaObject::invokeMethod(controller,[controller,generation,expected,deadline,safety,
        action=std::move(action),expired]() mutable {
        if(generation->load(std::memory_order_acquire)!=expected) {expired();return;}
        if(safety)action();
        else dispatchSimulationWhenIdle(controller,generation,expected,deadline,std::move(action),expired);
    },Qt::QueuedConnection);
}

void FpgaControlPage::setUiTestState(bool enabled, bool dataStalled, bool partialFailure,
                                      qint64 elapsedMs, VaporView::SensorTransportPath path)
{
    setSensorTransportPath(path);
    const bool wasEnabled=property("fpgaUiTestMode").toBool();
    if(enabled&&!wasEnabled) {
        setProperty("tec103Ports", QJsonObject{});
        setTemperatureConnectionState(false, {});
        clearMeasurements();
        simulation_=Simulation{};simulation_.saved=configuration();
        simulation_.savedUsb=usbDiscovery_;simulation_.savedUsbRemote=usbRemote_;simulation_.savedUsbAvailable=usbHostAvailable_;
        const auto firmware=property("fpgaHistoricalExtensionsFixture").toBool()
            ? VaporView::Ground::Devices::FpgaSimulatorFirmware::HistoricalExtensions
            : VaporView::Ground::Devices::FpgaSimulatorFirmware::Rd105Integrated;
        auto transport=VaporView::Ground::Devices::makeFpgaUsbSimulator(nullptr,
            VaporView::Ground::Devices::FpgaSimulatorStartup::Idle,firmware);
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
        connect(controller,&VaporView::Ground::Devices::FpgaDeviceController::temperaturePortsChanged,receiver,[this,publish](const QJsonObject& ports) {setProperty("tec103Ports",ports);publish();});
        connect(controller,&VaporView::Ground::Devices::FpgaDeviceController::temperatureConnectionChanged,receiver,[this,publish](bool connected,const QString& detail,bool identifying) {
            setTemperatureConnectionState(connected,detail,identifying);publish();
        });
        connect(controller,&VaporView::Ground::Devices::FpgaDeviceController::temperatureDataUpdated,receiver,[this](const auto& data) {
            emit simulationTemperatureDataUpdated(data);
        });
        connect(controller,&VaporView::Ground::Devices::FpgaDeviceController::capabilitiesUpdated,receiver,[this](quint32 bits){setExtensions(bits);});
        connect(controller,&VaporView::Ground::Devices::FpgaDeviceController::managementUpdated,receiver,[this](const auto& state){setManagementState(state);});
        connect(controller,&VaporView::Ground::Devices::FpgaDeviceController::measurementUpdated,receiver,[this](const auto& m) {
            updateSensor(m.reading);emit simulationMeasurementsUpdated(m);
        });
        connect(controller,&VaporView::Ground::Devices::FpgaDeviceController::waveformUpdated,receiver,[this](const auto& wave){updateWaveform(wave);emit simulationWaveformUpdated(wave);});
        connect(controller,&VaporView::Ground::Devices::FpgaDeviceController::sensorConfigurationApplied,receiver,[this](quint16 source,const QMap<quint32,quint32>& values) {
            if(source==0x46) {auto config=configuration();config.ai8.pollIntervalMs=values.value(0x662c);setConfiguration(config);}
            emit simulationSensorConfigurationApplied(source,values);
        });
        connect(controller,&VaporView::Ground::Devices::FpgaDeviceController::operationFailed,receiver,[this](bool ready) {
            emit simulationOperationFailed(ready);
        });
        connect(controller,&VaporView::Ground::Devices::FpgaDeviceController::logRecordGenerated,receiver,[this](const auto& record) {
            appendDiagnostic(record.message);
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
    readingTimes_.clear();
    waveTimes_.clear();rawEnabled_.clear();
    for (auto *label : sensorLabels_)
        label->setText(text("等待有效样本", "Waiting for valid sample"));
    for (const quint16 source : plots_.keys())
        clearWaveformDisplay(source, "等待波形", "Waiting for waveform");
    for(auto *title:firstPlotTitles_)title->setText("H1");
    for(auto *title:secondPlotTitles_)title->setText("H2");
    for (auto *label : hardwareLabels_)
        label->setText(QStringLiteral("—"));
    if (diagnostics_)
        diagnostics_->clear();
}
