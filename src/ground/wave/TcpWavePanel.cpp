#include "ground/widgets/CustomTitleBar.h"
#include "LogService.h"
#include "shared/theme/AppTheme.h"
#include "shared/theme/TopLevelCardStyle.h"
#include "shared/theme/SingleLevelPopupMenu.h"
#include "TcpWavePanel.h"
#include "WaterProfileAccumulator.h"
#include "WavePlotFrame.h"
#include "WaveformRowWidgets.h"
#include "shared/theme/SingleLevelPopupComboBox.h"
#include <QToolTip>
#include "ground/widgets/VisualTextLabel.h"
#include "ground/widgets/LabelTextSelection.h"
#include "ground/widgets/SegmentedSwitchButton.h"
#include "shared/config/SettingsWriteBarrier.h"
#include "shared/config/ApplicationConfig.h"
#include <QAbstractSocket>
#include <QApplication>
#include <QByteArray>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>
#include <QPaintEvent>
#include <QPen>
#include <QPixmap>
#include <QPolygonF>
#include <QRectF>
#include <QPushButton>
#include <QResizeEvent>
#include <QSettings>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QStandardItemModel>
#include <QSize>
#include <QSizePolicy>
#include <QSpinBox>
#include <QStringList>
#include <QSvgRenderer>
#include <QTcpSocket>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QVariant>
#include <QWheelEvent>
#include <QWidgetAction>
#include <QDateTime>
#include <QtEndian>
#include <algorithm>
#include <vector>
#include <cmath>
#include <functional>
#include <limits>
#include <utility>

using VaporView::AppThemeColor;
using VaporView::appThemeColor;
using VaporView::configureComboBoxPopup;
using VaporView::isDarkThemeEnabled;
using VaporView::SingleLevelPopupAnchor;
using VaporView::SingleLevelPopupMenu;
using VaporView::SingleLevelPopupMenuRow;
using VaporView::SingleLevelPopupTextAlignment;
using VaporView::WavePlotFrame::PlotTheme;
using VaporView::WavePlotFrame::plotThemeFor;

namespace
{
constexpr int kHeaderSize = 4;
constexpr int kFloatSize = 4;
constexpr int kMaxPayloadBytes = 16 * 1024 * 1024;
constexpr int kPreferredPayloadBytes = 200000;
constexpr int kTcpControlHeight = 36;
constexpr int kTcpButtonHeight = kTcpControlHeight;
constexpr int kTcpCompactButtonTextPadding = 56;
constexpr int kTcpTitleBarHeight = kTcpControlHeight + 4;
constexpr int kTcpMainTitleBarHeight = kTcpControlHeight + 8;
constexpr int kTcpTitleBarPrimarySpacing = 10;
constexpr int kWaveDisplayIconSize = 28;
constexpr int kWaveDisplayIconInnerSize = 22;
constexpr int kTcpTitleBarRealtimeHostSpacing = 12;
constexpr int kTcpTitleBarFieldSpacing = 18;
constexpr int kTcpTitleBarStatusSpacing = 8;
constexpr int kTcpFrameRateMinimumWidth = 132;
constexpr int kWavePlotMinimumHeight = 120;
constexpr int kWavePlotMaximumHeight = 150;
constexpr int kPeakPlotMinimumHeight = 150;
constexpr int kPeakPlotMaximumHeight = 190;
constexpr int kPlotTopMargin = 2;
constexpr int kPlotRightMargin = 2;
constexpr int kPlotBottomMarginExtra = 8;
constexpr int kWavePlotLeftMargin = 72;
constexpr int kPeakPlotLeftMargin = 72;
constexpr int kMinTimeXAxisLabelCount = 2;
constexpr qint64 kTimeXAxisMsecsPerInterval = 1000;
constexpr qreal kTimeXAxisLabelGap = 4.0;
constexpr qreal kTimeXAxisLabelRightInset = 2.0;
constexpr int kDefaultPeakSearchStartIndex = 0;
constexpr int kDefaultPeakSearchEndIndex = 0;
constexpr int kPeakTrendFrameWindow = 1000;
constexpr int kLiveDisplayRefreshMs = 20;
constexpr int kFrameRateLabelRefreshMs = 250;
constexpr int kProcessBufferMaxFramesPerPass = 32;
constexpr qint64 kProcessBufferBudgetMs = 8;
constexpr int kTcpBufferBacklogWarningBytes = 4 * 1024 * 1024;
constexpr qsizetype kTcpInvalidResyncDisconnectBytes = 4 * 1024 * 1024;
constexpr qsizetype kTcpBufferMaxBacklogBytes = 2 * kMaxPayloadBytes + 2 * kHeaderSize;
constexpr qsizetype kTcpBufferResyncKeepBytes = kHeaderSize - 1;
constexpr qint64 kTcpBufferBacklogWarningIntervalMs = 5000;
constexpr qint64 kFrameRateWindowMs = 5000;
constexpr double kMaxReasonableWaveMagnitude = 1.0e6;
constexpr int kRemoteStatusCountWidth = 6;
constexpr int kRemoteStatusPeakWidth = 12;
constexpr int kRemoteStatusRmsWidth = 8;
constexpr int kRemoteStatusRangeWidth = 15;
constexpr int kRemoteStatusIndexWidth = 6;
constexpr const char *kTooltipAnchorRectProperty = "_vv_tooltip_anchor_rect";

class TcpWaveCardOutline final : public QWidget
{
public:
    explicit TcpWaveCardOutline(QGroupBox *card)
        : QWidget(card)
    {
        setObjectName(QStringLiteral("tcpWaveCardOutline"));
        setProperty("tcpWaveCardOutline", true);
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
        setAttribute(Qt::WA_StyledBackground, true);
        setAutoFillBackground(false);
        setFocusPolicy(Qt::NoFocus);

        card->installEventFilter(this);
        syncGeometry();
        show();
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched == parentWidget() && event->type() == QEvent::Resize)
        {
            syncGeometry();
        }
        return QWidget::eventFilter(watched, event);
    }

private:
    void syncGeometry()
    {
        setGeometry(parentWidget()->rect());
        raise();
    }
};

void installTcpWaveCardOutline(QGroupBox *card)
{
    if (card)
    {
        new TcpWaveCardOutline(card);
    }
}

QString hexPreview(const QByteArray& data, int limit = 12)
{
    const int count = std::min(limit, static_cast<int>(data.size()));
    QStringList parts;
    parts.reserve(count);
    for (int i = 0; i < count; ++i)
    {
        parts << QString("%1").arg(static_cast<unsigned char>(data.at(i)), 2, 16, QChar('0')).toUpper();
    }
    return parts.join(' ');
}

QString headerOrderLabel(bool english, TcpWavePanel::HeaderByteOrder order)
{
    switch (order)
    {
    case TcpWavePanel::HeaderByteOrder::LittleEndian:
        return english ? "little-endian" : "小端";
    case TcpWavePanel::HeaderByteOrder::BigEndian:
        return english ? "big-endian" : "大端";
    case TcpWavePanel::HeaderByteOrder::Unknown:
    default:
        return english ? "unknown" : "未知";
    }
}

QString formatWaveValue(double value, int fixedDecimals)
{
    if (!std::isfinite(value))
    {
        return QStringLiteral("NaN");
    }

    const double magnitude = std::fabs(value);
    if (magnitude >= 100000.0 || (magnitude > 0.0 && magnitude < 0.0001))
    {
        return QString::number(value, 'g', 6);
    }
    return QString::number(value, 'f', fixedDecimals);
}

QFont numericFontFrom(const QFont& base)
{
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    if (base.pointSizeF() > 0.0)
    {
        font.setPointSizeF(base.pointSizeF());
    }
    font.setWeight(static_cast<QFont::Weight>(base.weight()));
    font.setBold(base.bold());
    return font;
}

int widestTextWidth(const QLabel *label, const QStringList& candidates)
{
    const QFontMetrics metrics(label->font());
    int width = 0;
    for (const QString& candidate : candidates)
    {
        width = std::max(width, metrics.horizontalAdvance(candidate));
    }
    return width;
}

class PeakHeaderBarWidget final : public QWidget
{
public:
    explicit PeakHeaderBarWidget(QWidget *parent = nullptr)
        : QWidget(parent)
    {
    }

    void setReferenceRowWidget(const QWidget *widget)
    {
        reference_row_widget_ = widget;
    }

    QSize minimumSizeHint() const override
    {
        return expandedSize(QWidget::minimumSizeHint());
    }

    QSize sizeHint() const override
    {
        return expandedSize(QWidget::sizeHint());
    }

private:
    QSize expandedSize(const QSize &base) const
    {
        const int refHeight = (reference_row_widget_ && reference_row_widget_->minimumHeight() > 0)
            ? reference_row_widget_->minimumHeight()
            : kTcpButtonHeight;
        const int singleRowHeight = std::max(
            1,
            static_cast<int>(std::lround(refHeight * static_cast<double>(kTcpTitleBarHeight) / kTcpButtonHeight)));
        return QSize(base.width(), std::max(base.height(), singleRowHeight));
    }

    const QWidget *reference_row_widget_ = nullptr;
};

class DecimalAlignedPeakLabel final : public QLabel
{
public:
    using QLabel::QLabel;

    void setPeakText(const QString& value)
    {
        ensurePolished();
        const QFontMetrics metrics(font());
        // A float displayed with seven significant digits needs up to seven
        // integer digits plus a sign, or ten fractional digits / an exponent.
        setFixedWidth(metrics.horizontalAdvance(QStringLiteral("-9999999")) +
                      metrics.horizontalAdvance(QStringLiteral(".0001234567")) + 2);
        setText(value);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        const QString value = text();
        int decimal = value.indexOf(QLatin1Char('.'));
        if (decimal < 0)
            decimal = value.indexOf(QLatin1Char('e'));
        if (decimal < 0)
            decimal = value.size();
        const QFontMetrics metrics(font());
        const QRect area = contentsRect();
        const int decimalX = area.left() + metrics.horizontalAdvance(QStringLiteral("-9999999"));
        QPainter painter(this);
        painter.setFont(font());
        painter.setPen(palette().color(isEnabled() ? QPalette::Active : QPalette::Disabled, QPalette::WindowText));
        painter.setClipRect(area);
        const int baseline = area.top() + (area.height() - metrics.height()) / 2 + metrics.ascent();
        painter.drawText(QPoint(decimalX - metrics.horizontalAdvance(value.left(decimal)), baseline), value.left(decimal));
        painter.drawText(QPoint(decimalX, baseline), value.mid(decimal));
    }
};

QString fixedStatusText(const QString& text, int width)
{
    return text.rightJustified(std::max(width, static_cast<int>(text.size())), QLatin1Char(' '));
}

QString fixedStatusInteger(qint64 value, int width)
{
    return fixedStatusText(QString::number(value), width);
}

QString fixedStatusField(const QString& text, int width)
{
    return text.leftJustified(std::max(width, static_cast<int>(text.size())), QLatin1Char(' '));
}

QString fixedNumericStatusField(const QString& text, int width)
{
    QString displayText = text;
    if (!displayText.isEmpty() &&
        displayText.at(0) != QLatin1Char('-') &&
        displayText.at(0) != QLatin1Char('+'))
    {
        displayText.prepend(QLatin1Char(' '));
    }
    return displayText.leftJustified(std::max(width, static_cast<int>(displayText.size())), QLatin1Char(' '));
}

QString fixedStatusFloat(double value, int decimals, int width)
{
    return fixedNumericStatusField(std::isfinite(value)
                                       ? QString::number(value, 'f', decimals)
                                       : QStringLiteral("--"),
                                   width);
}

QString remoteWaveformStatusText(bool english, int sampleCount)
{
    return QStringLiteral("%1 %2")
        .arg(english ? QStringLiteral("Points:") : QStringLiteral("采样点:"),
             fixedStatusField(QString::number(sampleCount), kRemoteStatusCountWidth));
}

QString remoteFeatureStatusText(bool english, double peak, double rms, quint32 searchStart, quint32 searchEnd, float peakIndex)
{
    const QString fieldGap = QStringLiteral("  ");
    const QString rmsLabel = english ? QStringLiteral("RMS:") : QStringLiteral("均方根RMS:");
    QString text = QStringLiteral("%1%2%3%4%5")
        .arg(english ? QStringLiteral("Peak:") : QStringLiteral("峰值:"),
             fixedStatusFloat(peak, 6, kRemoteStatusPeakWidth),
             fieldGap,
             rmsLabel,
             fixedStatusFloat(rms, 4, kRemoteStatusRmsWidth));
    if (searchStart > 0 || searchEnd > 0)
    {
        const QString rangeText = QStringLiteral("[%1,%2)")
            .arg(searchStart)
            .arg(searchEnd == 0
                     ? (english ? QStringLiteral("end") : QStringLiteral("末尾"))
                     : QString::number(searchEnd));
        text += QStringLiteral("%1%2 %3%4%5 %6")
            .arg(fieldGap,
                 english ? QStringLiteral("Range:") : QStringLiteral("区间:"),
                 fixedStatusField(rangeText, kRemoteStatusRangeWidth),
                 fieldGap,
                 english ? QStringLiteral("Index:") : QStringLiteral("下标:"),
                 fixedStatusField(QString::number(peakIndex, 'f', 0), kRemoteStatusIndexWidth));
    }
    return text;
}

QString remoteInvalidFeatureStatusText(bool english)
{
    return english ? QStringLiteral("Feature invalid") : QStringLiteral("特征无效");
}

QString findResourceFile(const QString& relativePath)
{
    const QString appDir = QApplication::applicationDirPath();
    const QStringList candidates = {
        QDir(appDir).filePath(relativePath),
        QDir(appDir).filePath(QStringLiteral("../") + relativePath),
        QDir(appDir).filePath(QStringLiteral("../../") + relativePath)
    };

    for (const QString& path : candidates)
    {
        if (QFileInfo::exists(path))
        {
            return path;
        }
    }
    return QString();
}

QPixmap renderLucidePixmap(const QByteArray& svgData, const QColor& color, qreal devicePixelRatio)
{
    QByteArray tinted = svgData;
    tinted.replace("currentColor", color.name(QColor::HexRgb).toUtf8());

    const qreal dpr = std::max<qreal>(1.0, devicePixelRatio);
    const int physicalSize = std::max(1, static_cast<int>(std::ceil(kWaveDisplayIconSize * dpr)));
    QPixmap pixmap(physicalSize, physicalSize);
    pixmap.setDevicePixelRatio(dpr);
    pixmap.fill(Qt::transparent);

    QSvgRenderer renderer(tinted);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const qreal iconOffset = (kWaveDisplayIconSize - kWaveDisplayIconInnerSize) * 0.5;
    renderer.render(&painter, QRectF(iconOffset, iconOffset, kWaveDisplayIconInnerSize, kWaveDisplayIconInnerSize));
    return pixmap;
}

QIcon createLucideIcon(const QString& iconName, const QColor& color)
{
    QFile file(findResourceFile(QStringLiteral("resources/lucide/%1.svg").arg(iconName)));
    if (!file.open(QIODevice::ReadOnly))
    {
        return QIcon();
    }

    const QByteArray svgData = file.readAll();
    QIcon icon;
    for (const qreal dpr : {1.0, 1.25, 1.5, 2.0, 3.0})
    {
        icon.addPixmap(renderLucidePixmap(svgData, color, dpr), QIcon::Normal);
    }
    return icon;
}

QIcon createWaveDisplayIcon(const QColor& color)
{
    return createLucideIcon(QStringLiteral("sliders-horizontal"), color);
}

QIcon createMenuCheckIcon(const QColor& color)
{
    return createLucideIcon(QStringLiteral("check"), color);
}

constexpr const char *kSectionTitleIconNameProperty = "_vv_section_title_icon_name";
constexpr int kSectionTitleIconBoxSize = 26;
constexpr int kSectionTitleIconSize = 22;

QPixmap renderSectionTitleLucidePixmap(const QByteArray& svgData, const QColor& color, qreal devicePixelRatio)
{
    QByteArray tinted = svgData;
    tinted.replace("currentColor", color.name(QColor::HexRgb).toUtf8());

    const qreal dpr = std::max<qreal>(1.0, devicePixelRatio);
    constexpr int kLogicalSize = 32;
    const int physicalSize = std::max(1, static_cast<int>(std::ceil(kLogicalSize * dpr)));
    QPixmap pixmap(physicalSize, physicalSize);
    pixmap.setDevicePixelRatio(dpr);
    pixmap.fill(Qt::transparent);

    QSvgRenderer renderer(tinted);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    renderer.render(&painter, QRectF(2, 2, 28, 28));
    return pixmap;
}

QIcon createSectionTitleLucideIcon(const QString& iconName, const QColor& color)
{
    QFile file(findResourceFile(QStringLiteral("resources/lucide/%1.svg").arg(iconName)));
    if (!file.open(QIODevice::ReadOnly))
    {
        return QIcon();
    }

    const QByteArray svgData = file.readAll();
    QIcon icon;
    for (const qreal dpr : {1.0, 1.25, 1.5, 2.0, 3.0})
    {
        icon.addPixmap(renderSectionTitleLucidePixmap(svgData, color, dpr), QIcon::Normal);
    }
    return icon;
}

QColor sectionTitleIconColor(bool dark)
{
    return dark ? appThemeColor(AppThemeColor::TextTitle, true) : QColor(0, 0, 0);
}

void updateSectionTitleIcon(QLabel *iconLabel, bool dark)
{
    if (!iconLabel)
    {
        return;
    }
    const QString iconName = iconLabel->property(kSectionTitleIconNameProperty).toString();
    if (iconName.isEmpty())
    {
        iconLabel->clear();
        return;
    }
    iconLabel->setPixmap(createSectionTitleLucideIcon(iconName, sectionTitleIconColor(dark)).pixmap(
        QSize(kSectionTitleIconSize, kSectionTitleIconSize)));
}

QWidget *createSectionTitleCluster(QWidget *parent,
                                   const QString& iconName,
                                   QLabel *titleLabel,
                                   int titleHeight)
{
    auto *cluster = new QWidget(parent);
    cluster->setObjectName(QStringLiteral("sectionTitleCluster"));
    cluster->setFixedHeight(titleHeight);
    cluster->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    auto *layout = new QHBoxLayout(cluster);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    auto *iconLabel = new QLabel(cluster);
    iconLabel->setObjectName(QStringLiteral("sectionTitleIcon"));
    iconLabel->setProperty(kSectionTitleIconNameProperty, iconName);
    iconLabel->setAlignment(Qt::AlignCenter);
    iconLabel->setFixedSize(kSectionTitleIconBoxSize, titleHeight);
    iconLabel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    iconLabel->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    updateSectionTitleIcon(iconLabel, VaporView::isDarkThemeEnabled());
    layout->addWidget(iconLabel, 0, Qt::AlignVCenter);

    cluster->setProperty("fontMetricsManagedWidth", true);
    titleLabel->setProperty("fontMetricsManagedWidth", true);
    titleLabel->setParent(cluster);
    VaporView::configureSelectableCardTitle(titleLabel);
    titleLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    titleLabel->setMargin(0);
    titleLabel->setContentsMargins(0, 0, 0, 0);
    titleLabel->setFixedHeight(titleHeight);
    titleLabel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    layout->addWidget(titleLabel, 0, Qt::AlignVCenter);

    return cluster;
}

void updateSectionTitleIcons(QWidget *root, bool dark)
{
    if (!root)
    {
        return;
    }
    const auto labels = root->findChildren<QLabel *>(QStringLiteral("sectionTitleIcon"));
    for (QLabel *label : labels)
    {
        updateSectionTitleIcon(label, dark);
    }
}

class WaveDisplayTitleLabel final : public VaporView::VisualTextLabel
{
public:
    explicit WaveDisplayTitleLabel(QWidget *parent = nullptr)
        : VaporView::VisualTextLabel(parent)
        , menu_(new SingleLevelPopupMenu(this))
    {
        setFocusPolicy(Qt::NoFocus);
        setMouseTracking(true);
        menu_->setObjectName(QStringLiteral("waveDisplayMenu"));
        menu_->refreshTheme();
        show_default_row_ = addModeRow(5);
        show_all_row_ = addModeRow(0);
        show_raw_row_ = addModeRow(1);
        show_h2_row_ = addModeRow(4);
        show_harmonic_row_ = addModeRow(2);
        show_peak_trend_row_ = addModeRow(3);
        setCurrentStates(true, false, false, false, false, false);
    }

    QSize sizeHint() const override
    {
        QSize size = QLabel::sizeHint();
        if (!text().isEmpty())
        {
            const QFontMetrics metrics(font());
            size.rwidth() = std::max({size.width(), metrics.horizontalAdvance(text()) + 8,
                                      metrics.tightBoundingRect(text()).width() + 8});
            size.rheight() = std::max(size.height(), metrics.height());
        }
        if (inline_icon_visible_)
        {
            size.rwidth() += kWaveDisplayIconSize + kIconGap;
            size.rheight() = std::max(size.height(), kWaveDisplayIconSize);
        }
        return size;
    }

    QSize minimumSizeHint() const override
    {
        return sizeHint();
    }

    void refreshTitleGeometry()
    {
        setFixedWidth(sizeHint().width());
        if (QWidget *cluster = parentWidget(); cluster && cluster->property("fontMetricsManagedWidth").toBool())
        {
            cluster->layout()->invalidate();
            cluster->setFixedWidth(cluster->layout()->sizeHint().width());
        }
        refreshTooltipAnchor();
    }

    void setIcon(const QIcon& icon)
    {
        normal_icon_ = icon;
        update();
    }

    void setEnglish(bool english, bool fpgaMode)
    {
        const QString title = english ? QStringLiteral("Wave display settings") : QStringLiteral("波形显示设置");
        setToolTip(title);
        setAccessibleName(title);
        menu_->setTitle(english ? QStringLiteral("Wave Display") : QStringLiteral("波形显示"));
        show_default_row_->setText(english ? QStringLiteral("Default View") : QStringLiteral("默认显示"));
        show_default_row_->defaultAction()->setVisible(fpgaMode);
        show_all_row_->setText(english ? QStringLiteral("Show All") : QStringLiteral("全部显示"));
        show_raw_row_->setText(fpgaMode
            ? (english ? QStringLiteral("Show First Harmonic H1") : QStringLiteral("显示一次谐波H1"))
            : (english ? QStringLiteral("Show Raw Signal") : QStringLiteral("显示原始信号")));
        show_h2_row_->setText(english ? QStringLiteral("Show Second Harmonic H2") : QStringLiteral("显示二次谐波H2"));
        show_h2_row_->defaultAction()->setVisible(fpgaMode);
        show_harmonic_row_->setText(fpgaMode
            ? (english ? QStringLiteral("Show Normalized Second Harmonic") : QStringLiteral("显示归一化二次谐波"))
            : (english ? QStringLiteral("Show Second Harmonic") : QStringLiteral("显示二次谐波")));
        show_peak_trend_row_->setText(fpgaMode
            ? (english ? QStringLiteral("Show Normalized Second Harmonic Peak Trend") : QStringLiteral("显示归一化二次谐波峰值趋势"))
            : (english ? QStringLiteral("Show Second Harmonic Peak Trend") : QStringLiteral("显示二次谐波峰值趋势")));
        refreshTooltipAnchor();
    }

    void setCurrentStates(bool showAll, bool showRaw, bool showHarmonic, bool showPeakTrend, bool showH2, bool showDefault)
    {
        show_all_ = showAll;
        show_default_ = showDefault;
        show_default_row_->setChecked(show_default_);
        show_raw_ = showRaw;
        show_harmonic_ = showHarmonic;
        show_peak_trend_ = showPeakTrend;
        show_h2_ = showH2;
        show_all_row_->setChecked(show_all_);
        show_raw_row_->setChecked(show_raw_);
        show_harmonic_row_->setChecked(show_harmonic_);
        show_peak_trend_row_->setChecked(show_peak_trend_);
        show_h2_row_->setChecked(show_h2_);
    }

    void updateCheckIcons(bool dark)
    {
        const auto preset = createMenuCheckIcon(appThemeColor(AppThemeColor::MenuCheckText, dark));
        for (auto *row : {show_default_row_, show_all_row_, show_raw_row_, show_h2_row_,
                          show_harmonic_row_, show_peak_trend_row_})
        {
            row->setCheckIcon(preset);
            row->refreshTheme();
        }
    }

    void setModeChangedCallback(std::function<void(bool, bool, bool, bool, bool, bool)> callback)
    {
        mode_changed_ = std::move(callback);
    }

    void popupMenuFrom(QWidget *anchor)
    {
        menu_->popupFrom(anchor ? anchor : this);
    }

protected:
    bool event(QEvent *event) override
    {
        const bool handled = QLabel::event(event);
        if (!event)
        {
            return handled;
        }

        switch (event->type())
        {
        case QEvent::Resize:
        case QEvent::Show:
        case QEvent::ContentsRectChange:
            refreshTooltipAnchor();
            break;
        case QEvent::FontChange:
        case QEvent::StyleChange:
            refreshTitleGeometry();
            break;
        default:
            break;
        }

        return handled;
    }

    void paintEvent(QPaintEvent *event) override
    {
        if (!inline_icon_visible_)
        {
            VaporView::VisualTextLabel::paintEvent(event);
            return;
        }

        refreshTooltipAnchor();
        VaporView::VisualTextLabel::paintEvent(event);
        QPainter painter(this);
        const QRect icon_area = iconRect();
        if (icon_hovered_)
        {
            const bool dark = VaporView::isDarkThemeEnabled();
            painter.setRenderHint(QPainter::Antialiasing, true);
            painter.setPen(Qt::NoPen);
            painter.setBrush(appThemeColor(AppThemeColor::TitleBarHover, dark));
            painter.drawRoundedRect(icon_area, 4, 4);
        }

        const QIcon& icon = normal_icon_;
        if (icon.isNull())
        {
            const bool dark = VaporView::isDarkThemeEnabled();
            painter.setPen(appThemeColor(AppThemeColor::Primary, dark));
            painter.drawText(icon_area, Qt::AlignCenter, QStringLiteral("..."));
            return;
        }
        icon.paint(&painter, icon_area);
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        if (inline_icon_visible_ &&
            event->button() == Qt::LeftButton &&
            iconRect().contains(event->position().toPoint()))
        {
            menu_->popupFrom(this, SingleLevelPopupAnchor::Left, QPoint(iconRect().left(), 0));
            event->accept();
            return;
        }
        VaporView::VisualTextLabel::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        refreshTooltipAnchor();
        setIconHovered(inline_icon_visible_ && iconRect().contains(event->position().toPoint()));
        VaporView::VisualTextLabel::mouseMoveEvent(event);
    }

    void leaveEvent(QEvent *event) override
    {
        setIconHovered(false);
        QLabel::leaveEvent(event);
    }

private:
    QRect iconRect() const
    {
        const QFontMetrics metrics(font());
        const QRect area = contentsRect();
        const int iconX = area.left() + metrics.horizontalAdvance(text()) + kIconGap;
        const int iconY = area.top() + (area.height() - kWaveDisplayIconSize) / 2;
        return QRect(iconX, iconY, kWaveDisplayIconSize, kWaveDisplayIconSize);
    }

    void refreshTooltipAnchor()
    {
        if (!inline_icon_visible_ || width() <= 0 || height() <= 0 || text().isEmpty())
        {
            setProperty(kTooltipAnchorRectProperty, QVariant());
            return;
        }
        setProperty(kTooltipAnchorRectProperty, QVariant::fromValue(iconRect()));
    }

    void setIconHovered(bool hovered)
    {
        if (icon_hovered_ == hovered)
        {
            return;
        }
        icon_hovered_ = hovered;
        if (hovered)
        {
            setCursor(Qt::PointingHandCursor);
        }
        else
        {
            unsetCursor();
        }
        update();
    }

    static constexpr int kIconGap = 8;

    QIcon normal_icon_;
    SingleLevelPopupMenu *menu_;
    SingleLevelPopupMenuRow *show_default_row_ = nullptr;
    SingleLevelPopupMenuRow *show_all_row_ = nullptr;
    SingleLevelPopupMenuRow *show_raw_row_ = nullptr;
    SingleLevelPopupMenuRow *show_h2_row_ = nullptr;
    SingleLevelPopupMenuRow *show_harmonic_row_ = nullptr;
    SingleLevelPopupMenuRow *show_peak_trend_row_ = nullptr;
    std::function<void(bool, bool, bool, bool, bool, bool)> mode_changed_;
    bool show_default_ = false;
    bool show_all_ = false;
    bool show_raw_ = false;
    bool show_harmonic_ = false;
    bool show_peak_trend_ = false;
    bool show_h2_ = false;
    bool icon_hovered_ = false;
    bool inline_icon_visible_ = false;

    SingleLevelPopupMenuRow *addModeRow(int mode)
    {
        auto *row = new SingleLevelPopupMenuRow(menu_);
        const QString objectName = [mode]() {
            switch (mode)
            {
            case 1:
                return QStringLiteral("tcpWaveDisplayRawMenuAction");
            case 2:
                return QStringLiteral("tcpWaveDisplayHarmonicMenuAction");
            case 3:
                return QStringLiteral("tcpWaveDisplayPeakTrendMenuAction");
            case 4:
                return QStringLiteral("fpgaWaveDisplayH2MenuAction");
            case 5:
                return QStringLiteral("fpgaWaveDisplayDefaultMenuAction");
            case 0:
            default:
                return QStringLiteral("tcpWaveDisplayShowAllMenuAction");
            }
        }();
        row->setObjectName(objectName);
        row->setTextAlignment(SingleLevelPopupTextAlignment::Left);
        row->setHorizontalPadding(18, 14);
        row->setRowSpacing(6);
        row->setCheckSlotWidth(18);
        row->setCheckIconSize(QSize(18, 18));
        row->setRowHeight(34);
        row->setMinimumRowWidth(238);
        row->setCloseOnClick(mode == 0 || mode == 5);
        auto *action = menu_->addRow(row);
        action->setObjectName(objectName);
        action->setCheckable(true);
        connect(action, &QAction::triggered, this, [this, mode]() {
            if (mode == 5)
            {
                setCurrentStates(false, false, false, false, false, true);
            }
            else if (mode == 0)
            {
                setCurrentStates(!show_all_, false, false, false, false, false);
            }
            else
            {
                show_all_ = false;
                if (mode == 1)
                {
                    show_raw_ = !show_raw_;
                }
                else if (mode == 2)
                {
                    show_harmonic_ = !show_harmonic_;
                }
                else if (mode == 3)
                {
                    show_peak_trend_ = !show_peak_trend_;
                }
                else if (mode == 4)
                {
                    show_h2_ = !show_h2_;
                }
                setCurrentStates(false, show_raw_, show_harmonic_, show_peak_trend_, show_h2_, false);
            }
            if (mode_changed_)
            {
                mode_changed_(show_all_, show_raw_, show_harmonic_, show_peak_trend_, show_h2_, show_default_);
            }
        });
        return row;
    }
};

bool isReasonableWavePayload(const QVector<float>& values, double maxMagnitude, double *observedMaxMagnitude = nullptr)
{
    double observedMax = 0.0;
    for (float value : values)
    {
        if (!std::isfinite(value))
        {
            if (observedMaxMagnitude)
            {
                *observedMaxMagnitude = std::numeric_limits<double>::infinity();
            }
            return false;
        }

        observedMax = std::max(observedMax, std::fabs(static_cast<double>(value)));
        if (observedMax > maxMagnitude)
        {
            if (observedMaxMagnitude)
            {
                *observedMaxMagnitude = observedMax;
            }
            return false;
        }
    }

    if (observedMaxMagnitude)
    {
        *observedMaxMagnitude = observedMax;
    }
    return true;
}

double percentileValue(QVector<double> values, double percentile)
{
    if (values.isEmpty())
    {
        return std::numeric_limits<double>::quiet_NaN();
    }

    std::sort(values.begin(), values.end());
    const double clampedPercentile = std::clamp(percentile, 0.0, 1.0);
    const double scaledIndex = clampedPercentile * static_cast<double>(values.size() - 1);
    const int lowerIndex = static_cast<int>(std::floor(scaledIndex));
    const int upperIndex = static_cast<int>(std::ceil(scaledIndex));
    if (lowerIndex == upperIndex)
    {
        return values.at(lowerIndex);
    }

    const double fraction = scaledIndex - static_cast<double>(lowerIndex);
    return values.at(lowerIndex) * (1.0 - fraction) + values.at(upperIndex) * fraction;
}

float waveformPeakValue(const QVector<float>& samples, int searchStartIndex, int searchEndIndex)
{
    if (samples.isEmpty())
    {
        return std::numeric_limits<float>::quiet_NaN();
    }

    const int sampleCount = samples.size();
    const int startIndex = std::clamp(searchStartIndex, 0, sampleCount);
    const int endIndex = searchEndIndex <= 0
        ? sampleCount
        : std::clamp(searchEndIndex, 0, sampleCount);
    if (startIndex >= endIndex)
    {
        return std::numeric_limits<float>::quiet_NaN();
    }

    bool hasPeak = false;
    float peakValue = std::numeric_limits<float>::lowest();
    for (int index = startIndex; index < endIndex; ++index)
    {
        const float value = samples.at(index);
        if (!std::isfinite(value))
        {
            continue;
        }
        hasPeak = true;
        peakValue = std::max(peakValue, value);
    }
    return hasPeak ? peakValue : std::numeric_limits<float>::quiet_NaN();
}

qreal timeAxisLabelWidth(const QFontMetrics& metrics)
{
    return std::max<qreal>(36.0, metrics.horizontalAdvance(QStringLiteral("00:00:00")));
}

int timeAxisLabelCountForWidth(qreal plotWidth, const QFontMetrics& metrics)
{
    const qreal targetSpacing = timeAxisLabelWidth(metrics) + kTimeXAxisLabelGap;
    const int intervals = static_cast<int>(std::floor(std::max<qreal>(0.0, plotWidth) / targetSpacing));
    return std::max(kMinTimeXAxisLabelCount, intervals + 1);
}

QString timeAxisTickLabel(qint64 wallMsecs)
{
    if (wallMsecs <= 0)
    {
        wallMsecs = QDateTime::currentMSecsSinceEpoch();
    }
    return QDateTime::fromMSecsSinceEpoch(wallMsecs)
        .toLocalTime()
        .toString(QStringLiteral("hh:mm:ss"));
}

qint64 wallMsecsFromTimestampUs(quint64 timestampUs)
{
    if (timestampUs == 0)
    {
        return 0;
    }
    // Host timestamps are Unix microseconds. Small values are relative/test
    // timestamps and cannot be rendered as a meaningful wall-clock time.
    constexpr quint64 kMinimumEpochUs = 946684800000000ULL; // 2000-01-01
    if (timestampUs < kMinimumEpochUs)
    {
        return QDateTime::currentMSecsSinceEpoch();
    }
    return static_cast<qint64>(timestampUs / 1000ULL);
}

struct TimeAxisState
{
    qint64 min_wall_msecs = 0;
    qint64 max_wall_msecs = 0;
    QStringList labels;
};

template<typename LabelAt>
int fittingXAxisIntervals(int maximum, qreal width, const QFontMetrics &metrics, LabelAt labelAt)
{
    for (int count : {12, 8, 6, 4, 2}) {
        if (count > maximum) continue;
        int widest = 0;
        for (int i = 0; i <= count; ++i)
            widest = std::max(widest, metrics.horizontalAdvance(labelAt(i, count)));
        if (width >= count * (widest + 6)) return count;
    }
    return 1;
}

TimeAxisState makeTimeAxisState(qint64 minWallMsecs,
                                qint64 maxWallMsecs,
                                qreal plotWidth,
                                const QFontMetrics& metrics)
{
    const int labelCount = timeAxisLabelCountForWidth(plotWidth, metrics);
    const qint64 minimumSpan = static_cast<qint64>(labelCount - 1) * kTimeXAxisMsecsPerInterval;
    if (maxWallMsecs <= 0)
    {
        maxWallMsecs = QDateTime::currentMSecsSinceEpoch();
    }
    if (minWallMsecs <= 0 || maxWallMsecs <= minWallMsecs ||
        maxWallMsecs - minWallMsecs < minimumSpan)
    {
        minWallMsecs = maxWallMsecs - minimumSpan;
    }

    TimeAxisState state;
    state.min_wall_msecs = minWallMsecs;
    state.max_wall_msecs = maxWallMsecs;
    state.labels.reserve(labelCount);
    for (int index = 0; index < labelCount; ++index)
    {
        const qint64 tickMsecs = minWallMsecs + qRound64(
            static_cast<double>(maxWallMsecs - minWallMsecs) * index /
            static_cast<double>(std::max(1, labelCount - 1)));
        state.labels.append(timeAxisTickLabel(tickMsecs));
    }
    return state;
}

void setTimeAxisProperties(QWidget *widget, const TimeAxisState& state)
{
    if (!widget)
    {
        return;
    }
    widget->setProperty("xAxisLabelText", QString());
    widget->setProperty("xAxisTimeMode", true);
    widget->setProperty("xAxisTimeLabelFormat", QStringLiteral("hh:mm:ss"));
    widget->setProperty("xAxisTickCount", state.labels.size());
    widget->setProperty("xAxisTickLabels", state.labels);
    widget->setProperty("xAxisLeftLabel", state.labels.isEmpty() ? QString() : state.labels.first());
    widget->setProperty("xAxisRightLabel", state.labels.isEmpty() ? QString() : state.labels.last());
    widget->setProperty("xAxisSingleLabel", state.labels.size() <= 1);
    widget->setProperty("xAxisTimeMinMsecs", state.min_wall_msecs);
    widget->setProperty("xAxisTimeMaxMsecs", state.max_wall_msecs);
    widget->setProperty("xAxisTimeMinSeconds",
                        static_cast<double>(state.min_wall_msecs) / 1000.0);
    widget->setProperty("xAxisTimeMaxSeconds",
                        static_cast<double>(state.max_wall_msecs) / 1000.0);
    widget->setProperty("xAxisTimeSpanSeconds",
                        static_cast<double>(std::max<qint64>(0, state.max_wall_msecs - state.min_wall_msecs)) / 1000.0);
    // Keep the old test-facing property names in sync with the new clock labels.
    widget->setProperty("xAxisStartLabelText", state.labels.isEmpty() ? QString() : state.labels.first());
    widget->setProperty("xAxisRangeLabelText", QString());
    widget->setProperty("xAxisEndLabelText", state.labels.isEmpty() ? QString() : state.labels.last());
}

QRectF timeAxisPlotRect(const QRect& widgetRect,
                        int leftMargin,
                        int bottomMargin,
                        const QFontMetrics& metrics)
{
    const QRectF baseRect = widgetRect.adjusted(leftMargin,
                                                kPlotTopMargin,
                                                -kPlotRightMargin,
                                                -bottomMargin);
    const qreal labelHalfWidth = timeAxisLabelWidth(metrics) / 2.0;
    const qreal left = std::max(baseRect.left(), labelHalfWidth);
    const qreal right = std::min(baseRect.right(),
                                 widgetRect.width() - labelHalfWidth - kTimeXAxisLabelRightInset);
    if (right > left + 1.0)
    {
        return QRectF(left, baseRect.top(), right - left, baseRect.height());
    }
    return baseRect;
}

void drawTimeAxis(QPainter& painter,
                  const QRectF& plotRect,
                  const TimeAxisState& state,
                  const QFontMetrics& metrics,
                  const QColor& color,
                  bool compactLabels = false)
{
    if (state.labels.isEmpty() || plotRect.width() <= 1.0)
    {
        return;
    }

    painter.setPen(color);
    const int intervals = std::max(1, static_cast<int>(state.labels.size()) - 1);
    const qreal axisLabelTop = plotRect.bottom() + 2.0;
    for (int index = 0; index < static_cast<int>(state.labels.size()); ++index)
    {
        const qreal x = plotRect.left() + plotRect.width() * index / static_cast<qreal>(intervals);
        const qreal labelWidth = compactLabels ? metrics.horizontalAdvance(state.labels.at(index)) + 4
                                               : std::max(36, metrics.horizontalAdvance(state.labels.at(index)));
        const qreal labelLeft = std::clamp(x - labelWidth / 2.0,
                                           0.0,
                                           std::max<qreal>(0.0, painter.device()->width() -
                                                               labelWidth - kTimeXAxisLabelRightInset));
        painter.drawText(QRectF(labelLeft, axisLabelTop, labelWidth, metrics.height()),
                         Qt::AlignHCenter | Qt::AlignVCenter,
                         state.labels.at(index));
    }
}

int drawYLabels(QPainter &painter, const QRectF &area, int intervals, double minValue, double maxValue, int decimals)
{
    const int height = painter.fontMetrics().height();
    const qreal lastTop = std::max(area.top(), area.bottom() - height + 2);
    int drawn = 0;
    qreal previousBottom = -height;
    for (int tick = 0; tick <= intervals; ++tick) {
        const qreal ratio = qreal(tick) / intervals;
        const qreal top = std::clamp(area.top() + area.height() * ratio - height / 2., area.top() - 2, lastTop);
        if (top < previousBottom || (tick > 0 && tick < intervals && top + height > lastTop)) continue;
        painter.drawText(QRectF(2, top, area.left() - 6, height), Qt::AlignRight | Qt::AlignVCenter,
                         formatWaveValue(maxValue - (maxValue - minValue) * ratio, decimals));
        ++drawn;
        previousBottom = top + height;
    }
    return drawn;
}

void updateTimeAxisGeometryProperties(QWidget *widget,
                                      const QRectF& plotRect,
                                      const TimeAxisState& state)
{
    if (!widget)
    {
        return;
    }
    widget->setProperty("plotAreaLeft", plotRect.left());
    widget->setProperty("plotAreaTop", plotRect.top());
    widget->setProperty("plotAreaRight", plotRect.right());
    widget->setProperty("plotAreaBottom", plotRect.bottom());
    widget->setProperty("xAxisTimeFirstTickX", plotRect.left());
    widget->setProperty("xAxisTimeLastTickX", plotRect.right());
    widget->setProperty("xAxisTimeRightLabel", state.labels.isEmpty() ? QString() : state.labels.last());
}
}

class WavePlotWidget : public QWidget
{
public:
    int minimumHeightForFourYTicks() const
    {
        const int h = fontMetrics().height();
        return kPlotTopMargin + VaporView::WavePlotFrame::minimumPlotHeightForYTicks(h) + h + kPlotBottomMarginExtra;
    }

    explicit WavePlotWidget(const QColor& lineColor, QWidget *parent = nullptr)
        : QWidget(parent)
        , line_color_(lineColor)
    {
        setFont(numericFontFrom(font()));
        setMinimumHeight(kWavePlotMinimumHeight);
        setMaximumHeight(kWavePlotMaximumHeight);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        setProperty("xAxisBottomMarginExtra", kPlotBottomMarginExtra);
        updateXAxisLabelProperty();
    }

#ifdef VAPORVIEW_MAIN_WINDOW_TESTING
    const QVector<float> &samples() const { return samples_; }
#endif

    void setSamples(const QVector<float>& samples, qint64 frameWallMsecs = 0)
    {
        samples_ = samples;
        setProperty("sampleCount", samples_.size());
        if (samples_.isEmpty())
        {
            frame_wall_msecs_ = 0;
        }
        else if (frameWallMsecs > 0)
        {
            frame_wall_msecs_ = frameWallMsecs;
        }
        else if (frame_wall_msecs_ <= 0)
        {
            frame_wall_msecs_ = QDateTime::currentMSecsSinceEpoch();
        }
        updateXAxisLabelProperty();
        update();
    }

    void setEmptyText(const QString& text)
    {
        empty_text_ = text;
        update();
    }

    void setSampleAxisMaxIntervals(int intervals)
    {
        sample_axis_max_intervals_ = intervals;
        updateXAxisLabelProperty();
        update();
    }

    void setSampleAxis(bool enabled, quint32 totalPoints = 0)
    {
        sample_axis_ = enabled;
        total_points_ = totalPoints;
        updateXAxisLabelProperty();
        update();
    }

    void setEnglish(bool english)
    {
        Q_UNUSED(english);
        updateXAxisLabelProperty();
        update();
    }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QWidget::resizeEvent(event);
        updateXAxisLabelProperty();
    }

    void paintEvent(QPaintEvent *event) override
    {
        QWidget::paintEvent(event);

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const PlotTheme theme = plotThemeFor(this);
        painter.fillRect(rect(), theme.background);

        const QFontMetrics fm = painter.fontMetrics();
        const int leftMargin = kWavePlotLeftMargin;
        const int bottomMargin = fm.height() + kPlotBottomMarginExtra;
        const QRectF plotRect = timeAxisPlotRect(rect(), leftMargin, bottomMargin, fm);
        const TimeAxisState timeAxis = axisState(plotRect.width(), fm);
        setTimeAxisProperties(this, timeAxis);
        setProperty("xAxisTimeMode", !sample_axis_);
        updateTimeAxisGeometryProperties(this, plotRect, timeAxis);
        const int yIntervals = VaporView::WavePlotFrame::adaptiveYIntervals(plotRect.height(), fm.height());
        setProperty("yTickCount", yIntervals + 1);
        const auto drawGrid = [&]() {
            VaporView::WavePlotFrame::drawGrid(painter, plotRect, std::max(1, static_cast<int>(timeAxis.labels.size()) - 1), theme, yIntervals);
        };

        float minValue = std::numeric_limits<float>::infinity();
        float maxValue = -std::numeric_limits<float>::infinity();
        for (float value : samples_) {
            if (!std::isfinite(value)) continue;
            minValue = std::min(minValue, value);
            maxValue = std::max(maxValue, value);
        }
        if (!std::isfinite(minValue))
        {
            setProperty("drawnYTickCount", 0);
            drawGrid();
            drawTimeAxis(painter, plotRect, timeAxis, fm, theme.text, sample_axis_ && sample_axis_max_intervals_ > 4);
            painter.setPen(theme.mutedText);
            painter.drawText(plotRect, Qt::AlignCenter, empty_text_);
            return;
        }

        if (std::fabs(maxValue - minValue) < 1e-6f)
        {
            const float pad = std::max(1e-6f, std::fabs(maxValue) * 0.05f + 1e-6f);
            minValue -= pad;
            maxValue += pad;
        }

        drawGrid();

        const int columns = std::max(2, static_cast<int>(std::floor(plotRect.width())));
        QPolygonF polyline;
        const int vertexCount = std::min<int>(columns, samples_.size());
        polyline.reserve(vertexCount);
        const int sampleCount = samples_.size();
        const auto drawSegment = [&] {
            if (polyline.size() == 1) painter.drawPoint(polyline.first());
            else painter.drawPolyline(polyline);
        };
        int previousIndex = -1;
        painter.setPen(QPen(line_color_, 1.4));
        for (int x = 0; x < vertexCount; ++x)
        {
            const double ratio = vertexCount == 1 ? 0.0 : static_cast<double>(x) / static_cast<double>(vertexCount - 1);
            const int index = std::clamp(static_cast<int>(std::llround(ratio * (sampleCount - 1))), 0, sampleCount - 1);
            const float value = samples_.at(index);
            const bool gap = std::any_of(samples_.cbegin() + previousIndex + 1, samples_.cbegin() + index + 1,
                [](float sample) { return !std::isfinite(sample); });
            previousIndex = index;
            if (gap) {
                drawSegment();
                polyline.clear();
            }
            if (!std::isfinite(value)) continue;
            const double normalized = (value - minValue) / std::max(1e-6f, maxValue - minValue);
            const qreal px = plotRect.left() + ratio * plotRect.width();
            const qreal py = plotRect.bottom() - normalized * plotRect.height();
            polyline.append(QPointF(px, py));
        }

        drawSegment();

        painter.setPen(theme.text);
        setProperty("drawnYTickCount", drawYLabels(painter, plotRect, yIntervals, minValue, maxValue, 3));
        drawTimeAxis(painter, plotRect, timeAxis, fm, theme.text, sample_axis_ && sample_axis_max_intervals_ > 4);
    }

private:
    TimeAxisState axisState(qreal width, const QFontMetrics &metrics) const
    {
        if (!sample_axis_) return makeTimeAxisState(0, frame_wall_msecs_, width, metrics);
        TimeAxisState state;
        const auto labelAt = [this](int i, int count) {
            return QString::number(quint64(total_points_ ? total_points_ - 1 : 0) * i / count);
        };
        const int count = sample_axis_max_intervals_ == 4 ? 4
            : fittingXAxisIntervals(sample_axis_max_intervals_, width, metrics, labelAt);
        for (int i = 0; i <= count; ++i) state.labels.append(labelAt(i, count));
        return state;
    }
    void updateXAxisLabelProperty()
    {
        const int widgetWidth = std::max(width(), sizeHint().width());
        const QFontMetrics metrics(font());
        const QRectF plotRect = timeAxisPlotRect(
            QRect(0, 0, widgetWidth, std::max(height(), minimumHeight())),
            kWavePlotLeftMargin,
            metrics.height() + kPlotBottomMarginExtra,
            metrics);
        setTimeAxisProperties(this, axisState(plotRect.width(), metrics));
        setProperty("xAxisTimeMode", !sample_axis_);
    }

    QColor line_color_;
    QVector<float> samples_;
    qint64 frame_wall_msecs_ = 0;
    int sample_axis_max_intervals_ = 4;
    bool sample_axis_ = false;
    quint32 total_points_ = 0;
    QString empty_text_ = QStringLiteral("No data");
};

class PeakTrendPlotWidget : public QWidget
{
public:
    int minimumHeightForFourYTicks() const
    {
        const int h = fontMetrics().height();
        return kPlotTopMargin + VaporView::WavePlotFrame::minimumPlotHeightForYTicks(h) + h + kPlotBottomMarginExtra;
    }

    enum class PlotMode
    {
        Scatter,
        Polyline
    };

    explicit PeakTrendPlotWidget(QWidget *parent = nullptr)
        : QWidget(parent)
        , plot_mode_(PlotMode::Scatter)
        , view_start_index_(0)
        , view_count_(0)
    {
        setFont(numericFontFrom(font()));
        setMinimumHeight(kPeakPlotMinimumHeight);
        setMaximumHeight(kPeakPlotMaximumHeight);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        setProperty("xAxisBottomMarginExtra", kPlotBottomMarginExtra);
        updateXAxisLabelProperty();
    }

    void setPeakValues(const QVector<float>& values,
                       const QVector<qint64>& wallMsecs = {})
    {
        const bool keepTail = peak_values_.isEmpty() ||
            view_count_ <= 0 ||
            (view_start_index_ + visibleCount()) >= peak_values_.size();
        peak_values_ = values;
        setProperty("sampleCount", peak_values_.size());
        peak_wall_msecs_ = wallMsecs;
        if (peak_wall_msecs_.size() != peak_values_.size())
        {
            peak_wall_msecs_.resize(peak_values_.size());
        }
        normalizeView(keepTail);
        notifyViewChanged();
        updateXAxisLabelProperty();
        update();
    }

    void setPlotMode(PlotMode mode)
    {
        plot_mode_ = mode;
        update();
    }

    void setViewRange(int startIndex, int count)
    {
        if (peak_values_.isEmpty())
        {
            return;
        }

        if (count <= 0 || count >= static_cast<int>(peak_values_.size()))
        {
            view_start_index_ = 0;
            view_count_ = 0;
        }
        else
        {
            view_start_index_ = startIndex;
            view_count_ = count;
            normalizeView(false);
        }

        notifyViewChanged();
        updateXAxisLabelProperty();
        update();
    }

    void setViewChangedCallback(std::function<void(int, int, int)> callback)
    {
        on_view_changed_ = std::move(callback);
        notifyViewChanged();
    }

    void setEmptyText(const QString& text)
    {
        empty_text_ = text;
        update();
    }

    void setEnglish(bool english)
    {
        Q_UNUSED(english);
        updateXAxisLabelProperty();
        update();
    }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QWidget::resizeEvent(event);
        updateXAxisLabelProperty();
    }

    void paintEvent(QPaintEvent *event) override
    {
        QWidget::paintEvent(event);

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, false);
        const PlotTheme theme = plotThemeFor(this);
        painter.fillRect(rect(), theme.background);

        const QFontMetrics fm = painter.fontMetrics();
        const int leftMargin = kPeakPlotLeftMargin;
        const int bottomMargin = fm.height() + kPlotBottomMarginExtra;
        const QRectF plotRect = timeAxisPlotRect(rect(), leftMargin, bottomMargin, fm);
        const TimeAxisState timeAxis = visibleTimeAxisState(plotRect.width(), fm);
        setTimeAxisProperties(this, timeAxis);
        updateTimeAxisGeometryProperties(this, plotRect, timeAxis);
        const int yIntervals = VaporView::WavePlotFrame::adaptiveYIntervals(plotRect.height(), fm.height());
        setProperty("yTickCount", yIntervals + 1);
        const auto drawGrid = [&]() {
            VaporView::WavePlotFrame::drawGrid(painter, plotRect, std::max(1, static_cast<int>(timeAxis.labels.size()) - 1), theme, yIntervals);
        };

        if (peak_values_.isEmpty())
        {
            setProperty("drawnYTickCount", 0);
            drawGrid();
            drawTimeAxis(painter, plotRect, timeAxis, fm, theme.text);
            painter.setPen(theme.mutedText);
            painter.drawText(plotRect, Qt::AlignCenter, empty_text_);
            return;
        }

        const int startIndex = visibleStartIndex();
        const int count = visibleCount();
        QVector<int> finiteIndices;
        finiteIndices.reserve(count);
        float minValue = std::numeric_limits<float>::max();
        float maxValue = std::numeric_limits<float>::lowest();
        for (int i = 0; i < count; ++i)
        {
            const float value = peak_values_.at(startIndex + i);
            if (!std::isfinite(value))
            {
                continue;
            }
            finiteIndices.push_back(i);
            minValue = std::min(minValue, value);
            maxValue = std::max(maxValue, value);
        }
        if (finiteIndices.isEmpty())
        {
            setProperty("drawnYTickCount", 0);
            drawGrid();
            drawTimeAxis(painter, plotRect, timeAxis, fm, theme.text);
            painter.setPen(theme.mutedText);
            painter.drawText(plotRect, Qt::AlignCenter, empty_text_);
            return;
        }
        if (std::fabs(maxValue - minValue) < 1e-6f)
        {
            const float pad = std::max(1e-6f, std::fabs(maxValue) * 0.05f + 1e-6f);
            minValue -= pad;
            maxValue += pad;
        }

        drawGrid();

        const QColor seriesColor = appThemeColor(AppThemeColor::PlotSeriesWaveOrange, false);
        if (plot_mode_ == PlotMode::Polyline && count >= 2)
        {
            painter.setPen(QPen(seriesColor, 1.5));
            QPolygonF segment;
            const int sampledCount = std::max(2, std::min(count, static_cast<int>(std::ceil(plotRect.width())) + 1));
            segment.reserve(sampledCount);
            for (int sample = 0; sample < sampledCount; ++sample)
            {
                const int i = std::clamp(
                    static_cast<int>(std::llround(static_cast<double>(sample) * (count - 1) / (sampledCount - 1))),
                    0,
                    count - 1);
                const float value = peak_values_.at(startIndex + i);
                if (!std::isfinite(value))
                {
                    if (segment.size() >= 2)
                    {
                        painter.drawPolyline(segment);
                    }
                    segment.clear();
                    continue;
                }

                const double ratio = timeRatio(startIndex + i, startIndex, count, timeAxis);
                const double normalized = (value - minValue) / std::max(1e-6f, maxValue - minValue);
                segment.push_back(QPointF(plotRect.left() + ratio * plotRect.width(),
                                          plotRect.bottom() - normalized * plotRect.height()));
            }
            if (segment.size() >= 2)
            {
                painter.drawPolyline(segment);
            }
        }
        else
        {
            painter.setRenderHint(QPainter::Antialiasing, true);
            painter.setPen(Qt::NoPen);
            painter.setBrush(seriesColor);
            for (int i = 0; i < count; ++i)
            {
                const float value = peak_values_.at(startIndex + i);
                if (!std::isfinite(value))
                {
                    continue;
                }
                const double ratio = timeRatio(startIndex + i, startIndex, count, timeAxis);
                const double normalized = (value - minValue) / std::max(1e-6f, maxValue - minValue);
                const QPointF point(plotRect.left() + ratio * plotRect.width(),
                                    plotRect.bottom() - normalized * plotRect.height());
                painter.drawEllipse(point, 2.5, 2.5);
            }
            painter.setRenderHint(QPainter::Antialiasing, false);
        }

        painter.setPen(theme.text);
        setProperty("drawnYTickCount", drawYLabels(painter, plotRect, yIntervals, minValue, maxValue, 3));
        drawTimeAxis(painter, plotRect, timeAxis, fm, theme.text);
    }

private:
    int visibleStartIndex() const
    {
        const int totalCount = static_cast<int>(peak_values_.size());
        return peak_values_.isEmpty() ? 0 : std::clamp(view_start_index_, 0, std::max(0, totalCount - visibleCount()));
    }

    int visibleCount() const
    {
        const int totalCount = static_cast<int>(peak_values_.size());
        if (peak_values_.isEmpty())
        {
            return 0;
        }
        if (view_count_ <= 0 || view_count_ >= totalCount)
        {
            return totalCount;
        }
        return std::clamp(view_count_, 1, totalCount);
    }

    void normalizeView(bool keepTail)
    {
        const int totalCount = static_cast<int>(peak_values_.size());
        if (peak_values_.isEmpty())
        {
            view_start_index_ = 0;
            view_count_ = 0;
            return;
        }

        if (view_count_ <= 0 || view_count_ >= totalCount)
        {
            view_start_index_ = 0;
            view_count_ = 0;
            return;
        }

        view_count_ = std::clamp(view_count_, 1, totalCount);
        if (keepTail)
        {
            view_start_index_ = std::max(0, totalCount - view_count_);
        }
        else
        {
            view_start_index_ = std::clamp(view_start_index_, 0, std::max(0, totalCount - view_count_));
        }
    }

    void notifyViewChanged()
    {
        if (on_view_changed_)
        {
            on_view_changed_(static_cast<int>(peak_values_.size()), visibleStartIndex(), visibleCount());
        }
    }

    TimeAxisState visibleTimeAxisState(qreal plotWidth, const QFontMetrics& metrics) const
    {
        const int startIndex = visibleStartIndex();
        const int count = visibleCount();
        qint64 minWallMsecs = 0;
        qint64 maxWallMsecs = 0;
        for (int index = 0; index < count; ++index)
        {
            const int valueIndex = startIndex + index;
            if (valueIndex < 0 || valueIndex >= peak_wall_msecs_.size())
            {
                continue;
            }
            const qint64 wallMsecs = peak_wall_msecs_.at(valueIndex);
            if (wallMsecs <= 0)
            {
                continue;
            }
            minWallMsecs = minWallMsecs <= 0 ? wallMsecs : std::min(minWallMsecs, wallMsecs);
            maxWallMsecs = std::max(maxWallMsecs, wallMsecs);
        }
        return makeTimeAxisState(minWallMsecs, maxWallMsecs, plotWidth, metrics);
    }

    double timeRatio(int index,
                     int startIndex,
                     int count,
                     const TimeAxisState& timeAxis) const
    {
        if (index >= 0 && index < peak_wall_msecs_.size() &&
            peak_wall_msecs_.at(index) > 0 &&
            timeAxis.max_wall_msecs > timeAxis.min_wall_msecs)
        {
            return std::clamp(
                static_cast<double>(peak_wall_msecs_.at(index) - timeAxis.min_wall_msecs) /
                    static_cast<double>(timeAxis.max_wall_msecs - timeAxis.min_wall_msecs),
                0.0,
                1.0);
        }
        return count <= 1 ? 0.5 : static_cast<double>(index - startIndex) / static_cast<double>(count - 1);
    }

    void updateXAxisLabelProperty()
    {
        const int widgetWidth = std::max(width(), sizeHint().width());
        const QFontMetrics metrics(font());
        const QRectF plotRect = timeAxisPlotRect(
            QRect(0, 0, widgetWidth, std::max(height(), minimumHeight())),
            kPeakPlotLeftMargin,
            metrics.height() + kPlotBottomMarginExtra,
            metrics);
        setTimeAxisProperties(this, visibleTimeAxisState(plotRect.width(), metrics));
    }

    QVector<float> peak_values_;
    QVector<qint64> peak_wall_msecs_;
    PlotMode plot_mode_;
    int view_start_index_;
    int view_count_;
    QString empty_text_ = QStringLiteral("No peak data");
    std::function<void(int, int, int)> on_view_changed_;
};

class WaterProfilePlotWidget final : public QWidget
{
public:
    int minimumHeightForFourYTicks() const
    {
        const int h = fontMetrics().height();
        return 4 + VaporView::WavePlotFrame::minimumPlotHeightForYTicks(h) + h + 8;
    }

    explicit WaterProfilePlotWidget(QWidget *parent) : QWidget(parent)
    {
        setMouseTracking(true);
        setFont(numericFontFrom(font()));
        setMinimumHeight(kPeakPlotMinimumHeight);
        setMaximumHeight(QWIDGETSIZE_MAX);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    }

    void setMaxXAxisIntervals(int intervals)
    {
        max_x_axis_intervals_ = intervals;
        update();
    }

    void setUnbinnedProfile(QVector<WaterProfileAccumulator::RawPoint> points, std::optional<WaterProfileAccumulator::Point> latest,
                            quint64 totalSamples, const QString &unit, bool english, const QString &emptyText)
    {
        raw_points_ = std::move(points);
        layers_.clear();
        setProperty("sampleCount", totalSamples);
        setProperty("layerCount", raw_points_.size());
        configureProfile(latest, WaterProfileAccumulator::unbinnedResolution, unit, english, emptyText);
    }

    void setProfile(QVector<WaterProfileAccumulator::Layer> layers, std::optional<WaterProfileAccumulator::Point> latest,
                    int resolution, const QString &unit, bool english, const QString &emptyText)
    {
        layers_ = std::move(layers);
        raw_points_.clear();
        quint64 count = 0;
        for (const auto &layer : layers_) count += layer.sampleCount;
        setProperty("sampleCount", count);
        setProperty("layerCount", layers_.size());
        configureProfile(latest, resolution, unit, english, emptyText);
    }

private:
    void configureProfile(std::optional<WaterProfileAccumulator::Point> latest, int resolution,
                          const QString &unit, bool english, const QString &emptyText)
    {
        latest_ = latest;
        resolution_ = resolution;
        unit_ = unit;
        english_ = english;
        empty_text_ = emptyText;
        axes_ = english ? QStringLiteral("X: concentration%1. Y: lidar height (m).").arg(unit)
                        : QStringLiteral("横轴：水汽浓度%1。纵轴：测距雷达高度 (m)。").arg(unit);
        setToolTip(axes_);
        nodes_.clear();
        node_raw_indices_.clear();
        setProperty("renderedPointCount", layers_.size());
        update();
    }

    bool unbinned() const { return resolution_ == WaterProfileAccumulator::unbinnedResolution; }

protected:
    void mouseMoveEvent(QMouseEvent *event) override
    {
        int hit = -1;
        double nearest = 8.0;
        for (int i = 0; i < nodes_.size(); ++i) {
            const double distance = QLineF(nodes_[i], event->position()).length();
            if (distance <= nearest) { nearest = distance; hit = i; }
        }
        if (hit < 0) { resetTooltip(); return; }
        QString message;
        if (unbinned()) {
            const auto &point = raw_points_.at(node_raw_indices_.at(hit));
            message = (english_ ? QStringLiteral("Height: %1 m\nConcentration: %2%3") : QStringLiteral("高度：%1 m\n浓度：%2%3"))
                .arg(formatWaveValue(point.heightM, 3), formatWaveValue(point.concentration, 3), unit_);
            if (point.sampleCount > 1)
                message += (english_ ? QStringLiteral("\nMerged samples: %1") : QStringLiteral("\n近重合样本数：%1")).arg(point.sampleCount);
        } else {
            const auto &layer = layers_[hit];
            message = (english_ ? QStringLiteral("Height: %1–%2 m\nCenter: %3 m\nMean: %4%5\nSample SD: %6\nSamples: %7")
                : QStringLiteral("高度区间：%1–%2 m\n代表高度：%3 m\n平均浓度：%4%5\n样本标准差：%6\n样本数：%7"))
                .arg(layer.minHeightM).arg(layer.maxHeightM).arg(layer.centerHeightM).arg(formatWaveValue(layer.meanConcentration, 3), unit_,
                    layer.sampleCount >= 2 ? formatWaveValue(layer.stdDev, 3) + unit_ : QStringLiteral("—")).arg(layer.sampleCount);
        }
        setToolTip(message);
        QToolTip::showText(event->globalPosition().toPoint(), message, this);
    }

    void leaveEvent(QEvent *) override { resetTooltip(); }

    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const auto theme = plotThemeFor(this);
        painter.fillRect(rect(), theme.background);
        const QFontMetrics metrics = painter.fontMetrics();
        const auto bounds = [](double low, double high, double minimumSpan) {
            const double span = std::max(minimumSpan, (high - low) * 1.12);
            const double start = std::max(0.0, low + ((high - low) - span) * 0.5);
            return std::pair{start, start + span};
        };
        double lowX = latest_ ? latest_->concentration : 0, highX = lowX;
        double lowY = latest_ ? latest_->heightM : 0, highY = lowY;
        for (const auto &layer : layers_) {
            lowX = std::min(lowX, layer.meanConcentration);
            highX = std::max(highX, layer.meanConcentration);
            lowY = std::min(lowY, layer.centerHeightM);
            highY = std::max(highY, layer.centerHeightM);
        }
        for (const auto &point : std::as_const(raw_points_)) {
            lowX = std::min(lowX, point.concentration);
            highX = std::max(highX, point.concentration);
            lowY = std::min(lowY, point.heightM);
            highY = std::max(highY, point.heightM);
        }
        const bool empty = unbinned() ? raw_points_.isEmpty() : layers_.isEmpty();
        node_raw_indices_.clear();
        setProperty("renderedPointCount", 0);
        const auto [xMin, xMax] = bounds(lowX, highX, std::max(1.0, highX * 0.02));
        const auto [yMin, yMax] = bounds(lowY, highY, unbinned() ? 2.0 : std::max(5.0, resolution_ * 2.0));
        nodes_.clear();
        const int rightMargin = 5 + metrics.horizontalAdvance(formatWaveValue(xMax, 2)) / 2;
        const int leftMargin = 8 + std::max({metrics.horizontalAdvance(formatWaveValue(yMin, 2)),
                                           metrics.horizontalAdvance(formatWaveValue(yMax, 2)),
                                           metrics.horizontalAdvance(formatWaveValue(xMin, 2)) / 2});
        const QRectF area = QRectF(rect()).adjusted(leftMargin, 4, -rightMargin, -metrics.height() - 8);
        if (area.width() < 2 || area.height() < 2) {
            setProperty("drawnYTickCount", 0);
            return;
        }
        const auto labelAt = [&](int i, int count) {
            return formatWaveValue(xMin + (xMax - xMin) * (double(i) / count), 2);
        };
        const int columns = max_x_axis_intervals_ == 4 ? (area.width() >= 4 * (rightMargin * 2 + 8) ? 4 : 2)
            : fittingXAxisIntervals(max_x_axis_intervals_, area.width(), metrics, labelAt);
        setProperty("xAxisTickCount", empty ? 0 : columns + 1);
        const int yIntervals = VaporView::WavePlotFrame::adaptiveYIntervals(area.height(), metrics.height());
        setProperty("yTickCount", yIntervals + 1);
        VaporView::WavePlotFrame::drawGrid(painter, area, columns, theme, yIntervals);
        if (empty) {
            setProperty("drawnYTickCount", 0);
            painter.setPen(theme.mutedText);
            painter.drawText(area, Qt::AlignCenter | Qt::TextWordWrap, empty_text_);
            return;
        }
        painter.setPen(theme.text);
        for (int i = 0; i <= columns; ++i) {
            const double ratio = double(i) / columns;
            const QString label = labelAt(i, columns);
            const int labelWidth = metrics.horizontalAdvance(label) + 2;
            const qreal x = area.left() + area.width() * ratio;
            painter.drawText(QRectF(x - labelWidth / 2.0, area.bottom() + 2, labelWidth, metrics.height()),
                             Qt::AlignCenter, label);
        }
        setProperty("drawnYTickCount", drawYLabels(painter, area, yIntervals, yMin, yMax, 2));
        const auto position = [&](double value, double height) {
            return QPointF(area.left() + area.width() * ((value - xMin) / (xMax - xMin)),
                          area.bottom() - area.height() * ((height - yMin) / (yMax - yMin)));
        };
        const bool dark = theme.background.lightness() < 128;
        const QColor blue = appThemeColor(AppThemeColor::PlotSeriesWaveBlue, dark);
        painter.setPen(QPen(blue, 1.5));
        painter.setBrush(blue);
        if (unbinned()) {
            constexpr int cellPx = 3;
            const int cols = int(std::ceil(area.width() / cellPx)) + 1;
            const int rows = int(std::ceil(area.height() / cellPx)) + 1;
            std::vector<quint8> occupied(size_t(cols) * size_t(rows), 0);
            for (int i = int(raw_points_.size()) - 1; i >= 0; --i) {
                const auto &point = raw_points_.at(i);
                const auto node = position(point.concentration, point.heightM);
                const int x = std::clamp(int((node.x() - area.left()) / cellPx), 0, cols - 1);
                const int y = std::clamp(int((node.y() - area.top()) / cellPx), 0, rows - 1);
                auto &cell = occupied[size_t(y) * cols + x];
                if (cell) continue;
                cell = 1;
                painter.drawEllipse(node, 2.5, 2.5);
                nodes_.append(node);
                node_raw_indices_.append(i);
            }
        } else {
            for (int i = 0; i < layers_.size(); ++i) {
                const auto &layer = layers_[i];
                const auto node = position(layer.meanConcentration, layer.centerHeightM);
                if (i && layer.binIndex == layers_[i - 1].binIndex + 1) painter.drawLine(nodes_.last(), node);
                painter.drawEllipse(node, 2.5, 2.5);
                nodes_.append(node);
            }
        }
        setProperty("renderedPointCount", nodes_.size());
        if (latest_) {
            painter.setPen(QPen(theme.background, 1.5));
            painter.setBrush(appThemeColor(AppThemeColor::PlotSeriesWaveOrange, dark));
            painter.drawEllipse(position(latest_->concentration, latest_->heightM), 4.5, 4.5);
        }
    }

private:
    void resetTooltip()
    {
        if (toolTip() != axes_) {
            setToolTip(axes_);
            QToolTip::hideText();
        }
    }
    QVector<WaterProfileAccumulator::RawPoint> raw_points_;
    QVector<int> node_raw_indices_;
    QVector<WaterProfileAccumulator::Layer> layers_;
    QVector<QPointF> nodes_;
    std::optional<WaterProfileAccumulator::Point> latest_;
    int max_x_axis_intervals_ = 4;
    int resolution_ = 5;
    bool english_ = false;
    QString empty_text_, axes_, unit_;
};

// A second set of plot widgets observes the panel's existing per-channel data.
// Only the ingestion path owns history and writes to the profile accumulator.
struct TcpWavePanel::WaveformPage {
    struct Lane {
        std::array<WavePlotWidget *, 4> waves;
        std::array<QLabel *, 5> info;
        QLabel *heading;
        PeakTrendPlotWidget *trend;
        VaporView::Ground::Widgets::SegmentedSwitchButton *mode;
        QPushButton *clear;
        WaterCards water;
    };
    std::array<Lane, 2> lanes;
    std::array<VaporView::Ground::Wave::VerticalRowLabel *, 7> labels;
    QLabel *titleLabel;
    QPushButton *raw, *harmonic;
    QScrollArea *scroll = nullptr;
    std::array<QWidget *, 7> rows{};
    std::array<VaporView::Ground::Wave::RowResizeHandle *, 7> handles{};
    std::array<std::array<QFrame *, 2>, 7> cards{};
    std::array<std::array<QHBoxLayout *, 2>, 7> headerLayouts{};
    CalibrationControls calibration;
};

TcpWavePanel::TcpWavePanel(QWidget *parent, std::shared_ptr<WaterProfileAccumulator> profile)
    : QWidget(parent)
    , host_edit_(nullptr)
    , port_edit_(nullptr)
    , connect_button_(nullptr)
    , host_label_(nullptr)
    , port_label_(nullptr)
    , panel_title_label_(nullptr)
    , wave_display_button_(nullptr)
    , frame_rate_label_(nullptr)
    , status_label_(nullptr)
    , hint_label_(nullptr)
    , wave1_title_label_(nullptr)
    , wave4_title_label_(nullptr)
    , peak_title_label_(nullptr)
    , wave1_info_label_(nullptr)
    , wave4_info_label_(nullptr)
    , wave1_group_(nullptr)
    , wave4_group_(nullptr)
    , peak_group_(nullptr)
    , wave1_plot_(nullptr)
    , wave4_plot_(nullptr)
    , peak_plot_(nullptr)
    , peak_filter_button_(nullptr)
    , peak_mode_button_(nullptr)
    , peak_clear_button_(nullptr)
    , control_layout_(nullptr)
    , top_controls_layout_(nullptr)
    , plots_layout_(nullptr)
    , water_profile_(profile ? std::move(profile) : std::make_shared<WaterProfileAccumulator>())
    , socket_(nullptr)
    , live_display_timer_(nullptr)
    , pending_wave1_payload_()
    , peak_raw_history_()
    , pending_wave1_info_text_()
    , pending_wave4_info_text_()
    , pending_live_status_text_()
    , remote_waveform_status_text_()
    , remote_feature_status_text_()
    , peak_filter_settings_()
    , peak_search_start_index_(kDefaultPeakSearchStartIndex)
    , peak_search_end_index_(kDefaultPeakSearchEndIndex)
    , peak_plot_scatter_mode_(true)
    , parse_mode_(ParseMode::AutoDetect)
    , read_state_(ReadState::Wave1Header)
    , header_byte_order_(HeaderByteOrder::Unknown)
    , float_encoding_(FloatEncoding::Unknown)
    , expected_payload_size_(0)
    , frame_count_(0)
    , frame_arrival_times_ms_()
    , last_live_decode_time_ms_(0)
    , last_frame_rate_label_update_ms_(0)
    , last_backlog_warning_ms_(0)
    , invalid_resync_discarded_bytes_(0)
    , live_display_dirty_(false)
    , process_buffer_pending_(false)
    , payload_order_auto_correct_logged_(false)
    , user_disconnect_requested_(false)
    , suppress_next_disconnected_log_(false)
    , socket_error_logged_for_current_connection_(false)
    , is_english_(false)
    , compact_layout_(false)
    , remote_sky_mode_(false)
    , remote_wave_tcp_connected_(false)
    , last_remote_feature_time_us_(0)
    , remote_expected_feature_interval_us_(0)
{
    setupUi();
    loadRememberedInputState();
    setupSocket();
    setEnglish(false);
}

TcpWavePanel::~TcpWavePanel()
{
    saveRememberedInputState();
    requestGracefulDisconnect();
    if (socket_)
    {
        socket_->deleteLater();
        socket_ = nullptr;
    }
}

void TcpWavePanel::setCompactLayout(bool compact)
{
    if (compact_layout_ == compact)
    {
        return;
    }

    compact_layout_ = compact;
    layoutPlots();
    updateGeometry();
}

int TcpWavePanel::preferredPanelHeight() const
{
    const int topControlsHeight = control_layout_ ? control_layout_->sizeHint().height() : kTcpTitleBarHeight;
    const QMargins margins = layout()->contentsMargins();
    const int plotHeight = plots_layout_->minimumSize().height();
    return topControlsHeight + plotHeight + (plotHeight ? layout()->spacing() : 0)
        + margins.top() + margins.bottom();
}

bool TcpWavePanel::hasVisibleWaveDisplay() const
{
    const auto &mode = waveDisplayMode();
    return mode.all || mode.raw || mode.harmonic || mode.peak_trend || (fpga_mode_ && (mode.h2 || mode.defaults));
}

bool TcpWavePanel::hasRecentData(qint64 timeoutMs) const
{
    const qint64 age = QDateTime::currentMSecsSinceEpoch() - last_live_decode_time_ms_;
    return isConnected() && last_live_decode_time_ms_ > 0 && age >= 0 && age < timeoutMs;
}

bool TcpWavePanel::usesExpandedPanelHeight() const
{
    return plots_layout_->count() > 1;
}

static QHBoxLayout *createWaveCard(QWidget *parent, QGroupBox *&group, QLabel *&title)
{
    group = new QGroupBox(parent);
    group->setObjectName(QStringLiteral("sensorGroupBox"));
    group->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    auto *body = new QVBoxLayout(group);
    body->setContentsMargins(2, 2, 2, 2);
    body->setSpacing(0);
    auto *header = new QWidget(group);
    header->setObjectName(QStringLiteral("sectionTitleBar"));
    header->setMinimumHeight(kTcpTitleBarHeight);
    header->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto *row = new QHBoxLayout(header);
    row->setContentsMargins(8, 2, 8, 2);
    row->setSpacing(6);
    title = new VaporView::VisualTextLabel(header);
    title->setObjectName(QStringLiteral("sectionTitleLabel"));
    VaporView::configureSelectableCardTitle(title);
    title->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    title->setMargin(0);
    title->setContentsMargins(0, 0, 0, 0);
    title->setFixedHeight(kTcpButtonHeight);
    row->addWidget(title, 0, Qt::AlignVCenter | Qt::AlignLeft);
    body->addWidget(header, 0, Qt::AlignTop);
    installTcpWaveCardOutline(group);
    return row;
}

static QLabel *createChannelHeading(QWidget *parent)
{
    auto *label = new VaporView::VisualTextLabel(parent);
    label->setObjectName(QStringLiteral("sectionTitleLabel"));
    VaporView::configureSelectableCardTitle(label);
    return label;
}

void TcpWavePanel::setupUi()
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(4, 0, 4, 4);
    mainLayout->setSpacing(4);

    control_layout_ = new QGridLayout();
    control_layout_->setContentsMargins(0, 0, 0, 0);
    control_layout_->setHorizontalSpacing(1);
    control_layout_->setVerticalSpacing(4);

    auto *topControlsBar = new QWidget(this);
    topControlsBar->setObjectName("sectionTitleBar");
    topControlsBar->setMinimumHeight(kTcpMainTitleBarHeight);
    topControlsBar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    top_controls_layout_ = new QHBoxLayout(topControlsBar);
    top_controls_layout_->setContentsMargins(8, 2, 8, 2);
    top_controls_layout_->setSpacing(0);
    control_layout_->addWidget(topControlsBar, 0, 0, 1, 6);

    auto *waveDisplayTitle = new WaveDisplayTitleLabel(topControlsBar);
    waveDisplayTitle->setModeChangedCallback([this](bool showAll, bool showRaw, bool showHarmonic, bool showPeakTrend, bool showH2, bool showDefault) {
        (fpga_mode_ ? fpga_wave_display_ : direct_wave_display_) = {showAll, showRaw, showHarmonic, showPeakTrend, showH2, showDefault};
        applyWaveDisplayMode();
    });
    panel_title_label_ = waveDisplayTitle;
    panel_title_label_->setObjectName("sectionTitleLabel");
    top_controls_layout_->addWidget(createSectionTitleCluster(topControlsBar,
                                                              QStringLiteral("square-activity"),
                                                              panel_title_label_,
                                                              kTcpButtonHeight),
                                    0,
                                    Qt::AlignVCenter | Qt::AlignLeft);
    wave_display_button_ = new QToolButton(topControlsBar);
    wave_display_button_->setObjectName(QStringLiteral("tcpWaveDisplayButton"));
    wave_display_button_->setAutoRaise(true);
    wave_display_button_->setCursor(Qt::PointingHandCursor);
    wave_display_button_->setFocusPolicy(Qt::NoFocus);
    wave_display_button_->setFixedSize(28, 28);
    wave_display_button_->setIconSize(QSize(kWaveDisplayIconSize, kWaveDisplayIconSize));
    connect(wave_display_button_, &QToolButton::clicked, this, [this]() {
        if (auto *titleLabel = dynamic_cast<WaveDisplayTitleLabel *>(panel_title_label_))
        {
            titleLabel->popupMenuFrom(wave_display_button_);
        }
    });
    top_controls_layout_->addSpacing(4);
    top_controls_layout_->addWidget(wave_display_button_, 0, Qt::AlignVCenter | Qt::AlignLeft);
    top_controls_layout_->addSpacing(kTcpTitleBarPrimarySpacing);

    frame_rate_label_ = new VaporView::VisualTextLabel(this);
    frame_rate_label_->setObjectName("fieldLabel");
    frame_rate_label_->setFont(numericFontFrom(frame_rate_label_->font()));
    const int frameRateWidth = std::max(
        kTcpFrameRateMinimumWidth,
        widestTextWidth(frame_rate_label_,
                        {QStringLiteral("实时频率: -999.99 Hz"),
                         QStringLiteral("Realtime: -999.99 Hz")}) + 8);
    frame_rate_label_->setFixedWidth(frameRateWidth);
    frame_rate_label_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    top_controls_layout_->addWidget(frame_rate_label_, 0, Qt::AlignVCenter | Qt::AlignLeft);
    top_controls_layout_->addSpacing(kTcpTitleBarRealtimeHostSpacing);

    auto *hostRowLayout = new QHBoxLayout();
    hostRowLayout->setContentsMargins(0, 0, 0, 0);
    hostRowLayout->setSpacing(4);
    host_label_ = new VaporView::VisualTextLabel(this);
    host_label_->setObjectName("fieldLabel");
    hostRowLayout->addWidget(host_label_, 0, Qt::AlignVCenter | Qt::AlignRight);

    host_edit_ = new QLineEdit(this);
    host_edit_->setObjectName(QStringLiteral("tcpWaveHostEdit"));
    host_edit_->setText("127.0.0.1");
    host_edit_->setFixedHeight(kTcpControlHeight);
    host_edit_->setMinimumWidth(90);
    host_edit_->setMaximumWidth(110);
    hostRowLayout->addWidget(host_edit_, 0, Qt::AlignVCenter | Qt::AlignLeft);
    top_controls_layout_->addLayout(hostRowLayout, 0);
    top_controls_layout_->addSpacing(kTcpTitleBarFieldSpacing);

    auto *portRowLayout = new QHBoxLayout();
    portRowLayout->setContentsMargins(0, 0, 0, 0);
    portRowLayout->setSpacing(4);
    port_label_ = new VaporView::VisualTextLabel(this);
    port_label_->setObjectName("fieldLabel");
    portRowLayout->addWidget(port_label_, 0, Qt::AlignVCenter | Qt::AlignRight);

    port_edit_ = new QLineEdit(this);
    port_edit_->setObjectName(QStringLiteral("tcpWavePortEdit"));
    port_edit_->setText(QStringLiteral("8888"));
    port_edit_->setValidator(new QIntValidator(1, 65535, port_edit_));
    port_edit_->setAlignment(Qt::AlignCenter);
    port_edit_->setFixedHeight(kTcpControlHeight);
    port_edit_->setFixedWidth(port_edit_->fontMetrics().horizontalAdvance(QStringLiteral("65535")) + 40);
    portRowLayout->addWidget(port_edit_, 0, Qt::AlignVCenter | Qt::AlignLeft);
    top_controls_layout_->addLayout(portRowLayout, 0);
    top_controls_layout_->addSpacing(kTcpTitleBarFieldSpacing);

    connect_button_ = new QPushButton(this);
    connect_button_->setObjectName("compactTcpStartButton");
    connect_button_->setFixedHeight(kTcpButtonHeight);
    connect(connect_button_, &QPushButton::clicked, this, &TcpWavePanel::onToggleConnectionClicked);
    top_controls_layout_->addWidget(connect_button_, 0, Qt::AlignVCenter | Qt::AlignLeft);


    for (bool raw : {true, false}) {
        auto *button = new QPushButton(topControlsBar);
        button->setObjectName(raw ? QStringLiteral("fpgaMonitorRawButton") : QStringLiteral("fpgaMonitorHarmonicButton"));
        button->setProperty("fpgaWaveAction", true);
        button->setFixedHeight(kTcpButtonHeight);
        button->hide();
        top_controls_layout_->addSpacing(4);
        top_controls_layout_->addWidget(button, 0, Qt::AlignVCenter);
        connect(button, &QPushButton::clicked, this, [this, raw] {
            emit fpgaAcquisitionRequested(raw, !(raw ? fpga_raw_enabled_ : fpga_acquiring_));
        });
        (raw ? fpga_raw_button_ : fpga_harmonic_button_) = button;
    }

    status_label_ = new VaporView::VisualTextLabel(this);
    status_label_->setObjectName("fieldLabel");
    status_label_->setFont(numericFontFrom(status_label_->font()));
    status_label_->setStyleSheet(QStringLiteral(
        "QLabel { font-family: \"Cascadia Mono\", \"Consolas\", \"Courier New\", monospace;"
        "font-size: 14px; font-weight: 600; }"));
    status_label_->setTextFormat(Qt::PlainText);
    status_label_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    status_label_->setWordWrap(false);
    status_label_->setFixedHeight(kTcpButtonHeight);
    status_label_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    status_label_->setMinimumWidth(0);
    status_label_->setVisible(false);
    top_controls_layout_->addSpacing(kTcpTitleBarStatusSpacing);
    top_controls_layout_->addWidget(status_label_, 1, Qt::AlignVCenter | Qt::AlignLeft);
    top_controls_layout_->addStretch(1);

    hint_label_ = new QLabel(this);
    hint_label_->setWordWrap(true);
    hint_label_->setVisible(false);
    control_layout_->addWidget(hint_label_, 1, 0, 1, 6);

    mainLayout->addLayout(control_layout_);

    plots_layout_ = new QGridLayout();
    plots_layout_->setSpacing(1);

    const QStringList names{QStringLiteral("tcpWaveRawPlot"), QStringLiteral("fpgaWaveH2Plot"), QStringLiteral("tcpWaveHarmonicPlot")};
    for (int channel = 0; channel < 2; ++channel) {
        auto &lane = home_harmonics_[channel];
        for (int row = 0; row < 3; ++row) {
            auto *header = createWaveCard(this, lane.cards[row], lane.titles[row]);
            header->parentWidget()->setFixedHeight(kTcpTitleBarHeight);
            auto *info = lane.info[row] = new QLabel(lane.cards[row]);
            info->setObjectName(QStringLiteral("fieldLabel"));
            info->setFont(numericFontFrom(info->font()));
            info->setTextFormat(Qt::PlainText);
            info->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
            header->addWidget(info, 1);
            const auto color = row == 0 ? AppThemeColor::PlotSeriesWaveBlue : AppThemeColor::PlotSeriesWaveOrange;
            auto *plot = lane.plots[row] = new WavePlotWidget(appThemeColor(color, false), lane.cards[row]);
            plot->setObjectName(names[row] + (channel ? QStringLiteral("1") : QString()));
            static_cast<QVBoxLayout *>(lane.cards[row]->layout())->addWidget(plot, 1);
        }
    }
    // The TCP view reuses the first lane's raw and normalized plots.
    const auto &first = home_harmonics_[0];
    wave1_group_ = first.cards[0]; fpga_h2_group_ = first.cards[1]; wave4_group_ = first.cards[2];
    wave1_title_label_ = first.titles[0]; fpga_h2_title_label_ = first.titles[1]; wave4_title_label_ = first.titles[2];
    wave1_info_label_ = first.info[0]; fpga_h2_info_label_ = first.info[1]; wave4_info_label_ = first.info[2];
    wave1_plot_ = first.plots[0]; fpga_h2_plot_ = first.plots[1]; wave4_plot_ = first.plots[2];

    peak_group_ = new QGroupBox(this);
    peak_group_->setObjectName("sensorGroupBox");
    peak_group_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::MinimumExpanding);
    auto *peakLayout = new QVBoxLayout(peak_group_);
    peakLayout->setContentsMargins(2, 2, 2, 2);
    peakLayout->setSpacing(0);
    auto *peakHeaderBar = new PeakHeaderBarWidget(peak_group_);
    peak_header_bar_ = peakHeaderBar;
    peak_header_bar_->setObjectName("sectionTitleBar");
    peak_header_bar_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    peak_header_bar_->installEventFilter(this);
    auto *peakHeaderOuterLayout = new QVBoxLayout(peak_header_bar_);
    peakHeaderOuterLayout->setContentsMargins(8, 1, 8, 1);
    peakHeaderOuterLayout->setSpacing(0);
    peak_header_top_layout_ = new QHBoxLayout();
    peak_header_top_layout_->setContentsMargins(0, 0, 0, 0);
    peak_header_top_layout_->setSpacing(6);
    peak_header_wrapped_layout_ = new QHBoxLayout();
    peak_header_wrapped_layout_->setContentsMargins(0, 0, 0, 0);
    peak_header_wrapped_layout_->setSpacing(6);
    peakHeaderOuterLayout->addLayout(peak_header_top_layout_);
    peakHeaderOuterLayout->addLayout(peak_header_wrapped_layout_);
    peak_title_label_ = new VaporView::VisualTextLabel(peak_header_bar_);
    peak_title_label_->setObjectName("sectionTitleLabel");
    VaporView::configureSelectableCardTitle(peak_title_label_);
    peak_title_label_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    peak_title_label_->setMargin(0);
    peak_title_label_->setContentsMargins(0, 0, 0, 0);
    peak_title_label_->setFixedHeight(kTcpButtonHeight);
    peakHeaderBar->setReferenceRowWidget(peak_title_label_);
    peak_header_top_layout_->addWidget(peak_title_label_, 0, Qt::AlignVCenter | Qt::AlignLeft);
    peak_filter_button_ = new QPushButton(peak_header_bar_);
    peak_filter_button_->setObjectName("compactTcpButton");
    peak_filter_button_->setFixedHeight(kTcpButtonHeight);
    {
        const QFontMetrics metrics(peak_filter_button_->font());
        const int width = std::max({
            134,
            metrics.horizontalAdvance(QStringLiteral("Search:999999-999999 / Exclude Range")) + kTcpCompactButtonTextPadding,
            metrics.horizontalAdvance(QStringLiteral("峰值搜索:999999-999999 / 排除区间")) + kTcpCompactButtonTextPadding
        });
        peak_filter_button_->setFixedWidth(width);
    }
    connect(peak_filter_button_, &QPushButton::clicked, this, &TcpWavePanel::onConfigurePeakFilterClicked);
    peak_header_top_layout_->addWidget(peak_filter_button_, 0, Qt::AlignVCenter | Qt::AlignLeft);
    peak_header_top_layout_->addStretch(1);
    peak_mode_button_ = new VaporView::Ground::Widgets::SegmentedSwitchButton(peak_header_bar_);
    peak_mode_button_->setObjectName("tcpWavePeakPlotModeSwitch");
    peak_mode_button_->setFixedSize(128, kTcpButtonHeight);
    connect(peak_mode_button_, &VaporView::Ground::Widgets::SegmentedSwitchButton::selectionRequested,
            this, &TcpWavePanel::onPeakPlotModeSelected);
    peak_header_top_layout_->addWidget(peak_mode_button_, 0, Qt::AlignVCenter | Qt::AlignRight);
    peak_clear_button_ = new QPushButton(peak_header_bar_);
    peak_clear_button_->setObjectName("compactTcpButton");
    peak_clear_button_->setFixedHeight(kTcpButtonHeight);
    peak_clear_button_->setMinimumWidth(72);
    peak_clear_button_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    connect(peak_clear_button_, &QPushButton::clicked, this, &TcpWavePanel::onClearPeakPlotClicked);
    peak_header_top_layout_->addWidget(peak_clear_button_, 0, Qt::AlignVCenter | Qt::AlignRight);
    peakLayout->addWidget(peak_header_bar_, 0, Qt::AlignTop);
    fpga_peak_controls_ = new QWidget(peak_group_);
    auto *fpgaPeakControlsLayout = new QHBoxLayout(fpga_peak_controls_);
    fpgaPeakControlsLayout->setContentsMargins(8, 2, 8, 2);
    fpgaPeakControlsLayout->setSpacing(6);
    fpga_peak_info_label_ = new QLabel(QStringLiteral(" "), fpga_peak_controls_);
    fpga_peak_info_label_->setObjectName(QStringLiteral("fpgaMonitorPeakInfo"));
    fpga_peak_info_label_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    fpga_peak_info_label_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    fpga_peak_info_label_->hide();
    fpga_peak_value_label_ = new DecimalAlignedPeakLabel(fpga_peak_controls_);
    fpga_peak_value_label_->setObjectName(QStringLiteral("fpgaMonitorPeakValue"));
    static_cast<DecimalAlignedPeakLabel *>(fpga_peak_value_label_)->setPeakText(QStringLiteral("—"));
    fpga_peak_value_label_->hide();
    fpgaPeakControlsLayout->addWidget(fpga_peak_info_label_, 1, Qt::AlignVCenter);
    fpgaPeakControlsLayout->addWidget(fpga_peak_value_label_, 0, Qt::AlignVCenter);
    peakLayout->addWidget(fpga_peak_controls_, 0, Qt::AlignTop);
    fpga_peak_controls_->hide();
    peak_plot_ = new PeakTrendPlotWidget(this);
    peak_plot_->setObjectName(QStringLiteral("tcpWavePeakTrendPlot"));
    peak_plot_->setMaximumHeight(QWIDGETSIZE_MAX);
    peak_plot_->setPlotMode(peak_plot_scatter_mode_ ? PeakTrendPlotWidget::PlotMode::Scatter : PeakTrendPlotWidget::PlotMode::Polyline);
    peakLayout->addWidget(peak_plot_, 1);
    installTcpWaveCardOutline(peak_group_);

    auto *secondHeader = createWaveCard(this, second_peak_card_, second_peak_title_);
    secondHeader->addStretch();
    auto *secondStatus = new QHBoxLayout;
    secondStatus->setContentsMargins(8, 2, 8, 2);
    second_peak_mode_ = new VaporView::Ground::Widgets::SegmentedSwitchButton(second_peak_card_);
    second_peak_mode_->setObjectName(QStringLiteral("tcpWavePeakPlotModeSwitch1"));
    second_peak_mode_->setFixedSize(128, kTcpButtonHeight);
    second_peak_mode_->setSwitchChecked(true, false);
    second_peak_clear_ = new QPushButton(second_peak_card_);
    second_peak_clear_->setObjectName(QStringLiteral("compactTcpButton"));
    second_peak_clear_->setFixedHeight(kTcpButtonHeight);
    secondStatus->addWidget(second_peak_mode_);
    secondStatus->addWidget(second_peak_clear_);
    second_peak_info_ = new QLabel(second_peak_card_);
    second_peak_info_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    second_peak_info_->setObjectName(QStringLiteral("fpgaMonitorPeakInfo1"));
    second_peak_info_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    second_peak_value_ = new DecimalAlignedPeakLabel(second_peak_card_);
    second_peak_value_->setObjectName(QStringLiteral("fpgaMonitorPeakValue1"));
    secondStatus->addWidget(second_peak_info_, 1);
    secondStatus->addWidget(second_peak_value_);
    auto *secondLayout = static_cast<QVBoxLayout *>(second_peak_card_->layout());
    secondLayout->addLayout(secondStatus);
    second_peak_plot_ = new PeakTrendPlotWidget(second_peak_card_);
    second_peak_plot_->setObjectName(QStringLiteral("tcpWavePeakTrendPlot1"));
    second_peak_plot_->setMaximumHeight(QWIDGETSIZE_MAX);
    secondLayout->addWidget(second_peak_plot_, 1);
    connect(second_peak_mode_, &VaporView::Ground::Widgets::SegmentedSwitchButton::selectionRequested, this, [this](bool scatter) {
        second_peak_mode_->setSwitchChecked(scatter, false);
        second_peak_plot_->setPlotMode(scatter ? PeakTrendPlotWidget::PlotMode::Scatter : PeakTrendPlotWidget::PlotMode::Polyline);
    });
    connect(second_peak_clear_, &QPushButton::clicked, this, [this] { clearFpgaPeakTrend(1); });

    home_calibration_bar_ = new QWidget(this);
    auto *calibrationRow = new QHBoxLayout(home_calibration_bar_);
    calibrationRow->setContentsMargins(8, 0, 8, 0);
    addCalibrationControls(home_calibration_, calibrationRow);
    control_layout_->addWidget(home_calibration_bar_, 1, 0, 1, 6);
    for (int channel = 0; channel < 2; ++channel) {
        home_channel_labels_[channel] = createChannelHeading(this);
        createWaterPlots(home_water_[channel], this, channel);
    }
    for (auto *group : {wave1_group_, fpga_h2_group_, wave4_group_, peak_group_})
        group->setSizePolicy(QSizePolicy::Ignored, group->sizePolicy().verticalPolicy());
    mainLayout->addLayout(plots_layout_, 1);

    connect(host_edit_, &QLineEdit::textChanged, this, [this](const QString&) {
        saveRememberedInputState();
        emit connectionEndpointChanged(host(), port());
    });
    connect(port_edit_, &QLineEdit::textChanged, this, [this](const QString&) {
        saveRememberedInputState();
        emit connectionEndpointChanged(host(), port());
    });

    live_display_timer_ = new QTimer(this);
    live_display_timer_->setTimerType(Qt::PreciseTimer);
    live_display_timer_->setInterval(kLiveDisplayRefreshMs);
    connect(live_display_timer_, &QTimer::timeout, this, &TcpWavePanel::updateLiveDisplay);
    live_display_timer_->start();
}

void TcpWavePanel::loadRememberedInputState()
{
    VaporView::migrateLegacyApplicationConfig();
    QSettings applicationSettings = VaporView::applicationConfigSettings();
    applicationSettings.beginGroup(QStringLiteral("TcpWavePanel"));
    QSettings userSettings("VaporView", "TcpWavePanel");
    const QString hostValue = applicationSettings.value("connection/host", host_edit_->text()).toString();
    const QString portValue = applicationSettings.value("connection/port", port_edit_->text()).toString();
    const QString peakFilterMode = userSettings.value("peak_filter/mode", QStringLiteral("none")).toString().trimmed().toLower();
    if (peakFilterMode == QStringLiteral("iqr"))
    {
        peak_filter_settings_.mode = PeakFilterMode::IqrOutlier;
    }
    else if (peakFilterMode == QStringLiteral("keep_range"))
    {
        peak_filter_settings_.mode = PeakFilterMode::KeepRange;
    }
    else if (peakFilterMode == QStringLiteral("exclude_range"))
    {
        peak_filter_settings_.mode = PeakFilterMode::ExcludeRange;
    }
    else
    {
        peak_filter_settings_.mode = PeakFilterMode::None;
    }
    peak_filter_settings_.min_value = userSettings.value("peak_filter/min_value", 0.0).toDouble();
    peak_filter_settings_.max_value = userSettings.value("peak_filter/max_value", 0.0).toDouble();
    peak_search_start_index_ = std::max(0, userSettings.value("peak_search/start_index", kDefaultPeakSearchStartIndex).toInt());
    peak_search_end_index_ = std::max(0, userSettings.value("peak_search/end_index", kDefaultPeakSearchEndIndex).toInt());

    {
        const QSignalBlocker hostBlocker(host_edit_);
        host_edit_->setText(hostValue);
    }
    {
        const QSignalBlocker portBlocker(port_edit_);
        port_edit_->setText(portValue);
    }

    updatePeakFilterButtonText();
}

void TcpWavePanel::saveRememberedInputState() const
{
    QSettings applicationSettings = VaporView::applicationConfigSettings();
    applicationSettings.beginGroup(QStringLiteral("TcpWavePanel"));
    VaporView::setPersistentSetting(applicationSettings, QStringLiteral("connection/host"), host_edit_->text());
    VaporView::setPersistentSetting(applicationSettings, QStringLiteral("connection/port"), port_edit_->text().trimmed());

    QSettings userSettings("VaporView", "TcpWavePanel");

    QString modeKey = QStringLiteral("none");
    if (peak_filter_settings_.mode == PeakFilterMode::IqrOutlier)
    {
        modeKey = QStringLiteral("iqr");
    }
    else if (peak_filter_settings_.mode == PeakFilterMode::KeepRange)
    {
        modeKey = QStringLiteral("keep_range");
    }
    else if (peak_filter_settings_.mode == PeakFilterMode::ExcludeRange)
    {
        modeKey = QStringLiteral("exclude_range");
    }
    VaporView::setPersistentSetting(userSettings, QStringLiteral("peak_filter/mode"), modeKey);
    VaporView::setPersistentSetting(userSettings, QStringLiteral("peak_filter/min_value"), peak_filter_settings_.min_value);
    VaporView::setPersistentSetting(userSettings, QStringLiteral("peak_filter/max_value"), peak_filter_settings_.max_value);
    VaporView::setPersistentSetting(userSettings, QStringLiteral("peak_search/start_index"), peak_search_start_index_);
    VaporView::setPersistentSetting(userSettings, QStringLiteral("peak_search/end_index"), peak_search_end_index_);
}

void TcpWavePanel::setEnglish(bool english)
{
    is_english_ = english;
    for (int channel = 0; channel < 2; ++channel) {
        const QString text = channel == 0
            ? (english ? QStringLiteral("ADC 0 / DLIA 0 · 1368 nm (In-situ)") : QStringLiteral("ADC 0 / DLIA 0 · 1368 nm（原位）"))
            : (english ? QStringLiteral("ADC 1 / DLIA 1 · 816 nm (Remote)") : QStringLiteral("ADC 1 / DLIA 1 · 816 nm（遥测）"));
        home_channel_labels_[channel]->setText(text);
        if (waveform_page_) waveform_page_->lanes[channel].heading->setText(text);
    }
    if (panel_title_label_)
    {
        panel_title_label_->setText(english ? "Wave Monitor" : "波形监控");
        if (auto *titleLabel = dynamic_cast<WaveDisplayTitleLabel *>(panel_title_label_))
        {
            titleLabel->refreshTitleGeometry();
        }
    }
    updateWaveDisplayModeTexts();
    updateWaveDisplayModeIcon();
    resetFrameRateDisplay();
    if (wave_display_button_)
    {
        const QString tooltip = english ? QStringLiteral("Wave display settings") : QStringLiteral("波形显示设置");
        wave_display_button_->setToolTip(tooltip);
        wave_display_button_->setAccessibleName(tooltip);
    }
    host_label_->setText(english ? "TCP Host:" : "TCP主机:");
    port_label_->setText(english ? "Port:" : "端口:");
    connect_button_->setText(isConnected()
        ? (english ? "Disconnect" : "断开")
        : (english ? "Connect" : "连接"));
    wave1_group_->setTitle(QString());
    wave4_group_->setTitle(QString());
    peak_group_->setTitle(QString());
    if (wave1_title_label_)
    {
        wave1_title_label_->setText(fpga_mode_
            ? (english ? "First Harmonic H1" : "一次谐波H1")
            : (english ? "Raw Signal" : "原始信号"));
    }
    fpga_h2_title_label_->setText(english ? "Second Harmonic H2" : "二次谐波H2");
    fpga_h2_plot_->setEnglish(english);
    fpga_h2_plot_->setEmptyText(english ? QStringLiteral("No data") : QStringLiteral("暂无数据"));
    if (wave4_title_label_)
    {
        wave4_title_label_->setText(english ? "Normalized Second Harmonic" : "归一化二次谐波");
    }
    if (peak_title_label_)
    {
        const QString titleText = english ? QStringLiteral("Normalized Second Harmonic Peak Trend") : QStringLiteral("归一化二次谐波峰值趋势");
        peak_title_label_->setText(titleText);
        peak_title_label_->setToolTip(titleText);
    }
    if (wave1_plot_)
    {
        wave1_plot_->setEnglish(english);
        wave1_plot_->setEmptyText(english ? QStringLiteral("No data") : QStringLiteral("暂无数据"));
    }
    if (wave4_plot_)
    {
        wave4_plot_->setEnglish(english);
        wave4_plot_->setEmptyText(english ? QStringLiteral("No data") : QStringLiteral("暂无数据"));
    }
    if (peak_plot_)
    {
        peak_plot_->setEnglish(english);
        peak_plot_->setEmptyText(english ? QStringLiteral("No peak data") : QStringLiteral("暂无峰值数据"));
    }
    if (peak_clear_button_)
    {
        peak_clear_button_->setText(english ? "Clear Trend" : "清空趋势");
    }
    hint_label_->clear();
    hint_label_->setVisible(false);

    wave1_info_label_->setText(english ? "waiting for frame" : "等待数据帧");
    wave4_info_label_->setText(english ? "waiting for frame" : "等待数据帧");
    updatePeakFilterButtonText();
    updatePeakPlotModeButtonText();
    updatePeakHeaderLayout();
    for (int row = 0; row < 3; ++row) {
        home_harmonics_[1].titles[row]->setText(home_harmonics_[0].titles[row]->text());
        home_harmonics_[1].plots[row]->setEmptyText(english ? "No data" : "暂无数据");
    }
    second_peak_title_->setText(peak_title_label_->text());
    second_peak_clear_->setText(peak_clear_button_->text());
    second_peak_mode_->setSegmentTexts(english ? "Line" : "折线", english ? "Scatter" : "散点");
    second_peak_mode_->setStateDescription(english ? "Trend" : "趋势显示", english ? ": " : "：");
    second_peak_plot_->setEmptyText(english ? "No peak data" : "暂无峰值数据");
    if (fpga_mode_) refreshFpgaWaveform();
    if (waveform_page_) {
        waveform_page_->titleLabel->setText(english ? QStringLiteral("Waveforms") : QStringLiteral("波形"));
        const QStringList labels = english
            ? QStringList{"Raw", "H1", "H2", "Norm H2", "Peak Trend", "Concentration", "Water Profile"}
            : QStringList{"原始波形", "一次谐波", "二次谐波", "归一化二次谐波", "峰值趋势", "水汽浓度", "水汽廓线"};
        for (int row = 0; row < labels.size(); ++row) waveform_page_->labels[row]->setText(labels[row]);
        for (auto &lane : waveform_page_->lanes) {
            for (auto *wave : lane.waves) wave->setEmptyText(english ? "No data" : "暂无数据");
            lane.trend->setEmptyText(english ? "No peak data" : "暂无峰值数据");
            lane.mode->setSegmentTexts(english ? "Line" : "折线", english ? "Scatter" : "散点");
            lane.clear->setText(peak_clear_button_->text());
        }
        if (!fpga_mode_) refreshFpgaWaveform();
    }
    refreshWaterPlots();
    applyWaveDisplayMode();

    if (!socket_ || socket_->state() != QAbstractSocket::ConnectedState)
    {
        setStatusText(english ? "Idle" : "空闲");
    }
    setFpgaAcquisitionState(fpga_acquisition_available_, fpga_acquisition_busy_, fpga_acquiring_, fpga_raw_enabled_);
}

void TcpWavePanel::setFpgaMode(bool enabled)
{
    if (fpga_mode_ == enabled) return;
    fpga_mode_ = enabled;
    setProperty("fpgaWaveformMode", enabled);
    if (enabled && socket_ && socket_->state() != QAbstractSocket::UnconnectedState)
        requestGracefulDisconnect();
    const QList<QWidget *> tcpControls = {host_label_, host_edit_, port_label_, port_edit_,
                                         connect_button_, frame_rate_label_};
    for (QWidget *widget : tcpControls)
        widget->setVisible(!enabled);
    fpga_raw_button_->setVisible(enabled);
    fpga_harmonic_button_->setVisible(enabled);
    fpga_peak_info_label_->setVisible(enabled);
    fpga_peak_value_label_->setVisible(enabled);
    peak_filter_button_->setVisible(!enabled);
    fpga_peak_controls_->setVisible(enabled);
    clearRemoteWaveformDisplay();
    if (!enabled) { fpga_waves_.clear(); fpga_peaks_.clear(); clearWaterSamples(); clearWaterProfiles(); }
    wave1_plot_->setSampleAxis(enabled);
    wave4_plot_->setSampleAxis(enabled);
    updateEndpointEditorState();
    setEnglish(is_english_);
}

void TcpWavePanel::setFpgaAcquisitionState(bool available, bool busy, bool acquiring, bool rawEnabled)
{
    fpga_acquisition_available_ = available;
    fpga_acquisition_busy_ = busy;
    fpga_acquiring_ = acquiring;
    fpga_raw_enabled_ = rawEnabled;
    fpga_raw_button_->setText(is_english_
        ? (rawEnabled ? "Raw waveform: Stop" : "Raw waveform: Start")
        : (rawEnabled ? "原始波形：停止" : "原始波形：开始"));
    fpga_harmonic_button_->setText(is_english_
        ? (acquiring ? "H1/H2: Stop" : "H1/H2: Start")
        : (acquiring ? "H1/H2：停止" : "H1/H2：开始"));
    fpga_raw_button_->setToolTip(is_english_
        ? "Start acquisition if needed, then enable RAW upload. Stop only disables RAW upload."
        : "需要原始波形时开启 RAW 上传；停止只关闭原始波形上传，保留 H1/H2 采集。");
    fpga_harmonic_button_->setToolTip(is_english_
        ? "Uses FPGA Start/Stop acquisition. Stopping also stops raw signal acquisition."
        : "对应 FPGA 页面的 H1/H2 采集；停止会同时停止原始波形上传。");
    for (auto *button : {fpga_raw_button_, fpga_harmonic_button_})
        button->setEnabled(fpga_mode_ && available && !busy);
    if (waveform_page_) {
        for (auto [button, source] : {std::pair{waveform_page_->raw, fpga_raw_button_},
                                      std::pair{waveform_page_->harmonic, fpga_harmonic_button_}}) {
            button->setText(source->text());
            button->setToolTip(source->toolTip());
            button->setEnabled(source->isEnabled());
        }
    }
}

void TcpWavePanel::setFpgaWaveform(quint16 source, const QVector<QVector<double>> &samples,
                                  const QStringList &components, const QString &detail, quint32 totalPoints, bool preview)
{
    if (source != 0x20 && source != 0x21 && source != 0x30 && source != 0x31) return;
    fpga_waves_[source] = {samples, components, detail, totalPoints, preview};
    if (fpga_mode_) refreshFpgaWaveform(source & 1);
}

void TcpWavePanel::setFpgaSecondHarmonicPeak(quint16 source, quint32 cycleId, quint64 timestamp, double peak, bool preview)
{
    if (!fpga_mode_ || (source != 0x30 && source != 0x31)) return;
    auto &trend = fpga_peaks_[source];
    if (trend.hasCycle && trend.lastCycleId == cycleId && trend.lastTimestamp == timestamp) return;
    trend.hasCycle = true;
    trend.lastCycleId = cycleId;
    trend.lastTimestamp = timestamp;
    trend.preview = preview;
    trend.values.append(float(peak));
    // FPGA timestamps are device ticks; label the trend with host receipt time.
    trend.wallMsecs.append(QDateTime::currentMSecsSinceEpoch());
    if (trend.values.size() > kPeakTrendFrameWindow) {
        const int count = trend.values.size() - kPeakTrendFrameWindow;
        trend.values.remove(0, count);
        trend.wallMsecs.remove(0, count);
    }
    refreshFpgaWaveform(source & 1);
}

void TcpWavePanel::refreshFpgaWaveform(int changedChannel)
{
    if (!waveform_page_ && !fpga_mode_) return;
    for (int channel = 0; channel < 2; ++channel) {
        if (changedChannel < 0 || changedChannel == channel)
            refreshFpgaChannel(channel);
    }
}

void TcpWavePanel::refreshFpgaChannel(int channel)
{
    const auto dlia = fpga_mode_ ? fpga_waves_.value(0x30 + channel) : FpgaWaveDisplay{};
    const auto convert = [](const QVector<double> &values) {
        QVector<float> result; result.reserve(values.size());
        for (double value : values) result.append(float(value));
        return result;
    };
    auto h1 = convert(dlia.samples.value(dlia.components.indexOf(QStringLiteral("H1"))));
    auto h2 = convert(dlia.samples.value(dlia.components.indexOf(QStringLiteral("H2"))));
    if (dlia.components.contains(QStringLiteral("I1"))) {
        const auto magnitude = [&dlia](const QString &inPhase, const QString &quadrature) {
            const auto &i = dlia.samples.value(dlia.components.indexOf(inPhase));
            const auto &q = dlia.samples.value(dlia.components.indexOf(quadrature));
            QVector<float> values; values.reserve(std::min(i.size(), q.size()));
            for (qsizetype point = 0; point < std::min(i.size(), q.size()); ++point)
                values.append(float(std::hypot(i[point], q[point])));
            return values;
        };
        h1 = magnitude(QStringLiteral("I1"), QStringLiteral("Q1"));
        h2 = magnitude(QStringLiteral("I2"), QStringLiteral("Q2"));
    }
    QVector<float> normalized;
    const int normalizedH2 = dlia.components.indexOf(QStringLiteral("H2/max(H1)"));
    if (normalizedH2 >= 0) normalized = convert(dlia.samples.value(normalizedH2));
    // Use one display origin for all components of the complete sweep.
    bool centered = false;
    if (!dlia.preview && h2.size() > 1) {
        const auto [low, peak] = std::minmax_element(h2.cbegin(), h2.cend());
        if (*peak > *low) {
            const auto count = h2.size();
            const auto offset = (peak - h2.cbegin() + count - count / 2) % count;
            for (auto *trace : {&h1, &h2, &normalized}) {
                if (trace->size() == count) std::rotate(trace->begin(), trace->begin() + offset, trace->end());
            }
            centered = true;
        }
    }
    const QString waiting = is_english_ ? QStringLiteral("Waiting for FPGA waveform") : QStringLiteral("等待 FPGA 波形");
    const auto present = [this, &waiting](QLabel *label, const FpgaWaveDisplay &wave, bool empty) {
        if (wave.samples.isEmpty() && !wave.detail.isEmpty()) {
            label->setText(wave.detail);
            label->setToolTip(wave.detail);
            return;
        }
        const bool historical = wave.detail.contains(QStringLiteral("历史")) || wave.detail.contains(QStringLiteral("historical"), Qt::CaseInsensitive);
        const QString quality = historical ? (is_english_ ? QStringLiteral("Historical") : QStringLiteral("历史值"))
            : empty ? (is_english_ ? QStringLiteral("Unavailable") : QStringLiteral("无有效波形"))
                    : wave.preview ? (is_english_ ? QStringLiteral("Preview") : QStringLiteral("预览"))
                                   : (is_english_ ? QStringLiteral("Complete") : QStringLiteral("完整周期"));
        label->setText(wave.samples.isEmpty() ? waiting
            : QString(is_english_ ? "%1 points · %2" : "%1 点 · %2")
                .arg(wave.preview ? QStringLiteral("%1/%2").arg(wave.samples.value(0).size()).arg(wave.totalPoints)
                                  : QString::number(wave.totalPoints)).arg(quality));
        label->setToolTip(wave.detail);
    };
    const auto trend = fpga_peaks_.value(0x30 + channel);
    const QString peakText = (dlia.samples.isEmpty() && !dlia.detail.isEmpty()) || trend.values.isEmpty() || !std::isfinite(trend.values.last())
        ? QStringLiteral("—") : QString::number(trend.values.last(), 'g', 7);
    if (waveform_page_) {
        auto &lane = waveform_page_->lanes[channel];
        const auto raw = fpga_mode_ ? fpga_waves_.value(0x20 + channel) : FpgaWaveDisplay{};
        const auto adc = convert(raw.samples.value(raw.components.indexOf(QStringLiteral("ADC"))));
        const std::array<const QVector<float> *, 4> traces{&adc, &h1, &h2, &normalized};
        for (int row = 0; row < 4; ++row) {
            const auto &wave = row == 0 ? raw : dlia;
            present(lane.info[row], wave, traces[row]->isEmpty());
            lane.waves[row]->setSampleAxis(true, wave.totalPoints);
            lane.waves[row]->setSamples(*traces[row]);
        }
        lane.trend->setPeakValues(trend.values, trend.wallMsecs);
        lane.info[4]->setText((is_english_ ? QStringLiteral("Latest: ") : QStringLiteral("最新：")) + peakText);
    }
    if (!fpga_mode_) return;
    if (channel == 0) {
        wave1_history_ = h1;
        fpga_h2_history_ = h2;
        wave4_history_ = normalized;
        peak_history_ = trend.values;
        peak_wall_msecs_ = trend.wallMsecs;
    }
    const auto &home = home_harmonics_[channel];
    const std::array<const QVector<float> *, 3> traces{&h1, &h2, &normalized};
    for (int row = 0; row < 3; ++row) {
        const auto &samples = *traces[row];
        present(home.info[row], dlia, std::none_of(samples.cbegin(), samples.cend(), [](float v) { return std::isfinite(v); }));
        home.plots[row]->setSampleAxis(true, dlia.totalPoints);
        home.plots[row]->setSamples(samples);
    }
    if (!dlia.detail.isEmpty()) home.info[2]->setToolTip(dlia.detail + (is_english_
        ? QStringLiteral("\nH2 divided by the largest H1 magnitude in the received cycle or preview.")
        : QStringLiteral("\nH2 除以本周期或预览内的 H1 最大幅值。")));
    if (centered) {
        const auto origin = is_english_ ? QStringLiteral("H2 peak centered") : QStringLiteral("H2 主峰居中");
        for (auto *label : home.info) label->setToolTip(label->toolTip() + '\n' + origin);
    }
    (channel ? second_peak_plot_ : peak_plot_)->setPeakValues(trend.values, trend.wallMsecs);
    auto *peakInfo = channel ? second_peak_info_ : fpga_peak_info_label_;
    auto *peakValue = channel ? second_peak_value_ : fpga_peak_value_label_;
    peakInfo->setText(trend.values.isEmpty() ? waiting
        : QString(is_english_ ? "%1 · Latest:" : "%1 · 最新：")
            .arg(trend.preview ? (is_english_ ? QStringLiteral("Preview peaks") : QStringLiteral("预览峰值"))
                               : (is_english_ ? QStringLiteral("Cycle peaks") : QStringLiteral("周期峰值"))));
    static_cast<DecimalAlignedPeakLabel *>(peakValue)->setPeakText(peakText);
    peakInfo->setToolTip(is_english_
        ? QStringLiteral("Maximum H2 divided by maximum H1, before display decimation. Preview peaks cover received points only. Time is host receipt time.")
        : QStringLiteral("绘图抽稀前计算的 H2 峰值与 H1 峰值之比。预览峰值仅覆盖收到的点，时间为本机接收时间。"));
}

void TcpWavePanel::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (!event)
    {
        return;
    }

    if (event->type() == QEvent::FontChange || event->type() == QEvent::ApplicationFontChange ||
        event->type() == QEvent::StyleChange) updateWaveformPageRowMinimumHeights();

    if (event->type() == QEvent::ApplicationPaletteChange ||
        event->type() == QEvent::PaletteChange ||
        event->type() == QEvent::StyleChange ||
        event->type() == QEvent::FontChange)
    {
        updateWaveDisplayModeIcon();
        updatePeakFilterButtonText();
        updatePeakHeaderLayout();
    }
}

bool TcpWavePanel::eventFilter(QObject *watched, QEvent *event)
{
    if (waveform_page_ && watched == waveform_page_->scroll && event &&
        (event->type() == QEvent::Show || event->type() == QEvent::FontChange ||
         event->type() == QEvent::StyleChange || event->type() == QEvent::LayoutRequest))
        updateWaveformPageRowMinimumHeights();
    if (watched == peak_header_bar_ && event)
    {
        switch (event->type())
        {
        case QEvent::Show:
        case QEvent::Resize:
        case QEvent::FontChange:
        case QEvent::StyleChange:
        case QEvent::LayoutRequest:
            updatePeakHeaderLayout();
            break;
        default:
            break;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void TcpWavePanel::updatePeakHeaderLayout()
{
    if (!peak_header_bar_ || !peak_header_bar_->isVisible() ||
        !peak_header_top_layout_ || !peak_header_wrapped_layout_ ||
        !peak_title_label_ || !peak_mode_button_ || !peak_clear_button_)
    {
        return;
    }

    const QMargins barMargins = peak_header_bar_->layout()
        ? peak_header_bar_->layout()->contentsMargins()
        : QMargins();
    const int availableWidth = peak_header_bar_->contentsRect().width()
        - barMargins.left() - barMargins.right();
    if (availableWidth <= 0)
    {
        return;
    }

    const auto layoutWidth = [](const QWidget *widget) {
        int width = widget->sizeHint().width();
        if (widget->minimumWidth() > 0)
        {
            width = std::max(width, widget->minimumWidth());
        }
        if (widget->maximumWidth() < QWIDGETSIZE_MAX)
        {
            width = std::min(width, widget->maximumWidth());
        }
        return width;
    };

    const int spacing = std::max(0, peak_header_top_layout_->spacing());
    const int titleWidth = std::max(
        peak_title_label_->sizeHint().width(),
        peak_title_label_->fontMetrics().horizontalAdvance(peak_title_label_->text()) + 2);
    const int filterWidth = (peak_filter_button_ && !peak_filter_button_->isHidden())
        ? layoutWidth(peak_filter_button_) + spacing
        : 0;
    const int requiredWidth = titleWidth + filterWidth + spacing
        + layoutWidth(peak_mode_button_) + spacing + layoutWidth(peak_clear_button_);
    const bool shouldWrap = requiredWidth > availableWidth;
    if (shouldWrap == peak_header_wrapped_)
    {
        return;
    }

    peak_header_wrapped_ = shouldWrap;
    peak_header_top_layout_->removeWidget(peak_mode_button_);
    peak_header_top_layout_->removeWidget(peak_clear_button_);
    peak_header_wrapped_layout_->removeWidget(peak_mode_button_);
    peak_header_wrapped_layout_->removeWidget(peak_clear_button_);
    while (QLayoutItem *item = peak_header_wrapped_layout_->takeAt(0))
    {
        delete item;
    }

    if (shouldWrap)
    {
        peak_header_wrapped_layout_->addStretch(1);
        peak_header_wrapped_layout_->addWidget(peak_mode_button_, 0, Qt::AlignVCenter | Qt::AlignRight);
        peak_header_wrapped_layout_->addWidget(peak_clear_button_, 0, Qt::AlignVCenter | Qt::AlignRight);
    }
    else
    {
        peak_header_top_layout_->addWidget(peak_mode_button_, 0, Qt::AlignVCenter | Qt::AlignRight);
        peak_header_top_layout_->addWidget(peak_clear_button_, 0, Qt::AlignVCenter | Qt::AlignRight);
    }
    if (auto *outerLayout = qobject_cast<QVBoxLayout *>(peak_header_bar_->layout()))
    {
        outerLayout->setSpacing(shouldWrap ? 2 : 0);
        outerLayout->invalidate();
    }
    peak_header_bar_->updateGeometry();
    if (peak_group_ && peak_group_->layout())
    {
        peak_group_->layout()->invalidate();
    }
    if (peak_group_)
    {
        peak_group_->updateGeometry();
    }
    if (plots_layout_)
    {
        plots_layout_->invalidate();
    }
    updateGeometry();
    emit preferredPanelHeightChanged();
}

void TcpWavePanel::updateWaveDisplayModeTexts()
{
    if (auto *titleLabel = dynamic_cast<WaveDisplayTitleLabel *>(panel_title_label_))
    {
        titleLabel->setEnglish(is_english_, fpga_mode_);
    }
}

void TcpWavePanel::updateWaveDisplayModeIcon()
{
    const bool dark = VaporView::isDarkThemeEnabled();
    updateSectionTitleIcons(this, dark);
    if (auto *titleLabel = dynamic_cast<WaveDisplayTitleLabel *>(panel_title_label_))
    {
        const QIcon waveDisplayIcon = createWaveDisplayIcon(appThemeColor(AppThemeColor::Primary, dark));
        titleLabel->setIcon(waveDisplayIcon);
        titleLabel->updateCheckIcons(dark);
        if (wave_display_button_)
        {
            wave_display_button_->setIcon(waveDisplayIcon);
        }
    }
    if (wave_display_button_)
    {
        const QString hoverColor = appThemeColor(AppThemeColor::TitleBarHover, dark).name(QColor::HexRgb);
        const QString pressedColor = appThemeColor(AppThemeColor::PrimarySubtlePressed, dark).name(QColor::HexRgb);
        wave_display_button_->setStyleSheet(QStringLiteral(
            "QToolButton#tcpWaveDisplayButton { background-color: transparent; border: none; border-radius: 4px; padding: 0px; }"
            "QToolButton#tcpWaveDisplayButton:hover { background-color: %1; }"
            "QToolButton#tcpWaveDisplayButton:pressed { background-color: %2; }")
            .arg(hoverColor, pressedColor));
    }
}

const TcpWavePanel::WaveDisplayMode &TcpWavePanel::waveDisplayMode() const
{
    return fpga_mode_ ? fpga_wave_display_ : direct_wave_display_;
}

void TcpWavePanel::applyWaveDisplayMode()
{
    const auto &mode = waveDisplayMode();
    const bool showH2 = fpga_mode_ && (mode.all || mode.h2);
    fpga_h2_group_->setVisible(showH2);
    const bool showRawWave = mode.all || mode.raw;
    const bool showHarmonicWave = mode.all || mode.harmonic || (!fpga_mode_ && mode.defaults);
    const bool showPeakTrend = mode.all || mode.peak_trend || (!fpga_mode_ && mode.defaults);

    if (auto *titleLabel = dynamic_cast<WaveDisplayTitleLabel *>(panel_title_label_))
    {
        titleLabel->setCurrentStates(mode.all, mode.raw, mode.harmonic, mode.peak_trend, mode.h2, mode.defaults);
    }

    if (wave1_group_)
    {
        wave1_group_->setVisible(showRawWave);
    }
    if (wave4_group_)
    {
        wave4_group_->setVisible(showHarmonicWave);
    }
    if (peak_group_)
    {
        peak_group_->setVisible(showPeakTrend);
    }

    const bool showWater = fpga_mode_ && (mode.all || mode.defaults);
    const std::array<bool, 3> showSecond{showRawWave, showH2, showHarmonicWave};
    for (int row = 0; row < 3; ++row) {
        home_harmonics_[1].cards[row]->setVisible(fpga_mode_ && showSecond[row]);
        home_harmonics_[1].plots[row]->setMaximumHeight(QWIDGETSIZE_MAX);
    }
    second_peak_card_->setVisible(fpga_mode_ && showPeakTrend);
    home_calibration_bar_->setVisible(showWater);
    for (auto *heading : home_channel_labels_) heading->setVisible(fpga_mode_ && hasVisibleWaveDisplay());
    for (const auto &cards : home_water_) {
        cards.concentration->setVisible(showWater);
        cards.profile->setVisible(showWater);
    }
    layoutPlots();
    const bool wavePlotsFillAvailableHeight = fpga_mode_ || !showPeakTrend;
    fpga_h2_plot_->setMaximumHeight(showH2 && wavePlotsFillAvailableHeight
        ? QWIDGETSIZE_MAX : kWavePlotMaximumHeight);
    if (wave1_plot_)
    {
        wave1_plot_->setMaximumHeight(showRawWave && wavePlotsFillAvailableHeight
            ? QWIDGETSIZE_MAX
            : kWavePlotMaximumHeight);
    }
    if (wave4_plot_)
    {
        wave4_plot_->setMaximumHeight(showHarmonicWave && wavePlotsFillAvailableHeight
            ? QWIDGETSIZE_MAX
            : kWavePlotMaximumHeight);
    }

    if (showRawWave && wave1_plot_)
    {
        wave1_plot_->setSamples(wave1_history_, wave1_wall_msecs_);
    }
    if (showHarmonicWave && wave4_plot_)
    {
        wave4_plot_->setSamples(wave4_history_, wave4_wall_msecs_);
    }
    if (showPeakTrend && peak_plot_)
    {
        peak_plot_->setPeakValues(peak_history_, peak_wall_msecs_);
    }

    if (peak_group_ && peak_group_->layout())
    {
        peak_group_->layout()->invalidate();
    }
    if (plots_layout_)
    {
        plots_layout_->invalidate();
    }
    updateGeometry();
    emit preferredPanelHeightChanged();
}

QWidget *TcpWavePanel::createWaveformPage(QWidget *parent)
{
    Q_ASSERT(!waveform_page_);
    auto *scroll = new QScrollArea(parent);
    const auto setSurface = [](QWidget *widget, const QString &name) {
        widget->setObjectName(name);
        widget->setAttribute(Qt::WA_StyledBackground);
        widget->setAutoFillBackground(true);
    };
    setSurface(scroll, QStringLiteral("waveformMonitorPage"));
    setSurface(scroll->viewport(), QStringLiteral("waveformPageViewport"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *body = new QWidget(scroll);
    setSurface(body, QStringLiteral("waveformPageBody"));
    auto *list = new QVBoxLayout(body);
    list->setContentsMargins(12, 8, 12, 12);
    list->setSpacing(0);
    list->setSizeConstraint(QLayout::SetMinAndMaxSize);
    auto *outerCard = new QFrame(body);
    outerCard->setObjectName(QStringLiteral("waveformPageOuterCard"));
    outerCard->setFrameShape(QFrame::NoFrame);
    VaporView::configureTopLevelCard(outerCard);
    list->addWidget(outerCard);
    auto *outerLayout = new QVBoxLayout(outerCard);
    outerLayout->setContentsMargins(1, 0, 1, 1);
    outerLayout->setSpacing(0);
    auto *bar = new QWidget(outerCard);
    setSurface(bar, QStringLiteral("sectionTitleBar"));
    bar->setMinimumHeight(kTcpMainTitleBarHeight);
    bar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto *controls = new QHBoxLayout(bar);
    controls->setContentsMargins(8, 2, 8, 2);
    controls->setSpacing(6);
    outerLayout->addWidget(bar, 0, Qt::AlignTop);
    waveform_page_ = std::make_unique<WaveformPage>();
    connect(scroll, &QObject::destroyed, this, [this] { waveform_page_.reset(); });
    auto &page = *waveform_page_;
    page.titleLabel = new VaporView::VisualTextLabel(bar);
    page.titleLabel->setObjectName(QStringLiteral("sectionTitleLabel"));
    controls->addWidget(createSectionTitleCluster(bar, QStringLiteral("chart-line"), page.titleLabel, kTcpButtonHeight),
                        0, Qt::AlignVCenter | Qt::AlignLeft);
    controls->addSpacing(8);
    for (bool raw : {true, false}) {
        auto *button = new QPushButton(bar);
        button->setObjectName(raw ? QStringLiteral("wavePageRawButton") : QStringLiteral("wavePageHarmonicButton"));
        button->setProperty("fpgaWaveAction", true);
        button->setProperty("styleSheetManagedHeight", true);
        button->setFixedHeight(kTcpButtonHeight);
        controls->addWidget(button, 0, Qt::AlignVCenter);
        auto *homeButton = raw ? fpga_raw_button_ : fpga_harmonic_button_;
        connect(button, &QPushButton::clicked, homeButton, &QPushButton::click);
        (raw ? page.raw : page.harmonic) = button;
    }
    addCalibrationControls(page.calibration, controls);
    page.scroll = scroll;
    using VaporView::Ground::Wave::VerticalRowLabel;
    using VaporView::Ground::Wave::RowResizeHandle;
    auto *content = new QWidget(outerCard);
    setSurface(content, QStringLiteral("waveformPageContent"));
    auto *contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(12, 10, 12, 12);
    contentLayout->setSpacing(0);
    outerLayout->addWidget(content, 1);
    auto *headings = new QHBoxLayout;
    headings->setSpacing(6);
    // Use the same font-dependent width as the seven row labels.
    auto *headingSpacer = new VerticalRowLabel(content);
    auto spacerPolicy = headingSpacer->sizePolicy();
    spacerPolicy.setRetainSizeWhenHidden(true);
    headingSpacer->setSizePolicy(spacerPolicy);
    headings->addWidget(headingSpacer);
    headingSpacer->hide();
    for (int channel = 0; channel < 2; ++channel) {
        page.lanes[channel].heading = createChannelHeading(content);
        headings->addWidget(page.lanes[channel].heading, 1);
    }
    contentLayout->addLayout(headings);
    contentLayout->addSpacing(6);
    const QStringList plotNames{"Raw", "H1", "H2", "Normalized"};
    for (int index = 0; index < 7; ++index) {
        auto *row = new QWidget(content);
        page.rows[index] = row;
        setSurface(row, QStringLiteral("waveformPageRow"));
        row->setFixedHeight(index < 4 ? 136 : index == 6 ? 154 : 186);
        contentLayout->addWidget(row);
        auto *rowLayout = new QVBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(0);
        auto *columns = new QHBoxLayout;
        columns->setSpacing(6);
        rowLayout->addLayout(columns, 1);
        page.labels[index] = new VerticalRowLabel(row);
        page.labels[index]->setObjectName(QStringLiteral("waveformRowLabel"));
        columns->addWidget(page.labels[index]);
        for (int channel = 0; channel < 2; ++channel) {
            auto &lane = page.lanes[channel];
            auto *cell = new QFrame(row);
            page.cards[index][channel] = cell;
            cell->setObjectName(QStringLiteral("waveformPageCard"));
            cell->setFrameShape(QFrame::NoFrame);
            cell->setAttribute(Qt::WA_StyledBackground);
            cell->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
            auto *layout = new QVBoxLayout(cell);
            layout->setContentsMargins(8, 6, 8, 6);
            layout->setSpacing(0);
            columns->addWidget(cell, 1);
            if (index == 5) { lane.water.concentration = cell; continue; }
            if (index == 6) {
                lane.water.profile = cell;
                createWaterPlots(lane.water, content, channel);
                continue;
            }
            auto *infoRow = new QHBoxLayout;
            page.headerLayouts[index][channel] = infoRow;
            layout->addLayout(infoRow);
            lane.info[index] = new QLabel(cell);
            lane.info[index]->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
            infoRow->addWidget(lane.info[index], 1);
            if (index < 4) {
                const auto color = index < 2 ? AppThemeColor::PlotSeriesWaveBlue : AppThemeColor::PlotSeriesWaveOrange;
                auto *wave = new WavePlotWidget(appThemeColor(color, false), cell);
                wave->setObjectName(QStringLiteral("wavePage%1Channel%2").arg(plotNames[index]).arg(channel));
                wave->setSampleAxisMaxIntervals(12);
                wave->setProperty("styleSheetManagedHeight", true);
                wave->setMaximumHeight(QWIDGETSIZE_MAX);
                layout->addWidget(wave, 1);
                lane.waves[index] = wave;
            } else {
                lane.trend = new PeakTrendPlotWidget(cell);
                lane.trend->setObjectName(QStringLiteral("wavePageTrendChannel%1").arg(channel));
                lane.trend->setProperty("styleSheetManagedHeight", true);
                lane.trend->setMaximumHeight(QWIDGETSIZE_MAX);
                layout->addWidget(lane.trend, 1);
                lane.mode = new VaporView::Ground::Widgets::SegmentedSwitchButton(cell);
                lane.mode->setFixedSize(128, kTcpButtonHeight);
                lane.mode->setSwitchChecked(true, false);
                lane.clear = new QPushButton(cell);
                lane.clear->setObjectName(QStringLiteral("compactTcpButton"));
                lane.clear->setFixedHeight(kTcpButtonHeight);
                infoRow->addWidget(lane.mode);
                infoRow->addWidget(lane.clear);
                connect(lane.mode, &VaporView::Ground::Widgets::SegmentedSwitchButton::selectionRequested, cell, [&lane](bool scatter) {
                    lane.mode->setSwitchChecked(scatter, false);
                    lane.trend->setPlotMode(scatter ? PeakTrendPlotWidget::PlotMode::Scatter : PeakTrendPlotWidget::PlotMode::Polyline);
                });
                connect(lane.clear, &QPushButton::clicked, this, [this, channel] { clearFpgaPeakTrend(channel); });
            }
        }
        auto *handle = new RowResizeHandle(row, 0, 640, {}, row);
        page.handles[index] = handle;
        handle->setMinHeightProvider([this, index] { return waveformPageRowMinimumHeight(index); });
        handle->setObjectName(QStringLiteral("waveformRowResizeHandle"));
        rowLayout->addWidget(handle);
    }
    list->addStretch();
    scroll->setWidget(body);
    setEnglish(is_english_);
    scroll->installEventFilter(this);
    updateWaveformPageRowMinimumHeights();
    return scroll;
}

int TcpWavePanel::waveformPageRowMinimumHeight(int index) const
{
    const auto &page = *waveform_page_;
    int cardHeight = 0;
    for (int ch = 0; ch < 2; ++ch) {
        const auto &lane = page.lanes[ch];
        const auto applyMinimum = [](auto *plot) {
            const int minimum = plot->minimumHeightForFourYTicks();
            plot->setMinimumHeight(minimum);
            return minimum;
        };
        int height = index < 4 ? applyMinimum(lane.waves[index]) : index == 4 ? applyMinimum(lane.trend)
                   : index == 5 ? applyMinimum(lane.water.series) : applyMinimum(lane.water.layers);
        auto *card = page.cards[index][ch];
        auto *layout = card->layout();
        if (auto *header = page.headerLayouts[index][ch]) {
            height += std::max({header->sizeHint().height(), header->minimumSize().height(),
                                index < 4 ? lane.info[index]->fontMetrics().height() : 0}) + layout->spacing();
        }
        const auto margins = card->contentsMargins() + layout->contentsMargins();
        cardHeight = std::max(cardHeight, height + margins.top() + margins.bottom());
    }
    auto *handle = page.handles[index];
    const int handleHeight = std::max(handle->height(), handle->minimumHeight());
    return cardHeight + (handleHeight ? handleHeight : 6);
}

void TcpWavePanel::updateWaveformPageRowMinimumHeights()
{
    if (!waveform_page_) return;
    for (int index = 0; index < 7; ++index) {
        auto *handle = waveform_page_->handles[index];
        if (!handle) continue;
        const int minimum = waveformPageRowMinimumHeight(index);
        handle->setHeightBounds(minimum, std::max(minimum, 640));
        auto *row = waveform_page_->rows[index];
        row->setProperty("waveformRowMinHeight", minimum);
        if (row->height() < minimum || row->maximumHeight() < minimum) row->setFixedHeight(minimum);
    }
}

void TcpWavePanel::addCalibrationControls(CalibrationControls &controls, QHBoxLayout *row)
{
    controls.label = new QLabel(row->parentWidget());
    controls.label->setObjectName(QStringLiteral("waterCalibrationLabel"));
    controls.label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    controls.label->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    auto *combo = controls.combo = new VaporView::SingleLevelPopupComboBox(row->parentWidget());
    combo->setObjectName(QStringLiteral("waterCalibrationComboBox"));
    combo->setProperty("waterCalibrationCombo", true);
    combo->setProperty("styleSheetManagedHeight", true);
    combo->setFixedHeight(kTcpButtonHeight);
    combo->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    combo->setMinimumContentsLength(12);
    combo->setMinimumWidth(140);
    combo->setMaximumWidth(260);
    combo->setPopupFitContents(true);
    combo->setSelectionCheckIconProvider([] {
        return createMenuCheckIcon(appThemeColor(AppThemeColor::MenuCheckText, isDarkThemeEnabled()));
    });
    row->addWidget(controls.label, 0, Qt::AlignVCenter);
    row->addWidget(combo, 0, Qt::AlignVCenter);
    row->addStretch(1);
    connect(combo, &QComboBox::activated, this, [this, combo](int index) {
        if (water_calibration_locked_ || index < 0 || index >= combo->count()) return;
        const QString path = combo->itemData(index).toString();
        if (path.isEmpty() || (path == water_calibration_selected_path_ && !water_calibration_key_.isEmpty())) return;
        water_calibration_selected_path_ = path;
        water_calibration_name_ = QFileInfo(path).fileName();
        syncCalibrationControls();
        emit waterCalibrationFileSelected(path);
    });
}

static QString profileResolutionText(int resolution, bool english)
{
    if (resolution == WaterProfileAccumulator::unbinnedResolution)
        return english ? QStringLiteral("Unbinned") : QStringLiteral("不分箱格");
    return QStringLiteral("%1 m").arg(resolution);
}

void TcpWavePanel::createWaterPlots(WaterCards &cards, QWidget *parent, int channel)
{
    const bool listCell = cards.profile != nullptr;
    QHBoxLayout *profileHeader;
    if (listCell) {
        profileHeader = new QHBoxLayout;
        waveform_page_->headerLayouts[6][channel] = profileHeader;
        static_cast<QVBoxLayout *>(cards.profile->layout())->addLayout(profileHeader);
    } else {
        QGroupBox *group;
        createWaveCard(parent, group, cards.concentrationTitle);
        cards.concentration = group;
        profileHeader = createWaveCard(parent, group, cards.profileTitle);
        cards.profile = group;
    }
    cards.concentration->setProperty("waterPlotCard", "concentration");
    cards.series = new PeakTrendPlotWidget(cards.concentration);
    cards.series->setObjectName(channel ? QStringLiteral("waterConcentrationPlot1") : QStringLiteral("waterConcentrationPlot"));
    if (listCell) cards.series->setProperty("styleSheetManagedHeight", true);
    else cards.series->setMinimumHeight(kWavePlotMinimumHeight);
    cards.series->setMaximumHeight(QWIDGETSIZE_MAX);
    cards.series->setPlotMode(PeakTrendPlotWidget::PlotMode::Polyline);
    static_cast<QVBoxLayout *>(cards.concentration->layout())->addWidget(cards.series, 1);
    cards.profile->setProperty("waterPlotCard", "profile");
    cards.layers = new WaterProfilePlotWidget(cards.profile);
    cards.summary = new QLabel(cards.profile);
    cards.summary->setObjectName(QStringLiteral("waterProfileSummary"));
    cards.summary->setIndent(6);
    cards.summary->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    if (listCell) {
        cards.layers->setMaxXAxisIntervals(12);
        cards.layers->setProperty("styleSheetManagedHeight", true);
        profileHeader->addWidget(cards.summary, 1);
    } else {
        profileHeader->addStretch();
        static_cast<QVBoxLayout *>(cards.profile->layout())->addWidget(cards.summary);
    }
    cards.resolution = new VaporView::SingleLevelPopupComboBox(cards.profile);
    cards.resolution->setObjectName(QStringLiteral("waterProfileResolution%1").arg(channel));
    cards.resolution->setFixedHeight(kTcpButtonHeight);
    cards.resolution->setPopupFitContents(true);
    cards.resolution->setSelectionCheckIconProvider([] {
        return createMenuCheckIcon(appThemeColor(AppThemeColor::MenuCheckText, isDarkThemeEnabled()));
    });
    for (const int resolution : WaterProfileAccumulator::resolutions)
        cards.resolution->addItem(profileResolutionText(resolution, is_english_), resolution);
    profileHeader->addWidget(cards.resolution);
    connect(cards.resolution, &QComboBox::currentIndexChanged, this, [this, channel, combo = cards.resolution] {
        profile_resolution_m_[channel] = combo->currentData().toInt();
        refreshWaterPlots(channel);
    });
    cards.clear = new QPushButton(cards.profile);
    cards.clear->setObjectName(QStringLiteral("compactTcpButton"));
    cards.clear->setFixedHeight(kTcpButtonHeight);
    profileHeader->addWidget(cards.clear);
    connect(cards.clear, &QPushButton::clicked, this, [this, channel] {
        excluded_profile_id_[channel] = last_water_id_[channel];
        water_profile_->clearChannel(channel);
        refreshWaterPlots();
    });
    cards.layers->setObjectName(channel ? QStringLiteral("waterProfilePlot1") : QStringLiteral("waterProfilePlot"));
    static_cast<QVBoxLayout *>(cards.profile->layout())->addWidget(cards.layers, 1);
}

void TcpWavePanel::setWaterCalibrationFiles(const QStringList &files, const QString &selectedPath)
{
    water_calibration_files_ = files;
    water_calibration_selected_path_ = selectedPath;
    syncCalibrationControls();
}

void TcpWavePanel::syncCalibrationControls()
{
    const auto fileText = is_english_ ? QStringLiteral("Calibration File") : QStringLiteral("定标文件");
    const QString selectText = is_english_ ? QStringLiteral("Select a calibration file") : QStringLiteral("请选择定标文件");
    const QString unselectedText = is_english_ ? QStringLiteral("Not selected") : QStringLiteral("未选择");
    const bool selected = !water_calibration_name_.isEmpty();
    QString tooltip = selected ? water_calibration_name_ : selectText;
    if (!selected && water_calibration_files_.isEmpty()) {
        tooltip = is_english_ ? QStringLiteral("No .vcal files. Generate or import one on the Water Calibration page first.")
                              : QStringLiteral("没有可用的 .vcal 文件，请先在水汽定标页面生成或导入定标文件。");
    }
    if (!water_calibration_detail_.isEmpty()) tooltip += QStringLiteral("\n") + water_calibration_detail_;
    const auto update = [&](const CalibrationControls &controls) {
        auto *combo = controls.combo;
        if (!combo) return;
        controls.label->setText(fileText);
        controls.label->setToolTip(tooltip);
        combo->setAccessibleName(fileText);
        combo->setToolTip(tooltip);
        const QSignalBlocker blocker(combo);
        combo->clear();
        if (!selected) {
            combo->addItem(unselectedText, QString());
            if (auto *model = qobject_cast<QStandardItemModel *>(combo->model())) model->item(0)->setEnabled(false);
        }
        for (const auto &path : water_calibration_files_) combo->addItem(QFileInfo(path).fileName(), path);
        int index = 0;
        if (selected) {
            index = combo->findData(water_calibration_selected_path_);
            if (index < 0) index = combo->findText(water_calibration_name_);
            if (index < 0) {
                index = combo->count();
                combo->addItem(water_calibration_name_, water_calibration_selected_path_);
            }
        }
        combo->setCurrentIndex(index);
        combo->setEnabled(!water_calibration_locked_ && !water_calibration_files_.isEmpty());
    };
    update(home_calibration_);
    if (waveform_page_) update(waveform_page_->calibration);
}

void TcpWavePanel::setWaterCalibration(const QString &key, const QString &name, const QString &unit,
                                       bool locked, const QString &detail)
{
    water_calibration_locked_ = locked;
    if (key.isEmpty() && name.isEmpty()) water_calibration_selected_path_.clear();
    if (water_calibration_key_ == key && water_calibration_name_ == name &&
        water_unit_ == unit && water_calibration_detail_ == detail) {
        syncCalibrationControls();
        return;
    }
    const bool changed = water_calibration_key_ != key || water_calibration_name_ != name;
    if (changed) {
        if (!water_calibration_key_.isEmpty()) water_profile_->clearAll();
        last_water_id_ = {};
        excluded_profile_id_ = {};
        water_calibration_key_ = key;
        water_samples_.clear();
    }
    water_calibration_name_ = name;
    water_unit_ = unit;
    water_calibration_detail_ = detail;
    refreshWaterPlots();
}

void TcpWavePanel::clearWaterSamples()
{
    if (water_samples_.isEmpty()) return;
    water_samples_.clear();
    refreshWaterPlots();
}

void TcpWavePanel::clearWaterProfiles()
{
    // CalibrationService republishes the current reading on unrelated changes.
    excluded_profile_id_ = last_water_id_;
    water_profile_->clearAll();
    refreshWaterPlots();
}

void TcpWavePanel::appendWaterSample(int channel, const QString &id, qint64 timeMs, double value, double heightM)
{
    if (!fpga_mode_ || channel < 0 || channel > 1 || water_calibration_key_.isEmpty() || id.isEmpty() || timeMs <= 0) return;
    last_water_id_[channel] = id;
    const bool paired = id != excluded_profile_id_[channel] && water_profile_->append(channel, id, timeMs, value, heightM);
    auto &series = water_samples_[channel];
    if (series.isEmpty() || (series.last().timeMs <= timeMs && series.last().id != id)) {
        series.append({id, timeMs, value});
        if (series.size() > kPeakTrendFrameWindow) series.removeFirst();
    } else if (!paired) return;
    refreshWaterPlots(channel);
}

void TcpWavePanel::refreshWaterPlots(int changedChannel)
{
    const auto name = is_english_ ? QStringLiteral("Water Vapor Concentration") : QStringLiteral("水汽浓度");
    const auto profile = is_english_ ? QStringLiteral("Water Vapor Profile") : QStringLiteral("水汽廓线");
    const QString unit = water_unit_.isEmpty() ? QString() : QStringLiteral(" (%1)").arg(water_unit_);
    const QString selectText = is_english_ ? QStringLiteral("Select a calibration file") : QStringLiteral("请选择定标文件");
    if (changedChannel < 0) syncCalibrationControls();
    for (int channel = 0; channel < 2; ++channel) {
        if (changedChannel >= 0 && changedChannel != channel) continue;
        const auto latest = water_profile_->latest(channel);
        const bool unbinned = profile_resolution_m_[channel] == WaterProfileAccumulator::unbinnedResolution;
        const auto points = unbinned ? water_profile_->rawPoints(channel) : QVector<WaterProfileAccumulator::RawPoint>{};
        const auto totalSamples = water_profile_->rawSampleCount(channel);
        const auto layers = water_profile_->layers(channel, profile_resolution_m_[channel]);
        const auto missing = QStringLiteral("—");
        const auto count = unbinned ? points.size() : layers.size();
        const QString summary = (unbinned
            ? (is_english_ ? QStringLiteral("Y Height: %1   X Concentration: %2   Points: %3") : QStringLiteral("Y 高度：%1   X 浓度：%2   点数：%3"))
            : is_english_ ? QStringLiteral("Y Height: %1   X Concentration: %2   Layers: %3")
            : QStringLiteral("Y 高度：%1   X 浓度：%2   层数：%3"))
            .arg(latest ? formatWaveValue(latest->heightM, 2) + QStringLiteral(" m") : missing,
                 latest ? formatWaveValue(latest->concentration, 2) + QStringLiteral(" ") + water_unit_ : missing,
                 count ? QString::number(count) : missing);
        const auto &series = water_samples_.value(channel);
        QVector<float> values;
        QVector<qint64> times;
        for (const auto &sample : series) {
            values.append(float(sample.value));
            times.append(sample.timeMs);
        }
        const auto update = [&](WaterCards &cards, const QString &suffix) {
            cards.summary->setText(summary);
            cards.summary->setToolTip(unbinned && totalSamples > quint64(count)
                ? summary + (is_english_ ? QStringLiteral("\nTotal samples: %1") : QStringLiteral("\n总样本数：%1")).arg(totalSamples) : summary);
            const QSignalBlocker blocker(cards.resolution);
            cards.resolution->setItemText(cards.resolution->findData(0), profileResolutionText(0, is_english_));
            cards.resolution->setCurrentIndex(cards.resolution->findData(profile_resolution_m_[channel]));
            cards.resolution->setToolTip(is_english_ ? QStringLiteral("Height resolution") : QStringLiteral("高度分辨率"));
            cards.clear->setText(is_english_ ? QStringLiteral("Clear") : QStringLiteral("清空"));
            if (cards.concentrationTitle) cards.concentrationTitle->setText(name + unit + suffix);
            if (cards.profileTitle) cards.profileTitle->setText(profile + suffix);
            cards.series->setEmptyText(water_calibration_name_.isEmpty() ? selectText
                : (is_english_ ? QStringLiteral("Waiting for concentration") : QStringLiteral("等待有效浓度")));
            cards.series->setPeakValues(values, times);
            const QString emptyText = water_calibration_name_.isEmpty() ? selectText
                : (is_english_ ? QStringLiteral("Waiting for paired lidar and concentration") : QStringLiteral("等待配对的测距与浓度"));
            if (unbinned) cards.layers->setUnbinnedProfile(points, latest, totalSamples, unit, is_english_, emptyText);
            else cards.layers->setProfile(layers, latest, profile_resolution_m_[channel], unit, is_english_, emptyText);
        };
        const QString suffix = QStringLiteral(" · CH %1").arg(channel);
        update(home_water_[channel], suffix);
        if (waveform_page_) update(waveform_page_->lanes[channel].water, {});
    }
}

void TcpWavePanel::layoutPlots()
{
    while (auto *item = plots_layout_->takeAt(0)) delete item;
    plots_layout_->setSpacing(4);
    plots_layout_->setColumnStretch(0, 1);
    plots_layout_->setColumnStretch(1, 1);
    int row = 0;
    const auto addRow = [&](QWidget *left, QWidget *right = nullptr) {
        if (left && left->isHidden()) left = nullptr;
        if (right && right->isHidden()) right = nullptr;
        if (left && right) {
            plots_layout_->addWidget(left, row, 0);
            plots_layout_->addWidget(right, row++, 1);
        } else if (left || right) {
            plots_layout_->addWidget(left ? left : right, row++, 0, 1, 2);
        }
    };
    if (fpga_mode_) {
        addRow(home_channel_labels_[0], home_channel_labels_[1]);
        for (int index = 0; index < 3; ++index) addRow(home_harmonics_[0].cards[index], home_harmonics_[1].cards[index]);
        addRow(peak_group_, second_peak_card_);
        addRow(home_water_[0].concentration, home_water_[1].concentration);
        addRow(home_water_[0].profile, home_water_[1].profile);
    } else {
        addRow(wave1_group_, compact_layout_ ? nullptr : wave4_group_);
        if (compact_layout_) addRow(wave4_group_);
        addRow(peak_group_);
    }
}

void TcpWavePanel::updatePeakPlotModeButtonText()
{
    if (!peak_mode_button_)
    {
        return;
    }

    peak_mode_button_->setSegmentTexts(is_english_ ? QStringLiteral("Line") : QStringLiteral("折线"),
                                       is_english_ ? QStringLiteral("Scatter") : QStringLiteral("散点"));
    peak_mode_button_->setStateDescription(is_english_ ? QStringLiteral("Trend") : QStringLiteral("趋势显示"),
                                          is_english_ ? QStringLiteral(": ") : QStringLiteral("："));
    if (peak_mode_button_->switchChecked() != peak_plot_scatter_mode_)
        peak_mode_button_->setSwitchChecked(peak_plot_scatter_mode_, false);
}

QString TcpWavePanel::peakFilterModeText(PeakFilterMode mode) const
{
    switch (mode)
    {
    case PeakFilterMode::IqrOutlier:
        return is_english_ ? QStringLiteral("IQR") : QStringLiteral("IQR");
    case PeakFilterMode::KeepRange:
        return is_english_ ? QStringLiteral("Keep") : QStringLiteral("保留");
    case PeakFilterMode::ExcludeRange:
        return is_english_ ? QStringLiteral("Exclude") : QStringLiteral("排除");
    case PeakFilterMode::None:
    default:
        return is_english_ ? QStringLiteral("Off") : QStringLiteral("关闭");
    }
}

void TcpWavePanel::updatePeakFilterButtonText()
{
    if (!peak_filter_button_)
    {
        return;
    }

    const QString searchEndText = peak_search_end_index_ <= 0
        ? (is_english_ ? QStringLiteral("end") : QStringLiteral("末尾"))
        : QString::number(peak_search_end_index_);
    const QString text = QStringLiteral("%1:%2-%3 / %4")
        .arg(is_english_ ? QStringLiteral("Search") : QStringLiteral("峰值搜索"))
        .arg(peak_search_start_index_)
        .arg(searchEndText)
        .arg(peakFilterModeText(peak_filter_settings_.mode));
    peak_filter_button_->setText(text);
    peak_filter_button_->setToolTip(text);
    const QFontMetrics metrics(peak_filter_button_->font());
    peak_filter_button_->setFixedWidth(std::clamp(metrics.horizontalAdvance(text) + kTcpCompactButtonTextPadding, 168, 420));
    updatePeakHeaderLayout();
}

float TcpWavePanel::currentWaveformPeakValue(const QVector<float>& samples) const
{
    return waveformPeakValue(samples, peak_search_start_index_, peak_search_end_index_);
}

void TcpWavePanel::rebuildPeakHistory()
{
    peak_history_.clear();
    peak_history_.reserve(peak_raw_history_.size());

    QVector<double> finiteValues;
    finiteValues.reserve(peak_raw_history_.size());
    for (float value : peak_raw_history_)
    {
        if (std::isfinite(value))
        {
            finiteValues.push_back(static_cast<double>(value));
        }
    }

    double iqrLowerBound = -std::numeric_limits<double>::infinity();
    double iqrUpperBound = std::numeric_limits<double>::infinity();
    if (peak_filter_settings_.mode == PeakFilterMode::IqrOutlier && finiteValues.size() >= 4)
    {
        const double q1 = percentileValue(finiteValues, 0.25);
        const double q3 = percentileValue(finiteValues, 0.75);
        if (std::isfinite(q1) && std::isfinite(q3))
        {
            const double iqr = q3 - q1;
            const double padding = std::max(1e-6, iqr * 1.5);
            iqrLowerBound = q1 - padding;
            iqrUpperBound = q3 + padding;
        }
    }

    const double rangeMin = std::min(peak_filter_settings_.min_value, peak_filter_settings_.max_value);
    const double rangeMax = std::max(peak_filter_settings_.min_value, peak_filter_settings_.max_value);
    for (float rawValue : peak_raw_history_)
    {
        bool keepValue = std::isfinite(rawValue);
        if (keepValue)
        {
            const double value = static_cast<double>(rawValue);
            switch (peak_filter_settings_.mode)
            {
            case PeakFilterMode::IqrOutlier:
                keepValue = value >= iqrLowerBound && value <= iqrUpperBound;
                break;
            case PeakFilterMode::KeepRange:
                keepValue = value >= rangeMin && value <= rangeMax;
                break;
            case PeakFilterMode::ExcludeRange:
                keepValue = !(value >= rangeMin && value <= rangeMax);
                break;
            case PeakFilterMode::None:
            default:
                keepValue = true;
                break;
            }
        }

        peak_history_.push_back(keepValue
            ? rawValue
            : std::numeric_limits<float>::quiet_NaN());
    }

    live_display_dirty_ = true;
}

void TcpWavePanel::resetFrameRateDisplay()
{
    last_frame_rate_label_update_ms_ = 0;
    if (!frame_rate_label_)
    {
        return;
    }
    frame_rate_label_->setText(QString(is_english_ ? "Realtime: %1 Hz" : "实时频率: %1 Hz")
        .arg(fixedStatusText(QStringLiteral("--"), 7)));
}

void TcpWavePanel::updateFrameRateDisplay(qint64 arrivalTimeMs)
{
    if (!frame_rate_label_)
    {
        return;
    }

    frame_arrival_times_ms_.push_back(arrivalTimeMs);
    while (!frame_arrival_times_ms_.isEmpty() &&
           arrivalTimeMs - frame_arrival_times_ms_.front() > kFrameRateWindowMs)
    {
        frame_arrival_times_ms_.removeFirst();
    }

    if (last_frame_rate_label_update_ms_ > 0 &&
        arrivalTimeMs - last_frame_rate_label_update_ms_ < kFrameRateLabelRefreshMs)
    {
        return;
    }
    last_frame_rate_label_update_ms_ = arrivalTimeMs;

    double rateHz = 0.0;
    if (frame_arrival_times_ms_.size() >= 2)
    {
        const qint64 elapsedMs = frame_arrival_times_ms_.back() - frame_arrival_times_ms_.front();
        if (elapsedMs > 0)
        {
            rateHz = (frame_arrival_times_ms_.size() - 1) * 1000.0 / static_cast<double>(elapsedMs);
        }
    }

    frame_rate_label_->setText(QString(is_english_ ? "Realtime: %1 Hz" : "实时频率: %1 Hz")
        .arg(fixedStatusText(QString::number(rateHz, 'f', 2), 7)));
}

void TcpWavePanel::updateLiveDisplay()
{
    if (fpga_mode_) return;
    if (!live_display_dirty_)
    {
        return;
    }

    live_display_dirty_ = false;
    if ((direct_wave_display_.all || direct_wave_display_.raw) && wave1_plot_)
    {
        wave1_plot_->setSamples(wave1_history_, wave1_wall_msecs_);
    }
    if ((direct_wave_display_.all || direct_wave_display_.harmonic) && wave4_plot_)
    {
        wave4_plot_->setSamples(wave4_history_, wave4_wall_msecs_);
    }
    if ((direct_wave_display_.all || direct_wave_display_.peak_trend) && peak_plot_)
    {
        peak_plot_->setPeakValues(peak_history_, peak_wall_msecs_);
    }
    if (wave1_info_label_ && !pending_wave1_info_text_.isEmpty())
    {
        wave1_info_label_->setText(pending_wave1_info_text_);
    }
    if (wave4_info_label_ && !pending_wave4_info_text_.isEmpty())
    {
        wave4_info_label_->setText(pending_wave4_info_text_);
    }
    if (!pending_live_status_text_.isEmpty())
    {
        setStatusText(pending_live_status_text_);
    }
}

void TcpWavePanel::updatePendingRemoteLiveStatus()
{
    QStringList parts;
    if (!remote_waveform_status_text_.isEmpty())
    {
        parts << remote_waveform_status_text_;
    }
    if (!remote_feature_status_text_.isEmpty())
    {
        parts << remote_feature_status_text_;
    }
    if (parts.isEmpty())
    {
        pending_live_status_text_.clear();
        return;
    }
    pending_live_status_text_ = parts.join(QStringLiteral("  "));
}

void TcpWavePanel::attachWaveformSplitControls(QLabel *label, QSpinBox *spinBox)
{
    if (!top_controls_layout_ || !label || !spinBox)
    {
        return;
    }

    label->setParent(this);
    spinBox->setParent(this);
    auto *splitRowLayout = new QHBoxLayout();
    splitRowLayout->setContentsMargins(0, 0, 0, 0);
    splitRowLayout->setSpacing(4);
    splitRowLayout->addWidget(label, 0, Qt::AlignVCenter | Qt::AlignRight);
    splitRowLayout->addWidget(spinBox, 0, Qt::AlignVCenter | Qt::AlignLeft);
    top_controls_layout_->insertSpacing(std::max(0, top_controls_layout_->count() - 2), kTcpTitleBarFieldSpacing);
    top_controls_layout_->insertLayout(std::max(0, top_controls_layout_->count() - 2), splitRowLayout, 0);
}

QString TcpWavePanel::host() const
{
    return host_edit_ ? host_edit_->text() : QStringLiteral("127.0.0.1");
}

int TcpWavePanel::port() const
{
    bool ok = false;
    const int value = port_edit_ ? port_edit_->text().trimmed().toInt(&ok) : 8888;
    return ok && value >= 1 && value <= 65535 ? value : 8888;
}

bool TcpWavePanel::endpointEditable() const
{
    return host_edit_ && port_edit_ && host_edit_->isEnabled() && port_edit_->isEnabled();
}

void TcpWavePanel::setConnectionEndpoint(const QString& host, int port, bool persist)
{
    const QString trimmedHost = host.trimmed();
    const QString boundedHost = trimmedHost.isEmpty()
        ? QStringLiteral("127.0.0.1")
        : trimmedHost;
    const int boundedPort = port >= 1 && port <= 65535 ? port : 8888;
    const bool changed =
        (host_edit_ && host_edit_->text() != boundedHost) ||
        (port_edit_ && port_edit_->text() != QString::number(boundedPort));
    if (host_edit_)
    {
        const QSignalBlocker blocker(host_edit_);
        host_edit_->setText(boundedHost);
    }
    if (port_edit_)
    {
        const QSignalBlocker blocker(port_edit_);
        port_edit_->setText(QString::number(boundedPort));
    }
    if (persist)
    {
        saveRememberedInputState();
    }
    if (changed)
    {
        emit connectionEndpointChanged(boundedHost, boundedPort);
    }
}

bool TcpWavePanel::isConnected() const
{
    if (ui_test_mode_)
    {
        return ui_test_connected_;
    }
    if (remote_sky_mode_)
    {
        return remote_wave_tcp_connected_;
    }
    return socket_ && socket_->state() == QAbstractSocket::ConnectedState;
}

bool TcpWavePanel::isConnecting() const
{
    if (ui_test_mode_)
    {
        return false;
    }
    if (remote_sky_mode_ || !socket_)
    {
        return false;
    }
    return socket_->state() == QAbstractSocket::HostLookupState ||
           socket_->state() == QAbstractSocket::ConnectingState;
}

void TcpWavePanel::toggleConnection()
{
    onToggleConnectionClicked();
}

void TcpWavePanel::setRemoteSkyMode(bool enabled)
{
    if (remote_sky_mode_ != enabled) { fpga_waves_.clear(); fpga_peaks_.clear(); }
    remote_sky_mode_ = enabled;
    if (host_label_)
    {
        host_label_->setText(is_english_ ? "TCP Host:" : "TCP主机:");
    }
    if (port_label_)
    {
        port_label_->setText(is_english_ ? "Port:" : "端口:");
    }
    updateEndpointEditorState();
    if (enabled && socket_ && socket_->state() != QAbstractSocket::UnconnectedState)
    {
        requestGracefulDisconnect();
    }
    if (connect_button_)
    {
        connect_button_->setText(isConnected()
            ? (is_english_ ? "Disconnect" : "断开")
            : (is_english_ ? "Connect" : "连接"));
    }
    if (status_label_)
    {
        status_label_->clear();
        status_label_->setVisible(false);
    }
    if (enabled && !ui_test_mode_ && !remote_wave_tcp_connected_)
    {
        clearRemoteWaveformDisplay(is_english_ ? QStringLiteral("TCP wave is not connected")
                                               : QStringLiteral("TCP波形未连接"));
    }
    if (fpga_mode_) refreshFpgaWaveform();
}

void TcpWavePanel::setRemoteWaveTcpState(VaporView::DeviceState state)
{
    const bool wasConnected = remote_wave_tcp_connected_;
    remote_wave_tcp_connected_ = state == VaporView::DeviceState::Connected;
    if (ui_test_mode_)
    {
        return;
    }
    if (remote_sky_mode_ && connect_button_)
    {
        connect_button_->setText(remote_wave_tcp_connected_
            ? (is_english_ ? "Disconnect" : "断开")
            : (is_english_ ? "Connect" : "连接"));
    }
    if (remote_sky_mode_)
    {
        updateEndpointEditorState();
        const bool hasRemoteDataStatus = !remote_waveform_status_text_.isEmpty() || !remote_feature_status_text_.isEmpty();
        if (remote_wave_tcp_connected_ && hasRemoteDataStatus)
        {
            updatePendingRemoteLiveStatus();
            if (!pending_live_status_text_.isEmpty())
            {
                setStatusText(pending_live_status_text_);
            }
        }
        else
        {
            setStatusText(QString(is_english_ ? "TCP wave: %1" : "TCP波形：%1")
                               .arg(VaporView::deviceStateName(state)));
        }
        if (!remote_wave_tcp_connected_ && wasConnected)
        {
            last_remote_feature_time_us_ = 0;
            clearRemoteWaveformDisplay(is_english_ ? QStringLiteral("TCP wave disconnected")
                                                   : QStringLiteral("TCP波形已断开"));
        }
    }
}

void TcpWavePanel::setRemoteFeatureRateHz(double rateHz)
{
    remote_expected_feature_interval_us_ = rateHz > 0.0 && std::isfinite(rateHz)
        ? static_cast<quint64>(std::max(1.0, 1'000'000.0 / rateHz))
        : 0ULL;
}

void TcpWavePanel::injectRemoteRawSignalFrame(quint64 timestampUs, const QVector<float>& samples)
{
    if (fpga_mode_) return;
    if (ui_test_mode_ ? !ui_test_connected_
                      : (remote_sky_mode_ && !remote_wave_tcp_connected_))
    {
        return;
    }
    if (samples.isEmpty())
    {
        return;
    }
    const QString sampleCountText = fixedStatusInteger(samples.size(), kRemoteStatusCountWidth);
    wave1_history_ = samples;
    wave1_wall_msecs_ = wallMsecsFromTimestampUs(timestampUs);
    pending_wave1_info_text_ = QString(is_english_ ? "%1 samples" : "%1 点")
        .arg(sampleCountText);
    remote_waveform_status_text_ = remoteWaveformStatusText(is_english_, samples.size());
    updatePendingRemoteLiveStatus();
    live_display_dirty_ = true;
}

void TcpWavePanel::injectRemoteSecondHarmonicFrame(quint64 timestampUs, const QVector<float>& samples)
{
    if (fpga_mode_) return;
    if (ui_test_mode_ ? !ui_test_connected_
                      : (remote_sky_mode_ && !remote_wave_tcp_connected_))
    {
        return;
    }
    if (samples.isEmpty())
    {
        return;
    }
    wave4_history_ = samples;
    wave4_wall_msecs_ = wallMsecsFromTimestampUs(timestampUs);
    ++frame_count_;
    updateFrameRateDisplay(QDateTime::currentMSecsSinceEpoch());
    const QString sampleCountText = fixedStatusInteger(samples.size(), kRemoteStatusCountWidth);
    if (!wave1_history_.isEmpty())
    {
        pending_wave1_info_text_ = QString(is_english_ ? "%1 samples" : "%1 点")
            .arg(fixedStatusInteger(wave1_history_.size(), kRemoteStatusCountWidth));
    }
    pending_wave4_info_text_ = QString(is_english_ ? "%1 samples" : "%1 点").arg(sampleCountText);
    remote_waveform_status_text_ = remoteWaveformStatusText(is_english_, samples.size());
    updatePendingRemoteLiveStatus();
    live_display_dirty_ = true;
    emit normalizedSecondHarmonicFrameReady(timestampUs, wave4_history_);
}

void TcpWavePanel::injectRemoteWaveformFeature(const VaporView::WaveformFeature& feature)
{
    if (fpga_mode_) return;
    if (ui_test_mode_ ? !ui_test_connected_
                      : (remote_sky_mode_ && !remote_wave_tcp_connected_))
    {
        return;
    }
    const bool validFeature = feature.quality_flags == 0 && feature.host_time_us > 0 && std::isfinite(feature.peak);
    if (!validFeature)
    {
        peak_raw_history_.push_back(std::numeric_limits<float>::quiet_NaN());
        peak_wall_msecs_.push_back(wallMsecsFromTimestampUs(feature.host_time_us));
        last_remote_feature_time_us_ = feature.host_time_us;
        if (peak_raw_history_.size() > kPeakTrendFrameWindow)
        {
            const int removeCount = peak_raw_history_.size() - kPeakTrendFrameWindow;
            peak_raw_history_.remove(0, removeCount);
            peak_wall_msecs_.remove(0, std::min<qsizetype>(removeCount, peak_wall_msecs_.size()));
        }
        rebuildPeakHistory();
        remote_feature_status_text_ = remoteInvalidFeatureStatusText(is_english_);
        updatePendingRemoteLiveStatus();
        live_display_dirty_ = true;
        return;
    }
    peak_raw_history_.push_back(feature.peak);
    peak_wall_msecs_.push_back(wallMsecsFromTimestampUs(feature.host_time_us));
    last_remote_feature_time_us_ = feature.host_time_us;
    if (peak_raw_history_.size() > kPeakTrendFrameWindow)
    {
        const int removeCount = peak_raw_history_.size() - kPeakTrendFrameWindow;
        peak_raw_history_.remove(0, removeCount);
        peak_wall_msecs_.remove(0, std::min<qsizetype>(removeCount, peak_wall_msecs_.size()));
    }
    rebuildPeakHistory();
    remote_feature_status_text_ = remoteFeatureStatusText(is_english_,
                                                           feature.peak,
                                                           feature.rms,
                                                           feature.search_start_index,
                                                           feature.search_end_index,
                                                           feature.peak_index);
    updatePendingRemoteLiveStatus();
    live_display_dirty_ = true;
}

void TcpWavePanel::applyRemotePeakSearchRange(quint32 startIndex, quint32 endIndex)
{
    peak_search_start_index_ = static_cast<int>(startIndex);
    peak_search_end_index_ = static_cast<int>(endIndex);
    peak_raw_history_.clear();
    peak_history_.clear();
    peak_wall_msecs_.clear();
    last_remote_feature_time_us_ = 0;
    if (peak_plot_)
    {
        peak_plot_->setPeakValues({});
    }
    saveRememberedInputState();
    updatePeakFilterButtonText();
    setStatusText(is_english_
        ? QStringLiteral("Peak search range accepted. Waiting for the next feature frame.")
        : QStringLiteral("峰值搜索区间已接受，等待下一帧特征值。"));
}

void TcpWavePanel::rejectRemotePeakSearchRange(const QString& reason)
{
    setStatusText(is_english_
        ? QStringLiteral("Peak search range rejected: %1").arg(reason)
        : QStringLiteral("峰值搜索区间被拒绝：%1").arg(reason));
}

void TcpWavePanel::setUiTestMode(bool enabled)
{
    if (ui_test_mode_ == enabled)
    {
        return;
    }
    fpga_waves_.clear();
    fpga_peaks_.clear();
    if (fpga_mode_) refreshFpgaWaveform();
    if (enabled)
    {
        ui_test_saved_host_ = host_edit_ ? host_edit_->text() : QString();
        ui_test_saved_port_ = port_edit_ ? port_edit_->text() : QString();
        ui_test_mode_ = true;
        ui_test_connected_ = true;
        if (host_edit_) host_edit_->setText(QStringLiteral("ui-test.local"));
        if (port_edit_) port_edit_->setText(QStringLiteral("18888"));
        setConnectedUiState(true);
        setStatusText(is_english_ ? QStringLiteral("UI test waveform source connected (simulated)")
                                  : QStringLiteral("界面测试波形源已连接（模拟）"));
        return;
    }
    clearRemoteWaveformDisplay();
    ui_test_mode_ = false;
    ui_test_connected_ = false;
    if (host_edit_) host_edit_->setText(ui_test_saved_host_);
    if (port_edit_) port_edit_->setText(ui_test_saved_port_);
    setConnectedUiState(false);
    setStatusText(QString());
}

void TcpWavePanel::setUiTestConnected(bool connected)
{
    if (!ui_test_mode_ || ui_test_connected_ == connected)
    {
        return;
    }
    ui_test_connected_ = connected;
    setConnectedUiState(connected);
    emit connectionStateChanged(connected);
}

#ifdef VAPORVIEW_MAIN_WINDOW_TESTING
void TcpWavePanel::testFlushLiveDisplay()
{
    updateLiveDisplay();
}

QVector<float> TcpWavePanel::testHarmonicSamples(int channel) const
{
    return channel ? home_harmonics_[1].plots[2]->samples() : wave4_history_;
}
QVector<float> TcpWavePanel::testFirstHarmonicSamples(int channel) const
{
    return channel ? home_harmonics_[1].plots[0]->samples() : wave1_history_;
}
QVector<float> TcpWavePanel::testSecondHarmonicSamples(int channel) const
{
    return channel ? home_harmonics_[1].plots[1]->samples() : fpga_h2_history_;
}
QVector<float> TcpWavePanel::testPeakValues(int channel) const
{
    return channel ? fpga_peaks_.value(0x31).values : peak_history_;
}

QString TcpWavePanel::testRawXAxisLabel() const
{
    return wave1_plot_ ? wave1_plot_->property("xAxisRightLabel").toString() : QString();
}

QString TcpWavePanel::testHarmonicXAxisLabel() const
{
    return wave4_plot_ ? wave4_plot_->property("xAxisRightLabel").toString() : QString();
}

QString TcpWavePanel::testPeakXAxisLabel() const
{
    return peak_plot_ ? peak_plot_->property("xAxisRightLabel").toString() : QString();
}

QString TcpWavePanel::testPeakXAxisStartLabel() const
{
    return peak_plot_ ? peak_plot_->property("xAxisLeftLabel").toString() : QString();
}

QString TcpWavePanel::testPeakXAxisEndLabel() const
{
    return peak_plot_ ? peak_plot_->property("xAxisRightLabel").toString() : QString();
}

int TcpWavePanel::testWavePlotBottomMarginExtra() const
{
    return wave1_plot_ ? wave1_plot_->property("xAxisBottomMarginExtra").toInt() : 0;
}

int TcpWavePanel::testPeakPlotBottomMarginExtra() const
{
    return peak_plot_ ? peak_plot_->property("xAxisBottomMarginExtra").toInt() : 0;
}

void TcpWavePanel::testFeedSocketBytes(const QByteArray& bytes)
{
    appendIncomingSocketBytes(bytes, false);
}

void TcpWavePanel::testFeedReadyReadBytes(const QByteArray& bytes)
{
    appendIncomingSocketBytes(bytes, true);
}

void TcpWavePanel::testSetConnectionEndpoint(const QString& host, const QString& portText)
{
    if (host_edit_)
    {
        host_edit_->setText(host);
    }
    if (port_edit_)
    {
        port_edit_->setText(portText);
    }
}

qsizetype TcpWavePanel::testBufferedByteCount() const
{
    return buffer_.size();
}
#endif

void TcpWavePanel::setupSocket()
{
    socket_ = new QTcpSocket(this);
    connect(socket_, &QTcpSocket::connected, this, &TcpWavePanel::onSocketConnected);
    connect(socket_, &QTcpSocket::disconnected, this, &TcpWavePanel::onSocketDisconnected);
    connect(socket_, &QTcpSocket::readyRead, this, &TcpWavePanel::onSocketReadyRead);
    connect(socket_, &QTcpSocket::stateChanged, this, [this](QAbstractSocket::SocketState) {
        onSocketStateChanged();
    });
    connect(socket_, &QAbstractSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
        onSocketError();
    });
}

void TcpWavePanel::recreateSocket()
{
    if (socket_)
    {
        requestGracefulDisconnect();
        socket_->deleteLater();
        socket_ = nullptr;
    }
    setupSocket();
}

void TcpWavePanel::requestGracefulDisconnect()
{
    if (!socket_)
    {
        return;
    }

    if (socket_->state() == QAbstractSocket::ConnectedState)
    {
        socket_->disconnectFromHost();
        if (socket_->state() != QAbstractSocket::UnconnectedState)
        {
            socket_->waitForDisconnected(1000);
        }
    }
    else if (socket_->state() == QAbstractSocket::ConnectingState ||
             socket_->state() == QAbstractSocket::HostLookupState)
    {
        socket_->abort();
    }
}

void TcpWavePanel::onToggleConnectionClicked()
{
    if (fpga_mode_) return;
    if (ui_test_mode_)
    {
        ui_test_connected_ = !ui_test_connected_;
        setConnectedUiState(ui_test_connected_);
        setStatusText(ui_test_connected_
            ? (is_english_ ? QStringLiteral("UI test waveform source connected (simulated)")
                            : QStringLiteral("界面测试波形源已连接（模拟）"))
            : (is_english_ ? QStringLiteral("UI test waveform source disconnected")
                            : QStringLiteral("界面测试波形源已断开")));
        emit connectionStateChanged(ui_test_connected_);
        publishTcpWaveLog(VaporView::LogLevel::Info,
                          ui_test_connected_ ? QStringLiteral("tcp_wave_ui_test_connected")
                                             : QStringLiteral("tcp_wave_ui_test_disconnected"),
                          ui_test_connected_ ? QStringLiteral("界面测试 TCP 波形源已连接。")
                                             : QStringLiteral("界面测试 TCP 波形源已断开。"),
                          {{QStringLiteral("simulated"), true},
                           {QStringLiteral("ui_visibility"), QStringLiteral("details")}});
        return;
    }
    if (remote_sky_mode_)
    {
        const bool connectRequested = !remote_wave_tcp_connected_;
        publishTcpWaveLog(VaporView::LogLevel::Info,
                          connectRequested ? QStringLiteral("tcp_wave_remote_connection_requested")
                                           : QStringLiteral("tcp_wave_remote_disconnection_requested"),
                          connectRequested ? QStringLiteral("已请求连接 TCP 波形。")
                                           : QStringLiteral("已请求断开 TCP 波形。"),
                          {{QStringLiteral("execution_path"), QStringLiteral("remote_sky")},
                           {QStringLiteral("requested_connected"), connectRequested},
                           {QStringLiteral("ui_visibility"), QStringLiteral("details")}});
        emit remoteWaveTcpConnectionRequested(connectRequested);
        return;
    }

    if (socket_ && socket_->state() != QAbstractSocket::UnconnectedState)
    {
        user_disconnect_requested_ = true;
        publishTcpWaveLog(VaporView::LogLevel::Info,
                          QStringLiteral("tcp_wave_disconnection_requested"),
                          QStringLiteral("正在断开 TCP 波形连接。"),
                          {{QStringLiteral("reason_code"), QStringLiteral("USER_REQUEST")},
                           {QStringLiteral("host"), host()},
                           {QStringLiteral("port"), port()},
                           {QStringLiteral("ui_visibility"), QStringLiteral("details")}});
        requestGracefulDisconnect();
        return;
    }

    bool portOk = false;
    const int targetPort = port_edit_ ? port_edit_->text().trimmed().toInt(&portOk) : 8888;
    if (!portOk || targetPort < 1 || targetPort > 65535)
    {
        const QString message = is_english_
            ? QStringLiteral("Enter a TCP port from 1 to 65535.")
            : QStringLiteral("请输入 1 到 65535 之间的 TCP 端口。");
        publishTcpWaveLog(VaporView::LogLevel::Warning,
                          QStringLiteral("tcp_wave_connection_rejected_invalid_port"),
                          QStringLiteral("TCP 波形端口无效，无法建立连接。"),
                          {{QStringLiteral("reason_code"), QStringLiteral("INVALID_PORT")},
                           {QStringLiteral("input_value"), port_edit_ ? port_edit_->text().trimmed() : QString()},
                           {QStringLiteral("ui_visibility"), QStringLiteral("attention")}});
        setStatusText(message);
        if (port_edit_)
        {
            port_edit_->setFocus();
        }
        return;
    }

    publishTcpWaveLog(VaporView::LogLevel::Info,
                      QStringLiteral("tcp_wave_connection_started"),
                      QStringLiteral("正在连接 TCP 波形。"),
                      {{QStringLiteral("host"), host_edit_ ? host_edit_->text() : QString()},
                       {QStringLiteral("port"), targetPort},
                       {QStringLiteral("ui_visibility"), QStringLiteral("details")}});
    recreateSocket();
    buffer_.clear();
    wave1_history_.clear();
    wave4_history_.clear();
    wave1_wall_msecs_ = 0;
    wave4_wall_msecs_ = 0;
    peak_raw_history_.clear();
    peak_history_.clear();
    peak_wall_msecs_.clear();
    last_remote_feature_time_us_ = 0;
    pending_wave1_payload_.clear();
    if (peak_plot_)
    {
        peak_plot_->setPeakValues({});
    }
    resetParserState();
    parse_mode_ = ParseMode::AutoDetect;
    header_byte_order_ = HeaderByteOrder::Unknown;
    float_encoding_ = FloatEncoding::Unknown;
    frame_count_ = 0;
    last_live_decode_time_ms_ = 0;
    last_backlog_warning_ms_ = 0;
    invalid_resync_discarded_bytes_ = 0;
    process_buffer_pending_ = false;
    payload_order_auto_correct_logged_ = false;
    user_disconnect_requested_ = false;
    suppress_next_disconnected_log_ = false;
    socket_error_logged_for_current_connection_ = false;
    frame_arrival_times_ms_.clear();
    resetFrameRateDisplay();
    setStatusText(QString(is_english_ ? "Connecting to %1:%2..." : "正在连接 %1:%2...")
        .arg(host_edit_->text()).arg(targetPort));
    socket_->connectToHost(host_edit_->text(), static_cast<quint16>(targetPort));
    onSocketStateChanged();
}

void TcpWavePanel::onPeakPlotModeSelected(bool scatter)
{
    if (peak_plot_scatter_mode_ == scatter) return;
    peak_plot_scatter_mode_ = scatter;
    if (peak_plot_)
    {
        peak_plot_->setPlotMode(peak_plot_scatter_mode_ ? PeakTrendPlotWidget::PlotMode::Scatter : PeakTrendPlotWidget::PlotMode::Polyline);
    }
}

void TcpWavePanel::clearFpgaPeakTrend(int channel)
{
    auto &trend = fpga_peaks_[0x30 + channel];
    trend.values.clear();
    trend.wallMsecs.clear();
    refreshFpgaWaveform(channel);
}

void TcpWavePanel::onClearPeakPlotClicked()
{
    if (fpga_mode_) {
        clearFpgaPeakTrend(0);
        return;
    }
    peak_raw_history_.clear();
    peak_history_.clear();
    peak_wall_msecs_.clear();
    last_remote_feature_time_us_ = 0;
    if (peak_plot_)
    {
        peak_plot_->setPeakValues({});
    }
}

void TcpWavePanel::onConfigurePeakFilterClicked()
{
    QDialog dialog(this);
    dialog.setWindowTitle(is_english_ ? QStringLiteral("Peak Search Range") : QStringLiteral("峰值搜索区间"));
    VaporView::installCustomTitleBar(&dialog, false);

    QWidget *content = dialog.findChild<QWidget *>(QStringLiteral("customTitleBarContent"));
    if (!content)
    {
        content = &dialog;
    }
    auto *layout = qobject_cast<QVBoxLayout *>(content->layout());
    if (!layout)
    {
        layout = new QVBoxLayout(content);
    }
    layout->setContentsMargins(22, 18, 22, 18);
    layout->setSpacing(14);

    auto *formWidget = new QWidget(content);
    auto *formLayout = new QGridLayout(formWidget);
    formLayout->setContentsMargins(0, 0, 0, 0);
    formLayout->setHorizontalSpacing(14);
    formLayout->setVerticalSpacing(10);
    const int labelColumnWidth = is_english_ ? 104 : 86;
    const int inputColumnWidth = 240;
    auto addFormRow = [formWidget, formLayout, labelColumnWidth](int row, const QString& labelText, QWidget *editor) {
        auto *label = new QLabel(labelText, formWidget);
        label->setMinimumWidth(labelColumnWidth);
        label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        editor->setMinimumHeight(34);
        formLayout->addWidget(label, row, 0, Qt::AlignRight | Qt::AlignVCenter);
        formLayout->addWidget(editor, row, 1);
    };

    auto *searchStartSpin = new QSpinBox(formWidget);
    searchStartSpin->setRange(0, 10000000);
    searchStartSpin->setSingleStep(1000);
    searchStartSpin->setValue(peak_search_start_index_);
    searchStartSpin->setMinimumWidth(inputColumnWidth);
    addFormRow(0, is_english_ ? QStringLiteral("Search Start") : QStringLiteral("搜索起点"), searchStartSpin);

    auto *searchEndSpin = new QSpinBox(formWidget);
    searchEndSpin->setRange(0, 10000000);
    searchEndSpin->setSingleStep(1000);
    searchEndSpin->setSpecialValueText(is_english_ ? QStringLiteral("Full Frame") : QStringLiteral("整帧"));
    searchEndSpin->setValue(std::max(0, peak_search_end_index_));
    searchEndSpin->setMinimumWidth(inputColumnWidth);
    addFormRow(1, is_english_ ? QStringLiteral("Search End") : QStringLiteral("搜索终点"), searchEndSpin);

    auto *modeCombo = new QComboBox(formWidget);
    modeCombo->addItem(is_english_ ? QStringLiteral("Off") : QStringLiteral("关闭"), static_cast<int>(PeakFilterMode::None));
    modeCombo->addItem(is_english_ ? QStringLiteral("IQR Outlier Filter") : QStringLiteral("IQR 异常值过滤"), static_cast<int>(PeakFilterMode::IqrOutlier));
    modeCombo->addItem(is_english_ ? QStringLiteral("Keep Range") : QStringLiteral("保留区间"), static_cast<int>(PeakFilterMode::KeepRange));
    modeCombo->addItem(is_english_ ? QStringLiteral("Exclude Range") : QStringLiteral("排除区间"), static_cast<int>(PeakFilterMode::ExcludeRange));
    modeCombo->setCurrentIndex(std::max(0, modeCombo->findData(static_cast<int>(peak_filter_settings_.mode))));
    modeCombo->setMinimumWidth(inputColumnWidth);
    configureComboBoxPopup(modeCombo, isDarkThemeEnabled());
    addFormRow(2, is_english_ ? QStringLiteral("Trend Filter") : QStringLiteral("趋势过滤"), modeCombo);

    auto *minEdit = new QLineEdit(QString::number(peak_filter_settings_.min_value, 'f', 6), formWidget);
    auto *maxEdit = new QLineEdit(QString::number(peak_filter_settings_.max_value, 'f', 6), formWidget);
    minEdit->setMinimumWidth(inputColumnWidth);
    maxEdit->setMinimumWidth(inputColumnWidth);
    addFormRow(3, is_english_ ? QStringLiteral("Range Min") : QStringLiteral("区间最小值"), minEdit);
    addFormRow(4, is_english_ ? QStringLiteral("Range Max") : QStringLiteral("区间最大值"), maxEdit);
    formLayout->setColumnMinimumWidth(0, labelColumnWidth);
    formLayout->setColumnMinimumWidth(1, inputColumnWidth);
    formLayout->setColumnStretch(1, 1);
    layout->addWidget(formWidget);

    auto *hintLabel = new QLabel(
        is_english_
            ? QStringLiteral("Peak search uses sample indexes [start, end). Search End = Full Frame uses all remaining samples. IQR removes statistical outliers. Keep Range keeps only values inside [min, max]. Exclude Range removes values inside [min, max]. If you change the search window, the existing live trend is cleared and new frames use the updated range.")
            : QStringLiteral("峰值搜索使用采样点下标 [起点, 终点)。搜索终点为“整帧”时表示一直搜索到本帧末尾。IQR 会过滤统计异常值。保留区间只保留 [最小值, 最大值] 内的峰值。排除区间会过滤 [最小值, 最大值] 内的峰值。修改搜索窗口后，已有实时趋势会清空，后续新帧按新区间计算。"),
        content);
    hintLabel->setWordWrap(true);
    hintLabel->setMinimumWidth(labelColumnWidth + inputColumnWidth + formLayout->horizontalSpacing());
    hintLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    layout->addWidget(hintLabel);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, content);
    if (QPushButton *okButton = buttons->button(QDialogButtonBox::Ok))
    {
        okButton->setText(is_english_ ? QStringLiteral("OK") : QStringLiteral("确定"));
    }
    if (QPushButton *cancelButton = buttons->button(QDialogButtonBox::Cancel))
    {
        cancelButton->setText(is_english_ ? QStringLiteral("Cancel") : QStringLiteral("取消"));
    }
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    dialog.setMinimumSize(is_english_ ? QSize(580, 460) : QSize(560, 450));
    dialog.resize(dialog.minimumSize());
    if (dialog.exec() != QDialog::Accepted)
    {
        return;
    }

    bool minOk = false;
    bool maxOk = false;
    const double minValue = minEdit->text().trimmed().toDouble(&minOk);
    const double maxValue = maxEdit->text().trimmed().toDouble(&maxOk);
    const int searchStart = searchStartSpin->value();
    const int searchEnd = searchEndSpin->value();
    const PeakFilterMode mode = static_cast<PeakFilterMode>(modeCombo->currentData().toInt());
    if (searchEnd > 0 && searchEnd <= searchStart)
    {
        QMessageBox::warning(
            this,
            is_english_ ? QStringLiteral("Peak Search Range") : QStringLiteral("峰值搜索区间"),
            is_english_ ? QStringLiteral("Search End must be greater than Search Start, or set to Full Frame.") : QStringLiteral("搜索终点必须大于搜索起点，或者设置为整帧。"));
        return;
    }
    if ((mode == PeakFilterMode::KeepRange || mode == PeakFilterMode::ExcludeRange) && (!minOk || !maxOk))
    {
        QMessageBox::warning(
            this,
            is_english_ ? QStringLiteral("Trend Filter") : QStringLiteral("趋势过滤"),
            is_english_ ? QStringLiteral("Please enter valid numeric range values.") : QStringLiteral("请输入有效的数值区间。"));
        return;
    }

    const bool peakSearchChanged =
        peak_search_start_index_ != searchStart ||
        peak_search_end_index_ != searchEnd;
    peak_filter_settings_.mode = mode;
    if (minOk)
    {
        peak_filter_settings_.min_value = minValue;
    }
    if (maxOk)
    {
        peak_filter_settings_.max_value = maxValue;
    }

    if (peakSearchChanged)
    {
        if (remote_sky_mode_)
        {
            saveRememberedInputState();
            updatePeakFilterButtonText();
            emit remotePeakSearchRangeRequested(static_cast<quint32>(searchStart), static_cast<quint32>(searchEnd));
            setStatusText(is_english_
                ? QStringLiteral("Peak search range sent; waiting for ACK.")
                : QStringLiteral("峰值搜索区间已发送，等待 ACK。"));
        }
        else
        {
            peak_search_start_index_ = searchStart;
            peak_search_end_index_ = searchEnd;
            peak_raw_history_.clear();
            peak_history_.clear();
            peak_wall_msecs_.clear();
            last_remote_feature_time_us_ = 0;
            if (peak_plot_)
            {
                peak_plot_->setPeakValues({});
            }
            saveRememberedInputState();
            updatePeakFilterButtonText();
            setStatusText(is_english_
                ? QStringLiteral("Peak search range updated. Existing live peak trend was cleared; new frames will use the new range.")
                : QStringLiteral("峰值搜索区间已更新，现有实时峰值趋势已清空；后续新帧将按新区间计算。"));
        }
    }
    else
    {
        saveRememberedInputState();
        updatePeakFilterButtonText();
        rebuildPeakHistory();
    }
}

void TcpWavePanel::onSocketConnected()
{
    setConnectedUiState(true);
    emit connectionStateChanged(true);
    publishTcpWaveLog(VaporView::LogLevel::Info,
                      QStringLiteral("tcp_wave_connected"),
                      QStringLiteral("TCP 波形已连接。"),
                      {{QStringLiteral("host"), host_edit_ ? host_edit_->text() : QString()},
                       {QStringLiteral("port"), port()},
                       {QStringLiteral("ui_visibility"), QStringLiteral("details")}});
    setStatusText(QString(is_english_
        ? "Connected to %1:%2, waiting for the first frame..."
        : "已连接到 %1:%2，正在等待首帧数据...")
        .arg(host_edit_->text()).arg(port()));
}

void TcpWavePanel::onSocketDisconnected()
{
    const bool suppressLog = suppress_next_disconnected_log_ || socket_error_logged_for_current_connection_;
    const QString reason = socket_ && socket_->error() != QAbstractSocket::UnknownSocketError
        ? socket_->errorString()
        : (is_english_ ? QStringLiteral("connection closed") : QStringLiteral("连接已关闭"));
    const bool userRequested = user_disconnect_requested_;
    setConnectedUiState(false);
    emit connectionStateChanged(false);
    if (!suppressLog)
    {
        publishTcpWaveLog(userRequested ? VaporView::LogLevel::Info : VaporView::LogLevel::Warning,
                          userRequested ? QStringLiteral("tcp_wave_disconnected")
                                        : QStringLiteral("tcp_wave_disconnected_unexpectedly"),
                          userRequested ? QStringLiteral("TCP 波形已断开。")
                                        : QStringLiteral("TCP 波形连接非预期断开。"),
                          {{QStringLiteral("reason_code"), userRequested
                               ? QStringLiteral("USER_REQUEST")
                               : QStringLiteral("REMOTE_DISCONNECTED")},
                           {QStringLiteral("system_error"), reason},
                           {QStringLiteral("received_frames"), static_cast<qlonglong>(frame_count_)},
                           {QStringLiteral("buffered_bytes"), static_cast<qlonglong>(buffer_.size())},
                           {QStringLiteral("expected_payload_bytes"), expected_payload_size_},
                           {QStringLiteral("host"), host_edit_ ? host_edit_->text() : QString()},
                           {QStringLiteral("port"), port()},
                           {QStringLiteral("ui_visibility"), userRequested
                               ? QStringLiteral("details")
                               : QStringLiteral("attention")}});
    }
    user_disconnect_requested_ = false;
    suppress_next_disconnected_log_ = false;
    socket_error_logged_for_current_connection_ = false;
    if (frame_count_ > 0)
    {
        setStatusText(QString(is_english_
            ? "Disconnected after receiving %1 frames"
            : "已断开，本次共接收 %1 帧").arg(frame_count_));
    }
    else
    {
        setStatusText(is_english_ ? "Disconnected without receiving any frame" : "已断开，本次未收到任何数据帧");
    }
    frame_arrival_times_ms_.clear();
    resetFrameRateDisplay();
}

void TcpWavePanel::onSocketReadyRead()
{
    if (!socket_)
    {
        return;
    }

    appendIncomingSocketBytes(socket_->readAll(), true);
}

void TcpWavePanel::appendIncomingSocketBytes(const QByteArray& bytes, bool publishBacklogWarning)
{
    buffer_.append(bytes);
    if (enforceTcpBufferBacklogLimit())
    {
        return;
    }
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (publishBacklogWarning &&
        buffer_.size() >= kTcpBufferBacklogWarningBytes &&
        (last_backlog_warning_ms_ <= 0 ||
         nowMs - last_backlog_warning_ms_ >= kTcpBufferBacklogWarningIntervalMs))
    {
        last_backlog_warning_ms_ = nowMs;
        publishTcpWaveLog(VaporView::LogLevel::Warning,
                          QStringLiteral("tcp_wave_receive_backlog"),
                          QStringLiteral("TCP 波形接收缓冲出现积压，界面处理可能慢于数据流。"),
                          {{QStringLiteral("buffered_bytes"), static_cast<qlonglong>(buffer_.size())},
                           {QStringLiteral("backlog_threshold"), kTcpBufferBacklogWarningBytes},
                           {QStringLiteral("ui_dedupe_key"), QStringLiteral("tcp_wave:receive_backlog")},
                           {QStringLiteral("ui_visibility"), QStringLiteral("attention")}});
    }
    if (!process_buffer_pending_)
    {
        processBuffer();
    }
}

void TcpWavePanel::onSocketStateChanged()
{
    if (!socket_)
    {
        setConnectedUiState(false);
        return;
    }

    if (remote_sky_mode_)
    {
        updateEndpointEditorState();
        return;
    }

    switch (socket_->state())
    {
    case QAbstractSocket::HostLookupState:
    case QAbstractSocket::ConnectingState:
        host_edit_->setEnabled(false);
        port_edit_->setEnabled(false);
        connect_button_->setEnabled(false);
        connect_button_->setText(is_english_ ? "Connecting..." : "连接中...");
        setStatusText(QString(is_english_ ? "Connecting to %1:%2..." : "正在连接 %1:%2...")
            .arg(host_edit_->text()).arg(port()));
        break;
    case QAbstractSocket::ConnectedState:
        setConnectedUiState(true);
        break;
    case QAbstractSocket::ClosingState:
        setConnectedUiState(true);
        setStatusText(is_english_ ? "Disconnecting..." : "正在断开...");
        break;
    case QAbstractSocket::UnconnectedState:
    default:
        setConnectedUiState(false);
        break;
    }
}

void TcpWavePanel::onSocketError()
{
    if (!socket_)
    {
        return;
    }
    const QString errorText = socket_->errorString();
    const auto socketError = socket_->error();
    setStatusText(errorText);
    if (socketError == QAbstractSocket::RemoteHostClosedError)
    {
        return;
    }
    socket_error_logged_for_current_connection_ = true;
    publishTcpWaveLog(VaporView::LogLevel::Error,
                      QStringLiteral("tcp_wave_socket_error"),
                      QStringLiteral("TCP 波形 socket 发生错误。"),
                      {{QStringLiteral("error_code"), QStringLiteral("SOCKET_ERROR")},
                       {QStringLiteral("socket_error_code"), static_cast<int>(socketError)},
                       {QStringLiteral("system_error"), errorText},
                       {QStringLiteral("host"), host_edit_ ? host_edit_->text() : QString()},
                       {QStringLiteral("port"), port()},
                       {QStringLiteral("received_frames"), static_cast<qlonglong>(frame_count_)},
                       {QStringLiteral("buffered_bytes"), static_cast<qlonglong>(buffer_.size())},
                       {QStringLiteral("ui_visibility"), QStringLiteral("attention")},
                       {QStringLiteral("ui_dedupe_key"), QStringLiteral("tcp_wave:socket_error:%1")
                            .arg(static_cast<int>(socketError))}});
    if (socket_->state() == QAbstractSocket::UnconnectedState)
    {
        setConnectedUiState(false);
        emit connectionStateChanged(false);
    }
}

void TcpWavePanel::setConnectedUiState(bool connected)
{
    if (remote_sky_mode_)
    {
        updateEndpointEditorState();
        if (connect_button_)
        {
            connect_button_->setEnabled(true);
            connect_button_->setText(remote_wave_tcp_connected_
                ? (is_english_ ? "Disconnect" : "断开")
                : (is_english_ ? "Connect" : "连接"));
        }
        return;
    }

    const bool active = connected && socket_ && socket_->state() != QAbstractSocket::UnconnectedState;
    updateEndpointEditorState();
    if (socket_ && socket_->state() == QAbstractSocket::ClosingState)
    {
        connect_button_->setText(is_english_ ? "Disconnecting..." : "正在断开...");
        connect_button_->setEnabled(false);
        return;
    }

    connect_button_->setEnabled(true);
    connect_button_->setText(active ? (is_english_ ? "Disconnect" : "断开") : (is_english_ ? "Connect" : "连接"));
}

void TcpWavePanel::updateEndpointEditorState()
{
    const bool localActive =
        !remote_sky_mode_ &&
        socket_ &&
        socket_->state() != QAbstractSocket::UnconnectedState;
    const bool endpointEditable = !fpga_mode_ && (remote_sky_mode_
        ? !remote_wave_tcp_connected_
        : !localActive);
    if (host_edit_)
    {
        host_edit_->setEnabled(endpointEditable);
    }
    if (port_edit_)
    {
        port_edit_->setEnabled(endpointEditable);
    }
}

void TcpWavePanel::setStatusText(const QString& text)
{
    if (status_label_ && status_label_->text() != text)
    {
        status_label_->setTextFormat(Qt::PlainText);
        status_label_->setText(text);
    }
    if (status_label_)
    {
        status_label_->setVisible(false);
    }
}

void TcpWavePanel::clearRemoteWaveformDisplay(const QString& statusText)
{
    wave1_history_.clear();
    wave4_history_.clear();
    wave1_wall_msecs_ = 0;
    wave4_wall_msecs_ = 0;
    peak_raw_history_.clear();
    peak_history_.clear();
    peak_wall_msecs_.clear();
    last_remote_feature_time_us_ = 0;
    frame_arrival_times_ms_.clear();
    frame_count_ = 0;
    pending_wave1_payload_.clear();
    pending_wave1_info_text_.clear();
    pending_wave4_info_text_.clear();
    pending_live_status_text_.clear();
    remote_waveform_status_text_.clear();
    remote_feature_status_text_.clear();
    last_live_decode_time_ms_ = 0;
    last_frame_rate_label_update_ms_ = 0;
    if (wave1_plot_) wave1_plot_->setSamples({});
    fpga_h2_history_.clear();
    if (fpga_h2_plot_) fpga_h2_plot_->setSamples({});
    if (wave4_plot_) wave4_plot_->setSamples({});
    for (auto *plot : home_harmonics_[1].plots) plot->setSamples({});
    second_peak_plot_->setPeakValues({});
    if (peak_plot_) peak_plot_->setPeakValues({});
    if (wave1_info_label_) wave1_info_label_->setText(QStringLiteral("--"));
    if (fpga_h2_info_label_) fpga_h2_info_label_->setText(QStringLiteral("--"));
    if (wave4_info_label_) wave4_info_label_->setText(QStringLiteral("--"));
    resetFrameRateDisplay();
    if (!statusText.isEmpty())
    {
        setStatusText(statusText);
    }
    live_display_dirty_ = false;
}

void TcpWavePanel::resetParserState()
{
    read_state_ = ReadState::Wave1Header;
    expected_payload_size_ = 0;
}

void TcpWavePanel::publishTcpWaveLog(VaporView::LogLevel level,
                                     const QString& event,
                                     const QString& message,
                                     QVariantMap fields, const char *file, const char *function, int line) const
{
    fields.insert(QStringLiteral("event"), event);
    fields.insert(QStringLiteral("ui_visible"), true);
    if (!fields.contains(QStringLiteral("ui_visibility")))
    {
        fields.insert(QStringLiteral("ui_visibility"),
                      level >= VaporView::LogLevel::Warning ? QStringLiteral("attention")
                                                            : QStringLiteral("details"));
    }

    VaporView::LogRecord record;
    record.level = level;
    record.source = QStringLiteral("Ground");
    record.category = QStringLiteral("telemetry.wave.tcp");
    record.message = message;
    record.fields = std::move(fields);
    VaporView::LogService::addCallLocation(record, file, function, line);
    if (!VaporView::LogService::withCurrentInstance([&](VaporView::LogService& logService) {
            logService.publish(record);
        }))
    {
        VaporView::LogService::writeLogFallback(record);
    }
}

void TcpWavePanel::scheduleDeferredProcessBuffer()
{
    if (process_buffer_pending_)
    {
        return;
    }

    process_buffer_pending_ = true;
    QTimer::singleShot(0, this, [this]() {
        processBuffer();
    });
}

bool TcpWavePanel::enforceTcpBufferBacklogLimit()
{
    if (buffer_.size() <= kTcpBufferMaxBacklogBytes)
    {
        return false;
    }

    disconnectInvalidTcpStream(QString(is_english_
        ? "TCP wave receive backlog exceeded %1 bytes; disconnecting from invalid or unsupported stream."
        : "TCP 波形接收缓冲超过 %1 字节，已断开异常或不支持的数据流。")
        .arg(static_cast<qlonglong>(kTcpBufferMaxBacklogBytes)));
    return true;
}

bool TcpWavePanel::discardInvalidTcpPrefix(qsizetype bytesToDrop)
{
    if (bytesToDrop <= 0)
    {
        return false;
    }

    buffer_.remove(0, bytesToDrop);
    invalid_resync_discarded_bytes_ += bytesToDrop;
    publishTcpWaveLog(VaporView::LogLevel::Debug,
                      QStringLiteral("tcp_wave_resynchronized"),
                      QStringLiteral("TCP 波形重同步已丢弃字节以查找有效帧边界。"),
                      {{QStringLiteral("discarded_bytes"), static_cast<qlonglong>(bytesToDrop)},
                       {QStringLiteral("total_discarded_bytes"), static_cast<qlonglong>(invalid_resync_discarded_bytes_)},
                       {QStringLiteral("buffered_bytes"), static_cast<qlonglong>(buffer_.size())},
                       {QStringLiteral("ui_visibility"), QStringLiteral("hidden")},
                       {QStringLiteral("ui_dedupe_key"), QStringLiteral("tcp_wave:resynchronized")}});

    if (invalid_resync_discarded_bytes_ >= kTcpInvalidResyncDisconnectBytes)
    {
        disconnectInvalidTcpStream(QString(is_english_
            ? "TCP wave stream did not produce a valid frame boundary after discarding %1 bytes; disconnecting."
            : "TCP 波形数据流已丢弃 %1 字节仍未找到有效帧边界，已断开连接。")
            .arg(static_cast<qlonglong>(invalid_resync_discarded_bytes_)));
        return true;
    }
    return false;
}

void TcpWavePanel::disconnectInvalidTcpStream(const QString& statusText)
{
    const qsizetype bufferedBytes = buffer_.size();
    const qsizetype discardedBytes = invalid_resync_discarded_bytes_;
    setStatusText(statusText);
    publishTcpWaveLog(VaporView::LogLevel::Error,
                      QStringLiteral("tcp_wave_invalid_stream_disconnected"),
                      QStringLiteral("TCP 波形数据流无效，已主动断开连接。"),
                      {{QStringLiteral("error_code"), QStringLiteral("INVALID_WAVE_STREAM")},
                       {QStringLiteral("buffered_bytes"), static_cast<qlonglong>(bufferedBytes)},
                       {QStringLiteral("discarded_bytes"), static_cast<qlonglong>(discardedBytes)},
                       {QStringLiteral("backlog_threshold"), kTcpBufferMaxBacklogBytes},
                       {QStringLiteral("ui_visibility"), QStringLiteral("attention")},
                       {QStringLiteral("ui_dedupe_key"), QStringLiteral("tcp_wave:invalid_stream_disconnected")}});
    buffer_.clear();
    pending_wave1_payload_.clear();
    resetParserState();
    parse_mode_ = ParseMode::AutoDetect;
    header_byte_order_ = HeaderByteOrder::Unknown;
    invalid_resync_discarded_bytes_ = 0;
    process_buffer_pending_ = false;
    suppress_next_disconnected_log_ = true;

    if (socket_ && socket_->state() != QAbstractSocket::UnconnectedState)
    {
        socket_->disconnectFromHost();
    }
}

void TcpWavePanel::processBuffer()
{
    process_buffer_pending_ = false;
    const qint64 passStartMs = QDateTime::currentMSecsSinceEpoch();
    int completedFramesThisPass = 0;

    while (true)
    {
        switch (read_state_)
        {
        case ReadState::Wave1Header:
            if (!tryConsumeHeader())
            {
                return;
            }
            read_state_ = ReadState::Wave1Payload;
            break;
        case ReadState::Wave1Payload:
            if (!tryConsumePayload(pending_wave1_payload_))
            {
                return;
            }
            read_state_ = ReadState::Wave4Header;
            break;
        case ReadState::Wave4Header:
            if (!tryConsumeHeader())
            {
                return;
            }
            read_state_ = ReadState::Wave4Payload;
            break;
        case ReadState::Wave4Payload:
        {
            QByteArray wave4Payload;
            if (!tryConsumePayload(wave4Payload))
            {
                return;
            }

            const qint64 arrivalTimeMs = QDateTime::currentMSecsSinceEpoch();
            const quint64 frameTimestampUs =
                static_cast<quint64>(QDateTime::currentDateTimeUtc().toMSecsSinceEpoch()) * 1000ULL;
            ++frame_count_;
            updateFrameRateDisplay(arrivalTimeMs);

            QByteArray rawSignalPayload = pending_wave1_payload_;
            QByteArray harmonicPayload = wave4Payload;
            const VaporView::TcpWavePayloadOrderAnalysis orderAnalysis =
                VaporView::analyzeTcpWavePayloadOrder(rawSignalPayload, harmonicPayload, float_encoding_);
            if (orderAnalysis.order == VaporView::TcpWavePayloadOrder::HarmonicThenRaw)
            {
                std::swap(rawSignalPayload, harmonicPayload);
                if (!payload_order_auto_correct_logged_)
                {
                    payload_order_auto_correct_logged_ = true;
                    publishTcpWaveLog(VaporView::LogLevel::Info,
                                      QStringLiteral("tcp_wave_payload_order_corrected"),
                                      QStringLiteral("已自动校正反向 TCP 波形负载顺序。"),
                                      {{QStringLiteral("confidence"), orderAnalysis.confidence},
                                       {QStringLiteral("ui_visibility"), QStringLiteral("details")}});
                }
            }
            emit rawWaveFrameReady(frameTimestampUs, rawSignalPayload, harmonicPayload, float_encoding_);

            const bool updateLiveFrame =
                last_live_decode_time_ms_ <= 0 ||
                arrivalTimeMs - last_live_decode_time_ms_ >= kLiveDisplayRefreshMs;
            if (updateLiveFrame)
            {
                last_live_decode_time_ms_ = arrivalTimeMs;
                QVector<float> wave1 = decodeFloatPayload(rawSignalPayload);
                QVector<float> wave4 = decodeFloatPayload(harmonicPayload);

                double wave1MaxMagnitude = 0.0;
                double wave4MaxMagnitude = 0.0;
                if (!isReasonableWavePayload(wave1, kMaxReasonableWaveMagnitude, &wave1MaxMagnitude) ||
                    !isReasonableWavePayload(wave4, kMaxReasonableWaveMagnitude, &wave4MaxMagnitude))
                {
                    setStatusText(QString(is_english_
                        ? "Dropped abnormal TCP display frame, raw max=%1, harmonic max=%2. Raw recording continues..."
                        : "已跳过异常 TCP 显示帧，原始信号最大值=%1，二次谐波最大值=%2，原始记录继续...")
                        .arg(formatWaveValue(wave1MaxMagnitude, 3))
                        .arg(formatWaveValue(wave4MaxMagnitude, 3)));
                    float_encoding_ = FloatEncoding::Unknown;
                }
                else
                {
                    wave1_history_ = std::move(wave1);
                    wave4_history_ = std::move(wave4);
                    wave1_wall_msecs_ = wallMsecsFromTimestampUs(frameTimestampUs);
                    wave4_wall_msecs_ = wave1_wall_msecs_;
                    const float rawPeakValue = currentWaveformPeakValue(wave4_history_);
                    peak_raw_history_.push_back(rawPeakValue);
                    peak_wall_msecs_.push_back(wave4_wall_msecs_);
                    if (peak_raw_history_.size() > kPeakTrendFrameWindow)
                    {
                        const int removeCount = peak_raw_history_.size() - kPeakTrendFrameWindow;
                        peak_raw_history_.remove(0, removeCount);
                        peak_wall_msecs_.remove(0, std::min<qsizetype>(removeCount, peak_wall_msecs_.size()));
                    }
                    rebuildPeakHistory();
                    const float displayedPeakValue = peak_history_.isEmpty()
                        ? rawPeakValue
                        : peak_history_.back();
                    emit normalizedSecondHarmonicFrameReady(
                        frameTimestampUs,
                        wave4_history_);

                    const auto describeRange = [](const QVector<float>& values) {
                        if (values.isEmpty())
                        {
                            return QStringLiteral("min=%1 max=%2")
                                .arg(fixedNumericStatusField(QStringLiteral("0.000000"), 14),
                                     fixedNumericStatusField(QStringLiteral("0.000000"), 14));
                        }
                        const auto [minIt, maxIt] = std::minmax_element(values.cbegin(), values.cend());
                        return QString("min=%1 max=%2")
                            .arg(fixedNumericStatusField(formatWaveValue(*minIt, 6), 14),
                                 fixedNumericStatusField(formatWaveValue(*maxIt, 6), 14));
                    };

                    const QString wave1SampleCount = fixedStatusInteger(wave1_history_.size(), kRemoteStatusCountWidth);
                    const QString wave4SampleCount = fixedStatusInteger(wave4_history_.size(), kRemoteStatusCountWidth);
                    pending_wave1_info_text_ = QString(is_english_
                        ? "%1 samples  %2"
                        : "%1 个采样点  %2")
                        .arg(wave1SampleCount)
                        .arg(describeRange(wave1_history_));
                    pending_wave4_info_text_ = QString(is_english_
                        ? "%1 samples  %2  %3"
                        : "%1 个采样点  %2  %3")
                        .arg(wave4SampleCount)
                        .arg(describeRange(wave4_history_))
                        .arg([this, rawPeakValue, displayedPeakValue]() {
                            const QString rawPeakText = std::isfinite(rawPeakValue)
                                ? fixedNumericStatusField(formatWaveValue(rawPeakValue, 6), 14)
                                : fixedNumericStatusField(QStringLiteral("--"), 14);
                            const QString displayedPeakText = std::isfinite(displayedPeakValue)
                                ? fixedNumericStatusField(formatWaveValue(displayedPeakValue, 6), 14)
                                : fixedStatusField(is_english_ ? QStringLiteral("filtered") : QStringLiteral("已过滤"), 14);
                            if (peak_filter_settings_.mode == PeakFilterMode::None ||
                                (!std::isfinite(rawPeakValue) && !std::isfinite(displayedPeakValue)) ||
                                (std::isfinite(rawPeakValue) && std::isfinite(displayedPeakValue) &&
                                 std::fabs(static_cast<double>(rawPeakValue) - static_cast<double>(displayedPeakValue)) < 1e-9))
                            {
                                return QString(is_english_ ? "peak=%1" : "峰值=%1").arg(rawPeakText);
                            }
                            return QString(is_english_ ? "peak(raw/show)=%1/%2" : "峰值(原始/显示)=%1/%2")
                                .arg(rawPeakText, displayedPeakText);
                        }());

                    pending_live_status_text_ = QString(is_english_
                        ? "Receiving frame %3 from %1:%2, float format: %4"
                        : "正在接收来自 %1:%2 的数据帧，第 %3 帧，浮点格式: %4")
                        .arg(host_edit_->text())
                        .arg(port())
                        .arg(fixedStatusInteger(frame_count_, 8))
                        .arg(VaporView::tcpFloatEncodingLabel(is_english_, float_encoding_));
                    live_display_dirty_ = true;
                }
            }

            pending_wave1_payload_.clear();
            resetParserState();
            ++completedFramesThisPass;
            if (completedFramesThisPass >= kProcessBufferMaxFramesPerPass ||
                QDateTime::currentMSecsSinceEpoch() - passStartMs >= kProcessBufferBudgetMs)
            {
                if (!buffer_.isEmpty())
                {
                    scheduleDeferredProcessBuffer();
                }
                return;
            }
            break;
        }
        }
    }
}

bool TcpWavePanel::trySynchronizeLengthPrefixedStream()
{
    if (buffer_.size() < kHeaderSize)
    {
        return false;
    }

    if (header_byte_order_ != HeaderByteOrder::Unknown)
    {
        const qint32 candidate = decodeHeaderValue(buffer_.constData(), header_byte_order_);
        if (isValidPayloadSize(candidate))
        {
            invalid_resync_discarded_bytes_ = 0;
            return true;
        }
    }

    const HeaderByteOrder orders[] = {
        HeaderByteOrder::LittleEndian,
        HeaderByteOrder::BigEndian,
    };
    struct BoundaryCandidate
    {
        int offset = 0;
        HeaderByteOrder order = HeaderByteOrder::Unknown;
        double score = -std::numeric_limits<double>::infinity();
    };
    BoundaryCandidate bestCandidate;
    bool hasBestCandidate = false;
    for (int offset = 0; offset <= buffer_.size() - kHeaderSize; ++offset)
    {
        for (HeaderByteOrder order : orders)
        {
            const qint32 firstPayloadSize = decodeHeaderValue(buffer_.constData() + offset, order);
            if (!isValidPayloadSize(firstPayloadSize))
            {
                continue;
            }

            const bool preferredSize = firstPayloadSize == kPreferredPayloadBytes;
            const int secondHeaderOffset = offset + kHeaderSize + firstPayloadSize;
            const bool canValidateSecondHeader = secondHeaderOffset + kHeaderSize <= buffer_.size();
            if (!canValidateSecondHeader)
            {
                if (offset != 0 && !preferredSize)
                {
                    continue;
                }
                double score = offset == 0 ? 40.0 : 0.0;
                if (preferredSize)
                {
                    score += 10.0;
                }
                if (!hasBestCandidate || score > bestCandidate.score)
                {
                    bestCandidate = {offset, order, score};
                    hasBestCandidate = true;
                }
                continue;
            }

            const qint32 secondPayloadSize = canValidateSecondHeader
                ? decodeHeaderValue(buffer_.constData() + secondHeaderOffset, order)
                : 0;
            const bool secondHeaderValid = isValidPayloadSize(secondPayloadSize);
            if (!preferredSize && !secondHeaderValid)
            {
                continue;
            }

            double score = offset == 0 ? 50.0 : 0.0;
            if (preferredSize)
            {
                score += 10.0;
            }
            if (secondHeaderValid)
            {
                score += 20.0;
                const int firstPayloadOffset = offset + kHeaderSize;
                const int secondPayloadOffset = secondHeaderOffset + kHeaderSize;
                if (secondPayloadOffset + secondPayloadSize <= buffer_.size())
                {
                    const VaporView::TcpWavePayloadOrderAnalysis orderAnalysis =
                        VaporView::analyzeTcpWavePayloadOrder(
                            buffer_.mid(firstPayloadOffset, firstPayloadSize),
                            buffer_.mid(secondPayloadOffset, secondPayloadSize),
                            float_encoding_);
                    if (orderAnalysis.order == VaporView::TcpWavePayloadOrder::RawThenHarmonic)
                    {
                        score += 100.0 + std::min(orderAnalysis.confidence, 50.0);
                    }
                    else if (orderAnalysis.order == VaporView::TcpWavePayloadOrder::HarmonicThenRaw)
                    {
                        score += 100.0 + std::min(orderAnalysis.confidence, 50.0);
                    }
                }
            }

            if (!hasBestCandidate || score > bestCandidate.score)
            {
                bestCandidate = {offset, order, score};
                hasBestCandidate = true;
            }
        }
    }

    if (hasBestCandidate)
    {
        if (bestCandidate.offset > 0)
        {
            setStatusText(QString(is_english_
                ? "Recovered TCP frame boundary after skipping %1 bytes, header order: %2"
                : "已跳过 %1 字节并重新找到TCP帧边界，帧头字节序: %2")
                .arg(bestCandidate.offset)
                .arg(headerOrderLabel(is_english_, bestCandidate.order)));
            buffer_.remove(0, bestCandidate.offset);
        }
        else if (parse_mode_ == ParseMode::AutoDetect)
        {
            setStatusText(QString(is_english_
                ? "Detected length-prefixed TCP payloads, header order: %1"
                : "已识别为长度前缀TCP负载格式，帧头字节序: %1")
                .arg(headerOrderLabel(is_english_, bestCandidate.order)));
        }

        header_byte_order_ = bestCandidate.order;
        parse_mode_ = ParseMode::LengthPrefixed;
        invalid_resync_discarded_bytes_ = 0;
        return true;
    }

    const qint32 little = decodeHeaderValue(buffer_.constData(), HeaderByteOrder::LittleEndian);
    const qint32 big = decodeHeaderValue(buffer_.constData(), HeaderByteOrder::BigEndian);
    setStatusText(QString(is_english_
        ? "Waiting for a valid TCP frame boundary, bytes: %1, little=%2, big=%3"
        : "正在等待有效的TCP帧边界，字节预览：%1，小端=%2，大端=%3")
        .arg(hexPreview(buffer_))
        .arg(little)
        .arg(big));
    const qsizetype bytesToDrop = std::max<qsizetype>(0, buffer_.size() - kTcpBufferResyncKeepBytes);
    discardInvalidTcpPrefix(bytesToDrop);
    return false;
}

bool TcpWavePanel::isValidPayloadSize(qint32 candidate) const
{
    return candidate > 0 && candidate <= kMaxPayloadBytes && (candidate % kFloatSize) == 0;
}

qint32 TcpWavePanel::decodeHeaderValue(const char *raw, HeaderByteOrder order) const
{
    const uchar *bytes = reinterpret_cast<const uchar*>(raw);
    switch (order)
    {
    case HeaderByteOrder::LittleEndian:
        return qFromLittleEndian<qint32>(bytes);
    case HeaderByteOrder::BigEndian:
        return qFromBigEndian<qint32>(bytes);
    case HeaderByteOrder::Unknown:
    default:
        return qFromLittleEndian<qint32>(bytes);
    }
}

bool TcpWavePanel::tryConsumeHeader()
{
    if (!trySynchronizeLengthPrefixedStream())
    {
        return false;
    }

    const qint32 candidate = decodeHeaderValue(buffer_.constData(), header_byte_order_);
    if (!isValidPayloadSize(candidate))
    {
        setStatusText(QString(is_english_
            ? "Unexpected TCP frame header after resync (%1), bytes: %2"
            : "重同步后TCP帧头仍异常（%1），字节预览：%2")
            .arg(candidate)
            .arg(hexPreview(buffer_)));
        return false;
    }

    expected_payload_size_ = candidate;
    buffer_.remove(0, kHeaderSize);
    return true;
}

bool TcpWavePanel::tryConsumePayload(QByteArray& rawPayload)
{
    if (buffer_.size() < expected_payload_size_)
    {
        return false;
    }

    rawPayload = buffer_.left(expected_payload_size_);
    buffer_.remove(0, expected_payload_size_);
    if (float_encoding_ == FloatEncoding::Unknown)
    {
        float_encoding_ = VaporView::autoDetectTcpFloatEncoding(rawPayload);
        setStatusText(QString(is_english_
            ? "Detected float payload format: %1"
            : "已识别浮点负载格式: %1")
            .arg(VaporView::tcpFloatEncodingLabel(is_english_, float_encoding_)));
    }
    expected_payload_size_ = 0;
    return true;
}

QVector<float> TcpWavePanel::decodeFloatPayload(const QByteArray& payload) const
{
    const FloatEncoding encoding = float_encoding_ == FloatEncoding::Unknown
        ? FloatEncoding::LittleEndian
        : float_encoding_;
    return VaporView::decodeTcpFloatPayload(payload, encoding);
}
