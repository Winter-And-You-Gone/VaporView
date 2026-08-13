#include "shared/theme/SingleLevelPopupMenu.h"

#include "shared/theme/AppTheme.h"

#include <QAction>
#include <QApplication>
#include <QEvent>
#include <QFontMetrics>
#include <QFocusEvent>
#include <QGuiApplication>
#include <QHideEvent>
#include <QKeyEvent>
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
constexpr int kPopupBoundaryMargin = 8;
constexpr const char *kDefaultRowObjectName = "singleLevelPopupMenuRow";

QString accessibleNameFromText(QString text)
{
    text.remove(QLatin1Char('&'));
    return text.simplified();
}

QWidget *popupHostWindow(const QWidget *popup, QWidget *anchor)
{
    QWidget *hostCandidate = anchor;
    if (!hostCandidate && popup)
    {
        hostCandidate = popup->parentWidget();
    }
    QWidget *hostWindow = hostCandidate ? hostCandidate->window() : nullptr;
    return hostWindow && hostWindow != popup ? hostWindow : nullptr;
}

QRect popupAvailableGeometry(const QWidget *popup, QWidget *anchor)
{
    const QPoint referencePoint = anchor
        ? anchor->mapToGlobal(anchor->rect().center())
        : popup ? popup->geometry().center() : QPoint();
    QScreen *screen = QGuiApplication::screenAt(referencePoint);
    QRect available = screen
        ? screen->availableGeometry().adjusted(kPopupBoundaryMargin,
                                               kPopupBoundaryMargin,
                                               -kPopupBoundaryMargin,
                                               -kPopupBoundaryMargin)
        : QRect();

    if (QWidget *hostWindow = popupHostWindow(popup, anchor))
    {
        const QRect hostAvailable(
            hostWindow->mapToGlobal(hostWindow->rect().topLeft()),
            hostWindow->rect().size());
        const QRect insetHostAvailable = hostAvailable.adjusted(kPopupBoundaryMargin,
                                                                 kPopupBoundaryMargin,
                                                                 -kPopupBoundaryMargin,
                                                                 -kPopupBoundaryMargin);
        if (available.isValid())
        {
            const QRect sharedAvailable = available.intersected(insetHostAvailable);
            if (sharedAvailable.isValid())
            {
                available = sharedAvailable;
            }
        }
        else
        {
            available = insetHostAvailable;
        }
    }
    return available;
}

QPoint constrainedPopupTopLeft(const QWidget *popup,
                               QWidget *anchor,
                               const QPoint& desiredTopLeft,
                               const QSize& popupSize)
{
    const QRect available = popupAvailableGeometry(popup, anchor);
    if (!available.isValid() || !popupSize.isValid())
    {
        return desiredTopLeft;
    }

    return QPoint(
        std::clamp(desiredTopLeft.x(),
                   available.left(),
                   std::max(available.left(), available.right() - popupSize.width() + 1)),
        std::clamp(desiredTopLeft.y(),
                   available.top(),
                   std::max(available.top(), available.bottom() - popupSize.height() + 1)));
}

void constrainPopupToHostWindow(QWidget *popup)
{
    if (!popup)
    {
        return;
    }
    const QPoint constrained = constrainedPopupTopLeft(popup, nullptr, popup->pos(), popup->size());
    if (constrained != popup->pos())
    {
        popup->move(constrained);
    }
}

QImage boxBlurredAlpha(const QSize& size,
                       const QRectF& sourceRect,
                       qreal radius,
                       int horizontalBlurRadius,
                       int verticalBlurRadius,
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
    if (w <= 0 || h <= 0 || horizontalBlurRadius <= 0 || verticalBlurRadius <= 0)
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

    const int horizontalDiameter = horizontalBlurRadius * 2 + 1;
    const int verticalDiameter = verticalBlurRadius * 2 + 1;
    for (int pass = 0; pass < iterations; ++pass)
    {
        for (int y = 0; y < h; ++y)
        {
            int sum = 0;
            for (int x = -horizontalBlurRadius; x <= horizontalBlurRadius; ++x)
            {
                const int cx = std::clamp(x, 0, w - 1);
                sum += src[static_cast<size_t>(y * w + cx)];
            }
            for (int x = 0; x < w; ++x)
            {
                tmp[static_cast<size_t>(y * w + x)] = static_cast<uchar>(sum / horizontalDiameter);
                const int removeX = std::clamp(x - horizontalBlurRadius, 0, w - 1);
                const int addX = std::clamp(x + horizontalBlurRadius + 1, 0, w - 1);
                sum += src[static_cast<size_t>(y * w + addX)] - src[static_cast<size_t>(y * w + removeX)];
            }
        }

        for (int x = 0; x < w; ++x)
        {
            int sum = 0;
            for (int y = -verticalBlurRadius; y <= verticalBlurRadius; ++y)
            {
                const int cy = std::clamp(y, 0, h - 1);
                sum += tmp[static_cast<size_t>(cy * w + x)];
            }
            for (int y = 0; y < h; ++y)
            {
                dst[static_cast<size_t>(y * w + x)] = static_cast<uchar>(sum / verticalDiameter);
                const int removeY = std::clamp(y - verticalBlurRadius, 0, h - 1);
                const int addY = std::clamp(y + verticalBlurRadius + 1, 0, h - 1);
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
    : QToolButton(parent)
    , text_label_(new QLabel(this))
    , check_label_(new QLabel(this))
{
    setObjectName(QString::fromLatin1(kDefaultRowObjectName));
    setAttribute(Qt::WA_Hover, true);
    setAttribute(Qt::WA_StyledBackground, false);
    setAutoFillBackground(false);
    setAutoRaise(false);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::StrongFocus);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    setStyleSheet(QStringLiteral(
        "QToolButton { background: transparent; border: none; padding: 0px; margin: 0px; }"
        "QToolButton:focus { outline: none; }"
        "QToolButton::menu-indicator { image: none; width: 0px; }"));

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
    setProperty("keyboardFocus", false);
    setProperty("menuRow", true);

    connect(this, &QToolButton::toggled, this, &SingleLevelPopupMenuRow::updateCheckedState);
    connect(this, &QToolButton::pressed, this, [this]() {
        update();
    });
    connect(this, &QToolButton::released, this, [this]() {
        update();
    });

    refreshTheme();
    layoutChildren();
}

QString SingleLevelPopupMenuRow::text() const
{
    return text_label_->text();
}

void SingleLevelPopupMenuRow::setText(const QString& text)
{
    if (!syncing_from_action_)
    {
        if (QAction *action = defaultAction())
        {
            if (action->text() != text)
            {
                action->setText(text);
            }
        }
    }
    QToolButton::setText(text);
    text_label_->setText(text);
    const QString accessible = accessibleNameFromText(text);
    if (!accessible.isEmpty())
    {
        setAccessibleName(accessible);
    }
    updateGeometry();
    layoutChildren();
}

void SingleLevelPopupMenuRow::setChecked(bool checked)
{
    if (!syncing_from_action_)
    {
        if (QAction *action = defaultAction())
        {
            if (action->isCheckable() && action->isChecked() != checked)
            {
                action->setChecked(checked);
            }
        }
    }
    if (isCheckable())
    {
        QToolButton::setChecked(checked);
    }
    updateCheckedState(checked);
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

void SingleLevelPopupMenuRow::syncFromDefaultAction()
{
    QAction *action = defaultAction();
    if (!action)
    {
        updateCheckedState(isChecked());
        return;
    }

    syncing_from_action_ = true;

    if (!action->objectName().isEmpty() && objectName() != action->objectName())
    {
        setObjectName(action->objectName());
    }

    const QString actionText = action->text();
    if (!actionText.isEmpty() || text_label_->text().isEmpty())
    {
        QToolButton::setText(actionText);
        text_label_->setText(actionText);
        const QString accessible = accessibleNameFromText(actionText);
        if (!accessible.isEmpty())
        {
            setAccessibleName(accessible);
        }
    }
    if (!action->toolTip().isEmpty())
    {
        setToolTip(action->toolTip());
        setAccessibleDescription(action->toolTip());
    }
    else if (!action->statusTip().isEmpty())
    {
        setAccessibleDescription(action->statusTip());
    }

    setIcon(action->icon());
    setEnabled(action->isEnabled());
    setVisible(action->isVisible());
    setCheckable(action->isCheckable());
    if (action->isCheckable())
    {
        QToolButton::setChecked(action->isChecked());
        updateCheckedState(action->isChecked());
    }
    else
    {
        updateCheckedState(checked_);
    }

    syncing_from_action_ = false;
    updateGeometry();
    layoutChildren();
    update();
}

void SingleLevelPopupMenuRow::setKeyboardFocusHighlight(bool active)
{
    if (keyboard_focus_highlight_ == active)
    {
        return;
    }
    keyboard_focus_highlight_ = active;
    setProperty("keyboardFocus", keyboard_focus_highlight_);
    update();
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
    QToolButton::changeEvent(event);
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
    QToolButton::enterEvent(event);
    setHovered(true);
}

void SingleLevelPopupMenuRow::focusInEvent(QFocusEvent *event)
{
    QToolButton::focusInEvent(event);
    setKeyboardFocusHighlight(event && event->reason() != Qt::MouseFocusReason);
}

void SingleLevelPopupMenuRow::focusOutEvent(QFocusEvent *event)
{
    QToolButton::focusOutEvent(event);
    setKeyboardFocusHighlight(false);
}

void SingleLevelPopupMenuRow::leaveEvent(QEvent *event)
{
    QToolButton::leaveEvent(event);
    setHovered(false);
}

void SingleLevelPopupMenuRow::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const bool active = isEnabled();
    if (active && (hovered_ || keyboard_focus_highlight_ || isDown()))
    {
        QColor fill = appThemeColor(AppThemeColor::MenuHover, isDarkThemeEnabled());
        if (isDown())
        {
            fill = fill.darker(isDarkThemeEnabled() ? 118 : 104);
        }
        painter.fillRect(rect(), fill);
    }
}

void SingleLevelPopupMenuRow::resizeEvent(QResizeEvent *event)
{
    QToolButton::resizeEvent(event);
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

void SingleLevelPopupMenuRow::updateCheckedState(bool checked)
{
    checked_ = checked;
    setProperty("selected", checked_);
    setProperty("hasCheckIcon", checked_ && !check_icon_.isNull());
    updateCheckIcon();
    update();
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
    if (action->objectName().isEmpty() &&
        !row->objectName().isEmpty() &&
        row->objectName() != QString::fromLatin1(kDefaultRowObjectName))
    {
        action->setObjectName(row->objectName());
    }
    action->setDefaultWidget(row);
    row->setDefaultAction(action);
    row->installEventFilter(this);
    QMenu::addAction(action);

    connect(action, &QAction::changed, row, &SingleLevelPopupMenuRow::syncFromDefaultAction);
    connect(action, &QObject::objectNameChanged, row, [row]() {
        row->syncFromDefaultAction();
    });
    connect(row, &QToolButton::pressed, this, [this, row]() {
        if (!row->closeOnClick())
        {
            prepareKeepOpenAfterTrigger(row);
        }
    });
    QPointer<SingleLevelPopupMenuRow> rowGuard(row);
    connect(action, &QObject::destroyed, this, [rowGuard]() {
        if (rowGuard)
        {
            rowGuard->setDefaultAction(nullptr);
            rowGuard->syncFromDefaultAction();
            rowGuard->deleteLater();
        }
    });
    connect(action, &QAction::triggered, this, [this, row]() {
        if (row->closeOnClick())
        {
            keep_open_after_trigger_ = false;
            keep_open_focus_row_.clear();
            hide();
            return;
        }
        prepareKeepOpenAfterTrigger(row);
        QTimer::singleShot(0, this, &SingleLevelPopupMenu::restoreKeptOpenPopup);
    });
    row->syncFromDefaultAction();
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

void SingleLevelPopupMenu::setShadowMargin(int margin)
{
    shadow_margin_ = std::max(0, margin);
    refreshTheme();
    updateGeometry();
}

void SingleLevelPopupMenu::setBottomShadowMargin(int margin)
{
    bottom_shadow_margin_ = std::max(0, margin);
    refreshTheme();
    updateGeometry();
}

int SingleLevelPopupMenu::shadowMargin() const
{
    return panel_padding_ >= 8 ? shadow_margin_ : 0;
}

int SingleLevelPopupMenu::bottomShadowMargin() const
{
    return panel_padding_ >= 8 ? bottom_shadow_margin_ : 0;
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
    const int bottomMargin = bottomShadowMargin();
    setContentsMargins(margin, margin, margin, bottomMargin);
    setProperty("floatingPanelChrome", floatingPanel);
    setProperty("shadowMargin", margin);
    setProperty("shadowBottomMargin", bottomMargin);

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

    focus_restore_widget_ = anchor;
    refreshTheme();
    clearRowHoverStates();
    ensurePolished();
    adjustSize();
    const QSize popupSize = sizeHint();
    const QRect anchorRect(anchor->mapToGlobal(QPoint(0, 0)), anchor->size());
    const int margin = shadowMargin();
    const int bottomMargin = bottomShadowMargin();
    QPoint popupTopLeft = anchor->mapToGlobal(QPoint(-margin, anchor->height() - margin));
    if (anchorEdge == SingleLevelPopupAnchor::Right)
    {
        popupTopLeft = anchor->mapToGlobal(QPoint(anchor->width() - popupSize.width() + margin,
                                                 anchor->height() - margin));
    }
    const QRect available = popupAvailableGeometry(this, anchor);
    if (available.isValid())
    {
        const bool exceedsBottom = popupTopLeft.y() + popupSize.height() > available.bottom() + 1;
        if (exceedsBottom)
        {
            const int upwardTop = anchorRect.top() - popupSize.height() + bottomMargin;
            if (upwardTop >= available.top())
            {
                popupTopLeft.setY(upwardTop);
            }
        }
    }
    popupTopLeft += offset;
    popupTopLeft = constrainedPopupTopLeft(this, anchor, popupTopLeft, popupSize);
    popup(popupTopLeft);
    move(popupTopLeft);
    applyRoundedMask();
}

void SingleLevelPopupMenu::applyRoundedMask()
{
    if (shadowMargin() > 0 || bottomShadowMargin() > 0)
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
    if (shadowMargin() <= 0 && bottomShadowMargin() <= 0)
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
    const bool extendedBottomShadow = bottomShadowMargin() > shadowMargin();
    const QImage softShadow = boxBlurredAlpha(size(),
                                              panel.adjusted(-1.0, 7.0, 1.0, 0.0),
                                              corner_radius_ + 2.0,
                                              extendedBottomShadow ? 7 : 9,
                                              extendedBottomShadow ? 14 : 9,
                                              3);
    const QImage contactShadow = boxBlurredAlpha(size(),
                                                 panel.adjusted(0.0, 1.0, 0.0, 0.0),
                                                 corner_radius_,
                                                 extendedBottomShadow ? 3 : 4,
                                                 extendedBottomShadow ? 7 : 4,
                                                 2);
    drawTintedAlphaImage(painter, softShadow, shadowTint, dark ? 82 : 38);
    drawTintedAlphaImage(painter, contactShadow, shadowTint, dark ? 48 : 14);

    QPainterPath panelPath;
    panelPath.addRoundedRect(panel, corner_radius_, corner_radius_);
    painter.fillPath(panelPath, dark ? appThemeColor(AppThemeColor::MenuPanel, dark) : QColor(255, 255, 255));
}

bool SingleLevelPopupMenu::eventFilter(QObject *object, QEvent *event)
{
    auto *row = qobject_cast<SingleLevelPopupMenuRow *>(object);
    if (row && event && event->type() == QEvent::KeyPress)
    {
        if (handleNavigationKey(static_cast<QKeyEvent *>(event), row))
        {
            return true;
        }
    }
    return QMenu::eventFilter(object, event);
}

void SingleLevelPopupMenu::hideEvent(QHideEvent *event)
{
    const bool keepOpenAfterTrigger = keep_open_after_trigger_;
    clearRowHoverStates();
    for (SingleLevelPopupMenuRow *row : rows())
    {
        if (row)
        {
            row->setKeyboardFocusHighlight(false);
        }
    }

    if (keepOpenAfterTrigger)
    {
        QMenu::hideEvent(event);
        QTimer::singleShot(0, this, &SingleLevelPopupMenu::restoreKeptOpenPopup);
        return;
    }

    QPointer<QWidget> restoreTarget = focus_restore_widget_;
    focus_restore_widget_.clear();
    QMenu::hideEvent(event);

    if (restoreTarget && restoreTarget->isVisible() && restoreTarget->isEnabled())
    {
        QTimer::singleShot(0, restoreTarget, [restoreTarget]() {
            if (restoreTarget && restoreTarget->isVisible() && restoreTarget->isEnabled())
            {
                restoreTarget->setFocus(Qt::PopupFocusReason);
            }
        });
    }
}

void SingleLevelPopupMenu::keyPressEvent(QKeyEvent *event)
{
    if (handleNavigationKey(event, qobject_cast<SingleLevelPopupMenuRow *>(QApplication::focusWidget())))
    {
        return;
    }
    QMenu::keyPressEvent(event);
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
    setFocus(Qt::PopupFocusReason);
    clearRowHoverStates();
    refreshTheme();
    syncRowWidths();
    applyRoundedMask();
    constrainPopupToHostWindow(this);
    QTimer::singleShot(0, this, [this]() {
        syncRowWidths();
        applyRoundedMask();
        constrainPopupToHostWindow(this);
    });
}

QRect SingleLevelPopupMenu::panelRect() const
{
    const int margin = shadowMargin();
    return rect().adjusted(margin, margin, -margin, -bottomShadowMargin());
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

QList<SingleLevelPopupMenuRow *> SingleLevelPopupMenu::focusableRows() const
{
    QList<SingleLevelPopupMenuRow *> result;
    for (SingleLevelPopupMenuRow *row : rows())
    {
        if (row && row->isVisible() && row->isEnabled())
        {
            result.push_back(row);
        }
    }
    return result;
}

void SingleLevelPopupMenu::focusRow(SingleLevelPopupMenuRow *targetRow, Qt::FocusReason reason)
{
    if (!targetRow)
    {
        return;
    }
    for (SingleLevelPopupMenuRow *row : rows())
    {
        if (row)
        {
            row->setKeyboardFocusHighlight(row == targetRow);
        }
    }
    targetRow->setFocus(reason);
}

bool SingleLevelPopupMenu::handleNavigationKey(QKeyEvent *event, SingleLevelPopupMenuRow *sourceRow)
{
    if (!event)
    {
        return false;
    }

    const QList<SingleLevelPopupMenuRow *> candidates = focusableRows();
    auto focusCandidate = [&]() -> SingleLevelPopupMenuRow * {
        if (sourceRow && candidates.contains(sourceRow))
        {
            return sourceRow;
        }
        for (SingleLevelPopupMenuRow *row : candidates)
        {
            if (row && row->hasFocus())
            {
                return row;
            }
        }
        return nullptr;
    };
    auto focusByOffset = [&](int offset) {
        if (candidates.isEmpty())
        {
            return;
        }
        SingleLevelPopupMenuRow *current = focusCandidate();
        int index = candidates.indexOf(current);
        const int count = candidates.size();
        if (index < 0)
        {
            focusRow(candidates.at(offset >= 0 ? 0 : count - 1), Qt::OtherFocusReason);
            return;
        }
        const int nextIndex = (index + offset + count) % count;
        focusRow(candidates.at(nextIndex), Qt::OtherFocusReason);
    };

    switch (event->key())
    {
    case Qt::Key_Down:
        focusByOffset(1);
        event->accept();
        return true;
    case Qt::Key_Up:
        focusByOffset(-1);
        event->accept();
        return true;
    case Qt::Key_Home:
        if (!candidates.isEmpty())
        {
            focusRow(candidates.first(), Qt::OtherFocusReason);
        }
        event->accept();
        return true;
    case Qt::Key_End:
        if (!candidates.isEmpty())
        {
            focusRow(candidates.last(), Qt::OtherFocusReason);
        }
        event->accept();
        return true;
    case Qt::Key_Return:
    case Qt::Key_Enter:
    case Qt::Key_Space:
        if (SingleLevelPopupMenuRow *row = focusCandidate())
        {
            row->click();
        }
        event->accept();
        return true;
    case Qt::Key_Escape:
        hide();
        event->accept();
        return true;
    case Qt::Key_Tab:
    case Qt::Key_Backtab:
        hide();
        event->accept();
        return true;
    case Qt::Key_Right:
        if (SingleLevelPopupMenuRow *row = focusCandidate())
        {
            if (QAction *action = row->defaultAction())
            {
                if (auto *submenu = qobject_cast<SingleLevelPopupMenu *>(action->menu()))
                {
                    submenu->popupFrom(row, SingleLevelPopupAnchor::Right);
                    event->accept();
                    return true;
                }
            }
        }
        return false;
    case Qt::Key_Left:
        hide();
        event->accept();
        return true;
    default:
        return false;
    }
}

void SingleLevelPopupMenu::prepareKeepOpenAfterTrigger(SingleLevelPopupMenuRow *row)
{
    keep_open_after_trigger_ = true;
    keep_open_position_ = pos();
    keep_open_focus_row_ = row;
}

void SingleLevelPopupMenu::restoreKeptOpenPopup()
{
    if (!keep_open_after_trigger_)
    {
        return;
    }

    keep_open_after_trigger_ = false;
    const QPoint restorePosition = keep_open_position_;
    QPointer<SingleLevelPopupMenuRow> focusRowAfterRestore = keep_open_focus_row_;
    keep_open_focus_row_.clear();

    if (!isVisible())
    {
        popup(restorePosition);
        move(restorePosition);
        applyRoundedMask();
        syncRowWidths();
    }
    if (focusRowAfterRestore && focusRowAfterRestore->isVisible() && focusRowAfterRestore->isEnabled())
    {
        focusRow(focusRowAfterRestore, Qt::OtherFocusReason);
    }
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
