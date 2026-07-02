#include "CustomTitleBar.h"
#include "AppTheme.h"
#include "TcpWavePanel.h"
#include "VisualTextLabel.h"
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
#include <QSettings>
#include <QSignalBlocker>
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
#include <cmath>
#include <functional>
#include <limits>

using VaporView::AppThemeColor;
using VaporView::appThemeColor;

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
constexpr int kWavePlotLeftMargin = 72;
constexpr int kPeakPlotLeftMargin = 72;
constexpr int kDefaultPeakSearchStartIndex = 0;
constexpr int kDefaultPeakSearchEndIndex = 0;
constexpr int kPeakTrendFrameWindow = 1000;
constexpr int kLiveDisplayRefreshMs = 20;
constexpr int kFrameRateLabelRefreshMs = 250;
constexpr int kProcessBufferMaxFramesPerPass = 32;
constexpr qint64 kProcessBufferBudgetMs = 8;
constexpr int kTcpBufferBacklogWarningBytes = 4 * 1024 * 1024;
constexpr qint64 kTcpBufferBacklogWarningIntervalMs = 5000;
constexpr qint64 kFrameRateWindowMs = 5000;
constexpr double kMaxReasonableWaveMagnitude = 1.0e6;
constexpr int kRemoteStatusCountWidth = 6;
constexpr int kRemoteStatusPeakWidth = 12;
constexpr int kRemoteStatusRmsWidth = 8;
constexpr int kRemoteStatusRangeWidth = 15;
constexpr int kRemoteStatusIndexWidth = 6;
constexpr const char *kTooltipAnchorRectProperty = "_vv_tooltip_anchor_rect";

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
        .arg(english ? QStringLiteral("Remote pts:") : QStringLiteral("远程点:"),
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
    return createLucideIcon(QStringLiteral("sliders-vertical"), color);
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

    titleLabel->setParent(cluster);
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

class WaveDisplayMenuRow final : public QWidget
{
public:
    explicit WaveDisplayMenuRow(QWidget *parent = nullptr)
        : QWidget(parent)
        , text_label_(new QLabel(this))
        , check_label_(new QLabel(this))
    {
        setAttribute(Qt::WA_Hover, true);
        setCursor(Qt::PointingHandCursor);
        setFixedHeight(34);
        setMinimumWidth(238);

        auto *layout = new QHBoxLayout(this);
        layout->setContentsMargins(12, 0, 10, 0);
        layout->setSpacing(16);
        text_label_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        text_label_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        layout->addWidget(text_label_, 1);
        check_label_->setFixedSize(18, 18);
        check_label_->setAlignment(Qt::AlignCenter);
        layout->addWidget(check_label_, 0, Qt::AlignVCenter | Qt::AlignRight);
        refreshTheme();
    }

    void setText(const QString& text)
    {
        text_label_->setText(text);
    }

    void setCheckIcon(const QIcon& icon)
    {
        check_icon_ = icon;
        updateCheckIcon();
    }

    void setChecked(bool checked)
    {
        checked_ = checked;
        updateCheckIcon();
    }

    void setClickedCallback(std::function<void()> callback)
    {
        clicked_ = std::move(callback);
    }

    void refreshTheme()
    {
        const bool dark = VaporView::isDarkThemeEnabled();
        text_label_->setStyleSheet(QStringLiteral("QLabel { color: %1; background: transparent; }")
            .arg(appThemeColor(AppThemeColor::MenuText, dark).name(QColor::HexRgb)));
        updateCheckIcon();
        update();
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event);
        QPainter painter(this);
        const bool dark = VaporView::isDarkThemeEnabled();
        if (underMouse())
        {
            painter.fillRect(rect(), appThemeColor(AppThemeColor::MenuHover, dark));
        }
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && rect().contains(event->position().toPoint()))
        {
            if (clicked_)
            {
                clicked_();
            }
            event->accept();
            return;
        }
        QWidget::mouseReleaseEvent(event);
    }

    void enterEvent(QEnterEvent *event) override
    {
        QWidget::enterEvent(event);
        update();
    }

    void leaveEvent(QEvent *event) override
    {
        QWidget::leaveEvent(event);
        update();
    }

private:
    void updateCheckIcon()
    {
        if (!checked_ || check_icon_.isNull())
        {
            check_label_->clear();
            return;
        }
        check_label_->setPixmap(check_icon_.pixmap(18, 18));
    }

    QLabel *text_label_;
    QLabel *check_label_;
    QIcon check_icon_;
    std::function<void()> clicked_;
    bool checked_ = false;
};

class WaveDisplayTitleLabel final : public VaporView::VisualTextLabel
{
public:
    explicit WaveDisplayTitleLabel(QWidget *parent = nullptr)
        : VaporView::VisualTextLabel(parent)
        , menu_(new QMenu(this))
    {
        setFocusPolicy(Qt::NoFocus);
        setMouseTracking(true);
        show_all_row_ = addModeRow(0);
        show_raw_row_ = addModeRow(1);
        show_harmonic_row_ = addModeRow(2);
        show_peak_trend_row_ = addModeRow(3);
        setCurrentStates(true, false, false, false);
    }

    QSize sizeHint() const override
    {
        QSize size = QLabel::sizeHint();
        if (!text().isEmpty())
        {
            const QFontMetrics metrics(font());
            size.rwidth() = std::max(size.width(), metrics.horizontalAdvance(text()) + 8);
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

    void refreshFixedWidth()
    {
        setFixedWidth(sizeHint().width());
        if (QWidget *cluster = parentWidget())
        {
            cluster->setFixedWidth(cluster->sizeHint().width());
            cluster->updateGeometry();
        }
        updateGeometry();
        refreshTooltipAnchor();
    }

    void setIcon(const QIcon& icon)
    {
        normal_icon_ = icon;
        update();
    }

    void setEnglish(bool english)
    {
        const QString title = english ? QStringLiteral("Wave display settings") : QStringLiteral("波形显示设置");
        setToolTip(title);
        setAccessibleName(title);
        menu_->setTitle(english ? QStringLiteral("Wave Display") : QStringLiteral("波形显示"));
        show_all_row_->setText(english ? QStringLiteral("Show All") : QStringLiteral("全部显示"));
        show_raw_row_->setText(english ? QStringLiteral("Show Raw Signal") : QStringLiteral("显示原始信号"));
        show_harmonic_row_->setText(english ? QStringLiteral("Show Second Harmonic") : QStringLiteral("显示二次谐波"));
        show_peak_trend_row_->setText(english ? QStringLiteral("Show Second Harmonic Peak Trend") : QStringLiteral("显示二次谐波峰值趋势"));
        refreshTooltipAnchor();
    }

    void setCurrentStates(bool showAll, bool showRaw, bool showHarmonic, bool showPeakTrend)
    {
        show_all_ = showAll;
        show_raw_ = showRaw;
        show_harmonic_ = showHarmonic;
        show_peak_trend_ = showPeakTrend;
        show_all_row_->setChecked(show_all_);
        show_raw_row_->setChecked(show_raw_);
        show_harmonic_row_->setChecked(show_harmonic_);
        show_peak_trend_row_->setChecked(show_peak_trend_);
    }

    void setCheckIcon(const QIcon& icon)
    {
        check_icon_ = icon;
        for (WaveDisplayMenuRow *row : {show_all_row_, show_raw_row_, show_harmonic_row_, show_peak_trend_row_})
        {
            row->setCheckIcon(check_icon_);
            row->refreshTheme();
        }
    }

    void setModeChangedCallback(std::function<void(bool, bool, bool, bool)> callback)
    {
        mode_changed_ = std::move(callback);
    }

    void popupMenuFrom(QWidget *anchor)
    {
        QWidget *popupAnchor = anchor ? anchor : this;
        menu_->popup(popupAnchor->mapToGlobal(QPoint(0, popupAnchor->height())));
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
            refreshFixedWidth();
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
            painter.setBrush(dark ? QColor(30, 30, 30) : appThemeColor(AppThemeColor::TitleBarHover, dark));
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
            menu_->popup(mapToGlobal(QPoint(iconRect().left(), height())));
            event->accept();
            return;
        }
        QLabel::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        refreshTooltipAnchor();
        setIconHovered(inline_icon_visible_ && iconRect().contains(event->position().toPoint()));
        QLabel::mouseMoveEvent(event);
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
    QIcon check_icon_;
    QMenu *menu_;
    WaveDisplayMenuRow *show_all_row_ = nullptr;
    WaveDisplayMenuRow *show_raw_row_ = nullptr;
    WaveDisplayMenuRow *show_harmonic_row_ = nullptr;
    WaveDisplayMenuRow *show_peak_trend_row_ = nullptr;
    std::function<void(bool, bool, bool, bool)> mode_changed_;
    bool show_all_ = false;
    bool show_raw_ = false;
    bool show_harmonic_ = false;
    bool show_peak_trend_ = false;
    bool icon_hovered_ = false;
    bool inline_icon_visible_ = false;

    WaveDisplayMenuRow *addModeRow(int mode)
    {
        auto *row = new WaveDisplayMenuRow(menu_);
        auto *action = new QWidgetAction(menu_);
        action->setDefaultWidget(row);
        menu_->addAction(action);
        row->setClickedCallback([this, mode]() {
            if (mode == 0)
            {
                setCurrentStates(true, false, false, false);
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
                setCurrentStates(false, show_raw_, show_harmonic_, show_peak_trend_);
            }
            if (mode_changed_)
            {
                mode_changed_(show_all_, show_raw_, show_harmonic_, show_peak_trend_);
            }
            if (mode == 0)
            {
                menu_->hide();
            }
        });
        return row;
    }
};

struct PlotTheme
{
    QColor background;
    QColor grid;
    QColor border;
    QColor text;
    QColor mutedText;
};

PlotTheme plotThemeFor(const QWidget *widget)
{
    const QPalette palette = widget->palette();
    QColor background = palette.color(QPalette::Base);
    if (!background.isValid() || background.alpha() == 0)
    {
        background = palette.color(QPalette::Window);
    }
    const bool dark = background.lightness() < 128;
    const QColor neutralGrid = dark ? QColor(QStringLiteral("#2A2A2A")) : QColor(QStringLiteral("#E6E8EC"));
    const QColor neutralBorder = dark ? QColor(QStringLiteral("#3D3D3D")) : QColor(QStringLiteral("#D7DCE3"));
    return {
        background,
        neutralGrid,
        neutralBorder,
        appThemeColor(AppThemeColor::PlotText, dark),
        appThemeColor(AppThemeColor::PlotMutedText, dark)
    };
}

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
}

class WavePlotWidget : public QWidget
{
public:
    explicit WavePlotWidget(const QColor& lineColor, QWidget *parent = nullptr)
        : QWidget(parent)
        , line_color_(lineColor)
    {
        setFont(numericFontFrom(font()));
        setMinimumHeight(kWavePlotMinimumHeight);
        setMaximumHeight(kWavePlotMaximumHeight);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    }

    void setSamples(const QVector<float>& samples)
    {
        samples_ = samples;
        update();
    }

    void setEmptyText(const QString& text)
    {
        empty_text_ = text;
        update();
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        QWidget::paintEvent(event);

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const PlotTheme theme = plotThemeFor(this);
        painter.fillRect(rect(), theme.background);

        const QFontMetrics fm = painter.fontMetrics();
        const int leftMargin = kWavePlotLeftMargin;
        const int bottomMargin = fm.height() + 2;
        const QRectF plotRect = rect().adjusted(leftMargin, kPlotTopMargin, -kPlotRightMargin, -bottomMargin);
        const auto drawGrid = [&]() {
            painter.setPen(QPen(theme.grid, 1));
            for (int i = 0; i <= 10; ++i)
            {
                const qreal x = plotRect.left() + plotRect.width() * i / 10.0;
                painter.drawLine(QPointF(x, plotRect.top()), QPointF(x, plotRect.bottom()));
            }
            for (int i = 0; i <= 6; ++i)
            {
                const qreal y = plotRect.top() + plotRect.height() * i / 6.0;
                painter.drawLine(QPointF(plotRect.left(), y), QPointF(plotRect.right(), y));
            }

            painter.setPen(QPen(theme.border, 1));
            painter.drawRect(plotRect);
        };

        if (samples_.isEmpty())
        {
            drawGrid();
            painter.setPen(theme.mutedText);
            painter.drawText(plotRect, Qt::AlignCenter, empty_text_);
            return;
        }

        auto [minIt, maxIt] = std::minmax_element(samples_.cbegin(), samples_.cend());
        float minValue = *minIt;
        float maxValue = *maxIt;
        if (std::fabs(maxValue - minValue) < 1e-6f)
        {
            const float pad = std::max(1e-6f, std::fabs(maxValue) * 0.05f + 1e-6f);
            minValue -= pad;
            maxValue += pad;
        }

        drawGrid();

        const int columns = std::max(2, static_cast<int>(std::floor(plotRect.width())));
        QPolygonF polyline;
        polyline.reserve(columns);
        const int sampleCount = samples_.size();
        for (int x = 0; x < columns; ++x)
        {
            const double ratio = columns == 1 ? 0.0 : static_cast<double>(x) / static_cast<double>(columns - 1);
            const int index = std::clamp(static_cast<int>(std::llround(ratio * (sampleCount - 1))), 0, sampleCount - 1);
            const float value = samples_.at(index);
            const double normalized = (value - minValue) / std::max(1e-6f, maxValue - minValue);
            const qreal px = plotRect.left() + ratio * plotRect.width();
            const qreal py = plotRect.bottom() - normalized * plotRect.height();
            polyline.append(QPointF(px, py));
        }

        painter.setPen(QPen(line_color_, 1.4));
        painter.drawPolyline(polyline);

        painter.setPen(theme.text);
        for (int i = 0; i < 4; ++i)
        {
            const double ratio = i / 3.0;
            const double value = maxValue - (maxValue - minValue) * ratio;
            const qreal y = plotRect.top() + plotRect.height() * ratio;
            const qreal labelTop = i == 0
                ? plotRect.top() - 2
                : (i == 3 ? plotRect.bottom() - fm.height() + 2 : y - fm.height() * 0.5);
            painter.drawText(QRectF(2, labelTop, leftMargin - 4, fm.height()),
                             Qt::AlignRight | Qt::AlignVCenter,
                             formatWaveValue(value, 3));
        }
        painter.drawText(QRectF(plotRect.left(), plotRect.bottom() + 2, plotRect.width(), fm.height()), Qt::AlignRight | Qt::AlignVCenter,
                         QString("%1 samples").arg(sampleCount));
    }

private:
    QColor line_color_;
    QVector<float> samples_;
    QString empty_text_ = QStringLiteral("No data");
};

class PeakTrendPlotWidget : public QWidget
{
public:
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
    }

    void setPeakValues(const QVector<float>& values)
    {
        const bool keepTail = peak_values_.isEmpty() ||
            view_count_ <= 0 ||
            (view_start_index_ + visibleCount()) >= peak_values_.size();
        peak_values_ = values;
        normalizeView(keepTail);
        notifyViewChanged();
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

protected:
    void paintEvent(QPaintEvent *event) override
    {
        QWidget::paintEvent(event);

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, false);
        const PlotTheme theme = plotThemeFor(this);
        painter.fillRect(rect(), theme.background);

        const QFontMetrics fm = painter.fontMetrics();
        const int leftMargin = kPeakPlotLeftMargin;
        const int bottomMargin = fm.height() + 2;
        const QRectF plotRect = rect().adjusted(leftMargin, kPlotTopMargin, -kPlotRightMargin, -bottomMargin);
        const auto drawGrid = [&]() {
            painter.setPen(QPen(theme.grid, 1));
            for (int i = 0; i <= 10; ++i)
            {
                const qreal x = plotRect.left() + plotRect.width() * i / 10.0;
                painter.drawLine(QPointF(x, plotRect.top()), QPointF(x, plotRect.bottom()));
            }
            for (int i = 0; i <= 6; ++i)
            {
                const qreal y = plotRect.top() + plotRect.height() * i / 6.0;
                painter.drawLine(QPointF(plotRect.left(), y), QPointF(plotRect.right(), y));
            }

            painter.setPen(QPen(theme.border, 1));
            painter.drawRect(plotRect);
        };

        if (peak_values_.isEmpty())
        {
            drawGrid();
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
            drawGrid();
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

                const double ratio = static_cast<double>(i) / static_cast<double>(count - 1);
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
                const double ratio = count == 1 ? 0.5 : static_cast<double>(i) / static_cast<double>(count - 1);
                const double normalized = (value - minValue) / std::max(1e-6f, maxValue - minValue);
                const QPointF point(plotRect.left() + ratio * plotRect.width(),
                                    plotRect.bottom() - normalized * plotRect.height());
                painter.drawEllipse(point, 2.5, 2.5);
            }
            painter.setRenderHint(QPainter::Antialiasing, false);
        }

        painter.setPen(theme.text);
        for (int i = 0; i < 4; ++i)
        {
            const double ratio = i / 3.0;
            const double value = maxValue - (maxValue - minValue) * ratio;
            const qreal y = plotRect.top() + plotRect.height() * ratio;
            const qreal labelTop = i == 0
                ? plotRect.top() - 2
                : (i == 3 ? plotRect.bottom() - fm.height() + 2 : y - fm.height() * 0.5);
            painter.drawText(QRectF(2, labelTop, leftMargin - 4, fm.height()),
                             Qt::AlignRight | Qt::AlignVCenter,
                             formatWaveValue(value, 3));
        }
        painter.drawText(QRectF(plotRect.left(), plotRect.bottom() + 2, plotRect.width() * 0.55, fm.height()),
                         Qt::AlignLeft | Qt::AlignVCenter,
                         QString("%1-%2 / %3")
                             .arg(startIndex + 1)
                             .arg(startIndex + count)
                             .arg(peak_values_.size()));
        painter.drawText(QRectF(plotRect.left(), plotRect.bottom() + 2, plotRect.width(), fm.height()), Qt::AlignRight | Qt::AlignVCenter,
                         QString("%1 frames").arg(count));
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

    QVector<float> peak_values_;
    PlotMode plot_mode_;
    int view_start_index_;
    int view_count_;
    QString empty_text_ = QStringLiteral("No peak data");
    std::function<void(int, int, int)> on_view_changed_;
};

TcpWavePanel::TcpWavePanel(QWidget *parent)
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
    , wave_display_all_(true)
    , wave_display_raw_(false)
    , wave_display_harmonic_(false)
    , wave_display_peak_trend_(false)
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
    , live_display_dirty_(false)
    , process_buffer_pending_(false)
    , payload_order_auto_correct_logged_(false)
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
    if (plots_layout_)
    {
        plots_layout_->setDirection(compact ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
        plots_layout_->setSpacing(compact ? 4 : 1);
        plots_layout_->invalidate();
        plots_layout_->activate();
    }
    updateGeometry();
}

int TcpWavePanel::preferredPanelHeight() const
{
    const int topControlsHeight = control_layout_ ? control_layout_->sizeHint().height() : kTcpTitleBarHeight;
    int height = topControlsHeight + 4;

    const bool showRawWave = wave_display_all_ || wave_display_raw_;
    const bool showHarmonicWave = wave_display_all_ || wave_display_harmonic_;
    const bool showPeakTrend = wave_display_all_ || wave_display_peak_trend_;
    const int visibleWaveRows = (showRawWave ? 1 : 0) + (showHarmonicWave ? 1 : 0);

    if (visibleWaveRows > 0)
    {
        const int plotRowHeight = kTcpTitleBarHeight + kWavePlotMinimumHeight + 6;
        height += 4;
        height += compact_layout_
            ? visibleWaveRows * plotRowHeight + std::max(0, visibleWaveRows - 1) * 4
            : plotRowHeight;
    }
    if (showPeakTrend)
    {
        height += 4;
        height += kTcpTitleBarHeight + kPeakPlotMinimumHeight + 8;
    }
    return height;
}

bool TcpWavePanel::hasVisibleWaveDisplay() const
{
    return wave_display_all_ || wave_display_raw_ || wave_display_harmonic_ || wave_display_peak_trend_;
}

bool TcpWavePanel::usesExpandedPanelHeight() const
{
    const int visibleDisplayCount =
        ((wave_display_all_ || wave_display_raw_) ? 1 : 0) +
        ((wave_display_all_ || wave_display_harmonic_) ? 1 : 0) +
        ((wave_display_all_ || wave_display_peak_trend_) ? 1 : 0);
    return visibleDisplayCount > 1;
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
    topControlsBar->setFixedHeight(kTcpTitleBarHeight);
    top_controls_layout_ = new QHBoxLayout(topControlsBar);
    top_controls_layout_->setContentsMargins(8, 2, 8, 2);
    top_controls_layout_->setSpacing(0);
    control_layout_->addWidget(topControlsBar, 0, 0, 1, 6);

    auto *waveDisplayTitle = new WaveDisplayTitleLabel(topControlsBar);
    waveDisplayTitle->setModeChangedCallback([this](bool showAll, bool showRaw, bool showHarmonic, bool showPeakTrend) {
        wave_display_all_ = showAll;
        wave_display_raw_ = showRaw;
        wave_display_harmonic_ = showHarmonic;
        wave_display_peak_trend_ = showPeakTrend;
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

    plots_layout_ = new QHBoxLayout();
    plots_layout_->setSpacing(1);

    wave1_group_ = new QGroupBox(this);
    wave1_group_->setObjectName("sensorGroupBox");
    auto *wave1Layout = new QVBoxLayout(wave1_group_);
    wave1Layout->setContentsMargins(2, 2, 2, 2);
    auto *wave1HeaderBar = new QWidget(wave1_group_);
    wave1HeaderBar->setObjectName("sectionTitleBar");
    wave1HeaderBar->setFixedHeight(kTcpTitleBarHeight);
    auto *wave1HeaderLayout = new QHBoxLayout(wave1HeaderBar);
    wave1HeaderLayout->setContentsMargins(8, 2, 8, 2);
    wave1HeaderLayout->setSpacing(8);
    wave1_title_label_ = new VaporView::VisualTextLabel(this);
    wave1_title_label_->setObjectName("sectionTitleLabel");
    wave1_title_label_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    wave1_title_label_->setMargin(0);
    wave1_title_label_->setContentsMargins(0, 0, 0, 0);
    wave1_title_label_->setFixedHeight(kTcpButtonHeight);
    wave1HeaderLayout->addWidget(wave1_title_label_, 0, Qt::AlignVCenter | Qt::AlignLeft);
    wave1_info_label_ = new QLabel(this);
    wave1_info_label_->setObjectName("fieldLabel");
    wave1_info_label_->setFont(numericFontFrom(wave1_info_label_->font()));
    wave1_info_label_->setTextFormat(Qt::PlainText);
    wave1_info_label_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    wave1_info_label_->setWordWrap(false);
    wave1_info_label_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    wave1HeaderLayout->addWidget(wave1_info_label_, 1);
    wave1Layout->addWidget(wave1HeaderBar);
    wave1_plot_ = new WavePlotWidget(appThemeColor(AppThemeColor::PlotSeriesWaveBlue, false), this);
    wave1Layout->addWidget(wave1_plot_, 1);
    plots_layout_->addWidget(wave1_group_, 1);

    wave4_group_ = new QGroupBox(this);
    wave4_group_->setObjectName("sensorGroupBox");
    auto *wave4Layout = new QVBoxLayout(wave4_group_);
    wave4Layout->setContentsMargins(2, 2, 2, 2);
    auto *wave4HeaderBar = new QWidget(wave4_group_);
    wave4HeaderBar->setObjectName("sectionTitleBar");
    wave4HeaderBar->setFixedHeight(kTcpTitleBarHeight);
    auto *wave4HeaderLayout = new QHBoxLayout(wave4HeaderBar);
    wave4HeaderLayout->setContentsMargins(8, 2, 8, 2);
    wave4HeaderLayout->setSpacing(8);
    wave4_title_label_ = new VaporView::VisualTextLabel(this);
    wave4_title_label_->setObjectName("sectionTitleLabel");
    wave4_title_label_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    wave4_title_label_->setMargin(0);
    wave4_title_label_->setContentsMargins(0, 0, 0, 0);
    wave4_title_label_->setFixedHeight(kTcpButtonHeight);
    wave4HeaderLayout->addWidget(wave4_title_label_, 0, Qt::AlignVCenter | Qt::AlignLeft);
    wave4_info_label_ = new QLabel(this);
    wave4_info_label_->setObjectName("fieldLabel");
    wave4_info_label_->setFont(numericFontFrom(wave4_info_label_->font()));
    wave4_info_label_->setTextFormat(Qt::PlainText);
    wave4_info_label_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    wave4_info_label_->setWordWrap(false);
    wave4_info_label_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    wave4HeaderLayout->addWidget(wave4_info_label_, 1);
    wave4Layout->addWidget(wave4HeaderBar);
    wave4_plot_ = new WavePlotWidget(appThemeColor(AppThemeColor::PlotSeriesWaveOrange, false), this);
    wave4Layout->addWidget(wave4_plot_, 1);
    plots_layout_->addWidget(wave4_group_, 1);

    mainLayout->addLayout(plots_layout_, 1);

    peak_group_ = new QGroupBox(this);
    peak_group_->setObjectName("sensorGroupBox");
    peak_group_->setMinimumHeight(198);
    peak_group_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::MinimumExpanding);
    auto *peakLayout = new QVBoxLayout(peak_group_);
    peakLayout->setContentsMargins(2, 2, 2, 2);
    auto *peakHeaderBar = new QWidget(peak_group_);
    peakHeaderBar->setObjectName("sectionTitleBar");
    peakHeaderBar->setFixedHeight(kTcpTitleBarHeight);
    auto *peakHeaderLayout = new QHBoxLayout(peakHeaderBar);
    peakHeaderLayout->setContentsMargins(8, 2, 8, 2);
    peakHeaderLayout->setSpacing(6);
    peak_title_label_ = new VaporView::VisualTextLabel(this);
    peak_title_label_->setObjectName("sectionTitleLabel");
    peak_title_label_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    peak_title_label_->setMargin(0);
    peak_title_label_->setContentsMargins(0, 0, 0, 0);
    peak_title_label_->setFixedHeight(kTcpButtonHeight);
    peakHeaderLayout->addWidget(peak_title_label_, 0, Qt::AlignVCenter | Qt::AlignLeft);
    peak_filter_button_ = new QPushButton(this);
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
    peakHeaderLayout->addWidget(peak_filter_button_, 0, Qt::AlignVCenter | Qt::AlignLeft);
    peak_mode_button_ = new QPushButton(this);
    peak_mode_button_->setObjectName("compactTcpButton");
    peak_mode_button_->setFixedHeight(kTcpButtonHeight);
    peak_mode_button_->setMinimumWidth(98);
    connect(peak_mode_button_, &QPushButton::clicked, this, &TcpWavePanel::onTogglePeakPlotModeClicked);
    peakHeaderLayout->addWidget(peak_mode_button_, 0, Qt::AlignVCenter | Qt::AlignLeft);
    peak_clear_button_ = new QPushButton(this);
    peak_clear_button_->setObjectName("compactTcpButton");
    peak_clear_button_->setFixedHeight(kTcpButtonHeight);
    peak_clear_button_->setMinimumWidth(72);
    connect(peak_clear_button_, &QPushButton::clicked, this, &TcpWavePanel::onClearPeakPlotClicked);
    peakHeaderLayout->addWidget(peak_clear_button_, 0, Qt::AlignVCenter | Qt::AlignLeft);
    peakHeaderLayout->addStretch(1);
    peakLayout->addWidget(peakHeaderBar);
    peak_plot_ = new PeakTrendPlotWidget(this);
    peak_plot_->setPlotMode(peak_plot_scatter_mode_ ? PeakTrendPlotWidget::PlotMode::Scatter : PeakTrendPlotWidget::PlotMode::Polyline);
    peakLayout->addWidget(peak_plot_);
    mainLayout->addWidget(peak_group_, 0);

    connect(host_edit_, &QLineEdit::textChanged, this, [this](const QString&) {
        saveRememberedInputState();
    });
    connect(port_edit_, &QLineEdit::textChanged, this, [this](const QString&) {
        saveRememberedInputState();
    });

    live_display_timer_ = new QTimer(this);
    live_display_timer_->setTimerType(Qt::PreciseTimer);
    live_display_timer_->setInterval(kLiveDisplayRefreshMs);
    connect(live_display_timer_, &QTimer::timeout, this, &TcpWavePanel::updateLiveDisplay);
    live_display_timer_->start();
}

void TcpWavePanel::loadRememberedInputState()
{
    QSettings settings("VaporView", "TcpWavePanel");
    const QString hostValue = settings.value("connection/host", host_edit_->text()).toString();
    const QString portValue = settings.value("connection/port", port_edit_->text()).toString();
    const QString peakFilterMode = settings.value("peak_filter/mode", QStringLiteral("none")).toString().trimmed().toLower();
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
    peak_filter_settings_.min_value = settings.value("peak_filter/min_value", 0.0).toDouble();
    peak_filter_settings_.max_value = settings.value("peak_filter/max_value", 0.0).toDouble();
    peak_search_start_index_ = std::max(0, settings.value("peak_search/start_index", kDefaultPeakSearchStartIndex).toInt());
    peak_search_end_index_ = std::max(0, settings.value("peak_search/end_index", kDefaultPeakSearchEndIndex).toInt());

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
    QSettings settings("VaporView", "TcpWavePanel");
    settings.setValue("connection/host", host_edit_->text());
    settings.setValue("connection/port", port_edit_->text().trimmed());

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
    settings.setValue("peak_filter/mode", modeKey);
    settings.setValue("peak_filter/min_value", peak_filter_settings_.min_value);
    settings.setValue("peak_filter/max_value", peak_filter_settings_.max_value);
    settings.setValue("peak_search/start_index", peak_search_start_index_);
    settings.setValue("peak_search/end_index", peak_search_end_index_);
}

void TcpWavePanel::setEnglish(bool english)
{
    is_english_ = english;
    if (panel_title_label_)
    {
        panel_title_label_->setText(english ? "TCP Wave Monitor" : "TCP波形监视");
        if (auto *titleLabel = dynamic_cast<WaveDisplayTitleLabel *>(panel_title_label_))
        {
            titleLabel->refreshFixedWidth();
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
    connect_button_->setText(remote_sky_mode_
        ? (remote_wave_tcp_connected_ ? (english ? "Disconnect Sky Wave" : "断开天空波形")
                                      : (english ? "Connect Sky Wave" : "连接天空波形"))
        : (socket_ && socket_->state() == QAbstractSocket::ConnectedState
            ? (english ? "Disconnect" : "断开")
            : (english ? "Connect" : "连接")));
    wave1_group_->setTitle(QString());
    wave4_group_->setTitle(QString());
    peak_group_->setTitle(QString());
    if (wave1_title_label_)
    {
        wave1_title_label_->setText(english ? "Raw Signal" : "原始信号");
    }
    if (wave4_title_label_)
    {
        wave4_title_label_->setText(english ? "Normalized Second Harmonic" : "归一化二次谐波");
    }
    if (peak_title_label_)
    {
        const QString titleText = english
            ? QStringLiteral("Normalized Second Harmonic Peak Trend")
            : QStringLiteral("归一化二次谐波峰值趋势");
        peak_title_label_->setText(titleText);
        peak_title_label_->setToolTip(titleText);
    }
    if (wave1_plot_)
    {
        wave1_plot_->setEmptyText(english ? QStringLiteral("No data") : QStringLiteral("暂无数据"));
    }
    if (wave4_plot_)
    {
        wave4_plot_->setEmptyText(english ? QStringLiteral("No data") : QStringLiteral("暂无数据"));
    }
    if (peak_plot_)
    {
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
    applyWaveDisplayMode();

    if (!socket_ || socket_->state() != QAbstractSocket::ConnectedState)
    {
        setStatusText(english ? "Idle" : "空闲");
    }
}

void TcpWavePanel::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (!event)
    {
        return;
    }

    if (event->type() == QEvent::ApplicationPaletteChange ||
        event->type() == QEvent::PaletteChange ||
        event->type() == QEvent::StyleChange)
    {
        updateWaveDisplayModeIcon();
        updatePeakFilterButtonText();
    }
}

void TcpWavePanel::updateWaveDisplayModeTexts()
{
    if (auto *titleLabel = dynamic_cast<WaveDisplayTitleLabel *>(panel_title_label_))
    {
        titleLabel->setEnglish(is_english_);
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
        titleLabel->setCheckIcon(createMenuCheckIcon(appThemeColor(AppThemeColor::MenuCheckText, dark)));
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

void TcpWavePanel::applyWaveDisplayMode()
{
    const bool showRawWave = wave_display_all_ || wave_display_raw_;
    const bool showHarmonicWave = wave_display_all_ || wave_display_harmonic_;
    const bool showPeakTrend = wave_display_all_ || wave_display_peak_trend_;

    if (auto *titleLabel = dynamic_cast<WaveDisplayTitleLabel *>(panel_title_label_))
    {
        titleLabel->setCurrentStates(wave_display_all_, wave_display_raw_, wave_display_harmonic_, wave_display_peak_trend_);
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

    const bool wavePlotsFillAvailableHeight = !showPeakTrend;
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
        wave1_plot_->setSamples(wave1_history_);
    }
    if (showHarmonicWave && wave4_plot_)
    {
        wave4_plot_->setSamples(wave4_history_);
    }
    if (showPeakTrend && peak_plot_)
    {
        peak_plot_->setPeakValues(peak_history_);
    }

    updateGeometry();
    emit preferredPanelHeightChanged();
}

void TcpWavePanel::updatePeakPlotModeButtonText()
{
    if (!peak_mode_button_)
    {
        return;
    }

    peak_mode_button_->setText(peak_plot_scatter_mode_
        ? (is_english_ ? "Trend: Line" : "趋势显示：折线")
        : (is_english_ ? "Trend: Scatter" : "趋势显示：散点"));
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
    if (!live_display_dirty_)
    {
        return;
    }

    live_display_dirty_ = false;
    if ((wave_display_all_ || wave_display_raw_) && wave1_plot_)
    {
        wave1_plot_->setSamples(wave1_history_);
    }
    if ((wave_display_all_ || wave_display_harmonic_) && wave4_plot_)
    {
        wave4_plot_->setSamples(wave4_history_);
    }
    if ((wave_display_all_ || wave_display_peak_trend_) && peak_plot_)
    {
        peak_plot_->setPeakValues(peak_history_);
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

bool TcpWavePanel::isConnected() const
{
    if (remote_sky_mode_)
    {
        return remote_wave_tcp_connected_;
    }
    return socket_ && socket_->state() == QAbstractSocket::ConnectedState;
}

bool TcpWavePanel::isConnecting() const
{
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
    remote_sky_mode_ = enabled;
    if (host_edit_) host_edit_->setEnabled(!enabled);
    if (port_edit_) port_edit_->setEnabled(!enabled);
    if (enabled && socket_ && socket_->state() != QAbstractSocket::UnconnectedState)
    {
        requestGracefulDisconnect();
    }
    if (connect_button_)
    {
        connect_button_->setText(enabled
            ? (remote_wave_tcp_connected_ ? (is_english_ ? "Disconnect Sky Wave" : "断开天空波形")
                                          : (is_english_ ? "Connect Sky Wave" : "连接天空波形"))
                             : (isConnected() ? (is_english_ ? "Disconnect" : "断开")
                             : (is_english_ ? "Connect" : "连接")));
    }
    if (status_label_)
    {
        status_label_->setVisible(enabled);
        if (!enabled)
        {
            status_label_->clear();
        }
    }
    if (enabled && !remote_wave_tcp_connected_)
    {
        clearRemoteWaveformDisplay(is_english_ ? QStringLiteral("Sky Wave TCP is not connected")
                                               : QStringLiteral("天空端波形 TCP 未连接"));
    }
}

void TcpWavePanel::setRemoteWaveTcpState(VaporView::DeviceState state)
{
    const bool wasConnected = remote_wave_tcp_connected_;
    remote_wave_tcp_connected_ = state == VaporView::DeviceState::Connected;
    if (remote_sky_mode_ && connect_button_)
    {
        connect_button_->setText(remote_wave_tcp_connected_
            ? (is_english_ ? "Disconnect Sky Wave" : "断开天空波形")
            : (is_english_ ? "Connect Sky Wave" : "连接天空波形"));
    }
    if (remote_sky_mode_)
    {
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
            setStatusText(QString(is_english_ ? "Remote Sky wave TCP: %1" : "天空端波形 TCP：%1")
                              .arg(VaporView::deviceStateName(state)));
        }
        if (!remote_wave_tcp_connected_ && wasConnected)
        {
            last_remote_feature_time_us_ = 0;
            clearRemoteWaveformDisplay(is_english_ ? QStringLiteral("Sky Wave TCP disconnected")
                                                   : QStringLiteral("天空端波形 TCP 已断开"));
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
    Q_UNUSED(timestampUs);
    if (remote_sky_mode_ && !remote_wave_tcp_connected_)
    {
        return;
    }
    if (samples.isEmpty())
    {
        return;
    }
    const QString sampleCountText = fixedStatusInteger(samples.size(), kRemoteStatusCountWidth);
    wave1_history_ = samples;
    pending_wave1_info_text_ = QString(is_english_ ? "remote source: %1 samples" : "远程源：%1 点")
        .arg(sampleCountText);
    remote_waveform_status_text_ = remoteWaveformStatusText(is_english_, samples.size());
    updatePendingRemoteLiveStatus();
    live_display_dirty_ = true;
}

void TcpWavePanel::injectRemoteSecondHarmonicFrame(quint64 timestampUs, const QVector<float>& samples)
{
    if (remote_sky_mode_ && !remote_wave_tcp_connected_)
    {
        return;
    }
    if (samples.isEmpty())
    {
        return;
    }
    wave4_history_ = samples;
    ++frame_count_;
    updateFrameRateDisplay(QDateTime::currentMSecsSinceEpoch());
    const QString sampleCountText = fixedStatusInteger(samples.size(), kRemoteStatusCountWidth);
    pending_wave1_info_text_ = is_english_ ? "Remote Sky source" : "天空端远程源";
    pending_wave4_info_text_ = QString(is_english_ ? "%1 samples" : "%1 点").arg(sampleCountText);
    remote_waveform_status_text_ = remoteWaveformStatusText(is_english_, samples.size());
    updatePendingRemoteLiveStatus();
    live_display_dirty_ = true;
    emit normalizedSecondHarmonicFrameReady(timestampUs, wave4_history_);
}

void TcpWavePanel::injectRemoteWaveformFeature(const VaporView::WaveformFeature& feature)
{
    if (remote_sky_mode_ && !remote_wave_tcp_connected_)
    {
        return;
    }
    const bool validFeature = feature.quality_flags == 0 && feature.host_time_us > 0 && std::isfinite(feature.peak);
    if (!validFeature)
    {
        peak_raw_history_.push_back(std::numeric_limits<float>::quiet_NaN());
        last_remote_feature_time_us_ = feature.host_time_us;
        if (peak_raw_history_.size() > kPeakTrendFrameWindow)
        {
            peak_raw_history_.remove(0, peak_raw_history_.size() - kPeakTrendFrameWindow);
        }
        rebuildPeakHistory();
        remote_feature_status_text_ = remoteInvalidFeatureStatusText(is_english_);
        updatePendingRemoteLiveStatus();
        live_display_dirty_ = true;
        return;
    }
    peak_raw_history_.push_back(feature.peak);
    last_remote_feature_time_us_ = feature.host_time_us;
    if (peak_raw_history_.size() > kPeakTrendFrameWindow)
    {
        peak_raw_history_.remove(0, peak_raw_history_.size() - kPeakTrendFrameWindow);
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
    last_remote_feature_time_us_ = 0;
    if (peak_plot_)
    {
        peak_plot_->setPeakValues({});
    }
    saveRememberedInputState();
    updatePeakFilterButtonText();
    setStatusText(is_english_
        ? QStringLiteral("Peak search range accepted by sky. Waiting for the next feature frame.")
        : QStringLiteral("峰值搜索区间已下发到天空端，等待下一帧特征值。"));
}

void TcpWavePanel::rejectRemotePeakSearchRange(const QString& reason)
{
    setStatusText(is_english_
        ? QStringLiteral("Sky rejected peak search range: %1").arg(reason)
        : QStringLiteral("天空端拒绝峰值搜索区间：%1").arg(reason));
}

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
    if (remote_sky_mode_)
    {
        const bool connectRequested = !remote_wave_tcp_connected_;
        emit logMessageRequested(connectRequested
            ? (is_english_ ? QStringLiteral("Requesting Sky wave TCP connection...")
                           : QStringLiteral("正在请求连接天空端波形 TCP..."))
            : (is_english_ ? QStringLiteral("Requesting Sky wave TCP disconnection...")
                           : QStringLiteral("正在请求断开天空端波形 TCP...")));
        emit remoteWaveTcpConnectionRequested(connectRequested);
        return;
    }

    if (socket_ && socket_->state() != QAbstractSocket::UnconnectedState)
    {
        emit logMessageRequested(is_english_ ? QStringLiteral("Disconnecting TCP wave link...")
                                             : QStringLiteral("正在断开 TCP 波形连接..."));
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
        emit logMessageRequested(message);
        setStatusText(message);
        if (port_edit_)
        {
            port_edit_->setFocus();
        }
        return;
    }

    emit logMessageRequested(QString(is_english_ ? "Connecting TCP wave link: %1:%2..."
                                                 : "正在连接 TCP 波形：%1:%2...")
        .arg(host_edit_->text()).arg(targetPort));
    recreateSocket();
    buffer_.clear();
    wave1_history_.clear();
    wave4_history_.clear();
    peak_raw_history_.clear();
    peak_history_.clear();
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
    process_buffer_pending_ = false;
    payload_order_auto_correct_logged_ = false;
    frame_arrival_times_ms_.clear();
    resetFrameRateDisplay();
    setStatusText(QString(is_english_ ? "Connecting to %1:%2..." : "正在连接 %1:%2...")
        .arg(host_edit_->text()).arg(targetPort));
    socket_->connectToHost(host_edit_->text(), static_cast<quint16>(targetPort));
    onSocketStateChanged();
}

void TcpWavePanel::onTogglePeakPlotModeClicked()
{
    peak_plot_scatter_mode_ = !peak_plot_scatter_mode_;
    updatePeakPlotModeButtonText();
    if (peak_plot_)
    {
        peak_plot_->setPlotMode(peak_plot_scatter_mode_ ? PeakTrendPlotWidget::PlotMode::Scatter : PeakTrendPlotWidget::PlotMode::Polyline);
    }
}

void TcpWavePanel::onClearPeakPlotClicked()
{
    peak_raw_history_.clear();
    peak_history_.clear();
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
                ? QStringLiteral("Remote Sky: peak search range sent to sky; waiting for ACK.")
                : QStringLiteral("Remote Sky 模式：峰值搜索区间已发送到天空端，等待 ACK。"));
        }
        else
        {
            peak_search_start_index_ = searchStart;
            peak_search_end_index_ = searchEnd;
            peak_raw_history_.clear();
            peak_history_.clear();
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
    emit logMessageRequested(QString(is_english_
        ? "TCP wave link connected: %1:%2"
        : "TCP 波形已连接：%1:%2")
        .arg(host_edit_->text()).arg(port()));
    setStatusText(QString(is_english_
        ? "Connected to %1:%2, waiting for the first frame..."
        : "已连接到 %1:%2，正在等待首帧数据...")
        .arg(host_edit_->text()).arg(port()));
}

void TcpWavePanel::onSocketDisconnected()
{
    const QString reason = socket_ && socket_->error() != QAbstractSocket::UnknownSocketError
        ? socket_->errorString()
        : (is_english_ ? QStringLiteral("connection closed") : QStringLiteral("连接已关闭"));
    setConnectedUiState(false);
    emit connectionStateChanged(false);
    emit logMessageRequested(QString(is_english_
        ? "TCP wave link disconnected: %1; received frames=%2, buffered bytes=%3, expected payload=%4"
        : "TCP 波形已断开：%1；已接收帧=%2，客户端缓冲=%3 字节，当前期望负载=%4 字节")
        .arg(reason)
        .arg(frame_count_)
        .arg(static_cast<qlonglong>(buffer_.size()))
        .arg(expected_payload_size_));
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

    buffer_.append(socket_->readAll());
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (buffer_.size() >= kTcpBufferBacklogWarningBytes &&
        (last_backlog_warning_ms_ <= 0 ||
         nowMs - last_backlog_warning_ms_ >= kTcpBufferBacklogWarningIntervalMs))
    {
        last_backlog_warning_ms_ = nowMs;
        emit logMessageRequested(QString(is_english_
            ? "TCP wave receive backlog is %1 bytes; UI processing may be falling behind."
            : "TCP 波形接收缓冲已积压 %1 字节；界面处理可能跟不上数据流。")
            .arg(static_cast<qlonglong>(buffer_.size())));
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
    setStatusText(errorText);
    emit logMessageRequested(QString(is_english_
        ? "TCP wave socket error: %1; received frames=%2, buffered bytes=%3"
        : "TCP 波形 socket 错误：%1；已接收帧=%2，客户端缓冲=%3 字节")
        .arg(errorText)
        .arg(frame_count_)
        .arg(static_cast<qlonglong>(buffer_.size())));
    if (socket_->state() == QAbstractSocket::UnconnectedState)
    {
        setConnectedUiState(false);
    }
}

void TcpWavePanel::setConnectedUiState(bool connected)
{
    const bool active = connected && socket_ && socket_->state() != QAbstractSocket::UnconnectedState;
    host_edit_->setEnabled(!active);
    port_edit_->setEnabled(!active);
    if (socket_ && socket_->state() == QAbstractSocket::ClosingState)
    {
        connect_button_->setText(is_english_ ? "Disconnecting..." : "正在断开...");
        connect_button_->setEnabled(false);
        return;
    }

    connect_button_->setEnabled(true);
    connect_button_->setText(active ? (is_english_ ? "Disconnect" : "断开") : (is_english_ ? "Connect" : "连接"));
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
        status_label_->setVisible(remote_sky_mode_);
    }
}

void TcpWavePanel::clearRemoteWaveformDisplay(const QString& statusText)
{
    wave1_history_.clear();
    wave4_history_.clear();
    peak_raw_history_.clear();
    peak_history_.clear();
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
    if (wave4_plot_) wave4_plot_->setSamples({});
    if (peak_plot_) peak_plot_->setPeakValues({});
    if (wave1_info_label_) wave1_info_label_->setText(QStringLiteral("--"));
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
                    emit logMessageRequested(QString(is_english_
                        ? "Auto-corrected reversed TCP waveform payload order (confidence=%1)."
                        : "已自动校正反向 TCP 波形负载顺序（置信比=%1）。")
                        .arg(orderAnalysis.confidence, 0, 'f', 1));
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
                    const float rawPeakValue = currentWaveformPeakValue(wave4_history_);
                    peak_raw_history_.push_back(rawPeakValue);
                    if (peak_raw_history_.size() > kPeakTrendFrameWindow)
                    {
                        peak_raw_history_.remove(0, peak_raw_history_.size() - kPeakTrendFrameWindow);
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
