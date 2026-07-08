#include "SingleLevelPopupMenu.h"

#include "AppTheme.h"

#include <QApplication>
#include <QEvent>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QImage>
#include <QRegion>
#include <QResizeEvent>
#include <QScreen>
#include <QSizePolicy>
#include <QShowEvent>
#include <QStyle>
#include <QTimer>
#include <QWidgetAction>

#include <algorithm>
#include <vector>

namespace VaporView
{
namespace
{
constexpr int kMenuShadowMargin = 22;

QImage boxBlurredAlpha(const QSize& size,
                       const QRectF& sourceRect,
                       qreal radius,
                       int blurRadius,
                       int iterations)
{
    QImage alpha(size, QImage::Format_ARGB32_Premultiplied);
    alpha.fill(Qt::transparent);
    {
        QPainter maskPainter(&alpha);
        maskPainter.setRenderHint(QPainter::Antialiasing, true);
        maskPainter.setPen(Qt::NoPen);
        maskPainter.setBrush(Qt::black);
        QPainterPath path;
        path.addRoundedRect(sourceRect, radius, radius);
        maskPainter.fillPath(path, Qt::black);
    }

    const int w = alpha.width();
    const int h = alpha.height();
    if (w <= 0 || h <= 0 || blurRadius <= 0)
    {
        return alpha;
    }

    std::vector<uchar> src(static_cast<size_t>(w * h));
    std::vector<uchar> tmp(src.size());
    std::vector<uchar> dst(src.size());
    for (int y = 0; y < h; ++y)
    {
        const auto *line = reinterpret_cast<const QRgb *>(alpha.constScanLine(y));
        for (int x = 0; x < w; ++x)
        {
            src[static_cast<size_t>(y * w + x)] = static_cast<uchar>(qAlpha(line[x]));
        }
    }

    const int diameter = blurRadius * 2 + 1;
    for (int pass = 0; pass < iterations; ++pass)
    {
        for (int y = 0; y < h; ++y)
        {
            int sum = 0;
            for (int x = -blurRadius; x <= blurRadius; ++x)
            {
                const int cx = std::clamp(x, 0, w - 1);
                sum += src[static_cast<size_t>(y * w + cx)];
            }
            for (int x = 0; x < w; ++x)
            {
                tmp[static_cast<size_t>(y * w + x)] = static_cast<uchar>(sum / diameter);
                const int removeX = std::clamp(x - blurRadius, 0, w - 1);
                const int addX = std::clamp(x + blurRadius + 1, 0, w - 1);
                sum += src[static_cast<size_t>(y * w + addX)] - src[static_cast<size_t>(y * w + removeX)];
            }
        }

        for (int x = 0; x < w; ++x)
        {
            int sum = 0;
            for (int y = -blurRadius; y <= blurRadius; ++y)
            {
                const int cy = std::clamp(y, 0, h - 1);
                sum += tmp[static_cast<size_t>(cy * w + x)];
            }
            for (int y = 0; y < h; ++y)
            {
                dst[static_cast<size_t>(y * w + x)] = static_cast<uchar>(sum / diameter);
                const int removeY = std::clamp(y - blurRadius, 0, h - 1);
                const int addY = std::clamp(y + blurRadius + 1, 0, h - 1);
                sum += tmp[static_cast<size_t>(addY * w + x)] - tmp[static_cast<size_t>(removeY * w + x)];
            }
        }
        src.swap(dst);
    }

    QImage blurred(size, QImage::Format_ARGB32_Premultiplied);
    blurred.fill(Qt::transparent);
    for (int y = 0; y < h; ++y)
    {
        auto *line = reinterpret_cast<QRgb *>(blurred.scanLine(y));
        for (int x = 0; x < w; ++x)
        {
            const int a = src[static_cast<size_t>(y * w + x)];
            line[x] = qRgba(0, 0, 0, a);
        }
    }
    return blurred;
}

void drawTintedAlphaImage(QPainter& painter,
                          const QImage& alpha,
                          const QColor& tint,
                          int maxAlpha)
{
    if (alpha.isNull() || maxAlpha <= 0)
    {
        return;
    }

    QImage tinted(alpha.size(), QImage::Format_ARGB32_Premultiplied);
    tinted.fill(Qt::transparent);
    for (int y = 0; y < alpha.height(); ++y)
    {
        const auto *srcLine = reinterpret_cast<const QRgb *>(alpha.constScanLine(y));
        auto *dstLine = reinterpret_cast<QRgb *>(tinted.scanLine(y));
        for (int x = 0; x < alpha.width(); ++x)
        {
            const int a = (qAlpha(srcLine[x]) * maxAlpha) / 255;
            dstLine[x] = qRgba(tint.red(), tint.green(), tint.blue(), a);
        }
    }
    painter.drawImage(QPoint(0, 0), tinted);
}
}

SingleLevelPopupMenuRow::SingleLevelPopupMenuRow(QWidget *parent)
    : QWidget(parent)
    , text_label_(new QLabel(this))
    , check_label_(new QLabel(this))
{
    setObjectName(QStringLiteral("singleLevelPopupMenuRow"));
    setAttribute(Qt::WA_Hover, true);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::NoFocus);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    text_label_->setObjectName(QStringLiteral("singleLevelPopupMenuText"));
    text_label_->setMargin(0);
    text_label_->setIndent(0);
    text_label_->setAttribute(Qt::WA_TransparentForMouseEvents, true);

    check_label_->setObjectName(QStringLiteral("singleLevelPopupMenuCheck"));
    check_label_->setAlignment(Qt::AlignCenter);
    check_label_->setMargin(0);
    check_label_->setIndent(0);
    check_label_->setAttribute(Qt::WA_TransparentForMouseEvents, true);

    setProperty("textAlignment", QStringLiteral("left"));
    setProperty("checkIconAlignment", QStringLiteral("right"));
    setProperty("selected", false);
    setProperty("hasCheckIcon", false);
    setProperty("hovered", false);

    refreshTheme();
    layoutChildren();
}

QString SingleLevelPopupMenuRow::text() const
{
    return text_label_->text();
}

void SingleLevelPopupMenuRow::setText(const QString& text)
{
    text_label_->setText(text);
    updateGeometry();
    layoutChildren();
}

void SingleLevelPopupMenuRow::setChecked(bool checked)
{
    checked_ = checked;
    setProperty("selected", checked_);
    setProperty("hasCheckIcon", checked_ && !check_icon_.isNull());
    updateCheckIcon();
    update();
}

bool SingleLevelPopupMenuRow::isChecked() const
{
    return checked_;
}

void SingleLevelPopupMenuRow::setCheckIcon(const QIcon& icon)
{
    check_icon_ = icon;
    setProperty("hasCheckIcon", checked_ && !check_icon_.isNull());
    updateCheckIcon();
}

void SingleLevelPopupMenuRow::setCheckIconSize(const QSize& size)
{
    if (size.isValid() && !size.isEmpty())
    {
        check_icon_size_ = size;
        updateCheckIcon();
    }
}

void SingleLevelPopupMenuRow::setTextAlignment(SingleLevelPopupTextAlignment alignment)
{
    text_alignment_ = alignment;
    text_label_->setAlignment((alignment == SingleLevelPopupTextAlignment::Center ? Qt::AlignCenter
                                                                                  : Qt::AlignLeft | Qt::AlignVCenter));
    setProperty("textAlignment", alignment == SingleLevelPopupTextAlignment::Center ? QStringLiteral("center")
                                                                                   : QStringLiteral("left"));
    layoutChildren();
}

void SingleLevelPopupMenuRow::setTextFixedWidth(int width)
{
    text_fixed_width_ = std::max(0, width);
    layoutChildren();
    updateGeometry();
}

void SingleLevelPopupMenuRow::setCheckSlotWidth(int width)
{
    check_slot_width_ = std::max(0, width);
    layoutChildren();
    updateGeometry();
}

void SingleLevelPopupMenuRow::setHorizontalPadding(int left, int right)
{
    left_padding_ = std::max(0, left);
    right_padding_ = std::max(0, right);
    layoutChildren();
    updateGeometry();
}

void SingleLevelPopupMenuRow::setRowSpacing(int spacing)
{
    row_spacing_ = std::max(0, spacing);
    layoutChildren();
    updateGeometry();
}

void SingleLevelPopupMenuRow::setRowHeight(int height)
{
    row_height_ = std::max(1, height);
    setFixedHeight(row_height_);
    layoutChildren();
    updateGeometry();
}

void SingleLevelPopupMenuRow::setMinimumRowWidth(int width)
{
    minimum_row_width_ = std::max(1, width);
    setMinimumWidth(minimum_row_width_);
    if (this->width() < minimum_row_width_)
    {
        resize(minimum_row_width_, height() > 0 ? height() : row_height_);
    }
    layoutChildren();
    updateGeometry();
}

void SingleLevelPopupMenuRow::setCloseOnClick(bool close)
{
    close_on_click_ = close;
}

bool SingleLevelPopupMenuRow::closeOnClick() const
{
    return close_on_click_;
}

QLabel *SingleLevelPopupMenuRow::textLabel() const
{
    return text_label_;
}

QLabel *SingleLevelPopupMenuRow::checkLabel() const
{
    return check_label_;
}

void SingleLevelPopupMenuRow::clearHover()
{
    setHovered(false);
}

void SingleLevelPopupMenuRow::refreshTheme()
{
    updateLabelStyles();
    updateCheckIcon();
    update();
}

QSize SingleLevelPopupMenuRow::sizeHint() const
{
    const QFontMetrics metrics(text_label_->font());
    const int textWidth = text_fixed_width_ > 0 ? text_fixed_width_ : metrics.horizontalAdvance(text_label_->text());
    const int width = std::max(minimum_row_width_,
                               left_padding_ + textWidth + row_spacing_ + check_slot_width_ + right_padding_);
    return QSize(width, row_height_);
}

QSize SingleLevelPopupMenuRow::minimumSizeHint() const
{
    return sizeHint();
}

void SingleLevelPopupMenuRow::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event && (event->type() == QEvent::FontChange ||
                  event->type() == QEvent::PaletteChange ||
                  event->type() == QEvent::ApplicationPaletteChange ||
                  event->type() == QEvent::StyleChange ||
                  event->type() == QEvent::EnabledChange))
    {
        refreshTheme();
        layoutChildren();
    }
}

void SingleLevelPopupMenuRow::enterEvent(QEnterEvent *event)
{
    QWidget::enterEvent(event);
    setHovered(true);
}

void SingleLevelPopupMenuRow::leaveEvent(QEvent *event)
{
    QWidget::leaveEvent(event);
    setHovered(false);
}

void SingleLevelPopupMenuRow::mouseReleaseEvent(QMouseEvent *event)
{
    if (isEnabled() && event->button() == Qt::LeftButton && rect().contains(event->position().toPoint()))
    {
        emit clicked();
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void SingleLevelPopupMenuRow::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    if (hovered_ && isEnabled())
    {
        painter.fillRect(rect(), appThemeColor(AppThemeColor::MenuHover, isDarkThemeEnabled()));
    }
}

void SingleLevelPopupMenuRow::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    layoutChildren();
}

void SingleLevelPopupMenuRow::layoutChildren()
{
    const int h = height() > 0 ? height() : row_height_;
    const int checkX = std::max(left_padding_, width() - right_padding_ - check_slot_width_);
    check_label_->setGeometry(checkX, 0, check_slot_width_, h);

    int textX = left_padding_;
    const int availableTextWidth = std::max(0, checkX - row_spacing_ - textX);
    int textWidth = availableTextWidth;
    if (text_alignment_ == SingleLevelPopupTextAlignment::Center)
    {
        textX = left_padding_ + check_slot_width_ + row_spacing_;
        textWidth = std::max(0, width() - textX - right_padding_ - check_slot_width_ - row_spacing_);
    }
    if (text_fixed_width_ > 0 && text_alignment_ != SingleLevelPopupTextAlignment::Center)
    {
        textWidth = std::min(text_fixed_width_, availableTextWidth);
    }
    text_label_->setGeometry(textX, 0, textWidth, h);
}

void SingleLevelPopupMenuRow::updateCheckIcon()
{
    if (!checked_ || check_icon_.isNull())
    {
        check_label_->clear();
        return;
    }
    check_label_->setPixmap(check_icon_.pixmap(check_icon_size_));
}

void SingleLevelPopupMenuRow::updateLabelStyles()
{
    const bool dark = isDarkThemeEnabled();
    const QString textColor = appThemeColorName(isEnabled() ? AppThemeColor::MenuText
                                                           : AppThemeColor::MenuDisabledText,
                                                dark);
    const QString checkColor = appThemeColorName(isEnabled() ? AppThemeColor::MenuCheckText
                                                            : AppThemeColor::MenuDisabledText,
                                                 dark);
    text_label_->setFont(font());
    check_label_->setFont(font());
    text_label_->setStyleSheet(QStringLiteral("QLabel { color: %1; background: transparent; border: none; padding: 0px; margin: 0px; }")
        .arg(textColor));
    check_label_->setStyleSheet(QStringLiteral("QLabel { color: %1; background: transparent; border: none; padding: 0px; margin: 0px; }")
        .arg(checkColor));
    text_label_->setEnabled(isEnabled());
    check_label_->setEnabled(isEnabled());
}

void SingleLevelPopupMenuRow::setHovered(bool hovered)
{
    if (hovered_ == hovered)
    {
        return;
    }
    hovered_ = hovered;
    setProperty("hovered", hovered_);
    update();
}

SingleLevelPopupMenu::SingleLevelPopupMenu(QWidget *parent)
    : QMenu(parent)
{
    corner_radius_ = 10;
    panel_padding_ = 12;
    setObjectName(QStringLiteral("singleLevelPopupMenu"));
    setWindowFlag(Qt::FramelessWindowHint, true);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setAttribute(Qt::WA_StyledBackground, false);
    setAutoFillBackground(false);
    setWindowFlag(Qt::NoDropShadowWindowHint, true);
    refreshTheme();
}

QWidgetAction *SingleLevelPopupMenu::addRow(SingleLevelPopupMenuRow *row)
{
    if (!row)
    {
        return nullptr;
    }

    row->setParent(this);
    auto *action = new QWidgetAction(this);
    action->setText(row->text());
    action->setDefaultWidget(row);
    QMenu::addAction(action);
    connect(row, &SingleLevelPopupMenuRow::clicked, this, [this, row, action]() {
        action->trigger();
        if (row->closeOnClick())
        {
            hide();
        }
    });
    return action;
}

QList<SingleLevelPopupMenuRow *> SingleLevelPopupMenu::rows() const
{
    QList<SingleLevelPopupMenuRow *> result;
    for (QAction *action : actions())
    {
        auto *widgetAction = qobject_cast<QWidgetAction *>(action);
        auto *row = widgetAction ? qobject_cast<SingleLevelPopupMenuRow *>(widgetAction->defaultWidget()) : nullptr;
        if (row)
        {
            result.push_back(row);
        }
    }
    return result;
}

void SingleLevelPopupMenu::setCornerRadius(int radius)
{
    corner_radius_ = std::max(0, radius);
    refreshTheme();
    applyRoundedMask();
}

int SingleLevelPopupMenu::cornerRadius() const
{
    return corner_radius_;
}

void SingleLevelPopupMenu::setPanelPadding(int padding)
{
    panel_padding_ = std::max(0, padding);
    refreshTheme();
    updateGeometry();
}

int SingleLevelPopupMenu::panelPadding() const
{
    return panel_padding_;
}

void SingleLevelPopupMenu::setPanelContentWidth(int width)
{
    setFixedWidth(std::max(1, width) + shadowMargin() * 2);
}

void SingleLevelPopupMenu::refreshTheme()
{
    if (objectName().isEmpty())
    {
        setObjectName(QStringLiteral("singleLevelPopupMenu"));
    }

    const bool dark = isDarkThemeEnabled();
    const int horizontalPadding = panel_padding_ >= 8 ? 0 : panel_padding_;
    const bool floatingPanel = panel_padding_ >= 8;
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_NoSystemBackground, floatingPanel);
    setAttribute(Qt::WA_StyledBackground, !floatingPanel);
    setAutoFillBackground(false);
    const int margin = shadowMargin();
    setContentsMargins(margin, margin, margin, margin);
    setProperty("floatingPanelChrome", floatingPanel);
    setProperty("shadowMargin", margin);

    const QString panelRule = floatingPanel
        ? QStringLiteral("background-color: transparent; border: none; border-radius: %1px; padding: %2px %3px;")
              .arg(corner_radius_)
              .arg(panel_padding_)
              .arg(horizontalPadding)
        : QStringLiteral("background-color: %1; border: 1px solid %2; border-radius: %3px; padding: %4px %5px;")
              .arg(appThemeColorName(AppThemeColor::MenuPanel, dark),
                   appThemeColorName(AppThemeColor::Border, dark))
              .arg(corner_radius_)
              .arg(panel_padding_)
              .arg(horizontalPadding);
    const QString styleSheet = QStringLiteral(
        "QMenu#%1 { %2 }"
        "QMenu#%1::item { background-color: transparent; padding: 0px; margin: 0px; }"
        "QMenu#%1::item:selected { background-color: transparent; }")
        .arg(objectName(), panelRule);
    setStyleSheet(styleSheet);

    for (SingleLevelPopupMenuRow *row : rows())
    {
        row->refreshTheme();
    }
}

void SingleLevelPopupMenu::popupFrom(QWidget *anchor, SingleLevelPopupAnchor anchorEdge, const QPoint& offset)
{
    if (!anchor)
    {
        return;
    }

    refreshTheme();
    clearRowHoverStates();
    ensurePolished();
    adjustSize();
    const QSize popupSize = sizeHint();
    const QRect anchorRect(anchor->mapToGlobal(QPoint(0, 0)), anchor->size());
    const int margin = shadowMargin();
    QPoint popupTopLeft = anchor->mapToGlobal(QPoint(-margin, anchor->height() - margin));
    if (anchorEdge == SingleLevelPopupAnchor::Right)
    {
        popupTopLeft = anchor->mapToGlobal(QPoint(anchor->width() - popupSize.width() + margin,
                                                 anchor->height() - margin));
    }
    if (QScreen *screen = QGuiApplication::screenAt(anchorRect.center()))
    {
        const QRect available = screen->availableGeometry().adjusted(8, 8, -8, -8);
        QRect hostAvailable;
        if (QWidget *hostWindow = anchor->window())
        {
            hostAvailable = QRect(hostWindow->mapToGlobal(QPoint(0, 0)), hostWindow->size()).adjusted(8, 8, -8, -8);
        }
        const bool exceedsScreenBottom = popupTopLeft.y() + popupSize.height() > available.bottom() + 1;
        const bool exceedsHostBottom = hostAvailable.isValid() &&
                                       popupTopLeft.y() + popupSize.height() > hostAvailable.bottom() + 1;
        if (exceedsScreenBottom || exceedsHostBottom)
        {
            const int upwardTop = anchorRect.top() - popupSize.height() + margin;
            if (!hostAvailable.isValid() || upwardTop >= hostAvailable.top())
            {
                popupTopLeft.setY(upwardTop);
            }
        }
        popupTopLeft.setX(std::clamp(popupTopLeft.x(),
                                     available.left(),
                                     std::max(available.left(), available.right() - popupSize.width() + 1)));
        popupTopLeft.setY(std::clamp(popupTopLeft.y(),
                                     available.top(),
                                     std::max(available.top(), available.bottom() - popupSize.height() + 1)));
    }
    popupTopLeft += offset;
    popup(popupTopLeft);
    move(popupTopLeft);
    applyRoundedMask();
}

void SingleLevelPopupMenu::applyRoundedMask()
{
    if (shadowMargin() > 0)
    {
        clearMask();
        setProperty("roundedMaskApplied", false);
        return;
    }

    const QSize menuSize = maskSize();
    if (!menuSize.isValid() || menuSize.isEmpty())
    {
        return;
    }

    QPainterPath path;
    path.addRoundedRect(QRectF(QPointF(0.0, 0.0), QSizeF(menuSize)).adjusted(0.0, 0.0, -1.0, -1.0),
                        corner_radius_,
                        corner_radius_);
    setMask(QRegion(path.toFillPolygon().toPolygon()));
    setProperty("roundedMaskApplied", !mask().isEmpty());
}

void SingleLevelPopupMenu::paintEvent(QPaintEvent *event)
{
    if (shadowMargin() <= 0)
    {
        QMenu::paintEvent(event);
        return;
    }

    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setCompositionMode(QPainter::CompositionMode_Clear);
    painter.fillRect(rect(), Qt::transparent);
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);

    const QRectF panel = panelRect();
    if (!panel.isValid() || panel.isEmpty())
    {
        return;
    }

    const bool dark = isDarkThemeEnabled();
    const QColor shadowTint = QColor(0, 0, 0);
    const QImage softShadow = boxBlurredAlpha(size(),
                                              panel.adjusted(-1.0, 7.0, 1.0, 10.0),
                                              corner_radius_ + 2.0,
                                              9,
                                              3);
    const QImage contactShadow = boxBlurredAlpha(size(),
                                                 panel.adjusted(0.0, 1.0, 0.0, 2.0),
                                                 corner_radius_,
                                                 4,
                                                 2);
    drawTintedAlphaImage(painter, softShadow, shadowTint, dark ? 82 : 38);
    drawTintedAlphaImage(painter, contactShadow, shadowTint, dark ? 48 : 14);

    QPainterPath panelPath;
    panelPath.addRoundedRect(panel, corner_radius_, corner_radius_);
    painter.fillPath(panelPath, dark ? appThemeColor(AppThemeColor::MenuPanel, dark) : QColor(255, 255, 255));
}

void SingleLevelPopupMenu::resizeEvent(QResizeEvent *event)
{
    QMenu::resizeEvent(event);
    syncRowWidths();
    applyRoundedMask();
}

void SingleLevelPopupMenu::showEvent(QShowEvent *event)
{
    QMenu::showEvent(event);
    clearRowHoverStates();
    refreshTheme();
    syncRowWidths();
    applyRoundedMask();
    QTimer::singleShot(0, this, [this]() {
        syncRowWidths();
        applyRoundedMask();
    });
}

int SingleLevelPopupMenu::shadowMargin() const
{
    return panel_padding_ >= 8 ? kMenuShadowMargin : 0;
}

QRect SingleLevelPopupMenu::panelRect() const
{
    const int margin = shadowMargin();
    return rect().adjusted(margin, margin, -margin, -margin);
}

QSize SingleLevelPopupMenu::maskSize() const
{
    QSize menuSize = size();
    if (!menuSize.isValid() || menuSize.isEmpty())
    {
        menuSize = sizeHint();
    }
    return menuSize;
}

void SingleLevelPopupMenu::syncRowWidths()
{
    if (panel_padding_ < 8)
    {
        return;
    }
    if (!isVisible())
    {
        return;
    }
    const int availableWidth = std::max(1, panelRect().width());
    for (SingleLevelPopupMenuRow *row : rows())
    {
        if (!row)
        {
            continue;
        }
        if (row->width() != availableWidth ||
            row->minimumWidth() != availableWidth ||
            row->maximumWidth() != availableWidth)
        {
            row->setMinimumRowWidth(availableWidth);
            row->setFixedWidth(availableWidth);
        }
    }
}

void SingleLevelPopupMenu::clearRowHoverStates()
{
    for (SingleLevelPopupMenuRow *row : rows())
    {
        if (row)
        {
            row->clearHover();
        }
    }
}

}
