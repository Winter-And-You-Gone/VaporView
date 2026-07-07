#include "SingleLevelPopupMenu.h"

#include "AppTheme.h"

#include <QApplication>
#include <QEvent>
#include <QFontMetrics>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QRegion>
#include <QResizeEvent>
#include <QSizePolicy>
#include <QShowEvent>
#include <QStyle>
#include <QTimer>
#include <QWidgetAction>

#include <algorithm>

namespace VaporView
{
namespace
{
constexpr int kMenuShadowMargin = 14;
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
    hovered_ = true;
    update();
}

void SingleLevelPopupMenuRow::leaveEvent(QEvent *event)
{
    QWidget::leaveEvent(event);
    hovered_ = false;
    update();
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
    int textWidth = std::max(0, checkX - row_spacing_ - textX);
    if (text_alignment_ == SingleLevelPopupTextAlignment::Center)
    {
        textX = left_padding_ + check_slot_width_ + row_spacing_;
        textWidth = std::max(0, width() - textX - right_padding_ - check_slot_width_ - row_spacing_);
    }
    if (text_fixed_width_ > 0 && text_alignment_ != SingleLevelPopupTextAlignment::Center)
    {
        textWidth = std::min(text_fixed_width_, std::max(0, width() - textX - right_padding_));
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

SingleLevelPopupMenu::SingleLevelPopupMenu(QWidget *parent)
    : QMenu(parent)
{
    setObjectName(QStringLiteral("singleLevelPopupMenu"));
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_StyledBackground, true);
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

void SingleLevelPopupMenu::refreshTheme()
{
    if (objectName().isEmpty())
    {
        setObjectName(QStringLiteral("singleLevelPopupMenu"));
    }

    const bool dark = isDarkThemeEnabled();
    const int horizontalPadding = panel_padding_ >= 8 ? 0 : panel_padding_;
    const int chromeMargin = shadowMargin();
    setContentsMargins(chromeMargin, chromeMargin, chromeMargin, chromeMargin);
    setProperty("floatingPanelChrome", chromeMargin > 0);
    setProperty("shadowMargin", chromeMargin);

    const QString panelRule = chromeMargin > 0
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
    ensurePolished();
    adjustSize();
    const QSize popupSize = sizeHint();
    QPoint popupTopLeft = anchor->mapToGlobal(QPoint(-shadowMargin(), anchor->height() - shadowMargin()));
    if (anchorEdge == SingleLevelPopupAnchor::Right)
    {
        popupTopLeft = anchor->mapToGlobal(QPoint(anchor->width() - popupSize.width() + shadowMargin(),
                                                 anchor->height() - shadowMargin()));
    }
    popupTopLeft += offset;
    popup(popupTopLeft);
    move(popupTopLeft);
    applyRoundedMask();
}

void SingleLevelPopupMenu::applyRoundedMask()
{
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

void SingleLevelPopupMenu::resizeEvent(QResizeEvent *event)
{
    QMenu::resizeEvent(event);
    syncRowWidths();
    applyRoundedMask();
}

void SingleLevelPopupMenu::showEvent(QShowEvent *event)
{
    QMenu::showEvent(event);
    refreshTheme();
    syncRowWidths();
    applyRoundedMask();
    QTimer::singleShot(0, this, [this]() {
        syncRowWidths();
        applyRoundedMask();
    });
}

void SingleLevelPopupMenu::paintEvent(QPaintEvent *event)
{
    if (shadowMargin() <= 0)
    {
        QMenu::paintEvent(event);
        return;
    }

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRectF panel = panelRect();
    if (!panel.isValid() || panel.isEmpty())
    {
        return;
    }

    const bool dark = isDarkThemeEnabled();
    const QColor shadowA = dark ? QColor(0, 0, 0, 115) : QColor(15, 23, 42, 34);
    const QColor shadowB = dark ? QColor(0, 0, 0, 70) : QColor(15, 23, 42, 22);
    const QColor shadowC = dark ? QColor(0, 0, 0, 42) : QColor(15, 23, 42, 14);

    auto drawLayer = [&](const QRectF& rect, const QColor& color, qreal radius) {
        QPainterPath layer;
        layer.addRoundedRect(rect, radius, radius);
        painter.fillPath(layer, color);
    };

    drawLayer(panel.adjusted(-1.0, 5.0, 1.0, 7.0), shadowC, corner_radius_ + 3.0);
    drawLayer(panel.adjusted(-1.0, 2.0, 1.0, 4.0), shadowB, corner_radius_ + 2.0);
    drawLayer(panel.adjusted(0.0, 1.0, 0.0, 2.0), shadowA, corner_radius_ + 1.0);

    QPainterPath panelPath;
    panelPath.addRoundedRect(panel, corner_radius_, corner_radius_);
    painter.fillPath(panelPath, dark ? appThemeColor(AppThemeColor::MenuPanel, dark) : QColor(255, 255, 255));
}

int SingleLevelPopupMenu::shadowMargin() const
{
    return panel_padding_ >= 8 ? kMenuShadowMargin : 0;
}

QRect SingleLevelPopupMenu::panelRect() const
{
    return rect().adjusted(kMenuShadowMargin,
                           kMenuShadowMargin,
                           -kMenuShadowMargin,
                           -kMenuShadowMargin);
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

}
