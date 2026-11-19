#include "ground/main/GroundMainWindowSupport.h"
#include "ground/widgets/VisualTextLabel.h"
#include "ground/widgets/LabelTextSelection.h"
#include "shared/theme/AppTheme.h"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QCursor>
#include <QEvent>
#include <QFontMetrics>
#include <QFrame>
#include <QGuiApplication>
#include <QHelpEvent>
#include <QHBoxLayout>
#include <QImage>
#include <QKeySequence>
#include <QLabel>
#include <QLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QScreen>
#include <QSplitter>
#include <QStyle>
#include <QStyleOptionButton>
#include <QToolButton>
#include <QToolTip>
#include <QVBoxLayout>
#include <QWindow>

#include <algorithm>
#include <cmath>
#include <functional>
#include <utility>
#include <vector>

namespace VaporView::Ground::MainSupport
{

class MenuItemEventFilter : public QObject
{
public:
    MenuItemEventFilter(std::function<void()> hoverCallback,
                        std::function<void()> clickCallback,
                        QObject *parent)
        : QObject(parent)
        , hover_callback_(std::move(hoverCallback))
        , click_callback_(std::move(clickCallback))
    {
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        const auto *widget = qobject_cast<QWidget *>(watched);
        // A queued pointer event can outlive the menu's visible state.
        // Do not reopen submenus or execute actions after their row is hidden.
        if (!widget || !widget->isVisible() || !widget->isEnabled())
        {
            return false;
        }
        if ((event->type() == QEvent::Enter || event->type() == QEvent::MouseMove) && hover_callback_)
        {
            hover_callback_();
        }
        else if (event->type() == QEvent::MouseButtonRelease && click_callback_)
        {
            auto *mouseEvent = static_cast<QMouseEvent *>(event);
            if (mouseEvent->button() == Qt::LeftButton)
            {
                click_callback_();
                return true;
            }
        }
        return false;
    }

private:
    std::function<void()> hover_callback_;
    std::function<void()> click_callback_;
};

QImage menuBoxBlurredAlpha(const QSize& size,
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
            line[x] = qRgba(0, 0, 0, src[static_cast<size_t>(y * w + x)]);
        }
    }
    return blurred;
}

void drawMenuTintedAlphaImage(QPainter& painter,
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

void paintFloatingMenuShadow(QPainter& painter, const QSize& size, const QRectF& panel, qreal radius, bool dark)
{
    const QImage soft = menuBoxBlurredAlpha(size, panel.adjusted(-1, 7, 1, 10), radius + 2, 9, 3);
    const QImage contact = menuBoxBlurredAlpha(size, panel.adjusted(0, 1, 0, 2), radius, 4, 2);
    drawMenuTintedAlphaImage(painter, soft, Qt::black, dark ? 82 : 38);
    drawMenuTintedAlphaImage(painter, contact, Qt::black, dark ? 48 : 14);
}

class FloatingTitleMenuPanel final : public QFrame
{
public:
    explicit FloatingTitleMenuPanel(QWidget *parent = nullptr)
        : QFrame(parent, Qt::Tool | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint)
    {
        setObjectName(QStringLiteral("floatingTitleMenuPanel"));
        setAttribute(Qt::WA_TranslucentBackground, true);
        setAttribute(Qt::WA_NoSystemBackground, true);
        setAttribute(Qt::WA_StyledBackground, false);
        setAttribute(Qt::WA_ShowWithoutActivating, true);
        setAutoFillBackground(false);
        setFocusPolicy(Qt::StrongFocus);
        setFrameShape(QFrame::NoFrame);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        setWindowModality(Qt::NonModal);
        setContentsMargins(kFloatingMenuShadowMarginPx,
                           kFloatingMenuShadowMarginPx,
                           kFloatingMenuShadowMarginPx,
                           kFloatingMenuShadowMarginPx);
        setProperty("floatingPanelChrome", true);
        setProperty("shadowMargin", kFloatingMenuShadowMarginPx);
        setProperty("cornerRadius", kFloatingMenuCornerRadiusPx);
    }

    QRect contentRect() const
    {
        return rect().adjusted(kFloatingMenuShadowMarginPx,
                               kFloatingMenuShadowMarginPx,
                               -kFloatingMenuShadowMarginPx,
                               -kFloatingMenuShadowMarginPx);
    }

    void setContentFixedSize(const QSize& size)
    {
        content_size_ = size.expandedTo(QSize(0, 0));
        setFixedSize(content_size_ + QSize(kFloatingMenuShadowMarginPx * 2,
                                           kFloatingMenuShadowMarginPx * 2));
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setCompositionMode(QPainter::CompositionMode_Clear);
        painter.fillRect(rect(), Qt::transparent);
        painter.setCompositionMode(QPainter::CompositionMode_SourceOver);

        const QRectF panel = contentRect();
        if (!panel.isValid() || panel.isEmpty())
        {
            return;
        }

        const bool dark = qApp && qApp->property(kAppDarkThemeProperty).toBool();
        paintFloatingMenuShadow(painter, size(), panel, kFloatingMenuCornerRadiusPx, dark);

        QPainterPath panelPath;
        panelPath.addRoundedRect(panel, kFloatingMenuCornerRadiusPx, kFloatingMenuCornerRadiusPx);
        painter.fillPath(panelPath, dark ? appThemeColor(AppThemeColor::MenuPanel, true) : QColor(255, 255, 255));
    }

private:
    QSize content_size_;
};

QRect floatingMenuContentRect(QWidget *panel)
{
    if (auto *floatingPanel = dynamic_cast<FloatingTitleMenuPanel *>(panel))
    {
        return floatingPanel->contentRect();
    }
    return panel ? panel->rect() : QRect();
}

void setFloatingMenuContentFixedSize(QWidget *panel, const QSize& size)
{
    if (auto *floatingPanel = dynamic_cast<FloatingTitleMenuPanel *>(panel))
    {
        floatingPanel->setContentFixedSize(size);
        return;
    }
    if (panel)
    {
        panel->setFixedSize(size);
    }
}

class AppSidebarFrame final : public QFrame
{
public:
    explicit AppSidebarFrame(QWidget *parent = nullptr)
        : QFrame(parent)
    {
    }

    QSize minimumSizeHint() const override
    {
        QSize hint = QFrame::minimumSizeHint();
        hint.setWidth(0);
        return hint;
    }
};

QString shortcutText(const QKeySequence& sequence)
{
    return sequence.isEmpty() ? QString() : sequence.toString(QKeySequence::NativeText);
}

QString shortcutTextFromAction(const QAction *action)
{
    return action ? shortcutText(action->shortcut()) : QString();
}

QString shortcutTextFromWidget(QWidget *widget)
{
    if (!widget)
    {
        return {};
    }

    const QString propertyShortcut = widget->property(kTooltipShortcutProperty).toString().trimmed();
    if (!propertyShortcut.isEmpty())
    {
        return propertyShortcut;
    }

    if (auto *toolButton = qobject_cast<QToolButton *>(widget))
    {
        const QString actionShortcut = shortcutTextFromAction(toolButton->defaultAction());
        if (!actionShortcut.isEmpty())
        {
            return actionShortcut;
        }
    }

    if (auto *button = qobject_cast<QAbstractButton *>(widget))
    {
        const QString buttonShortcut = shortcutText(button->shortcut());
        if (!buttonShortcut.isEmpty())
        {
            return buttonShortcut;
        }
    }

    for (const QAction *action : widget->actions())
    {
        const QString actionShortcut = shortcutTextFromAction(action);
        if (!actionShortcut.isEmpty())
        {
            return actionShortcut;
        }
    }

    return {};
}

void fitButtonMinimumWidth(QAbstractButton *button, int floorWidth)
{
    if (!button)
    {
        return;
    }

    const int iconWidth = button->icon().isNull() ? 0 : button->iconSize().width() + 8;
    const int textWidth = button->fontMetrics().horizontalAdvance(button->text());
    button->setMinimumWidth(std::max(floorWidth, textWidth + iconWidth + 42));
}

void fitButtonFixedWidth(QAbstractButton *button, int floorWidth, int padding)
{
    if (!button)
    {
        return;
    }

    const int iconWidth = button->icon().isNull() ? 0 : button->iconSize().width() + 8;
    const int textWidth = button->fontMetrics().horizontalAdvance(button->text());
    const int width = std::max(floorWidth, textWidth + iconWidth + padding);
    button->setMinimumWidth(width);
    button->setMaximumWidth(width);
}

QString shortcutTextFromTooltipSuffix(QString& text)
{
    static const QRegularExpression suffixPattern(
        QStringLiteral(R"(\s*[(（]([^）)]*(?:Ctrl|Alt|Shift|Meta|Cmd|Esc|Enter|Return|Tab|F\d{1,2})[^）)]*)[)）]\s*$)"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = suffixPattern.match(text);
    if (!match.hasMatch())
    {
        return {};
    }

    text = text.left(match.capturedStart()).trimmed();
    return match.captured(1).trimmed();
}

class AppTooltipPopup final : public QFrame
{
public:
    AppTooltipPopup()
        : QFrame(nullptr, Qt::ToolTip | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint |
                          Qt::WindowTransparentForInput | Qt::WindowDoesNotAcceptFocus)
        , text_label_(new QLabel(this))
        , shortcut_label_(new QLabel(this))
    {
        setObjectName(QStringLiteral("appTooltipPopup"));
        setAttribute(Qt::WA_ShowWithoutActivating, true);
        setAttribute(Qt::WA_TranslucentBackground, true);
        setAttribute(Qt::WA_NoSystemBackground, true);
        setAttribute(Qt::WA_StyledBackground, false);
        setAutoFillBackground(false);
        setFocusPolicy(Qt::NoFocus);

        auto *layout = new QHBoxLayout(this);
        layout->setContentsMargins(16, 8, 16, 9);
        layout->setSpacing(10);

        text_label_->setObjectName(QStringLiteral("appTooltipText"));
        text_label_->setTextFormat(Qt::PlainText);
        text_label_->setAlignment(Qt::AlignVCenter);
        text_label_->setWordWrap(false);
        text_label_->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        layout->addWidget(text_label_);

        shortcut_label_->setObjectName(QStringLiteral("appTooltipShortcut"));
        shortcut_label_->setTextFormat(Qt::PlainText);
        shortcut_label_->setAlignment(Qt::AlignCenter);
        shortcut_label_->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        layout->addWidget(shortcut_label_);
    }

    void togglePinned()
    {
        auto *button = qobject_cast<QAbstractButton *>(sender());
        if (!button || !button->isVisible() || button->toolTip().trimmed().isEmpty())
        {
            return;
        }
        if (pinned_target_ == button)
        {
            // A hover request queued before the click must not reopen the guide.
            dismissed_target_ = button;
            dismiss();
            return;
        }
        disconnect(pinned_target_destroyed_);
        dismissed_target_.clear();
        pinned_target_ = button;
        pinned_target_destroyed_ = connect(button, &QObject::destroyed, this, &AppTooltipPopup::dismiss);
        QToolTip::hideText();
        showFor(button, button->toolTip().trimmed(), {}, isDarkThemeEnabled());
    }

    bool suppressesHover(const QWidget *target) const
    {
        return pinned_target_ || (target && dismissed_target_ == target);
    }

    void dismiss()
    {
        pinned_target_.clear();
        disconnect(pinned_target_destroyed_);
        hide();
    }

    void handleInteraction(QObject *watched, QEvent *event)
    {
        const auto type = event->type();
        if (dismissed_target_ && watched == dismissed_target_.data() &&
            (type == QEvent::Hide || (type == QEvent::Leave &&
             !dismissed_target_->rect().contains(dismissed_target_->mapFromGlobal(QCursor::pos())))))
        {
            dismissed_target_.clear();
        }

        if (pinned_target_)
        {
            if (type == QEvent::Hide && watched == pinned_target_.data())
            {
                dismiss();
            }
            else if ((type == QEvent::Move || type == QEvent::Resize || type == QEvent::ToolTipChange) &&
                     (watched == pinned_target_.data() || watched == pinned_target_->window()))
            {
                showFor(pinned_target_, pinned_target_->toolTip(), {}, isDarkThemeEnabled());
            }
            return;
        }

        if (type == QEvent::Leave || type == QEvent::MouseButtonPress || type == QEvent::KeyPress ||
            type == QEvent::Wheel || type == QEvent::ApplicationDeactivate || type == QEvent::WindowDeactivate)
        {
            hide();
        }
    }

    void showFor(QWidget *target, const QString& text, const QString& shortcut, bool dark, const QRect& anchor = QRect())
    {
        if (!target || text.trimmed().isEmpty())
        {
            hide();
            return;
        }

        const QRect targetRect = anchor.isValid()
            ? QRect(target->mapToGlobal(anchor.topLeft()), anchor.size())
            : QRect(target->mapToGlobal(QPoint(0, 0)), target->size());
        QRect bounds = target->window() ? target->window()->frameGeometry() : QRect();
        if (!bounds.isValid())
        {
            if (QScreen *screen = QGuiApplication::screenAt(targetRect.center()))
            {
                bounds = screen->availableGeometry();
            }
        }
        if (!bounds.isValid())
        {
            if (QScreen *screen = QGuiApplication::primaryScreen())
            {
                bounds = screen->availableGeometry();
            }
        }
        if (!bounds.isValid())
        {
            bounds = QRect(targetRect.center() - QPoint(260, 100), QSize(520, 200));
        }
        bounds.adjust(8, 8, -8, -8);

        dark_ = dark;
        // The title-menu blur extends farther below the panel than to the sides.
        constexpr int tooltipShadowMargin = kFloatingMenuShadowMarginPx + 16;
        shadow_margin_ = target->property("_vv_tooltip_pinnable").toBool() ? tooltipShadowMargin : 0;
        const int margin = shadow_margin_;
        layout()->setContentsMargins(16 + margin, 8 + margin, 16 + margin, 9 + margin);
        const int maxPanelWidth = std::max(160, std::min(520, bounds.width() - 2 * margin));
        text_label_->setMaximumWidth(std::max(90, maxPanelWidth - (shortcut.isEmpty() ? 32 : 128)));
        text_label_->setWordWrap(QFontMetrics(text_label_->font()).horizontalAdvance(text) > text_label_->maximumWidth());
        text_label_->setText(text);
        shortcut_label_->setVisible(!shortcut.isEmpty());
        shortcut_label_->setText(shortcut);

        const QString shortcutBackground = dark ? QStringLiteral("rgb(66, 66, 66)") : QStringLiteral("rgb(232, 232, 232)");
        const QString foreground = dark ? QStringLiteral("#FFFFFF") : QStringLiteral("#000000");
        popup_background_ = dark ? QColor(45, 45, 45) : QColor(253, 253, 252);
        popup_border_ = dark ? QColor(QStringLiteral("#474747")) : QColor(QStringLiteral("#E8E8E8"));
        setStyleSheet(QStringLiteral(
            "QLabel#appTooltipText { background: transparent; color: %1; font-size: 16px; font-weight: 500; }"
            "QLabel#appTooltipShortcut { background-color: %2; color: %1; border: none; border-radius: 11px; padding: 1px 9px 2px 9px; font-size: 15px; font-weight: 500; }")
            .arg(foreground, shortcutBackground));

        adjustSize();
        const QSize popupSize = sizeHint().boundedTo(QSize(maxPanelWidth + 2 * margin, 1000));
        resize(popupSize);

        const int gap = 8;
        int x = targetRect.center().x() - width() / 2;
        int y = targetRect.bottom() + gap - margin;
        if (y + height() > bounds.bottom())
        {
            y = targetRect.top() - height() - gap + margin;
        }

        x = std::clamp(x, bounds.left(), std::max(bounds.left(), bounds.right() - width() + 1));
        y = std::clamp(y, bounds.top(), std::max(bounds.top(), bounds.bottom() - height() + 1));
        move(x, y);
        show();
        raise();
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setCompositionMode(QPainter::CompositionMode_Clear);
        painter.fillRect(rect(), Qt::transparent);
        painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
        constexpr qreal radius = 13.0;
        const qreal inset = shadow_margin_ + 0.5;
        const QRectF roundedRect = QRectF(rect()).adjusted(inset, inset, -inset, -inset);
        if (shadow_margin_ > 0)
        {
            paintFloatingMenuShadow(painter, size(), roundedRect, radius, dark_);
        }
        painter.setPen(QPen(popup_border_, 1.0));
        painter.setBrush(popup_background_);
        painter.drawRoundedRect(roundedRect, radius, radius);
    }

private:
    int shadow_margin_ = 0;
    bool dark_ = false;
    QColor popup_background_ = QColor(253, 253, 252);
    QColor popup_border_ = QColor(QStringLiteral("#E8E8E8"));
    QLabel *text_label_;
    QLabel *shortcut_label_;
    QPointer<QWidget> pinned_target_;
    QPointer<QWidget> dismissed_target_;
    QMetaObject::Connection pinned_target_destroyed_;
};

static AppTooltipPopup *appTooltipPopup(bool create = false)
{
    static AppTooltipPopup *popup = nullptr;
    if (!popup && create)
    {
        popup = new AppTooltipPopup();
    }
    return popup;
}

bool handleAppTooltipEvent(QObject *watched, QEvent *event, bool dark)
{
    if (event->type() == QEvent::Show)
    {
        if (auto *button = qobject_cast<QAbstractButton *>(watched);
            button && button->property("_vv_tooltip_pinnable").toBool())
        {
            QObject::connect(button, &QAbstractButton::clicked,
                             appTooltipPopup(true), &AppTooltipPopup::togglePinned, Qt::UniqueConnection);
        }
    }
    auto *popup = appTooltipPopup();
    if (event->type() != QEvent::ToolTip)
    {
        if (popup)
        {
            popup->handleInteraction(watched, event);
        }
        return false;
    }

    auto *widget = qobject_cast<QWidget *>(watched);
    if (popup && popup->suppressesHover(widget))
    {
        event->accept();
        return true;
    }
    if (!widget || !widget->isVisible())
    {
        return false;
    }

    QString text = widget->toolTip().trimmed();
    if (text.isEmpty())
    {
        return false;
    }

    popup = appTooltipPopup(true);
    QRect anchor;
    const QVariant anchorValue = widget->property("_vv_tooltip_anchor_rect");
    if (anchorValue.isValid() && anchorValue.canConvert<QRect>())
    {
        anchor = anchorValue.toRect();
        if (auto *helpEvent = dynamic_cast<QHelpEvent *>(event);
            anchor.isValid() && helpEvent && !anchor.contains(helpEvent->pos()))
        {
            QToolTip::hideText();
            popup->dismiss();
            event->accept();
            return true;
        }
    }

    QString shortcut = shortcutTextFromWidget(widget);
    const QString suffixShortcut = shortcutTextFromTooltipSuffix(text);
    if (shortcut.isEmpty())
    {
        shortcut = suffixShortcut;
    }

    QToolTip::hideText();
    popup->showFor(widget, text, shortcut, dark, anchor);
    event->accept();
    return true;
}


class MainCardResizeHandle : public QWidget
{
public:
    MainCardResizeHandle(QWidget *targetCard, int minimumTargetHeight, QWidget *parent = nullptr)
        : QWidget(parent)
        , target_card_(targetCard)
        , minimum_target_height_(minimumTargetHeight)
        , drag_start_y_(0)
        , target_start_height_(0)
        , dragging_(false)
    {
        setObjectName(QStringLiteral("mainCardResizeHandle"));
        setAttribute(Qt::WA_Hover, true);
        setCursor(Qt::SizeVerCursor);
        setFixedHeight(kMainCardResizeHandleHeight);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setProperty("dragging", false);
        if (target_card_)
        {
            target_card_->setProperty(kMainCardMinimumHeightProperty, minimum_target_height_);
        }
    }

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() != Qt::LeftButton || !target_card_)
        {
            QWidget::mousePressEvent(event);
            return;
        }

        dragging_ = true;
        drag_start_y_ = dragCursorY();
        target_start_height_ = target_card_->height();
        target_height_ = target_start_height_;
        target_card_->setProperty(kMainCardResizeDraggingProperty, true);
        applyTargetHeight();
        setProperty("dragging", true);
        refreshStyle();
        event->accept();
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (!dragging_ || !target_card_)
        {
            QWidget::mouseMoveEvent(event);
            return;
        }

        const int deltaY = dragCursorY() - drag_start_y_;
        bool ok = false;
        const int propertyMinimum = target_card_->property(kMainCardMinimumHeightProperty).toInt(&ok);
        // Responsive layout can reduce the content minimum after construction.
        const int effectiveMinimum = ok ? std::max(0, propertyMinimum) : minimum_target_height_;
        const int nextHeight = std::max(effectiveMinimum, target_start_height_ + deltaY);
        if (target_height_ != nextHeight)
        {
            target_card_->setProperty(kMainCardUserResizedHeightProperty, true);
            target_height_ = nextHeight;
        }
        applyTargetHeight();
        event->accept();
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && dragging_)
        {
            applyTargetHeight();
            dragging_ = false;
            if (target_card_)
            {
                target_card_->setProperty(kMainCardResizeDraggingProperty, false);
            }
            setProperty("dragging", false);
            refreshStyle();
            event->accept();
            return;
        }

        QWidget::mouseReleaseEvent(event);
    }

private:
    int dragCursorY() const
    {
        const QVariant testCursorY = property(kMainCardResizeTestCursorYProperty);
        return testCursorY.isValid() ? testCursorY.toInt() : QCursor::pos().y();
    }

    void applyTargetHeight()
    {
        if (!target_card_ || target_height_ <= 0)
        {
            return;
        }

        if (auto *splitter = qobject_cast<QSplitter *>(target_card_))
        {
            for (int index = 0; index < splitter->count(); ++index)
            {
                QWidget *card = splitter->widget(index);
                if (card &&
                    (card->minimumHeight() != target_height_ ||
                     card->maximumHeight() != target_height_))
                {
                    card->setFixedHeight(target_height_);
                }
            }
        }
        if (target_card_->minimumHeight() != target_height_ ||
            target_card_->maximumHeight() != target_height_)
        {
            target_card_->setFixedHeight(target_height_);
        }
    }

    void refreshStyle()
    {
        style()->unpolish(this);
        style()->polish(this);
        update();
    }

    QWidget *target_card_;
    int minimum_target_height_;
    int drag_start_y_;
    int target_start_height_;
    int target_height_ = 0;
    bool dragging_;
};

class ShrinkablePanel : public QWidget
{
public:
    using QWidget::QWidget;

    QSize sizeHint() const override
    {
        QSize hint = QWidget::sizeHint();
        hint.setWidth(minimumWidth());
        return hint;
    }

    QSize minimumSizeHint() const override
    {
        QSize hint = QWidget::minimumSizeHint();
        hint.setWidth(minimumWidth());
        return hint;
    }
};

class WindowResizeHandle : public QWidget
{
public:
    explicit WindowResizeHandle(Qt::Edges edges, QWidget *parent)
        : QWidget(parent)
        , edges_(edges)
    {
        setFocusPolicy(Qt::NoFocus);
        setCursor(cursorForEdges(edges_));
    }

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        QWidget *topLevelWindow = window();
        if (event->button() == Qt::LeftButton && topLevelWindow && topLevelWindow->windowHandle())
        {
            topLevelWindow->windowHandle()->startSystemResize(edges_);
            event->accept();
            return;
        }
        QWidget::mousePressEvent(event);
    }

private:
    static QCursor cursorForEdges(Qt::Edges edges)
    {
        const bool horizontal = edges.testFlag(Qt::LeftEdge) || edges.testFlag(Qt::RightEdge);
        const bool vertical = edges.testFlag(Qt::TopEdge) || edges.testFlag(Qt::BottomEdge);
        if (horizontal && vertical)
        {
            const bool topLeftBottomRight =
                (edges.testFlag(Qt::TopEdge) && edges.testFlag(Qt::LeftEdge)) ||
                (edges.testFlag(Qt::BottomEdge) && edges.testFlag(Qt::RightEdge));
            return QCursor(topLeftBottomRight ? Qt::SizeFDiagCursor : Qt::SizeBDiagCursor);
        }
        if (horizontal)
        {
            return QCursor(Qt::SizeHorCursor);
        }
        return QCursor(Qt::SizeVerCursor);
    }

    Qt::Edges edges_;
};


class SpinBoxArrowHoverFilter final : public QObject
{
public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        auto *spin = qobject_cast<QAbstractSpinBox *>(watched);
        if (!spin)
        {
            return QObject::eventFilter(watched, event);
        }

        if (event->type() == QEvent::HoverMove)
        {
            updateHoverPart(spin, static_cast<QHoverEvent *>(event)->position().toPoint());
        }
        else if (event->type() == QEvent::MouseMove)
        {
            updateHoverPart(spin, static_cast<QMouseEvent *>(event)->position().toPoint());
        }
        else if (event->type() == QEvent::Leave || event->type() == QEvent::HoverLeave)
        {
            setHoverPart(spin, QString());
        }
        else if (event->type() == QEvent::EnabledChange)
        {
            setHoverPart(spin, QString());
        }
        return QObject::eventFilter(watched, event);
    }

private:
    static void updateHoverPart(QAbstractSpinBox *spin, const QPoint& position)
    {
        if (!spin->isEnabled())
        {
            setHoverPart(spin, QString());
            return;
        }

        QStyleOptionSpinBox option;
        option.initFrom(spin);
        option.subControls = QStyle::SC_All;
        option.buttonSymbols = spin->buttonSymbols();
        option.frame = spin->hasFrame();
        const QRect upRect = spin->style()->subControlRect(QStyle::CC_SpinBox,
                                                           &option,
                                                           QStyle::SC_SpinBoxUp,
                                                           spin);
        const QRect downRect = spin->style()->subControlRect(QStyle::CC_SpinBox,
                                                             &option,
                                                             QStyle::SC_SpinBoxDown,
                                                             spin);
        setHoverPart(spin, upRect.contains(position)
                               ? QStringLiteral("up")
                               : downRect.contains(position)
                                   ? QStringLiteral("down")
                                   : QString());
    }

    static void setHoverPart(QAbstractSpinBox *spin, const QString& part)
    {
        if (spin->property("spinArrowHover").toString() == part)
        {
            return;
        }
        spin->setProperty("spinArrowHover", part);
        spin->style()->unpolish(spin);
        spin->style()->polish(spin);
        spin->update();
    }
};


class TitleBarFeedbackCheckBox final : public QCheckBox
{
public:
    explicit TitleBarFeedbackCheckBox(QWidget *parent = nullptr)
        : QCheckBox(parent)
    {
        setMouseTracking(true);
        setProperty("indicatorCanvasSize", kIndicatorCanvasSize);
        setProperty("indicatorIconSize", kIndicatorIconSize);
        setProperty("indicatorFeedbackColorRole", QStringLiteral("TitleBarHover"));
        setProperty("indicatorHovered", false);
    }

protected:
    bool hitButton(const QPoint& pos) const override
    {
        return indicatorRect().contains(pos);
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        setIndicatorHovered(indicatorRect().contains(event->position().toPoint()));
        QCheckBox::mouseMoveEvent(event);
    }

    void leaveEvent(QEvent *event) override
    {
        setIndicatorHovered(false);
        QCheckBox::leaveEvent(event);
    }

    void paintEvent(QPaintEvent *event) override
    {
        QCheckBox::paintEvent(event);

        const QRect indicatorRect = this->indicatorRect();
        if (!indicatorRect.isValid())
        {
            return;
        }

        const QPalette::ColorGroup colorGroup = isEnabled()
            ? (isActiveWindow() ? QPalette::Active : QPalette::Inactive)
            : QPalette::Disabled;

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        if (indicator_hovered_ || hasFocus() || isDown())
        {
            const bool dark = qApp && qApp->property(kAppDarkThemeProperty).toBool();
            painter.setPen(Qt::NoPen);
            painter.setBrush(appThemeColor(AppThemeColor::TitleBarHover, dark));
            painter.drawRoundedRect(indicatorRect, kIndicatorCornerRadius, kIndicatorCornerRadius);
        }

        const int iconExtent = std::min({kIndicatorIconSize,
                                         indicatorRect.width(),
                                         indicatorRect.height()});
        QRect iconRect(0, 0, iconExtent, iconExtent);
        iconRect.moveCenter(indicatorRect.center());
        const QIcon icon = createLucideIcon(
            isChecked() ? QStringLiteral("square-check-big") : QStringLiteral("square"),
            palette().color(colorGroup, QPalette::Text));
        icon.paint(&painter,
                   iconRect,
                   Qt::AlignCenter,
                   isEnabled() ? QIcon::Normal : QIcon::Disabled,
                   isChecked() ? QIcon::On : QIcon::Off);
    }

private:
    QRect indicatorRect() const
    {
        QStyleOptionButton option;
        initStyleOption(&option);
        return style()->subElementRect(QStyle::SE_CheckBoxIndicator, &option, this);
    }

    void setIndicatorHovered(bool hovered)
    {
        if (indicator_hovered_ == hovered)
        {
            return;
        }
        indicator_hovered_ = hovered;
        setProperty("indicatorHovered", indicator_hovered_);
        update();
    }

    static constexpr int kIndicatorCanvasSize = 34;
    static constexpr int kIndicatorIconSize = 24;
    static constexpr int kIndicatorCornerRadius = 6;
    bool indicator_hovered_ = false;
};


bool isHoverEnterLikeEvent(QEvent::Type type)
{
    return type == QEvent::Enter ||
           type == QEvent::HoverEnter ||
           type == QEvent::HoverMove ||
           type == QEvent::MouseMove ||
           type == QEvent::MouseButtonPress ||
           type == QEvent::MouseButtonRelease;
}

bool isHoverLeaveLikeEvent(QEvent::Type type)
{
    return type == QEvent::Leave ||
           type == QEvent::HoverLeave;
}

bool widgetContainsGlobalCursor(const QWidget *widget, const QPoint& cursorPos)
{
    return widget &&
           widget->isVisible() &&
           widget->isEnabled() &&
           QRect(widget->mapToGlobal(QPoint(0, 0)), widget->size()).contains(cursorPos);
}

void setWidgetBooleanProperty(QWidget *widget, const char *propertyName, bool enabled)
{
    if (!widget)
    {
        return;
    }

    if (widget->property(propertyName).toBool() == enabled)
    {
        widget->update();
        return;
    }

    widget->setProperty(propertyName, enabled);
    if (widget->style())
    {
        widget->style()->unpolish(widget);
        widget->style()->polish(widget);
    }
    widget->update();
}


void configureHoverParticipant(QWidget *widget, const char *participantProperty, QObject *eventFilter)
{
    if (!widget)
    {
        return;
    }

    widget->setProperty(participantProperty, true);
    widget->setAttribute(Qt::WA_Hover, true);
    widget->setMouseTracking(true);
    if (eventFilter)
    {
        widget->installEventFilter(eventFilter);
    }
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
    iconLabel->setPixmap(createLucideIcon(iconName, sectionTitleIconColor(dark)).pixmap(
        QSize(kSectionTitleIconSize, kSectionTitleIconSize)));
}

QLabel *createSectionTitleCluster(QWidget *parent,
                                  const QString& iconName,
                                  int titleHeight,
                                  QWidget **clusterOut)
{
    auto *cluster = new QWidget(parent);
    cluster->setObjectName(QStringLiteral("sectionTitleCluster"));
    cluster->setFixedHeight(titleHeight);
    cluster->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

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

    auto *titleLabel = new VaporView::VisualTextLabel(cluster);
    titleLabel->setObjectName(QStringLiteral("sectionTitleLabel"));
    VaporView::configureSelectableCardTitle(titleLabel);
    titleLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    titleLabel->setMargin(0);
    titleLabel->setContentsMargins(0, 0, 0, 0);
    titleLabel->setFixedHeight(titleHeight);
    titleLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    layout->addWidget(titleLabel, 0, Qt::AlignVCenter);

    if (clusterOut)
    {
        *clusterOut = cluster;
    }
    return titleLabel;
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

void setSectionTitleIconName(QLabel *titleLabel, const QString& iconName, bool dark)
{
    if (!titleLabel)
    {
        return;
    }
    QWidget *cluster = titleLabel->parentWidget();
    if (!cluster)
    {
        return;
    }
    auto *iconLabel = cluster->findChild<QLabel *>(QStringLiteral("sectionTitleIcon"), Qt::FindDirectChildrenOnly);
    if (!iconLabel)
    {
        return;
    }
    if (iconLabel->property(kSectionTitleIconNameProperty).toString() == iconName)
    {
        return;
    }
    iconLabel->setProperty(kSectionTitleIconNameProperty, iconName);
    updateSectionTitleIcon(iconLabel, dark);
}


void installMenuItemEventFilter(QObject *target,
                                std::function<void()> hoverCallback,
                                std::function<void()> clickCallback)
{
    if (target)
    {
        target->installEventFilter(
            new MenuItemEventFilter(std::move(hoverCallback), std::move(clickCallback), target));
    }
}

QFrame *createFloatingTitleMenuPanel(QWidget *parent)
{
    return new FloatingTitleMenuPanel(parent);
}

QWidget *createAppSidebarFrame(QWidget *parent)
{
    return new AppSidebarFrame(parent);
}

QWidget *createMainCardResizeHandle(QWidget *target, int minimumHeight, QWidget *parent)
{
    return new MainCardResizeHandle(target, minimumHeight, parent);
}

QWidget *createShrinkablePanel(QWidget *parent)
{
    return new ShrinkablePanel(parent);
}

QWidget *createWindowResizeHandle(Qt::Edges edges, QWidget *parent)
{
    return new WindowResizeHandle(edges, parent);
}

QCheckBox *createTitleBarFeedbackCheckBox(QWidget *parent)
{
    return new TitleBarFeedbackCheckBox(parent);
}

void installSpinBoxArrowHoverFilter(QObject *application)
{
    if (application)
    {
        application->installEventFilter(new SpinBoxArrowHoverFilter(application));
    }
}

} // namespace VaporView::Ground::MainSupport
