#include "MainWindow.h"
#include "AppTheme.h"
#include "CustomTitleBar.h"
#ifdef VAPORVIEW_HAS_OSGEARTH
#include "map3d/Map3DWindow.h"
#endif
#include "RtkConfigDialog.h"
#include "SessionViewerWindow.h"
#include "SkyDeviceConfigDialog.h"
#include "SingleLevelPopupMenu.h"
#include "TcpWaveEncoding.h"
#include "TcpWavePanel.h"
#include "VisualTextLabel.h"
#include "WindowSizing.h"
#include "data_collector.h"
#include "data_types.h"
#include "serial_probe_utils.h"
#include <QMenu>
#include <QAbstractItemView>
#include <QAbstractButton>
#include <QAbstractSpinBox>
#include <QAction>
#include <QButtonGroup>
#include <QCheckBox>
#include <QCursor>
#include <QDateTimeEdit>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFormLayout>
#include <QMessageBox>
#include <QMouseEvent>
#include <QFile>
#include <QFileInfo>
#include <QFileDialog>
#include <QFont>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QSaveFile>
#include <QTextStream>
#include <QStringConverter>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QTimeZone>
#include <QGridLayout>
#include <QFrame>
#include <QScrollArea>
#include <QSplitter>
#include <QTimer>
#include <QDir>
#include <QDirIterator>
#include <QScrollBar>
#include <QShortcut>
#include <QScreen>
#include <QSpacerItem>
#include <QStringList>
#include <QApplication>
#include <QGuiApplication>
#include <QHelpEvent>
#include <QLayout>
#include <QIntValidator>
#include <QSerialPortInfo>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QHash>
#include <QIcon>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPaintEvent>
#include <QPolygonF>
#include <QPixmap>
#include <QRegion>
#include <QSvgRenderer>
#include <QSet>
#include <QSignalBlocker>
#include <QSettings>
#include <QStackedWidget>
#include <QStyle>
#include <QStyleOptionToolButton>
#include <QThread>
#include <QToolButton>
#include <QToolTip>
#include <QTransform>
#include <QVariant>
#include <QVariantAnimation>
#include <QVector>
#include <QWidgetAction>
#include <QWindow>
#include <QtEndian>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <dwmapi.h>
#include <windows.h>
#include <windowsx.h>
#endif
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <utility>
#include <vector>

using VaporView::AppThemeColor;
using VaporView::appThemeColor;
using VaporView::appThemeColorName;
using VaporView::appThemePalette;
using VaporView::appThemeRgba;
using VaporView::applyAppThemeTokens;
using VaporView::configureComboBoxPopup;
using VaporView::kAppDarkThemeProperty;
using VaporView::SingleLevelPopupAnchor;
using VaporView::SingleLevelPopupMenu;
using VaporView::SingleLevelPopupMenuRow;
using VaporView::SingleLevelPopupTextAlignment;

namespace
{
constexpr int kFloatingMenuShadowMarginPx = 22;
constexpr int kFloatingMenuCornerRadiusPx = 10;

bool isTemperatureCommonCommand(VaporView::CommandId command)
{
    return command == VaporView::CommandId::SetTemperatureControllerMode ||
           command == VaporView::CommandId::SetTemperatureDeviceAddress ||
           command == VaporView::CommandId::SetTemperatureRs485Baud ||
           command == VaporView::CommandId::SetTemperatureOvertempOutputMode ||
           command == VaporView::CommandId::RestoreTemperatureFactoryDefaults;
}

int temperatureRs485BaudRateForIndex(quint16 index)
{
    static constexpr std::array<int, 8> kRates = {4800, 9600, 19200, 38400, 57600, 115200, 230400, 460800};
    return index < kRates.size() ? kRates.at(index) : 9600;
}

int rememberedTemperatureSlaveAddress()
{
    QSettings settings(QStringLiteral("VaporView"), QStringLiteral("MainWindow"));
    return std::clamp(settings.value(QStringLiteral("serial/temperature_slave_address"), 1).toInt(), 1, 247);
}

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
        Q_UNUSED(watched);
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
        setFocusPolicy(Qt::NoFocus);
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
        const QColor shadowTint(0, 0, 0);
        const QImage softShadow = menuBoxBlurredAlpha(size(),
                                                      panel.adjusted(-1.0, 7.0, 1.0, 10.0),
                                                      kFloatingMenuCornerRadiusPx + 2.0,
                                                      9,
                                                      3);
        const QImage contactShadow = menuBoxBlurredAlpha(size(),
                                                         panel.adjusted(0.0, 1.0, 0.0, 2.0),
                                                         kFloatingMenuCornerRadiusPx,
                                                         4,
                                                         2);
        drawMenuTintedAlphaImage(painter, softShadow, shadowTint, dark ? 82 : 38);
        drawMenuTintedAlphaImage(painter, contactShadow, shadowTint, dark ? 48 : 14);

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

constexpr const char *kTooltipShortcutProperty = "_vv_tooltip_shortcut";

QString shortcutText(const QKeySequence& sequence)
{
    return sequence.isEmpty() ? QString() : sequence.toString(QKeySequence::NativeText);
}

QString shortcutTextFromAction(const QAction *action)
{
    return action ? shortcutText(action->shortcut()) : QString();
}

bool writeJsonFileAtomically(const QString& filename, const QJsonObject& object, QString *errorMessage)
{
    QSaveFile file(filename);
    if (!file.open(QIODevice::WriteOnly))
    {
        if (errorMessage) *errorMessage = file.errorString();
        return false;
    }

    const QByteArray payload = QJsonDocument(object).toJson(QJsonDocument::Indented);
    if (file.write(payload) != payload.size())
    {
        if (errorMessage) *errorMessage = file.errorString();
        return false;
    }

    if (!file.commit())
    {
        if (errorMessage) *errorMessage = file.errorString();
        return false;
    }

    return true;
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

void fitButtonMinimumWidth(QAbstractButton *button, int floorWidth = 0)
{
    if (!button)
    {
        return;
    }

    const int iconWidth = button->icon().isNull() ? 0 : button->iconSize().width() + 8;
    const int textWidth = button->fontMetrics().horizontalAdvance(button->text());
    button->setMinimumWidth(std::max(floorWidth, textWidth + iconWidth + 42));
}

void fitButtonFixedWidth(QAbstractButton *button, int floorWidth = 0, int padding = 24)
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
        : QFrame(nullptr, Qt::ToolTip | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint)
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

        const int maxPopupWidth = std::max(160, std::min(520, bounds.width()));
        text_label_->setMaximumWidth(std::max(90, maxPopupWidth - (shortcut.isEmpty() ? 32 : 128)));
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
        const QSize popupSize = sizeHint().boundedTo(QSize(maxPopupWidth, 1000));
        resize(popupSize);

        const int gap = 8;
        int x = targetRect.center().x() - width() / 2;
        int y = targetRect.bottom() + gap;
        if (y + height() > bounds.bottom())
        {
            y = targetRect.top() - height() - gap;
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
        const QRectF roundedRect = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        painter.setPen(QPen(popup_border_, 1.0));
        painter.setBrush(popup_background_);
        painter.drawRoundedRect(roundedRect, 13.0, 13.0);
    }

private:
    QColor popup_background_ = QColor(253, 253, 252);
    QColor popup_border_ = QColor(QStringLiteral("#E8E8E8"));
    QLabel *text_label_;
    QLabel *shortcut_label_;
};

AppTooltipPopup *appTooltipPopup()
{
    static AppTooltipPopup *popup = new AppTooltipPopup();
    return popup;
}

void hideAppTooltipPopup()
{
    appTooltipPopup()->hide();
}

bool showAppTooltip(QObject *watched, QEvent *event, bool dark)
{
    auto *widget = qobject_cast<QWidget *>(watched);
    if (!widget || !widget->isVisible())
    {
        return false;
    }

    QString text = widget->toolTip().trimmed();
    if (text.isEmpty())
    {
        return false;
    }

    QRect anchor;
    const QVariant anchorValue = widget->property("_vv_tooltip_anchor_rect");
    if (anchorValue.isValid() && anchorValue.canConvert<QRect>())
    {
        anchor = anchorValue.toRect();
        if (auto *helpEvent = dynamic_cast<QHelpEvent *>(event);
            anchor.isValid() && helpEvent && !anchor.contains(helpEvent->pos()))
        {
            QToolTip::hideText();
            hideAppTooltipPopup();
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
    appTooltipPopup()->showFor(widget, text, shortcut, dark, anchor);
    event->accept();
    return true;
}

constexpr const char *kBaseMinWidthProperty = "_vv_base_min_width";
constexpr const char *kBaseMinHeightProperty = "_vv_base_min_height";
constexpr const char *kBaseMaxWidthProperty = "_vv_base_max_width";
constexpr const char *kBaseMaxHeightProperty = "_vv_base_max_height";
constexpr const char *kBaseSpacingProperty = "_vv_base_spacing";
constexpr const char *kBaseMarginsLeftProperty = "_vv_base_margin_left";
constexpr const char *kBaseMarginsTopProperty = "_vv_base_margin_top";
constexpr const char *kBaseMarginsRightProperty = "_vv_base_margin_right";
constexpr const char *kBaseMarginsBottomProperty = "_vv_base_margin_bottom";
constexpr const char *kTextWidthCandidatesProperty = "_vv_text_width_candidates";
constexpr const char *kTextWidthPaddingProperty = "_vv_text_width_padding";
constexpr const char *kNumericWidthCandidatesProperty = "_vv_numeric_width_candidates";
constexpr const char *kNumericWidthPaddingProperty = "_vv_numeric_width_padding";
constexpr const char *kMainCardMinimumHeightProperty = "_vv_main_card_minimum_height";
constexpr int kMainPageInputHeight = 36;
constexpr int kMainPageButtonHeight = kMainPageInputHeight;
constexpr int kDeviceConfigAutoDetectButtonMinWidth = 124;
constexpr int kDeviceConfigSourceModeComboWidth = 156;
constexpr int kDeviceConfigSkyDeviceButtonMinWidth = 132;
constexpr int kDeviceConfigTopButtonPadding = 24;
constexpr int kHomeDeviceButtonSize = 32;
constexpr int kHomeDeviceIconSize = 18;
constexpr int kHomeDeviceCapsuleHeight = 32;
constexpr int kHomeDeviceRowHeight = kHomeDeviceButtonSize;
constexpr int kHomeDeviceGridColumns = 3;
constexpr int kHomeDeviceGridRows = 2;
constexpr int kHomeDeviceGridRowGap = 2;
constexpr int kHomeDeviceItemGap = 12;
constexpr int kHomeDeviceActionSpinnerFrames = 30;
constexpr int kHomeDeviceActionSpinnerIntervalMs = 25;
constexpr int kHomeDeviceActionSpinnerMinimumMs = 1000;
constexpr int kMainPageTitleBarHeight = kMainPageInputHeight + 4;
constexpr int kEnvironmentTitleBarHeight = kMainPageButtonHeight;
constexpr int kHomeOverviewCardOuterPadding = 1;
constexpr int kHomeOverviewBodyPadding = 2;
constexpr int kConfigFormBottomPadding = 4;
constexpr int kConfigHomeBodyBottomPadding = kHomeOverviewBodyPadding;
constexpr int kConfigCardBottomPadding = kHomeOverviewCardOuterPadding;
constexpr int kHomeTelemetrySummaryHeightPadding = 4;
constexpr int kConfigCardMinHeight = kMainPageTitleBarHeight + kMainPageButtonHeight + kConfigHomeBodyBottomPadding + kConfigCardBottomPadding;
constexpr int kHomeOverviewDeviceMinWidth = 568;
constexpr int kHomeOverviewTemperatureMinWidth = 380;
constexpr int kHomeOverviewSplitterHandleWidth = 8;
constexpr const char *kHomeOverviewSplitterInitializedProperty = "_vv_home_overview_splitter_initialized";
constexpr int kSensorNavigationStretch = 4;
constexpr int kSensorEnvironmentStretch = 1;
constexpr int kTcpWaveCardMinHeight = 430;
constexpr int kCompactTcpWaveCardMinHeight = 560;
constexpr int kAppSidebarIconOnlyBaseWidth = 52;
constexpr int kAppSidebarFullBaseWidth = 122;
constexpr int kAppSidebarVisualPadding = 8;
constexpr int kAppSidebarTopBottomPadding = 6;
constexpr int kAppSidebarButtonHeight = 48;
constexpr int kAppSidebarCompactButtonSize = 44;
constexpr int kAppSidebarFullIconSize = 20;
constexpr int kAppSidebarCompactIconSize = 32;
constexpr int kMainCardResizeHandleHeight = 3;
constexpr int kEnvStatusIconSize = 18;
constexpr int kEpsilonSideTitleWidth = 24;
constexpr int kEpsilonTitleColumnWidth = 90;
constexpr int kEpsilonMotionTitleColumnWidth = 180;
constexpr int kEpsilonLeftValueColumnWidth = 130;
constexpr int kEpsilonPositionValueColumnWidth = 112;
constexpr int kEpsilonMotionValueColumnWidth = 145;
constexpr int kEpsilonFieldBaseSpacing = 2;
constexpr int kEpsilonMotionFieldSpacing = 8;
constexpr int kEpsilonFieldMinimumHeight = 20;
constexpr int kTemperatureControllerPlotWidth = 260;
constexpr int kTemperatureControllerPlotMinHeight = 190;
constexpr int kTemperatureControllerValueWidth = 126;
constexpr int kTemperatureControllerInputWidth = 112;
constexpr int kTemperatureControllerWideInputWidth = 138;
constexpr int kTemperatureControllerTopEnableWidth = 106;
constexpr int kTemperatureControllerTopEnableHeight = 34;
constexpr int kTemperatureControllerTopModeWidth = 132;
constexpr int kTemperatureControllerTopTargetWidth = 172;
constexpr int kTemperatureControllerCompactInputWidth = 112;
constexpr int kTemperatureControllerCompactPidInputWidth = 82;
constexpr int kTemperatureControllerMaxOutputLabelWidth = 168;
constexpr int kTemperatureControllerCompactLabelWidth = 72;
constexpr int kTemperatureControllerControlLabelWidth = 150;
constexpr int kTemperatureControllerConfigRowHeight = 38;
constexpr int kTemperatureControllerTopControlsHeight = 38;
constexpr int kTemperatureControllerChannelStackHeight = 54;
constexpr int kTemperatureControllerCommonStackHeight = kTemperatureControllerChannelStackHeight;
constexpr int kTemperatureControllerHistoryLimit = 240;
constexpr int kRemotePacketRateWindowMs = 5000;
constexpr qint64 kTcpRecordingStatusRefreshMs = 500;
constexpr quint64 kTcpRawRecordQueueWarningBytes = 32ULL * 1024ULL * 1024ULL;
constexpr quint64 kTcpRawRecordQueueMaxBytes = 256ULL * 1024ULL * 1024ULL;
constexpr qint64 kTcpRawRecordQueueWarningIntervalMs = 5000;
constexpr int kPtbMinSampleRateHz = 1;
constexpr int kPtbMaxSampleRateHz = 70;
constexpr const char *kTcpWavePeakIndexCsvHeader =
    "host_time_us,peak_value,peak_index,point_count,search_start_index,search_end_index\n";

struct TcpWavePeakSummary
{
    float value = std::numeric_limits<float>::quiet_NaN();
    int index = -1;
    quint32 point_count = 0;
};

TcpWavePeakSummary summarizeTcpWavePeakSamples(const char *samples,
                                               qsizetype byteCount,
                                               VaporView::TcpFloatEncoding encoding)
{
    TcpWavePeakSummary summary;
    if (!samples || byteCount <= 0 || byteCount % static_cast<qsizetype>(sizeof(float)) != 0)
    {
        return summary;
    }

    const qsizetype sampleCount = byteCount / static_cast<qsizetype>(sizeof(float));
    summary.point_count = static_cast<quint32>(std::min<quint64>(
        static_cast<quint64>(sampleCount),
        static_cast<quint64>(std::numeric_limits<quint32>::max())));
    const VaporView::TcpFloatEncoding effectiveEncoding = encoding == VaporView::TcpFloatEncoding::Unknown
        ? VaporView::autoDetectTcpFloatEncoding(QByteArray(samples, static_cast<qsizetype>(byteCount)))
        : encoding;

    bool hasPeak = false;
    float peakValue = std::numeric_limits<float>::lowest();
    int peakIndex = -1;
    const int scanCount = static_cast<int>(std::min<quint64>(
        static_cast<quint64>(sampleCount),
        static_cast<quint64>(std::numeric_limits<int>::max())));
    for (int index = 0; index < scanCount; ++index)
    {
        const float value = VaporView::decodeTcpFloatSample(samples + index * static_cast<int>(sizeof(float)), effectiveEncoding);
        if (!std::isfinite(value))
        {
            continue;
        }
        if (!hasPeak || value > peakValue)
        {
            hasPeak = true;
            peakValue = value;
            peakIndex = index;
        }
    }

    if (hasPeak)
    {
        summary.value = peakValue;
        summary.index = peakIndex;
    }
    return summary;
}

TcpWavePeakSummary summarizeTcpWavePeakRecordPayload(const QByteArray& payload, quint32 flags)
{
    TcpWavePeakSummary summary;
    if (payload.size() < static_cast<qsizetype>(sizeof(quint32) * 2))
    {
        return summary;
    }

    quint32 rawSizeLe = 0;
    quint32 harmonicSizeLe = 0;
    const char *cursor = payload.constData();
    std::memcpy(&rawSizeLe, cursor, sizeof(rawSizeLe));
    cursor += sizeof(rawSizeLe);
    std::memcpy(&harmonicSizeLe, cursor, sizeof(harmonicSizeLe));
    cursor += sizeof(harmonicSizeLe);

    const quint32 rawSize = qFromLittleEndian(rawSizeLe);
    const quint32 harmonicSize = qFromLittleEndian(harmonicSizeLe);
    const quint64 requiredBytes = static_cast<quint64>(sizeof(quint32) * 2) + rawSize + harmonicSize;
    if (requiredBytes > static_cast<quint64>(payload.size()) ||
        harmonicSize == 0 ||
        harmonicSize % static_cast<quint32>(sizeof(float)) != 0)
    {
        return summary;
    }

    return summarizeTcpWavePeakSamples(payload.constData() + sizeof(quint32) * 2 + rawSize,
                                       static_cast<qsizetype>(harmonicSize),
                                       VaporView::tcpFloatEncodingFromRawDatFlags(flags));
}

QString peakValueCsvText(float value)
{
    return std::isfinite(value)
        ? QString::number(static_cast<double>(value), 'g', 9)
        : QString();
}

void notePacketArrival(QVector<qint64>& arrivals, qint64 now)
{
    arrivals.push_back(now);
    while (!arrivals.isEmpty() && now - arrivals.front() > kRemotePacketRateWindowMs)
    {
        arrivals.removeFirst();
    }
}

double packetRateFromArrivals(const QVector<qint64>& arrivals)
{
    if (arrivals.size() < 2)
    {
        return 0.0;
    }
    const qint64 elapsedMs = arrivals.back() - arrivals.front();
    if (elapsedMs <= 0)
    {
        return 0.0;
    }
    return (arrivals.size() - 1) * 1000.0 / static_cast<double>(elapsedMs);
}

QFont numericFontFrom(const QFont& base)
{
    QFont font(base);
    font.setFamilies({
        QStringLiteral("Consolas"),
        QStringLiteral("Monaco"),
        QStringLiteral("Courier New")
    });
    font.setStyleHint(QFont::Monospace);
    font.setFixedPitch(true);
    return font;
}

int widestTextWidth(const QFont& font, const QStringList& candidates)
{
    const QFontMetrics metrics(font);
    int width = 0;
    for (const QString& candidate : candidates)
    {
        width = std::max(width, metrics.horizontalAdvance(candidate));
    }
    return width;
}

void applyFixedNumericLabelWidth(QLabel *label, const QStringList& candidates, int padding = 0)
{
    if (!label)
    {
        return;
    }
    label->setFont(numericFontFrom(label->font()));
    QStringList widthCandidates = candidates;
    if (!label->text().isEmpty())
    {
        widthCandidates.append(label->text());
    }
    const int width = widestTextWidth(label->font(), widthCandidates) + padding;
    label->setMinimumWidth(width);
    label->setMaximumWidth(width);
    label->setSizePolicy(QSizePolicy::Fixed, label->sizePolicy().verticalPolicy());
}

void setFixedNumericLabelWidth(QLabel *label, const QStringList& candidates, int padding = 0)
{
    if (!label)
    {
        return;
    }
    label->setProperty(kNumericWidthCandidatesProperty, candidates);
    label->setProperty(kNumericWidthPaddingProperty, padding);
    applyFixedNumericLabelWidth(label, candidates, padding);
}

void applyFixedTextLabelWidth(QLabel *label, const QStringList& candidates, int padding = 0)
{
    if (!label)
    {
        return;
    }
    QStringList widthCandidates = candidates;
    if (!label->text().isEmpty())
    {
        widthCandidates.append(label->text());
    }
    const int width = widestTextWidth(label->font(), widthCandidates) + padding;
    label->setMinimumWidth(width);
    label->setMaximumWidth(width);
    label->setSizePolicy(QSizePolicy::Fixed, label->sizePolicy().verticalPolicy());
}

void setFixedTextLabelWidth(QLabel *label, const QStringList& candidates, int padding = 0)
{
    if (!label)
    {
        return;
    }
    label->setProperty(kTextWidthCandidatesProperty, candidates);
    label->setProperty(kTextWidthPaddingProperty, padding);
    applyFixedTextLabelWidth(label, candidates, padding);
}

void refreshFixedTextLabelWidth(QLabel *label)
{
    if (!label)
    {
        return;
    }
    const QStringList widthCandidates = label->property(kTextWidthCandidatesProperty).toStringList();
    if (widthCandidates.isEmpty())
    {
        return;
    }
    const int padding = label->property(kTextWidthPaddingProperty).toInt();
    applyFixedTextLabelWidth(label, widthCandidates, std::max(0, padding));
}

void refreshFixedNumericLabelWidth(QLabel *label)
{
    if (!label)
    {
        return;
    }
    const QStringList widthCandidates = label->property(kNumericWidthCandidatesProperty).toStringList();
    if (widthCandidates.isEmpty())
    {
        return;
    }
    const int padding = label->property(kNumericWidthPaddingProperty).toInt();
    applyFixedNumericLabelWidth(label, widthCandidates, std::max(0, padding));
}

void polishNumericLabel(QLabel *label)
{
    if (!label)
    {
        return;
    }
    label->style()->unpolish(label);
    label->style()->polish(label);
    refreshFixedNumericLabelWidth(label);
}

QStringList environmentFieldLabelWidthCandidates()
{
    return {
        QStringLiteral("Distance:"),
        QStringLiteral("Strength:"),
        QStringLiteral("Pressure:"),
        QStringLiteral("Temp:"),
        QStringLiteral("Humidity:"),
        QStringLiteral("距离:"),
        QStringLiteral("强度:"),
        QStringLiteral("气压:"),
        QStringLiteral("温度:"),
        QStringLiteral("湿度:")
    };
}

QStringList temperatureControllerFieldLabelWidthCandidates()
{
    return {
        QStringLiteral("Internal:"),
        QStringLiteral("Error:"),
        QStringLiteral("Target:"),
        QStringLiteral("Output Enable:"),
        QStringLiteral("Mode:"),
        QStringLiteral("Max Output:"),
        QStringLiteral("自身温度:"),
        QStringLiteral("错误码:"),
        QStringLiteral("目标温度(°C):"),
        QStringLiteral("输出使能"),
        QStringLiteral("输出模式"),
        QStringLiteral("最大输出电压百分比(%)"),
        QStringLiteral("PID:")
    };
}

QStringList temperatureControllerStatusLabelWidthCandidates()
{
    return {
        QStringLiteral("Internal:"),
        QStringLiteral("Error:"),
        QStringLiteral("Mode:"),
        QStringLiteral("Controller Mode:"),
        QStringLiteral("自身温度:"),
        QStringLiteral("错误码:"),
        QStringLiteral("温控器模式:")
    };
}

QString fixedTextField(const QString& text, int width, Qt::Alignment alignment = Qt::AlignRight)
{
    const int targetWidth = std::max(width, static_cast<int>(text.size()));
    return alignment == Qt::AlignLeft
        ? text.leftJustified(targetWidth, QLatin1Char(' '))
        : text.rightJustified(targetWidth, QLatin1Char(' '));
}

QString fixedDecimalWithUnit(double value, int decimals, int numberWidth, const QString& unit)
{
    const QString number = std::isfinite(value)
        ? QString::number(value, 'f', decimals)
        : QStringLiteral("---");
    return unit.isEmpty()
        ? fixedTextField(number, numberWidth)
        : QStringLiteral("%1 %2").arg(fixedTextField(number, numberWidth), unit);
}

QString compactDecimalWithUnit(double value, int decimals, const QString& unit)
{
    const QString number = std::isfinite(value)
        ? QString::number(value, 'f', decimals)
        : QStringLiteral("---");
    return unit.isEmpty()
        ? number
        : QStringLiteral("%1 %2").arg(number, unit);
}

std::string epsilonGnssFixTextForCode(int fix_code)
{
    switch (fix_code)
    {
    case 0: return "NO_GPS";
    case 1: return "NO_FIX";
    case 2: return "2D";
    case 3: return "3D";
    case 4: return "DGPS";
    case 5: return "RTK_FLOAT";
    case 6: return "RTK_FIXED";
    case 7: return "STATIC";
    case 8: return "PPP";
    case 9: return "RTK_DUAL";
    default: return "UNKNOWN";
    }
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
        drag_start_y_ = event->globalPosition().toPoint().y();
        target_start_height_ = target_card_->height();
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

        const int deltaY = event->globalPosition().toPoint().y() - drag_start_y_;
        bool ok = false;
        const int propertyMinimum = target_card_->property(kMainCardMinimumHeightProperty).toInt(&ok);
        const int effectiveMinimum = ok ? std::max(minimum_target_height_, propertyMinimum) : minimum_target_height_;
        const int nextHeight = std::max(effectiveMinimum, target_start_height_ + deltaY);
        target_card_->setFixedHeight(nextHeight);
        event->accept();
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && dragging_)
        {
            dragging_ = false;
            setProperty("dragging", false);
            refreshStyle();
            event->accept();
            return;
        }

        QWidget::mouseReleaseEvent(event);
    }

private:
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

QString skyDeviceDisplayName(VaporView::SkyDeviceId device)
{
    switch (device)
    {
    case VaporView::SkyDeviceId::Epsilon: return QStringLiteral("EPSILON");
    case VaporView::SkyDeviceId::Ptb: return QStringLiteral("PTB210");
    case VaporView::SkyDeviceId::Hmp: return QStringLiteral("HMP3");
    case VaporView::SkyDeviceId::Lidar: return QStringLiteral("TFA1500-L");
    case VaporView::SkyDeviceId::TemperatureController: return QStringLiteral("RD105");
    case VaporView::SkyDeviceId::WaveTcp: return QStringLiteral("Wave TCP");
    case VaporView::SkyDeviceId::All: return QStringLiteral("全部设备");
    }
    return QStringLiteral("Device");
}

QString homeDeviceDisplayName(VaporView::SkyDeviceId device, bool english)
{
    switch (device)
    {
    case VaporView::SkyDeviceId::Epsilon:
        return english ? QStringLiteral("EPSILON Nav") : QStringLiteral("EPSILON 组合导航");
    case VaporView::SkyDeviceId::Ptb:
        return english ? QStringLiteral("PTB210 Barometer") : QStringLiteral("PTB210 气压计");
    case VaporView::SkyDeviceId::Hmp:
        return english ? QStringLiteral("HMP Temp/Humidity") : QStringLiteral("HMP 温湿度");
    case VaporView::SkyDeviceId::Lidar:
        return english ? QStringLiteral("TFA1500-L LiDAR") : QStringLiteral("TFA1500-L 激光测距");
    case VaporView::SkyDeviceId::TemperatureController:
        return english ? QStringLiteral("RD105 Thermal") : QStringLiteral("RD105 激光温控");
    case VaporView::SkyDeviceId::WaveTcp:
        return english ? QStringLiteral("Wave Source") : QStringLiteral("波形源");
    case VaporView::SkyDeviceId::All:
        return english ? QStringLiteral("All devices") : QStringLiteral("全部设备");
    }
    return english ? QStringLiteral("Device") : QStringLiteral("设备");
}

QString formatBitRate(double bitsPerSecond)
{
    if (!std::isfinite(bitsPerSecond) || bitsPerSecond <= 0.0)
    {
        return QStringLiteral("0 bps");
    }
    if (bitsPerSecond < 1000.0)
    {
        return QStringLiteral("%1 bps").arg(bitsPerSecond, 0, 'f', 0);
    }
    if (bitsPerSecond < 1'000'000.0)
    {
        const double kilobitsPerSecond = bitsPerSecond / 1000.0;
        const int decimals = kilobitsPerSecond < 10.0 ? 2 : (kilobitsPerSecond < 100.0 ? 1 : 0);
        return QStringLiteral("%1 kbps").arg(kilobitsPerSecond, 0, 'f', decimals);
    }
    if (bitsPerSecond < 1'000'000'000.0)
    {
        const double megabitsPerSecond = bitsPerSecond / 1'000'000.0;
        const int decimals = megabitsPerSecond < 10.0 ? 2 : 1;
        return QStringLiteral("%1 Mbps").arg(megabitsPerSecond, 0, 'f', decimals);
    }
    const double gigabitsPerSecond = bitsPerSecond / 1'000'000'000.0;
    const int decimals = gigabitsPerSecond < 10.0 ? 2 : 1;
    return QStringLiteral("%1 Gbps").arg(gigabitsPerSecond, 0, 'f', decimals);
}

QString formatFrequencyText(double hz)
{
    if (!std::isfinite(hz) || hz < 0.0)
    {
        hz = 0.0;
    }
    return QStringLiteral("%1 Hz").arg(hz, 0, 'f', 1);
}

QString remoteNoDataText(bool english)
{
    return english ? QStringLiteral("No data") : QStringLiteral("无数据");
}

QString remoteDisconnectedText(bool english)
{
    return english ? QStringLiteral("Not connected") : QStringLiteral("未连接");
}

QString remoteStaleText(bool english)
{
    return english ? QStringLiteral("Stale") : QStringLiteral("超时");
}

QString formatElapsedCompact(quint64 elapsedMs)
{
    const quint64 totalSeconds = elapsedMs / 1000ULL;
    const quint64 hours = totalSeconds / 3600ULL;
    const quint64 minutes = (totalSeconds / 60ULL) % 60ULL;
    const quint64 seconds = totalSeconds % 60ULL;
    if (hours > 0)
    {
        return QStringLiteral("%1:%2:%3")
            .arg(hours)
            .arg(minutes, 2, 10, QLatin1Char('0'))
            .arg(seconds, 2, 10, QLatin1Char('0'));
    }
    return QStringLiteral("%1:%2")
        .arg(minutes)
        .arg(seconds, 2, 10, QLatin1Char('0'));
}

constexpr int kDefaultEpsilonSampleRateHz = 100;
constexpr int kDefaultPtbSampleRateHz = 20;
constexpr int kDefaultHmpSampleRateHz = 20;
constexpr int kDefaultLidarSampleRateHz = 100;
constexpr int kDefaultTemperatureSampleRateHz = 5;
constexpr int kMaxTemperatureSampleRateHz = 20;
constexpr int kDefaultMainWindowWidth = 1280;
constexpr int kDefaultMainWindowHeight = 800;
constexpr int kMinimumMainWindowWidth = 1024;
constexpr int kMinimumMainWindowHeight = 640;
constexpr int kCompactHomeScreenWidth = 1600;
constexpr int kCompactHomeScreenHeight = 900;
constexpr int kCompactHomeViewportWidth = 1400;
constexpr quint64 kImuPpsSyncWindowUs = 2ULL * 1000ULL * 1000ULL;
constexpr char kUnifiedRawMagic[8] = {'V', 'V', 'R', 'A', 'W', 'D', 'A', 'T'};
constexpr quint32 kUnifiedRawFormatVersion = 2u;
constexpr quint32 kUnifiedRawRecordMarker = 0x44525756u;
constexpr quint16 kRawSourceEpsilon = 1u;
constexpr quint16 kRawSourcePtb = 2u;
constexpr quint16 kRawSourceHmp = 3u;
constexpr quint16 kRawSourceLidar = 4u;
constexpr quint16 kRawSourceTcpWave = 5u;
constexpr quint16 kRawRecordTypeGeneric = 1u;
constexpr quint32 kRawTcpWaveCombinedPayloadFlag = 0x00000001u;
constexpr const char *kMainWindowProperty = "vaporViewMainWindow";
constexpr const char *kEnglishProperty = "vaporViewEnglish";

int clampPtbSampleRate(int hz)
{
    return std::clamp(hz, kPtbMinSampleRateHz, kPtbMaxSampleRateHz);
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
    constexpr int kLogicalSize = 32;
    const int physicalSize = std::max(1, static_cast<int>(std::ceil(kLogicalSize * dpr)));
    QPixmap pixmap(physicalSize, physicalSize);
    pixmap.setDevicePixelRatio(dpr);
    pixmap.fill(Qt::transparent);

    QSvgRenderer renderer(tinted);
    if (!renderer.isValid())
    {
        return QPixmap();
    }

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    renderer.render(&painter, QRectF(2, 2, 28, 28));
    return pixmap;
}

void addLucideIconPixmaps(QIcon& icon, const QByteArray& svgData, const QColor& color, QIcon::Mode mode)
{
    for (const qreal dpr : {1.0, 1.25, 1.5, 2.0, 3.0})
    {
        icon.addPixmap(renderLucidePixmap(svgData, color, dpr), mode);
    }
}

bool isDarkToolbarTheme();
QColor toolbarColor(AppThemeColor color);

QIcon createLucideIcon(const QString& iconName, const QColor& color)
{
    static QHash<QString, QIcon> cache;
    const QColor disabledColor = toolbarColor(AppThemeColor::ToolbarDisabled);
    const QString cacheKey = QStringLiteral("%1:%2:%3")
        .arg(iconName)
        .arg(color.rgba(), 0, 16)
        .arg(disabledColor.rgba(), 0, 16);
    auto it = cache.constFind(cacheKey);
    if (it != cache.constEnd())
    {
        return it.value();
    }

    QFile file(findResourceFile(QStringLiteral("resources/lucide/%1.svg").arg(iconName)));
    if (!file.open(QIODevice::ReadOnly))
    {
        return QIcon();
    }

    const QByteArray svgData = file.readAll();
    QIcon icon;
    addLucideIconPixmaps(icon, svgData, color, QIcon::Normal);
    addLucideIconPixmaps(icon, svgData, disabledColor, QIcon::Disabled);
    cache.insert(cacheKey, icon);
    return icon;
}

QIcon createRotatedLucideIcon(const QString& iconName, const QColor& color, int degrees)
{
    static QHash<QString, QIcon> cache;
    const int normalizedDegrees = ((degrees % 360) + 360) % 360;
    const QString cacheKey = QStringLiteral("%1:%2:%3")
        .arg(iconName)
        .arg(color.rgba(), 0, 16)
        .arg(normalizedDegrees);
    auto it = cache.constFind(cacheKey);
    if (it != cache.constEnd())
    {
        return it.value();
    }

    const QPixmap source = createLucideIcon(iconName, color).pixmap(QSize(32, 32));
    if (source.isNull())
    {
        return QIcon();
    }

    QPixmap rotated(source.size());
    rotated.setDevicePixelRatio(source.devicePixelRatio());
    rotated.fill(Qt::transparent);

    const QSizeF logicalSize = source.deviceIndependentSize();
    QPainter painter(&rotated);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.translate(logicalSize.width() / 2.0, logicalSize.height() / 2.0);
    painter.rotate(normalizedDegrees);
    painter.translate(-logicalSize.width() / 2.0, -logicalSize.height() / 2.0);
    painter.drawPixmap(QPointF(0.0, 0.0), source);

    QIcon icon;
    icon.addPixmap(rotated, QIcon::Normal);
    icon.addPixmap(rotated, QIcon::Disabled);
    cache.insert(cacheKey, icon);
    return icon;
}

constexpr const char *kSectionTitleIconNameProperty = "_vv_section_title_icon_name";
constexpr const char *kSidebarIconNameProperty = "_vv_sidebar_icon_name";
constexpr const char *kSidebarCompactProperty = "_vv_sidebar_compact";
constexpr const char *kSidebarHoverProperty = "_vv_hover";
constexpr const char *kSidebarHoverParticipantProperty = "_vv_sidebar_hover_button";
constexpr const char *kTitleBarHoverProperty = "titleBarHover";
constexpr const char *kTitleBarHoverParticipantProperty = "_vv_title_bar_hover_button";
constexpr const char *kCustomLogoStateProperty = "_vv_logo_state";
constexpr int kSectionTitleIconBoxSize = 26;
constexpr int kSectionTitleIconSize = 22;

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

void setDangerTextPalette(QWidget *widget)
{
    if (!widget)
    {
        return;
    }

    QPalette palette = widget->palette();
    const QColor danger = appThemeColor(AppThemeColor::Danger, VaporView::isDarkThemeEnabled());
    palette.setColor(QPalette::WindowText, danger);
    palette.setColor(QPalette::Text, danger);
    widget->setPalette(palette);
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
    cluster->setAttribute(Qt::WA_TransparentForMouseEvents, true);
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
    updateSectionTitleIcon(iconLabel, VaporView::isDarkThemeEnabled());
    layout->addWidget(iconLabel, 0, Qt::AlignVCenter);

    auto *titleLabel = new VaporView::VisualTextLabel(cluster);
    titleLabel->setObjectName(QStringLiteral("sectionTitleLabel"));
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

QString vaporViewLogoResourcePath(bool dark)
{
    return findResourceFile(dark
        ? QStringLiteral("resources/VaproViewLOGO/VaporViewLOGO_rgb217_119_87.svg")
        : QStringLiteral("resources/VaproViewLOGO/VaporViewLOGO_black.svg"));
}

QPixmap renderVaporViewLogo(bool dark, int size, qreal devicePixelRatio)
{
    const QString path = vaporViewLogoResourcePath(dark);
    if (path.isEmpty())
    {
        return QPixmap();
    }

    const qreal dpr = std::max<qreal>(1.0, devicePixelRatio);
    const int logicalSize = std::max(1, size);
    const int physicalSize = std::max(1, qCeil(logicalSize * dpr));
    QPixmap pixmap(physicalSize, physicalSize);
    pixmap.setDevicePixelRatio(dpr);
    pixmap.fill(Qt::transparent);

    QSvgRenderer renderer(path);
    if (!renderer.isValid())
    {
        return QPixmap();
    }

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    renderer.render(&painter, QRectF(0, 0, logicalSize, logicalSize));
    return pixmap;
}

QIcon createVaporViewLogoIcon(bool dark)
{
    const QString path = vaporViewLogoResourcePath(dark);
    return path.isEmpty() ? QIcon() : QIcon(path);
}

QIcon createRefreshIcon()
{
    return createLucideIcon(QStringLiteral("refresh-cw"), toolbarColor(AppThemeColor::ToolbarBlue));
}

QIcon createConnectIcon()
{
    return createLucideIcon(QStringLiteral("link"), toolbarColor(AppThemeColor::ToolbarGreen));
}

QIcon createCancelIcon()
{
    return createLucideIcon(QStringLiteral("circle-x"), toolbarColor(AppThemeColor::ToolbarRed));
}

QIcon createDisconnectIcon()
{
    return createLucideIcon(QStringLiteral("unlink"), toolbarColor(AppThemeColor::ToolbarRed));
}

QIcon createPlayIcon()
{
    return createLucideIcon(QStringLiteral("play"), toolbarColor(AppThemeColor::ToolbarGreen));
}

QIcon createPauseIcon()
{
    return createLucideIcon(QStringLiteral("pause"), toolbarColor(AppThemeColor::ToolbarBlue));
}

QIcon createStopIcon()
{
    return createLucideIcon(QStringLiteral("square"), toolbarColor(AppThemeColor::ToolbarRed));
}

QIcon createTimerIcon()
{
    return createLucideIcon(QStringLiteral("timer"), toolbarColor(AppThemeColor::ToolbarBlue));
}

QIcon createRtkSatelliteIcon(bool running)
{
    return createLucideIcon(
        QStringLiteral("satellite"),
        running ? toolbarColor(AppThemeColor::ToolbarGreen)
                : appThemeColor(AppThemeColor::Primary, isDarkToolbarTheme()));
}

QIcon createClearLogIcon()
{
    return createLucideIcon(QStringLiteral("trash-2"), toolbarColor(AppThemeColor::ToolbarBlue));
}

QIcon createLogFilterIcon()
{
    return createLucideIcon(QStringLiteral("funnel"), toolbarColor(AppThemeColor::ToolbarBlue));
}

QIcon createLogSidePanelToggleIcon(bool collapsed)
{
    return createLucideIcon(collapsed ? QStringLiteral("panel-right-open")
                                      : QStringLiteral("panel-right-close"),
                            toolbarColor(AppThemeColor::ToolbarBlue));
}

QIcon createAppSidebarToggleIcon(bool collapsed)
{
    QIcon icon;
    const QIcon source = createLogSidePanelToggleIcon(collapsed);
    for (const QSize size : {QSize(32, 32), QSize(48, 48), QSize(64, 64), QSize(96, 96)})
    {
        QPixmap pixmap = source.pixmap(size);
        if (!pixmap.isNull())
        {
            icon.addPixmap(pixmap.transformed(QTransform().scale(-1, 1)));
        }
    }
    return icon;
}

QIcon createMenuCheckIcon()
{
    return createLucideIcon(QStringLiteral("check"), appThemeColor(AppThemeColor::MenuCheckText, true));
}

QIcon createMenuCheckIcon(bool dark)
{
    return createLucideIcon(QStringLiteral("check"), appThemeColor(AppThemeColor::MenuCheckText, dark));
}

QIcon createWaveformViewerIcon()
{
    return createLucideIcon(QStringLiteral("audio-waveform"), toolbarColor(AppThemeColor::ToolbarBlue));
}

QIcon createLanguageIcon()
{
    return createLucideIcon(QStringLiteral("languages"), toolbarColor(AppThemeColor::ToolbarBlue));
}

QIcon createDarkThemeIcon()
{
    return createLucideIcon(QStringLiteral("moon"), toolbarColor(AppThemeColor::ToolbarBlue));
}

QIcon createLightThemeIcon()
{
    return createLucideIcon(QStringLiteral("sun"), toolbarColor(AppThemeColor::ToolbarAmber));
}

QIcon createTitleBarIcon(const QString& iconName, bool dark)
{
    return createLucideIcon(iconName, dark
        ? appThemeColor(AppThemeColor::TextTitle, true)
        : appThemeColor(AppThemeColor::TextStrong, false));
}

bool isDarkToolbarTheme()
{
    if (qApp)
    {
        const QVariant value = qApp->property(kAppDarkThemeProperty);
        if (value.isValid())
        {
            return value.toBool();
        }
        const QPalette palette = qApp->palette();
        return palette.color(QPalette::Window).lightness() < 128 ||
               palette.color(QPalette::Base).lightness() < 128;
    }
    return false;
}

QColor toolbarColor(AppThemeColor color)
{
    return appThemeColor(color, isDarkToolbarTheme());
}

QString titleApplicationPanelStyleSheet(bool dark)
{
    if (dark)
    {
        return applyAppThemeTokens(QStringLiteral(R"(
QFrame#titleApplicationPanel,
QFrame#titleApplicationSubPanel,
QFrame#titleApplicationNestedPanel {
    background-color: transparent;
    border: none;
}
QFrame#titleApplicationMainMenu,
QFrame#titleApplicationSubMenu,
QFrame#titleApplicationNestedMenu {
    background-color: transparent;
    border: none;
    border-radius: 0px;
}
QFrame#titleApplicationMenuItem {
    background-color: transparent;
    border: none;
    border-radius: 0px;
}
QFrame#titleApplicationMenuItem[selected="true"],
QFrame#titleApplicationMenuItem:hover {
    background-color: @vv-menu-hover;
}
QLabel#titleApplicationMenuText {
    color: @vv-menu-text;
    background-color: transparent;
    border: none;
    padding: 0px;
}
QLabel#titleApplicationMenuShortcut,
QLabel#titleApplicationMenuArrow,
QLabel#titleApplicationMenuCheck {
    color: @vv-menu-meta;
    background-color: transparent;
    border: none;
    padding: 0px;
}
QLabel#titleApplicationMenuCheck {
    color: @vv-menu-check;
}
QLabel#titleApplicationMenuText:disabled,
QLabel#titleApplicationMenuShortcut:disabled,
QLabel#titleApplicationMenuArrow:disabled,
QLabel#titleApplicationMenuCheck:disabled {
    color: @vv-menu-disabled;
}
QWidget#titleApplicationSubPage {
    background-color: transparent;
    border: none;
}
QWidget#titleApplicationSubPageContent,
QStackedWidget#titleApplicationSubStack,
QScrollArea#titleApplicationSubScroll,
QScrollArea#titleApplicationSubScroll > QWidget,
QScrollArea#titleApplicationSubScroll > QWidget > QWidget {
    background-color: transparent;
    border: none;
}
)"), true);
    }

    return applyAppThemeTokens(QStringLiteral(R"(
QFrame#titleApplicationPanel,
QFrame#titleApplicationSubPanel,
QFrame#titleApplicationNestedPanel {
    background-color: transparent;
    border: none;
}
QFrame#titleApplicationMainMenu,
QFrame#titleApplicationSubMenu,
QFrame#titleApplicationNestedMenu {
    background-color: transparent;
    border: none;
    border-radius: 0px;
}
QFrame#titleApplicationMenuItem {
    background-color: transparent;
    border: none;
    border-radius: 0px;
}
QFrame#titleApplicationMenuItem[selected="true"],
QFrame#titleApplicationMenuItem:hover {
    background-color: @vv-menu-hover;
}
QLabel#titleApplicationMenuText {
    color: @vv-text;
    background-color: transparent;
    border: none;
    padding: 0px;
}
QLabel#titleApplicationMenuShortcut,
QLabel#titleApplicationMenuArrow,
QLabel#titleApplicationMenuCheck {
    color: @vv-menu-meta;
    background-color: transparent;
    border: none;
    padding: 0px;
}
QLabel#titleApplicationMenuCheck {
    color: @vv-menu-check;
}
QLabel#titleApplicationMenuText:disabled,
QLabel#titleApplicationMenuShortcut:disabled,
QLabel#titleApplicationMenuArrow:disabled,
QLabel#titleApplicationMenuCheck:disabled {
    color: @vv-menu-disabled;
}
QWidget#titleApplicationSubPage {
    background-color: transparent;
    border: none;
}
QWidget#titleApplicationSubPageContent,
QStackedWidget#titleApplicationSubStack,
QScrollArea#titleApplicationSubScroll,
QScrollArea#titleApplicationSubScroll > QWidget,
QScrollArea#titleApplicationSubScroll > QWidget > QWidget {
    background-color: transparent;
    border: none;
}
)"), false);
}

QString customTitleBarStyleSheet(bool dark)
{
    if (dark)
    {
        return QStringLiteral(R"(
QWidget#customTitleBar {
    background-color: @vv-window;
    border-bottom: 1px solid @vv-border;
}
QLabel#customTitleLabel {
    color: @vv-text-title;
    font-size: 15px;
    font-weight: 600;
    padding: 0px 8px;
}
QLabel#customTitleLogo {
    background-color: transparent;
    border: none;
    border-radius: 6px;
}
QLabel#customTitleLogo[titleBarHover="true"] {
    background-color: @vv-title-hover;
}
QToolButton#titleBarButton,
QToolButton#titleBarMenuButton,
QToolButton#windowMinimizeButton,
QToolButton#windowMaximizeButton,
QToolButton#windowCloseButton {
    background-color: transparent;
    border: none;
    border-radius: 6px;
    padding: 0px;
    margin: 0px;
}
QToolButton#titleBarButton:hover,
QToolButton#titleBarMenuButton:hover,
QToolButton#windowMinimizeButton:hover,
QToolButton#windowMaximizeButton:hover,
QToolButton#titleBarButton[titleBarHover="true"],
QToolButton#titleBarMenuButton[titleBarHover="true"],
QToolButton#windowMinimizeButton[titleBarHover="true"],
QToolButton#windowMaximizeButton[titleBarHover="true"] {
    background-color: @vv-title-hover;
}
QToolButton#windowCloseButton:hover,
QToolButton#windowCloseButton[titleBarHover="true"] {
    background-color: @vv-title-hover;
}
QWidget#customTitleBar QToolButton::menu-indicator {
    image: none;
    width: 0px;
    height: 0px;
}
QFrame#titleBarSeparator {
    background-color: @vv-border;
    border: none;
}
)");
    }

    return QStringLiteral(R"(
QWidget#customTitleBar {
    background-color: @vv-surface;
    border-bottom: 1px solid @vv-border;
}
QLabel#customTitleLabel {
    color: @vv-text;
    font-size: 15px;
    font-weight: 600;
    padding: 0px 8px;
}
QLabel#customTitleLogo {
    background-color: transparent;
    border: none;
    border-radius: 6px;
}
QLabel#customTitleLogo[titleBarHover="true"] {
    background-color: @vv-title-hover;
}
QToolButton#titleBarButton,
QToolButton#titleBarMenuButton,
QToolButton#windowMinimizeButton,
QToolButton#windowMaximizeButton,
QToolButton#windowCloseButton {
    background-color: transparent;
    border: none;
    border-radius: 6px;
    padding: 0px;
    margin: 0px;
}
QToolButton#titleBarButton:hover,
QToolButton#titleBarMenuButton:hover,
QToolButton#windowMinimizeButton:hover,
QToolButton#windowMaximizeButton:hover,
QToolButton#titleBarButton[titleBarHover="true"],
QToolButton#titleBarMenuButton[titleBarHover="true"],
QToolButton#windowMinimizeButton[titleBarHover="true"],
QToolButton#windowMaximizeButton[titleBarHover="true"] {
    background-color: @vv-title-hover;
}
QToolButton#titleBarButton:pressed,
QToolButton#titleBarMenuButton:pressed,
QToolButton#windowMinimizeButton:pressed,
QToolButton#windowMaximizeButton:pressed,
QToolButton#titleBarButton:checked,
QToolButton#titleBarMenuButton:checked,
QToolButton#windowMinimizeButton:checked,
QToolButton#windowMaximizeButton:checked {
    background-color: @vv-title-hover;
}
QToolButton#windowCloseButton:hover,
QToolButton#windowCloseButton[titleBarHover="true"] {
    background-color: @vv-close-hover;
}
QWidget#customTitleBar QToolButton::menu-indicator {
    image: none;
    width: 0px;
    height: 0px;
}
QFrame#titleBarSeparator {
    background-color: @vv-border;
    border: none;
}
)");
}

#ifdef Q_OS_WIN
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_CAPTION_COLOR
#define DWMWA_CAPTION_COLOR 35
#endif
#ifndef DWMWA_TEXT_COLOR
#define DWMWA_TEXT_COLOR 36
#endif
#ifndef DWMWA_COLOR_DEFAULT
#define DWMWA_COLOR_DEFAULT 0xFFFFFFFF
#endif

void setWindowsTitleBarDark(QWidget *window, bool dark)
{
    if (!window)
    {
        return;
    }

    HWND hwnd = reinterpret_cast<HWND>(window->winId());
    if (!hwnd)
    {
        return;
    }

    const BOOL useDark = dark ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &useDark, sizeof(useDark));

    const QColor themeCaptionColor = appThemeColor(AppThemeColor::Surface, true);
    const QColor themeTextColor = appThemeColor(AppThemeColor::Text, true);
    const COLORREF captionColor = dark
        ? RGB(themeCaptionColor.red(), themeCaptionColor.green(), themeCaptionColor.blue())
        : DWMWA_COLOR_DEFAULT;
    const COLORREF textColor = dark
        ? RGB(themeTextColor.red(), themeTextColor.green(), themeTextColor.blue())
        : DWMWA_COLOR_DEFAULT;
    DwmSetWindowAttribute(hwnd, DWMWA_CAPTION_COLOR, &captionColor, sizeof(captionColor));
    DwmSetWindowAttribute(hwnd, DWMWA_TEXT_COLOR, &textColor, sizeof(textColor));
}
#else
void setWindowsTitleBarDark(QWidget *, bool)
{
}
#endif

QString darkThemeStyleSheet()
{
    return QStringLiteral(R"(
QMainWindow {
    background-color: @vv-window;
}
QWidget#appCentralWidget,
QWidget#mainCardsPane,
QFrame#appSidebar,
QStackedWidget#mainPageStack,
QWidget#temperaturePage,
QWidget#deviceConfigPage,
QMainWindow#sessionViewerWindow,
QWidget#sessionViewerCentralWidget,
QWidget#sessionViewerViewport,
QWidget#sessionViewerContentPane,
QScrollArea#sessionViewerScrollArea,
QScrollArea#sessionViewerScrollArea > QWidget,
QScrollArea#sessionViewerScrollArea > QWidget > QWidget,
QSplitter#sessionViewerContentSplitter,
QScrollArea,
QScrollArea > QWidget,
QScrollArea > QWidget > QWidget,
QScrollArea#mainCardsScrollArea,
QWidget#mainCardsViewport,
QScrollArea#mainCardsScrollArea > QWidget,
QScrollArea#mainCardsScrollArea > QWidget > QWidget,
QAbstractScrollArea,
QSplitter {
    background-color: @vv-window;
}
QSplitter#appLayoutSplitter,
QSplitter#appLayoutSplitter > QWidget,
QSplitter#mainContentSplitter,
QSplitter#mainContentSplitter > QWidget,
QSplitter#homeOverviewSplitter,
QSplitter#homeOverviewSplitter > QWidget {
    background-color: @vv-window;
}
QMenuBar,
QToolBar,
QStatusBar,
QMenu,
QMessageBox {
    background-color: @vv-surface;
    color: @vv-text-title;
    border-color: @vv-border;
}
QMenuBar::item,
QMenu::item,
QToolBar QToolButton {
    color: @vv-text-title;
}
QToolBar QToolButton {
    background-color: @vv-primary;
}
QMenuBar::item:selected,
QMenu::item:selected,
QToolBar QToolButton:hover {
    background-color: @vv-primary;
    color: @vv-white;
}
QToolTip {
    background-color: rgb(253, 253, 252);
    color: #000000;
    border: 1px solid rgb(232, 232, 232);
    border-radius: 13px;
    padding: 8px 16px;
    font-size: 16px;
}
QMenuBar::item:pressed,
QToolBar QToolButton:pressed {
    background-color: @vv-primary;
    color: @vv-white;
}
QToolBar::separator {
    background-color: @vv-border;
}
QGroupBox {
    background-color: @vv-surface;
    border: 1px solid @vv-border;
    border-top: 40px solid @vv-surface;
    color: @vv-text-title;
}
QDialog#rtkConfigDialog,
QWidget#rtkConfigViewport,
QWidget#rtkConfigContent,
QScrollArea#rtkConfigScrollArea {
    background-color: @vv-window;
}
QDialog#rtkConfigDialog QGroupBox#rtkCardGroup {
    background-color: @vv-surface;
    border: 1px solid @vv-border;
    border-radius: 8px;
    margin-top: 0px;
    padding: 0px;
    color: @vv-text;
}
QDialog#rtkConfigDialog QGroupBox#rtkCardGroup::title {
    color: transparent;
}
QDialog#rtkConfigDialog QWidget#sectionTitleBar {
    background-color: @vv-surface;
    border: none;
    border-bottom: 1px solid @vv-border;
    border-top-left-radius: 7px;
    border-top-right-radius: 7px;
}
QDialog#rtkConfigDialog QLabel#sectionTitleLabel {
    background-color: transparent;
    border: none;
    color: @vv-text;
    margin: 0px;
    padding: 0px;
}
QGroupBox#sensorGroupBox {
    background-color: @vv-surface;
    border: 1px solid @vv-border;
    border-radius: 8px;
    margin-top: 0px;
    padding: 0px;
    color: @vv-text;
}
QFrame#appSidebar {
    background-color: @vv-surface;
    border-right: 1px solid @vv-border;
}
QPushButton#appSidebarButton {
    background-color: transparent;
    border: 1px solid transparent;
    border-radius: 6px;
    color: @vv-text;
    font-weight: 600;
    min-height: 34px;
    max-height: 34px;
    padding: 6px 8px;
    text-align: left;
    outline: none;
}
QPushButton#appSidebarButton:focus {
    outline: none;
}
QPushButton#appSidebarButton[_vv_sidebar_compact="true"] {
    min-width: 42px;
    max-width: 42px;
    min-height: 42px;
    max-height: 42px;
    padding: 0px;
    text-align: center;
    outline: none;
}
QPushButton#appSidebarButton:hover {
    background-color: @vv-primary-subtle;
    color: @vv-primary;
}
QPushButton#appSidebarButton:checked {
    background-color: @vv-primary;
    border-color: @vv-primary;
    color: @vv-white;
}
QPushButton#dangerButton {
    background-color: @vv-danger;
    border: 1px solid @vv-danger;
    border-radius: 6px;
    color: @vv-white;
    font-weight: 700;
    padding: 6px 14px;
}
QGroupBox#sensorRowContainer {
    background-color: transparent;
    border: none;
    border-radius: 0px;
    margin-top: 0px;
    padding: 0px;
}
QFrame#logPanelFrame {
    background-color: @vv-surface;
    border: 1px solid @vv-border;
    border-radius: 8px;
}
QWidget#logSidePanel {
    background-color: @vv-window;
    border: none;
}
QFrame#logPanelFrame QWidget#sectionTitleBar {
    background-color: @vv-surface;
    border: none;
    border-bottom: 1px solid @vv-border;
    border-top-left-radius: 7px;
    border-top-right-radius: 7px;
}
QFrame#logPanelFrame QToolButton#titleBarButton:hover {
    background-color: @vv-border;
}
QFrame#logPanelFrame QLabel#sectionTitleLabel {
    background-color: transparent;
    border: none;
    color: @vv-white;
}
QFrame#recordingStatusCard {
    background-color: @vv-surface;
    border: 1px solid @vv-border;
    border-radius: 8px;
}
QFrame#recordingStatusCard QWidget#sectionTitleBar {
    background-color: @vv-surface;
    border: none;
    border-bottom: 1px solid @vv-border;
    border-top-left-radius: 7px;
    border-top-right-radius: 7px;
}
QFrame#recordingStatusCard QLabel#sectionTitleLabel {
    background-color: transparent;
    border: none;
    color: @vv-white;
}
QWidget#recordingStatusBody {
    background-color: @vv-surface;
    border: none;
    border-bottom-left-radius: 7px;
    border-bottom-right-radius: 7px;
}
QLabel#recordingStatusLabel {
    background-color: transparent;
    border: none;
    color: @vv-white;
    font-size: 14px;
    font-weight: 600;
}
QWidget#sectionTitleBar,
QLabel#sectionTitleLabel {
    background-color: @vv-surface;
    border-color: @vv-border;
    color: @vv-white;
}
QWidget#environmentSectionTitleBar {
    background-color: @vv-surface;
    border-color: @vv-border;
}
QWidget#environmentSectionTitleBar QLabel,
QWidget#environmentSectionTitleBar QLabel#sectionTitleLabel {
    background-color: transparent;
    border: none;
    color: @vv-white;
    margin: 0px;
    padding: 0px;
}
QLabel {
    color: @vv-white;
}
QLabel#fieldLabel,
QLabel#rateLabel,
QLabel#separatorLabel {
    color: @vv-white;
    margin: 0px;
    padding: 0px;
}
QLabel#rtkStatusLabel {
    color: @vv-white;
    font-weight: bold;
}
QFrame#epsilonSectionCard {
    background-color: @vv-surface;
    border: 1px solid @vv-border;
    border-radius: 8px;
}
QWidget#homeTelemetrySummaryContainer {
    background-color: transparent;
    border: none;
}
QFrame#homeTelemetrySectionCard {
    background-color: @vv-surface;
    border: 1px solid @vv-border;
    border-radius: 6px;
}
QLabel#homeOverviewSectionTitle {
    color: @vv-primary;
    font-size: 14px;
    font-weight: 700;
}
QFrame#homeOverviewDivider {
    background-color: @vv-border;
    border: none;
    min-width: 1px;
    max-width: 1px;
}
QLabel#epsilonSectionLabel {
    color: @vv-white;
    background-color: @vv-surface;
    border: none;
    border-right: 1px solid @vv-border;
    font-weight: 700;
}
QLabel#valueLabel {
    color: @vv-white;
    background-color: transparent;
    font-family: "Consolas", "Monaco", "Courier New", monospace;
    font-size: 14px;
    font-weight: 600;
}
QLabel#highlightedValue {
    color: @vv-white;
    background-color: @vv-border;
    font-family: "Cascadia Mono", "Consolas", "Courier New", monospace;
}
PtbPanel QLabel#highlightedValue,
HmpPanel QLabel#highlightedValue,
LidarPanel QLabel#highlightedValue,
TemperatureControllerPanel QLabel#highlightedValue {
    font-family: "Consolas", "Monaco", "Courier New", monospace;
    font-size: 14px;
    font-weight: 600;
    background-color: transparent;
    padding: 0px;
    border-radius: 0px;
}
QLabel#rateLabel {
    font-family: "Cascadia Mono", "Consolas", "Courier New", monospace;
    margin: 0px;
    padding: 0px;
}
QComboBox,
QLineEdit,
QSpinBox,
QDoubleSpinBox,
QTextEdit {
    background-color: @vv-surface;
    border: 1px solid @vv-border;
    color: @vv-text;
    selection-background-color: @vv-primary-subtle-pressed;
    selection-color: @vv-white;
}
QTextEdit#logTextEdit {
    background-color: @vv-surface;
    border: none;
    border-radius: 0px;
}
QWidget#logTextViewport {
    background-color: @vv-surface;
    border: none;
}
QComboBox:hover,
QLineEdit:hover,
QSpinBox:hover,
QDoubleSpinBox:hover {
    border-color: @vv-border;
}
QComboBox:focus,
QLineEdit:focus,
QSpinBox:focus,
QDoubleSpinBox:focus {
    border-color: @vv-focus;
}
QComboBox:disabled,
QLineEdit:disabled,
QSpinBox:disabled,
QDoubleSpinBox:disabled {
    background-color: @vv-border;
    color: @vv-text-disabled;
}
QComboBox QAbstractItemView {
    background-color: @vv-menu-panel;
    border: none;
    border-radius: 12px;
    color: @vv-menu-text;
    selection-background-color: @vv-menu-hover;
    selection-color: @vv-menu-text;
    padding: 12px 0px;
    outline: none;
}
QComboBox QAbstractItemView::item {
    background-color: transparent;
    color: @vv-menu-text;
    padding: 7px 14px;
    min-height: 30px;
    border: 0px;
    border-radius: 0px;
}
QComboBox QAbstractItemView::item:hover,
QComboBox QAbstractItemView::item:selected,
QComboBox QAbstractItemView::item:selected:active,
QComboBox QAbstractItemView::item:selected:!active {
    background-color: @vv-menu-hover;
    color: @vv-menu-text;
}
QComboBox QAbstractItemView::item:disabled {
    background-color: transparent;
    color: @vv-menu-disabled;
}
QComboBox QAbstractItemView::item:selected:disabled {
    background-color: @vv-menu-hover;
    color: @vv-menu-disabled;
}
QPushButton {
    background-color: @vv-primary;
    color: @vv-white;
    border: none;
}
QPushButton:hover,
QPushButton:pressed,
QPushButton:checked {
    background-color: @vv-primary;
    color: @vv-white;
}
QPushButton:disabled {
    background-color: @vv-border;
    color: @vv-text-disabled-strong;
}
TemperatureControllerPanel QFrame#temperatureChannelTopBar {
    background-color: @vv-surface-alt;
    border: 1px solid @vv-border;
    border-radius: 8px;
}
QScrollBar:vertical {
    background-color: @vv-surface-sunken;
    width: 12px;
    border: none;
    border-radius: 6px;
    margin: 14px 0px 14px 0px;
}
QScrollBar::handle:vertical {
    background-color: @vv-scrollbar-handle;
    min-height: 30px;
    border-radius: 6px;
    border: 2px solid @vv-surface-sunken;
    margin: 0px;
}
QScrollBar::handle:vertical:hover {
    background-color: @vv-scrollbar-handle-hover;
}
QScrollBar::add-page:vertical,
QScrollBar::sub-page:vertical {
    background-color: @vv-surface-sunken;
    border-radius: 6px;
}
QScrollBar::add-page:vertical:hover,
QScrollBar::sub-page:vertical:hover,
QScrollBar::add-page:vertical:pressed,
QScrollBar::sub-page:vertical:pressed {
    background-color: @vv-surface-sunken;
}
QScrollBar::add-line:vertical,
QScrollBar::sub-line:vertical {
    background-color: @vv-surface-sunken;
    border: none;
    height: 14px;
    subcontrol-origin: margin;
}
QScrollBar::sub-line:vertical {
    border-top-left-radius: 6px;
    border-top-right-radius: 6px;
    subcontrol-position: top;
}
QScrollBar::add-line:vertical {
    border-bottom-left-radius: 6px;
    border-bottom-right-radius: 6px;
    subcontrol-position: bottom;
}
QScrollBar::add-line:vertical:hover,
QScrollBar::sub-line:vertical:hover,
QScrollBar::add-line:vertical:pressed,
QScrollBar::sub-line:vertical:pressed {
    background-color: @vv-surface-sunken;
}
QScrollBar:horizontal {
    background-color: @vv-surface-sunken;
    height: 12px;
    border: none;
    border-radius: 6px;
    margin: 0px;
}
QScrollBar::handle:horizontal {
    background-color: @vv-scrollbar-handle;
    min-width: 30px;
    border-radius: 6px;
    border: 2px solid @vv-surface-sunken;
    margin: 0px;
}
QScrollBar::handle:horizontal:hover {
    background-color: @vv-scrollbar-handle-hover;
}
QScrollBar::add-page:horizontal,
QScrollBar::sub-page:horizontal {
    background-color: @vv-surface-sunken;
    border-radius: 6px;
}
QScrollBar::add-page:horizontal:hover,
QScrollBar::sub-page:horizontal:hover,
QScrollBar::add-page:horizontal:pressed,
QScrollBar::sub-page:horizontal:pressed {
    background-color: @vv-surface-sunken;
}
QScrollBar::add-line:horizontal,
QScrollBar::sub-line:horizontal {
    width: 0px;
    background-color: @vv-surface-sunken;
}
QSplitter::handle,
QSplitter#mainContentSplitter::handle:horizontal {
    background-color: @vv-window;
}
QSplitter#appLayoutSplitter::handle:horizontal {
    width: 8px;
    background-color: @vv-window;
}
QSplitter#appLayoutSplitter::handle:horizontal:hover {
    background-color: @vv-window;
}
QSplitter#appLayoutSplitter::handle:horizontal:pressed {
    background-color: @vv-window;
}
QSplitter#homeOverviewSplitter::handle:horizontal {
    width: 8px;
    background-color: @vv-window;
}
QSplitter#homeOverviewSplitter::handle:horizontal:hover {
    background-color: @vv-border;
}
QSplitter#homeOverviewSplitter::handle:horizontal:pressed {
    background-color: @vv-border;
}
QWidget#mainCardResizeHandle {
    min-height: 3px;
    max-height: 3px;
    background-color: @vv-window;
}
QSplitter#mainContentSplitter::handle:horizontal:hover {
    background-color: @vv-border;
}
QWidget#mainCardResizeHandle:hover {
    background-color: @vv-border;
}
QSplitter#mainContentSplitter::handle:horizontal:pressed {
    background-color: @vv-border;
}
QWidget#mainCardResizeHandle[dragging="true"] {
    background-color: @vv-border;
}
QCheckBox,
QRadioButton {
    color: @vv-text-title;
}
QCheckBox::indicator,
QRadioButton::indicator {
    background-color: @vv-surface;
    border-color: @vv-border;
}
QLabel[data-valid="true"] {
    color: @vv-white;
}
QLabel[data-valid="false"] {
    color: @vv-white;
}
QLabel#homeDeviceStatusCapsule {
    background-color: @vv-surface-alt;
    border: 1px solid @vv-border;
    border-radius: 12px;
    color: @vv-text-title;
    font-size: 12px;
    font-weight: 700;
    padding: 2px 8px;
}
QLabel#homeDeviceStatusCapsule[connected="true"] {
    background-color: @vv-hd-ok-bg;
    border: 1px solid @vv-hd-ok;
    color: @vv-hd-ok;
}
QLabel#homeDeviceStatusCapsule[connected="false"] {
    background-color: @vv-hd-bad-bg;
    border: 1px solid @vv-hd-bad;
    color: @vv-hd-bad;
}
QLabel#homeDeviceStatusCapsule[state="disabled"] {
    background-color: @vv-surface-alt;
    border: 1px solid @vv-border;
    color: @vv-text-muted;
}
QLabel#homeDeviceStatusCapsule[state="disconnected"] {
    background-color: @vv-hd-bad-bg;
    border: 1px solid @vv-hd-bad;
    color: @vv-hd-bad;
}
QLabel#homeDeviceStatusCapsule[state="connecting"] {
    background-color: @vv-primary-subtle;
    border: 1px solid @vv-primary;
    color: @vv-primary;
}
QLabel#homeDeviceStatusCapsule[state="connected"] {
    background-color: @vv-hd-ok-bg;
    border: 1px solid @vv-hd-ok;
    color: @vv-hd-ok;
}
QToolButton#homeDeviceActionButton {
    background-color: @vv-surface-alt;
    border: 1px solid @vv-border;
    border-radius: 7px;
    padding: 2px;
}
QToolButton#homeDeviceActionButton:disabled {
    background-color: @vv-surface-alt;
    border-color: @vv-border;
}
QToolButton#homeDeviceActionButton[state="disconnected"] {
    background-color: @vv-hd-ok-bg;
    border-color: @vv-hd-ok;
}
QToolButton#homeDeviceActionButton[state="connecting"] {
    background-color: @vv-hd-ok-bg;
    border-color: @vv-hd-ok;
}
QToolButton#homeDeviceActionButton[state="connected"] {
    background-color: @vv-hd-bad-bg;
    border-color: @vv-hd-bad;
}
QToolButton#homeDeviceActionButton:hover {
    background-color: @vv-primary-subtle;
    border-color: @vv-border-strong;
}
QLabel#statusIndicator[status="connected"] {
    background-color: @vv-success-bg;
    color: @vv-success;
}
QLabel#statusIndicator[status="disconnected"] {
    background-color: @vv-danger-bg;
    color: @vv-danger;
}
QLabel#statusIndicator[status="warning"] {
    background-color: @vv-warning-bg;
    color: @vv-warning;
}
)") + QStringLiteral(R"(
QMenu {
    background-color: @vv-menu-panel;
    border: 1px solid @vv-border;
    border-radius: 10px;
    color: @vv-menu-text;
    padding: 12px 0px;
}
QMenu::item {
    background-color: transparent;
    border: none;
    border-radius: 0px;
    color: @vv-menu-text;
    padding: 8px 32px 8px 16px;
}
QMenu::item:selected {
    background-color: @vv-menu-hover;
    color: @vv-menu-text;
}
QMenu::item:disabled {
    background-color: transparent;
    color: @vv-menu-disabled;
}
)");
}

QString darkOverviewStyleSheet()
{
    return QStringLiteral(R"(
QFrame#deviceTelemetrySectionTitlePane {
    background-color: @vv-surface-alt;
    border: none;
    border-right: 1px solid @vv-border;
    border-top-left-radius: 6px;
    border-bottom-left-radius: 6px;
}
QLabel#deviceTelemetrySectionTitleLabel {
    background-color: transparent;
    border: none;
    color: @vv-text-strong;
    font-size: 13px;
    font-weight: 700;
    padding: 0px;
    margin: 0px;
}
QFrame#homeTelemetrySummaryPill {
    background-color: @vv-field-bg;
    border: 1px solid @vv-border;
    border-radius: 8px;
    padding: 0px;
    margin: 0px;
}
QFrame#homeTelemetrySummaryPill QLabel {
    background-color: transparent;
    border: none;
    color: @vv-text;
    font-size: 13px;
    font-weight: 600;
    padding: 0px;
    margin: 0px;
}
QFrame#homeTelemetrySummaryPill QLabel#homeTelemetrySummaryValueLabel {
    font-family: "Cascadia Mono", "Consolas", "Courier New", monospace;
}
QFrame#homeTelemetrySummaryPill QLabel[telemetryAvailable="false"] {
    color: @vv-text-muted;
}
QLabel#homeTelemetrySummaryNameLabel[deviceConfigLink="true"] {
    color: @vv-text-strong;
    font-size: 14px;
    font-weight: 700;
}
QLabel#homeTelemetrySummaryValueLabel[deviceConfigLink="true"] {
    color: @vv-text-strong;
    font-size: 14px;
    font-weight: 600;
}
QLabel#homeTelemetrySummaryTitleLabel[skyTelemetryTitle="true"] {
    color: @vv-primary;
}
QLabel#temperatureOverviewValuePill {
    background-color: @vv-surface-alt;
    border: 1px solid @vv-border;
    border-radius: 10px;
    color: @vv-text-strong;
}
QToolButton#temperatureOverviewChannelButton {
    background-color: @vv-surface-alt;
    border: 1px solid @vv-border;
    color: @vv-primary;
}
QToolButton#temperatureOverviewChannelButton[available="false"] {
    background-color: @vv-surface-alt;
    border-color: @vv-border;
    color: @vv-text-muted;
}
QPushButton#temperatureOverviewOutputSwitch {
    background-color: transparent;
    color: @vv-text;
}
)");
}

QString mainCardsScrollBarBackgroundStyleSheet(bool dark)
{
    const QString background = dark ? QStringLiteral("@vv-window") : QStringLiteral("@vv-surface");
    return QStringLiteral(
        "QScrollArea#mainCardsScrollArea QScrollBar:vertical, "
        "QScrollArea#mainCardsScrollArea QScrollBar:horizontal, "
        "QScrollArea#mainCardsScrollArea QScrollBar::add-page:vertical, "
        "QScrollArea#mainCardsScrollArea QScrollBar::sub-page:vertical, "
        "QScrollArea#mainCardsScrollArea QScrollBar::add-page:horizontal, "
        "QScrollArea#mainCardsScrollArea QScrollBar::sub-page:horizontal, "
        "QScrollArea#mainCardsScrollArea QScrollBar::add-line:vertical, "
        "QScrollArea#mainCardsScrollArea QScrollBar::sub-line:vertical, "
        "QScrollArea#mainCardsScrollArea QScrollBar::add-line:horizontal, "
        "QScrollArea#mainCardsScrollArea QScrollBar::sub-line:horizontal { "
        "background-color: %1; }"
        "QScrollArea#mainCardsScrollArea QScrollBar::handle:vertical, "
        "QScrollArea#mainCardsScrollArea QScrollBar::handle:horizontal { "
        "border: 2px solid %1; }")
        .arg(background);
}

QString rtkConfigCardStyleSheet()
{
    return QStringLiteral(
        "QDialog#rtkConfigDialog QGroupBox#sensorGroupBox { "
        "background-color: @vv-surface; "
        "border: 1px solid @vv-border; "
        "border-top: 1px solid @vv-border; "
        "border-radius: 8px; "
        "margin-top: 0px; "
        "padding: 0px; "
        "color: @vv-text; "
        "}"
        "QDialog#rtkConfigDialog QGroupBox#sensorGroupBox::title { "
        "color: transparent; "
        "height: 0px; "
        "margin: 0px; "
        "padding: 0px; "
        "}");
}

#pragma pack(push, 1)
struct UnifiedRawFileHeader
{
    char magic[8];
    quint32 version;
    quint32 header_size;
    quint16 source_id;
    quint16 reserved;
};

struct UnifiedRawRecordHeader
{
    quint32 marker;
    quint32 header_size;
    quint64 host_timestamp_us;
    quint32 payload_size;
    quint16 source_id;
    quint16 record_type;
    quint32 flags;
    quint64 sequence;
};
#pragma pack(pop)

QString recordingTimestampUtc()
{
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
}

QString recordingSessionDirectoryTimestamp()
{
    return QDateTime::currentDateTime().toString("yyyy-MM-dd_HH-mm-ss");
}

QString csvEscape(const QString &value)
{
    QString escaped = value;
    escaped.replace("\"", "\"\"");
    if (escaped.contains(',') || escaped.contains('"') || escaped.contains('\n') || escaped.contains('\r'))
    {
        escaped = QString("\"%1\"").arg(escaped);
    }
    return escaped;
}

QString csvBool(bool value)
{
    return value ? QStringLiteral("true") : QStringLiteral("false");
}

QString imuFrameTypeName(VaporView::ImuFrameType type)
{
    switch (type)
    {
    case VaporView::ImuFrameType::HI81:
        return QStringLiteral("HI81");
    case VaporView::ImuFrameType::HI83:
        return QStringLiteral("HI83");
    case VaporView::ImuFrameType::HI91:
        return QStringLiteral("HI91");
    case VaporView::ImuFrameType::HI92:
        return QStringLiteral("HI92");
    case VaporView::ImuFrameType::Unknown:
    default:
        return QStringLiteral("Unknown");
    }
}

QString imuRatePeriodText(int hz)
{
    switch (hz)
    {
    case 1: return QStringLiteral("1");
    case 2: return QStringLiteral("0.5");
    case 5: return QStringLiteral("0.2");
    case 10: return QStringLiteral("0.1");
    case 20: return QStringLiteral("0.05");
    case 50: return QStringLiteral("0.02");
    case 100: return QStringLiteral("0.01");
    case 200: return QStringLiteral("0.005");
    case 250: return QStringLiteral("0.004");
    case 500: return QStringLiteral("0.002");
    case 1000: return QStringLiteral("0.001");
    default: return QString();
    }
}

void applyComboText(QComboBox *combo, const QString& value)
{
    if (!combo || value.isEmpty())
    {
        return;
    }
    const QSignalBlocker blocker(combo);
    const int idx = combo->findText(value);
    if (idx >= 0)
    {
        combo->setCurrentIndex(idx);
        return;
    }
    if (combo->isEditable())
    {
        combo->setEditText(value);
    }
    else
    {
        combo->setCurrentText(value);
    }
}

QString sourceModeDisplayText(bool english, int index)
{
    return index == 1
        ? (english ? QStringLiteral("Sky-Ground Remote Mode") : QStringLiteral("天地远程模式"))
        : (english ? QStringLiteral("Local") : QStringLiteral("本地"));
}

QString sourceModeStorageValue(int index)
{
    return index == 1 ? QStringLiteral("remote") : QStringLiteral("local");
}

QString skyTelemetryTransportDisplayText(bool english, const QString& transport)
{
    return transport == QStringLiteral("serial")
        ? (english ? QStringLiteral("Serial") : QStringLiteral("串口"))
        : QStringLiteral("TCP");
}

void updateSkyTelemetryTransportComboTexts(QComboBox *combo, bool english)
{
    if (!combo)
    {
        return;
    }

    const QSignalBlocker blocker(combo);
    for (int i = 0; i < combo->count(); ++i)
    {
        combo->setItemText(i, skyTelemetryTransportDisplayText(english, combo->itemData(i).toString()));
    }
}

int sourceModeIndexFromStoredValue(const QString& value)
{
    const QString normalized = value.trimmed().toLower();
    if (normalized == QStringLiteral("remote") ||
        normalized == QStringLiteral("remote sky") ||
        normalized == QStringLiteral("1") ||
        normalized.contains(QStringLiteral("sky")) ||
        normalized.contains(QStringLiteral("天空")) ||
        normalized.contains(QStringLiteral("天地")) ||
        normalized.contains(QStringLiteral("远程")))
    {
        return 1;
    }
    if (normalized == QStringLiteral("local") ||
        normalized == QStringLiteral("0") ||
        normalized.contains(QStringLiteral("本地")))
    {
        return 0;
    }
    return -1;
}

bool shouldMirrorToErrorLog(const QString& message)
{
    static const QStringList keywords = {
        QStringLiteral("error"),
        QStringLiteral("failed"),
        QStringLiteral("timeout"),
        QStringLiteral("exception"),
        QStringLiteral("disconnect"),
        QStringLiteral("异常"),
        QStringLiteral("失败"),
        QStringLiteral("错误"),
        QStringLiteral("超时"),
        QStringLiteral("掉线"),
        QStringLiteral("断开"),
    };
    const QString lower = message.toLower();
    for (const QString& keyword : keywords)
    {
        if (lower.contains(keyword.toLower()))
        {
            return true;
        }
    }
    return false;
}
void rememberBaseMetric(QObject *object, const char *propertyName, int value)
{
    if (!object->property(propertyName).isValid())
    {
        object->setProperty(propertyName, value);
    }
}

struct EpsilonPacketConfigOption
{
    quint8 packet_id = 0;
    const char *message_name = nullptr;
    const char *title_zh = nullptr;
    const char *title_en = nullptr;
    std::vector<int> supported_rates_hz;
};

constexpr int kEpsilonPacketConfigApplyVersion = 2;

const std::vector<EpsilonPacketConfigOption>& epsilonPacketConfigOptions()
{
    static const std::vector<EpsilonPacketConfigOption> kOptions = {
        {0x40, "MSG_IMU", "IMU原始数据", "IMU Raw Data", {0, 1, 2, 5, 10, 20, 50, 100, 200, 250, 500, 1000}},
        {0x41, "MSG_AHRS", "AHRS姿态解", "AHRS Attitude", {0, 1, 2, 5, 10, 20, 50, 100, 250, 500}},
        {0x42, "MSG_INSGPS", "INS/GPS融合解", "INS/GPS Navigation", {0, 1, 2, 5, 10, 20, 50, 100, 250, 500}},
        {0x50, "MSG_SYS_STATE", "系统状态", "System State", {0, 1, 2, 5, 10, 20, 50, 100, 250, 500}},
        {0x59, "MSG_RAW_GNSS", "原始GNSS", "Raw GNSS", {0, 1, 2, 5, 10, 20, 50, 100, 250, 500}},
        {0x5A, "MSG_SATELLITE", "卫星汇总", "Satellite Summary", {0, 1, 2, 5, 10, 20, 50, 100, 250, 500}},
        {0x5C, "MSG_GEODETIC_POS", "大地坐标", "Geodetic Position", {0, 1, 2, 5, 10, 20, 50, 100, 250, 500}},
        {0x5D, "MSG_ECEF_POS", "ECEF坐标", "ECEF Position", {0, 1, 2, 5, 10, 20, 50, 100, 250, 500}},
    };
    return kOptions;
}

QString epsilonPacketRateSettingsKey(quint8 packetId)
{
    return QStringLiteral("epsilon_custom_packet_rate_%1")
        .arg(packetId, 2, 16, QLatin1Char('0'))
        .toUpper();
}

int nearestSupportedEpsilonPacketRate(const EpsilonPacketConfigOption& option, int desiredRateHz)
{
    int fallbackRateHz = 0;
    for (int rateHz : option.supported_rates_hz)
    {
        if (rateHz == desiredRateHz)
        {
            return rateHz;
        }
        if (rateHz <= desiredRateHz)
        {
            fallbackRateHz = rateHz;
        }
    }
    return fallbackRateHz;
}

std::map<uint8_t, int> groupedEpsilonPacketRates(int baseRateHz)
{
    const int lowRateHz = std::min(baseRateHz, 20);
    std::map<uint8_t, int> rates;
    for (const EpsilonPacketConfigOption& option : epsilonPacketConfigOptions())
    {
        const int desiredRateHz =
            (option.packet_id == 0x59 || option.packet_id == 0x5A ||
             option.packet_id == 0x5C || option.packet_id == 0x5D)
                ? lowRateHz
                : baseRateHz;
        rates[option.packet_id] = nearestSupportedEpsilonPacketRate(option, desiredRateHz);
    }
    return rates;
}

std::map<uint8_t, int> defaultEpsilonPacketRates()
{
    return {
        {0x40, 250},
        {0x41, 50},
        {0x42, 100},
        {0x50, 100},
        {0x59, 10},
        {0x5A, 1},
        {0x5C, 10},
        {0x5D, 10},
    };
}

bool epsilonPacketRateSupported(const EpsilonPacketConfigOption& option, int rateHz)
{
    return std::find(option.supported_rates_hz.cbegin(), option.supported_rates_hz.cend(), rateHz) != option.supported_rates_hz.cend();
}

std::map<uint8_t, int> loadCustomEpsilonPacketRates(QSettings& settings, int fallbackBaseRateHz)
{
    std::map<uint8_t, int> packetRates = groupedEpsilonPacketRates(fallbackBaseRateHz);
    for (const EpsilonPacketConfigOption& option : epsilonPacketConfigOptions())
    {
        const int fallbackRate = packetRates[option.packet_id];
        const int storedRate = settings.value(epsilonPacketRateSettingsKey(option.packet_id), fallbackRate).toInt();
        packetRates[option.packet_id] = epsilonPacketRateSupported(option, storedRate) ? storedRate : fallbackRate;
    }
    return packetRates;
}

std::map<uint8_t, int> effectiveEpsilonPacketRates(QSettings& settings, int baseRateHz, bool *usingCustomProfile = nullptr)
{
    bool useCustomProfile = settings.value("epsilon_custom_packet_rates_enabled", false).toBool();
    if (useCustomProfile &&
        !settings.value("epsilon_custom_packet_rates_user_saved", false).toBool() &&
        loadCustomEpsilonPacketRates(settings, baseRateHz) == defaultEpsilonPacketRates())
    {
        useCustomProfile = false;
    }
    if (usingCustomProfile)
    {
        *usingCustomProfile = useCustomProfile;
    }
    return useCustomProfile ? loadCustomEpsilonPacketRates(settings, baseRateHz) : groupedEpsilonPacketRates(baseRateHz);
}

QString epsilonPacketRatesSignature(const std::map<uint8_t, int>& packetRates)
{
    QStringList parts;
    for (const EpsilonPacketConfigOption& option : epsilonPacketConfigOptions())
    {
        const auto it = packetRates.find(option.packet_id);
        const int rateHz = (it != packetRates.end()) ? it->second : -1;
        parts << QStringLiteral("%1=%2")
                     .arg(option.packet_id, 2, 16, QLatin1Char('0'))
                     .toUpper()
                     .arg(rateHz);
    }
    return parts.join(';');
}

QString epsilonPacketRatesSummary(const std::map<uint8_t, int>& packetRates)
{
    QStringList parts;
    for (const EpsilonPacketConfigOption& option : epsilonPacketConfigOptions())
    {
        const auto it = packetRates.find(option.packet_id);
        if (it == packetRates.end())
        {
            continue;
        }
        parts << QStringLiteral("%1=%2Hz")
                     .arg(option.packet_id, 2, 16, QLatin1Char('0'))
                     .toUpper()
                     .arg(it->second);
    }
    return parts.join(QStringLiteral(", "));
}

int epsilonPacketCallbackRate(const std::map<uint8_t, int>& packetRates, int fallbackRateHz)
{
    int maxRateHz = 0;
    for (const auto& entry : packetRates)
    {
        maxRateHz = std::max(maxRateHz, entry.second);
    }
    return maxRateHz > 0 ? maxRateHz : fallbackRateHz;
}

QString epsilonPacketDialogRowLabel(const EpsilonPacketConfigOption& option, bool english)
{
    if (english)
    {
        return QStringLiteral("%1 [%2]")
            .arg(QString::fromLatin1(option.message_name))
            .arg(option.packet_id, 2, 16, QLatin1Char('0'));
    }
    return QStringLiteral("%1 [%2]")
        .arg(QString::fromUtf8(option.title_zh))
        .arg(option.packet_id, 2, 16, QLatin1Char('0'));
}

QString epsilonPacketRateDisplayText(int rateHz, bool english)
{
    return rateHz == 0
        ? (english ? QStringLiteral("No Output (0 Hz)") : QStringLiteral("不输出 (0 Hz)"))
        : QStringLiteral("%1 Hz").arg(rateHz);
}
}

class EpsilonPanel : public QWidget
{
public:
    explicit EpsilonPanel(QLabel *rateLabel = nullptr, QWidget *parent = nullptr)
        : QWidget(parent)
        , rate_label_(rateLabel)
        , cards_layout_(nullptr)
        , current_card_columns_(0)
        , is_english_(false)
        , compact_layout_(false)
        , total_rate_hz_(0.0)
        , imu_packet_rate_hz_(0.0)
        , ahrs_packet_rate_hz_(0.0)
        , insgps_packet_rate_hz_(0.0)
        , sys_state_packet_rate_hz_(0.0)
        , raw_gnss_packet_rate_hz_(0.0)
        , satellite_packet_rate_hz_(0.0)
        , geodetic_packet_rate_hz_(0.0)
        , ecef_packet_rate_hz_(0.0)
    {
        setObjectName(QStringLiteral("epsilonPanel"));
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        setupUi();
        setEnglish(false);
    }

    void updateRate(double hz)
    {
        total_rate_hz_ = hz;
        refreshRateLabel();
    }

    void setEnglish(bool english)
    {
        is_english_ = english;
        refreshRateLabel();
        for (auto it = title_labels_.cbegin(); it != title_labels_.cend(); ++it)
        {
            it.value()->setText(english ? title_en_.value(it.key()) : title_zh_.value(it.key()));
        }
        for (auto it = section_labels_.cbegin(); it != section_labels_.cend(); ++it)
        {
            const QString text = english ? section_en_.value(it.key()) : section_zh_.value(it.key());
            it.value()->setText(formatSectionTitle(text, english));
        }
        updateCardGridLayout(true);
    }

    void setCompactLayout(bool compact)
    {
        const bool changed = compact_layout_ != compact;
        compact_layout_ = compact;
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        updateCardGridLayout(changed);
        QTimer::singleShot(0, this, [this]() {
            updateCardGridLayout(true);
        });
        if (layout())
        {
            layout()->invalidate();
            layout()->activate();
        }
        updateGeometry();
    }

    void updateData(const VaporView::EpsilonData& epsilon_data)
    {
        auto setValue = [this](const QString& key, const QString& value) {
            if (QLabel *label = value_labels_.value(key, nullptr))
            {
                const QString display_text = value.isEmpty() ? QStringLiteral("--") : value;
                label->setText(display_text);
                label->setToolTip(display_text);
            }
        };
        if (!epsilon_data.valid)
        {
            total_rate_hz_ = 0.0;
            imu_packet_rate_hz_ = 0.0;
            ahrs_packet_rate_hz_ = 0.0;
            insgps_packet_rate_hz_ = 0.0;
            sys_state_packet_rate_hz_ = 0.0;
            raw_gnss_packet_rate_hz_ = 0.0;
            satellite_packet_rate_hz_ = 0.0;
            geodetic_packet_rate_hz_ = 0.0;
            ecef_packet_rate_hz_ = 0.0;
            refreshRateLabel();
            for (QLabel *label : value_labels_)
            {
                if (label)
                {
                    label->setText(QStringLiteral("--"));
                    label->setToolTip(QStringLiteral("--"));
                }
            }
            return;
        }
        auto scalar = [](double value, int decimals) {
            if (!std::isfinite(value))
            {
                return QString();
            }
            return QString::number(value, 'f', decimals);
        };
        auto valueTriple = [](double a, double b, double c, int decimals) {
            if (!std::isfinite(a) || !std::isfinite(b) || !std::isfinite(c))
            {
                return QString();
            }
            return QStringLiteral("%1/%2/%3")
                .arg(a, 0, 'f', decimals)
                .arg(b, 0, 'f', decimals)
                .arg(c, 0, 'f', decimals);
        };
        const bool gnss_fix_valid = epsilon_data.gnss_fix_code >= 2;
        const bool utc_valid = epsilon_data.utc_unix_s > 0;

        const QString utcText = utc_valid
            ? QDateTime::fromSecsSinceEpoch(static_cast<qint64>(epsilon_data.utc_unix_s), QTimeZone::UTC)
                  .addMSecs(static_cast<qint64>(epsilon_data.utc_microseconds / 1000U))
                  .toString(Qt::ISODateWithMs)
            : QString();

        setValue(QStringLiteral("time_utc"), utcText);
        setValue(QStringLiteral("device_ts"), epsilon_data.device_timestamp_us > 0
            ? QStringLiteral("%1 us").arg(epsilon_data.device_timestamp_us)
            : QString());
        imu_packet_rate_hz_ = epsilon_data.imu_packet_rate_hz;
        ahrs_packet_rate_hz_ = epsilon_data.ahrs_packet_rate_hz;
        insgps_packet_rate_hz_ = epsilon_data.insgps_packet_rate_hz;
        sys_state_packet_rate_hz_ = epsilon_data.sys_state_packet_rate_hz;
        raw_gnss_packet_rate_hz_ = epsilon_data.raw_gnss_packet_rate_hz;
        satellite_packet_rate_hz_ = epsilon_data.satellite_packet_rate_hz;
        geodetic_packet_rate_hz_ = epsilon_data.geodetic_packet_rate_hz;
        ecef_packet_rate_hz_ = epsilon_data.ecef_packet_rate_hz;
        refreshRateLabel();
        setValue(QStringLiteral("fix"), QString::fromStdString(epsilon_data.gnss_fix_text));
        setValue(QStringLiteral("sat"), epsilon_data.gnss_satellites > 0 ? QString::number(epsilon_data.gnss_satellites) : QString());
        setValue(QStringLiteral("lat"), gnss_fix_valid ? scalar(epsilon_data.latitude_deg, 8) : QString());
        setValue(QStringLiteral("lon"), gnss_fix_valid ? scalar(epsilon_data.longitude_deg, 8) : QString());
        setValue(QStringLiteral("height"), gnss_fix_valid ? scalar(epsilon_data.height_m, 3) : QString());
        setValue(QStringLiteral("ned_vel"), gnss_fix_valid
            ? valueTriple(epsilon_data.vel_n_mps, epsilon_data.vel_e_mps, epsilon_data.vel_d_mps, 3)
            : QString());
        setValue(QStringLiteral("imu_acc"),
                 valueTriple(epsilon_data.imu_acc_x_mps2, epsilon_data.imu_acc_y_mps2, epsilon_data.imu_acc_z_mps2, 3));
        setValue(QStringLiteral("imu_gyr"),
                 valueTriple(epsilon_data.imu_gyr_x_radps, epsilon_data.imu_gyr_y_radps, epsilon_data.imu_gyr_z_radps, 4));
        setValue(QStringLiteral("rpy"),
                 valueTriple(epsilon_data.roll_deg, epsilon_data.pitch_deg, epsilon_data.yaw_deg, 2));
        setValue(QStringLiteral("acc"),
                 gnss_fix_valid && std::isfinite(epsilon_data.hacc_m) && std::isfinite(epsilon_data.vacc_m)
                     ? QStringLiteral("%1m/%2m")
                           .arg(epsilon_data.hacc_m, 0, 'f', 3)
                           .arg(epsilon_data.vacc_m, 0, 'f', 3)
                     : QString());
        setValue(QStringLiteral("heading_valid"), boolText(epsilon_data.heading_valid));
        setValue(QStringLiteral("status_bits"), formatSystemStatus(epsilon_data.system_status_bits));
        setValue(QStringLiteral("filter_bits"), formatFilterStatus(epsilon_data.filter_status_bits, gnss_fix_valid));
        setValue(QStringLiteral("frames"),
                 is_english_
                     ? QStringLiteral("raw %1 / dropped %2")
                           .arg(epsilon_data.raw_frame_count)
                           .arg(epsilon_data.dropped_frame_count)
                     : QStringLiteral("原始 %1 / 丢帧 %2")
                           .arg(epsilon_data.raw_frame_count)
                           .arg(epsilon_data.dropped_frame_count));
    }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QWidget::resizeEvent(event);
        updateCardGridLayout(true);
    }

private:
    QString formatRateValue(double hz) const
    {
        constexpr int kRateFieldChars = 9; // "-999.9 Hz" keeps signs and separators stable.
        QString text;
        if (!(hz > 0.0) || !std::isfinite(hz))
        {
            text = QStringLiteral("-- Hz");
        }
        else
        {
            text = QStringLiteral("%1 Hz").arg(hz, 0, 'f', 1);
        }
        return text.rightJustified(std::max(kRateFieldChars, static_cast<int>(text.size())), QLatin1Char(' '));
    }

    QString boolText(bool value) const
    {
        if (is_english_)
        {
            return value ? QStringLiteral("Yes") : QStringLiteral("No");
        }
        return value ? QStringLiteral("是") : QStringLiteral("否");
    }

    QString formatHex16(quint16 value) const
    {
        return QStringLiteral("0x%1").arg(value, 4, 16, QLatin1Char('0')).toUpper();
    }

    QString formatSystemStatus(quint16 bits) const
    {
        if (bits == 0)
        {
            return is_english_
                ? QStringLiteral("%1 OK").arg(formatHex16(bits))
                : QStringLiteral("%1 正常").arg(formatHex16(bits));
        }
        return is_english_
            ? QStringLiteral("%1 Check").arg(formatHex16(bits))
            : QStringLiteral("%1 需检查").arg(formatHex16(bits));
    }

    QString formatFilterStatus(quint16 bits, bool fusionActive) const
    {
        QStringList states;
        if (bits == 0)
        {
            states << (is_english_ ? QStringLiteral("not initialized") : QStringLiteral("未初始化"));
        }
        else
        {
            states << (is_english_ ? QStringLiteral("initialized") : QStringLiteral("已初始化"));
            if (fusionActive)
            {
                states << (is_english_ ? QStringLiteral("position fusion active") : QStringLiteral("定位融合中"));
            }
        }
        return QStringLiteral("%1 %2").arg(formatHex16(bits), states.join(QStringLiteral(" / ")));
    }

    void refreshRateLabel()
    {
        if (!rate_label_)
        {
            return;
        }

        const QString totalText = is_english_
            ? QStringLiteral("Total Rate: %1").arg(formatRateValue(total_rate_hz_))
            : QStringLiteral("总频率：%1").arg(formatRateValue(total_rate_hz_));
        rate_label_->setText(totalText);
        rate_label_->setToolTip(totalText);
    }

    static QString formatSectionTitle(const QString& text, bool english)
    {
        if (english)
        {
            QString formatted = text;
            formatted.replace(QStringLiteral(" / "), QStringLiteral("\n/\n"));
            formatted.replace(QChar(' '), QChar('\n'));
            return formatted;
        }

        QStringList chars;
        chars.reserve(text.size());
        for (const QChar ch : text)
        {
            if (!ch.isSpace())
            {
                chars.append(QString(ch));
            }
        }
        return chars.join(QChar('\n'));
    }

    void registerSectionLabel(QLabel *label,
                              const QString& key,
                              const QString& zhTitle,
                              const QString& enTitle)
    {
        label->setObjectName(QStringLiteral("epsilonSectionLabel"));
        QFont font = label->font();
        font.setBold(true);
        label->setFont(font);
        label->setAlignment(Qt::AlignCenter);
        label->setFixedWidth(kEpsilonSideTitleWidth);
        label->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
        section_labels_.insert(key, label);
        section_zh_.insert(key, zhTitle);
        section_en_.insert(key, enTitle);
    }

    int availableCardWidth() const
    {
        int availableWidth = contentsRect().width();
        if (const QWidget *parent = parentWidget())
        {
            int parentWidth = parent->contentsRect().width() - 4;
            if (const QWidget *grandParent = parent->parentWidget())
            {
                parentWidth = std::max(parentWidth, grandParent->contentsRect().width() - 8);
            }
            if (parentWidth > 0)
            {
                availableWidth = std::max(availableWidth, parentWidth);
            }
        }
        return availableWidth;
    }

    int desiredCardColumns() const
    {
        if (section_cards_.isEmpty())
        {
            return 1;
        }
        if (!compact_layout_)
        {
            return 3;
        }

        const int availableWidth = availableCardWidth();
        if (availableWidth <= 0)
        {
            return compact_layout_ ? 1 : 3;
        }

        const QVector<int> widths = standardCardWidths();
        const int gap = std::max(0, cards_layout_ ? cards_layout_->horizontalSpacing() : 0);
        const int cardCount = widths.size();
        int allWidth = 0;
        for (int width : widths)
        {
            allWidth += width;
        }
        allWidth += gap * std::max(0, cardCount - 1);
        if (cardCount >= 3 && availableWidth >= allWidth)
        {
            return 3;
        }

        if (cardCount >= 2)
        {
            const int firstRowWidth = widths.at(0) + widths.at(1) + gap;
            const int wrappedRowWidth = cardCount >= 3 ? widths.at(2) : 0;
            if (availableWidth >= std::max(firstRowWidth, wrappedRowWidth))
            {
                return 2;
            }
        }

        return 1;
    }

    QVector<int> standardCardWidths() const
    {
        QVector<int> widths;
        widths.reserve(section_card_standard_widths_.size());
        for (int width : section_card_standard_widths_)
        {
            widths.push_back(width);
        }
        return widths;
    }

    int fieldSpacingForSection(const QString& key) const
    {
        return key == QStringLiteral("motion")
            ? kEpsilonMotionFieldSpacing
            : kEpsilonFieldBaseSpacing;
    }

    int fieldSpacingForCard(int cardIndex) const
    {
        const QString key = cardIndex >= 0 && cardIndex < section_card_keys_.size()
            ? section_card_keys_.at(cardIndex)
            : QString();
        return fieldSpacingForSection(key);
    }

    QStringList valueWidthSamplesForSection(const QString& key) const
    {
        if (key == QStringLiteral("status"))
        {
            return is_english_
                ? QStringList{QStringLiteral("2026-07-01T23:59:59.999Z"),
                              QStringLiteral("1782934672910000 us"),
                              QStringLiteral("raw 9999 / dropped 999"),
                              QStringLiteral("0X0060 initialized / position fusion active"),
                              QStringLiteral("Yes")}
                : QStringList{QStringLiteral("2026-07-01T23:59:59.999Z"),
                              QStringLiteral("1782934672910000 us"),
                              QStringLiteral("原始 9999 / 丢帧 999"),
                              QStringLiteral("0X0060 已初始化 / 定位融合中"),
                              QStringLiteral("是")};
        }
        if (key == QStringLiteral("position"))
        {
            return QStringList{QStringLiteral("RTK_FIXED"),
                               QStringLiteral("99"),
                               QStringLiteral("-179.99999999"),
                               QStringLiteral("-9999.999"),
                               QStringLiteral("0.999m/0.999m")};
        }
        if (key == QStringLiteral("motion"))
        {
            return QStringList{QStringLiteral("-12.345/12.345/-12.345"),
                               QStringLiteral("-12.345/-12.345/12.345"),
                               QStringLiteral("-0.1234/-0.1234/0.1234"),
                               QStringLiteral("-179.99/-89.99/359.99")};
        }
        return {};
    }

    bool syncSectionColumnWidths()
    {
        const int count = std::min(section_card_grids_.size(), section_card_title_labels_.size());
        bool changed = false;
        for (int i = 0; i < count; ++i)
        {
            int titleWidth = 0;
            for (QLabel *label : section_card_title_labels_.at(i))
            {
                if (!label)
                {
                    continue;
                }
                titleWidth = std::max(titleWidth,
                                      std::max(label->sizeHint().width(),
                                               QFontMetrics(label->font()).horizontalAdvance(label->text())));
            }
            int valueWidth = 0;
            if (i < section_card_value_labels_.size())
            {
                for (QLabel *label : section_card_value_labels_.at(i))
                {
                    if (!label)
                    {
                        continue;
                    }
                    label->ensurePolished();
                    valueWidth = std::max(valueWidth, label->fontMetrics().horizontalAdvance(label->text()));
                }
            }
            const QString sectionKey = i < section_card_keys_.size() ? section_card_keys_.at(i) : QString();
            for (const QString& sample : valueWidthSamplesForSection(sectionKey))
            {
                if (i < section_card_value_labels_.size() && !section_card_value_labels_.at(i).isEmpty())
                {
                    QLabel *sampleLabel = section_card_value_labels_.at(i).constFirst();
                    if (sampleLabel)
                    {
                        sampleLabel->ensurePolished();
                        valueWidth = std::max(valueWidth, sampleLabel->fontMetrics().horizontalAdvance(sample));
                    }
                }
            }
            valueWidth += 2;
            QGridLayout *grid = section_card_grids_.at(i);
            if (!grid)
            {
                continue;
            }
            if (grid->columnMinimumWidth(0) != titleWidth)
            {
                grid->setColumnMinimumWidth(0, titleWidth);
                changed = true;
            }
            if (grid->columnMinimumWidth(1) != valueWidth)
            {
                grid->setColumnMinimumWidth(1, valueWidth);
                changed = true;
            }
            for (QLabel *label : section_card_title_labels_.at(i))
            {
                if (label)
                {
                    if (label->width() != titleWidth ||
                        label->minimumWidth() != titleWidth ||
                        label->maximumWidth() != titleWidth)
                    {
                        label->setFixedWidth(titleWidth);
                        changed = true;
                    }
                }
            }
            if (i < section_card_value_labels_.size())
            {
                for (QLabel *label : section_card_value_labels_.at(i))
                {
                    if (!label)
                    {
                        continue;
                    }
                    if (label->minimumWidth() != valueWidth ||
                        label->maximumWidth() != QWIDGETSIZE_MAX)
                    {
                        label->setMinimumWidth(valueWidth);
                        label->setMaximumWidth(QWIDGETSIZE_MAX);
                        changed = true;
                    }
                }
            }
            if (i < section_card_standard_widths_.size() &&
                i < section_card_chrome_widths_.size() &&
                i < section_card_value_widths_.size())
            {
                const int standardWidth = section_card_chrome_widths_.at(i) +
                                          titleWidth +
                                          fieldSpacingForCard(i) +
                                          valueWidth;
                if (section_card_standard_widths_.at(i) != standardWidth)
                {
                    section_card_standard_widths_[i] = standardWidth;
                    changed = true;
                }
                if (i < section_cards_.size() &&
                    section_cards_.at(i) &&
                    section_cards_.at(i)->minimumWidth() != standardWidth)
                {
                    section_cards_.at(i)->setMinimumWidth(standardWidth);
                    changed = true;
                }
                if (section_card_value_widths_.at(i) != valueWidth)
                {
                    section_card_value_widths_[i] = valueWidth;
                    changed = true;
                }
            }
        }
        return changed;
    }

    void setCardsExpandable(bool expandable)
    {
        for (QFrame *card : section_cards_)
        {
            card->setSizePolicy(expandable ? QSizePolicy::Expanding : QSizePolicy::Maximum,
                                expandable ? QSizePolicy::Expanding : QSizePolicy::Preferred);
        }
    }

    void syncCardMinimumHeights()
    {
        int maximumMinimumHeight = 0;
        for (QFrame *card : section_cards_)
        {
            if (QLayout *cardLayout = card->layout())
            {
                maximumMinimumHeight = std::max(maximumMinimumHeight, cardLayout->minimumSize().height());
            }
        }
        for (QFrame *card : section_cards_)
        {
            if (maximumMinimumHeight > 0)
            {
                card->setMinimumHeight(maximumMinimumHeight);
            }
        }
    }

    void updateFieldSpacingForCard(int cardIndex, int targetCardWidth)
    {
        if (cardIndex < 0 ||
            cardIndex >= section_card_grids_.size() ||
            cardIndex >= section_cards_.size() ||
            cardIndex >= section_card_standard_widths_.size())
        {
            return;
        }

        Q_UNUSED(targetCardWidth);
        section_card_grids_.at(cardIndex)->setHorizontalSpacing(fieldSpacingForCard(cardIndex));
    }

    void updateFieldSpacings(int columns, const QVector<int>& widths)
    {
        if (widths.isEmpty())
        {
            return;
        }

        const int availableWidth = std::max(0, availableCardWidth());
        const int gap = std::max(0, cards_layout_ ? cards_layout_->horizontalSpacing() : 0);
        if (columns >= 3)
        {
            for (int i = 0; i < widths.size(); ++i)
            {
                updateFieldSpacingForCard(i, widths.at(i));
            }
            return;
        }

        if (columns == 2 && widths.size() >= 3)
        {
            const int firstRowAvailableWidth = std::max(0, availableWidth - gap);
            const int firstRowStandardWidth = std::max(1, widths.at(0) + widths.at(1));
            const int firstWidth = std::max(widths.at(0), firstRowAvailableWidth * widths.at(0) / firstRowStandardWidth);
            const int secondWidth = std::max(widths.at(1), firstRowAvailableWidth - firstWidth);
            updateFieldSpacingForCard(0, firstWidth);
            updateFieldSpacingForCard(1, secondWidth);
            updateFieldSpacingForCard(2, availableWidth);
            return;
        }

        for (int i = 0; i < widths.size(); ++i)
        {
            updateFieldSpacingForCard(i, availableWidth);
        }
    }

    void updateCardGridLayout(bool force = false)
    {
        if (!cards_layout_ || section_cards_.isEmpty())
        {
            return;
        }

        cards_layout_->setHorizontalSpacing(compact_layout_ ? 4 : 2);
        cards_layout_->setVerticalSpacing(compact_layout_ ? 4 : 2);
        const bool widthsChanged = syncSectionColumnWidths();
        syncCardMinimumHeights();
        const QVector<int> widths = standardCardWidths();

        const int columns = desiredCardColumns();
        updateFieldSpacings(columns, widths);
        if (!force && current_card_columns_ == columns && !widthsChanged)
        {
            return;
        }

        for (QFrame *card : section_cards_)
        {
            cards_layout_->removeWidget(card);
        }
        for (int i = 0; i < 5; ++i)
        {
            cards_layout_->setColumnStretch(i, 0);
            cards_layout_->setRowStretch(i, 0);
            cards_layout_->setColumnMinimumWidth(i, 0);
        }

        if (columns >= 3)
        {
            setCardsExpandable(true);
            for (int i = 0; i < section_cards_.size(); ++i)
            {
                if (i < widths.size())
                {
                    cards_layout_->setColumnMinimumWidth(i, widths.at(i));
                    cards_layout_->setColumnStretch(i, std::max(1, widths.at(i)));
                }
                cards_layout_->addWidget(section_cards_.at(i), 0, i);
            }
        }
        else if (columns == 2 && section_cards_.size() >= 3)
        {
            setCardsExpandable(true);
            cards_layout_->setColumnMinimumWidth(0, widths.at(0));
            cards_layout_->setColumnMinimumWidth(1, widths.at(1));
            cards_layout_->setColumnStretch(0, std::max(1, widths.at(0)));
            cards_layout_->setColumnStretch(1, std::max(1, widths.at(1)));
            cards_layout_->addWidget(section_cards_.at(0), 0, 0);
            cards_layout_->addWidget(section_cards_.at(1), 0, 1);
            cards_layout_->addWidget(section_cards_.at(2), 1, 0, 1, 2);
        }
        else
        {
            setCardsExpandable(true);
            cards_layout_->setColumnStretch(0, 1);
            for (int i = 0; i < section_cards_.size(); ++i)
            {
                cards_layout_->addWidget(section_cards_.at(i), i, 0);
            }
        }

        current_card_columns_ = columns;
        cards_layout_->invalidate();
        cards_layout_->activate();
        updateGeometry();
    }

    QGridLayout *addSectionCard(const QString& key,
                                const QString& zhTitle,
                                const QString& enTitle,
                                int titleColumnWidth,
                                int valueColumnWidth)
    {
        auto *card = new QFrame(this);
        card->setObjectName(QStringLiteral("epsilonSectionCard"));
        card->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Maximum);
        section_cards_.push_back(card);

        auto *outerLayout = new QHBoxLayout(card);
        outerLayout->setContentsMargins(2, 2, 2, 2);
        outerLayout->setSpacing(2);

        auto *sectionLabel = new QLabel(card);
        registerSectionLabel(sectionLabel, key, zhTitle, enTitle);
        outerLayout->addWidget(sectionLabel);

        auto *cardLayout = new QGridLayout();
        cardLayout->setContentsMargins(2, 2, 2, 2);
        cardLayout->setHorizontalSpacing(fieldSpacingForSection(key));
        cardLayout->setVerticalSpacing(2);
        cardLayout->setColumnMinimumWidth(0, titleColumnWidth);
        cardLayout->setColumnMinimumWidth(1, valueColumnWidth);
        cardLayout->setColumnStretch(0, 0);
        cardLayout->setColumnStretch(1, 1);
        outerLayout->addLayout(cardLayout, 1);
        section_card_grids_.push_back(cardLayout);
        section_card_keys_.push_back(key);
        section_card_title_labels_.push_back({});
        section_card_value_labels_.push_back({});
        const int chromeWidth = kEpsilonSideTitleWidth + outerLayout->contentsMargins().left() +
                                outerLayout->contentsMargins().right() + outerLayout->spacing() +
                                cardLayout->contentsMargins().left() + cardLayout->contentsMargins().right();
        section_card_chrome_widths_.push_back(chromeWidth);
        section_card_value_widths_.push_back(valueColumnWidth);
        section_card_standard_widths_.push_back(chromeWidth + titleColumnWidth + fieldSpacingForSection(key) + valueColumnWidth);
        return cardLayout;
    }

    void addField(QGridLayout *layout,
                  int row,
                  int column,
                  const QString& key,
                  const QString& zhTitle,
                  const QString& enTitle,
                  int valueColumnWidth)
    {
        QLabel *title = new QLabel(this);
        title->setObjectName(QStringLiteral("fieldLabel"));
        title->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        title->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        if (const int titleColumnWidth = layout->columnMinimumWidth(column * 2); titleColumnWidth > 0)
        {
            title->setFixedWidth(titleColumnWidth);
        }
        title->setMinimumHeight(kEpsilonFieldMinimumHeight);
        QLabel *value = new QLabel(QStringLiteral("--"), this);
        value->setObjectName(QStringLiteral("valueLabel"));
        value->setTextInteractionFlags(Qt::TextSelectableByMouse);
        value->setWordWrap(true);
        value->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        value->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        value->setMinimumHeight(kEpsilonFieldMinimumHeight);
        value->setMinimumWidth(valueColumnWidth);
        layout->setRowMinimumHeight(row, kEpsilonFieldMinimumHeight);
        layout->addWidget(title, row, column * 2);
        layout->addWidget(value, row, column * 2 + 1);
        title_labels_.insert(key, title);
        value_labels_.insert(key, value);
        title_zh_.insert(key, zhTitle);
        title_en_.insert(key, enTitle);
        const int cardIndex = section_card_grids_.indexOf(layout);
        if (cardIndex >= 0 && cardIndex < section_card_title_labels_.size())
        {
            section_card_title_labels_[cardIndex].push_back(title);
        }
        if (cardIndex >= 0 && cardIndex < section_card_value_labels_.size())
        {
            section_card_value_labels_[cardIndex].push_back(value);
        }
    }

    void setupUi()
    {
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(2, 2, 2, 2);
        layout->setSpacing(2);

        if (!rate_label_)
        {
            rate_label_ = new VaporView::VisualTextLabel(this);
            rate_label_->setObjectName(QStringLiteral("rateLabel"));
            rate_label_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
            layout->addWidget(rate_label_);
        }
        rate_label_->setFont(numericFontFrom(rate_label_->font()));
        rate_label_->setMinimumWidth(0);
        rate_label_->setMaximumWidth(QWIDGETSIZE_MAX);
        rate_label_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);

        cards_layout_ = new QGridLayout();
        cards_layout_->setContentsMargins(0, 0, 0, 0);
        cards_layout_->setHorizontalSpacing(2);
        cards_layout_->setVerticalSpacing(2);

        QGridLayout *statusGrid = addSectionCard(QStringLiteral("status"),
                                                 QStringLiteral("总体状态"),
                                                 QStringLiteral("Overall Status"),
                                                 kEpsilonTitleColumnWidth,
                                                 kEpsilonLeftValueColumnWidth);
        int row = 0;
        addField(statusGrid, row++, 0, QStringLiteral("time_utc"), QStringLiteral("UTC时间:"), QStringLiteral("UTC Time:"), kEpsilonLeftValueColumnWidth);
        addField(statusGrid, row++, 0, QStringLiteral("device_ts"), QStringLiteral("设备时间戳:"), QStringLiteral("Device Timestamp:"), kEpsilonLeftValueColumnWidth);
        addField(statusGrid, row++, 0, QStringLiteral("frames"), QStringLiteral("原始帧/丢帧:"), QStringLiteral("Raw/Dropped Frames:"), kEpsilonLeftValueColumnWidth);
        addField(statusGrid, row++, 0, QStringLiteral("status_bits"), QStringLiteral("系统状态:"), QStringLiteral("System Status:"), kEpsilonLeftValueColumnWidth);
        addField(statusGrid, row++, 0, QStringLiteral("filter_bits"), QStringLiteral("滤波状态:"), QStringLiteral("Filter Status:"), kEpsilonLeftValueColumnWidth);
        addField(statusGrid, row++, 0, QStringLiteral("heading_valid"), QStringLiteral("航向有效:"), QStringLiteral("Heading Valid:"), kEpsilonLeftValueColumnWidth);

        QGridLayout *positionGrid = addSectionCard(QStringLiteral("position"),
                                                   QStringLiteral("定位状态"),
                                                   QStringLiteral("Position Status"),
                                                   kEpsilonTitleColumnWidth,
                                                   kEpsilonPositionValueColumnWidth);
        row = 0;
        addField(positionGrid, row++, 0, QStringLiteral("fix"), QStringLiteral("GNSS状态:"), QStringLiteral("GNSS Fix:"), kEpsilonPositionValueColumnWidth);
        addField(positionGrid, row++, 0, QStringLiteral("sat"), QStringLiteral("卫星数:"), QStringLiteral("Satellites:"), kEpsilonPositionValueColumnWidth);
        addField(positionGrid, row++, 0, QStringLiteral("lat"), QStringLiteral("纬度[deg]:"), QStringLiteral("Latitude [deg]:"), kEpsilonPositionValueColumnWidth);
        addField(positionGrid, row++, 0, QStringLiteral("lon"), QStringLiteral("经度[deg]:"), QStringLiteral("Longitude [deg]:"), kEpsilonPositionValueColumnWidth);
        addField(positionGrid, row++, 0, QStringLiteral("height"), QStringLiteral("高度[m]:"), QStringLiteral("Height [m]:"), kEpsilonPositionValueColumnWidth);
        addField(positionGrid, row++, 0, QStringLiteral("acc"), QStringLiteral("hAcc/vAcc:"), QStringLiteral("hAcc/vAcc:"), kEpsilonPositionValueColumnWidth);

        QGridLayout *motionGrid = addSectionCard(QStringLiteral("motion"),
                                                 QStringLiteral("姿态与运动"),
                                                 QStringLiteral("Attitude / Motion"),
                                                 kEpsilonMotionTitleColumnWidth,
                                                 kEpsilonMotionValueColumnWidth);
        row = 0;
        addField(motionGrid, row++, 0, QStringLiteral("ned_vel"), QStringLiteral("NED速度[m/s][N/E/D]:"), QStringLiteral("NED Velocity [m/s][N/E/D]:"), kEpsilonMotionValueColumnWidth);
        addField(motionGrid, row++, 0, QStringLiteral("imu_acc"), QStringLiteral("IMU加速度[m/s²][X/Y/Z]:"), QStringLiteral("IMU Accel [m/s²][X/Y/Z]:"), kEpsilonMotionValueColumnWidth);
        addField(motionGrid, row++, 0, QStringLiteral("imu_gyr"), QStringLiteral("IMU角速度[rad/s][X/Y/Z]:"), QStringLiteral("IMU Gyro [rad/s][X/Y/Z]:"), kEpsilonMotionValueColumnWidth);
        addField(motionGrid, row++, 0, QStringLiteral("rpy"), QStringLiteral("姿态角[deg][Roll/Pitch/Yaw]:"), QStringLiteral("Attitude [deg][Roll/Pitch/Yaw]:"), kEpsilonMotionValueColumnWidth);

        updateCardGridLayout(true);
        layout->addLayout(cards_layout_, 0);
        layout->addStretch(1);
    }

    QLabel *rate_label_;
    QGridLayout *cards_layout_;
    QVector<QFrame*> section_cards_;
    QVector<QGridLayout*> section_card_grids_;
    QVector<QString> section_card_keys_;
    QVector<int> section_card_standard_widths_;
    QVector<int> section_card_chrome_widths_;
    QVector<int> section_card_value_widths_;
    QVector<QVector<QLabel*>> section_card_title_labels_;
    QVector<QVector<QLabel*>> section_card_value_labels_;
    int current_card_columns_;
    QHash<QString, QLabel*> section_labels_;
    QHash<QString, QLabel*> title_labels_;
    QHash<QString, QLabel*> value_labels_;
    QHash<QString, QString> section_zh_;
    QHash<QString, QString> section_en_;
    QHash<QString, QString> title_zh_;
    QHash<QString, QString> title_en_;
    bool is_english_;
    bool compact_layout_;
    double total_rate_hz_;
    double imu_packet_rate_hz_;
    double ahrs_packet_rate_hz_;
    double insgps_packet_rate_hz_;
    double sys_state_packet_rate_hz_;
    double raw_gnss_packet_rate_hz_;
    double satellite_packet_rate_hz_;
    double geodetic_packet_rate_hz_;
    double ecef_packet_rate_hz_;
};

GnssPanel::GnssPanel(QWidget *parent)
    : QWidget(parent)
    , rate_label_(nullptr)
    , status_label_(nullptr)
    , time_label_(nullptr)
    , lat_label_(nullptr)
    , lon_label_(nullptr)
    , alt_label_(nullptr)
    , vel_n_label_(nullptr)
    , vel_e_label_(nullptr)
    , vel_ground_label_(nullptr)
    , heading_label_(nullptr)
    , pitch_label_(nullptr)
    , heading_len_label_(nullptr)
    , heading_type_label_(nullptr)
    , heading_sats_label_(nullptr)
    , sats_label_(nullptr)
    , gdop_label_(nullptr)
    , pdop_label_(nullptr)
    , hdop_label_(nullptr)
    , htdop_label_(nullptr)
    , tdop_label_(nullptr)
    , diff_age_label_(nullptr)
    , undulation_label_(nullptr)
    , sigma_lat_label_(nullptr)
    , sigma_lon_label_(nullptr)
    , sigma_alt_label_(nullptr)
    , cutoff_label_(nullptr)
    , status_lbl_(nullptr)
    , time_lbl_(nullptr)
    , lat_lbl_(nullptr)
    , lon_lbl_(nullptr)
    , alt_lbl_(nullptr)
    , vel_n_lbl_(nullptr)
    , vel_e_lbl_(nullptr)
    , vel_ground_lbl_(nullptr)
    , heading_lbl_(nullptr)
    , pitch_lbl_(nullptr)
    , heading_type_lbl_(nullptr)
    , heading_len_lbl_(nullptr)
    , heading_sats_lbl_(nullptr)
    , sats_lbl_(nullptr)
    , diff_lbl_(nullptr)
    , gdop_lbl_(nullptr)
    , pdop_lbl_(nullptr)
    , hdop_lbl_(nullptr)
    , htdop_lbl_(nullptr)
    , tdop_lbl_(nullptr)
    , cutoff_lbl_(nullptr)
    , undulation_lbl_(nullptr)
    , sigma_lat_lbl_(nullptr)
    , sigma_lon_lbl_(nullptr)
    , sigma_alt_lbl_(nullptr)
    , is_english_(false)
{
    setupUi();
}

void GnssPanel::setupUi()
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(4);
    mainLayout->setContentsMargins(6, 2, 6, 6);

    rate_label_ = new VaporView::VisualTextLabel(this);
    rate_label_->setObjectName("rateLabel");
    rate_label_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    setFixedNumericLabelWidth(rate_label_, {QStringLiteral("-999.9 Hz"), QStringLiteral("999.9 Hz"), QStringLiteral("-- Hz")}, 4);
    mainLayout->addWidget(rate_label_, 0, Qt::AlignRight);

    auto *colsLayout = new QHBoxLayout();
    colsLayout->setSpacing(12);

    auto *leftLayout = new QGridLayout();
    leftLayout->setVerticalSpacing(4);
    leftLayout->setHorizontalSpacing(1);

    auto *midLayout = new QGridLayout();
    midLayout->setVerticalSpacing(4);
    midLayout->setHorizontalSpacing(1);

    auto *rightLayout = new QGridLayout();
    rightLayout->setVerticalSpacing(4);
    rightLayout->setHorizontalSpacing(1);

    auto createRow = [](QGridLayout* grid, int row, QLabel*& lbl, QLabel*& valueLabel, QWidget* parent) {
        lbl = new QLabel(parent);
        lbl->setObjectName("fieldLabel");
        lbl->setMinimumHeight(22);
        valueLabel = new QLabel("---", parent);
        valueLabel->setObjectName("valueLabel");
        valueLabel->setMinimumHeight(22);
        grid->addWidget(lbl, row, 0);
        grid->addWidget(valueLabel, row, 1);
    };

    createRow(leftLayout, 0, status_lbl_, status_label_, this);
    createRow(leftLayout, 1, time_lbl_, time_label_, this);
    createRow(leftLayout, 2, lat_lbl_, lat_label_, this);
    createRow(leftLayout, 3, lon_lbl_, lon_label_, this);
    createRow(leftLayout, 4, alt_lbl_, alt_label_, this);
    createRow(leftLayout, 5, sigma_lat_lbl_, sigma_lat_label_, this);
    createRow(leftLayout, 6, sigma_lon_lbl_, sigma_lon_label_, this);
    createRow(leftLayout, 7, sigma_alt_lbl_, sigma_alt_label_, this);
    createRow(leftLayout, 8, undulation_lbl_, undulation_label_, this);

    createRow(midLayout, 0, vel_n_lbl_, vel_n_label_, this);
    createRow(midLayout, 1, vel_e_lbl_, vel_e_label_, this);
    createRow(midLayout, 2, vel_ground_lbl_, vel_ground_label_, this);
    createRow(midLayout, 3, heading_lbl_, heading_label_, this);
    createRow(midLayout, 4, pitch_lbl_, pitch_label_, this);
    createRow(midLayout, 5, heading_type_lbl_, heading_type_label_, this);
    createRow(midLayout, 6, heading_len_lbl_, heading_len_label_, this);
    createRow(midLayout, 7, heading_sats_lbl_, heading_sats_label_, this);
    createRow(midLayout, 8, sats_lbl_, sats_label_, this);
    createRow(midLayout, 9, diff_lbl_, diff_age_label_, this);

    createRow(rightLayout, 0, gdop_lbl_, gdop_label_, this);
    createRow(rightLayout, 1, pdop_lbl_, pdop_label_, this);
    createRow(rightLayout, 2, hdop_lbl_, hdop_label_, this);
    createRow(rightLayout, 3, htdop_lbl_, htdop_label_, this);
    createRow(rightLayout, 4, tdop_lbl_, tdop_label_, this);
    createRow(rightLayout, 5, cutoff_lbl_, cutoff_label_, this);

    if (time_label_)
    {
        time_label_->setWordWrap(true);
        time_label_->setMinimumHeight(40);
        time_label_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    }

    leftLayout->setColumnStretch(1, 1);
    midLayout->setColumnStretch(1, 1);
    rightLayout->setColumnStretch(1, 1);

    colsLayout->addLayout(leftLayout, 1);
    colsLayout->addLayout(midLayout, 1);
    colsLayout->addLayout(rightLayout, 1);

    mainLayout->addLayout(colsLayout);
    mainLayout->addStretch();
    setEnglish(false);
}

void GnssPanel::updateRate(double hz)
{
    if (rate_label_)
    {
        rate_label_->setText(std::isfinite(hz)
            ? fixedDecimalWithUnit(hz, 1, 6, QStringLiteral("Hz"))
            : QStringLiteral("%1 Hz").arg(fixedTextField(QStringLiteral("--"), 6)));
    }
}

void GnssPanel::setEnglish(bool english)
{
    is_english_ = english;
    if (english)
    {
        status_lbl_->setText("Status:");
        time_lbl_->setText("Time:");
        lat_lbl_->setText("Lat:");
        lon_lbl_->setText("Lon:");
        alt_lbl_->setText("Alt:");
        sigma_lat_lbl_->setText("σ Lat:");
        sigma_lon_lbl_->setText("σ Lon:");
        sigma_alt_lbl_->setText("σ Alt:");
        undulation_lbl_->setText("Undul:");
        vel_n_lbl_->setText("Vel N:");
        vel_e_lbl_->setText("Vel E:");
        vel_ground_lbl_->setText("Vel Gnd:");
        heading_lbl_->setText("Heading:");
        pitch_lbl_->setText("Pitch:");
        heading_type_lbl_->setText("Hd Type:");
        heading_len_lbl_->setText("Base L:");
        heading_sats_lbl_->setText("Hd Sats:");
        sats_lbl_->setText("Sats:");
        diff_lbl_->setText("Diff:");
        gdop_lbl_->setText("GDOP:");
        pdop_lbl_->setText("PDOP:");
        hdop_lbl_->setText("HDOP:");
        htdop_lbl_->setText("HTDOP:");
        tdop_lbl_->setText("TDOP:");
        cutoff_lbl_->setText("Cutoff:");
    }
    else
    {
        status_lbl_->setText("状态:");
        time_lbl_->setText("时间:");
        lat_lbl_->setText("纬度:");
        lon_lbl_->setText("经度:");
        alt_lbl_->setText("高度:");
        sigma_lat_lbl_->setText("纬度σ:");
        sigma_lon_lbl_->setText("经度σ:");
        sigma_alt_lbl_->setText("高度σ:");
        undulation_lbl_->setText("异常高:");
        vel_n_lbl_->setText("北速:");
        vel_e_lbl_->setText("东速:");
        vel_ground_lbl_->setText("地速:");
        heading_lbl_->setText("航向:");
        pitch_lbl_->setText("俯仰:");
        heading_type_lbl_->setText("定向类型:");
        heading_len_lbl_->setText("基线长:");
        heading_sats_lbl_->setText("定向卫星:");
        sats_lbl_->setText("卫星:");
        diff_lbl_->setText("差分龄:");
        gdop_lbl_->setText("GDOP:");
        pdop_lbl_->setText("PDOP:");
        hdop_lbl_->setText("HDOP:");
        htdop_lbl_->setText("HTDOP:");
        tdop_lbl_->setText("TDOP:");
        cutoff_lbl_->setText("截止角:");
    }
}

void GnssPanel::updateData(const VaporView::GnssData& gnss_data, quint64 timestamp_us)
{
    if (gnss_data.valid)
    {
        status_label_->setText(QString::fromStdString(gnss_data.position_status));
        status_label_->setProperty("data-valid", true);
        status_label_->style()->unpolish(status_label_);
        status_label_->style()->polish(status_label_);

        const QString formattedText = timestamp_us > 0
            ? QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(timestamp_us / 1000ULL), QTimeZone::UTC)
                  .toString("yyyy-MM-dd HH:mm:ss.zzz 'UTC'")
            : QStringLiteral("---");
        const QString rawText = timestamp_us > 0 ? QString::number(timestamp_us) + "us" : QStringLiteral("---");
        time_label_->setText(QString("%1\n%2").arg(formattedText, rawText));

        lat_label_->setText(fixedDecimalWithUnit(gnss_data.latitude, 8, 12, QStringLiteral("°")));
        lon_label_->setText(fixedDecimalWithUnit(gnss_data.longitude, 8, 13, QStringLiteral("°")));
        alt_label_->setText(fixedDecimalWithUnit(gnss_data.altitude, 3, 10, QStringLiteral("m")));
        sigma_lat_label_->setText(fixedDecimalWithUnit(gnss_data.sigma_lat, 3, 8, QStringLiteral("m")));
        sigma_lon_label_->setText(fixedDecimalWithUnit(gnss_data.sigma_lon, 3, 8, QStringLiteral("m")));
        sigma_alt_label_->setText(fixedDecimalWithUnit(gnss_data.sigma_alt, 3, 8, QStringLiteral("m")));
        undulation_label_->setText(fixedDecimalWithUnit(gnss_data.undulation, 3, 9, QStringLiteral("m")));
        vel_n_label_->setText(fixedDecimalWithUnit(gnss_data.vel_north, 3, 9, QStringLiteral("m/s")));
        vel_e_label_->setText(fixedDecimalWithUnit(gnss_data.vel_east, 3, 9, QStringLiteral("m/s")));
        vel_ground_label_->setText(fixedDecimalWithUnit(gnss_data.vel_ground, 3, 9, QStringLiteral("m/s")));
        heading_label_->setText(fixedDecimalWithUnit(gnss_data.heading, 2, 7, QStringLiteral("°")));
        pitch_label_->setText(fixedDecimalWithUnit(gnss_data.heading_pitch, 2, 7, QStringLiteral("°")));
        heading_type_label_->setText(QString::fromStdString(gnss_data.heading_type));
        heading_len_label_->setText(fixedDecimalWithUnit(gnss_data.heading_length, 3, 9, QStringLiteral("m")));
        heading_sats_label_->setText(QStringLiteral("%1/%2")
            .arg(fixedTextField(QString::number(gnss_data.heading_solnsvs), 3),
                 fixedTextField(QString::number(gnss_data.heading_trackedsvs), 3)));
        sats_label_->setText(QStringLiteral("%1/%2")
            .arg(fixedTextField(QString::number(gnss_data.num_satellites_used), 3),
                 fixedTextField(QString::number(gnss_data.num_satellites_tracked), 3)));
        diff_age_label_->setText(fixedDecimalWithUnit(gnss_data.diff_age, 1, 6, QStringLiteral("s")));
        gdop_label_->setText(fixedDecimalWithUnit(gnss_data.gdop, 2, 6, QString()));
        pdop_label_->setText(fixedDecimalWithUnit(gnss_data.pdop, 2, 6, QString()));
        hdop_label_->setText(fixedDecimalWithUnit(gnss_data.hdop, 2, 6, QString()));
        htdop_label_->setText(fixedDecimalWithUnit(gnss_data.htdop, 2, 6, QString()));
        tdop_label_->setText(fixedDecimalWithUnit(gnss_data.tdop, 2, 6, QString()));
        cutoff_label_->setText(fixedDecimalWithUnit(gnss_data.elevation_cutoff, 1, 6, QStringLiteral("°")));
    }
    else
    {
        status_label_->setText(QString::fromStdString(gnss_data.error_message));
        status_label_->setProperty("data-valid", false);
        status_label_->style()->unpolish(status_label_);
        status_label_->style()->polish(status_label_);
        time_label_->setText(QStringLiteral("---\n---"));
    }
}

ImuPanel::ImuPanel(QWidget *parent)
    : QWidget(parent)
    , rate_label_(nullptr)
    , acc_x_label_(nullptr)
    , acc_y_label_(nullptr)
    , acc_z_label_(nullptr)
    , gyr_x_label_(nullptr)
    , gyr_y_label_(nullptr)
    , gyr_z_label_(nullptr)
    , roll_label_(nullptr)
    , pitch_label_(nullptr)
    , yaw_label_(nullptr)
    , quat_w_label_(nullptr)
    , quat_x_label_(nullptr)
    , quat_y_label_(nullptr)
    , quat_z_label_(nullptr)
    , temp_label_(nullptr)
    , press_label_(nullptr)
    , source_label_(nullptr)
    , time_label_(nullptr)
    , pps_label_(nullptr)
    , source_lbl_(nullptr)
    , time_lbl_(nullptr)
    , pps_lbl_(nullptr)
    , accel_sep_(nullptr)
    , gyro_sep_(nullptr)
    , attitude_sep_(nullptr)
    , quat_sep_(nullptr)
    , env_sep_(nullptr)
    , temp_lbl_(nullptr)
    , press_lbl_(nullptr)
    , acc_x_lbl_(nullptr)
    , acc_y_lbl_(nullptr)
    , acc_z_lbl_(nullptr)
    , gyr_x_lbl_(nullptr)
    , gyr_y_lbl_(nullptr)
    , gyr_z_lbl_(nullptr)
    , roll_lbl_(nullptr)
    , pitch_lbl_(nullptr)
    , yaw_lbl_(nullptr)
    , quat_w_lbl_(nullptr)
    , quat_x_lbl_(nullptr)
    , quat_y_lbl_(nullptr)
    , quat_z_lbl_(nullptr)
    , is_english_(false)
{
    setupUi();
}

void ImuPanel::setupUi()
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(4);
    mainLayout->setContentsMargins(6, 2, 6, 6);

    rate_label_ = new VaporView::VisualTextLabel(this);
    rate_label_->setObjectName("rateLabel");
    rate_label_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    rate_label_->setFixedHeight(24);
    setFixedNumericLabelWidth(rate_label_, {QStringLiteral("-999.9 Hz"), QStringLiteral("999.9 Hz"), QStringLiteral("-- Hz")}, 4);
    mainLayout->addWidget(rate_label_, 0, Qt::AlignRight);

    auto *colsLayout = new QHBoxLayout();
    colsLayout->setSpacing(12);

    auto *leftLayout = new QGridLayout();
    leftLayout->setVerticalSpacing(4);
    leftLayout->setHorizontalSpacing(1);

    auto *rightLayout = new QGridLayout();
    rightLayout->setVerticalSpacing(4);
    rightLayout->setHorizontalSpacing(1);

    auto createRow = [](QGridLayout* grid, int row, QLabel*& lbl, QLabel*& valueLabel, QWidget* parent) {
        lbl = new QLabel(parent);
        lbl->setObjectName("fieldLabel");
        lbl->setMinimumHeight(22);
        valueLabel = new QLabel("---", parent);
        valueLabel->setObjectName("valueLabel");
        valueLabel->setMinimumHeight(22);
        grid->addWidget(lbl, row, 0);
        grid->addWidget(valueLabel, row, 1);
    };

    auto createSeparator = [](QGridLayout* grid, int row, QLabel*& sep, QWidget* parent) {
        sep = new QLabel(parent);
        sep->setObjectName("separatorLabel");
        sep->setMinimumHeight(26);
        grid->addWidget(sep, row, 0, 1, 2);
    };

    createRow(leftLayout, 0, source_lbl_, source_label_, this);
    createRow(leftLayout, 1, time_lbl_, time_label_, this);
    createRow(leftLayout, 2, pps_lbl_, pps_label_, this);
    createSeparator(leftLayout, 3, accel_sep_, this);
    createRow(leftLayout, 4, acc_x_lbl_, acc_x_label_, this);
    createRow(leftLayout, 5, acc_y_lbl_, acc_y_label_, this);
    createRow(leftLayout, 6, acc_z_lbl_, acc_z_label_, this);
    createSeparator(leftLayout, 7, gyro_sep_, this);
    createRow(leftLayout, 8, gyr_x_lbl_, gyr_x_label_, this);
    createRow(leftLayout, 9, gyr_y_lbl_, gyr_y_label_, this);
    createRow(leftLayout, 10, gyr_z_lbl_, gyr_z_label_, this);

    createSeparator(rightLayout, 0, attitude_sep_, this);
    createRow(rightLayout, 1, roll_lbl_, roll_label_, this);
    createRow(rightLayout, 2, pitch_lbl_, pitch_label_, this);
    createRow(rightLayout, 3, yaw_lbl_, yaw_label_, this);
    createSeparator(rightLayout, 4, quat_sep_, this);
    createRow(rightLayout, 5, quat_w_lbl_, quat_w_label_, this);
    createRow(rightLayout, 6, quat_x_lbl_, quat_x_label_, this);
    createRow(rightLayout, 7, quat_y_lbl_, quat_y_label_, this);
    createRow(rightLayout, 8, quat_z_lbl_, quat_z_label_, this);

    if (time_label_)
    {
        time_label_->setWordWrap(true);
        time_label_->setMinimumHeight(40);
        time_label_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    }

    leftLayout->setColumnStretch(1, 1);
    rightLayout->setColumnStretch(1, 1);

    colsLayout->addLayout(leftLayout, 1);
    colsLayout->addLayout(rightLayout, 1);

    mainLayout->addLayout(colsLayout);
    mainLayout->addStretch();
    setEnglish(false);
}

void ImuPanel::updateRate(double hz)
{
    if (rate_label_)
    {
        rate_label_->setText(std::isfinite(hz)
            ? fixedDecimalWithUnit(hz, 1, 6, QStringLiteral("Hz"))
            : QStringLiteral("%1 Hz").arg(fixedTextField(QStringLiteral("--"), 6)));
    }
}

void ImuPanel::setEnglish(bool english)
{
    is_english_ = english;
    if (english)
    {
        source_lbl_->setText("Source:");
        time_lbl_->setText("Time:");
        pps_lbl_->setText("PPS:");
        accel_sep_->setText("— Accel —");
        gyro_sep_->setText("— Gyro —");
        attitude_sep_->setText("— Attitude —");
        quat_sep_->setText("— Quaternion —");
        acc_x_lbl_->setText("X:");
        acc_y_lbl_->setText("Y:");
        acc_z_lbl_->setText("Z:");
        gyr_x_lbl_->setText("X:");
        gyr_y_lbl_->setText("Y:");
        gyr_z_lbl_->setText("Z:");
        roll_lbl_->setText("Roll:");
        pitch_lbl_->setText("Pitch:");
        yaw_lbl_->setText("Yaw:");
        quat_w_lbl_->setText("W:");
        quat_x_lbl_->setText("X:");
        quat_y_lbl_->setText("Y:");
        quat_z_lbl_->setText("Z:");
    }
    else
    {
        source_lbl_->setText("数据源:");
        time_lbl_->setText("时间:");
        pps_lbl_->setText("PPS有效:");
        accel_sep_->setText("— 加速度 —");
        gyro_sep_->setText("— 陀螺仪 —");
        attitude_sep_->setText("— 姿态 —");
        quat_sep_->setText("— 四元数 —");
        acc_x_lbl_->setText("X:");
        acc_y_lbl_->setText("Y:");
        acc_z_lbl_->setText("Z:");
        gyr_x_lbl_->setText("X:");
        gyr_y_lbl_->setText("Y:");
        gyr_z_lbl_->setText("Z:");
        roll_lbl_->setText("横滚:");
        pitch_lbl_->setText("俯仰:");
        yaw_lbl_->setText("航向:");
        quat_w_lbl_->setText("W:");
        quat_x_lbl_->setText("X:");
        quat_y_lbl_->setText("Y:");
        quat_z_lbl_->setText("Z:");
    }
}

void ImuPanel::updateData(const VaporView::ImuData& imu_data, quint64 gnss_timestamp_us)
{
    if (imu_data.valid)
    {
        source_label_->setText(imuFrameTypeName(imu_data.frame_type));
        source_label_->setProperty("data-valid", true);
        source_label_->style()->unpolish(source_label_);
        source_label_->style()->polish(source_label_);
        quint64 imuTimestampUs = 0;
        if (imu_data.from_hi83 && imu_data.system_time_us > 0)
        {
            imuTimestampUs = static_cast<quint64>(imu_data.system_time_us);
        }
        else if (imu_data.system_time_ms > 0)
        {
            imuTimestampUs = static_cast<quint64>(imu_data.system_time_ms) * 1000ULL;
        }

        bool ppsValid = false;
        quint64 deltaUs = 0;
        if (imuTimestampUs > 0 && gnss_timestamp_us > 0)
        {
            deltaUs = (imuTimestampUs > gnss_timestamp_us) ? (imuTimestampUs - gnss_timestamp_us)
                                                           : (gnss_timestamp_us - imuTimestampUs);
            ppsValid = deltaUs <= kImuPpsSyncWindowUs;
        }

        const QString formattedText = (imuTimestampUs > 0 && ppsValid)
            ? QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(imuTimestampUs / 1000ULL), QTimeZone::UTC)
                  .toString("yyyy-MM-dd HH:mm:ss.zzz 'UTC'")
            : QStringLiteral("---");
        QString rawText = QStringLiteral("---");
        if (imu_data.from_hi83 && imu_data.system_time_us > 0)
        {
            rawText = QString::number(imu_data.system_time_us) + "us";
        }
        else if (imu_data.system_time_ms > 0)
        {
            rawText = QString::number(imu_data.system_time_ms) + "ms";
        }
        time_label_->setText(QString("%1\n%2").arg(formattedText, rawText));

        if (gnss_timestamp_us == 0 || imuTimestampUs == 0)
        {
            pps_label_->setText(is_english_ ? "Unknown" : "未知");
        }
        else if (ppsValid)
        {
            const QString deltaText = fixedTextField(QString::number(deltaUs / 1000ULL), 6);
            pps_label_->setText(is_english_
                ? QString("Valid (Δ%1 ms)").arg(deltaText)
                : QString("有效 (差值%1 ms)").arg(deltaText));
        }
        else
        {
            const QString deltaText = fixedTextField(QString::number(deltaUs / 1000ULL), 6);
            pps_label_->setText(is_english_
                ? QString("Invalid (Δ%1 ms)").arg(deltaText)
                : QString("无效 (差值%1 ms)").arg(deltaText));
        }

        acc_x_label_->setText(fixedDecimalWithUnit(imu_data.acceleration[0], 3, 8, QString()));
        acc_y_label_->setText(fixedDecimalWithUnit(imu_data.acceleration[1], 3, 8, QString()));
        acc_z_label_->setText(fixedDecimalWithUnit(imu_data.acceleration[2], 3, 8, QString()));

        gyr_x_label_->setText(fixedDecimalWithUnit(imu_data.gyroscope[0], 3, 8, QString()));
        gyr_y_label_->setText(fixedDecimalWithUnit(imu_data.gyroscope[1], 3, 8, QString()));
        gyr_z_label_->setText(fixedDecimalWithUnit(imu_data.gyroscope[2], 3, 8, QString()));

        roll_label_->setText(fixedDecimalWithUnit(imu_data.rpy[0], 2, 7, QStringLiteral("°")));
        pitch_label_->setText(fixedDecimalWithUnit(imu_data.rpy[1], 2, 7, QStringLiteral("°")));
        yaw_label_->setText(fixedDecimalWithUnit(imu_data.rpy[2], 2, 7, QStringLiteral("°")));

        quat_w_label_->setText(fixedDecimalWithUnit(imu_data.quaternion[0], 4, 8, QString()));
        quat_x_label_->setText(fixedDecimalWithUnit(imu_data.quaternion[1], 4, 8, QString()));
        quat_y_label_->setText(fixedDecimalWithUnit(imu_data.quaternion[2], 4, 8, QString()));
        quat_z_label_->setText(fixedDecimalWithUnit(imu_data.quaternion[3], 4, 8, QString()));
    }
    else
    {
        source_label_->setText(QString::fromStdString(imu_data.error_message));
        source_label_->setProperty("data-valid", false);
        source_label_->style()->unpolish(source_label_);
        source_label_->style()->polish(source_label_);
        time_label_->setText(QStringLiteral("---\n---"));
        pps_label_->setText(is_english_ ? "Unknown" : "未知");
    }
}

PtbPanel::PtbPanel(QWidget *parent)
    : QWidget(parent)
    , rate_label_(nullptr)
    , pressure_label_(nullptr)
    , status_label_(nullptr)
    , pressure_lbl_(nullptr)
    , is_english_(false)
{
    setupUi();
}

void PtbPanel::setupUi()
{
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    auto *layout = new QVBoxLayout(this);
    layout->setSpacing(2);
    layout->setContentsMargins(6, 1, 6, 4);

    rate_label_ = new VaporView::VisualTextLabel(this);
    rate_label_->setObjectName("rateLabel");
    rate_label_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    rate_label_->setMinimumHeight(20);
    setFixedNumericLabelWidth(rate_label_, {QStringLiteral("-999.9 Hz"), QStringLiteral("999.9 Hz"), QStringLiteral("-- Hz")}, 4);

    auto *pressLayout = new QHBoxLayout();
    pressLayout->setSpacing(1);
    pressure_lbl_ = new QLabel(this);
    pressure_lbl_->setObjectName("fieldLabel");
    pressure_lbl_->setMinimumHeight(20);
    setFixedTextLabelWidth(pressure_lbl_, environmentFieldLabelWidthCandidates(), 6);
    pressLayout->addWidget(pressure_lbl_);
    pressure_label_ = new QLabel("--- hPa", this);
    pressure_label_->setObjectName("highlightedValue");
    pressure_label_->setMinimumHeight(20);
    setFixedNumericLabelWidth(pressure_label_, {QStringLiteral("-9999.99 hPa"), QStringLiteral("9999.99 hPa"), QStringLiteral("--- hPa")}, 18);
    pressLayout->addWidget(pressure_label_);
    pressLayout->addStretch();
    pressLayout->addWidget(rate_label_);
    layout->addLayout(pressLayout);

    setEnglish(false);
}

void PtbPanel::updateRate(double hz)
{
    if (rate_label_)
    {
        rate_label_->setText((hz > 0.0 && std::isfinite(hz))
            ? fixedDecimalWithUnit(hz, 1, 6, QStringLiteral("Hz"))
            : QStringLiteral("%1 Hz").arg(fixedTextField(QStringLiteral("--"), 6)));
    }
}

void PtbPanel::setEnglish(bool english)
{
    is_english_ = english;
    if (english)
    {
        pressure_lbl_->setText("Pressure:");
    }
    else
    {
        pressure_lbl_->setText("气压:");
    }
    refreshFixedTextLabelWidth(pressure_lbl_);
}

void PtbPanel::updateData(const VaporView::PtbData& ptb_data)
{
    if (ptb_data.valid)
    {
        pressure_label_->setText(fixedDecimalWithUnit(ptb_data.pressure_hpa, 2, 8, QStringLiteral("hPa")));
        pressure_label_->setProperty("data-valid", true);
        polishNumericLabel(pressure_label_);
    }
    else
    {
        pressure_label_->setText(fixedDecimalWithUnit(std::numeric_limits<double>::quiet_NaN(), 2, 8, QStringLiteral("hPa")));
        pressure_label_->setProperty("data-valid", false);
        polishNumericLabel(pressure_label_);
    }
}

HmpPanel::HmpPanel(QWidget *parent)
    : QWidget(parent)
    , rate_label_(nullptr)
    , humidity_label_(nullptr)
    , temperature_label_(nullptr)
    , status_label_(nullptr)
    , temp_lbl_(nullptr)
    , humidity_lbl_(nullptr)
    , is_english_(false)
{
    setupUi();
}

void HmpPanel::setupUi()
{
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    auto *layout = new QVBoxLayout(this);
    layout->setSpacing(2);
    layout->setContentsMargins(6, 1, 6, 4);

    rate_label_ = new VaporView::VisualTextLabel(this);
    rate_label_->setObjectName("rateLabel");
    rate_label_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    rate_label_->setMinimumHeight(20);
    setFixedNumericLabelWidth(rate_label_, {QStringLiteral("-999.9 Hz"), QStringLiteral("999.9 Hz"), QStringLiteral("-- Hz")}, 4);

    auto *tempLayout = new QHBoxLayout();
    tempLayout->setSpacing(1);
    temp_lbl_ = new QLabel(this);
    temp_lbl_->setObjectName("fieldLabel");
    temp_lbl_->setMinimumHeight(20);
    setFixedTextLabelWidth(temp_lbl_, environmentFieldLabelWidthCandidates(), 6);
    tempLayout->addWidget(temp_lbl_);
    temperature_label_ = new QLabel("--- °C", this);
    temperature_label_->setObjectName("highlightedValue");
    temperature_label_->setMinimumHeight(20);
    setFixedNumericLabelWidth(temperature_label_, {QStringLiteral("-9999.9 °C"), QStringLiteral("9999.9 °C"), QStringLiteral("--- °C")}, 18);
    tempLayout->addWidget(temperature_label_);
    tempLayout->addStretch();
    tempLayout->addWidget(rate_label_);
    layout->addLayout(tempLayout);

    auto *humidLayout = new QHBoxLayout();
    humidLayout->setSpacing(1);
    humidity_lbl_ = new QLabel(this);
    humidity_lbl_->setObjectName("fieldLabel");
    humidity_lbl_->setMinimumHeight(20);
    setFixedTextLabelWidth(humidity_lbl_, environmentFieldLabelWidthCandidates(), 6);
    humidLayout->addWidget(humidity_lbl_);
    humidity_label_ = new QLabel("--- %RH", this);
    humidity_label_->setObjectName("highlightedValue");
    humidity_label_->setMinimumHeight(20);
    setFixedNumericLabelWidth(humidity_label_, {QStringLiteral("-9999.9 %RH"), QStringLiteral("9999.9 %RH"), QStringLiteral("100.0 %RH"), QStringLiteral("--- %RH")}, 28);
    humidLayout->addWidget(humidity_label_);
    humidLayout->addStretch();
    layout->addLayout(humidLayout);

    setEnglish(false);
}

void HmpPanel::updateRate(double hz)
{
    if (rate_label_)
    {
        rate_label_->setText((hz > 0.0 && std::isfinite(hz))
            ? fixedDecimalWithUnit(hz, 1, 6, QStringLiteral("Hz"))
            : QStringLiteral("%1 Hz").arg(fixedTextField(QStringLiteral("--"), 6)));
    }
}

void HmpPanel::setEnglish(bool english)
{
    is_english_ = english;
    if (english)
    {
        temp_lbl_->setText("Temp:");
        humidity_lbl_->setText("Humidity:");
    }
    else
    {
        temp_lbl_->setText("温度:");
        humidity_lbl_->setText("湿度:");
    }
    refreshFixedTextLabelWidth(temp_lbl_);
    refreshFixedTextLabelWidth(humidity_lbl_);
}

void HmpPanel::updateData(const VaporView::HmpData& hmp_data)
{
    if (hmp_data.valid)
    {
        temperature_label_->setText(fixedDecimalWithUnit(hmp_data.temperature, 1, 8, QStringLiteral("°C")));
        humidity_label_->setText(fixedDecimalWithUnit(hmp_data.humidity, 1, 8, QStringLiteral("%RH")));
        temperature_label_->setProperty("data-valid", true);
        polishNumericLabel(temperature_label_);
        humidity_label_->setProperty("data-valid", true);
        polishNumericLabel(humidity_label_);
    }
    else
    {
        temperature_label_->setText(fixedDecimalWithUnit(std::numeric_limits<double>::quiet_NaN(), 1, 8, QStringLiteral("°C")));
        humidity_label_->setText(fixedDecimalWithUnit(std::numeric_limits<double>::quiet_NaN(), 1, 8, QStringLiteral("%RH")));
        temperature_label_->setProperty("data-valid", false);
        polishNumericLabel(temperature_label_);
        humidity_label_->setProperty("data-valid", false);
        polishNumericLabel(humidity_label_);
    }
}

LidarPanel::LidarPanel(QWidget *parent)
    : QWidget(parent)
    , rate_label_(nullptr)
    , distance_label_(nullptr)
    , strength_label_(nullptr)
    , status_label_(nullptr)
    , distance_lbl_(nullptr)
    , strength_lbl_(nullptr)
    , is_english_(false)
{
    setupUi();
}

void LidarPanel::setupUi()
{
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    auto *layout = new QVBoxLayout(this);
    layout->setSpacing(2);
    layout->setContentsMargins(6, 1, 6, 4);

    rate_label_ = new VaporView::VisualTextLabel(QStringLiteral("0.0 Hz"), this);
    rate_label_->setObjectName("rateLabel");
    rate_label_->setMinimumHeight(20);
    rate_label_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    setFixedNumericLabelWidth(rate_label_, {QStringLiteral("-999.9 Hz"), QStringLiteral("999.9 Hz"), QStringLiteral("-- Hz")}, 4);

    auto *distanceLayout = new QHBoxLayout();
    distanceLayout->setSpacing(1);
    distance_lbl_ = new QLabel(this);
    distance_lbl_->setObjectName("fieldLabel");
    distance_lbl_->setMinimumHeight(20);
    setFixedTextLabelWidth(distance_lbl_, environmentFieldLabelWidthCandidates(), 6);
    distanceLayout->addWidget(distance_lbl_);
    distance_label_ = new QLabel("--- m", this);
    distance_label_->setObjectName("highlightedValue");
    distance_label_->setMinimumHeight(20);
    setFixedNumericLabelWidth(distance_label_, {QStringLiteral("-9999.99 m"), QStringLiteral("9999.99 m"), QStringLiteral("--- m")}, 18);
    distanceLayout->addWidget(distance_label_);
    distanceLayout->addStretch();
    distanceLayout->addWidget(rate_label_);
    layout->addLayout(distanceLayout);

    auto *strengthLayout = new QHBoxLayout();
    strengthLayout->setSpacing(1);
    strength_lbl_ = new QLabel(this);
    strength_lbl_->setObjectName("fieldLabel");
    strength_lbl_->setMinimumHeight(20);
    setFixedTextLabelWidth(strength_lbl_, environmentFieldLabelWidthCandidates(), 6);
    strengthLayout->addWidget(strength_lbl_);
    strength_label_ = new QLabel("---", this);
    strength_label_->setObjectName("highlightedValue");
    strength_label_->setMinimumHeight(20);
    setFixedNumericLabelWidth(strength_label_, {QStringLiteral("-999999"), QStringLiteral("999999"), QStringLiteral("---")}, 18);
    strengthLayout->addWidget(strength_label_);
    strengthLayout->addStretch();
    layout->addLayout(strengthLayout);

    setEnglish(false);
}

void LidarPanel::updateRate(double hz)
{
    if (rate_label_)
    {
        rate_label_->setText((hz > 0.0 && std::isfinite(hz))
            ? fixedDecimalWithUnit(hz, 1, 6, QStringLiteral("Hz"))
            : QStringLiteral("%1 Hz").arg(fixedTextField(QStringLiteral("--"), 6)));
    }
}

void LidarPanel::setEnglish(bool english)
{
    is_english_ = english;
    if (english)
    {
        distance_lbl_->setText("Distance:");
        strength_lbl_->setText("Strength:");
    }
    else
    {
        distance_lbl_->setText("距离:");
        strength_lbl_->setText("强度:");
    }
    refreshFixedTextLabelWidth(distance_lbl_);
    refreshFixedTextLabelWidth(strength_lbl_);
}

void LidarPanel::updateData(const VaporView::LidarData& lidar_data)
{
    if (lidar_data.valid)
    {
        distance_label_->setText(fixedDecimalWithUnit(lidar_data.distance_m, 2, 8, QStringLiteral("m")));
        strength_label_->setText(fixedTextField(QString::number(lidar_data.signal_strength), 8));
        distance_label_->setProperty("data-valid", true);
        strength_label_->setProperty("data-valid", true);
        polishNumericLabel(distance_label_);
        polishNumericLabel(strength_label_);
    }
    else
    {
        distance_label_->setText(fixedDecimalWithUnit(std::numeric_limits<double>::quiet_NaN(), 2, 8, QStringLiteral("m")));
        strength_label_->setText(fixedTextField(QStringLiteral("---"), 8));
        distance_label_->setProperty("data-valid", false);
        strength_label_->setProperty("data-valid", false);
        polishNumericLabel(distance_label_);
        polishNumericLabel(strength_label_);
    }
}

class TemperatureTrendPlotWidget : public QWidget
{
public:
    explicit TemperatureTrendPlotWidget(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setObjectName(QStringLiteral("temperatureTrendPlot"));
        setFont(numericFontFrom(font()));
        applyPlotSizing();
        updateSampleProperties();
    }

    void setCompactMode(bool compact)
    {
        if (compact_mode_ == compact)
        {
            return;
        }
        compact_mode_ = compact;
        applyPlotSizing();
        updateGeometry();
        update();
    }

    void setEnglish(bool english)
    {
        is_english_ = english;
        update();
    }

    void setChannelIndex(int channelIndex)
    {
        channel_index_ = std::clamp(channelIndex, 0, 1);
        update();
    }

    void setSamples(const QVector<double>& samples)
    {
        samples_ = samples;
        updateSampleProperties();
        update();
    }

    void setTargetTemperature(double celsius)
    {
        target_temperature_c_ = std::isfinite(celsius)
            ? celsius
            : std::numeric_limits<double>::quiet_NaN();
        updateSampleProperties();
        update();
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        QWidget::paintEvent(event);

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const QPalette palette = this->palette();
        QColor background = palette.color(QPalette::Base);
        if (!background.isValid() || background.alpha() == 0)
        {
            background = palette.color(QPalette::Window);
        }
        const bool dark = background.lightness() < 128;
        const QColor grid = VaporView::appThemeColor(VaporView::AppThemeColor::PlotGrid, dark);
        const QColor border = VaporView::appThemeColor(VaporView::AppThemeColor::PlotBorder, dark);
        const QColor text = VaporView::appThemeColor(VaporView::AppThemeColor::PlotText, dark);
        const QColor muted = VaporView::appThemeColor(VaporView::AppThemeColor::PlotMutedText, dark);
        const QColor line = VaporView::appThemeColor(VaporView::AppThemeColor::PlotSeriesTemperature, dark);

        painter.fillRect(rect(), background);
        QFont axisFont = font();
        axisFont.setPointSize(std::max(8, axisFont.pointSize() - 2));
        const QFontMetrics fm = painter.fontMetrics();
        const QFontMetrics axisFm(axisFont);
        painter.setPen(text);
        constexpr int kYAxisTicks = 6;
        constexpr int kXAxisTicks = 4;
        const qreal leftAxisWidth = axisFm.horizontalAdvance(QStringLiteral("999")) + 6.0;
        constexpr qreal kBottomAxisHeight = 18.0;
        const QRectF plotRect = rect().adjusted(leftAxisWidth, 4.0, -4.0, -kBottomAxisHeight);
        auto drawGridAndAxes = [&](double minValue, double maxValue, int sampleCount) {
            painter.setPen(QPen(grid, 1));
            for (int i = 0; i <= kXAxisTicks; ++i)
            {
                const qreal x = plotRect.left() + plotRect.width() * i / static_cast<qreal>(kXAxisTicks);
                painter.drawLine(QPointF(x, plotRect.top()), QPointF(x, plotRect.bottom()));
            }
            for (int i = 0; i <= kYAxisTicks; ++i)
            {
                const qreal y = plotRect.top() + plotRect.height() * i / static_cast<qreal>(kYAxisTicks);
                painter.drawLine(QPointF(plotRect.left(), y), QPointF(plotRect.right(), y));
            }
            painter.setPen(QPen(border, 1));
            painter.drawRect(plotRect);

            painter.setFont(axisFont);
            painter.setPen(muted);
            for (int i = 0; i <= kYAxisTicks; ++i)
            {
                const double value = maxValue - (maxValue - minValue) * i / static_cast<double>(kYAxisTicks);
                const qreal y = plotRect.top() + plotRect.height() * i / static_cast<qreal>(kYAxisTicks);
                const QString label = axisTickLabel(value);
                const QRectF labelRect(0.0,
                                       y - axisFm.height() / 2.0,
                                       plotRect.left() - 4.0,
                                       axisFm.height());
                painter.drawText(labelRect, Qt::AlignRight | Qt::AlignVCenter, label);
            }
            for (int i = 0; i <= kXAxisTicks; ++i)
            {
                const int sampleIndex = sampleCount <= 1
                    ? 0
                    : qRound((sampleCount - 1) * i / static_cast<double>(kXAxisTicks));
                const qreal x = plotRect.left() + plotRect.width() * i / static_cast<qreal>(kXAxisTicks);
                const QString label = QString::number(sampleIndex);
                const qreal labelWidth = std::max<qreal>(36.0, axisFm.horizontalAdvance(label) + 8.0);
                const qreal labelLeft = std::clamp(x - labelWidth / 2.0,
                                                   plotRect.left(),
                                                   std::max(plotRect.left(), width() - labelWidth - 2.0));
                const QRectF labelRect(labelLeft,
                                       plotRect.bottom() + 2.0,
                                       labelWidth,
                                       axisFm.height());
                painter.drawText(labelRect, Qt::AlignHCenter | Qt::AlignTop, label);
            }
            painter.setFont(font());
        };

        QVector<double> finiteSamples;
        finiteSamples.reserve(samples_.size());
        for (double value : samples_)
        {
            if (std::isfinite(value))
            {
                finiteSamples.append(value);
            }
        }

        if (finiteSamples.isEmpty() || plotRect.width() <= 1.0 || plotRect.height() <= 1.0)
        {
            const auto [minValue, maxValue] = temperatureAxisRange(QVector<double>(), target_temperature_c_);
            drawGridAndAxes(minValue, maxValue, 0);
            painter.setPen(muted);
            QRectF visiblePlotRect = plotRect.intersected(QRectF(visibleRegion().boundingRect()));
            if (!visiblePlotRect.isValid() || visiblePlotRect.width() <= 1.0 || visiblePlotRect.height() <= 1.0)
            {
                visiblePlotRect = plotRect;
            }
            const QRectF textRect = visiblePlotRect.adjusted(4, 0, -4, 0);
            QString emptyText = compact_mode_
                ? (is_english_ ? QStringLiteral("No data") : QStringLiteral("暂无数据"))
                : (is_english_ ? QStringLiteral("No measured data") : QStringLiteral("暂无实际温度数据"));
            emptyText = fm.elidedText(emptyText, Qt::ElideRight,
                                      std::max(0, static_cast<int>(textRect.width())));
            if (!emptyText.isEmpty())
            {
                painter.drawText(textRect, Qt::AlignCenter, emptyText);
            }
            return;
        }

        const auto [minValue, maxValue] = temperatureAxisRange(finiteSamples, target_temperature_c_);
        drawGridAndAxes(minValue, maxValue, finiteSamples.size());

        QPolygonF polyline;
        polyline.reserve(finiteSamples.size());
        const int count = finiteSamples.size();
        for (int i = 0; i < count; ++i)
        {
            const double ratio = count == 1 ? 0.0 : static_cast<double>(i) / static_cast<double>(count - 1);
            const double normalized = (finiteSamples.at(i) - minValue) / std::max(1e-6, maxValue - minValue);
            polyline.append(QPointF(plotRect.left() + ratio * plotRect.width(),
                                    plotRect.bottom() - normalized * plotRect.height()));
        }

        painter.setPen(QPen(line, 1.6));
        painter.drawPolyline(polyline);
        painter.setBrush(line);
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(polyline.last(), 3.0, 3.0);
    }

private:
    static QString axisTickLabel(double value)
    {
        return std::abs(value - std::round(value)) < 0.05
            ? QString::number(qRound(value))
            : QString::number(value, 'f', 1);
    }

    static std::pair<double, double> temperatureAxisRange(const QVector<double>& finiteSamples, double targetTemperature)
    {
        double minValue = std::isfinite(targetTemperature) ? targetTemperature - 2.0 : 20.0;
        double maxValue = std::isfinite(targetTemperature) ? targetTemperature + 2.0 : 25.0;
        if (finiteSamples.isEmpty())
        {
            return {minValue, maxValue};
        }

        auto [minIt, maxIt] = std::minmax_element(finiteSamples.cbegin(), finiteSamples.cend());
        if (*minIt < minValue)
        {
            minValue = std::floor(*minIt) - 1.0;
        }
        if (*maxIt > maxValue)
        {
            maxValue = std::ceil(*maxIt) + 1.0;
        }
        return {minValue, maxValue};
    }

    void updateSampleProperties()
    {
        QVector<double> finiteSamples;
        finiteSamples.reserve(samples_.size());
        for (double value : samples_)
        {
            if (std::isfinite(value))
            {
                finiteSamples.append(value);
            }
        }

        const auto [minValue, maxValue] = temperatureAxisRange(finiteSamples, target_temperature_c_);
        setProperty("sampleCount", finiteSamples.size());
        setProperty("yAxisMinC", minValue);
        setProperty("yAxisMaxC", maxValue);
        setProperty("axisLabelsVisible", true);
        setProperty("yAxisTickCount", 7);
        setProperty("xAxisTickCount", 5);
    }

    void applyPlotSizing()
    {
        if (compact_mode_)
        {
            setMinimumSize(160, 144);
            setMaximumHeight(QWIDGETSIZE_MAX);
            setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
            return;
        }

        setMinimumSize(kTemperatureControllerPlotWidth, kTemperatureControllerPlotMinHeight);
        setMaximumHeight(QWIDGETSIZE_MAX);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    }

    QVector<double> samples_;
    double target_temperature_c_ = std::numeric_limits<double>::quiet_NaN();
    int channel_index_ = 0;
    bool compact_mode_ = false;
    bool is_english_ = false;
};

QString temperatureOverviewNumberText(double value)
{
    if (!std::isfinite(value))
    {
        return QStringLiteral("---");
    }

    return QLocale::c().toString(value, 'f', 5);
}

QString temperatureOverviewReservedNumberText()
{
    return QStringLiteral("999.99999");
}

int temperatureOverviewValueFontSizePx(const QLabel *label, const QString& value)
{
    constexpr int kFallbackWidth = 99;
    constexpr int kHorizontalPadding = 10;
    constexpr int kMinimumFontSize = 14;
    constexpr int kMaximumFontSize = 19;
    const int width = label && label->width() > 0 ? label->width() : kFallbackWidth;
    const int availableWidth = std::max(32, width - kHorizontalPadding);
    QFont valueFont = label ? label->font() : QFont();
    valueFont.setWeight(QFont::Bold);
    const QString reservedText = temperatureOverviewReservedNumberText();
    for (int size = kMaximumFontSize; size >= kMinimumFontSize; --size)
    {
        valueFont.setPixelSize(size);
        const QFontMetrics metrics(valueFont);
        const int requiredWidth = std::max(metrics.horizontalAdvance(value),
                                           metrics.horizontalAdvance(reservedText));
        if (requiredWidth <= availableWidth)
        {
            return size;
        }
    }
    return kMinimumFontSize;
}

void setTemperatureOverviewPillText(QLabel *label, const QString& title, const QString& value)
{
    if (!label)
    {
        return;
    }

    const int valueFontSize = temperatureOverviewValueFontSizePx(label, value);
    label->setProperty("reservedValueText", temperatureOverviewReservedNumberText());
    label->setProperty("valueFontSizePx", valueFontSize);
    QFont valueFont = label->font();
    valueFont.setWeight(QFont::Bold);
    valueFont.setPixelSize(valueFontSize);
    const int availableWidth = std::max(32, label->width() - 10);
    label->setProperty("reservedValueFits",
                       QFontMetrics(valueFont).horizontalAdvance(temperatureOverviewReservedNumberText()) <= availableWidth);
    label->setTextFormat(Qt::RichText);
    label->setText(QStringLiteral(
        "<div align=\"center\" style=\"line-height: 14px; white-space: nowrap;\">"
        "<span style=\"font-size: 12px; font-weight: 700;\">%1</span><br/>"
        "<span style=\"font-size: %2px; font-weight: 700;\">%3</span>"
        "</div>")
        .arg(title.toHtmlEscaped(), QString::number(valueFontSize), value.toHtmlEscaped()));
    label->style()->unpolish(label);
    label->style()->polish(label);
}

class TemperatureOverviewSwitchButton final : public QPushButton
{
public:
    explicit TemperatureOverviewSwitchButton(QWidget *parent = nullptr)
        : QPushButton(parent)
    {
        setCheckable(true);
        setObjectName(QStringLiteral("temperatureOverviewOutputSwitch"));
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::NoFocus);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        thumb_position_ = isChecked() ? 1.0 : 0.0;
        thumb_animation_ = new QVariantAnimation(this);
        thumb_animation_->setDuration(180);
        thumb_animation_->setEasingCurve(QEasingCurve::OutCubic);
        connect(thumb_animation_, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
            const qreal progress = std::clamp(value.toReal(), 0.0, 1.0);
            constexpr qreal kPi = 3.14159265358979323846;
            thumb_position_ = thumb_start_position_ + (thumb_target_position_ - thumb_start_position_) * progress;
            thumb_jelly_ = std::sin(progress * kPi);
            update();
        });
        connect(thumb_animation_, &QVariantAnimation::finished, this, [this]() {
            thumb_position_ = thumb_target_position_;
            thumb_jelly_ = 0.0;
            update();
        });
        refreshText();
    }

    void setEnglish(bool english)
    {
        is_english_ = english;
        refreshText();
    }

    bool switchChecked() const
    {
        return isChecked();
    }

    void setSwitchChecked(bool checked, bool animated)
    {
        const QSignalBlocker blocker(this);
        setChecked(checked);
        refreshText();

        const qreal target = checked ? 1.0 : 0.0;
        if (animated)
        {
            animateThumbTo(target);
        }
        else
        {
            if (thumb_animation_)
            {
                thumb_animation_->stop();
            }
            thumb_position_ = target;
            thumb_target_position_ = target;
            thumb_start_position_ = target;
            thumb_jelly_ = 0.0;
            update();
        }
    }

protected:
    void nextCheckState() override
    {
        // The overview switch is controlled by the command confirmation flow.
        // Do not let QAbstractButton pre-toggle before the confirmation dialog.
    }

    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event);

        const bool dark = VaporView::isDarkThemeEnabled();
        const bool checked = isChecked();
        const bool enabled = isEnabled();
        const QColor stateFill = checked
            ? appThemeColor(AppThemeColor::HomeDeviceSuccess, dark)
            : appThemeColor(AppThemeColor::HomeDeviceDanger, dark);
        const QColor border = appThemeColor(AppThemeColor::Border, dark);
        const QColor fill = appThemeColor(AppThemeColor::Surface, dark);
        const QColor switchFill = enabled
            ? stateFill
            : appThemeColor(AppThemeColor::Surface, dark);
        const QColor text = enabled
            ? appThemeColor(AppThemeColor::Primary, dark)
            : appThemeColor(AppThemeColor::TextMuted, dark);
        const QColor selectedFill = enabled
            ? appThemeColor(AppThemeColor::Surface, dark)
            : appThemeColor(AppThemeColor::SurfaceAlt, dark);
        const QColor selectedText = enabled
            ? stateFill
            : text;
        const QColor inactiveText = enabled
            ? appThemeColor(AppThemeColor::White, dark)
            : appThemeColor(AppThemeColor::TextMuted, dark);

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);

        const QRectF pillRect = rect().adjusted(0.5, 0.5, -0.5, -0.5);
        constexpr qreal kControlRadius = 10.0;
        painter.setPen(QPen(border, 1.0));
        painter.setBrush(fill);
        painter.drawRoundedRect(pillRect, kControlRadius, kControlRadius);

        const qreal gap = 3.0;
        const QRectF trackRect = pillRect.adjusted(gap, gap, -gap, -gap);
        QFont segmentFont = font();
        segmentFont.setWeight(QFont::DemiBold);

        const QRectF switchRect = trackRect;
        constexpr qreal kInnerGap = 2.0;
        const QRectF switchCapsuleRect = switchRect.adjusted(0.5, 0.5, -0.5, -0.5);
        const QRectF switchContentRect = switchCapsuleRect.adjusted(kInnerGap, kInnerGap, -kInnerGap, -kInnerGap);
        const qreal segmentWidth = switchContentRect.width() / 2.0;
        const qreal selectedLeft = switchContentRect.left() + segmentWidth * thumb_position_;
        QRectF selectedRect(selectedLeft, switchContentRect.top(), segmentWidth, switchContentRect.height());
        if (enabled && thumb_jelly_ > 0.001)
        {
            const qreal stretch = std::min<qreal>(segmentWidth * 0.22, 10.0) * thumb_jelly_;
            if (thumb_direction_ >= 0)
            {
                selectedRect.adjust(-stretch * 0.35, 0.0, stretch * 0.65, 0.0);
            }
            else
            {
                selectedRect.adjust(-stretch * 0.65, 0.0, stretch * 0.35, 0.0);
            }
            selectedRect.setLeft(std::max(selectedRect.left(), switchContentRect.left()));
            selectedRect.setRight(std::min(selectedRect.right(), switchContentRect.right()));
        }
        painter.setPen(Qt::NoPen);
        painter.setBrush(switchFill);
        painter.drawRoundedRect(switchCapsuleRect, kControlRadius - gap, kControlRadius - gap);
        painter.setPen(Qt::NoPen);
        painter.setBrush(selectedFill);
        painter.drawRoundedRect(selectedRect, kControlRadius - gap - kInnerGap, kControlRadius - gap - kInnerGap);

        const QRectF offRect(switchContentRect.left(), switchContentRect.top(), segmentWidth, switchContentRect.height());
        const QRectF onRect(switchContentRect.left() + segmentWidth, switchContentRect.top(), segmentWidth, switchContentRect.height());
        const bool offSelected = thumb_position_ < 0.5;
        painter.setFont(segmentFont);
        painter.setPen(offSelected ? selectedText : inactiveText);
        painter.drawText(offRect, Qt::AlignCenter, offText());
        painter.setPen(offSelected ? inactiveText : selectedText);
        painter.drawText(onRect, Qt::AlignCenter, onText());

        if (hasFocus())
        {
            painter.setPen(QPen(appThemeColor(AppThemeColor::Focus, dark), 1.0));
            painter.setBrush(Qt::NoBrush);
            painter.drawRoundedRect(pillRect.adjusted(2.0, 2.0, -2.0, -2.0),
                                    kControlRadius - 2.0,
                                    kControlRadius - 2.0);
        }
    }

private:
    QString offText() const
    {
        return is_english_ ? QStringLiteral("Off") : QStringLiteral("关闭");
    }

    QString onText() const
    {
        return is_english_ ? QStringLiteral("On") : QStringLiteral("开启");
    }

    QString outputLabelText() const
    {
        return is_english_ ? QStringLiteral("Output Enable") : QStringLiteral("输出使能");
    }

    void refreshText()
    {
        const QString text = QStringLiteral("%1: %2")
            .arg(outputLabelText(), isChecked() ? onText() : offText());
        setText(text);
        setToolTip(text);
        setStatusTip(text);
        setAccessibleName(text);
    }

    void animateThumbTo(qreal target)
    {
        if (!thumb_animation_)
        {
            thumb_position_ = target;
            thumb_target_position_ = target;
            thumb_start_position_ = target;
            thumb_jelly_ = 0.0;
            update();
            return;
        }

        if (qFuzzyCompare(thumb_position_, target))
        {
            thumb_position_ = target;
            thumb_target_position_ = target;
            thumb_start_position_ = target;
            thumb_jelly_ = 0.0;
            update();
            return;
        }

        thumb_animation_->stop();
        thumb_start_position_ = thumb_position_;
        thumb_target_position_ = target;
        thumb_direction_ = thumb_target_position_ >= thumb_start_position_ ? 1 : -1;
        thumb_jelly_ = 0.0;
        thumb_animation_->setStartValue(0.0);
        thumb_animation_->setEndValue(1.0);
        thumb_animation_->start();
    }

    bool is_english_ = false;
    qreal thumb_position_ = 0.0;
    qreal thumb_start_position_ = 0.0;
    qreal thumb_target_position_ = 0.0;
    qreal thumb_jelly_ = 0.0;
    int thumb_direction_ = 1;
    QVariantAnimation *thumb_animation_ = nullptr;
};

class SingleLevelPopupComboBox final : public QComboBox
{
public:
    explicit SingleLevelPopupComboBox(QWidget *parent = nullptr)
        : QComboBox(parent)
        , popup_menu_(new SingleLevelPopupMenu(this))
    {
        popup_menu_->setObjectName(QStringLiteral("singleLevelComboPopupMenu"));
        popup_menu_->setCornerRadius(10);
        popup_menu_->setPanelPadding(12);
        setProperty("usesSingleLevelPopupMenu", true);
    }

    void showPopup() override
    {
        rebuildPopupRows();
        popup_menu_->setPanelContentWidth(width());
        popup_menu_->popupFrom(this);
    }

    void hidePopup() override
    {
        if (popup_menu_)
        {
            popup_menu_->hide();
        }
        QComboBox::hidePopup();
    }

private:
    bool itemEnabled(int index) const
    {
        if (!model())
        {
            return true;
        }
        const QModelIndex modelIndex = model()->index(index, modelColumn(), rootModelIndex());
        return !modelIndex.isValid() || (modelIndex.flags() & Qt::ItemIsEnabled);
    }

    void rebuildPopupRows()
    {
        if (!popup_menu_)
        {
            return;
        }

        popup_menu_->clear();
        popup_menu_->setPanelContentWidth(width());
        const QIcon checkIcon = createLucideIcon(QStringLiteral("check"),
                                                 appThemeColor(AppThemeColor::MenuCheckText,
                                                               VaporView::isDarkThemeEnabled()));
        for (int i = 0; i < count(); ++i)
        {
            auto *row = new SingleLevelPopupMenuRow(popup_menu_);
            row->setText(itemText(i));
            row->setChecked(i == currentIndex());
            row->setCheckIcon(checkIcon);
            row->setCheckIconSize(QSize(16, 16));
            row->setTextAlignment(SingleLevelPopupTextAlignment::Left);
            row->setHorizontalPadding(18, 14);
            row->setCheckSlotWidth(18);
            row->setRowSpacing(6);
            row->setRowHeight(40);
            row->setMinimumRowWidth(width());
            row->setEnabled(itemEnabled(i));
            QWidgetAction *action = popup_menu_->addRow(row);
            if (!action)
            {
                continue;
            }
            action->setData(i);
            action->setEnabled(row->isEnabled());
            connect(action, &QAction::triggered, this, [this, i]() {
                if (i >= 0 && i < count())
                {
                    setCurrentIndex(i);
                }
            });
        }
        popup_menu_->refreshTheme();
    }

    SingleLevelPopupMenu *popup_menu_ = nullptr;
};

class SourceModeOverviewSwitchButton final : public QPushButton
{
public:
    explicit SourceModeOverviewSwitchButton(QWidget *parent = nullptr)
        : QPushButton(parent)
    {
        setCheckable(true);
        setObjectName(QStringLiteral("sourceModeOverviewSwitch"));
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::TabFocus);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        thumb_position_ = isChecked() ? 1.0 : 0.0;
        thumb_animation_ = new QVariantAnimation(this);
        thumb_animation_->setDuration(160);
        thumb_animation_->setEasingCurve(QEasingCurve::OutCubic);
        connect(thumb_animation_, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
            const qreal progress = std::clamp(value.toReal(), 0.0, 1.0);
            constexpr qreal kPi = 3.14159265358979323846;
            thumb_position_ = thumb_start_position_ + (thumb_target_position_ - thumb_start_position_) * progress;
            thumb_jelly_ = std::sin(progress * kPi);
            update();
        });
        connect(thumb_animation_, &QVariantAnimation::finished, this, [this]() {
            thumb_position_ = thumb_target_position_;
            thumb_jelly_ = 0.0;
            update();
        });
        refreshText();
    }

    void setEnglish(bool english)
    {
        is_english_ = english;
        refreshText();
        update();
    }

    bool switchChecked() const
    {
        return isChecked();
    }

    void setSwitchChecked(bool checked, bool animated)
    {
        const qreal target = checked ? 1.0 : 0.0;
        const bool continuingSameAnimation =
            !animated &&
            thumb_animation_ &&
            thumb_animation_->state() == QAbstractAnimation::Running &&
            qFuzzyCompare(thumb_target_position_, target);

        {
            const QSignalBlocker blocker(this);
            setChecked(checked);
        }
        refreshText();
        if (continuingSameAnimation)
        {
            update();
            return;
        }

        if (animated)
        {
            animateThumbTo(target);
        }
        else
        {
            if (thumb_animation_)
            {
                thumb_animation_->stop();
            }
            thumb_position_ = target;
            thumb_start_position_ = target;
            thumb_target_position_ = target;
            thumb_jelly_ = 0.0;
            update();
        }
    }

protected:
    void nextCheckState() override
    {
        // Source mode changes are routed through the existing data-source combo.
    }

    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event);

        const bool dark = VaporView::isDarkThemeEnabled();
        const bool enabled = isEnabled();
        const QColor border = appThemeColor(AppThemeColor::Border, dark);
        const QColor fill = enabled
            ? appThemeColor(AppThemeColor::PrimarySubtle, dark)
            : appThemeColor(AppThemeColor::SurfaceAlt, dark);
        const QColor trackFill = enabled
            ? appThemeColor(AppThemeColor::Primary, dark)
            : appThemeColor(AppThemeColor::Surface, dark);
        const QColor selectedFill = enabled
            ? appThemeColor(AppThemeColor::Surface, dark)
            : appThemeColor(AppThemeColor::SurfaceAlt, dark);
        const QColor selectedText = enabled
            ? appThemeColor(AppThemeColor::Primary, dark)
            : appThemeColor(AppThemeColor::TextMuted, dark);
        const QColor inactiveText = enabled
            ? appThemeColor(AppThemeColor::White, dark)
            : appThemeColor(AppThemeColor::TextMuted, dark);

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);

        const QRectF outerRect = rect().adjusted(0.5, 0.5, -0.5, -0.5);
        constexpr qreal kOuterRadius = 10.0;
        painter.setPen(QPen(border, 1.0));
        painter.setBrush(fill);
        painter.drawRoundedRect(outerRect, kOuterRadius, kOuterRadius);

        constexpr qreal kInset = 3.0;
        constexpr qreal kInnerInset = 2.0;
        const QRectF trackRect = outerRect.adjusted(kInset, kInset, -kInset, -kInset);
        const QRectF contentRect = trackRect.adjusted(kInnerInset, kInnerInset, -kInnerInset, -kInnerInset);
        const qreal segmentWidth = contentRect.width() / 2.0;
        QRectF selectedRect(contentRect.left() + segmentWidth * thumb_position_,
                            contentRect.top(),
                            segmentWidth,
                            contentRect.height());
        if (enabled && thumb_jelly_ > 0.001)
        {
            const qreal stretch = std::min<qreal>(segmentWidth * 0.24, 12.0) * thumb_jelly_;
            if (thumb_direction_ >= 0)
            {
                selectedRect.adjust(-stretch * 0.35, 0.0, stretch * 0.65, 0.0);
            }
            else
            {
                selectedRect.adjust(-stretch * 0.65, 0.0, stretch * 0.35, 0.0);
            }
            selectedRect.setLeft(std::max(selectedRect.left(), contentRect.left()));
            selectedRect.setRight(std::min(selectedRect.right(), contentRect.right()));
        }
        const QRectF localRect(contentRect.left(), contentRect.top(), segmentWidth, contentRect.height());
        const QRectF remoteRect(contentRect.left() + segmentWidth, contentRect.top(), segmentWidth, contentRect.height());
        const bool localSelected = thumb_position_ < 0.5;

        painter.setPen(Qt::NoPen);
        painter.setBrush(trackFill);
        painter.drawRoundedRect(trackRect, kOuterRadius - kInset, kOuterRadius - kInset);
        painter.setBrush(selectedFill);
        painter.drawRoundedRect(selectedRect, kOuterRadius - kInset - kInnerInset, kOuterRadius - kInset - kInnerInset);

        QFont segmentFont = font();
        segmentFont.setWeight(QFont::DemiBold);
        painter.setFont(segmentFont);
        painter.setPen(localSelected ? selectedText : inactiveText);
        painter.drawText(localRect, Qt::AlignCenter, localText());
        painter.setPen(localSelected ? inactiveText : selectedText);
        painter.drawText(remoteRect, Qt::AlignCenter, remoteText());

        if (hasFocus())
        {
            painter.setPen(QPen(appThemeColor(AppThemeColor::Focus, dark), 1.0));
            painter.setBrush(Qt::NoBrush);
            painter.drawRoundedRect(outerRect.adjusted(2.0, 2.0, -2.0, -2.0),
                                    kOuterRadius - 2.0,
                                    kOuterRadius - 2.0);
        }
    }

private:
    QString localText() const
    {
        return is_english_ ? QStringLiteral("Local") : QStringLiteral("本地");
    }

    QString remoteText() const
    {
        return is_english_ ? QStringLiteral("Remote") : QStringLiteral("远程");
    }

    void refreshText()
    {
        const QString text = is_english_
            ? QStringLiteral("Source: %1").arg(isChecked() ? remoteText() : localText())
            : QStringLiteral("数据源：%1").arg(isChecked() ? remoteText() : localText());
        setText(text);
        setToolTip(text);
        setStatusTip(text);
        setAccessibleName(text);
    }

    void animateThumbTo(qreal target)
    {
        if (!thumb_animation_ || qFuzzyCompare(thumb_position_, target))
        {
            thumb_position_ = target;
            thumb_start_position_ = target;
            thumb_target_position_ = target;
            thumb_jelly_ = 0.0;
            update();
            return;
        }

        thumb_animation_->stop();
        thumb_start_position_ = thumb_position_;
        thumb_target_position_ = target;
        thumb_direction_ = thumb_target_position_ >= thumb_start_position_ ? 1 : -1;
        thumb_jelly_ = 0.0;
        thumb_animation_->setStartValue(0.0);
        thumb_animation_->setEndValue(1.0);
        thumb_animation_->start();
    }

    bool is_english_ = false;
    qreal thumb_position_ = 0.0;
    qreal thumb_start_position_ = 0.0;
    qreal thumb_target_position_ = 0.0;
    qreal thumb_jelly_ = 0.0;
    int thumb_direction_ = 1;
    QVariantAnimation *thumb_animation_ = nullptr;
};

class TemperatureOverviewChannelButton final : public QToolButton
{
public:
    explicit TemperatureOverviewChannelButton(QWidget *parent = nullptr)
        : QToolButton(parent)
    {
        setProperty("textAlignment", QStringLiteral("center"));
        setProperty("iconAlignment", QStringLiteral("right"));
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event);

        QPainter painter(this);
        QStyleOptionToolButton option;
        initStyleOption(&option);
        option.text.clear();
        option.icon = QIcon();
        option.arrowType = Qt::NoArrow;
        style()->drawComplexControl(QStyle::CC_ToolButton, &option, &painter, this);

        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(palette().color(QPalette::ButtonText));
        QFont textFont = font();
        textFont.setWeight(QFont::DemiBold);
        painter.setFont(textFont);
        painter.drawText(rect().adjusted(18, 0, -18, 0), Qt::AlignCenter, text());

        const QIcon currentIcon = icon();
        if (!currentIcon.isNull())
        {
            const QSize size = iconSize().isValid() ? iconSize() : QSize(14, 14);
            const QRect iconRect(width() - size.width() - 10,
                                 (height() - size.height()) / 2,
                                 size.width(),
                                 size.height());
            currentIcon.paint(&painter, iconRect, Qt::AlignCenter, isEnabled() ? QIcon::Normal : QIcon::Disabled);
        }
    }
};

class TemperatureControllerOverviewPanel : public QWidget
{
public:
    explicit TemperatureControllerOverviewPanel(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setObjectName(QStringLiteral("temperatureOverviewPanel"));
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

        auto *layout = new QHBoxLayout(this);
        layout->setContentsMargins(kHomeOverviewBodyPadding,
                                   kHomeOverviewBodyPadding,
                                   kHomeOverviewBodyPadding,
                                   kHomeOverviewBodyPadding);
        layout->setSpacing(7);

        summary_widget_ = new QWidget(this);
        summary_widget_->setObjectName(QStringLiteral("temperatureOverviewSummary"));
        summary_widget_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
        summary_widget_->setFixedWidth(kOverviewControlWidth);
        summary_widget_->installEventFilter(this);
        auto *summaryLayout = new QVBoxLayout(summary_widget_);
        summaryLayout->setContentsMargins(0, 0, 0, 0);
        summaryLayout->setSpacing(kOverviewSummarySpacing);

        channel_button_ = new TemperatureOverviewChannelButton(summary_widget_);
        channel_button_->setObjectName(QStringLiteral("temperatureOverviewChannelButton"));
        channel_button_->setFixedWidth(kOverviewControlWidth);
        channel_button_->setFixedHeight(kOverviewChannelHeight);
        channel_button_->setPopupMode(QToolButton::DelayedPopup);
        channel_button_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        channel_button_->setLayoutDirection(Qt::RightToLeft);
        channel_button_->setIconSize(QSize(14, 14));
        channel_button_->setFocusPolicy(Qt::StrongFocus);
        channel_button_->setCursor(Qt::PointingHandCursor);
        channel_menu_ = new SingleLevelPopupMenu(channel_button_);
        channel_menu_->setObjectName(QStringLiteral("temperatureOverviewChannelMenu"));
        channel_menu_->setFixedWidth(kOverviewMenuOuterWidth);
        channel_menu_->setPanelPadding(kOverviewMenuPadding);
        channel_menu_->setCornerRadius(kOverviewMenuCornerRadius);
        channel_menu_->refreshTheme();
        connect(channel_menu_, &QMenu::aboutToShow, this, [this]() {
            updateSummaryControlHeights();
            if (channel_menu_) channel_menu_->applyRoundedMask();
        });
        connect(channel_button_, &QToolButton::clicked, this, [this]() {
            popupChannelMenu();
        });
        auto configureChannelMenuAction = [this](SingleLevelPopupMenuRow *row, const QString& text) {
            QFont rowFont = row->font();
            rowFont.setWeight(QFont::DemiBold);
            row->setFont(rowFont);
            row->setTextAlignment(SingleLevelPopupTextAlignment::Center);
            row->setCheckSlotWidth(18);
            row->setCheckIconSize(QSize(14, 14));
            row->setHorizontalPadding(12, 10);
            row->setRowSpacing(4);
            row->setRowHeight(kOverviewChannelHeight);
            row->setMinimumRowWidth(kOverviewMenuItemWidth);
            row->setFixedSize(kOverviewMenuItemWidth, kOverviewChannelHeight);
            row->setCursor(Qt::PointingHandCursor);
            row->setFocusPolicy(Qt::NoFocus);
            row->setText(text);
            QWidgetAction *action = channel_menu_->addRow(row);
            action->setText(text);
            return action;
        };
        channel_menu_row_1_ = new SingleLevelPopupMenuRow(channel_menu_);
        channel_menu_row_2_ = new SingleLevelPopupMenuRow(channel_menu_);
        channel_action_1_ = configureChannelMenuAction(channel_menu_row_1_, QStringLiteral("通道1"));
        channel_action_2_ = configureChannelMenuAction(channel_menu_row_2_, QStringLiteral("通道2"));
        for (QAction *action : {channel_action_1_, channel_action_2_})
        {
            action->setCheckable(true);
        }
        connect(channel_action_1_, &QAction::triggered, this, [this]() {
            selectChannel(0);
            if (channel_menu_) channel_menu_->hide();
        });
        connect(channel_action_2_, &QAction::triggered, this, [this]() {
            selectChannel(1);
            if (channel_menu_) channel_menu_->hide();
        });
        channel_button_->setMenu(channel_menu_);
        channel_button_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
        summaryLayout->addWidget(channel_button_, 0);

        target_temp_value_ = new QLabel(summary_widget_);
        target_temp_value_->setObjectName(QStringLiteral("temperatureOverviewValuePill"));
        target_temp_value_->setAlignment(Qt::AlignCenter);
        target_temp_value_->setWordWrap(false);
        target_temp_value_->setFixedWidth(kOverviewControlWidth);
        target_temp_value_->setMinimumHeight(kOverviewMinimumValueHeight);
        target_temp_value_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
        summaryLayout->addWidget(target_temp_value_, 1);

        current_temp_value_ = new QLabel(summary_widget_);
        current_temp_value_->setObjectName(QStringLiteral("temperatureOverviewValuePill"));
        current_temp_value_->setAlignment(Qt::AlignCenter);
        current_temp_value_->setWordWrap(false);
        current_temp_value_->setFixedWidth(kOverviewControlWidth);
        current_temp_value_->setMinimumHeight(kOverviewMinimumValueHeight);
        current_temp_value_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
        summaryLayout->addWidget(current_temp_value_, 1);

        output_switch_button_ = new TemperatureOverviewSwitchButton(summary_widget_);
        output_switch_button_->setFixedWidth(kOverviewControlWidth);
        output_switch_button_->setFixedHeight(kOverviewOutputHeight);
        output_switch_button_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        output_switch_button_->setStyleSheet(QStringLiteral(
            "QPushButton#temperatureOverviewOutputSwitch { min-height: %1px; max-height: %1px; }")
            .arg(kOverviewOutputHeight));
        connect(output_switch_button_, &QPushButton::clicked, this, [this]() {
            const bool requested = !output_switch_button_->switchChecked();
            if (output_enabled_callback_)
            {
                output_enabled_callback_(currentChannelNumber(), requested);
            }
        });
        summaryLayout->addWidget(output_switch_button_, 0);

        layout->addWidget(summary_widget_, 0);

        auto *divider = new QFrame(this);
        divider->setObjectName(QStringLiteral("homeOverviewDivider"));
        divider->setFixedWidth(1);
        divider->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
        layout->addWidget(divider);

        auto *plotSection = new QWidget(this);
        plotSection->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        auto *plotSectionLayout = new QVBoxLayout(plotSection);
        plotSectionLayout->setContentsMargins(0, 0, 0, 0);
        plotSectionLayout->setSpacing(0);

        plot_ = new TemperatureTrendPlotWidget(this);
        plot_->setCompactMode(true);
        plot_->setMinimumHeight(136);
        plot_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        plotSectionLayout->addWidget(plot_, 1);
        layout->addWidget(plotSection, 1);

        setEnglish(false);
        updateData(VaporView::TemperatureControllerData());
        updateSummaryControlHeights();
        scheduleSummaryControlHeightUpdate();
    }

    void setEnglish(bool english)
    {
        is_english_ = english;
        if (channel_action_1_) channel_action_1_->setText(english ? QStringLiteral("CH 1") : QStringLiteral("通道1"));
        if (channel_action_2_) channel_action_2_->setText(english ? QStringLiteral("CH 2") : QStringLiteral("通道2"));
        if (output_switch_button_) output_switch_button_->setEnglish(english);
        if (plot_)
        {
            plot_->setEnglish(english);
        }
        updateChannelButtonText();
        updateThemedIcons();
        refreshChannelUi();
    }

    void updateData(const VaporView::TemperatureControllerData& sample)
    {
        latest_data_ = sample;
        if (sample.valid)
        {
            for (int i = 0; i < static_cast<int>(measured_temperature_history_.size()); ++i)
            {
                const double target = sample.channels[i].target_temperature_c;
                if (std::isfinite(target))
                {
                    target_temperature_by_channel_[i] = target;
                }
                const double measured = sample.channels[i].measured_temperature_c;
                if (std::isfinite(measured))
                {
                    auto& history = measured_temperature_history_[i];
                    history.append(measured);
                    while (history.size() > kTemperatureControllerHistoryLimit)
                    {
                        history.removeFirst();
                    }
                }
            }
        }
        refreshChannelUi();
    }

    void setOutputEnabledCallback(std::function<void(quint8, bool)> callback)
    {
        output_enabled_callback_ = std::move(callback);
    }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QWidget::resizeEvent(event);
        updateSummaryControlHeights();
        scheduleSummaryControlHeightUpdate();
    }

    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched == summary_widget_ && event->type() == QEvent::Resize)
        {
            scheduleSummaryControlHeightUpdate();
        }
        return QWidget::eventFilter(watched, event);
    }

public:
    void updateThemedIcons()
    {
        if (channel_button_)
        {
            channel_button_->setIcon(createLucideIcon(QStringLiteral("chevron-down"),
                                                      toolbarColor(AppThemeColor::ToolbarBlue)));
            channel_button_->setIconSize(QSize(14, 14));
        }
        syncChannelMenuRow(channel_menu_row_1_, channel_action_1_);
        syncChannelMenuRow(channel_menu_row_2_, channel_action_2_);
    }

private:
    static constexpr int kOverviewControlWidth = 99;
    static constexpr int kOverviewMenuPadding = 12;
    static constexpr int kOverviewMenuCornerRadius = 10;
    static constexpr int kOverviewMenuShadowMargin = 22;
    static constexpr int kOverviewMenuItemWidth = kOverviewControlWidth;
    static constexpr int kOverviewMenuOuterWidth = kOverviewControlWidth + kOverviewMenuShadowMargin * 2;
    static constexpr int kOverviewSummarySpacing = 4;
    static constexpr int kOverviewChannelHeight = 34;
    static constexpr int kOverviewMinimumValueHeight = 44;
    static constexpr int kOverviewOutputHeight = 56;

    quint8 currentChannelNumber() const
    {
        return static_cast<quint8>(currentChannelIndex() + 1);
    }

    void scheduleSummaryControlHeightUpdate()
    {
        if (summary_height_update_pending_)
        {
            return;
        }
        summary_height_update_pending_ = true;
        QTimer::singleShot(0, this, [this]() {
            summary_height_update_pending_ = false;
            updateSummaryControlHeights();
        });
    }

    void updateSummaryControlHeights()
    {
        if (!summary_widget_ || !channel_button_)
        {
            return;
        }

        const int channelHeight = channel_button_->height();
        auto setMenuItemHeight = [](SingleLevelPopupMenuRow *widget, const QSize& size) {
            if (!widget)
            {
                return;
            }

            widget->setRowHeight(size.height());
            widget->setMinimumRowWidth(size.width());
            if (widget->minimumSize() != size || widget->maximumSize() != size || widget->size() != size)
            {
                widget->setFixedSize(size);
                widget->resize(size);
                widget->updateGeometry();
            }
        };
        setMenuItemHeight(channel_menu_row_1_, QSize(kOverviewMenuItemWidth, channelHeight));
        setMenuItemHeight(channel_menu_row_2_, QSize(kOverviewMenuItemWidth, channelHeight));
        if (channel_menu_)
        {
            channel_menu_->setFixedWidth(kOverviewMenuOuterWidth);
            channel_menu_->refreshTheme();
        }
    }

    void popupChannelMenu()
    {
        if (!channel_button_ || !channel_menu_)
        {
            return;
        }

        updateSummaryControlHeights();
        channel_menu_->popupFrom(channel_button_);
    }

    int currentChannelIndex() const
    {
        return std::clamp(selected_channel_index_, 0, 1);
    }

    QString channelText(int index) const
    {
        if (is_english_)
        {
            return index == 0 ? QStringLiteral("CH 1") : QStringLiteral("CH 2");
        }
        return index == 0 ? QStringLiteral("通道1") : QStringLiteral("通道2");
    }

    void selectChannel(int index)
    {
        const int nextIndex = std::clamp(index, 0, 1);
        if (selected_channel_index_ == nextIndex)
        {
            updateChannelButtonText();
            return;
        }
        selected_channel_index_ = nextIndex;
        updateChannelButtonText();
        refreshChannelUi();
    }

    void updateChannelButtonText()
    {
        if (channel_button_)
        {
            const QString text = channelText(currentChannelIndex());
            channel_button_->setText(text);
            channel_button_->setToolTip(is_english_
                ? QStringLiteral("Select temperature controller channel")
                : QStringLiteral("选择温控通道"));
            channel_button_->setAccessibleName(channel_button_->toolTip());
        }
        if (channel_action_1_) channel_action_1_->setChecked(currentChannelIndex() == 0);
        if (channel_action_2_) channel_action_2_->setChecked(currentChannelIndex() == 1);
        syncChannelMenuRow(channel_menu_row_1_, channel_action_1_);
        syncChannelMenuRow(channel_menu_row_2_, channel_action_2_);
    }

    void syncChannelMenuRow(SingleLevelPopupMenuRow *row, QAction *action)
    {
        if (!row || !action)
        {
            return;
        }
        row->setText(action->text());
        const bool selected = action->isChecked();
        row->setCheckIcon(createLucideIcon(QStringLiteral("check"),
                                           toolbarColor(AppThemeColor::MenuCheckText)));
        row->setChecked(selected);
        row->refreshTheme();
        row->update();
    }

    void refreshChannelUi()
    {
        const int index = currentChannelIndex();
        const bool valid = latest_data_.valid;
        const VaporView::TemperatureControllerChannelData& channel = latest_data_.channels[index];
        const bool measuredValid = valid && std::isfinite(channel.measured_temperature_c);
        const bool targetValid = valid && std::isfinite(channel.target_temperature_c);
        setTemperatureOverviewPillText(
            target_temp_value_,
            is_english_ ? QStringLiteral("Target Temp °C") : QStringLiteral("目标温度℃"),
            temperatureOverviewNumberText(targetValid ? channel.target_temperature_c : std::numeric_limits<double>::quiet_NaN()));
        setTemperatureOverviewPillText(
            current_temp_value_,
            is_english_ ? QStringLiteral("Current Temp °C") : QStringLiteral("当前温度℃"),
            temperatureOverviewNumberText(measuredValid ? channel.measured_temperature_c : std::numeric_limits<double>::quiet_NaN()));
        if (channel_button_)
        {
            channel_button_->setProperty("available", valid);
            channel_button_->setEnabled(valid);
            channel_button_->style()->unpolish(channel_button_);
            channel_button_->style()->polish(channel_button_);
            channel_button_->update();
        }
        if (output_switch_button_)
        {
            const bool outputEnabled = valid && channel.output_enabled;
            output_switch_button_->setEnabled(valid);
            output_switch_button_->setSwitchChecked(outputEnabled, output_switch_button_->switchChecked() != outputEnabled);
        }
        if (plot_)
        {
            plot_->setChannelIndex(index);
            plot_->setTargetTemperature(target_temperature_by_channel_[index]);
            plot_->setSamples(measured_temperature_history_[index]);
        }
    }

    QToolButton *channel_button_ = nullptr;
    QWidget *summary_widget_ = nullptr;
    SingleLevelPopupMenu *channel_menu_ = nullptr;
    QAction *channel_action_1_ = nullptr;
    QAction *channel_action_2_ = nullptr;
    SingleLevelPopupMenuRow *channel_menu_row_1_ = nullptr;
    SingleLevelPopupMenuRow *channel_menu_row_2_ = nullptr;
    QLabel *target_temp_value_ = nullptr;
    QLabel *current_temp_value_ = nullptr;
    TemperatureOverviewSwitchButton *output_switch_button_ = nullptr;
    TemperatureTrendPlotWidget *plot_ = nullptr;
    VaporView::TemperatureControllerData latest_data_;
    std::array<QVector<double>, 2> measured_temperature_history_{};
    std::array<double, 2> target_temperature_by_channel_{
        std::numeric_limits<double>::quiet_NaN(),
        std::numeric_limits<double>::quiet_NaN()};
    std::function<void(quint8, bool)> output_enabled_callback_;
    int selected_channel_index_ = 0;
    bool summary_height_update_pending_ = false;
    bool is_english_ = false;
};

TemperatureControllerPanel::TemperatureControllerPanel(QWidget *parent)
    : QWidget(parent)
{
    setupUi();
}

void TemperatureControllerPanel::setupUi()
{
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 10, 12, 12);
    layout->setSpacing(10);

    auto *statusLayout = new QGridLayout();
    statusLayout->setHorizontalSpacing(8);
    statusLayout->setVerticalSpacing(6);
    internal_temperature_lbl_ = new QLabel(this);
    internal_temperature_lbl_->setObjectName(QStringLiteral("fieldLabel"));
    internal_temperature_lbl_->setMinimumHeight(22);
    setFixedTextLabelWidth(internal_temperature_lbl_, temperatureControllerStatusLabelWidthCandidates(), 4);
    internal_temperature_label_ = new QLabel(QStringLiteral("--- °C"), this);
    internal_temperature_label_->setObjectName(QStringLiteral("highlightedValue"));
    internal_temperature_label_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    internal_temperature_label_->setMinimumHeight(22);
    internal_temperature_label_->setMinimumWidth(kTemperatureControllerValueWidth);
    internal_temperature_label_->setMaximumWidth(kTemperatureControllerValueWidth);
    error_code_lbl_ = new QLabel(this);
    error_code_lbl_->setObjectName(QStringLiteral("fieldLabel"));
    error_code_lbl_->setMinimumHeight(22);
    setFixedTextLabelWidth(error_code_lbl_, temperatureControllerStatusLabelWidthCandidates(), 4);
    error_code_label_ = new QLabel(QStringLiteral("0x0000"), this);
    error_code_label_->setObjectName(QStringLiteral("highlightedValue"));
    error_code_label_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    error_code_label_->setMinimumHeight(22);
    error_code_label_->setMinimumWidth(kTemperatureControllerValueWidth);
    error_code_label_->setMaximumWidth(kTemperatureControllerValueWidth);
    controller_mode_lbl_ = new QLabel(this);
    controller_mode_lbl_->setObjectName(QStringLiteral("fieldLabel"));
    controller_mode_lbl_->setProperty("temperatureControllerModeLabel", true);
    controller_mode_lbl_->setMinimumHeight(22);
    setFixedTextLabelWidth(controller_mode_lbl_, temperatureControllerStatusLabelWidthCandidates(), 4);
    controller_mode_combo_ = new SingleLevelPopupComboBox(this);
    controller_mode_combo_->setObjectName(QStringLiteral("temperatureControllerModeCombo"));
    controller_mode_combo_->setFixedWidth(206);
    controller_mode_combo_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    controller_mode_combo_->addItem(QStringLiteral("独立控制"), 0);
    controller_mode_combo_->addItem(QStringLiteral("通道1温差控制"), 1);
    controller_mode_combo_->addItem(QStringLiteral("通道2跟随输出"), 2);
    controller_mode_combo_->addItem(QStringLiteral("温差控制+跟随输出"), 3);
    connect(controller_mode_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        emit controllerModeRequested(static_cast<quint16>(controller_mode_combo_->currentData().toUInt()));
    });
    rate_label_ = new VaporView::VisualTextLabel(this);
    rate_label_->setObjectName(QStringLiteral("rateLabel"));
    rate_label_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    rate_label_->setMinimumHeight(22);
    setFixedNumericLabelWidth(rate_label_, {QStringLiteral("-999.9 Hz"), QStringLiteral("999.9 Hz"), QStringLiteral("-- Hz")}, 4);
    statusLayout->addWidget(internal_temperature_lbl_, 0, 0, Qt::AlignVCenter | Qt::AlignLeft);
    statusLayout->addWidget(internal_temperature_label_, 0, 1, Qt::AlignVCenter | Qt::AlignLeft);
    statusLayout->addWidget(error_code_lbl_, 0, 2, Qt::AlignVCenter | Qt::AlignLeft);
    statusLayout->addWidget(error_code_label_, 0, 3, Qt::AlignVCenter | Qt::AlignLeft);
    statusLayout->addWidget(rate_label_, 0, 5, Qt::AlignVCenter | Qt::AlignRight);
    statusLayout->addWidget(controller_mode_lbl_, 0, 6, Qt::AlignVCenter | Qt::AlignLeft);
    statusLayout->addWidget(controller_mode_combo_, 0, 7, Qt::AlignVCenter | Qt::AlignLeft);
    statusLayout->setColumnStretch(4, 1);
    layout->addLayout(statusLayout);

    auto *configCard = new QFrame(this);
    configCard->setObjectName(QStringLiteral("temperatureConfigCard"));
    configCard->setFrameShape(QFrame::NoFrame);
    configCard->setAttribute(Qt::WA_StyledBackground, true);
    configCard->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto *configCardLayout = new QVBoxLayout(configCard);
    configCardLayout->setContentsMargins(12, 12, 12, 12);
    configCardLayout->setSpacing(10);

    auto *channelTopRow = new QWidget(configCard);
    channelTopRow->setObjectName(QStringLiteral("temperatureChannelTopRow"));
    channelTopRow->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto *channelTopRowLayout = new QVBoxLayout(channelTopRow);
    channelTopRowLayout->setContentsMargins(0, 0, 0, 0);
    channelTopRowLayout->setSpacing(8);

    auto *channelSelectorRow = new QWidget(channelTopRow);
    channelSelectorRow->setObjectName(QStringLiteral("temperatureChannelSelectorRow"));
    channelSelectorRow->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    auto *channelSelectorRowLayout = new QHBoxLayout(channelSelectorRow);
    channelSelectorRowLayout->setContentsMargins(0, 0, 0, 0);
    channelSelectorRowLayout->setSpacing(12);

    channel_top_bar_ = new QFrame(channelTopRow);
    channel_top_bar_->setObjectName(QStringLiteral("temperatureChannelTopBar"));
    channel_top_bar_->setFrameShape(QFrame::NoFrame);
    channel_top_bar_->setAttribute(Qt::WA_StyledBackground, true);
    channel_top_bar_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    auto *channelTopBarLayout = new QHBoxLayout(channel_top_bar_);
    channelTopBarLayout->setContentsMargins(6, 6, 6, 6);
    channelTopBarLayout->setSpacing(6);

    auto createChannelButton = [this](int index) {
        auto *button = new QPushButton(this);
        button->setObjectName(QStringLiteral("temperatureChannelSelectorButton%1").arg(index + 1));
        button->setProperty("temperatureChannelSelector", true);
        button->setCheckable(true);
        button->setCursor(Qt::PointingHandCursor);
        button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        button->setFixedSize(72, 34);
        button->setText(index == 0 ? QStringLiteral("通道1") : QStringLiteral("通道2"));
        connect(button, &QPushButton::clicked, this, [this, index]() {
            selectChannel(index);
        });
        return button;
    };
    channel_button_1_ = createChannelButton(0);
    channel_button_2_ = createChannelButton(1);
    common_settings_button_ = new QPushButton(this);
    common_settings_button_->setObjectName(QStringLiteral("temperatureCommonSettingsButton"));
    common_settings_button_->setProperty("temperatureChannelSelector", true);
    common_settings_button_->setCheckable(true);
    common_settings_button_->setCursor(Qt::PointingHandCursor);
    common_settings_button_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    common_settings_button_->setFixedSize(88, 34);
    common_settings_button_->setText(QStringLiteral("通用设置"));
    connect(common_settings_button_, &QPushButton::clicked, this, [this]() {
        selectChannel(2);
    });
    channelTopBarLayout->addWidget(channel_button_1_);
    channelTopBarLayout->addWidget(channel_button_2_);
    channelTopBarLayout->addWidget(common_settings_button_);
    channelSelectorRowLayout->addWidget(channel_top_bar_, 0, Qt::AlignVCenter);

    output_enable_top_label_ = new QLabel(QStringLiteral("输出使能"), channelSelectorRow);
    output_enable_top_label_->setObjectName(QStringLiteral("temperatureOutputEnableTopLabel"));
    output_enable_top_label_->setProperty("temperatureOutputEnableTopLabel", true);
    output_enable_top_label_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    output_enable_top_label_->setMinimumHeight(kTemperatureControllerTopEnableHeight);
    channelSelectorRowLayout->addWidget(output_enable_top_label_, 0, Qt::AlignVCenter);

    auto createEnableSwitch = [this, channelSelectorRow](int index) {
        auto *enableSwitch = new TemperatureOverviewSwitchButton(channelSelectorRow);
        enableSwitch->setObjectName(QStringLiteral("temperatureOutputEnableSwitchChannel%1").arg(index + 1));
        enableSwitch->setProperty("temperatureOutputEnableSwitch", true);
        enableSwitch->setFixedSize(kTemperatureControllerTopEnableWidth, kTemperatureControllerTopEnableHeight);
        enableSwitch->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        const quint8 channelNumber = static_cast<quint8>(index + 1);
        connect(enableSwitch, &QPushButton::clicked, this, [this, channelNumber, enableSwitch]() {
            emit outputEnabledRequested(channelNumber, !enableSwitch->switchChecked());
        });
        return enableSwitch;
    };
    channels_[0].enable_switch = createEnableSwitch(0);
    channels_[1].enable_switch = createEnableSwitch(1);
    channelSelectorRowLayout->addWidget(channels_[0].enable_switch, 0, Qt::AlignVCenter);
    channelSelectorRowLayout->addWidget(channels_[1].enable_switch, 0, Qt::AlignVCenter);

    common_.factory_reset_button = new QPushButton(QStringLiteral("恢复出厂设置"), channelSelectorRow);
    common_.factory_reset_button->setObjectName(QStringLiteral("temperatureFactoryResetButton"));
    common_.factory_reset_button->setCursor(Qt::PointingHandCursor);
    common_.factory_reset_button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    common_.factory_reset_button->setFixedSize(142, 34);
    common_.factory_reset_button->setIconSize(QSize(18, 18));
    common_.factory_reset_button->setIcon(createLucideIcon(QStringLiteral("refresh-cw"),
                                                           appThemeColor(AppThemeColor::Danger, VaporView::isDarkThemeEnabled())));
    common_.factory_reset_button->setVisible(false);
    connect(common_.factory_reset_button, &QPushButton::clicked, this, [this]() {
        emit factoryResetRequested();
    });
    channelSelectorRowLayout->addWidget(common_.factory_reset_button, 0, Qt::AlignVCenter);

    channel_top_controls_stack_ = new QStackedWidget(channelSelectorRow);
    channel_top_controls_stack_->setObjectName(QStringLiteral("temperatureChannelTopControlsStack"));
    channel_top_controls_stack_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    channel_top_controls_stack_->setFixedHeight(kTemperatureControllerTopControlsHeight);
    channel_top_controls_stack_->addWidget(createChannelTopControlsPage(0));
    channel_top_controls_stack_->addWidget(createChannelTopControlsPage(1));
    channel_top_controls_stack_->addWidget(createCommonTopControlsPage());
    channelSelectorRowLayout->addWidget(channel_top_controls_stack_, 0, Qt::AlignVCenter);
    channelTopRowLayout->addWidget(channelSelectorRow, 0, Qt::AlignLeft);
    configCardLayout->addWidget(channelTopRow, 0, Qt::AlignLeft);

    channel_stack_ = new QStackedWidget(configCard);
    channel_stack_->setObjectName(QStringLiteral("temperatureChannelStack"));
    channel_stack_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    channel_stack_->setFixedHeight(kTemperatureControllerChannelStackHeight);
    channel_stack_->addWidget(createChannelPage(0));
    channel_stack_->addWidget(createChannelPage(1));
    channel_stack_->addWidget(createCommonSettingsPage());
    configCardLayout->addWidget(channel_stack_, 0);
    selectChannel(0);
    layout->addWidget(configCard, 0);

    temperature_plot_ = new TemperatureTrendPlotWidget(this);
    temperature_plot_->setProperty("temperatureConfigPlot", true);
    temperature_plot_->setCompactMode(true);
    temperature_plot_->setMinimumHeight(220);
    temperature_plot_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    layout->addWidget(temperature_plot_, 1);

    status_label_ = new QLabel(this);
    status_label_->setObjectName(QStringLiteral("fieldLabel"));
    status_label_->setMinimumHeight(20);
    status_label_->setWordWrap(true);
    layout->addWidget(status_label_);
    setEnglish(false);
    setCommandStatus(QStringLiteral("写入命令会在天空端读回确认后才返回成功。"));
    updateData(VaporView::TemperatureControllerData());
}

QWidget *TemperatureControllerPanel::createChannelTopControlsPage(int index)
{
    QWidget *page = new QWidget(channel_top_controls_stack_);
    page->setObjectName(QStringLiteral("temperatureChannelTopControlsPageChannel%1").arg(index + 1));
    page->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    page->setFixedHeight(kTemperatureControllerTopControlsHeight);
    auto *layout = new QHBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);
    ChannelWidgets& channel = channels_[index];

    auto makeFieldLabel = [](const QString& text) {
        auto *label = new QLabel(text);
        label->setObjectName(QStringLiteral("fieldLabel"));
        label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        label->setMinimumHeight(22);
        return label;
    };

    auto addTopField = [layout, &makeFieldLabel](const QString& labelText, QWidget *editor, QLabel *&label) {
        label = makeFieldLabel(labelText);
        editor->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        auto *field = new QWidget();
        field->setObjectName(QStringLiteral("temperatureTopBarField"));
        field->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        field->setFixedHeight(kTemperatureControllerTopControlsHeight);
        auto *fieldLayout = new QHBoxLayout(field);
        fieldLayout->setContentsMargins(0, 0, 0, 0);
        fieldLayout->setSpacing(8);
        fieldLayout->addWidget(label, 0, Qt::AlignLeft | Qt::AlignVCenter);
        fieldLayout->addWidget(editor, 0, Qt::AlignLeft | Qt::AlignVCenter);
        layout->addWidget(field, 0, Qt::AlignVCenter);
    };

    auto createCombo = [this](int width) {
        auto *combo = new SingleLevelPopupComboBox(this);
        combo->setFixedWidth(width);
        combo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
        return combo;
    };

    const quint8 channelNumber = static_cast<quint8>(index + 1);
    channel.mode_combo = createCombo(kTemperatureControllerTopModeWidth);
    channel.mode_combo->setObjectName(QStringLiteral("temperatureOutputModeComboChannel%1").arg(index + 1));
    channel.mode_combo->addItem(QStringLiteral("制冷和加热"), 0);
    channel.mode_combo->addItem(QStringLiteral("制冷"), 1);
    channel.mode_combo->addItem(QStringLiteral("加热"), 2);
    channel.mode_combo->addItem(QStringLiteral("关闭"), 3);
    addTopField(QStringLiteral("输出模式"), channel.mode_combo, channel.mode_label_text);

    channel.target_spin = new QDoubleSpinBox(this);
    channel.target_spin->setObjectName(QStringLiteral("temperatureTargetSpinChannel%1").arg(index + 1));
    channel.target_spin->setRange(-40.0, 100.0);
    channel.target_spin->setDecimals(5);
    channel.target_spin->setSuffix(QStringLiteral(" °C"));
    channel.target_spin->setFixedWidth(kTemperatureControllerTopTargetWidth);
    addTopField(QStringLiteral("目标温度(°C)"), channel.target_spin, channel.target_label_text);

    connect(channel.mode_combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, channelNumber, combo = channel.mode_combo](int) {
        emit outputModeRequested(channelNumber, static_cast<quint16>(combo->currentData().toUInt()));
    });
    connect(channel.target_spin, &QDoubleSpinBox::editingFinished, this, [this, channelNumber, spin = channel.target_spin]() {
        emit targetTemperatureRequested(channelNumber, spin->value());
    });

    return page;
}

QWidget *TemperatureControllerPanel::createCommonTopControlsPage()
{
    QWidget *page = new QWidget(channel_top_controls_stack_);
    page->setObjectName(QStringLiteral("temperatureChannelTopControlsPageCommon"));
    page->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    page->setFixedHeight(kTemperatureControllerTopControlsHeight);
    auto *layout = new QHBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);

    auto makeFieldLabel = [](const QString& text) {
        auto *label = new QLabel(text);
        label->setObjectName(QStringLiteral("fieldLabel"));
        label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        label->setMinimumHeight(22);
        return label;
    };

    auto addTopField = [layout, &makeFieldLabel](const QString& labelText, QWidget *editor, QLabel *&label) {
        label = makeFieldLabel(labelText);
        editor->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        auto *field = new QWidget();
        field->setObjectName(QStringLiteral("temperatureTopBarField"));
        field->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        field->setFixedHeight(kTemperatureControllerTopControlsHeight);
        auto *fieldLayout = new QHBoxLayout(field);
        fieldLayout->setContentsMargins(0, 0, 0, 0);
        fieldLayout->setSpacing(8);
        fieldLayout->addWidget(label, 0, Qt::AlignLeft | Qt::AlignVCenter);
        fieldLayout->addWidget(editor, 0, Qt::AlignLeft | Qt::AlignVCenter);
        layout->addWidget(field, 0, Qt::AlignVCenter);
    };

    common_.address_spin = new QSpinBox(this);
    common_.address_spin->setObjectName(QStringLiteral("temperatureDeviceAddressSpin"));
    common_.address_spin->setRange(1, 247);
    common_.address_spin->setFixedWidth(kTemperatureControllerInputWidth);
    addTopField(QStringLiteral("设置温控器485站号"), common_.address_spin, common_.address_label_text);

    common_.rs485_baud_combo = new QComboBox(this);
    common_.rs485_baud_combo->setObjectName(QStringLiteral("temperatureRs485BaudCombo"));
    common_.rs485_baud_combo->setFixedWidth(kTemperatureControllerWideInputWidth);
    common_.rs485_baud_combo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    const QList<int> baudRates = {4800, 9600, 19200, 38400, 57600, 115200, 230400, 460800};
    for (int i = 0; i < baudRates.size(); ++i)
    {
        common_.rs485_baud_combo->addItem(QString::number(baudRates.at(i)), i);
    }
    addTopField(QStringLiteral("设置485串口波特率"), common_.rs485_baud_combo, common_.rs485_baud_label_text);

    connect(common_.address_spin, &QSpinBox::editingFinished, this, [this]() {
        emit deviceAddressRequested(static_cast<quint16>(common_.address_spin->value()));
    });
    connect(common_.rs485_baud_combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        emit rs485BaudRequested(static_cast<quint16>(common_.rs485_baud_combo->currentData().toUInt()));
    });
    return page;
}

QWidget *TemperatureControllerPanel::createChannelPage(int index)
{
    QWidget *page = new QWidget(channel_stack_);
    page->setFixedHeight(kTemperatureControllerChannelStackHeight);
    auto *layout = new QGridLayout(page);
    layout->setContentsMargins(16, 8, 16, 8);
    layout->setHorizontalSpacing(16);
    layout->setVerticalSpacing(8);
    layout->setColumnStretch(0, 1);
    layout->setColumnStretch(1, 1);
    layout->setColumnStretch(2, 1);
    ChannelWidgets& channel = channels_[index];

    auto makeFieldLabel = [this](const QString& text) {
        auto *label = new QLabel(text, this);
        label->setObjectName(QStringLiteral("fieldLabel"));
        label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        label->setMinimumHeight(22);
        label->setFixedWidth(kTemperatureControllerCompactLabelWidth);
        return label;
    };

    auto addField = [layout, &makeFieldLabel](int row, int column, const QString& labelText, QWidget *editor, QLabel *&label, int columnSpan = 1) {
        label = makeFieldLabel(labelText);
        editor->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        auto *cell = new QWidget();
        cell->setObjectName(QStringLiteral("temperatureConfigFieldRow"));
        cell->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        cell->setFixedHeight(kTemperatureControllerConfigRowHeight);
        auto *cellLayout = new QHBoxLayout(cell);
        cellLayout->setContentsMargins(0, 0, 0, 0);
        cellLayout->setSpacing(10);
        cellLayout->addWidget(label, 0, Qt::AlignLeft | Qt::AlignVCenter);
        cellLayout->addStretch(1);
        cellLayout->addWidget(editor, 0, Qt::AlignRight | Qt::AlignVCenter);
        layout->addWidget(cell, row, column, 1, columnSpan);
    };

    channel.max_output_spin = new QSpinBox(this);
    channel.max_output_spin->setObjectName(QStringLiteral("temperatureMaxOutputSpinChannel%1").arg(index + 1));
    setWidgetBooleanProperty(channel.max_output_spin, "temperatureMaxOutputWarning", true);
    setDangerTextPalette(channel.max_output_spin);
    channel.max_output_spin->setRange(0, 90);
    channel.max_output_spin->setSuffix(QStringLiteral(" %"));
    channel.max_output_spin->setFixedWidth(kTemperatureControllerCompactInputWidth);
    addField(0, 0, QStringLiteral("最大输出电压百分比(%)"), channel.max_output_spin, channel.max_output_label_text);
    if (channel.max_output_label_text)
    {
        setWidgetBooleanProperty(channel.max_output_label_text, "temperatureMaxOutputWarning", true);
        setDangerTextPalette(channel.max_output_label_text);
        channel.max_output_label_text->setFixedWidth(kTemperatureControllerMaxOutputLabelWidth);
    }

    channel.kp_spin = new QSpinBox(this);
    channel.ki_spin = new QSpinBox(this);
    channel.kd_spin = new QSpinBox(this);
    channel.kp_spin->setObjectName(QStringLiteral("temperaturePidKpSpinChannel%1").arg(index + 1));
    channel.ki_spin->setObjectName(QStringLiteral("temperaturePidKiSpinChannel%1").arg(index + 1));
    channel.kd_spin->setObjectName(QStringLiteral("temperaturePidKdSpinChannel%1").arg(index + 1));
    for (QSpinBox *spin : {channel.kp_spin, channel.ki_spin, channel.kd_spin})
    {
        spin->setRange(0, std::numeric_limits<int>::max());
        spin->setFixedWidth(kTemperatureControllerCompactPidInputWidth);
        spin->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    }
    auto *pidEditor = new QWidget(this);
    pidEditor->setObjectName(QStringLiteral("temperaturePidEditorChannel%1").arg(index + 1));
    pidEditor->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    auto *pidLayout = new QHBoxLayout(pidEditor);
    pidLayout->setContentsMargins(0, 0, 0, 0);
    pidLayout->setSpacing(6);
    auto addPidSpin = [pidLayout](const QString& labelText, QSpinBox *spin) {
        auto *label = new QLabel(labelText);
        label->setObjectName(QStringLiteral("fieldLabel"));
        label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        label->setMinimumWidth(12);
        pidLayout->addWidget(label, 0, Qt::AlignVCenter);
        pidLayout->addWidget(spin, 0, Qt::AlignVCenter);
    };
    addPidSpin(QStringLiteral("P"), channel.kp_spin);
    addPidSpin(QStringLiteral("I"), channel.ki_spin);
    addPidSpin(QStringLiteral("D"), channel.kd_spin);
    addField(0, 1, QStringLiteral("PID"), pidEditor, channel.pid_label_text);

    channel.auto_pid_combo = new SingleLevelPopupComboBox(this);
    channel.auto_pid_combo->setObjectName(QStringLiteral("temperatureAutoPidComboChannel%1").arg(index + 1));
    channel.auto_pid_combo->setFixedWidth(kTemperatureControllerCompactInputWidth);
    channel.auto_pid_combo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    channel.auto_pid_combo->addItem(QStringLiteral("关闭"), 0);
    channel.auto_pid_combo->addItem(QStringLiteral("PID自整定"), 1);
    channel.auto_pid_combo->addItem(QStringLiteral("实时优化(预留)"), 2);
    addField(0, 2, QStringLiteral("自动 PID"), channel.auto_pid_combo, channel.auto_pid_label_text);
    const quint8 channelNumber = static_cast<quint8>(index + 1);
    connect(channel.auto_pid_combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, channelNumber, combo = channel.auto_pid_combo](int) {
        emit autoPidRequested(channelNumber, static_cast<quint16>(combo->currentData().toUInt()));
    });
    connect(channel.max_output_spin, &QSpinBox::editingFinished, this, [this, channelNumber, spin = channel.max_output_spin]() {
        emit maxOutputPercentRequested(channelNumber, static_cast<quint16>(spin->value()));
    });
    auto emitPid = [this, channelNumber, index]() {
        const ChannelWidgets& channel = channels_[index];
        emit pidRequested(channelNumber,
                          static_cast<quint32>(channel.kp_spin->value()),
                          static_cast<quint32>(channel.ki_spin->value()),
                          static_cast<quint32>(channel.kd_spin->value()));
    };
    connect(channel.kp_spin, &QSpinBox::editingFinished, this, emitPid);
    connect(channel.ki_spin, &QSpinBox::editingFinished, this, emitPid);
    connect(channel.kd_spin, &QSpinBox::editingFinished, this, emitPid);
    return page;
}

QWidget *TemperatureControllerPanel::createCommonSettingsPage()
{
    QWidget *page = new QWidget(channel_stack_);
    page->setObjectName(QStringLiteral("temperatureCommonSettingsPage"));
    page->setFixedHeight(kTemperatureControllerChannelStackHeight);
    auto *layout = new QGridLayout(page);
    layout->setContentsMargins(16, 8, 16, 8);
    layout->setHorizontalSpacing(18);
    layout->setVerticalSpacing(8);
    layout->setColumnStretch(0, 1);
    layout->setColumnStretch(1, 1);

    auto makeFieldLabel = [this](const QString& text) {
        auto *label = new QLabel(text, this);
        label->setObjectName(QStringLiteral("fieldLabel"));
        label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        label->setMinimumHeight(22);
        label->setFixedWidth(kTemperatureControllerControlLabelWidth);
        return label;
    };

    auto addField = [layout, &makeFieldLabel](int row, int column, const QString& labelText, QWidget *editor, QLabel *&label) {
        label = makeFieldLabel(labelText);
        editor->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        auto *cell = new QWidget();
        cell->setObjectName(QStringLiteral("temperatureCommonFieldRow"));
        cell->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        cell->setFixedHeight(kTemperatureControllerConfigRowHeight);
        auto *cellLayout = new QHBoxLayout(cell);
        cellLayout->setContentsMargins(0, 0, 0, 0);
        cellLayout->setSpacing(10);
        cellLayout->addWidget(label, 0, Qt::AlignLeft | Qt::AlignVCenter);
        cellLayout->addStretch(1);
        cellLayout->addWidget(editor, 0, Qt::AlignRight | Qt::AlignVCenter);
        layout->addWidget(cell, row, column);
    };

    common_.overtemp_output_combo = new SingleLevelPopupComboBox(this);
    common_.overtemp_output_combo->setObjectName(QStringLiteral("temperatureOvertempOutputModeCombo"));
    common_.overtemp_output_combo->setFixedWidth(kTemperatureControllerWideInputWidth);
    common_.overtemp_output_combo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    common_.overtemp_output_combo->addItem(QStringLiteral("继续输出"), 0);
    common_.overtemp_output_combo->addItem(QStringLiteral("关闭输出"), 1);
    addField(0, 0, QStringLiteral("过温输出模式"), common_.overtemp_output_combo, common_.overtemp_output_label_text);

    common_.internal_temperature_edit = new QLineEdit(this);
    common_.internal_temperature_edit->setObjectName(QStringLiteral("temperatureCommonInternalTemperatureEdit"));
    common_.internal_temperature_edit->setReadOnly(true);
    common_.internal_temperature_edit->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    common_.internal_temperature_edit->setFixedWidth(kTemperatureControllerInputWidth);
    addField(0, 1, QStringLiteral("温控器自身温度(°C)"), common_.internal_temperature_edit, common_.internal_temperature_label_text);

    connect(common_.overtemp_output_combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        emit overtempOutputModeRequested(static_cast<quint16>(common_.overtemp_output_combo->currentData().toUInt()));
    });
    return page;
}

void TemperatureControllerPanel::selectChannel(int index)
{
    const int pageIndex = std::clamp(index, 0, 2);
    selected_config_page_index_ = pageIndex;
    if (pageIndex < 2)
    {
        selected_channel_index_ = pageIndex;
    }
    const int channelIndex = std::clamp(selected_channel_index_, 0, 1);
    if (channel_stack_)
    {
        channel_stack_->setFixedHeight(pageIndex < 2
            ? kTemperatureControllerChannelStackHeight
            : kTemperatureControllerCommonStackHeight);
        channel_stack_->setCurrentIndex(pageIndex);
        channel_stack_->updateGeometry();
    }
    if (channel_top_controls_stack_)
    {
        channel_top_controls_stack_->setVisible(true);
        channel_top_controls_stack_->setCurrentIndex(pageIndex < 2 ? channelIndex : 2);
        if (QWidget *currentTopControls = channel_top_controls_stack_->currentWidget())
        {
            const int currentWidth = currentTopControls->sizeHint().width();
            if (currentWidth > 0)
            {
                channel_top_controls_stack_->setFixedWidth(currentWidth);
            }
        }
        channel_top_controls_stack_->updateGeometry();
    }
    if (common_.factory_reset_button)
    {
        common_.factory_reset_button->setVisible(pageIndex == 2);
    }
    if (output_enable_top_label_)
    {
        output_enable_top_label_->setVisible(pageIndex < 2);
    }
    for (int i = 0; i < static_cast<int>(channels_.size()); ++i)
    {
        if (channels_[i].enable_switch)
        {
            channels_[i].enable_switch->setVisible(pageIndex == i);
        }
    }
    auto updateButton = [pageIndex](QPushButton *button, int buttonIndex) {
        if (!button)
        {
            return;
        }
        const QSignalBlocker blocker(button);
        button->setChecked(pageIndex == buttonIndex);
        button->style()->unpolish(button);
        button->style()->polish(button);
        button->update();
    };
    updateButton(channel_button_1_, 0);
    updateButton(channel_button_2_, 1);
    updateButton(common_settings_button_, 2);

    if (temperature_plot_ && pageIndex < 2)
    {
        temperature_plot_->setChannelIndex(channelIndex);
        temperature_plot_->setTargetTemperature(target_temperature_by_channel_[channelIndex]);
        temperature_plot_->setSamples(measured_temperature_history_[channelIndex]);
    }
}

void TemperatureControllerPanel::updateRate(double hz)
{
    if (rate_label_)
    {
        rate_label_->setText((hz > 0.0 && std::isfinite(hz))
            ? fixedDecimalWithUnit(hz, 1, 6, QStringLiteral("Hz"))
            : QStringLiteral("%1 Hz").arg(fixedTextField(QStringLiteral("--"), 6)));
    }
}

void TemperatureControllerPanel::setEnglish(bool english)
{
    is_english_ = english;
    if (temperature_plot_)
    {
        temperature_plot_->setEnglish(english);
    }
    updateChannelTexts();
}

int TemperatureControllerPanel::channelIndex(quint8 channel) const
{
    if (channel == 0 || channel > channels_.size())
    {
        return -1;
    }
    return static_cast<int>(channel - 1);
}

void TemperatureControllerPanel::markCommandPending(VaporView::CommandId command, const VaporView::TemperatureControllerCommand& payload)
{
    const int index = channelIndex(payload.channel == 0 ? 1 : payload.channel);
    PendingChannelEdits *pending = index >= 0 ? &pending_channel_edits_[index] : nullptr;
    switch (command)
    {
    case VaporView::CommandId::SetTemperatureTarget:
        if (pending)
        {
            pending->target_temperature = true;
            pending->target_temperature_c = payload.target_temperature_c;
        }
        break;
    case VaporView::CommandId::SetTemperatureOutputMode:
        if (pending)
        {
            pending->output_mode = true;
            pending->output_mode_value = static_cast<int>(payload.output_mode);
        }
        break;
    case VaporView::CommandId::SetTemperatureMaxOutputPercent:
        if (pending)
        {
            pending->max_output_percent = true;
            pending->max_output_percent_value = static_cast<int>(payload.max_output_percent);
        }
        break;
    case VaporView::CommandId::SetTemperaturePid:
        if (pending)
        {
            pending->pid = true;
            pending->kp = static_cast<int>(payload.kp);
            pending->ki = static_cast<int>(payload.ki);
            pending->kd = static_cast<int>(payload.kd);
        }
        break;
    case VaporView::CommandId::SetTemperatureAutoPid:
        if (pending)
        {
            pending->auto_pid = true;
            pending->auto_pid_mode = static_cast<int>(payload.auto_pid_mode);
        }
        break;
    case VaporView::CommandId::SetTemperatureControllerMode:
        pending_controller_mode_ = true;
        pending_controller_mode_value_ = static_cast<int>(payload.controller_mode);
        break;
    case VaporView::CommandId::SetTemperatureDeviceAddress:
        pending_common_edits_.device_address = true;
        pending_common_edits_.device_address_value = static_cast<int>(payload.device_address);
        break;
    case VaporView::CommandId::SetTemperatureRs485Baud:
        pending_common_edits_.rs485_baud = true;
        pending_common_edits_.rs485_baud_index = static_cast<int>(payload.rs485_baud_index);
        break;
    case VaporView::CommandId::SetTemperatureOvertempOutputMode:
        pending_common_edits_.overtemp_output_mode = true;
        pending_common_edits_.overtemp_output_mode_value = static_cast<int>(payload.overtemp_output_mode);
        break;
    case VaporView::CommandId::RestoreTemperatureFactoryDefaults:
        pending_common_edits_ = PendingCommonEdits{};
        break;
    default:
        break;
    }
}

void TemperatureControllerPanel::clearCommandPending(VaporView::CommandId command, quint8 channel)
{
    const int index = channelIndex(channel == 0 ? 1 : channel);
    PendingChannelEdits *pending = index >= 0 ? &pending_channel_edits_[index] : nullptr;
    switch (command)
    {
    case VaporView::CommandId::SetTemperatureTarget:
        if (pending) pending->target_temperature = false;
        break;
    case VaporView::CommandId::SetTemperatureOutputMode:
        if (pending) pending->output_mode = false;
        break;
    case VaporView::CommandId::SetTemperatureMaxOutputPercent:
        if (pending) pending->max_output_percent = false;
        break;
    case VaporView::CommandId::SetTemperaturePid:
        if (pending) pending->pid = false;
        break;
    case VaporView::CommandId::SetTemperatureAutoPid:
        if (pending) pending->auto_pid = false;
        break;
    case VaporView::CommandId::SetTemperatureControllerMode:
        pending_controller_mode_ = false;
        break;
    case VaporView::CommandId::SetTemperatureDeviceAddress:
        pending_common_edits_.device_address = false;
        break;
    case VaporView::CommandId::SetTemperatureRs485Baud:
        pending_common_edits_.rs485_baud = false;
        break;
    case VaporView::CommandId::SetTemperatureOvertempOutputMode:
        pending_common_edits_.overtemp_output_mode = false;
        break;
    case VaporView::CommandId::RestoreTemperatureFactoryDefaults:
        pending_common_edits_ = PendingCommonEdits{};
        break;
    default:
        break;
    }
}

void TemperatureControllerPanel::updateChannelTexts()
{
    if (internal_temperature_lbl_) internal_temperature_lbl_->setText(is_english_ ? QStringLiteral("Internal:") : QStringLiteral("自身温度:"));
    if (error_code_lbl_) error_code_lbl_->setText(is_english_ ? QStringLiteral("Error:") : QStringLiteral("错误码:"));
    if (controller_mode_lbl_) controller_mode_lbl_->setText(is_english_ ? QStringLiteral("Mode:") : QStringLiteral("温控器模式:"));
    refreshFixedTextLabelWidth(internal_temperature_lbl_);
    refreshFixedTextLabelWidth(error_code_lbl_);
    refreshFixedTextLabelWidth(controller_mode_lbl_);
    if (controller_mode_combo_)
    {
        const QSignalBlocker blocker(controller_mode_combo_);
        controller_mode_combo_->setItemText(0, is_english_ ? QStringLiteral("Independent") : QStringLiteral("独立控制"));
        controller_mode_combo_->setItemText(1, is_english_ ? QStringLiteral("CH1 target follows CH2 temp") : QStringLiteral("通道1温差控制"));
        controller_mode_combo_->setItemText(2, is_english_ ? QStringLiteral("CH2 output follows CH1") : QStringLiteral("通道2跟随输出"));
        controller_mode_combo_->setItemText(3, is_english_ ? QStringLiteral("Combined") : QStringLiteral("温差控制+跟随输出"));
        controller_mode_combo_->setToolTip(is_english_
            ? QStringLiteral("RD105 CONTMODE. Modes 2 and 3 require a resistor temperature sensor on channel 2.")
            : QStringLiteral("RD105 CONTMODE。使用模式2和3时，通道2传感器接口应接入电阻温度传感器。"));
    }
    if (channel_button_1_) channel_button_1_->setText(is_english_ ? QStringLiteral("Channel 1") : QStringLiteral("通道1"));
    if (channel_button_2_) channel_button_2_->setText(is_english_ ? QStringLiteral("Channel 2") : QStringLiteral("通道2"));
    if (common_settings_button_) common_settings_button_->setText(is_english_ ? QStringLiteral("Common") : QStringLiteral("通用设置"));
    if (output_enable_top_label_) output_enable_top_label_->setText(is_english_ ? QStringLiteral("Output Enable") : QStringLiteral("输出使能"));
    if (common_.address_label_text) common_.address_label_text->setText(is_english_ ? QStringLiteral("RS485 address") : QStringLiteral("设置温控器485站号"));
    if (common_.rs485_baud_label_text) common_.rs485_baud_label_text->setText(is_english_ ? QStringLiteral("RS485 baud") : QStringLiteral("设置485串口波特率"));
    if (common_.overtemp_output_label_text) common_.overtemp_output_label_text->setText(is_english_ ? QStringLiteral("Over-temp output") : QStringLiteral("过温输出模式"));
    if (common_.internal_temperature_label_text) common_.internal_temperature_label_text->setText(is_english_ ? QStringLiteral("Internal temp (°C)") : QStringLiteral("温控器自身温度(°C)"));
    if (common_.factory_reset_button)
    {
        common_.factory_reset_button->setText(is_english_ ? QStringLiteral("Factory Reset") : QStringLiteral("恢复出厂设置"));
        common_.factory_reset_button->setIcon(createLucideIcon(QStringLiteral("refresh-cw"),
                                                               appThemeColor(AppThemeColor::Danger, VaporView::isDarkThemeEnabled())));
    }
    if (common_.overtemp_output_combo)
    {
        const QSignalBlocker blocker(common_.overtemp_output_combo);
        common_.overtemp_output_combo->setItemText(0, is_english_ ? QStringLiteral("Continue output") : QStringLiteral("继续输出"));
        common_.overtemp_output_combo->setItemText(1, is_english_ ? QStringLiteral("Disable output") : QStringLiteral("关闭输出"));
    }
    for (ChannelWidgets& channel : channels_)
    {
        if (channel.target_label_text) channel.target_label_text->setText(is_english_ ? QStringLiteral("Target Temp (°C)") : QStringLiteral("目标温度(°C)"));
        if (channel.enable_label_text) channel.enable_label_text->setText(is_english_ ? QStringLiteral("Output Enable") : QStringLiteral("输出使能"));
        if (channel.mode_label_text) channel.mode_label_text->setText(is_english_ ? QStringLiteral("Output Mode") : QStringLiteral("输出模式"));
        if (channel.max_output_label_text)
        {
            channel.max_output_label_text->setText(is_english_ ? QStringLiteral("Max Output Voltage (%)") : QStringLiteral("最大输出电压百分比(%)"));
            setDangerTextPalette(channel.max_output_label_text);
        }
        setDangerTextPalette(channel.max_output_spin);
        if (channel.pid_label_text) channel.pid_label_text->setText(QStringLiteral("PID"));
        if (channel.auto_pid_label_text) channel.auto_pid_label_text->setText(is_english_ ? QStringLiteral("Auto PID") : QStringLiteral("自动 PID"));
        if (channel.enable_switch)
        {
            auto *enableSwitch = static_cast<TemperatureOverviewSwitchButton *>(channel.enable_switch);
            enableSwitch->setEnglish(is_english_);
        }
        if (channel.mode_combo)
        {
            const QSignalBlocker blocker(channel.mode_combo);
            channel.mode_combo->setItemText(0, is_english_ ? QStringLiteral("Cool + Heat") : QStringLiteral("制冷和加热"));
            channel.mode_combo->setItemText(1, is_english_ ? QStringLiteral("Cool") : QStringLiteral("制冷"));
            channel.mode_combo->setItemText(2, is_english_ ? QStringLiteral("Heat") : QStringLiteral("加热"));
            channel.mode_combo->setItemText(3, is_english_ ? QStringLiteral("Off") : QStringLiteral("关闭"));
        }
        if (channel.auto_pid_combo)
        {
            const QSignalBlocker blocker(channel.auto_pid_combo);
            channel.auto_pid_combo->setItemText(0, is_english_ ? QStringLiteral("Off") : QStringLiteral("关闭"));
            channel.auto_pid_combo->setItemText(1, is_english_ ? QStringLiteral("PID auto-tune") : QStringLiteral("PID自整定"));
            channel.auto_pid_combo->setItemText(2, is_english_ ? QStringLiteral("Realtime optimize (reserved)") : QStringLiteral("实时优化(预留)"));
            channel.auto_pid_combo->setToolTip(is_english_
                ? QStringLiteral("RD105 AUTOPID: off, PID auto-tune, or reserved realtime optimization.")
                : QStringLiteral("RD105 AUTOPID：关闭、PID自整定，或预留的实时优化。"));
        }
    }
    if (status_label_ && status_label_->text().isEmpty()) setCommandStatus(is_english_ ? QStringLiteral("Writes are confirmed by reading back from RD105.") : QStringLiteral("写入命令会在天空端读回确认后才返回成功。"));
}

void TemperatureControllerPanel::updateChannelData(int index, const VaporView::TemperatureControllerChannelData& channelData, bool valid)
{
    ChannelWidgets& channel = channels_[index];
    PendingChannelEdits& pending = pending_channel_edits_[index];
    auto hasEditorFocus = [](QWidget *widget) {
        QWidget *focus = QApplication::focusWidget();
        return widget && (widget->hasFocus() || (focus && widget->isAncestorOf(focus)));
    };
    if (valid)
    {
        const QSignalBlocker targetBlocker(channel.target_spin);
        const QSignalBlocker modeBlocker(channel.mode_combo);
        const QSignalBlocker autoPidBlocker(channel.auto_pid_combo);
        const QSignalBlocker maxOutputBlocker(channel.max_output_spin);
        const QSignalBlocker kpBlocker(channel.kp_spin);
        const QSignalBlocker kiBlocker(channel.ki_spin);
        const QSignalBlocker kdBlocker(channel.kd_spin);

        if (pending.target_temperature &&
            std::isfinite(channelData.target_temperature_c) &&
            std::abs(channelData.target_temperature_c - pending.target_temperature_c) < 0.00001)
        {
            pending.target_temperature = false;
        }
        if (!pending.target_temperature && !hasEditorFocus(channel.target_spin))
        {
            channel.target_spin->setValue(channelData.target_temperature_c);
        }

        if (channel.enable_switch)
        {
            auto *enableSwitch = static_cast<TemperatureOverviewSwitchButton *>(channel.enable_switch);
            enableSwitch->setEnabled(valid);
            enableSwitch->setSwitchChecked(channelData.output_enabled,
                                           enableSwitch->switchChecked() != channelData.output_enabled);
        }

        if (pending.output_mode && channelData.output_mode == pending.output_mode_value)
        {
            pending.output_mode = false;
        }
        if (!pending.output_mode && !hasEditorFocus(channel.mode_combo))
        {
            const int modeIndex = channel.mode_combo->findData(channelData.output_mode);
            channel.mode_combo->setCurrentIndex(modeIndex >= 0 ? modeIndex : 0);
        }

        if (pending.auto_pid && channelData.auto_pid_mode == pending.auto_pid_mode)
        {
            pending.auto_pid = false;
        }
        if (!pending.auto_pid && !hasEditorFocus(channel.auto_pid_combo))
        {
            const int autoPidIndex = channel.auto_pid_combo->findData(channelData.auto_pid_mode);
            channel.auto_pid_combo->setCurrentIndex(autoPidIndex >= 0 ? autoPidIndex : 0);
        }

        if (pending.max_output_percent && channelData.max_output_percent == pending.max_output_percent_value)
        {
            pending.max_output_percent = false;
        }
        if (!pending.max_output_percent && !hasEditorFocus(channel.max_output_spin))
        {
            channel.max_output_spin->setValue(channelData.max_output_percent);
        }

        if (pending.pid &&
            channelData.kp == pending.kp &&
            channelData.ki == pending.ki &&
            channelData.kd == pending.kd)
        {
            pending.pid = false;
        }
        if (!pending.pid && !hasEditorFocus(channel.kp_spin))
        {
            channel.kp_spin->setValue(channelData.kp);
        }
        if (!pending.pid && !hasEditorFocus(channel.ki_spin))
        {
            channel.ki_spin->setValue(channelData.ki);
        }
        if (!pending.pid && !hasEditorFocus(channel.kd_spin))
        {
            channel.kd_spin->setValue(channelData.kd);
        }
    }
}

void TemperatureControllerPanel::updateData(const VaporView::TemperatureControllerData& controllerData)
{
    internal_temperature_label_->setText(fixedDecimalWithUnit(controllerData.valid ? controllerData.internal_temperature_c : std::numeric_limits<double>::quiet_NaN(), 2, 8, QStringLiteral("°C")));
    error_code_label_->setText(controllerData.valid ? QStringLiteral("0x%1").arg(controllerData.error_code, 4, 16, QLatin1Char('0')).toUpper() : QStringLiteral("---"));
    if (controller_mode_combo_)
    {
        const QSignalBlocker blocker(controller_mode_combo_);
        if (pending_controller_mode_ &&
            controllerData.valid &&
            controllerData.controller_mode == pending_controller_mode_value_)
        {
            pending_controller_mode_ = false;
        }
        QWidget *focus = QApplication::focusWidget();
        const bool controllerModeHasFocus =
            controller_mode_combo_->hasFocus() ||
            (focus && controller_mode_combo_->isAncestorOf(focus));
        if (!pending_controller_mode_ && !controllerModeHasFocus)
        {
            const int modeIndex = controller_mode_combo_->findData(controllerData.valid ? controllerData.controller_mode : 0);
            controller_mode_combo_->setCurrentIndex(modeIndex >= 0 ? modeIndex : 0);
        }
    }
    if (!error_text_label_)
    {
        error_text_label_ = new QLabel(this);
    }
    error_code_label_->setToolTip(controllerData.valid && controllerData.error_code != 0
        ? (is_english_ ? QStringLiteral("RD105 reported an error bitmask. Check the controller/manual before enabling output.")
                       : QStringLiteral("RD105 返回错误位掩码。开启输出前请检查温控器和手册。"))
        : (is_english_ ? QStringLiteral("No error reported") : QStringLiteral("未报告错误")));
    auto hasEditorFocus = [](QWidget *widget) {
        QWidget *focus = QApplication::focusWidget();
        return widget && (widget->hasFocus() || (focus && widget->isAncestorOf(focus)));
    };
    if (controllerData.valid)
    {
        if (pending_common_edits_.device_address &&
            controllerData.device_address == pending_common_edits_.device_address_value)
        {
            pending_common_edits_.device_address = false;
        }
        if (common_.address_spin && !pending_common_edits_.device_address && !hasEditorFocus(common_.address_spin))
        {
            const QSignalBlocker blocker(common_.address_spin);
            common_.address_spin->setValue(std::clamp(controllerData.device_address, common_.address_spin->minimum(), common_.address_spin->maximum()));
        }
        if (pending_common_edits_.rs485_baud &&
            controllerData.rs485_baud_index == pending_common_edits_.rs485_baud_index)
        {
            pending_common_edits_.rs485_baud = false;
        }
        if (common_.rs485_baud_combo && !pending_common_edits_.rs485_baud && !hasEditorFocus(common_.rs485_baud_combo))
        {
            const QSignalBlocker blocker(common_.rs485_baud_combo);
            const int baudIndex = common_.rs485_baud_combo->findData(controllerData.rs485_baud_index);
            common_.rs485_baud_combo->setCurrentIndex(baudIndex >= 0 ? baudIndex : 0);
        }
        if (pending_common_edits_.overtemp_output_mode &&
            controllerData.overtemp_output_mode == pending_common_edits_.overtemp_output_mode_value)
        {
            pending_common_edits_.overtemp_output_mode = false;
        }
        if (common_.overtemp_output_combo && !pending_common_edits_.overtemp_output_mode && !hasEditorFocus(common_.overtemp_output_combo))
        {
            const QSignalBlocker blocker(common_.overtemp_output_combo);
            const int overtempIndex = common_.overtemp_output_combo->findData(controllerData.overtemp_output_mode);
            common_.overtemp_output_combo->setCurrentIndex(overtempIndex >= 0 ? overtempIndex : 0);
        }
    }
    if (common_.internal_temperature_edit)
    {
        common_.internal_temperature_edit->setText(controllerData.valid && std::isfinite(controllerData.internal_temperature_c)
            ? QString::number(controllerData.internal_temperature_c, 'f', 0)
            : QStringLiteral("---"));
    }
    if (controllerData.valid)
    {
        for (int i = 0; i < static_cast<int>(measured_temperature_history_.size()); ++i)
        {
            const double target = controllerData.channels[i].target_temperature_c;
            if (std::isfinite(target))
            {
                target_temperature_by_channel_[i] = target;
            }
            const double measured = controllerData.channels[i].measured_temperature_c;
            if (std::isfinite(measured))
            {
                auto& history = measured_temperature_history_[i];
                history.append(measured);
                while (history.size() > kTemperatureControllerHistoryLimit)
                {
                    history.removeFirst();
                }
            }
        }
    }
    updateChannelData(0, controllerData.channels[0], controllerData.valid);
    updateChannelData(1, controllerData.channels[1], controllerData.valid);
    if (temperature_plot_)
    {
        const int channelIndex = std::clamp(selected_channel_index_, 0, 1);
        temperature_plot_->setChannelIndex(channelIndex);
        temperature_plot_->setTargetTemperature(target_temperature_by_channel_[channelIndex]);
        temperature_plot_->setSamples(measured_temperature_history_[channelIndex]);
    }
}

void TemperatureControllerPanel::setCommandStatus(const QString& text, bool error)
{
    if (!status_label_)
    {
        return;
    }
    status_label_->setText(text);
    status_label_->setProperty("data-valid", !error);
    status_label_->style()->unpolish(status_label_);
    status_label_->style()->polish(status_label_);
}

void TemperatureControllerPanel::setOutputEnabledControl(quint8 channel, bool enabled)
{
    if (channel == 0 || channel > channels_.size())
    {
        return;
    }
    QPushButton *button = channels_[channel - 1].enable_switch;
    if (!button)
    {
        return;
    }
    auto *enableSwitch = static_cast<TemperatureOverviewSwitchButton *>(button);
    enableSwitch->setSwitchChecked(enabled, enableSwitch->switchChecked() != enabled);
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , central_widget_(nullptr)
    , main_layout_(nullptr)
    , window_border_top_(nullptr)
    , window_border_right_(nullptr)
    , window_border_bottom_(nullptr)
    , window_border_left_(nullptr)
    , custom_title_bar_(nullptr)
    , custom_logo_label_(nullptr)
    , custom_title_label_(nullptr)
    , title_menu_btn_(nullptr)
    , title_language_btn_(nullptr)
    , log_side_panel_toggle_btn_(nullptr)
    , window_minimize_btn_(nullptr)
    , window_maximize_btn_(nullptr)
    , window_close_btn_(nullptr)
    , epsilon_panel_(nullptr)
    , gnss_panel_(nullptr)
    , imu_panel_(nullptr)
    , ptb_panel_(nullptr)
    , hmp_panel_(nullptr)
    , lidar_panel_(nullptr)
    , temperature_controller_panel_(nullptr)
    , log_text_edit_(nullptr)
    , log_filter_btn_(nullptr)
    , log_clear_btn_(nullptr)
    , status_label_(nullptr)
    , status_task_progress_bar_(nullptr)
    , status_task_spinner_label_(nullptr)
    , status_task_spinner_timer_(nullptr)
    , recording_status_card_(nullptr)
    , recording_status_title_lbl_(nullptr)
    , recording_status_label_(nullptr)
    , auto_detect_ports_btn_(nullptr)
    , epsilon_port_combo_(nullptr)
    , gnss_port_combo_(nullptr)
    , imu_port_combo_(nullptr)
    , ptb_port_combo_(nullptr)
    , hmp_port_combo_(nullptr)
    , lidar_port_combo_(nullptr)
    , temperature_port_combo_(nullptr)
    , epsilon_baud_combo_(nullptr)
    , gnss_baud_combo_(nullptr)
    , imu_baud_combo_(nullptr)
    , ptb_baud_combo_(nullptr)
    , hmp_baud_combo_(nullptr)
    , lidar_baud_combo_(nullptr)
    , temperature_baud_combo_(nullptr)
    , connect_btn_(nullptr)
    , cancel_connect_btn_(nullptr)
    , disconnect_btn_(nullptr)
    , scheduled_recording_action_(nullptr)
    , start_recording_btn_(nullptr)
    , pause_recording_btn_(nullptr)
    , stop_recording_btn_(nullptr)
    , refresh_ports_btn_(nullptr)
    , lang_action_(nullptr)
    , theme_toggle_action_(nullptr)
    , log_filter_ack_action_(nullptr)
    , log_filter_config_action_(nullptr)
    , log_filter_connection_action_(nullptr)
    , log_filter_recording_action_(nullptr)
    , clear_log_action_(nullptr)
    , session_viewer_action_(nullptr)
#ifdef VAPORVIEW_HAS_OSGEARTH
    , map3d_action_(nullptr)
    , map3d_diagnostics_action_(nullptr)
#endif
    , epsilon_reconfigure_action_(nullptr)
    , epsilon_rtcm_port_action_(nullptr)
    , epsilon_packet_rates_action_(nullptr)
    , recording_directory_action_(nullptr)
    , exit_action_(nullptr)
    , about_action_(nullptr)
    , font_scale_group_(nullptr)
    , font_tiny_action_(nullptr)
    , font_extra_small_action_(nullptr)
    , font_small_action_(nullptr)
    , font_normal_action_(nullptr)
    , font_large_action_(nullptr)
    , font_extra_large_action_(nullptr)
    , data_menu_(nullptr)
    , devices_menu_(nullptr)
    , view_menu_(nullptr)
    , font_menu_(nullptr)
    , language_menu_(nullptr)
    , help_menu_(nullptr)
    , recording_rate_menu_(nullptr)
    , log_filter_menu_(nullptr)
    , title_application_panel_(nullptr)
    , title_application_sub_panel_(nullptr)
    , title_application_nested_panel_(nullptr)
    , app_layout_splitter_(nullptr)
    , main_content_splitter_(nullptr)
    , home_overview_splitter_(nullptr)
    , app_sidebar_(nullptr)
    , app_nav_button_group_(nullptr)
    , home_nav_btn_(nullptr)
    , temperature_nav_btn_(nullptr)
    , rtk_config_nav_btn_(nullptr)
    , device_config_nav_btn_(nullptr)
    , main_page_stack_(nullptr)
    , app_sidebar_mode_(AppSidebarMode::Full)
    , app_sidebar_adjusting_(false)
    , app_sidebar_drag_width_(0)
    , app_sidebar_drag_width_valid_(false)
    , last_app_sidebar_visible_width_(0)
    , custom_logo_hovered_(false)
    , home_page_(nullptr)
    , temperature_page_(nullptr)
    , main_cards_scroll_area_(nullptr)
    , config_group_(nullptr)
    , data_group_(nullptr)
    , sensor_row_widget_(nullptr)
    , sensor_layout_(nullptr)
    , log_side_panel_(nullptr)
    , log_group_(nullptr)
    , tcp_wave_group_(nullptr)
    , epsilon_group_(nullptr)
    , gnss_group_(nullptr)
    , imu_group_(nullptr)
    , ptb_group_(nullptr)
    , hmp_group_(nullptr)
    , env_group_(nullptr)
    , temperature_overview_group_(nullptr)
    , temperature_controller_group_(nullptr)
    , lidar_group_(nullptr)
    , epsilon_lbl_(nullptr)
    , gnss_lbl_(nullptr)
    , imu_lbl_(nullptr)
    , ptb_lbl_(nullptr)
    , hmp_lbl_(nullptr)
    , lidar_lbl_(nullptr)
    , temperature_lbl_(nullptr)
    , home_epsilon_status_lbl_(nullptr)
    , home_ptb_status_lbl_(nullptr)
    , home_hmp_status_lbl_(nullptr)
    , home_lidar_status_lbl_(nullptr)
    , home_temperature_status_lbl_(nullptr)
    , home_wave_status_lbl_(nullptr)
    , home_epsilon_action_btn_(nullptr)
    , home_ptb_action_btn_(nullptr)
    , home_hmp_action_btn_(nullptr)
    , home_lidar_action_btn_(nullptr)
    , home_temperature_action_btn_(nullptr)
    , home_wave_action_btn_(nullptr)
    , data_telemetry_summary_card_(nullptr)
    , data_telemetry_summary_layout_(nullptr)
    , data_telemetry_link_summary_layout_(nullptr)
    , data_telemetry_device_summary_layout_(nullptr)
    , log_inline_title_lbl_(nullptr)
    , epsilon_inline_title_lbl_(nullptr)
    , gnss_inline_title_lbl_(nullptr)
    , imu_inline_title_lbl_(nullptr)
    , env_inline_title_lbl_(nullptr)
    , env_lidar_status_icon_(nullptr)
    , env_ptb_status_icon_(nullptr)
    , env_hmp_status_icon_(nullptr)
    , temperature_overview_inline_title_lbl_(nullptr)
    , temperature_controller_inline_title_lbl_(nullptr)
    , temperature_overview_panel_(nullptr)
    , config_inline_title_lbl_(nullptr)
    , global_rate_lbl_(nullptr)
    , epsilon_rate_lbl_(nullptr)
    , gnss_rate_lbl_(nullptr)
    , imu_rate_lbl_(nullptr)
    , ptb_rate_lbl_(nullptr)
    , hmp_rate_lbl_(nullptr)
    , lidar_rate_lbl_(nullptr)
    , temperature_rate_lbl_(nullptr)
    , data_source_mode_lbl_(nullptr)
    , source_mode_switch_(nullptr)
    , sky_telemetry_transport_lbl_(nullptr)
    , sky_telemetry_port_lbl_(nullptr)
    , sky_telemetry_baud_lbl_(nullptr)
    , sky_telemetry_tcp_host_lbl_(nullptr)
    , sky_telemetry_tcp_port_lbl_(nullptr)
    , sky_telemetry_row_widget_(nullptr)
    , global_rate_combo_(nullptr)
    , epsilon_rate_combo_(nullptr)
    , gnss_rate_combo_(nullptr)
    , imu_rate_combo_(nullptr)
    , ptb_rate_combo_(nullptr)
    , hmp_rate_combo_(nullptr)
    , lidar_rate_combo_(nullptr)
    , temperature_rate_combo_(nullptr)
    , data_source_mode_combo_(nullptr)
    , sky_telemetry_transport_combo_(nullptr)
    , sky_telemetry_port_combo_(nullptr)
    , sky_telemetry_baud_combo_(nullptr)
    , sky_telemetry_tcp_host_edit_(nullptr)
    , sky_telemetry_tcp_port_spin_(nullptr)
    , imu_format_combo_(nullptr)
    , epsilon_packet_rates_btn_(nullptr)
    , sky_device_config_btn_(nullptr)
    , epsilon_remote_connect_btn_(nullptr)
    , epsilon_remote_disconnect_btn_(nullptr)
    , epsilon_remote_reconnect_btn_(nullptr)
    , epsilon_remote_buttons_widget_(nullptr)
    , ptb_remote_connect_btn_(nullptr)
    , ptb_remote_disconnect_btn_(nullptr)
    , ptb_remote_reconnect_btn_(nullptr)
    , ptb_remote_buttons_widget_(nullptr)
    , hmp_remote_connect_btn_(nullptr)
    , hmp_remote_disconnect_btn_(nullptr)
    , hmp_remote_reconnect_btn_(nullptr)
    , hmp_remote_buttons_widget_(nullptr)
    , lidar_remote_connect_btn_(nullptr)
    , lidar_remote_disconnect_btn_(nullptr)
    , lidar_remote_reconnect_btn_(nullptr)
    , lidar_remote_buttons_widget_(nullptr)
    , temperature_remote_connect_btn_(nullptr)
    , temperature_remote_disconnect_btn_(nullptr)
    , temperature_remote_reconnect_btn_(nullptr)
    , temperature_remote_buttons_widget_(nullptr)
    , imu_apply_btn_(nullptr)
    , imu_hi91_btn_(nullptr)
    , imu_hi92_btn_(nullptr)
    , imu_baud_115200_btn_(nullptr)
    , imu_baud_921600_btn_(nullptr)
    , imu_rate_100_btn_(nullptr)
    , imu_rate_200_btn_(nullptr)
    , imu_rate_500_btn_(nullptr)
    , imu_rate_1000_btn_(nullptr)
    , epsilon_collector_(nullptr)
    , gnss_collector_(nullptr)
    , imu_collector_(nullptr)
    , ptb_collector_(nullptr)
    , hmp_collector_(nullptr)
    , lidar_collector_(nullptr)
    , temperature_controller_collector_(nullptr)
    , refresh_timer_(nullptr)
    , scheduled_recording_timer_(nullptr)
    , home_device_action_spinner_timer_(nullptr)
    , is_english_(false)
    , log_filter_ack_enabled_(false)
    , log_filter_config_enabled_(false)
    , log_filter_connection_enabled_(false)
    , log_filter_recording_enabled_(false)
    , language_switch_in_progress_(false)
    , has_inline_progress_log_(false)
    , connection_attempt_in_progress_(false)
    , port_detection_in_progress_(false)
    , epsilon_reconfigure_in_progress_(false)
    , is_connected_(false)
    , compact_home_layout_(false)
    , responsive_home_layout_refresh_pending_(false)
    , log_side_panel_width_initialized_(false)
    , log_side_panel_collapsed_(false)
    , last_log_side_panel_width_(0)
    , remote_sky_mode_(false)
    , remote_sky_online_(false)
    , remote_wave_stream_requested_(false)
    , remote_wave_stream_enable_pending_(false)
    , remote_wave_stream_auto_start_(true)
    , remote_recording_state_(0)
    , remote_last_status_ms_(0)
#ifdef VAPORVIEW_HAS_OSGEARTH
    , map3d_flush_timer_(new QTimer(this))
    , pending_map3d_samples_()
#endif
    , has_last_remote_recording_status_(false)
    , cancel_connection_requested_(false)
    , recording_thread_running_(false)
    , recording_paused_(false)
    , font_scale_percent_(100)
    , dark_theme_enabled_(false)
    , base_font_point_size_(0.0)
    , base_window_size_(kDefaultMainWindowWidth, kDefaultMainWindowHeight)
    , base_minimum_window_size_(kMinimumMainWindowWidth, kMinimumMainWindowHeight)
    , normal_window_geometry_()
    , epsilon_sample_rate_(kDefaultEpsilonSampleRateHz)
    , gnss_sample_rate_(1)
    , imu_sample_rate_(200)
    , ptb_sample_rate_(kDefaultPtbSampleRateHz)
    , hmp_sample_rate_(kDefaultHmpSampleRateHz)
    , lidar_sample_rate_(kDefaultLidarSampleRateHz)
    , temperature_sample_rate_(kDefaultTemperatureSampleRateHz)
    , recording_export_rate_hz_(20)
    , imu_recording_rate_hz_(0)
    , waveform_recording_rate_hz_(0)
    , status_task_spinner_index_(0)
    , home_device_action_spinner_step_(0)
    , scheduled_recording_mode_(ScheduledRecordingMode::None)
    , scheduled_recording_phase_(ScheduledRecordingPhase::Idle)
    , scheduled_recording_duration_seconds_(10 * 60)
    , scheduled_recording_interval_seconds_(60 * 60)
    , scheduled_recording_fixed_count_enabled_(false)
    , scheduled_recording_total_runs_(1)
    , scheduled_recording_completed_runs_(0)
    , scheduled_recording_next_start_()
    , scheduled_recording_stop_time_()
    , scheduled_recording_round_observed_session_(false)
    , steady_clock_anchor_(std::chrono::steady_clock::now())
    , system_clock_anchor_(std::chrono::system_clock::now())
    , sensors_file_(nullptr)
    , raw_epsilon_file_(nullptr)
    , raw_ptb_file_(nullptr)
    , raw_hmp_file_(nullptr)
    , raw_lidar_file_(nullptr)
    , raw_tcp_wave_file_(nullptr)
    , raw_tcp_wave_peak_index_file_(nullptr)
    , event_log_file_(nullptr)
    , error_log_file_(nullptr)
    , recording_directory_()
    , session_directory_()
    , session_name_()
    , session_start_time_utc_()
    , session_start_time_us_(0)
    , sensors_filename_()
    , raw_epsilon_filename_()
    , raw_ptb_filename_()
    , raw_hmp_filename_()
    , raw_lidar_filename_()
    , raw_tcp_wave_filename_()
    , raw_tcp_wave_peak_index_filename_()
    , raw_dat_doc_filename_()
    , session_metadata_filename_()
    , event_log_filename_()
    , error_log_filename_()
    , device_config_filename_()
    , last_recording_session_name_()
    , last_recording_entry_count_(0)
    , last_recording_waveform_frame_count_(0)
    , last_raw_epsilon_record_count_(0)
    , last_raw_ptb_record_count_(0)
    , last_raw_hmp_record_count_(0)
    , last_raw_lidar_record_count_(0)
    , last_raw_tcp_wave_record_count_(0)
    , last_tcp_recording_status_update_ms_(0)
    , recording_entry_count_(0)
    , waveform_frame_count_(0)
    , waveform_file_count_(0)
    , raw_epsilon_record_count_(0)
    , raw_ptb_record_count_(0)
    , raw_hmp_record_count_(0)
    , raw_lidar_record_count_(0)
    , raw_tcp_wave_record_count_(0)
    , last_imu_record_timestamp_us_(0)
    , tcp_raw_recording_worker_running_(false)
    , tcp_raw_record_queue_bytes_(0)
    , tcp_raw_record_dropped_count_(0)
    , last_tcp_raw_queue_warning_ms_(0)
    , rtk_config_action_(nullptr)
    , rtk_config_dialog_(nullptr)
    , rtk_service_running_(false)
    , tcp_wave_panel_(nullptr)
    , session_viewer_window_(nullptr)
#ifdef VAPORVIEW_HAS_OSGEARTH
    , map3d_window_(nullptr)
#endif
    , ground_telemetry_service_(nullptr)
    , sky_device_config_dialog_(nullptr)
{
    setWindowFlags(Qt::Window |
                   Qt::FramelessWindowHint |
                   Qt::WindowMinimizeButtonHint |
                   Qt::WindowMaximizeButtonHint |
                   Qt::WindowCloseButtonHint);
    setProperty(kMainWindowProperty, true);

#ifdef VAPORVIEW_HAS_OSGEARTH
    map3d_flush_timer_->setInterval(50);
    map3d_flush_timer_->setTimerType(Qt::CoarseTimer);
    connect(map3d_flush_timer_, &QTimer::timeout, this, &MainWindow::flushMap3DSamples);
#endif

    const double currentPointSize = qApp->font().pointSizeF();
    base_font_point_size_ = currentPointSize > 0.0 ? currentPointSize : 10.0;

    QSettings settings("VaporView", "MainWindow");
    font_scale_percent_ = settings.value(
        "font_scale_percent",
        VaporView::defaultFontScalePercentForScreen(this)).toInt();
    if (font_scale_percent_ < 70 || font_scale_percent_ > 150)
    {
        font_scale_percent_ = 100;
    }
    dark_theme_enabled_ = settings.value("dark_theme_enabled", false).toBool();
    if (qApp)
    {
        qApp->setProperty(kAppDarkThemeProperty, dark_theme_enabled_);
    }
    recording_directory_ = settings.value("recording_directory", defaultRecordingDirectory()).toString();
    if (recording_directory_.isEmpty())
    {
        recording_directory_ = defaultRecordingDirectory();
    }
    recording_export_rate_hz_ = settings.value("recording_export_rate_hz", 20).toInt();
    if (recording_export_rate_hz_ < 1 || recording_export_rate_hz_ > 200)
    {
        recording_export_rate_hz_ = 20;
    }
    imu_recording_rate_hz_ = settings.value("imu_recording_rate_hz", 0).toInt();
    if (imu_recording_rate_hz_ < 0 || imu_recording_rate_hz_ > 1000)
    {
        imu_recording_rate_hz_ = 0;
    }
    waveform_recording_rate_hz_ = 0;

    loadModernStyleSheet();
    
    setupMenuBar();
    setupToolBar();
    setupStatusBar();
    setupCentralWidget();
    setupWindowBorderFrames();
    setupWindowResizeHandles();
    ground_telemetry_service_ = new VaporView::GroundTelemetryService(this);
    auto currentRemoteEvent = [this](quint64 generation) {
        return isRemoteSkyMode() &&
               ground_telemetry_service_ &&
               ground_telemetry_service_->linkGeneration() == generation;
    };
    auto currentOpenRemoteEvent = [this, currentRemoteEvent](quint64 generation) {
        return currentRemoteEvent(generation) &&
               ground_telemetry_service_ &&
               ground_telemetry_service_->isOpen();
    };
    auto dispatchRemoteUi = [this](auto handler) {
        if (QThread::currentThread() == thread())
        {
            handler();
            return;
        }
        QMetaObject::invokeMethod(this, std::move(handler), Qt::QueuedConnection);
    };
    connect(ground_telemetry_service_, &VaporView::GroundTelemetryService::linkOpenChanged,
            this, [this, currentRemoteEvent, dispatchRemoteUi](bool open) {
                const quint64 generation = ground_telemetry_service_->linkGeneration();
                dispatchRemoteUi([this, currentRemoteEvent, generation, open]() {
                    if (!currentRemoteEvent(generation))
                    {
                        return;
                    }
                    onRemoteLinkOpenChanged(open);
                });
            },
            Qt::DirectConnection);
    connect(ground_telemetry_service_, &VaporView::GroundTelemetryService::logMessage,
            this, [this](const QString& message) { log(message); },
            Qt::QueuedConnection);
    connect(ground_telemetry_service_, &VaporView::GroundTelemetryService::basicTelemetryUpdated,
            this, [this, currentOpenRemoteEvent, dispatchRemoteUi](const VaporView::TelemetryBasic& telemetry) {
                const quint64 generation = ground_telemetry_service_->linkGeneration();
                dispatchRemoteUi([this, currentOpenRemoteEvent, generation, telemetry]() {
                    if (!currentOpenRemoteEvent(generation))
                    {
                        return;
                    }
                    onRemoteBasicTelemetryUpdated(telemetry);
                });
            },
            Qt::DirectConnection);
    connect(ground_telemetry_service_, &VaporView::GroundTelemetryService::waveformUpdated,
            this, [this, currentOpenRemoteEvent, dispatchRemoteUi](const VaporView::DownsampledWaveform& waveform) {
                const quint64 generation = ground_telemetry_service_->linkGeneration();
                dispatchRemoteUi([this, currentOpenRemoteEvent, generation, waveform]() {
                    if (!currentOpenRemoteEvent(generation))
                    {
                        return;
                    }
                    onRemoteWaveformUpdated(waveform);
                });
            },
            Qt::DirectConnection);
    connect(ground_telemetry_service_, &VaporView::GroundTelemetryService::waveformFeatureUpdated,
            this, [this, currentOpenRemoteEvent, dispatchRemoteUi](const VaporView::WaveformFeature& feature) {
                const quint64 generation = ground_telemetry_service_->linkGeneration();
                dispatchRemoteUi([this, currentOpenRemoteEvent, generation, feature]() {
                    if (!currentOpenRemoteEvent(generation))
                    {
                        return;
                    }
                    onRemoteWaveformFeatureUpdated(feature);
                });
            },
            Qt::DirectConnection);
    connect(ground_telemetry_service_, &VaporView::GroundTelemetryService::statusUpdated,
            this, [this, currentOpenRemoteEvent, dispatchRemoteUi](const VaporView::TelemetryStatus& status) {
                const quint64 generation = ground_telemetry_service_->linkGeneration();
                dispatchRemoteUi([this, currentOpenRemoteEvent, generation, status]() {
                    if (!currentOpenRemoteEvent(generation))
                    {
                        return;
                    }
                    onRemoteTelemetryStatusUpdated(status);
                });
            },
            Qt::DirectConnection);
    connect(ground_telemetry_service_, &VaporView::GroundTelemetryService::temperatureControllerStatusUpdated,
            this, [this, currentOpenRemoteEvent, dispatchRemoteUi](const VaporView::TemperatureControllerData& controllerData) {
                const quint64 generation = ground_telemetry_service_->linkGeneration();
                dispatchRemoteUi([this, currentOpenRemoteEvent, generation, controllerData]() {
                    if (!currentOpenRemoteEvent(generation))
                    {
                        return;
                    }
                    onRemoteTemperatureControllerStatusUpdated(controllerData);
                });
            },
            Qt::DirectConnection);
    connect(ground_telemetry_service_, &VaporView::GroundTelemetryService::commandAckReceived,
            this, [this, currentOpenRemoteEvent, dispatchRemoteUi](const VaporView::CommandAck& ack) {
                const quint64 generation = ground_telemetry_service_->linkGeneration();
                dispatchRemoteUi([this, currentOpenRemoteEvent, generation, ack]() {
                    if (!currentOpenRemoteEvent(generation))
                    {
                        return;
                    }
                    onRemoteCommandAckReceived(ack);
                });
            },
            Qt::DirectConnection);
    connect(ground_telemetry_service_, &VaporView::GroundTelemetryService::commandTimedOut,
            this, [this, currentRemoteEvent, dispatchRemoteUi](VaporView::CommandId commandId, quint16 commandSeq) {
                const quint64 generation = ground_telemetry_service_->linkGeneration();
                dispatchRemoteUi([this, currentRemoteEvent, generation, commandId, commandSeq]() {
                    if (!currentRemoteEvent(generation))
                    {
                        return;
                    }
                    if (isRemoteSkyMode() && commandId == VaporView::CommandId::RequestStatus && !remote_sky_online_ && status_label_)
                    {
                        status_label_->setText(is_english_ ? "Sky handshake timed out" : "天空端握手超时");
                        status_label_->setProperty("status", "disconnected");
                        status_label_->style()->unpolish(status_label_);
                        status_label_->style()->polish(status_label_);
                    }
                    if (commandId == VaporView::CommandId::SetPeakSearchRange)
                    {
                        remote_peak_search_commands_.remove(commandSeq);
                        if (tcp_wave_panel_)
                        {
                            tcp_wave_panel_->rejectRemotePeakSearchRange(is_english_ ? QStringLiteral("ACK timed out") : QStringLiteral("ACK 超时"));
                        }
                    }
                    else if (isTemperatureCommand(commandId))
                    {
                        const VaporView::TemperatureControllerCommand request = remote_temperature_commands_.take(commandSeq);
                        const quint8 channel = request.channel == 0 ? 1 : request.channel;
                        if (temperature_controller_panel_)
                        {
                            temperature_controller_panel_->clearCommandPending(commandId, channel);
                            temperature_controller_panel_->setCommandStatus(
                                temperatureCommandStatusText(commandId,
                                                             channel,
                                                             false,
                                                             is_english_ ? QStringLiteral("ACK timed out") : QStringLiteral("ACK 超时")),
                                true);
                        }
                        restoreTemperatureCommandUi(commandId, channel);
                    }
                    else if (commandId == VaporView::CommandId::EnableWaveformStreaming ||
                             commandId == VaporView::CommandId::DisableWaveformStreaming)
                    {
                        remote_wave_stream_enable_pending_ = false;
                        updateRemoteDeviceButtonText(VaporView::SkyDeviceId::WaveTcp,
                                                     remote_device_states_.value(VaporView::SkyDeviceId::WaveTcp,
                                                                                 VaporView::DeviceState::Disconnected));
                    }
                });
            },
            Qt::DirectConnection);
    loadRememberedInputState();
    bindRememberedInputState();

    base_minimum_window_size_ = QSize(kMinimumMainWindowWidth, kMinimumMainWindowHeight);
    base_window_size_ = QSize(kDefaultMainWindowWidth, kDefaultMainWindowHeight);
    resize(base_window_size_);
    setMinimumSize(base_minimum_window_size_);

    refresh_timer_ = new QTimer(this);
    connect(refresh_timer_, &QTimer::timeout, this, &MainWindow::onRefreshTimer);
    refresh_timer_->start(100);

    scheduled_recording_timer_ = new QTimer(this);
    scheduled_recording_timer_->setInterval(1000);
    scheduled_recording_timer_->setTimerType(Qt::CoarseTimer);
    connect(scheduled_recording_timer_, &QTimer::timeout, this, &MainWindow::onScheduledRecordingTick);

    setEnglish(false);
    applyStyleConfiguration();
    VaporView::centerWindowOnScreen(this);
    rememberNormalWindowGeometry();
    QTimer::singleShot(0, this, [this]() {
        setLogSidePanelToMinimumWidth();
        queueResponsiveHomeLayoutRefresh();
    });

    updateRecordingStatusLabel();
    updateConnectionStatus(false);
    updateSourceModeUi();
    qApp->installEventFilter(this);
}

MainWindow::~MainWindow()
{
    qApp->removeEventFilter(this);
    saveAppSidebarWidth();

    if (sky_device_config_dialog_)
    {
        delete sky_device_config_dialog_;
        sky_device_config_dialog_ = nullptr;
    }
    if (rtk_config_dialog_)
    {
        delete rtk_config_dialog_;
        rtk_config_dialog_ = nullptr;
    }
    if (session_viewer_window_)
    {
        delete session_viewer_window_;
        session_viewer_window_ = nullptr;
    }
#ifdef VAPORVIEW_HAS_OSGEARTH
    pending_map3d_samples_.clear();
    if (map3d_flush_timer_)
    {
        map3d_flush_timer_->stop();
    }
    if (map3d_window_)
    {
        delete map3d_window_;
        map3d_window_ = nullptr;
    }
#endif

    if (custom_title_bar_)
    {
        const auto buttons = custom_title_bar_->findChildren<QToolButton *>();
        for (QToolButton *button : buttons)
        {
            if (button)
            {
                button->setDefaultAction(nullptr);
                button->setMenu(nullptr);
            }
        }
        custom_title_bar_->removeEventFilter(this);
        if (custom_title_label_)
        {
            custom_title_label_->removeEventFilter(this);
        }
        if (custom_logo_label_)
        {
            custom_logo_label_->removeEventFilter(this);
        }
    }

    saveRememberedInputState();
    cancel_connection_requested_.store(true);
    if (port_detection_thread_.joinable())
    {
        port_detection_thread_.join();
    }
    if (connection_thread_.joinable())
    {
        connection_thread_.join();
    }
    if (epsilon_reconfigure_thread_.joinable())
    {
        epsilon_reconfigure_thread_.join();
    }
    stopRecording(false);
    stopAllCollectors();
}

bool MainWindow::shouldStartWindowMove(QObject *watched) const
{
    return watched == custom_title_bar_ ||
           watched == custom_title_label_;
}

bool MainWindow::belongsToMainWindow(QWidget *widget) const
{
    for (QWidget *current = widget; current; current = current->parentWidget())
    {
        if (current == this)
        {
            return true;
        }
    }
    return false;
}

void MainWindow::syncMainHoverStateFromCursor()
{
    const QPoint cursorPos = QCursor::pos();
    setCustomLogoHovered(widgetContainsGlobalCursor(custom_logo_label_, cursorPos));

    const QList<QToolButton*> titleButtons = findChildren<QToolButton *>();
    for (QToolButton *button : titleButtons)
    {
        if (!button || !button->property(kTitleBarHoverParticipantProperty).toBool())
        {
            continue;
        }
        setWidgetBooleanProperty(button,
                                 kTitleBarHoverProperty,
                                 widgetContainsGlobalCursor(button, cursorPos));
    }

    const QList<QPushButton*> sidebarButtons = findChildren<QPushButton *>(QStringLiteral("appSidebarButton"));
    for (QPushButton *button : sidebarButtons)
    {
        if (!button || !button->property(kSidebarHoverParticipantProperty).toBool())
        {
            continue;
        }
        setWidgetBooleanProperty(button,
                                 kSidebarHoverProperty,
                                 widgetContainsGlobalCursor(button, cursorPos));
    }
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::ToolTip && showAppTooltip(watched, event, dark_theme_enabled_))
    {
        return true;
    }

    const QEvent::Type eventType = event->type();
    if (eventType == QEvent::ApplicationActivate ||
        eventType == QEvent::WindowActivate ||
        eventType == QEvent::ActivationChange)
    {
        QTimer::singleShot(0, this, &MainWindow::syncMainHoverStateFromCursor);
    }

    if (auto *hoverWidget = qobject_cast<QWidget *>(watched))
    {
        const bool titleBarHoverParticipant =
            hoverWidget->property(kTitleBarHoverParticipantProperty).toBool();
        const bool sidebarHoverParticipant =
            hoverWidget->property(kSidebarHoverParticipantProperty).toBool();
        if (titleBarHoverParticipant || sidebarHoverParticipant)
        {
            const char *hoverProperty = titleBarHoverParticipant
                ? kTitleBarHoverProperty
                : kSidebarHoverProperty;
            if (isHoverEnterLikeEvent(eventType))
            {
                setWidgetBooleanProperty(hoverWidget, hoverProperty, true);
            }
            else if (isHoverLeaveLikeEvent(eventType))
            {
                setWidgetBooleanProperty(hoverWidget, hoverProperty, false);
            }
        }

        const bool hoverSyncAnchor = hoverWidget == this ||
                                     hoverWidget == custom_title_bar_ ||
                                     hoverWidget == app_sidebar_;
        if ((eventType == QEvent::Enter || eventType == QEvent::MouseMove) &&
            hoverSyncAnchor &&
            belongsToMainWindow(hoverWidget))
        {
            QTimer::singleShot(0, this, &MainWindow::syncMainHoverStateFromCursor);
        }
    }

    if (eventType == QEvent::Leave ||
        eventType == QEvent::MouseButtonPress ||
        eventType == QEvent::Wheel ||
        eventType == QEvent::KeyPress ||
        eventType == QEvent::ApplicationDeactivate ||
        eventType == QEvent::WindowDeactivate)
    {
        hideAppTooltipPopup();
    }

    const bool titleMenuVisible =
        (title_application_panel_ && title_application_panel_->isVisible()) ||
        (title_application_sub_panel_ && title_application_sub_panel_->isVisible()) ||
        (title_application_nested_panel_ && title_application_nested_panel_->isVisible());
    if (titleMenuVisible)
    {
        if (eventType == QEvent::ApplicationDeactivate ||
            eventType == QEvent::WindowDeactivate)
        {
            if (title_application_panel_)
            {
                title_application_panel_->hide();
            }
            if (title_application_sub_panel_)
            {
                title_application_sub_panel_->hide();
            }
            if (title_application_nested_panel_)
            {
                title_application_nested_panel_->hide();
            }
        }
        else if (eventType == QEvent::MouseButtonPress)
        {
            auto *mouseEvent = static_cast<QMouseEvent *>(event);
            const QPoint globalPos = mouseEvent->globalPosition().toPoint();
            auto containsGlobalPoint = [globalPos](const QWidget *widget) {
                return widget &&
                       widget->isVisible() &&
                       QRect(widget->mapToGlobal(QPoint(0, 0)), widget->size()).contains(globalPos);
            };

            const bool insideMenu =
                containsGlobalPoint(title_application_panel_) ||
                containsGlobalPoint(title_application_sub_panel_) ||
                containsGlobalPoint(title_application_nested_panel_);
            const bool insideMenuButton = containsGlobalPoint(title_menu_btn_);

            if (!insideMenu && !insideMenuButton)
            {
                if (title_application_panel_)
                {
                    title_application_panel_->hide();
                }
                if (title_application_sub_panel_)
                {
                    title_application_sub_panel_->hide();
                }
                if (title_application_nested_panel_)
                {
                    title_application_nested_panel_->hide();
                }
            }
        }
    }

    if (watched == custom_logo_label_)
    {
        if (eventType == QEvent::Enter || eventType == QEvent::HoverEnter)
        {
            setCustomLogoHovered(true);
        }
        else if (eventType == QEvent::Leave || eventType == QEvent::HoverLeave)
        {
            setCustomLogoHovered(false);
        }
        else if (eventType == QEvent::MouseButtonPress)
        {
            auto *mouseEvent = static_cast<QMouseEvent *>(event);
            if (mouseEvent->button() == Qt::LeftButton)
            {
                toggleAppSidebarFromLogo();
                return true;
            }
        }
        else if (eventType == QEvent::MouseButtonDblClick)
        {
            auto *mouseEvent = static_cast<QMouseEvent *>(event);
            if (mouseEvent->button() == Qt::LeftButton)
            {
                return true;
            }
        }
        else if (eventType == QEvent::KeyPress)
        {
            auto *keyEvent = static_cast<QKeyEvent *>(event);
            if (keyEvent->key() == Qt::Key_Return ||
                keyEvent->key() == Qt::Key_Enter ||
                keyEvent->key() == Qt::Key_Space)
            {
                toggleAppSidebarFromLogo();
                return true;
            }
        }
    }

    if (shouldStartWindowMove(watched))
    {
        if (eventType == QEvent::MouseButtonDblClick)
        {
            auto *mouseEvent = static_cast<QMouseEvent *>(event);
            if (mouseEvent->button() == Qt::LeftButton && !isFullScreen())
            {
                toggleWindowMaximized();
                return true;
            }
        }
        else if (eventType == QEvent::MouseButtonPress)
        {
            auto *mouseEvent = static_cast<QMouseEvent *>(event);
            if (mouseEvent->button() == Qt::LeftButton && windowHandle())
            {
                windowHandle()->startSystemMove();
                return true;
            }
        }
    }

    if (app_layout_splitter_ &&
        watched == app_layout_splitter_->handle(1) &&
        eventType == QEvent::MouseButtonRelease)
    {
        QTimer::singleShot(0, this, &MainWindow::finishAppSidebarResize);
    }

    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::changeEvent(QEvent *event)
{
    QMainWindow::changeEvent(event);
    if (event->type() == QEvent::ActivationChange)
    {
        QTimer::singleShot(0, this, &MainWindow::syncMainHoverStateFromCursor);
    }
    if (event->type() == QEvent::WindowStateChange)
    {
        if (!isWindowMaximizedForUi())
        {
            rememberNormalWindowGeometry();
        }
        updateWindowControlButtons();
        updateWindowBorderFrames();
        updateWindowResizeHandles();
        queueResponsiveHomeLayoutRefresh();
    }
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    if (!isWindowMaximizedForUi())
    {
        rememberNormalWindowGeometry();
    }
    updateWindowControlButtons();
    updateWindowBorderFrames();
    updateWindowResizeHandles();
    updateResponsiveHomeLayout();
    queueResponsiveHomeLayoutRefresh();
}

#ifdef Q_OS_WIN
bool MainWindow::nativeEvent(const QByteArray& eventType, void *message, qintptr *result)
{
    Q_UNUSED(eventType);

    auto *msg = static_cast<MSG *>(message);
    if (!msg || msg->message != WM_NCHITTEST || isFullScreen() || isWindowMaximizedForUi())
    {
        return QMainWindow::nativeEvent(eventType, message, result);
    }

    const int x = GET_X_LPARAM(msg->lParam);
    const int y = GET_Y_LPARAM(msg->lParam);
    const QPoint pos = mapFromGlobal(QPoint(x, y));
    const int borderWidth = scalePixels(8);
    const bool inWindowX = pos.x() >= 0 && pos.x() < width();
    const bool inWindowY = pos.y() >= 0 && pos.y() < height();
    const bool onLeft = inWindowY && pos.x() >= 0 && pos.x() < borderWidth;
    const bool onRight = inWindowY && pos.x() < width() && pos.x() >= width() - borderWidth;
    const bool onTop = inWindowX && pos.y() >= 0 && pos.y() < borderWidth;
    const bool onBottom = inWindowX && pos.y() < height() && pos.y() >= height() - borderWidth;

    if (onTop && onLeft)
    {
        *result = HTTOPLEFT;
        return true;
    }
    if (onTop && onRight)
    {
        *result = HTTOPRIGHT;
        return true;
    }
    if (onBottom && onLeft)
    {
        *result = HTBOTTOMLEFT;
        return true;
    }
    if (onBottom && onRight)
    {
        *result = HTBOTTOMRIGHT;
        return true;
    }
    if (onTop)
    {
        *result = HTTOP;
        return true;
    }
    if (onBottom)
    {
        *result = HTBOTTOM;
        return true;
    }
    if (onLeft)
    {
        *result = HTLEFT;
        return true;
    }
    if (onRight)
    {
        *result = HTRIGHT;
        return true;
    }

    return QMainWindow::nativeEvent(eventType, message, result);
}
#endif

void MainWindow::loadModernStyleSheet()
{
    const QDir appDir(QCoreApplication::applicationDirPath());
    const QStringList styleCandidates = {
        appDir.filePath("resources/modern_style.qss"),
        appDir.filePath("../resources/modern_style.qss"),
        appDir.filePath("../../resources/modern_style.qss"),
    };

    QString stylePath;
    QFile styleFile;
    for (const QString& candidate : styleCandidates)
    {
        styleFile.setFileName(QDir::cleanPath(candidate));
        if (styleFile.open(QFile::ReadOnly | QFile::Text))
        {
            stylePath = styleFile.fileName();
            break;
        }
    }
    
    if (styleFile.isOpen())
    {
        base_style_sheet_ = QString::fromUtf8(styleFile.readAll());
        styleFile.close();

        const QFileInfo styleInfo(stylePath);
        const QString resourceDir = styleInfo.absolutePath();
        const QString comboArrowPath = QDir(resourceDir).absoluteFilePath("combo_arrow_down.xpm").replace('\\', '/');
        const QString comboArrowUpPath = QDir(resourceDir).absoluteFilePath("combo_arrow_up.xpm").replace('\\', '/');
        const QString squareIconPath = QDir(resourceDir).absoluteFilePath("lucide/square.svg").replace('\\', '/');
        const QString squareCheckIconPath = QDir(resourceDir).absoluteFilePath("lucide/square-check-big.svg").replace('\\', '/');
        base_style_sheet_.replace("url(combo_arrow_down.xpm)", QString("url(%1)").arg(comboArrowPath));
        base_style_sheet_.replace("url(combo_arrow_up.xpm)", QString("url(%1)").arg(comboArrowUpPath));
        base_style_sheet_.replace("url(lucide/square.svg)", QString("url(%1)").arg(squareIconPath));
        base_style_sheet_.replace("url(lucide/square-check-big.svg)", QString("url(%1)").arg(squareCheckIconPath));
    }
    else
    {
        base_style_sheet_ =
            "* { font-family: \"Segoe UI\", \"Microsoft YaHei\", \"PingFang SC\", sans-serif; }"
            "QMainWindow { background-color: @vv-surface; }"
            "QWidget#appCentralWidget, QWidget#mainCardsPane, QFrame#appSidebar, QStackedWidget#mainPageStack, QWidget#temperaturePage, QWidget#deviceConfigPage, QWidget#logSidePanel, QMainWindow#sessionViewerWindow, QWidget#sessionViewerCentralWidget, QWidget#sessionViewerViewport, QWidget#sessionViewerContentPane, QScrollArea#mainCardsScrollArea, QScrollArea#sessionViewerScrollArea, QWidget#mainCardsViewport, QScrollArea#mainCardsScrollArea > QWidget, QScrollArea#mainCardsScrollArea > QWidget > QWidget, QScrollArea#sessionViewerScrollArea > QWidget, QScrollArea#sessionViewerScrollArea > QWidget > QWidget, QSplitter#appLayoutSplitter, QSplitter#mainContentSplitter, QSplitter#homeOverviewSplitter, QSplitter#homeOverviewSplitter > QWidget, QSplitter#sessionViewerContentSplitter { background-color: @vv-surface; }"
            "QFrame#appSidebar { background-color: @vv-surface; border-right: 1px solid @vv-border; }"
            "QPushButton#appSidebarButton { background-color: transparent; border: 1px solid transparent; border-radius: 6px; color: @vv-text; font-weight: 600; min-height: 34px; max-height: 34px; padding: 6px 8px; text-align: left; outline: none; }"
            "QPushButton#appSidebarButton:focus { outline: none; }"
            "QPushButton#appSidebarButton[_vv_sidebar_compact=\"true\"] { min-width: 42px; max-width: 42px; min-height: 42px; max-height: 42px; padding: 0px; text-align: center; outline: none; }"
            "QPushButton#appSidebarButton:hover, QPushButton#appSidebarButton[_vv_hover=\"true\"] { background-color: @vv-primary-subtle; color: @vv-primary; }"
            "QPushButton#appSidebarButton:checked { background-color: @vv-primary; border-color: @vv-primary; color: @vv-white; }"
            "QPushButton#dangerButton { background-color: @vv-danger; border: 1px solid @vv-danger; border-radius: 6px; color: @vv-white; font-weight: 700; padding: 6px 14px; }"
            "QMenuBar { background-color: @vv-surface; border-bottom: 1px solid @vv-border; padding: 4px 8px; }"
            "QMenuBar::item { background-color: transparent; padding: 6px 12px; border-radius: 4px; color: @vv-text; }"
            "QMenuBar::item:selected { background-color: @vv-primary-subtle; color: @vv-primary; }"
            "QMenu { background-color: @vv-menu-panel; border: 1px solid @vv-border; border-radius: 10px; color: @vv-menu-text; padding: 12px 0px; }"
            "QMenu::item { background-color: transparent; border: none; border-radius: 0px; color: @vv-menu-text; padding: 8px 32px 8px 16px; }"
            "QMenu::item:selected { background-color: @vv-menu-hover; color: @vv-menu-text; }"
            "QMenu::item:disabled { background-color: transparent; color: @vv-menu-disabled; }"
            "QToolBar { background-color: @vv-surface; border-bottom: 1px solid @vv-border; padding: 8px 12px; spacing: 8px; }"
            "QToolBar QToolButton { background-color: transparent; border: none; border-radius: 6px; padding: 10px 14px; color: @vv-text; font-size: 15px; }"
            "QToolBar QToolButton:hover { background-color: @vv-surface-alt; }"
            "QToolBar QToolButton:disabled { color: @vv-text; }"
            "QStatusBar { background-color: @vv-surface; border-top: 1px solid @vv-border; padding: 4px 12px; color: @vv-text; font-size: 14px; }"
            "QGroupBox { background-color: @vv-surface; border: 1px solid @vv-border; border-top: 40px solid @vv-surface; border-radius: 8px; margin-top: 0px; padding: 8px 8px 8px 8px; font-size: 15px; font-weight: bold; color: @vv-text; }"
            "QGroupBox#sensorGroupBox { margin-top: 0px; background-color: @vv-surface; border: 1px solid @vv-border; border-radius: 8px; padding: 0px 0px 0px 0px; }"
            "QGroupBox#sensorRowContainer { margin-top: 0px; background-color: transparent; border: none; border-radius: 0px; padding: 0px 0px 0px 0px; }"
            "QFrame#logPanelFrame { background-color: @vv-surface; border: 1px solid @vv-border; border-radius: 8px; }"
            "QFrame#recordingStatusCard { background-color: @vv-surface; border: 1px solid @vv-border; border-radius: 8px; }"
            "QFrame#recordingStatusCard QWidget#sectionTitleBar { background-color: @vv-surface; border: none; border-bottom: 1px solid @vv-border; border-top-left-radius: 7px; border-top-right-radius: 7px; }"
            "QFrame#recordingStatusCard QLabel#sectionTitleLabel { background-color: transparent; border: none; }"
            "QWidget#recordingStatusBody { background-color: @vv-surface; border: none; border-bottom-left-radius: 7px; border-bottom-right-radius: 7px; }"
            "QLabel#recordingStatusLabel { background-color: transparent; border: none; color: @vv-text; font-size: 14px; font-weight: 600; }"
            "QGroupBox::title { subcontrol-origin: border; subcontrol-position: top left; left: 12px; top: -30px; padding: 0px 2px; background-color: transparent; border: none; border-radius: 0px; color: @vv-text; }"
            "QDialog#rtkConfigDialog, QWidget#rtkConfigViewport, QWidget#rtkConfigContent, QScrollArea#rtkConfigScrollArea { background-color: @vv-surface; }"
            "QDialog#rtkConfigDialog QGroupBox#sensorGroupBox { background-color: @vv-surface; border: 1px solid @vv-border; border-top: 1px solid @vv-border; border-radius: 8px; margin-top: 0px; padding: 0px; color: @vv-text; }"
            "QDialog#rtkConfigDialog QGroupBox#sensorGroupBox::title { color: transparent; height: 0px; margin: 0px; padding: 0px; }"
            "QDialog#rtkConfigDialog QGroupBox#rtkCardGroup { background-color: @vv-surface; border: 1px solid @vv-border; border-radius: 8px; margin-top: 0px; padding: 0px; color: @vv-text; }"
            "QDialog#rtkConfigDialog QGroupBox#rtkCardGroup::title { color: transparent; }"
            "QDialog#rtkConfigDialog QWidget#sectionTitleBar { background-color: @vv-surface; border: none; border-bottom: 1px solid @vv-border; border-top-left-radius: 7px; border-top-right-radius: 7px; }"
            "QDialog#rtkConfigDialog QLabel#sectionTitleLabel { background-color: transparent; border: none; color: @vv-text; margin: 0px; padding: 0px; }"
            "QWidget#sectionTitleBar { background-color: @vv-surface; border-bottom: 1px solid @vv-border; border-top-left-radius: 7px; border-top-right-radius: 7px; min-height: 40px; max-height: 40px; }"
            "QWidget#sectionTitleBar QLabel { background-color: transparent; border: none; }"
            "QLabel { color: @vv-text; background-color: transparent; border: none; }"
            "QLabel#rateLabel { color: @vv-text; font-size: 13px; font-weight: bold; font-family: \"Cascadia Mono\", \"Consolas\", \"Courier New\", monospace; margin: 0px; padding: 0px; }"
            "QLabel#fieldLabel { color: @vv-text; font-size: 14px; font-weight: 600; }"
            "QLabel#separatorLabel { color: @vv-text; font-size: 14px; font-weight: bold; }"
            "QLabel#rtkStatusLabel { color: @vv-text; font-weight: bold; }"
            "QWidget#environmentSectionTitleBar { background-color: @vv-surface; border-bottom: 1px solid @vv-border; border-top-left-radius: 7px; border-top-right-radius: 7px; min-height: 36px; max-height: 36px; }"
            "QWidget#environmentSectionTitleBar QLabel { background-color: transparent; border: none; }"
            "QWidget#environmentSectionTitleBar QLabel#sectionTitleLabel { background-color: transparent; border: none; margin: 0px; padding: 0px; }"
            "QLabel#sectionTitleLabel { background-color: @vv-surface; border: none; border-bottom: 1px solid @vv-border; border-radius: 0px; color: @vv-text; font-size: 16px; font-weight: bold; margin: 0px; padding: 0px; }"
            "QWidget#sectionTitleBar QLabel#sectionTitleLabel { background-color: transparent; border: none; margin: 0px; padding: 0px; }"
            "QWidget#sectionTitleCluster { background-color: transparent; border: none; }"
            "QWidget#sectionTitleCluster QLabel#sectionTitleIcon { background-color: transparent; border: none; padding: 0px; margin: 0px; }"
            "QWidget#sectionTitleBar QWidget#sectionTitleCluster QLabel#sectionTitleLabel, QWidget#environmentSectionTitleBar QWidget#sectionTitleCluster QLabel#sectionTitleLabel { margin: 0px; padding: 0px; }"
            "QFrame#epsilonSectionCard { background-color: @vv-surface; border: 1px solid @vv-border; border-radius: 8px; }"
            "QWidget#homeTelemetrySummaryContainer { background-color: transparent; border: none; }"
            "QFrame#homeTelemetrySectionCard { background-color: @vv-surface; border: 1px solid @vv-border; border-radius: 6px; }"
            "QFrame#deviceTelemetrySectionTitlePane { background-color: @vv-surface-alt; border: none; border-right: 1px solid @vv-border; border-top-left-radius: 6px; border-bottom-left-radius: 6px; }"
            "QLabel#deviceTelemetrySectionTitleLabel { background-color: transparent; border: none; color: @vv-text-strong; font-size: 13px; font-weight: 700; padding: 0px; margin: 0px; }"
            "QLabel#homeOverviewSectionTitle { color: @vv-primary; font-size: 14px; font-weight: 700; padding: 0px; margin: 0px; }"
            "QLabel#homeTelemetrySummaryTitleLabel { color: @vv-primary; font-size: 13px; font-weight: 700; padding: 0px; margin: 0px; }"
            "QFrame#homeTelemetrySummaryPill { background-color: @vv-field-bg; border: 1px solid @vv-border; border-radius: 8px; padding: 0px; margin: 0px; }"
            "QFrame#homeTelemetrySummaryPill QLabel { background-color: transparent; border: none; color: @vv-text; font-size: 13px; font-weight: 600; padding: 0px; margin: 0px; }"
            "QFrame#homeTelemetrySummaryPill QLabel#homeTelemetrySummaryValueLabel { font-family: \"Cascadia Mono\", \"Consolas\", \"Courier New\", monospace; }"
            "QFrame#homeTelemetrySummaryPill QLabel[telemetryAvailable=\"false\"] { color: @vv-text-muted; }"
            "QLabel#homeTelemetrySummaryNameLabel[deviceConfigLink=\"true\"] { color: @vv-text-strong; font-size: 14px; font-weight: 700; }"
            "QLabel#homeTelemetrySummaryValueLabel[deviceConfigLink=\"true\"] { color: @vv-text-strong; font-size: 14px; font-weight: 600; }"
            "QLabel#homeTelemetrySummaryTitleLabel[skyTelemetryTitle=\"true\"] { color: @vv-primary; }"
            "QLabel#temperatureOverviewValuePill { background-color: @vv-surface; border: 1px solid @vv-border; border-radius: 10px; color: @vv-text-strong; font-family: \"Consolas\", \"Monaco\", \"Courier New\", monospace; font-size: 13px; font-weight: 700; padding: 2px 3px; margin: 0px; }"
            "QPushButton#temperatureOverviewOutputSwitch { background-color: transparent; border: none; padding: 0px; margin: 0px; color: @vv-text; font-size: 14px; font-weight: 700; }"
            "QToolButton#temperatureOverviewChannelButton { background-color: @vv-surface; border: 1px solid @vv-border; border-radius: 10px; color: @vv-primary; font-size: 13px; font-weight: 700; padding: 1px 8px 1px 8px; text-align: center; }"
            "QToolButton#temperatureOverviewChannelButton[available=\"false\"] { background-color: @vv-surface-alt; border-color: @vv-border; color: @vv-text-muted; }"
            "QToolButton#temperatureOverviewChannelButton:hover, QToolButton#temperatureOverviewChannelButton:pressed { background-color: @vv-surface; border-color: @vv-border-strong; }"
            "QToolButton#temperatureOverviewChannelButton[available=\"false\"]:hover, QToolButton#temperatureOverviewChannelButton[available=\"false\"]:pressed { background-color: @vv-surface-alt; border-color: @vv-border; }"
            "QToolButton#temperatureOverviewChannelButton::menu-indicator { image: none; width: 0px; height: 0px; }"
            "QFrame#homeOverviewDivider { background-color: @vv-border; border: none; min-width: 1px; max-width: 1px; }"
            "QLabel#epsilonSectionLabel { color: @vv-text; background-color: @vv-surface-alt; border: none; border-right: 1px solid @vv-border; font-size: 14px; font-weight: 700; padding: 2px; }"
            "QLabel#valueLabel { font-family: \"Consolas\", \"Monaco\", \"Courier New\", monospace; font-size: 14px; font-weight: 600; }"
            "QLabel#highlightedValue { font-family: \"Cascadia Mono\", \"Consolas\", \"Courier New\", monospace; }"
            "PtbPanel QLabel#highlightedValue, HmpPanel QLabel#highlightedValue, LidarPanel QLabel#highlightedValue, TemperatureControllerPanel QLabel#highlightedValue { font-family: \"Consolas\", \"Monaco\", \"Courier New\", monospace; font-size: 14px; font-weight: 600; background-color: transparent; padding: 0px; border-radius: 0px; }"
            "QComboBox { background-color: @vv-surface; border: 1px solid @vv-border; border-radius: 6px; padding: 4px 10px; min-height: 26px; max-height: 26px; color: @vv-text; font-size: 14px; }"
            "QComboBox:hover { border-color: @vv-border-strong; }"
            "QComboBox:focus { border-color: @vv-primary; border-width: 1px; }"
            "QComboBox:disabled { background-color: @vv-surface-alt; color: @vv-text; }"
            "QComboBox QAbstractItemView { background-color: @vv-menu-panel; border: none; border-radius: 12px; color: @vv-menu-text; selection-background-color: @vv-menu-hover; selection-color: @vv-menu-text; padding: 12px 0px; outline: none; }"
            "QComboBox QAbstractItemView::item { background-color: transparent; color: @vv-menu-text; padding: 7px 14px; min-height: 30px; border: 0px; border-radius: 0px; }"
            "QComboBox QAbstractItemView::item:hover, QComboBox QAbstractItemView::item:selected, QComboBox QAbstractItemView::item:selected:active, QComboBox QAbstractItemView::item:selected:!active { background-color: @vv-menu-hover; color: @vv-menu-text; }"
            "QComboBox QAbstractItemView::item:disabled { background-color: transparent; color: @vv-menu-disabled; }"
            "QComboBox QAbstractItemView::item:selected:disabled { background-color: @vv-menu-hover; color: @vv-menu-disabled; }"
            "QLineEdit { background-color: @vv-surface; border: 1px solid @vv-border; border-radius: 6px; padding: 4px 10px; min-height: 26px; max-height: 26px; color: @vv-text; font-size: 14px; }"
            "QLineEdit:hover { border-color: @vv-border-strong; }"
            "QLineEdit:focus { border-color: @vv-primary; border-width: 1px; }"
            "QLineEdit:disabled { background-color: @vv-surface-alt; color: @vv-text; }"
            "QSpinBox, QDoubleSpinBox { background-color: @vv-surface; border: 1px solid @vv-border; border-radius: 6px; padding: 4px 28px 4px 10px; min-height: 26px; max-height: 26px; color: @vv-text; font-size: 14px; }"
            "QSpinBox:hover, QDoubleSpinBox:hover { border-color: @vv-border-strong; }"
            "QSpinBox:focus, QDoubleSpinBox:focus { border-color: @vv-primary; border-width: 1px; }"
            "QSpinBox:disabled, QDoubleSpinBox:disabled { background-color: @vv-surface-alt; color: @vv-text; }"
            "QSpinBox::up-button, QSpinBox::down-button, QDoubleSpinBox::up-button, QDoubleSpinBox::down-button { width: 20px; border: none; background-color: transparent; subcontrol-origin: border; }"
            "QSpinBox::up-button, QDoubleSpinBox::up-button { subcontrol-position: top right; border-top-right-radius: 6px; }"
            "QSpinBox::down-button, QDoubleSpinBox::down-button { subcontrol-position: bottom right; border-bottom-right-radius: 6px; }"
            "QSpinBox::up-button:hover, QSpinBox::down-button:hover, QDoubleSpinBox::up-button:hover, QDoubleSpinBox::down-button:hover { background-color: @vv-surface-alt; }"
            "QSpinBox::up-arrow, QSpinBox::down-arrow, QDoubleSpinBox::up-arrow, QDoubleSpinBox::down-arrow { width: 0px; height: 0px; margin-right: 6px; border-left: 4px solid transparent; border-right: 4px solid transparent; }"
            "QSpinBox::up-arrow, QDoubleSpinBox::up-arrow { border-bottom: 5px solid @vv-control-arrow; }"
            "QSpinBox::down-arrow, QDoubleSpinBox::down-arrow { border-top: 5px solid @vv-control-arrow; }"
            "QTextEdit { background-color: @vv-surface; color: @vv-text; border: 1px solid @vv-border; border-radius: 6px; padding: 10px; font-family: \"Consolas\", \"Monaco\", \"Courier New\", monospace; font-size: 13px; }"
            "QTextEdit#logTextEdit { background-color: transparent; border: none; border-radius: 0px; }"
            "QWidget#logTextViewport { background-color: transparent; border: none; }"
            "QScrollBar:vertical { background-color: @vv-surface-sunken; width: 12px; border: none; border-radius: 6px; margin: 14px 0px 14px 0px; }"
            "QScrollBar::handle:vertical { background-color: @vv-scrollbar-handle; min-height: 30px; border-radius: 6px; border: 2px solid @vv-surface-sunken; margin: 0px; }"
            "QScrollBar::handle:vertical:hover { background-color: @vv-scrollbar-handle-hover; }"
            "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background-color: @vv-surface-sunken; border-radius: 6px; }"
            "QScrollBar::add-page:vertical:hover, QScrollBar::sub-page:vertical:hover, QScrollBar::add-page:vertical:pressed, QScrollBar::sub-page:vertical:pressed { background-color: @vv-surface-sunken; }"
            "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { background-color: @vv-surface-sunken; border: none; height: 14px; subcontrol-origin: margin; }"
            "QScrollBar::sub-line:vertical { border-top-left-radius: 6px; border-top-right-radius: 6px; subcontrol-position: top; }"
            "QScrollBar::add-line:vertical { border-bottom-left-radius: 6px; border-bottom-right-radius: 6px; subcontrol-position: bottom; }"
            "QScrollBar::add-line:vertical:hover, QScrollBar::sub-line:vertical:hover, QScrollBar::add-line:vertical:pressed, QScrollBar::sub-line:vertical:pressed { background-color: @vv-surface-sunken; }"
            "QScrollBar::up-arrow:vertical { image: url(combo_arrow_up.xpm); width: 12px; height: 8px; }"
            "QScrollBar::down-arrow:vertical { image: url(combo_arrow_down.xpm); width: 12px; height: 8px; }"
            "QScrollBar:horizontal { background-color: @vv-surface-sunken; height: 12px; border: none; border-radius: 6px; }"
            "QScrollBar::handle:horizontal { background-color: @vv-scrollbar-handle; min-width: 30px; border-radius: 6px; border: 2px solid @vv-surface-sunken; margin: 0px; }"
            "QScrollBar::handle:horizontal:hover { background-color: @vv-scrollbar-handle-hover; }"
            "QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { background-color: @vv-surface-sunken; border-radius: 6px; }"
            "QScrollBar::add-page:horizontal:hover, QScrollBar::sub-page:horizontal:hover, QScrollBar::add-page:horizontal:pressed, QScrollBar::sub-page:horizontal:pressed { background-color: @vv-surface-sunken; }"
            "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0px; background-color: @vv-surface-sunken; }"
            "QScrollArea#mainCardsScrollArea QScrollBar:vertical, QScrollArea#mainCardsScrollArea QScrollBar:horizontal, QScrollArea#mainCardsScrollArea QScrollBar::add-page:vertical, QScrollArea#mainCardsScrollArea QScrollBar::sub-page:vertical, QScrollArea#mainCardsScrollArea QScrollBar::add-page:horizontal, QScrollArea#mainCardsScrollArea QScrollBar::sub-page:horizontal, QScrollArea#mainCardsScrollArea QScrollBar::add-line:vertical, QScrollArea#mainCardsScrollArea QScrollBar::sub-line:vertical, QScrollArea#mainCardsScrollArea QScrollBar::add-line:horizontal, QScrollArea#mainCardsScrollArea QScrollBar::sub-line:horizontal { background-color: @vv-surface; }"
            "QScrollArea#mainCardsScrollArea QScrollBar::handle:vertical, QScrollArea#mainCardsScrollArea QScrollBar::handle:horizontal { border: 2px solid @vv-surface; }"
            "QSplitter::handle { background-color: transparent; }"
            "QSplitter#appLayoutSplitter::handle:horizontal { width: 8px; background-color: transparent; }"
            "QSplitter#appLayoutSplitter::handle:horizontal:hover { background-color: transparent; }"
            "QSplitter#appLayoutSplitter::handle:horizontal:pressed { background-color: transparent; }"
            "QSplitter#mainContentSplitter::handle:horizontal { width: 1px; background-color: transparent; }"
            "QSplitter#homeOverviewSplitter::handle:horizontal { width: 8px; background-color: @vv-surface; }"
            "QSplitter#homeOverviewSplitter::handle:horizontal:hover { background-color: @vv-resize-hover; }"
            "QSplitter#homeOverviewSplitter::handle:horizontal:pressed { background-color: @vv-resize-pressed; }"
            "QWidget#mainCardResizeHandle { min-height: 3px; max-height: 3px; background-color: transparent; }"
            "QSplitter#mainContentSplitter::handle:horizontal:hover { background-color: @vv-resize-hover; }"
            "QWidget#mainCardResizeHandle:hover { background-color: @vv-resize-hover; }"
            "QSplitter#mainContentSplitter::handle:horizontal:pressed { background-color: @vv-resize-pressed; }"
            "QWidget#mainCardResizeHandle[dragging=\"true\"] { background-color: @vv-resize-pressed; }"
            "QSplitter::handle:horizontal { width: 0px; }"
            "QSplitter::handle:vertical { height: 0px; }"
            "QSplitter#appLayoutSplitter::handle:horizontal { width: 8px; background-color: @vv-window; }"
            "QSplitter#appLayoutSplitter::handle:horizontal:hover { background-color: @vv-window; }"
            "QSplitter#appLayoutSplitter::handle:horizontal:pressed { background-color: @vv-window; }"
            "QSplitter#mainContentSplitter::handle:horizontal { width: 1px; background-color: @vv-border; }"
            "QSplitter#mainContentSplitter::handle:horizontal:hover { background-color: @vv-resize-hover; }"
            "QSplitter#mainContentSplitter::handle:horizontal:pressed { background-color: @vv-resize-pressed; }"
            "QSplitter#homeOverviewSplitter::handle:horizontal { width: 8px; background-color: @vv-surface; }"
            "QSplitter#homeOverviewSplitter::handle:horizontal:hover { background-color: @vv-resize-hover; }"
            "QSplitter#homeOverviewSplitter::handle:horizontal:pressed { background-color: @vv-resize-pressed; }"
            "QPushButton { background-color: @vv-primary; color: @vv-white; border: none; border-radius: 6px; padding: 4px 16px; font-size: 15px; font-weight: 500; min-height: 28px; max-height: 28px; }"
            "QPushButton:hover { background-color: @vv-primary-hover; }"
            "QPushButton:pressed { background-color: @vv-primary-pressed; }"
            "QPushButton:disabled { background-color: @vv-border-strong; color: @vv-white; }"
            "QPushButton#compactTcpButton { padding: 4px 14px; min-height: 28px; max-height: 28px; font-size: 14px; }"
            "QPushButton#compactTcpStartButton { padding: 4px 14px; min-height: 28px; max-height: 28px; font-size: 14px; }"
            "TemperatureControllerPanel QFrame#temperatureConfigCard { background-color: @vv-surface; border: 1px solid @vv-border; border-radius: 8px; }"
            "TemperatureControllerPanel QFrame#temperatureChannelTopBar { background-color: @vv-surface-alt; border: 1px solid @vv-border; border-radius: 8px; }"
            "TemperatureControllerPanel QStackedWidget#temperatureChannelStack { background-color: transparent; border: none; }"
            "TemperatureControllerPanel QPushButton[temperatureChannelSelector=\"true\"] { background-color: transparent; border: none; border-radius: 6px; color: @vv-text; font-size: 14px; font-weight: 500; min-height: 34px; max-height: 34px; padding: 0px 10px; text-align: center; }"
            "TemperatureControllerPanel QPushButton[temperatureChannelSelector=\"true\"]:checked { background-color: @vv-surface; color: @vv-primary; font-weight: 600; }"
            "TemperatureControllerPanel QPushButton[temperatureChannelSelector=\"true\"]:!checked:hover { background-color: @vv-primary-subtle; color: @vv-primary; }"
            "TemperatureControllerPanel QLabel[temperatureOutputEnableTopLabel=\"true\"] { color: @vv-text; font-size: 14px; font-weight: 600; }"
            "TemperatureControllerPanel QPushButton[temperatureOutputEnableSwitch=\"true\"] { background-color: transparent; border: none; padding: 0px; margin: 0px; min-width: 106px; max-width: 106px; min-height: 34px; max-height: 34px; outline: none; }"
            "QToolTip { background-color: rgb(45, 45, 45); color: #FFFFFF; border: 1px solid #474747; border-radius: 13px; padding: 8px 16px; font-size: 16px; }";
    }

}

QString temperatureControllerConfigStyleSheet()
{
    return QStringLiteral(
        "TemperatureControllerPanel QFrame#temperatureConfigCard { background-color: @vv-surface; border: 1px solid @vv-border; border-radius: 8px; }"
        "TemperatureControllerPanel QFrame#temperatureChannelTopBar { background-color: @vv-surface-alt; border: 1px solid @vv-border; border-radius: 8px; }"
        "TemperatureControllerPanel QStackedWidget#temperatureChannelStack { background-color: transparent; border: none; }"
        "TemperatureControllerPanel QPushButton[temperatureChannelSelector=\"true\"] { background-color: transparent; border: none; border-radius: 6px; color: @vv-text; font-size: 14px; font-weight: 500; min-height: 34px; max-height: 34px; padding: 0px 10px; text-align: center; }"
        "TemperatureControllerPanel QPushButton[temperatureChannelSelector=\"true\"]:checked { background-color: @vv-surface; color: @vv-primary; font-weight: 600; }"
        "TemperatureControllerPanel QPushButton[temperatureChannelSelector=\"true\"]:!checked:hover { background-color: @vv-primary-subtle; color: @vv-primary; }"
        "TemperatureControllerPanel QLabel[temperatureOutputEnableTopLabel=\"true\"] { color: @vv-text; font-size: 14px; font-weight: 600; }"
        "TemperatureControllerPanel QPushButton[temperatureOutputEnableSwitch=\"true\"] { background-color: transparent; border: none; padding: 0px; margin: 0px; min-width: 106px; max-width: 106px; min-height: 34px; max-height: 34px; outline: none; }"
        "TemperatureControllerPanel QPushButton#temperatureFactoryResetButton { background-color: transparent; border: 1px solid @vv-danger; border-radius: 8px; color: @vv-danger; font-size: 14px; font-weight: 600; padding: 0px 12px; text-align: center; }"
        "TemperatureControllerPanel QPushButton#temperatureFactoryResetButton:hover { background-color: rgba(220, 38, 38, 0.08); }"
        "TemperatureControllerPanel QLabel#fieldLabel[temperatureMaxOutputWarning=\"true\"] { color: @vv-danger; }"
        "TemperatureControllerPanel QSpinBox[temperatureMaxOutputWarning=\"true\"] { color: @vv-danger; }");
}

QString MainWindow::themedStyleSheet() const
{
    const QString baseStyle = applyAppThemeTokens(base_style_sheet_, false);
    const QString mainCardsScrollBarStyle =
        applyAppThemeTokens(mainCardsScrollBarBackgroundStyleSheet(dark_theme_enabled_),
                            dark_theme_enabled_);
    const QString rtkConfigCardStyle =
        applyAppThemeTokens(rtkConfigCardStyleSheet(), dark_theme_enabled_);
    return dark_theme_enabled_
        ? baseStyle +
              applyAppThemeTokens(darkThemeStyleSheet(), true) +
              applyAppThemeTokens(darkOverviewStyleSheet(), true) +
              mainCardsScrollBarStyle +
              rtkConfigCardStyle +
              applyAppThemeTokens(customTitleBarStyleSheet(true), true) +
              applyAppThemeTokens(temperatureControllerConfigStyleSheet(), true)
        : baseStyle +
              mainCardsScrollBarStyle +
              rtkConfigCardStyle +
              applyAppThemeTokens(customTitleBarStyleSheet(false), false) +
              applyAppThemeTokens(temperatureControllerConfigStyleSheet(), false);
}

QString MainWindow::scaledStyleSheet(const QString& styleSheet) const
{
    const QRegularExpression pixelRegex(R"((\d+)px)");
    QString scaled = styleSheet;
    QRegularExpressionMatchIterator it = pixelRegex.globalMatch(styleSheet);
    struct Replacement
    {
        qsizetype start;
        qsizetype length;
        QString text;
    };
    QList<Replacement> replacements;

    while (it.hasNext())
    {
        const QRegularExpressionMatch match = it.next();
        const int originalPx = match.captured(1).toInt();
        const int scaledPx = originalPx == 0 ? 0 : std::max(1, scalePixels(originalPx));
        replacements.append({match.capturedStart(0), match.capturedLength(0), QString("%1px").arg(scaledPx)});
    }

    for (auto replacementIt = replacements.crbegin(); replacementIt != replacements.crend(); ++replacementIt)
    {
        scaled.replace(replacementIt->start, replacementIt->length, replacementIt->text);
    }

    return scaled;
}

int MainWindow::scalePixels(int pixels) const
{
    return static_cast<int>(std::lround(pixels * font_scale_percent_ / 100.0));
}

int MainWindow::minimumLogSidePanelWidth() const
{
    const QString titleText = is_english_ ? QStringLiteral("Log") : QStringLiteral("日志");
    QFontMetrics titleMetrics(log_inline_title_lbl_ ? log_inline_title_lbl_->font() : font());
    const int titleClusterWidth =
        scalePixels(kSectionTitleIconBoxSize + 6) +
        titleMetrics.horizontalAdvance(titleText);
    const int titleBarMargins = scalePixels(16);
    const int titleBarSpacing = scalePixels(8 * 3);
    const int actionButtonsWidth = scalePixels(kMainPageButtonHeight * 2);
    const int cardMargins = scalePixels(2);
    const int safetyPadding = scalePixels(12);
    return titleBarMargins + titleClusterWidth + titleBarSpacing + actionButtonsWidth + cardMargins + safetyPadding;
}

int MainWindow::appSidebarIconOnlyWidth() const
{
    return std::max(kAppSidebarIconOnlyBaseWidth, scalePixels(kAppSidebarIconOnlyBaseWidth));
}

int MainWindow::appSidebarDefaultWidth() const
{
    return std::max(96, scalePixels(kAppSidebarFullBaseWidth));
}

int MainWindow::currentAppSidebarWidth() const
{
    if (!app_layout_splitter_)
    {
        return appSidebarDefaultWidth();
    }

    const QList<int> sizes = app_layout_splitter_->sizes();
    return sizes.isEmpty() ? appSidebarDefaultWidth() : std::max(0, sizes.at(0));
}

bool MainWindow::isAppSidebarCollapsed() const
{
    return currentAppSidebarWidth() == 0 ||
           appSidebarModeForWidth(currentAppSidebarWidth()) == AppSidebarMode::Collapsed;
}

void MainWindow::saveAppSidebarWidth() const
{
    QSettings settings("VaporView", "MainWindow");
    settings.setValue(QStringLiteral("app_sidebar_width"), currentAppSidebarWidth());
}

void MainWindow::setAppSidebarWidth(int width)
{
    if (!app_layout_splitter_)
    {
        return;
    }

    const int sidebarWidth = std::max(0, width);
    if (app_sidebar_)
    {
        app_sidebar_->setMinimumWidth(sidebarWidth);
        app_sidebar_->setMaximumWidth(sidebarWidth);
    }
    const int splitterWidth = app_layout_splitter_->width();
    const int handleWidth = app_layout_splitter_->handleWidth();
    const int contentWidth = splitterWidth > sidebarWidth + handleWidth
        ? std::max(1, splitterWidth - sidebarWidth - handleWidth)
        : 1600;

    const QSignalBlocker blocker(app_layout_splitter_);
    app_sidebar_adjusting_ = true;
    app_layout_splitter_->setSizes({sidebarWidth, contentWidth});
    app_sidebar_adjusting_ = false;
    if (app_sidebar_)
    {
        app_sidebar_->setMinimumWidth(0);
        app_sidebar_->setMaximumWidth(QWIDGETSIZE_MAX);
    }
}

void MainWindow::updateAppSidebarButtonTexts()
{
    const bool compact = app_sidebar_mode_ != AppSidebarMode::Full;
    auto applyButtonText = [this, compact](QPushButton *button, const QString& label) {
        if (!button)
        {
            return;
        }
        button->setText(compact ? QString() : label);
        button->setToolTip(label);
        button->setStatusTip(label);
        button->setAccessibleName(label);
        button->setProperty(kSidebarCompactProperty, compact);
        if (compact)
        {
            const int buttonSize = scalePixels(kAppSidebarCompactButtonSize);
            button->setFixedSize(buttonSize, buttonSize);
            button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        }
        else
        {
            button->setMinimumWidth(0);
            button->setMaximumWidth(QWIDGETSIZE_MAX);
            button->setFixedHeight(scalePixels(kAppSidebarButtonHeight));
            button->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        }
        const int iconSize = scalePixels(compact ? kAppSidebarCompactIconSize : kAppSidebarFullIconSize);
        button->setIconSize(QSize(iconSize, iconSize));
        if (button->style())
        {
            button->style()->unpolish(button);
            button->style()->polish(button);
        }
        button->update();
    };

    applyButtonText(home_nav_btn_, is_english_ ? QStringLiteral("Home") : QStringLiteral("首页"));
    applyButtonText(temperature_nav_btn_, is_english_ ? QStringLiteral("Thermal") : QStringLiteral("温控"));
    applyButtonText(rtk_config_nav_btn_, is_english_ ? QStringLiteral("RTK Config") : QStringLiteral("RTK配置"));
    applyButtonText(device_config_nav_btn_, is_english_ ? QStringLiteral("Device") : QStringLiteral("设备配置"));
    updateRtkConfigIcon();
    updateCustomTitleBarTexts();
}

MainWindow::AppSidebarMode MainWindow::appSidebarModeForWidth(int width) const
{
    const int normalizedWidth = std::max(0, width);
    const int iconOnlyWidth = appSidebarIconOnlyWidth();
    const int fullWidth = appSidebarDefaultWidth();
    const int collapsedThreshold = std::max(8, iconOnlyWidth / 2);
    const int fullThreshold = (iconOnlyWidth + fullWidth) / 2;

    if (normalizedWidth <= collapsedThreshold)
    {
        return AppSidebarMode::Collapsed;
    }
    if (normalizedWidth < fullThreshold)
    {
        return AppSidebarMode::IconsOnly;
    }
    return AppSidebarMode::Full;
}

int MainWindow::snappedAppSidebarWidth(int width) const
{
    const int normalizedWidth = std::max(0, width);
    switch (appSidebarModeForWidth(width))
    {
    case AppSidebarMode::Collapsed:
        return 0;
    case AppSidebarMode::IconsOnly:
        return appSidebarIconOnlyWidth();
    case AppSidebarMode::Full:
        return std::max(normalizedWidth, appSidebarDefaultWidth());
    }
    return appSidebarDefaultWidth();
}

void MainWindow::updateAppSidebarForWidth(int width, bool snapToNearest)
{
    const int normalizedWidth = std::max(0, width);
    const AppSidebarMode mode = appSidebarModeForWidth(normalizedWidth);
    if (!snapToNearest)
    {
        app_sidebar_drag_width_ = normalizedWidth;
        app_sidebar_drag_width_valid_ = true;
        if (normalizedWidth > 0)
        {
            last_app_sidebar_visible_width_ = normalizedWidth;
        }
        const AppSidebarMode dragMode = mode == AppSidebarMode::Collapsed
            ? AppSidebarMode::IconsOnly
            : mode;
        if (app_sidebar_mode_ != dragMode)
        {
            app_sidebar_mode_ = dragMode;
            updateAppSidebarButtonTexts();
        }
        return;
    }

    if (app_sidebar_mode_ != mode)
    {
        app_sidebar_mode_ = mode;
        updateAppSidebarButtonTexts();
    }

    const int snapWidth = snappedAppSidebarWidth(normalizedWidth);
    if (snapWidth > 0)
    {
        last_app_sidebar_visible_width_ = snapWidth;
    }
    if (snapWidth != normalizedWidth)
    {
        setAppSidebarWidth(snapWidth);
    }
    updateCustomLogoPixmap();
    updateCustomLogoTooltip();
}

void MainWindow::finishAppSidebarResize()
{
    const int targetWidth = app_sidebar_drag_width_valid_
        ? app_sidebar_drag_width_
        : currentAppSidebarWidth();

    app_sidebar_drag_width_valid_ = false;
    updateAppSidebarForWidth(targetWidth, true);
    saveAppSidebarWidth();
}

void MainWindow::toggleAppSidebarFromLogo()
{
    if (!app_layout_splitter_)
    {
        return;
    }

    if (isAppSidebarCollapsed())
    {
        int restoreWidth = last_app_sidebar_visible_width_ > 0
            ? last_app_sidebar_visible_width_
            : appSidebarDefaultWidth();
        restoreWidth = snappedAppSidebarWidth(restoreWidth);
        if (restoreWidth <= 0)
        {
            restoreWidth = appSidebarIconOnlyWidth();
        }
        app_sidebar_mode_ = appSidebarModeForWidth(restoreWidth);
        updateAppSidebarButtonTexts();
        setAppSidebarWidth(restoreWidth);
        last_app_sidebar_visible_width_ = restoreWidth;
    }
    else
    {
        const int currentWidth = currentAppSidebarWidth();
        const int rememberedWidth = snappedAppSidebarWidth(currentWidth);
        if (rememberedWidth > 0)
        {
            last_app_sidebar_visible_width_ = rememberedWidth;
        }
        app_sidebar_mode_ = AppSidebarMode::Collapsed;
        updateAppSidebarButtonTexts();
        setAppSidebarWidth(0);
    }

    saveAppSidebarWidth();
    updateCustomLogoPixmap();
    updateCustomLogoTooltip();
    queueResponsiveHomeLayoutRefresh();
}

void MainWindow::setCustomLogoHovered(bool hovered)
{
    if (custom_logo_hovered_ == hovered)
    {
        return;
    }

    custom_logo_hovered_ = hovered;
    updateCustomLogoPixmap();
    updateCustomLogoTooltip();
}

void MainWindow::updateCustomLogoPixmap()
{
    if (!custom_logo_label_)
    {
        return;
    }

    const int logoSize = scalePixels(44);
    custom_logo_label_->setFixedSize(logoSize, logoSize);
    const bool collapsed = isAppSidebarCollapsed();
    const QString logoState = custom_logo_hovered_
        ? (collapsed ? QStringLiteral("open-sidebar") : QStringLiteral("close-sidebar"))
        : QStringLiteral("logo");
    const QSize sidebarIconSize(scalePixels(24), scalePixels(24));
    const QPixmap pixmap = custom_logo_hovered_
        ? createAppSidebarToggleIcon(collapsed).pixmap(sidebarIconSize)
        : renderVaporViewLogo(dark_theme_enabled_, logoSize, custom_logo_label_->devicePixelRatioF());
    custom_logo_label_->setPixmap(pixmap);
    custom_logo_label_->setProperty(kCustomLogoStateProperty, logoState);
    custom_logo_label_->setProperty("titleBarHover", custom_logo_hovered_);
    if (custom_logo_label_->style())
    {
        custom_logo_label_->style()->unpolish(custom_logo_label_);
        custom_logo_label_->style()->polish(custom_logo_label_);
    }
    custom_logo_label_->update();
}

void MainWindow::updateCustomLogoTooltip()
{
    if (!custom_logo_label_)
    {
        return;
    }

    const QString tooltip = isAppSidebarCollapsed()
        ? (is_english_ ? QStringLiteral("Show left sidebar") : QStringLiteral("展开左侧栏"))
        : (is_english_ ? QStringLiteral("Hide left sidebar") : QStringLiteral("收起左侧栏"));
    custom_logo_label_->setToolTip(tooltip);
    custom_logo_label_->setStatusTip(tooltip);
    custom_logo_label_->setAccessibleName(tooltip);
}

void MainWindow::setLogSidePanelToMinimumWidth()
{
    if (!main_content_splitter_ || log_side_panel_collapsed_)
    {
        return;
    }

    const int minimumLogWidth = minimumLogSidePanelWidth();
    const int totalWidth = main_content_splitter_->width();
    if (totalWidth <= minimumLogWidth + main_content_splitter_->handleWidth())
    {
        return;
    }

    const int leftWidth = std::max(1, totalWidth - minimumLogWidth - main_content_splitter_->handleWidth());
    main_content_splitter_->setSizes({leftWidth, minimumLogWidth});
    last_log_side_panel_width_ = minimumLogWidth;
    log_side_panel_width_initialized_ = true;
}

void MainWindow::toggleLogSidePanel()
{
    setLogSidePanelCollapsed(!log_side_panel_collapsed_);
}

void MainWindow::setLogSidePanelCollapsed(bool collapsed)
{
    log_side_panel_collapsed_ = collapsed;

    if (!log_side_panel_ || !main_content_splitter_)
    {
        updateLogSidePanelToggleButton();
        return;
    }

    const int minimumLogWidth = minimumLogSidePanelWidth();
    const QList<int> sizes = main_content_splitter_->sizes();
    if (collapsed)
    {
        if (log_side_panel_width_initialized_ && sizes.size() >= 2 && sizes.at(1) >= minimumLogWidth)
        {
            last_log_side_panel_width_ = sizes.at(1);
        }
        const int totalWidth = std::max(1, main_content_splitter_->width() > 1
            ? main_content_splitter_->width()
            : base_window_size_.width());
        log_side_panel_->hide();
        main_content_splitter_->setSizes({totalWidth, 0});
        updateLogSidePanelToggleButton();
        queueResponsiveHomeLayoutRefresh();
        return;
    }

    log_side_panel_->setMinimumWidth(minimumLogWidth);
    log_side_panel_->show();
    log_side_panel_->setMaximumWidth(QWIDGETSIZE_MAX);

    const QRect availableGeometry = currentScreenAvailableGeometry();
    const int totalWidth = std::max(1, main_content_splitter_->width() > 1
        ? main_content_splitter_->width()
        : (availableGeometry.isValid() ? availableGeometry.width() : base_window_size_.width()));
    const int maxLogWidth = std::max(minimumLogWidth, totalWidth - main_content_splitter_->handleWidth() - 1);
    const int rememberedWidth = last_log_side_panel_width_ > 0 ? last_log_side_panel_width_ : minimumLogWidth;
    const int logWidth = std::min(std::max(rememberedWidth, minimumLogWidth), maxLogWidth);
    const int leftWidth = std::max(1, totalWidth - logWidth - main_content_splitter_->handleWidth());
    main_content_splitter_->setSizes({leftWidth, std::max(minimumLogWidth, totalWidth - leftWidth)});
    log_side_panel_width_initialized_ = true;
    updateLogSidePanelToggleButton();
    queueResponsiveHomeLayoutRefresh();
}

void MainWindow::updateLogSidePanelToggleButton()
{
    if (!log_side_panel_toggle_btn_)
    {
        return;
    }

    log_side_panel_toggle_btn_->setIcon(createLogSidePanelToggleIcon(log_side_panel_collapsed_));
    log_side_panel_toggle_btn_->setToolTip(log_side_panel_collapsed_
        ? (is_english_ ? QStringLiteral("Show right panel") : QStringLiteral("展开右侧栏"))
        : (is_english_ ? QStringLiteral("Hide right panel") : QStringLiteral("收起右侧栏")));
    log_side_panel_toggle_btn_->setStatusTip(log_side_panel_toggle_btn_->toolTip());
}

void MainWindow::applyScaledUiMetrics()
{
    auto applyWidgetMetrics = [this](QWidget *widget) {
        if (!widget)
        {
            return;
        }

        const int minimumWidth = widget->minimumWidth();
        const bool usesDynamicHomeOverviewWidth = widget == config_group_ && data_telemetry_summary_card_;
        if (minimumWidth > 0 && !usesDynamicHomeOverviewWidth)
        {
            rememberBaseMetric(widget, kBaseMinWidthProperty, minimumWidth);
            widget->setMinimumWidth(std::max(1, scalePixels(widget->property(kBaseMinWidthProperty).toInt())));
        }

        const int minimumHeight = widget->minimumHeight();
        if (minimumHeight > 0)
        {
            rememberBaseMetric(widget, kBaseMinHeightProperty, minimumHeight);
            widget->setMinimumHeight(std::max(1, scalePixels(widget->property(kBaseMinHeightProperty).toInt())));
        }

        const int maximumWidth = widget->maximumWidth();
        if (maximumWidth > 0 && maximumWidth < QWIDGETSIZE_MAX)
        {
            rememberBaseMetric(widget, kBaseMaxWidthProperty, maximumWidth);
            widget->setMaximumWidth(std::max(1, scalePixels(widget->property(kBaseMaxWidthProperty).toInt())));
        }

        const int maximumHeight = widget->maximumHeight();
        if (maximumHeight > 0 && maximumHeight < QWIDGETSIZE_MAX)
        {
            rememberBaseMetric(widget, kBaseMaxHeightProperty, maximumHeight);
            widget->setMaximumHeight(std::max(1, scalePixels(widget->property(kBaseMaxHeightProperty).toInt())));
        }

        if (auto *label = qobject_cast<QLabel *>(widget))
        {
            const QStringList textWidthCandidates = label->property(kTextWidthCandidatesProperty).toStringList();
            if (!textWidthCandidates.isEmpty())
            {
                const int padding = label->property(kTextWidthPaddingProperty).toInt();
                applyFixedTextLabelWidth(label, textWidthCandidates, std::max(0, scalePixels(padding)));
            }

            const QStringList widthCandidates = label->property(kNumericWidthCandidatesProperty).toStringList();
            if (!widthCandidates.isEmpty())
            {
                const int padding = label->property(kNumericWidthPaddingProperty).toInt();
                applyFixedNumericLabelWidth(label, widthCandidates, std::max(0, scalePixels(padding)));
            }
        }
    };

    applyWidgetMetrics(this);
    for (QWidget *widget : findChildren<QWidget*>())
    {
        applyWidgetMetrics(widget);
    }

    auto applyLayoutMetrics = [this](QLayout *layout) {
        if (!layout)
        {
            return;
        }

        if (layout->spacing() >= 0)
        {
            rememberBaseMetric(layout, kBaseSpacingProperty, layout->spacing());
            layout->setSpacing(std::max(0, scalePixels(layout->property(kBaseSpacingProperty).toInt())));
        }

        const QMargins margins = layout->contentsMargins();
        rememberBaseMetric(layout, kBaseMarginsLeftProperty, margins.left());
        rememberBaseMetric(layout, kBaseMarginsTopProperty, margins.top());
        rememberBaseMetric(layout, kBaseMarginsRightProperty, margins.right());
        rememberBaseMetric(layout, kBaseMarginsBottomProperty, margins.bottom());
        layout->setContentsMargins(
            std::max(0, scalePixels(layout->property(kBaseMarginsLeftProperty).toInt())),
            std::max(0, scalePixels(layout->property(kBaseMarginsTopProperty).toInt())),
            std::max(0, scalePixels(layout->property(kBaseMarginsRightProperty).toInt())),
            std::max(0, scalePixels(layout->property(kBaseMarginsBottomProperty).toInt()))
        );
    };

    if (layout())
    {
        applyLayoutMetrics(layout());
    }

    for (QLayout *layout : findChildren<QLayout*>())
    {
        applyLayoutMetrics(layout);
    }
}

bool MainWindow::shouldUseCompactHomeLayout() const
{
    const QRect availableGeometry = currentScreenAvailableGeometry();
    const QSize availableSize = availableGeometry.isValid() ? availableGeometry.size() : QSize();
    const int viewportWidth = main_cards_scroll_area_ && main_cards_scroll_area_->viewport()
        ? main_cards_scroll_area_->viewport()->width()
        : width();
    return (availableSize.isValid() &&
            (availableSize.width() <= kCompactHomeScreenWidth || availableSize.height() <= kCompactHomeScreenHeight)) ||
           (viewportWidth > 0 && viewportWidth <= kCompactHomeViewportWidth);
}

void MainWindow::updateResponsiveHomeLayout()
{
    if (!sensor_layout_ || !sensor_row_widget_ || !data_group_)
    {
        return;
    }

    const bool compact = shouldUseCompactHomeLayout();
    const bool layoutChanged = compact_home_layout_ != compact;
    compact_home_layout_ = compact;

    const QBoxLayout::Direction direction = compact ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight;
    if (sensor_layout_->direction() != direction)
    {
        sensor_layout_->setDirection(direction);
    }
    sensor_layout_->setSpacing(compact ? 4 : 2);

    if (epsilon_panel_)
    {
        epsilon_panel_->setCompactLayout(compact);
    }
    if (tcp_wave_panel_)
    {
        tcp_wave_panel_->setCompactLayout(compact);
    }
    if (tcp_wave_group_ && tcp_wave_panel_)
    {
        const int preferredTcpWaveHeight = tcp_wave_panel_->preferredPanelHeight();
        const bool useExpandedTcpWaveHeight = tcp_wave_panel_->usesExpandedPanelHeight();
        const int tcpWaveMinimumHeight = std::max(
            useExpandedTcpWaveHeight ? (compact ? kCompactTcpWaveCardMinHeight : kTcpWaveCardMinHeight) : preferredTcpWaveHeight,
            preferredTcpWaveHeight);
        tcp_wave_group_->setFixedHeight(tcpWaveMinimumHeight);
    }
    if (main_cards_scroll_area_ && main_cards_scroll_area_->widget() && main_cards_scroll_area_->viewport())
    {
        updateHomeDeviceOverviewMinimumWidth();
        const int viewportWidth = std::max(0, main_cards_scroll_area_->viewport()->width());
        const int overviewMinimumWidth = home_overview_splitter_ && config_group_ && temperature_overview_group_
            ? config_group_->minimumWidth() + temperature_overview_group_->minimumWidth() + home_overview_splitter_->handleWidth()
            : (config_group_ ? config_group_->minimumWidth() : 0);
        const bool widthConstrained = viewportWidth > 0 && viewportWidth < overviewMinimumWidth;
        const int contentWidth = widthConstrained ? overviewMinimumWidth : viewportWidth;
        main_cards_scroll_area_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        main_cards_scroll_area_->widget()->setSizePolicy(widthConstrained ? QSizePolicy::Preferred : QSizePolicy::Ignored,
                                                         QSizePolicy::Preferred);
        main_cards_scroll_area_->widget()->setMinimumWidth(widthConstrained ? overviewMinimumWidth : 0);
        main_cards_scroll_area_->widget()->setMaximumWidth(widthConstrained ? QWIDGETSIZE_MAX : viewportWidth);
        main_cards_scroll_area_->widget()->resize(contentWidth, main_cards_scroll_area_->widget()->height());
        if (home_overview_splitter_)
        {
            const int overviewWidth = widthConstrained ? overviewMinimumWidth : viewportWidth;
            home_overview_splitter_->setMinimumWidth(overviewWidth);
            home_overview_splitter_->setMaximumWidth(overviewWidth);
        }
        if (QLayout *leftLayout = main_cards_scroll_area_->widget()->layout())
        {
            leftLayout->invalidate();
            leftLayout->activate();
        }
        updateHomeDeviceOverviewMinimumWidth();
        if (home_overview_splitter_ && config_group_ && temperature_overview_group_)
        {
            const QList<int> sizes = home_overview_splitter_->sizes();
            const bool initialized = home_overview_splitter_->property(kHomeOverviewSplitterInitializedProperty).toBool();
            const int leftMinimum = config_group_->minimumWidth();
            const int rightMinimum = temperature_overview_group_->minimumWidth();
            const bool invalidSizes = sizes.size() < 2 || (sizes.at(0) + sizes.at(1)) <= 0;
            const int totalWidth = contentWidth > 0 ? contentWidth : std::max(overviewMinimumWidth, home_overview_splitter_->width());
            const int availableWidth = std::max(0, totalWidth - home_overview_splitter_->handleWidth());
            const bool sizeTooNarrow = sizes.size() >= 2 && availableWidth >= leftMinimum + rightMinimum &&
                (sizes.at(0) < leftMinimum || sizes.at(1) < rightMinimum);
            if ((!initialized || invalidSizes || sizeTooNarrow) && viewportWidth > 0)
            {
                const int maxLeftWidth = std::max(leftMinimum, availableWidth - rightMinimum);
                const int leftWidth = std::min(leftMinimum, maxLeftWidth);
                const int rightWidth = std::max(rightMinimum, availableWidth - leftWidth);
                home_overview_splitter_->setSizes({leftWidth, rightWidth});
                home_overview_splitter_->setProperty(kHomeOverviewSplitterInitializedProperty, true);
            }
        }
    }

    if (epsilon_group_)
    {
        epsilon_group_->setMaximumWidth(QWIDGETSIZE_MAX);
        epsilon_group_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        sensor_layout_->setAlignment(epsilon_group_, Qt::Alignment());
    }
    if (env_group_)
    {
        if (compact)
        {
            env_group_->setMaximumWidth(QWIDGETSIZE_MAX);
        }
        else
        {
            const int rowWidth = sensor_row_widget_->contentsRect().width();
            const int gap = std::max(0, sensor_layout_->spacing());
            const int availableWidth = std::max(0, rowWidth - gap);
            const int totalStretch = kSensorNavigationStretch + kSensorEnvironmentStretch;
            const int targetEnvironmentWidth = totalStretch > 0
                ? availableWidth * kSensorEnvironmentStretch / totalStretch
                : 0;
            const int environmentMinimumWidth = std::max(env_group_->minimumWidth(),
                                                         env_group_->minimumSizeHint().width());
            env_group_->setMaximumWidth(std::max(environmentMinimumWidth, targetEnvironmentWidth));
        }
        env_group_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        sensor_layout_->setAlignment(env_group_, Qt::Alignment());
    }
    if (sensor_layout_->count() >= 2)
    {
        sensor_layout_->setStretch(0, compact ? 0 : kSensorNavigationStretch);
        sensor_layout_->setStretch(1, compact ? 0 : kSensorEnvironmentStretch);
    }

    auto clearFixedHeight = [](QWidget *widget) {
        if (!widget)
        {
            return;
        }
        widget->setMinimumHeight(0);
        widget->setMaximumHeight(QWIDGETSIZE_MAX);
        if (QLayout *widgetLayout = widget->layout())
        {
            widgetLayout->activate();
        }
    };
    auto contentHeightFor = [](QWidget *widget) {
        if (!widget)
        {
            return 0;
        }
        return std::max(widget->minimumSizeHint().height(), widget->sizeHint().height());
    };

    clearFixedHeight(epsilon_group_);
    clearFixedHeight(env_group_);
    clearFixedHeight(sensor_row_widget_);
    clearFixedHeight(data_group_);
    if (sensor_layout_)
    {
        sensor_layout_->invalidate();
        sensor_layout_->activate();
    }

    const int epsilonHeight = contentHeightFor(epsilon_group_);
    const int envHeight = contentHeightFor(env_group_);
    int targetHeight = std::max(epsilonHeight, envHeight);
    if (compact && epsilonHeight > 0 && envHeight > 0)
    {
        targetHeight = epsilonHeight + envHeight + sensor_layout_->spacing();
    }

    if (targetHeight > 0)
    {
        if (compact)
        {
            if (epsilon_group_)
            {
                epsilon_group_->setMinimumHeight(epsilonHeight);
            }
            if (env_group_)
            {
                env_group_->setMinimumHeight(envHeight);
            }
        }
        else
        {
            if (epsilon_group_)
            {
                epsilon_group_->setFixedHeight(targetHeight);
            }
            if (env_group_)
            {
                env_group_->setFixedHeight(targetHeight);
            }
        }
        sensor_row_widget_->setMinimumHeight(targetHeight);
        data_group_->setProperty(kMainCardMinimumHeightProperty, targetHeight);
        data_group_->setMinimumHeight(targetHeight);
        if (layoutChanged || data_group_->height() < targetHeight)
        {
            data_group_->setFixedHeight(targetHeight);
        }
    }

    if (log_side_panel_)
    {
        const int minimumLogWidth = minimumLogSidePanelWidth();
        log_side_panel_->setMinimumWidth(minimumLogWidth);
        log_side_panel_->setMaximumWidth(QWIDGETSIZE_MAX);
    }

    if (main_content_splitter_ && !log_side_panel_collapsed_)
    {
        const QRect availableGeometry = currentScreenAvailableGeometry();
        const int totalWidth = std::max(1, main_content_splitter_->width() > 1
            ? main_content_splitter_->width()
            : (availableGeometry.isValid() ? availableGeometry.width() : base_window_size_.width()));
        const int minimumLogWidth = minimumLogSidePanelWidth();
        const int logWidth = minimumLogWidth;
        const QList<int> sizes = main_content_splitter_->sizes();
        const bool logPanelTooNarrow = sizes.size() >= 2 && sizes.at(1) < minimumLogWidth;
        const int initialLeftWidth = std::max(scalePixels(320), minimumLogWidth);
        const bool splitterHasRealWidth = main_content_splitter_->isVisible() && main_content_splitter_->width() > 1;
        const bool canInitializeLogWidth =
            splitterHasRealWidth &&
            totalWidth >= minimumLogWidth + main_content_splitter_->handleWidth() + initialLeftWidth;
        if (log_side_panel_width_initialized_ && sizes.size() >= 2 && sizes.at(1) >= minimumLogWidth)
        {
            last_log_side_panel_width_ = sizes.at(1);
        }
        if ((!log_side_panel_width_initialized_ && canInitializeLogWidth) ||
            (logPanelTooNarrow && totalWidth > minimumLogWidth + main_content_splitter_->handleWidth()))
        {
            const int leftWidth = std::max(1, totalWidth - logWidth - main_content_splitter_->handleWidth());
            main_content_splitter_->setSizes({leftWidth, std::max(1, totalWidth - leftWidth)});
            if (canInitializeLogWidth)
            {
                last_log_side_panel_width_ = minimumLogWidth;
                log_side_panel_width_initialized_ = true;
            }
        }
    }

    if (compact && layoutChanged)
    {
        queueResponsiveHomeLayoutRefresh();
    }
}

void MainWindow::queueResponsiveHomeLayoutRefresh()
{
    if (responsive_home_layout_refresh_pending_)
    {
        return;
    }

    responsive_home_layout_refresh_pending_ = true;
    QTimer::singleShot(0, this, [this]() {
        responsive_home_layout_refresh_pending_ = false;
        updateResponsiveHomeLayout();
    });
}

void MainWindow::applyStyleConfiguration()
{
    QFont appFont = qApp->font();
    appFont.setPointSizeF(base_font_point_size_ * font_scale_percent_ / 100.0);
    qApp->setPalette(appThemePalette(dark_theme_enabled_));
    qApp->setFont(appFont);
    qApp->setStyleSheet(scaledStyleSheet(themedStyleSheet()));
    configureComboPopupsIn(this);
    setWindowsTitleBarDark(this, dark_theme_enabled_);
    applyScaledUiMetrics();
    if (rtk_config_dialog_)
    {
        rtk_config_dialog_->setFontScale(font_scale_percent_);
    }
    if (app_layout_splitter_)
    {
        updateAppSidebarForWidth(currentAppSidebarWidth(), true);
    }
    updateAppSidebarButtonTexts();
    updateThemedIcons();
    updateCustomTitleBarStyle();
    updateResponsiveHomeLayout();
    QTimer::singleShot(0, this, [this]() {
        if (!log_side_panel_width_initialized_)
        {
            setLogSidePanelToMinimumWidth();
        }
    });

    if (!isFullScreen() && !isMaximized())
    {
        const QSize targetSize = size().expandedTo(minimumSize()).expandedTo(minimumSizeHint());
        if (targetSize != size())
        {
            resize(targetSize);
        }
    }

    // 主题切换会触发多次异步重算（同步 1 次 + resizeEvent 1 次 + singleShot 若干），
    // 早期重算发生时 qApp 的字体度量/样式尚未完全传播，viewport 宽度也未必稳定，
    // 可能算出偏大的 data_group 目标高度并被 setFixedHeight 锁住，把下方的 TCP
    // 波形卡片压下去后无法回弹。这里在 resize 完成、度量稳定后，解除卡片固定高度
    // 并按最新布局重测一次，使其收敛到正确值。
    auto releaseFixedHeight = [](QWidget *widget) {
        if (!widget)
        {
            return;
        }
        widget->setMinimumHeight(0);
        widget->setMaximumHeight(QWIDGETSIZE_MAX);
        if (QLayout *layout = widget->layout())
        {
            layout->invalidate();
            layout->activate();
        }
    };
    releaseFixedHeight(data_group_);
    releaseFixedHeight(tcp_wave_group_);
    updateRemoteTelemetrySummaryLabel();
    updateResponsiveHomeLayout();
    QTimer::singleShot(0, this, [this]() {
        updateRemoteTelemetrySummaryLabel();
        updateResponsiveHomeLayout();
        QTimer::singleShot(0, this, [this]() {
            updateHomeDeviceOverviewMinimumWidth();
            updateResponsiveHomeLayout();
        });
    });
}

void MainWindow::configureComboPopup(QComboBox *combo) const
{
    configureComboBoxPopup(combo, dark_theme_enabled_);
}

void MainWindow::configureComboPopupsIn(QWidget *scope) const
{
    if (!scope)
    {
        return;
    }

    if (auto *combo = qobject_cast<QComboBox *>(scope))
    {
        configureComboPopup(combo);
    }
    const QList<QComboBox*> combos = scope->findChildren<QComboBox *>();
    for (QComboBox *combo : combos)
    {
        configureComboPopup(combo);
    }
}

void MainWindow::setFontScale(int percent)
{
    if (percent < 70 || percent > 150 || font_scale_percent_ == percent)
    {
        return;
    }

    QSize targetSize = size();
    if (!isFullScreen() && !isMaximized())
    {
        targetSize = QSize(
            std::max(1, static_cast<int>(std::lround(base_window_size_.width() * percent / 100.0))),
            std::max(1, static_cast<int>(std::lround(base_window_size_.height() * percent / 100.0)))
        );
    }

    font_scale_percent_ = percent;
    discardTitleApplicationMenuPanel();
    applyStyleConfiguration();
    updateSourceModeUi();
    if (!isFullScreen() && !isMaximized())
    {
        targetSize = targetSize.expandedTo(minimumSize()).expandedTo(minimumSizeHint());
        if (targetSize != size())
        {
            resize(targetSize);
        }
    }
    if (rtk_config_dialog_)
    {
        rtk_config_dialog_->setFontScale(font_scale_percent_);
    }
    if (sky_device_config_dialog_)
    {
        sky_device_config_dialog_->setFontScale(font_scale_percent_);
    }

    QSettings settings("VaporView", "MainWindow");
    settings.setValue("font_scale_percent", font_scale_percent_);
}

void MainWindow::rebuildRecordingRateMenu()
{
    if (!recording_rate_menu_ || custom_title_bar_)
    {
        return;
    }

    recording_rate_menu_->clear();
    auto buildSubmenu = [this](QMenu *parent,
                               const QString& title,
                               const QVector<int>& standardRates,
                               int currentRate,
                               bool allowUnlimited,
                               const QString& unlimitedEnglish,
                               const QString& unlimitedChinese,
                               auto setter) {
        QMenu *submenu = parent->addMenu(title);

        auto addAction = [this, submenu, currentRate, &setter](int rate, const QString& text) {
            QAction *action = submenu->addAction(text);
            action->setIcon(rate == currentRate ? createMenuCheckIcon(dark_theme_enabled_) : QIcon());
            connect(action, &QAction::triggered, this, [this, rate, setter]() {
                setter(rate);
            });
        };

        if (allowUnlimited)
        {
            addAction(0, is_english_ ? unlimitedEnglish : unlimitedChinese);
        }

        if (currentRate > 0 && !standardRates.contains(currentRate))
        {
            addAction(currentRate,
                      QStringLiteral("%1 Hz%2").arg(currentRate).arg(is_english_ ? QStringLiteral(" (Custom)") : QStringLiteral("（当前）")));
        }

        for (int rate : standardRates)
        {
            addAction(rate, QStringLiteral("%1 Hz").arg(rate));
        }
    };

    buildSubmenu(recording_rate_menu_,
                 is_english_ ? QStringLiteral("TCP wave raw recording") : QStringLiteral("TCP波形原始记录"),
                 {},
                 0,
                 true,
                 QStringLiteral("Record every complete TCP frame"),
                 QStringLiteral("记录完整TCP原始帧"),
                 [this](int) { setWaveformRecordingRateHz(0); });

    buildSubmenu(recording_rate_menu_,
                 is_english_ ? QStringLiteral("EPSILON raw recording") : QStringLiteral("EPSILON原始记录"),
                 {},
                 0,
                 true,
                 QStringLiteral("Record verified FDILink raw frames"),
                 QStringLiteral("记录已校验FDILink原始帧"),
                 [this](int) { setImuRecordingRateHz(0); });

    buildSubmenu(recording_rate_menu_,
                 is_english_ ? QStringLiteral("Device CSV recording rate") : QStringLiteral("设备CSV记录频率"),
                 QVector<int>{1, 2, 5, 10, 20, 50, 100, 200},
                 std::clamp(recording_export_rate_hz_, 1, 200),
                 false,
                 QString(),
                 QString(),
                 [this](int rate) { setRecordingExportRateHz(rate); });
}

void MainWindow::setRecordingExportRateHz(int rate, bool should_log)
{
    const int normalizedRate = std::clamp(rate, 1, 200);
    const bool changed = recording_export_rate_hz_ != normalizedRate;
    recording_export_rate_hz_ = normalizedRate;
    rebuildRecordingRateMenu();
    discardTitleApplicationMenuPanel();
    saveRememberedInputState();

    if (changed && should_log)
    {
        log(QString(is_english_ ? "Other-devices recording rate set to %1 Hz" : "其余设备记录频率已设置为 %1 Hz").arg(recording_export_rate_hz_));
    }
}

void MainWindow::setImuRecordingRateHz(int rate, bool should_log)
{
    Q_UNUSED(rate);
    const int normalizedRate = 0;
    const bool changed = imu_recording_rate_hz_ != normalizedRate;
    imu_recording_rate_hz_ = normalizedRate;
    rebuildRecordingRateMenu();
    discardTitleApplicationMenuPanel();
    saveRememberedInputState();

    if (changed && should_log)
    {
        log(is_english_
            ? QStringLiteral("EPSILON raw recording keeps full verified FDILink frames")
            : QStringLiteral("EPSILON 原始记录固定保存完整已校验 FDILink 帧"));
    }
}

void MainWindow::setWaveformRecordingRateHz(int rate, bool should_log)
{
    Q_UNUSED(rate);
    const int normalizedRate = 0;
    const bool changed = waveform_recording_rate_hz_ != normalizedRate;
    waveform_recording_rate_hz_ = normalizedRate;
    rebuildRecordingRateMenu();
    discardTitleApplicationMenuPanel();
    saveRememberedInputState();

    if (changed && should_log)
    {
        log(is_english_
            ? QStringLiteral("TCP wave raw recording keeps every complete TCP frame")
            : QStringLiteral("TCP 波形原始记录固定保存每组完整 TCP 帧"));
    }
}

void MainWindow::loadRememberedInputState()
{
    QSettings settings("VaporView", "MainWindow");

    auto loadCombo = [&settings](QComboBox *combo, const QString& key, const QString& fallbackKey = QString()) {
        if (!combo)
        {
            return;
        }
        QVariant fallback = combo->currentText();
        if (!fallbackKey.isEmpty())
        {
            fallback = settings.value(fallbackKey, fallback);
        }
        applyComboText(combo, settings.value(key, fallback).toString());
    };

    loadCombo(epsilon_port_combo_, QStringLiteral("serial/epsilon_port"), QStringLiteral("serial/gnss_port"));
    loadCombo(ptb_port_combo_, QStringLiteral("serial/ptb_port"));
    loadCombo(hmp_port_combo_, QStringLiteral("serial/hmp_port"));
    loadCombo(lidar_port_combo_, QStringLiteral("serial/lidar_port"));
    loadCombo(temperature_port_combo_, QStringLiteral("serial/temperature_port"));

    loadCombo(epsilon_baud_combo_, QStringLiteral("serial/epsilon_baud"), QStringLiteral("serial/gnss_baud"));
    loadCombo(ptb_baud_combo_, QStringLiteral("serial/ptb_baud"));
    loadCombo(hmp_baud_combo_, QStringLiteral("serial/hmp_baud"));
    loadCombo(lidar_baud_combo_, QStringLiteral("serial/lidar_baud"));
    loadCombo(temperature_baud_combo_, QStringLiteral("serial/temperature_baud"));

    loadCombo(global_rate_combo_, QStringLiteral("rate/global"));
    loadCombo(epsilon_rate_combo_, QStringLiteral("rate/epsilon"), QStringLiteral("rate/gnss"));
    loadCombo(ptb_rate_combo_, QStringLiteral("rate/ptb"));
    loadCombo(hmp_rate_combo_, QStringLiteral("rate/hmp"));
    loadCombo(lidar_rate_combo_, QStringLiteral("rate/lidar"));
    loadCombo(temperature_rate_combo_, QStringLiteral("rate/temperature"));
    if (data_source_mode_combo_)
    {
        const QString value = settings.value(
            QStringLiteral("source/mode"),
            sourceModeStorageValue(data_source_mode_combo_->currentIndex())).toString();
        const int index = sourceModeIndexFromStoredValue(value);
        if (index >= 0)
        {
            const QSignalBlocker blocker(data_source_mode_combo_);
            data_source_mode_combo_->setCurrentIndex(index);
        }
    }
    loadCombo(sky_telemetry_port_combo_, QStringLiteral("telemetry/sky_port"));
    loadCombo(sky_telemetry_baud_combo_, QStringLiteral("telemetry/sky_baud"));
    if (sky_telemetry_transport_combo_)
    {
        const QString transport = settings.value(QStringLiteral("telemetry/transport"), QStringLiteral("tcp")).toString();
        const int index = sky_telemetry_transport_combo_->findData(transport);
        sky_telemetry_transport_combo_->setCurrentIndex(index >= 0 ? index : 0);
    }
    if (sky_telemetry_tcp_host_edit_)
    {
        sky_telemetry_tcp_host_edit_->setText(settings.value(QStringLiteral("telemetry/tcp_host"), QStringLiteral("192.168.1.2")).toString());
    }
    if (sky_telemetry_tcp_port_spin_)
    {
        sky_telemetry_tcp_port_spin_->setValue(settings.value(QStringLiteral("telemetry/tcp_port"), 39100).toInt());
    }

    recording_export_rate_hz_ = std::clamp(settings.value("recording_export_rate_hz", recording_export_rate_hz_).toInt(), 1, 200);
    imu_recording_rate_hz_ = std::clamp(settings.value("imu_recording_rate_hz", imu_recording_rate_hz_).toInt(), 0, 1000);
    waveform_recording_rate_hz_ = 0;
    rebuildRecordingRateMenu();

    const QStringList args = QCoreApplication::arguments();
    const int sourceIndex = args.indexOf(QStringLiteral("--source"));
    if (sourceIndex >= 0 && sourceIndex + 1 < args.size() &&
        args.at(sourceIndex + 1).compare(QStringLiteral("remote"), Qt::CaseInsensitive) == 0 &&
        data_source_mode_combo_)
    {
        data_source_mode_combo_->setCurrentIndex(1);
    }
    const int portIndex = args.indexOf(QStringLiteral("--telemetry-port"));
    if (portIndex >= 0 && portIndex + 1 < args.size() && sky_telemetry_port_combo_)
    {
        sky_telemetry_port_combo_->setEditText(args.at(portIndex + 1));
    }
    const int baudIndex = args.indexOf(QStringLiteral("--telemetry-baud"));
    if (baudIndex >= 0 && baudIndex + 1 < args.size() && sky_telemetry_baud_combo_)
    {
        sky_telemetry_baud_combo_->setCurrentText(args.at(baudIndex + 1));
    }
    const int transportIndex = args.indexOf(QStringLiteral("--telemetry-transport"));
    if (transportIndex >= 0 && transportIndex + 1 < args.size() && sky_telemetry_transport_combo_)
    {
        const QString transport = args.at(transportIndex + 1).trimmed().toLower();
        const int comboIndex = sky_telemetry_transport_combo_->findData(transport);
        if (comboIndex >= 0)
        {
            sky_telemetry_transport_combo_->setCurrentIndex(comboIndex);
        }
    }
    const int hostIndex = args.indexOf(QStringLiteral("--telemetry-host"));
    if (hostIndex >= 0 && hostIndex + 1 < args.size() && sky_telemetry_tcp_host_edit_)
    {
        sky_telemetry_tcp_host_edit_->setText(args.at(hostIndex + 1));
    }
    const int tcpPortIndex = args.indexOf(QStringLiteral("--telemetry-tcp-port"));
    if (tcpPortIndex >= 0 && tcpPortIndex + 1 < args.size() && sky_telemetry_tcp_port_spin_)
    {
        sky_telemetry_tcp_port_spin_->setValue(args.at(tcpPortIndex + 1).toInt());
    }
    onDataSourceModeChanged(data_source_mode_combo_ ? data_source_mode_combo_->currentIndex() : 0);
}

void MainWindow::saveRememberedInputState() const
{
    QSettings settings("VaporView", "MainWindow");

    auto saveCombo = [&settings](const QString& key, QComboBox *combo) {
        if (combo)
        {
            settings.setValue(key, combo->currentText());
        }
    };

    saveCombo(QStringLiteral("serial/epsilon_port"), epsilon_port_combo_);
    saveCombo(QStringLiteral("serial/ptb_port"), ptb_port_combo_);
    saveCombo(QStringLiteral("serial/hmp_port"), hmp_port_combo_);
    saveCombo(QStringLiteral("serial/lidar_port"), lidar_port_combo_);
    saveCombo(QStringLiteral("serial/temperature_port"), temperature_port_combo_);

    saveCombo(QStringLiteral("serial/epsilon_baud"), epsilon_baud_combo_);
    saveCombo(QStringLiteral("serial/ptb_baud"), ptb_baud_combo_);
    saveCombo(QStringLiteral("serial/hmp_baud"), hmp_baud_combo_);
    saveCombo(QStringLiteral("serial/lidar_baud"), lidar_baud_combo_);
    saveCombo(QStringLiteral("serial/temperature_baud"), temperature_baud_combo_);

    saveCombo(QStringLiteral("rate/global"), global_rate_combo_);
    saveCombo(QStringLiteral("rate/epsilon"), epsilon_rate_combo_);
    saveCombo(QStringLiteral("rate/ptb"), ptb_rate_combo_);
    saveCombo(QStringLiteral("rate/hmp"), hmp_rate_combo_);
    saveCombo(QStringLiteral("rate/lidar"), lidar_rate_combo_);
    saveCombo(QStringLiteral("rate/temperature"), temperature_rate_combo_);
    if (data_source_mode_combo_)
    {
        settings.setValue(QStringLiteral("source/mode"), sourceModeStorageValue(data_source_mode_combo_->currentIndex()));
    }
    saveCombo(QStringLiteral("telemetry/sky_port"), sky_telemetry_port_combo_);
    saveCombo(QStringLiteral("telemetry/sky_baud"), sky_telemetry_baud_combo_);
    if (sky_telemetry_transport_combo_)
    {
        settings.setValue(QStringLiteral("telemetry/transport"), sky_telemetry_transport_combo_->currentData().toString());
    }
    if (sky_telemetry_tcp_host_edit_)
    {
        settings.setValue(QStringLiteral("telemetry/tcp_host"), sky_telemetry_tcp_host_edit_->text().trimmed());
    }
    if (sky_telemetry_tcp_port_spin_)
    {
        settings.setValue(QStringLiteral("telemetry/tcp_port"), sky_telemetry_tcp_port_spin_->value());
    }
    settings.setValue("recording_export_rate_hz", recording_export_rate_hz_);
    settings.setValue("imu_recording_rate_hz", imu_recording_rate_hz_);
    settings.setValue("waveform_recording_rate_hz", waveform_recording_rate_hz_);
}

void MainWindow::bindRememberedInputState()
{
    auto bindCombo = [this](QComboBox *combo) {
        if (!combo)
        {
            return;
        }
        connect(combo, &QComboBox::currentTextChanged, this, [this](const QString&) {
            saveRememberedInputState();
            updateHomeDeviceStatusCapsules();
            updateTemperatureControllerTitleText();
            updateTemperatureTitleButtonsState();
        });
    };

    bindCombo(epsilon_port_combo_);
    bindCombo(ptb_port_combo_);
    bindCombo(hmp_port_combo_);
    bindCombo(lidar_port_combo_);
    bindCombo(temperature_port_combo_);
    bindCombo(epsilon_baud_combo_);
    bindCombo(ptb_baud_combo_);
    bindCombo(hmp_baud_combo_);
    bindCombo(lidar_baud_combo_);
    bindCombo(temperature_baud_combo_);
    bindCombo(global_rate_combo_);
    bindCombo(epsilon_rate_combo_);
    bindCombo(ptb_rate_combo_);
    bindCombo(hmp_rate_combo_);
    bindCombo(lidar_rate_combo_);
    bindCombo(temperature_rate_combo_);
    bindCombo(data_source_mode_combo_);
    bindCombo(sky_telemetry_transport_combo_);
    bindCombo(sky_telemetry_port_combo_);
    bindCombo(sky_telemetry_baud_combo_);
    if (sky_telemetry_tcp_host_edit_)
    {
        connect(sky_telemetry_tcp_host_edit_, &QLineEdit::textChanged, this, [this](const QString&) {
            saveRememberedInputState();
        });
    }
    if (sky_telemetry_tcp_port_spin_)
    {
        connect(sky_telemetry_tcp_port_spin_, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) {
            saveRememberedInputState();
        });
    }

}

bool MainWindow::isRemoteSkyMode() const
{
    return remote_sky_mode_;
}

bool MainWindow::isRemoteSkyTcpMode() const
{
    if (!sky_telemetry_transport_combo_)
    {
        return true;
    }
    return sky_telemetry_transport_combo_->currentData().toString() != QStringLiteral("serial");
}

void MainWindow::onDataSourceModeChanged(int index)
{
    remote_sky_mode_ = index == 1;
    saveRememberedInputState();
    clearRemoteSkyDataUi();
    if (tcp_wave_panel_)
    {
        tcp_wave_panel_->setRemoteSkyMode(remote_sky_mode_);
    }
    updateSourceModeUi();
    updateRecordingActionStates();
}

void MainWindow::updateSourceModeUi()
{
    const bool remote = isRemoteSkyMode();
    const bool localInputsEnabled = !remote && !is_connected_ &&
        !connection_attempt_in_progress_ && !port_detection_in_progress_ && !epsilon_reconfigure_in_progress_;
    const QList<QWidget*> localWidgets = {epsilon_port_combo_, epsilon_baud_combo_, ptb_port_combo_, ptb_baud_combo_,
                                          hmp_port_combo_, hmp_baud_combo_, lidar_port_combo_, lidar_baud_combo_,
                                          temperature_port_combo_, temperature_baud_combo_,
                                          epsilon_packet_rates_btn_, ptb_rate_combo_, hmp_rate_combo_, lidar_rate_combo_,
                                          temperature_rate_combo_};
    for (QWidget *widget : localWidgets)
    {
        if (widget)
        {
            widget->setEnabled(localInputsEnabled);
        }
    }
    if (auto_detect_ports_btn_)
    {
        auto_detect_ports_btn_->setEnabled(!remote && !is_connected_ && !connection_attempt_in_progress_);
    }
    if (source_mode_switch_)
    {
        source_mode_switch_->setEnabled(!is_connected_ && !connection_attempt_in_progress_);
        source_mode_switch_->setSwitchChecked(remote, source_mode_switch_->switchChecked() != remote);
    }
    const bool remoteInputsEnabled = remote && !is_connected_ && !connection_attempt_in_progress_;
    const bool tcpTelemetry = isRemoteSkyTcpMode();
    if (sky_telemetry_transport_combo_) sky_telemetry_transport_combo_->setEnabled(remoteInputsEnabled);
    if (sky_telemetry_port_combo_) sky_telemetry_port_combo_->setEnabled(remoteInputsEnabled && !tcpTelemetry);
    if (sky_telemetry_baud_combo_) sky_telemetry_baud_combo_->setEnabled(remoteInputsEnabled && !tcpTelemetry);
    if (sky_telemetry_tcp_host_edit_) sky_telemetry_tcp_host_edit_->setEnabled(remoteInputsEnabled && tcpTelemetry);
    if (sky_telemetry_tcp_port_spin_) sky_telemetry_tcp_port_spin_->setEnabled(remoteInputsEnabled && tcpTelemetry);
    if (sky_telemetry_row_widget_) sky_telemetry_row_widget_->setVisible(true);
    if (sky_telemetry_transport_lbl_) sky_telemetry_transport_lbl_->setVisible(true);
    if (sky_telemetry_transport_combo_) sky_telemetry_transport_combo_->setVisible(true);
    if (sky_telemetry_port_lbl_) sky_telemetry_port_lbl_->setVisible(!tcpTelemetry);
    if (sky_telemetry_port_combo_) sky_telemetry_port_combo_->setVisible(!tcpTelemetry);
    if (sky_telemetry_baud_lbl_) sky_telemetry_baud_lbl_->setVisible(!tcpTelemetry);
    if (sky_telemetry_baud_combo_) sky_telemetry_baud_combo_->setVisible(!tcpTelemetry);
    if (sky_telemetry_tcp_host_lbl_) sky_telemetry_tcp_host_lbl_->setVisible(tcpTelemetry);
    if (sky_telemetry_tcp_host_edit_) sky_telemetry_tcp_host_edit_->setVisible(tcpTelemetry);
    if (sky_telemetry_tcp_port_lbl_) sky_telemetry_tcp_port_lbl_->setVisible(tcpTelemetry);
    if (sky_telemetry_tcp_port_spin_) sky_telemetry_tcp_port_spin_->setVisible(tcpTelemetry);
    if (sky_device_config_btn_) sky_device_config_btn_->setEnabled(remote && ground_telemetry_service_ && ground_telemetry_service_->isOpen());
    setRemoteDeviceButtonsEnabled(remote && ground_telemetry_service_ && ground_telemetry_service_->isOpen());
    updateTemperatureControllerTitleText();
    updateTemperatureTitleButtonsState();
    updateRemoteTelemetrySummaryLabel();
    updateHomeDeviceStatusCapsules();
    updateConfigCardHeightForSourceMode();
    updateDeviceConfigState();
}

int MainWindow::scaledConfiguredHeight(QWidget *widget, int baseHeight) const
{
    if (widget && widget->property(kBaseMinHeightProperty).isValid())
    {
        return scalePixels(baseHeight);
    }
    return baseHeight;
}

int MainWindow::homeDeviceOverviewContentMinimumWidth() const
{
    if (!config_group_)
    {
        return kHomeOverviewDeviceMinWidth;
    }

    auto childNaturalWidth = [](const QWidget *widget) {
        if (!widget)
        {
            return 0;
        }
        int width = widget->minimumWidth();
        width = std::max(width, widget->minimumSizeHint().width());
        if (width <= 0)
        {
            width = widget->sizeHint().width();
        }
        return width;
    };

    auto telemetryPillWidthHint = [&childNaturalWidth](const QWidget *widget) {
        if (!widget || !widget->layout())
        {
            return 0;
        }

        int width = 0;
        int visibleItemCount = 0;
        const QMargins margins = widget->layout()->contentsMargins();
        for (int i = 0; i < widget->layout()->count(); ++i)
        {
            QLayoutItem *item = widget->layout()->itemAt(i);
            QWidget *child = item ? item->widget() : nullptr;
            if (!child || child->isHidden())
            {
                continue;
            }
            if (visibleItemCount > 0)
            {
                width += std::max(0, widget->layout()->spacing());
            }
            width += childNaturalWidth(child);
            ++visibleItemCount;
        }

        return margins.left() + width + margins.right();
    };

    auto widgetWidthHint = [&childNaturalWidth, &telemetryPillWidthHint](const QWidget *widget) {
        if (!widget)
        {
            return 0;
        }
        if (widget->objectName() == QStringLiteral("homeTelemetrySummaryPill"))
        {
            return telemetryPillWidthHint(widget);
        }
        if (widget->objectName() == QStringLiteral("homeTelemetrySummaryTitleLabel"))
        {
            return childNaturalWidth(widget);
        }

        int width = childNaturalWidth(widget);
        width = std::max(width, widget->sizeHint().width());
        return width;
    };

    auto horizontalLayoutContentWidth = [&widgetWidthHint](const QLayout *layout) {
        if (!layout)
        {
            return 0;
        }

        int width = 0;
        int visibleItemCount = 0;
        const QMargins margins = layout->contentsMargins();
        for (int i = 0; i < layout->count(); ++i)
        {
            QLayoutItem *item = layout->itemAt(i);
            if (!item || item->spacerItem())
            {
                continue;
            }

            int itemWidth = 0;
            if (QWidget *widget = item->widget())
            {
                if (widget->isHidden())
                {
                    continue;
                }
                itemWidth = widgetWidthHint(widget);
            }
            else if (QLayout *childLayout = item->layout())
            {
                itemWidth = childLayout->minimumSize().width();
            }
            else
            {
                itemWidth = item->minimumSize().width();
            }

            if (itemWidth <= 0)
            {
                continue;
            }
            if (visibleItemCount > 0)
            {
                width += std::max(0, layout->spacing());
            }
            width += itemWidth;
            ++visibleItemCount;
        }

        return margins.left() + width + margins.right();
    };

    auto sectionContentWidth = [this, &horizontalLayoutContentWidth](const QWidget *section) {
        if (!section || !section->layout())
        {
            return 0;
        }

        int widestLine = 0;
        const QMargins sectionMargins = section->layout()->contentsMargins();
        for (int i = 0; i < section->layout()->count(); ++i)
        {
            QLayoutItem *item = section->layout()->itemAt(i);
            QWidget *lineWidget = item ? item->widget() : nullptr;
            if (!lineWidget || lineWidget->isHidden())
            {
                continue;
            }
            widestLine = std::max(widestLine, horizontalLayoutContentWidth(lineWidget->layout()));
        }

        if (widestLine <= 0)
        {
            return 0;
        }
        return sectionMargins.left() +
               widestLine +
               sectionMargins.right() +
               scalePixels(12);
    };

    const QMargins cardMargins = config_group_->layout()
        ? config_group_->layout()->contentsMargins()
        : QMargins();
    QWidget *body = config_group_->findChild<QWidget *>(QStringLiteral("homeOverviewDeviceBody"));
    const QMargins bodyMargins = body && body->layout()
        ? body->layout()->contentsMargins()
        : QMargins(scalePixels(kHomeOverviewBodyPadding),
                   scalePixels(kHomeOverviewBodyPadding),
                   scalePixels(kHomeOverviewBodyPadding),
                   scalePixels(kConfigHomeBodyBottomPadding));

    auto widthInsideCard = [&cardMargins, &bodyMargins](int contentWidth) {
        return contentWidth +
               cardMargins.left() +
               cardMargins.right() +
               bodyMargins.left() +
               bodyMargins.right();
    };

    int minimumWidth = kHomeOverviewDeviceMinWidth;
    if (QWidget *homeDevices = config_group_->findChild<QWidget *>(QStringLiteral("homeOverviewDeviceGrid")))
    {
        minimumWidth = std::max(minimumWidth, widthInsideCard(widgetWidthHint(homeDevices)));
    }

    if (data_telemetry_summary_card_)
    {
        int summaryContentWidth = 0;
        const QList<QFrame*> sections =
            data_telemetry_summary_card_->findChildren<QFrame *>(QStringLiteral("homeTelemetrySectionCard"));
        for (QFrame *section : sections)
        {
            if (section->isHidden())
            {
                continue;
            }
            summaryContentWidth = std::max(summaryContentWidth, sectionContentWidth(section));
        }
        if (summaryContentWidth <= 0)
        {
            summaryContentWidth = widgetWidthHint(data_telemetry_summary_card_);
        }
        minimumWidth = std::max(minimumWidth, widthInsideCard(summaryContentWidth));
    }

    return minimumWidth;
}

void MainWindow::updateHomeDeviceOverviewMinimumWidth()
{
    if (!config_group_)
    {
        return;
    }

    const int contentMinimumWidth = homeDeviceOverviewContentMinimumWidth();
    config_group_->setMinimumWidth(contentMinimumWidth);

    if (!home_overview_splitter_ || !temperature_overview_group_)
    {
        return;
    }

    const QList<int> sizes = home_overview_splitter_->sizes();
    if (sizes.size() < 2 || sizes.at(0) >= contentMinimumWidth)
    {
        return;
    }

    const int availableWidth = std::max(0,
                                        std::max(home_overview_splitter_->width(),
                                                 sizes.at(0) + sizes.at(1) + home_overview_splitter_->handleWidth()) -
                                            home_overview_splitter_->handleWidth());
    const int rightMinimumWidth = temperature_overview_group_->minimumWidth();
    if (availableWidth < contentMinimumWidth + rightMinimumWidth)
    {
        return;
    }

    home_overview_splitter_->setSizes({
        contentMinimumWidth,
        std::max(rightMinimumWidth, availableWidth - contentMinimumWidth)
    });
}

void MainWindow::updateConfigCardHeightForSourceMode()
{
    if (!config_group_)
    {
        return;
    }

    int minimumHeight = scaledConfiguredHeight(config_group_, kConfigCardMinHeight);
    if (data_telemetry_summary_card_)
    {
        if (QLayout *summaryLayout = data_telemetry_summary_card_->layout())
        {
            summaryLayout->invalidate();
            summaryLayout->activate();
        }
        int summaryHeight = std::max(data_telemetry_summary_card_->sizeHint().height(),
                                     data_telemetry_summary_card_->minimumSizeHint().height());
        summaryHeight = std::max(summaryHeight + scalePixels(kHomeTelemetrySummaryHeightPadding),
                                 scalePixels(kMainPageInputHeight));
        data_telemetry_summary_card_->setMinimumHeight(summaryHeight);
        data_telemetry_summary_card_->setMaximumHeight(summaryHeight);
        const int homeDeviceRowHeight = scalePixels((kHomeDeviceRowHeight * kHomeDeviceGridRows) +
                                                    (kHomeDeviceGridRowGap * (kHomeDeviceGridRows - 1)));
        const int homeBodySpacing = scalePixels(2);
        const int homeBodyTopPadding = scalePixels(kHomeOverviewBodyPadding);
        const int homeBodyBottomPadding = scalePixels(kConfigHomeBodyBottomPadding);
        minimumHeight = std::max(minimumHeight,
                                 kMainPageTitleBarHeight +
                                     homeBodyTopPadding +
                                     homeDeviceRowHeight +
                                     homeBodySpacing +
                                     summaryHeight +
                                     homeBodyBottomPadding +
                                     scalePixels(kConfigCardBottomPadding));
    }

    config_group_->setProperty(kMainCardMinimumHeightProperty, minimumHeight);
    const int stableConfigMinimumHeight = std::max(config_group_->height(), minimumHeight);
    config_group_->setMinimumHeight(stableConfigMinimumHeight);
    if (temperature_overview_group_)
    {
        const int temperatureMinimumHeight = std::max(temperature_overview_group_->minimumSizeHint().height(),
                                                      temperature_overview_group_->sizeHint().height());
        minimumHeight = std::max(minimumHeight, temperatureMinimumHeight);
        temperature_overview_group_->setProperty(kMainCardMinimumHeightProperty, minimumHeight);
        temperature_overview_group_->setMinimumHeight(minimumHeight);
    }
    if (home_overview_splitter_)
    {
        home_overview_splitter_->setProperty(kMainCardMinimumHeightProperty, minimumHeight);
        const int stableSplitterMinimumHeight = std::max(home_overview_splitter_->height(), minimumHeight);
        home_overview_splitter_->setMinimumHeight(stableSplitterMinimumHeight);
        if (home_overview_splitter_->height() < stableSplitterMinimumHeight)
        {
            home_overview_splitter_->setFixedHeight(stableSplitterMinimumHeight);
        }
        return;
    }
    if (config_group_->height() < stableConfigMinimumHeight)
    {
        config_group_->setFixedHeight(stableConfigMinimumHeight);
    }
}

void MainWindow::clearRemoteSkyDataUi()
{
    remote_device_states_.clear();
    remote_last_data_ms_.clear();
    remote_packet_arrivals_ms_.clear();
    remote_waveform_channel_arrivals_ms_.clear();
    remote_temperature_commands_.clear();
    remote_last_status_ms_ = 0;
    remote_sky_online_ = false;
    remote_wave_stream_requested_ = false;
    remote_wave_stream_enable_pending_ = false;
    remote_wave_stream_auto_start_ = true;
    remote_status_ = VaporView::TelemetryStatus();
    remote_recording_state_ = 0;

    current_epsilon_ = VaporView::EpsilonData();
    current_gnss_ = VaporView::GnssData();
    current_imu_ = VaporView::ImuData();
    current_ptb_ = VaporView::PtbData();
    current_hmp_ = VaporView::HmpData();
    current_lidar_ = VaporView::LidarData();
    current_temperature_controller_ = VaporView::TemperatureControllerData();

    current_ptb_.error_message = remoteNoDataText(is_english_).toStdString();
    current_hmp_.error_message = remoteNoDataText(is_english_).toStdString();
    current_lidar_.error_message = remoteNoDataText(is_english_).toStdString();
    if (tcp_wave_panel_)
    {
        tcp_wave_panel_->setRemoteWaveTcpState(VaporView::DeviceState::Disconnected);
    }

    if (epsilon_panel_) epsilon_panel_->updateRate(0.0);
    if (ptb_panel_) ptb_panel_->updateRate(0.0);
    if (hmp_panel_) hmp_panel_->updateRate(0.0);
    if (lidar_panel_) lidar_panel_->updateRate(0.0);
    if (temperature_controller_panel_) temperature_controller_panel_->updateRate(0.0);
    if (epsilon_panel_) epsilon_panel_->updateData(current_epsilon_);
    if (gnss_panel_) gnss_panel_->updateData(current_gnss_, 0);
    if (imu_panel_) imu_panel_->updateData(current_imu_, 0);
    if (ptb_panel_) ptb_panel_->updateData(current_ptb_);
    if (hmp_panel_) hmp_panel_->updateData(current_hmp_);
    if (lidar_panel_) lidar_panel_->updateData(current_lidar_);
    if (temperature_controller_panel_) temperature_controller_panel_->updateData(current_temperature_controller_);
    if (temperature_overview_panel_) temperature_overview_panel_->updateData(current_temperature_controller_);
    updateEnvironmentStatusIcons(false, false, false);
    updateSourceModeUi();
    updateRemoteTelemetrySummaryLabel();
    updateHomeDeviceStatusCapsules();
    updateRecordingStatusLabel();
}

void MainWindow::markRemoteSkyLinkClosed()
{
    remote_last_status_ms_ = 0;
    remote_sky_online_ = false;
    remote_wave_stream_requested_ = false;
    remote_wave_stream_enable_pending_ = false;
    remote_packet_arrivals_ms_.clear();
    remote_waveform_channel_arrivals_ms_.clear();
    remote_temperature_commands_.clear();
    remote_recording_state_ = 0;
    remote_status_.recording_state = 0;
    if (tcp_wave_panel_)
    {
        tcp_wave_panel_->setRemoteWaveTcpState(VaporView::DeviceState::Disconnected);
    }
    refreshRemoteSkyDataUi();
    updateSourceModeUi();
    updateHomeDeviceStatusCapsules();
    updateRecordingStatusLabel();
}

bool MainWindow::remoteDeviceDataValid(VaporView::SkyDeviceId device, qint64 timeout_ms) const
{
    if (!ground_telemetry_service_ || !ground_telemetry_service_->isOpen())
    {
        return false;
    }
    if (remote_last_status_ms_ <= 0 || QDateTime::currentMSecsSinceEpoch() - remote_last_status_ms_ > 3000)
    {
        return false;
    }
    if (remote_device_states_.value(device, VaporView::DeviceState::Disconnected) != VaporView::DeviceState::Connected)
    {
        return false;
    }
    const qint64 lastDataMs = remote_last_data_ms_.value(device, 0);
    return lastDataMs > 0 && QDateTime::currentMSecsSinceEpoch() - lastDataMs <= timeout_ms;
}

QString MainWindow::remoteDeviceInvalidText(VaporView::SkyDeviceId device, qint64 timeout_ms) const
{
    if (!ground_telemetry_service_ || !ground_telemetry_service_->isOpen())
    {
        return remoteDisconnectedText(is_english_);
    }
    if (remote_last_status_ms_ <= 0)
    {
        return remoteNoDataText(is_english_);
    }
    if (QDateTime::currentMSecsSinceEpoch() - remote_last_status_ms_ > 3000)
    {
        return remoteStaleText(is_english_);
    }
    if (remote_device_states_.value(device, VaporView::DeviceState::Disconnected) != VaporView::DeviceState::Connected)
    {
        return remoteDisconnectedText(is_english_);
    }
    const qint64 lastDataMs = remote_last_data_ms_.value(device, 0);
    if (lastDataMs <= 0)
    {
        return remoteNoDataText(is_english_);
    }
    if (QDateTime::currentMSecsSinceEpoch() - lastDataMs > timeout_ms)
    {
        return remoteStaleText(is_english_);
    }
    return remoteNoDataText(is_english_);
}

void MainWindow::noteRemotePacket(VaporView::MsgType type)
{
    const int key = static_cast<int>(type);
    QVector<qint64>& arrivals = remote_packet_arrivals_ms_[key];
    notePacketArrival(arrivals, QDateTime::currentMSecsSinceEpoch());
}

void MainWindow::noteRemoteWaveformPacket(quint16 channelId)
{
    QVector<qint64>& arrivals = remote_waveform_channel_arrivals_ms_[static_cast<int>(channelId)];
    notePacketArrival(arrivals, QDateTime::currentMSecsSinceEpoch());
}

double MainWindow::remotePacketRate(VaporView::MsgType type) const
{
    return packetRateFromArrivals(remote_packet_arrivals_ms_.value(static_cast<int>(type)));
}

double MainWindow::remoteWaveformPacketRate(quint16 channelId) const
{
    return packetRateFromArrivals(remote_waveform_channel_arrivals_ms_.value(static_cast<int>(channelId)));
}

MainWindow::RemoteTelemetrySummarySections MainWindow::remoteTelemetrySummarySections() const
{
    const bool connected = ground_telemetry_service_ && ground_telemetry_service_->isOpen();

    auto hasDeviceData = [this, connected](VaporView::SkyDeviceId device, qint64 timeoutMs) {
        return connected && remoteDeviceDataValid(device, timeoutMs);
    };

    auto hasText = [this](bool hasData) {
        return hasData
            ? (is_english_ ? QStringLiteral("data") : QStringLiteral("有数据"))
            : (is_english_ ? QStringLiteral("none") : QStringLiteral("无数据"));
    };

    const QString actualWaveRate = formatFrequencyText(
        connected ? static_cast<double>(remote_status_.wave_tcp_actual_rate_hz) : 0.0);
    const double rxBps = connected ? ground_telemetry_service_->receiveBitsPerSecond() : 0.0;
    const double txBps = connected ? ground_telemetry_service_->transmitBitsPerSecond() : 0.0;

    auto makeItem = [](const QString& label, const QString& value, bool hasData, const QString& valueWidthText = QString()) {
        RemoteTelemetrySummarySections::Item item;
        item.label = label;
        item.value = value;
        item.valueWidthText = valueWidthText;
        item.hasData = hasData;
        return item;
    };

    QList<RemoteTelemetrySummarySections::Item> rateRows;
    QList<RemoteTelemetrySummarySections::Item> linkRows;
    QList<RemoteTelemetrySummarySections::Item> deviceRows;
    const QString frequencyWidthText = QStringLiteral("999.9 Hz");
    const QString bitRateWidthText = QStringLiteral("999.9 Mbps");
    auto appendPacketRate = [&](VaporView::MsgType type, const QString& label) {
        const double rate = remotePacketRate(type);
        rateRows << makeItem(label, formatFrequencyText(rate), connected && rate > 0.0, frequencyWidthText);
    };
    auto appendWaveformRate = [&](quint16 channelId, const QString& label) {
        const double rate = remoteWaveformPacketRate(channelId);
        rateRows << makeItem(label, formatFrequencyText(rate), connected && rate > 0.0, frequencyWidthText);
    };
    auto appendDevice = [&](VaporView::SkyDeviceId device, qint64 timeoutMs, const QString& label) {
        const bool hasData = hasDeviceData(device, timeoutMs);
        deviceRows << makeItem(label, hasText(hasData), hasData);
    };
    if (is_english_)
    {
        appendPacketRate(VaporView::MsgType::TelemetryBasic, QStringLiteral("Basic:"));
        appendPacketRate(VaporView::MsgType::WaveformFeature, QStringLiteral("Feature:"));
        appendPacketRate(VaporView::MsgType::TelemetryStatus, QStringLiteral("Status:"));
        appendWaveformRate(1, QStringLiteral("Wave raw:"));
        appendWaveformRate(4, QStringLiteral("Wave harm.:"));
        rateRows << makeItem(QStringLiteral("Wave capture:"), actualWaveRate, connected && remote_status_.wave_tcp_actual_rate_hz > 0.0f, frequencyWidthText);
        linkRows << makeItem(QStringLiteral("Sky->Ground:"), formatBitRate(rxBps), connected && rxBps > 0.0, bitRateWidthText);
        linkRows << makeItem(QStringLiteral("Ground->Sky:"), formatBitRate(txBps), connected && txBps > 0.0, bitRateWidthText);
        linkRows << makeItem(QStringLiteral("Total:"), formatBitRate(rxBps + txBps), connected && (rxBps + txBps) > 0.0, bitRateWidthText);
        appendDevice(VaporView::SkyDeviceId::Epsilon, 2000, QStringLiteral("EPSILON:"));
        appendDevice(VaporView::SkyDeviceId::Ptb, 3000, QStringLiteral("PTB:"));
        appendDevice(VaporView::SkyDeviceId::Hmp, 3000, QStringLiteral("HMP:"));
        appendDevice(VaporView::SkyDeviceId::Lidar, 2000, QStringLiteral("Lidar:"));
        appendDevice(VaporView::SkyDeviceId::WaveTcp, 3000, QStringLiteral("Wave:"));
    }
    else
    {
        appendPacketRate(VaporView::MsgType::TelemetryBasic, QStringLiteral("基础:"));
        appendPacketRate(VaporView::MsgType::WaveformFeature, QStringLiteral("特征值:"));
        appendPacketRate(VaporView::MsgType::TelemetryStatus, QStringLiteral("状态:"));
        appendWaveformRate(1, QStringLiteral("原始波形:"));
        appendWaveformRate(4, QStringLiteral("谐波波形:"));
        rateRows << makeItem(QStringLiteral("波形采集:"), actualWaveRate, connected && remote_status_.wave_tcp_actual_rate_hz > 0.0f, frequencyWidthText);
        linkRows << makeItem(QStringLiteral("天空→地面:"), formatBitRate(rxBps), connected && rxBps > 0.0, bitRateWidthText);
        linkRows << makeItem(QStringLiteral("地面→天空:"), formatBitRate(txBps), connected && txBps > 0.0, bitRateWidthText);
        linkRows << makeItem(QStringLiteral("合计:"), formatBitRate(rxBps + txBps), connected && (rxBps + txBps) > 0.0, bitRateWidthText);
        appendDevice(VaporView::SkyDeviceId::Epsilon, 2000, QStringLiteral("EPSILON："));
        appendDevice(VaporView::SkyDeviceId::Ptb, 3000, QStringLiteral("PTB："));
        appendDevice(VaporView::SkyDeviceId::Hmp, 3000, QStringLiteral("HMP："));
        appendDevice(VaporView::SkyDeviceId::Lidar, 2000, QStringLiteral("Lidar："));
        appendDevice(VaporView::SkyDeviceId::WaveTcp, 3000, QStringLiteral("波形："));
    }

    RemoteTelemetrySummarySections sections;
    sections.rateItems = rateRows;
    sections.linkItems = linkRows;
    sections.deviceItems = deviceRows;
    return sections;
}

void MainWindow::updateRemoteTelemetrySummaryLabel()
{
    if (!data_telemetry_summary_card_ && !device_config_.data_telemetry_summary_card)
    {
        return;
    }
    const RemoteTelemetrySummarySections sections = remoteTelemetrySummarySections();
    auto clearLayout = [](QLayout *layout) {
        if (!layout)
        {
            return;
        }
        while (QLayoutItem *item = layout->takeAt(0))
        {
            if (QWidget *widget = item->widget())
            {
                delete widget;
            }
            else if (QLayout *childLayout = item->layout())
            {
                while (QLayoutItem *childItem = childLayout->takeAt(0))
                {
                    if (QWidget *childWidget = childItem->widget())
                    {
                        delete childWidget;
                    }
                    delete childItem;
                }
                delete childLayout;
            }
            delete item;
        }
    };
    auto renderSummarySection = [this, &clearLayout](QWidget *summaryParent,
                                                     QVBoxLayout *sectionLayout,
                                                     const QString& title,
                                                     const QList<RemoteTelemetrySummarySections::Item>& items,
                                                     int firstLineItemCount,
                                                     int followingLineItemCount = -1,
                                                     bool useSideTitle = false,
                                                     bool compactAvailabilityValues = false) {
        if (!summaryParent || !sectionLayout)
        {
            return;
        }
        clearLayout(sectionLayout);

        auto addItemLabel = [this, useSideTitle, compactAvailabilityValues](QHBoxLayout *lineLayout,
                                                                            QWidget *lineWidget,
                                                                            const RemoteTelemetrySummarySections::Item& item) {
            auto *pill = new QFrame(lineWidget);
            pill->setObjectName(QStringLiteral("homeTelemetrySummaryPill"));
            pill->setProperty("deviceConfigLink", useSideTitle);
            pill->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
            pill->setMinimumHeight(scalePixels(useSideTitle ? 26 : 28));
            auto *pillLayout = new QHBoxLayout(pill);
            const int horizontalPadding = scalePixels(useSideTitle ? 4 : 8);
            pillLayout->setContentsMargins(horizontalPadding, scalePixels(1), horizontalPadding, scalePixels(1));
            pillLayout->setSpacing(scalePixels(useSideTitle ? 2 : 3));

            auto *nameLabel = new QLabel(item.label, pill);
            nameLabel->setObjectName(QStringLiteral("homeTelemetrySummaryNameLabel"));
            nameLabel->setProperty("deviceConfigLink", useSideTitle);
            nameLabel->setProperty("telemetryAvailable", item.hasData);
            nameLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
            nameLabel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
            nameLabel->setTextFormat(Qt::PlainText);
            nameLabel->ensurePolished();
            nameLabel->setMinimumWidth(nameLabel->fontMetrics().horizontalAdvance(item.label) + scalePixels(1));
            nameLabel->setMinimumHeight(nameLabel->fontMetrics().height() + scalePixels(2));
            pillLayout->addWidget(nameLabel, 0, Qt::AlignVCenter);

            const QString compactValue = item.hasData
                ? (is_english_ ? QStringLiteral("Yes") : QStringLiteral("有"))
                : (is_english_ ? QStringLiteral("No") : QStringLiteral("无"));
            const QString valueText = compactAvailabilityValues ? compactValue : item.value;
            auto *valueLabel = new QLabel(valueText, pill);
            valueLabel->setObjectName(QStringLiteral("homeTelemetrySummaryValueLabel"));
            valueLabel->setProperty("deviceConfigLink", useSideTitle);
            valueLabel->setProperty("telemetryAvailable", item.hasData);
            valueLabel->setFont(numericFontFrom(valueLabel->font()));
            valueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            valueLabel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
            valueLabel->setTextFormat(Qt::PlainText);
            valueLabel->ensurePolished();
            valueLabel->setMinimumHeight(valueLabel->fontMetrics().height() + scalePixels(2));
            const QString widthValue = compactAvailabilityValues
                ? (is_english_ ? QStringLiteral("Yes") : QStringLiteral("有"))
                : (item.valueWidthText.isEmpty() ? valueText : item.valueWidthText);
            const int valueWidth = std::max(valueLabel->fontMetrics().horizontalAdvance(widthValue),
                                            valueLabel->fontMetrics().horizontalAdvance(valueText)) + scalePixels(2);
            valueLabel->setMinimumWidth(valueWidth);
            valueLabel->setMaximumWidth(valueWidth);
            pillLayout->addWidget(valueLabel, 0, Qt::AlignVCenter);
            if (!useSideTitle)
            {
                const int pillWidth = pillLayout->contentsMargins().left() +
                                      nameLabel->minimumWidth() +
                                      pillLayout->spacing() +
                                      valueLabel->minimumWidth() +
                                      pillLayout->contentsMargins().right();
                pill->setFixedWidth(pillWidth);
            }

            lineLayout->addWidget(pill, 0, Qt::AlignVCenter);
        };

        auto *lineParent = summaryParent;
        QVBoxLayout *linesLayout = sectionLayout;
        if (useSideTitle)
        {
            auto verticalTitleText = [](const QString& source) {
                if (source.contains(QLatin1Char(' ')))
                {
                    return source.split(QLatin1Char(' '), Qt::SkipEmptyParts).join(QLatin1Char('\n'));
                }

                QStringList characters;
                characters.reserve(source.size());
                for (const QChar ch : source)
                {
                    if (!ch.isSpace())
                    {
                        characters << QString(ch);
                    }
                }
                return characters.join(QLatin1Char('\n'));
            };

            auto *sectionBody = new QWidget(summaryParent);
            auto *sectionBodyLayout = new QHBoxLayout(sectionBody);
            sectionBodyLayout->setContentsMargins(0, 0, 0, 0);
            sectionBodyLayout->setSpacing(8);

            auto *titlePane = new QFrame(sectionBody);
            titlePane->setObjectName(QStringLiteral("deviceTelemetrySectionTitlePane"));
            titlePane->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
            titlePane->setMinimumWidth(kEpsilonSideTitleWidth);
            titlePane->setMaximumWidth(kEpsilonSideTitleWidth);
            auto *titlePaneLayout = new QVBoxLayout(titlePane);
            titlePaneLayout->setContentsMargins(4, 4, 4, 4);
            titlePaneLayout->setSpacing(0);

            auto *titleLabel = new QLabel(titlePane);
            titleLabel->setObjectName(QStringLiteral("deviceTelemetrySectionTitleLabel"));
            titleLabel->setProperty("plainTitle", title);
            titleLabel->setText(verticalTitleText(title));
            titleLabel->setAlignment(Qt::AlignCenter);
            titleLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
            titlePaneLayout->addWidget(titleLabel, 1, Qt::AlignCenter);
            sectionBodyLayout->addWidget(titlePane, 0);

            lineParent = new QWidget(sectionBody);
            auto *contentLayout = new QVBoxLayout(lineParent);
            contentLayout->setContentsMargins(0, 2, 6, 2);
            contentLayout->setSpacing(2);
            linesLayout = contentLayout;
            sectionBodyLayout->addWidget(lineParent, 1);
            sectionLayout->addWidget(sectionBody, 0, Qt::AlignTop);
        }

        int renderedLineCount = 0;
        auto addLine = [&](int begin, int end, bool includeTitle) {
            ++renderedLineCount;
            auto *line = new QWidget(lineParent);
            line->setFixedHeight(scalePixels(useSideTitle ? 26 : 28));
            auto *lineLayout = new QHBoxLayout(line);
            lineLayout->setContentsMargins(0, 0, 0, 0);
            lineLayout->setSpacing(scalePixels(useSideTitle ? 4 : 2));

            if (!useSideTitle && includeTitle && !title.isEmpty())
            {
                auto *titleLabel = new QLabel(line);
                titleLabel->setObjectName(QStringLiteral("homeTelemetrySummaryTitleLabel"));
                titleLabel->setProperty("skyTelemetryTitle", true);
                titleLabel->setText(title + (is_english_ ? QStringLiteral(":") : QStringLiteral("：")));
                titleLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
                titleLabel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
                titleLabel->setMinimumWidth(titleLabel->fontMetrics().horizontalAdvance(titleLabel->text()) + scalePixels(4));
                lineLayout->addWidget(titleLabel, 0, Qt::AlignVCenter);
            }

            for (int i = begin; i < end; ++i)
            {
                addItemLabel(lineLayout, line, items.at(i));
            }
            lineLayout->addStretch(1);
            linesLayout->addWidget(line, 0, Qt::AlignLeft | Qt::AlignTop);
        };

        const int itemCount = static_cast<int>(items.size());
        const int firstLineCount = (firstLineItemCount < 0 || firstLineItemCount >= itemCount)
            ? itemCount
            : firstLineItemCount;
        addLine(0, firstLineCount, true);

        if (firstLineCount < itemCount)
        {
            const int remainingLineCount = followingLineItemCount > 0
                ? followingLineItemCount
                : itemCount - firstLineCount;
            for (int begin = firstLineCount; begin < itemCount; begin += remainingLineCount)
            {
                addLine(begin, std::min(begin + remainingLineCount, itemCount), false);
            }
        }

        sectionLayout->invalidate();
        sectionLayout->activate();
        if (QWidget *section = qobject_cast<QWidget *>(sectionLayout->parent()))
        {
            const int rowHeight = scalePixels(useSideTitle ? 26 : 28);
            const int rowSpacing = scalePixels(2);
            const int rowMargins = useSideTitle
                ? scalePixels(4)
                : sectionLayout->contentsMargins().top() + sectionLayout->contentsMargins().bottom();
            const int borderAllowance = scalePixels(2);
            const int sectionHeight = rowMargins +
                                      (renderedLineCount * rowHeight) +
                                      (std::max(0, renderedLineCount - 1) * rowSpacing) +
                                      borderAllowance;
            section->setFixedHeight(sectionHeight);
            section->adjustSize();
            section->updateGeometry();
        }
    };
    if (data_telemetry_summary_card_)
    {
        data_telemetry_summary_card_->setVisible(true);
        renderSummarySection(data_telemetry_summary_card_,
                             data_telemetry_summary_layout_,
                             is_english_ ? QStringLiteral("Sky-ground data stream rates") : QStringLiteral("天地数据流频率"),
                             sections.rateItems,
                             3,
                             3);
        renderSummarySection(data_telemetry_summary_card_,
                             data_telemetry_link_summary_layout_,
                             is_english_ ? QStringLiteral("Link rate") : QStringLiteral("链路速率"),
                             sections.linkItems,
                             -1);
        renderSummarySection(data_telemetry_summary_card_,
                             data_telemetry_device_summary_layout_,
                             is_english_ ? QStringLiteral("Data") : QStringLiteral("数据"),
                             sections.deviceItems,
                             -1,
                             -1,
                             false,
                             true);
        if (QLayout *summaryLayout = data_telemetry_summary_card_->layout())
        {
            summaryLayout->invalidate();
            summaryLayout->activate();
        }
        data_telemetry_summary_card_->updateGeometry();
        updateHomeDeviceOverviewMinimumWidth();
    }
    if (device_config_.data_telemetry_summary_card)
    {
        device_config_.data_telemetry_summary_card->setVisible(true);
        renderSummarySection(device_config_.data_telemetry_summary_card,
                             device_config_.data_telemetry_rate_summary_layout,
                             is_english_ ? QStringLiteral("Data stream rates") : QStringLiteral("数据流频率"),
                             sections.rateItems,
                             2,
                             2,
                             true);
        renderSummarySection(device_config_.data_telemetry_summary_card,
                             device_config_.data_telemetry_link_summary_layout,
                             is_english_ ? QStringLiteral("Link rate") : QStringLiteral("链路速率"),
                             sections.linkItems,
                             1,
                             1,
                             true);
        renderSummarySection(device_config_.data_telemetry_summary_card,
                             device_config_.data_telemetry_device_summary_layout,
                             is_english_ ? QStringLiteral("Data") : QStringLiteral("数据"),
                             sections.deviceItems,
                             3,
                             3,
                             true,
                             true);
        if (QLayout *summaryLayout = device_config_.data_telemetry_summary_card->layout())
        {
            summaryLayout->invalidate();
            summaryLayout->activate();
        }
        device_config_.data_telemetry_summary_card->updateGeometry();
    }
    if (home_overview_splitter_)
    {
        updateConfigCardHeightForSourceMode();
    }
}

void MainWindow::setDeviceConfigEpsilonPacketRates(const std::map<uint8_t, int>& packetRates)
{
    for (QComboBox *combo : device_config_.epsilon_packet_rate_combos)
    {
        if (!combo)
        {
            continue;
        }
        const auto packetId = static_cast<uint8_t>(combo->property("epsilonPacketId").toUInt());
        const auto it = packetRates.find(packetId);
        if (it == packetRates.end())
        {
            continue;
        }
        const int index = combo->findData(it->second);
        if (index >= 0)
        {
            const QSignalBlocker blocker(combo);
            combo->setCurrentIndex(index);
        }
    }
}

std::map<uint8_t, int> MainWindow::deviceConfigEpsilonPacketRates() const
{
    const QString epsilonRateText = epsilon_rate_combo_ ? epsilon_rate_combo_->currentText() : QStringLiteral("100");
    const int groupedRateHz = effectiveRateOrDefault(epsilonRateText, kDefaultEpsilonSampleRateHz, 200);
    std::map<uint8_t, int> packetRates = groupedEpsilonPacketRates(groupedRateHz);
    for (QComboBox *combo : device_config_.epsilon_packet_rate_combos)
    {
        if (!combo)
        {
            continue;
        }
        const auto packetId = static_cast<uint8_t>(combo->property("epsilonPacketId").toUInt());
        const QVariant rateValue = combo->currentData();
        if (rateValue.isValid())
        {
            packetRates[packetId] = rateValue.toInt();
        }
    }
    return packetRates;
}

void MainWindow::syncDeviceConfigEpsilonPanelFromSettings()
{
    if (!device_config_.epsilon_config_card)
    {
        return;
    }

    const QString epsilonRateText = epsilon_rate_combo_ ? epsilon_rate_combo_->currentText() : QStringLiteral("100");
    const int groupedRateHz = effectiveRateOrDefault(epsilonRateText, kDefaultEpsilonSampleRateHz, 200);
    QSettings settings("VaporView", "MainWindow");
    const bool customEnabled = settings.value("epsilon_custom_packet_rates_enabled", false).toBool();
    const std::map<uint8_t, int> packetRates = customEnabled
        ? loadCustomEpsilonPacketRates(settings, groupedRateHz)
        : groupedEpsilonPacketRates(groupedRateHz);
    if (device_config_.epsilon_packet_custom_check)
    {
        const QSignalBlocker blocker(device_config_.epsilon_packet_custom_check);
        device_config_.epsilon_packet_custom_check->setChecked(customEnabled);
    }
    setDeviceConfigEpsilonPacketRates(packetRates);
}

void MainWindow::saveDeviceConfigEpsilonPacketRates(bool applyAfterSave)
{
    if (!device_config_.epsilon_config_card ||
        connection_attempt_in_progress_ ||
        port_detection_in_progress_ ||
        epsilon_reconfigure_in_progress_)
    {
        return;
    }

    const QString epsilonRateText = epsilon_rate_combo_ ? epsilon_rate_combo_->currentText() : QStringLiteral("100");
    const int groupedRateHz = effectiveRateOrDefault(epsilonRateText, kDefaultEpsilonSampleRateHz, 200);
    const std::map<uint8_t, int> groupedRates = groupedEpsilonPacketRates(groupedRateHz);
    const std::map<uint8_t, int> defaultRates = defaultEpsilonPacketRates();
    const std::map<uint8_t, int> savedPacketRates = deviceConfigEpsilonPacketRates();

    QSettings settings("VaporView", "MainWindow");
    for (const EpsilonPacketConfigOption& option : epsilonPacketConfigOptions())
    {
        const auto it = savedPacketRates.find(option.packet_id);
        settings.setValue(epsilonPacketRateSettingsKey(option.packet_id),
                          it != savedPacketRates.end() ? it->second : groupedRates.at(option.packet_id));
    }

    bool hasCustomOverrides = false;
    for (const auto& entry : savedPacketRates)
    {
        const auto groupedIt = groupedRates.find(entry.first);
        if (groupedIt != groupedRates.end() && groupedIt->second != entry.second)
        {
            hasCustomOverrides = true;
            break;
        }
    }
    const bool effectiveCustomEnabled =
        (device_config_.epsilon_packet_custom_check && device_config_.epsilon_packet_custom_check->isChecked()) ||
        hasCustomOverrides;
    settings.setValue("epsilon_custom_packet_rates_enabled", effectiveCustomEnabled);
    settings.setValue("epsilon_custom_packet_rates_user_saved", effectiveCustomEnabled);
    settings.remove("epsilon_last_config_signature");
    settings.remove("epsilon_last_config_apply_version");

    if (hasCustomOverrides &&
        device_config_.epsilon_packet_custom_check &&
        !device_config_.epsilon_packet_custom_check->isChecked())
    {
        const QSignalBlocker blocker(device_config_.epsilon_packet_custom_check);
        device_config_.epsilon_packet_custom_check->setChecked(true);
        log(is_english_
                ? "[EPSILON] Packet-rate overrides detected, so the custom packet-rate profile has been enabled automatically."
                : "[EPSILON] 检测到包频率已偏离分组模式，已自动启用自定义包频率配置。");
    }

    if (effectiveCustomEnabled)
    {
        log(QString(is_english_
                        ? ((savedPacketRates == defaultRates)
                               ? "[EPSILON] Recommended default packet-rate profile saved: %1"
                               : "[EPSILON] Custom packet-rate profile saved: %1")
                        : ((savedPacketRates == defaultRates)
                               ? "[EPSILON] 已保存推荐默认包频率配置: %1"
                               : "[EPSILON] 已保存自定义包频率配置: %1"))
                .arg(epsilonPacketRatesSummary(savedPacketRates)));
    }
    else
    {
        log(QString(is_english_
                        ? "[EPSILON] Custom packet-rate profile disabled. The grouped %1 Hz profile will be used."
                        : "[EPSILON] 已关闭自定义包频率，后续将使用分组 %1 Hz 配置。")
                .arg(groupedRateHz));
    }

    const QString selectText = is_english_ ? "-- Select --" : "-- 选择 --";
    const QString epsilonPort = epsilon_port_combo_ ? epsilon_port_combo_->currentText().trimmed() : QString();
    if (applyAfterSave &&
        !recording_thread_running_.load() &&
        !epsilonPort.isEmpty() &&
        epsilonPort != selectText &&
        !isRateUnspecified(epsilonRateText))
    {
        log(is_english_
                ? "[EPSILON] Applying the saved packet-rate profile now..."
                : "[EPSILON] 正在应用刚保存的包频率配置...");
        onReconfigureEpsilonClicked();
    }
    else
    {
        log(is_english_
                ? "[EPSILON] Packet-rate profile saved. It will be used on the next connect/reconfigure."
                : "[EPSILON] 包频率配置已保存，将在下次连接或重配时生效。");
    }
}

void MainWindow::updateEnvironmentStatusIcons(bool lidarValid, bool ptbValid, bool hmpValid)
{
    auto updateIcon = [this](QLabel *label, bool valid, const QString& zhName, const QString& enName) {
        if (!label)
        {
            return;
        }
        const QIcon icon = createLucideIcon(valid ? QStringLiteral("check") : QStringLiteral("circle-x"),
                                            valid ? toolbarColor(AppThemeColor::ToolbarGreen) : toolbarColor(AppThemeColor::ToolbarRed));
        label->setPixmap(icon.pixmap(QSize(kEnvStatusIconSize, kEnvStatusIconSize)));
        const QString name = is_english_ ? enName : zhName;
        const QString state = valid
            ? (is_english_ ? QStringLiteral("valid") : QStringLiteral("有效"))
            : (is_english_ ? QStringLiteral("no data") : QStringLiteral("无数据"));
        label->setToolTip(is_english_
            ? QStringLiteral("%1: %2").arg(name, state)
            : QStringLiteral("%1：%2").arg(name, state));
    };

    updateIcon(env_lidar_status_icon_, lidarValid, QStringLiteral("Lidar"), QStringLiteral("Lidar"));
    updateIcon(env_ptb_status_icon_, ptbValid, QStringLiteral("PTB"), QStringLiteral("PTB"));
    updateIcon(env_hmp_status_icon_, hmpValid, QStringLiteral("HMP"), QStringLiteral("HMP"));
}

void MainWindow::refreshRemoteSkyDataUi()
{
    VaporView::EpsilonData epsilon = current_epsilon_;
    VaporView::PtbData ptb = current_ptb_;
    VaporView::HmpData hmp = current_hmp_;
    VaporView::LidarData lidar = current_lidar_;

    const bool epsilonValid = remoteDeviceDataValid(VaporView::SkyDeviceId::Epsilon, 2000);
    const bool ptbValid = remoteDeviceDataValid(VaporView::SkyDeviceId::Ptb, 3000);
    const bool hmpValid = remoteDeviceDataValid(VaporView::SkyDeviceId::Hmp, 3000);
    const bool lidarValid = remoteDeviceDataValid(VaporView::SkyDeviceId::Lidar, 2000);

    epsilon.valid = epsilonValid;
    ptb.valid = ptbValid;
    hmp.valid = hmpValid;
    lidar.valid = lidarValid;
    if (!ptbValid) ptb.error_message = remoteDeviceInvalidText(VaporView::SkyDeviceId::Ptb, 3000).toStdString();
    if (!hmpValid) hmp.error_message = remoteDeviceInvalidText(VaporView::SkyDeviceId::Hmp, 3000).toStdString();
    if (!lidarValid) lidar.error_message = remoteDeviceInvalidText(VaporView::SkyDeviceId::Lidar, 2000).toStdString();

    const double basicRate = (remote_last_status_ms_ > 0 &&
                              QDateTime::currentMSecsSinceEpoch() - remote_last_status_ms_ <= 3000)
        ? remote_status_.telemetry_basic_rate_hz
        : 0.0;
    if (status_label_ && ground_telemetry_service_ && ground_telemetry_service_->isOpen())
    {
        if (remote_last_status_ms_ <= 0)
        {
            status_label_->setText(is_english_ ? "Remote Sky connected, waiting for status" : "天空端数传已连接，等待状态");
            status_label_->setProperty("status", "connecting");
            status_label_->style()->unpolish(status_label_);
            status_label_->style()->polish(status_label_);
        }
        else if (QDateTime::currentMSecsSinceEpoch() - remote_last_status_ms_ > 3000)
        {
            status_label_->setText(is_english_ ? "Remote Sky status timeout" : "天空端状态超时");
            status_label_->setProperty("status", "disconnected");
            status_label_->style()->unpolish(status_label_);
            status_label_->style()->polish(status_label_);
        }
    }
    if (epsilon_panel_) epsilon_panel_->updateRate(epsilonValid ? basicRate : 0.0);
    if (ptb_panel_) ptb_panel_->updateRate(ptbValid ? basicRate : 0.0);
    if (hmp_panel_) hmp_panel_->updateRate(hmpValid ? basicRate : 0.0);
    if (lidar_panel_) lidar_panel_->updateRate(lidarValid ? basicRate : 0.0);
    if (temperature_controller_panel_) temperature_controller_panel_->updateRate(remoteDeviceDataValid(VaporView::SkyDeviceId::TemperatureController, 3000) ? remote_status_.status_rate_hz : 0.0);
    if (epsilon_panel_) epsilon_panel_->updateData(epsilon);
    if (ptb_panel_) ptb_panel_->updateData(ptb);
    if (hmp_panel_) hmp_panel_->updateData(hmp);
    if (lidar_panel_) lidar_panel_->updateData(lidar);
    if (temperature_controller_panel_) temperature_controller_panel_->updateData(current_temperature_controller_);
    if (temperature_overview_panel_) temperature_overview_panel_->updateData(current_temperature_controller_);
    updateEnvironmentStatusIcons(lidarValid, ptbValid, hmpValid);
    updateRemoteTelemetrySummaryLabel();
    updateHomeDeviceStatusCapsules();
}

void MainWindow::requestRemoteWaveTcpConnection(bool connectRequested)
{
    remote_wave_stream_requested_ = false;
    remote_wave_stream_auto_start_ = connectRequested;
    if (ground_telemetry_service_ && ground_telemetry_service_->isOpen())
    {
        if (connectRequested)
        {
            remote_wave_stream_enable_pending_ = true;
            ground_telemetry_service_->sendCommand(VaporView::CommandId::EnableWaveformStreaming);
        }
        else
        {
            remote_wave_stream_enable_pending_ = false;
            ground_telemetry_service_->sendCommand(VaporView::CommandId::DisableWaveformStreaming);
        }
    }
    if (!connectRequested && tcp_wave_panel_)
    {
        remote_device_states_.insert(VaporView::SkyDeviceId::WaveTcp, VaporView::DeviceState::Disconnected);
        remote_last_data_ms_.remove(VaporView::SkyDeviceId::WaveTcp);
        tcp_wave_panel_->setRemoteWaveTcpState(VaporView::DeviceState::Disconnected);
        updateRemoteTelemetrySummaryLabel();
    }
    sendRemoteDeviceCommand(connectRequested ? VaporView::CommandId::ConnectDevice : VaporView::CommandId::DisconnectDevice,
                            VaporView::SkyDeviceId::WaveTcp);
    updateHomeDeviceStatusCapsules();
}

QPushButton *MainWindow::createRemoteDeviceButton(const QString& text, VaporView::CommandId command, VaporView::SkyDeviceId device)
{
    auto *button = new QPushButton(text, this);
    button->setFixedHeight(kMainPageButtonHeight);
    button->setMinimumWidth(60);
    const QString action = command == VaporView::CommandId::ConnectDevice
        ? QStringLiteral("连接")
        : command == VaporView::CommandId::DisconnectDevice
            ? QStringLiteral("断开")
            : QStringLiteral("重连");
    button->setToolTip(QStringLiteral("请求天空端%1 %2").arg(action, skyDeviceDisplayName(device)));
    connect(button, &QPushButton::clicked, this, [this, command, device]() {
        sendRemoteDeviceCommand(command, device);
    });
    return button;
}

void MainWindow::setRemoteDeviceButtonsEnabled(bool enabled)
{
    for (QPushButton *button : {epsilon_remote_connect_btn_, epsilon_remote_disconnect_btn_, epsilon_remote_reconnect_btn_,
                               ptb_remote_connect_btn_, ptb_remote_disconnect_btn_, ptb_remote_reconnect_btn_,
                               hmp_remote_connect_btn_, hmp_remote_disconnect_btn_, hmp_remote_reconnect_btn_,
                               lidar_remote_connect_btn_, lidar_remote_disconnect_btn_, lidar_remote_reconnect_btn_,
                               temperature_remote_connect_btn_, temperature_remote_disconnect_btn_, temperature_remote_reconnect_btn_})
    {
        if (button)
        {
            button->setEnabled(enabled);
        }
    }
    for (QWidget *widget : {epsilon_remote_buttons_widget_, ptb_remote_buttons_widget_, hmp_remote_buttons_widget_, lidar_remote_buttons_widget_, temperature_remote_buttons_widget_})
    {
        if (widget)
        {
            widget->setVisible(true);
        }
    }
}

void MainWindow::updateTemperatureControllerTitleText()
{
    if (!temperature_controller_inline_title_lbl_)
    {
        return;
    }

    const QString portText = temperature_port_combo_
        ? temperature_port_combo_->currentText().trimmed()
        : QString();
    const bool hasPort = !portText.isEmpty() && !portText.startsWith(QStringLiteral("--"));
    const QString base = is_english_
        ? QStringLiteral("RD105 Laser Driver Board Temperature Controller")
        : QStringLiteral("RD105激光驱动板温控器");
    const QString portDisplay = hasPort
        ? portText
        : (is_english_ ? QStringLiteral("No serial port") : QStringLiteral("未选择串口"));
    temperature_controller_inline_title_lbl_->setText(QStringLiteral("%1 · %2").arg(base, portDisplay));
    temperature_controller_inline_title_lbl_->setToolTip(is_english_
        ? QStringLiteral("Selected RD105 serial port: %1").arg(portDisplay)
        : QStringLiteral("当前 RD105 串口：%1").arg(portDisplay));
}

void MainWindow::updateTemperatureTitleButtonsState()
{
    const QString connectText = is_english_ ? QStringLiteral("Connect") : QStringLiteral("连接");
    const QString disconnectText = is_english_ ? QStringLiteral("Disconnect") : QStringLiteral("断开");
    const QString reconnectText = is_english_ ? QStringLiteral("Reconnect") : QStringLiteral("重连");
    if (temperature_remote_connect_btn_) temperature_remote_connect_btn_->setText(connectText);
    if (temperature_remote_disconnect_btn_) temperature_remote_disconnect_btn_->setText(disconnectText);
    if (temperature_remote_reconnect_btn_) temperature_remote_reconnect_btn_->setText(reconnectText);
    for (QPushButton *button : {temperature_remote_connect_btn_,
                                temperature_remote_disconnect_btn_,
                                temperature_remote_reconnect_btn_})
    {
        if (button)
        {
            fitButtonMinimumWidth(button, 64);
        }
    }

    if (isRemoteSkyMode())
    {
        const bool remoteCommandEnabled = ground_telemetry_service_ && ground_telemetry_service_->isOpen();
        if (temperature_remote_connect_btn_) temperature_remote_connect_btn_->setEnabled(remoteCommandEnabled);
        if (temperature_remote_disconnect_btn_) temperature_remote_disconnect_btn_->setEnabled(remoteCommandEnabled);
        if (temperature_remote_reconnect_btn_) temperature_remote_reconnect_btn_->setEnabled(remoteCommandEnabled);
        updateRemoteDeviceButtonText(VaporView::SkyDeviceId::TemperatureController,
                                     remote_device_states_.value(VaporView::SkyDeviceId::TemperatureController,
                                                                 VaporView::DeviceState::Disconnected));
        return;
    }

    const bool hasPort = homeDevicePortSelected(VaporView::SkyDeviceId::TemperatureController);
    const bool connected = homeDeviceConnected(VaporView::SkyDeviceId::TemperatureController);
    const bool canConnect = hasPort && !connected && connect_btn_ && connect_btn_->isEnabled();
    const bool canDisconnect = connected && disconnect_btn_ && disconnect_btn_->isEnabled();
    const bool canReconnect = hasPort && !connection_attempt_in_progress_ &&
        !port_detection_in_progress_ && !epsilon_reconfigure_in_progress_ &&
        (canConnect || canDisconnect);
    if (temperature_remote_connect_btn_) temperature_remote_connect_btn_->setEnabled(canConnect);
    if (temperature_remote_disconnect_btn_) temperature_remote_disconnect_btn_->setEnabled(canDisconnect);
    if (temperature_remote_reconnect_btn_) temperature_remote_reconnect_btn_->setEnabled(canReconnect);

    const QString portText = temperature_port_combo_
        ? temperature_port_combo_->currentText().trimmed()
        : QString();
    const QString portDisplay = hasPort
        ? portText
        : (is_english_ ? QStringLiteral("No serial port selected") : QStringLiteral("未选择串口"));
    if (temperature_remote_connect_btn_)
    {
        temperature_remote_connect_btn_->setToolTip(is_english_
            ? QStringLiteral("Connect the local RD105 on %1").arg(portDisplay)
            : QStringLiteral("连接本地 RD105：%1").arg(portDisplay));
    }
    if (temperature_remote_disconnect_btn_)
    {
        temperature_remote_disconnect_btn_->setToolTip(is_english_
            ? QStringLiteral("Disconnect the local RD105 on %1").arg(portDisplay)
            : QStringLiteral("断开本地 RD105：%1").arg(portDisplay));
    }
    if (temperature_remote_reconnect_btn_)
    {
        temperature_remote_reconnect_btn_->setToolTip(is_english_
            ? QStringLiteral("Reconnect the local RD105 on %1").arg(portDisplay)
            : QStringLiteral("重连本地 RD105：%1").arg(portDisplay));
    }
}

void MainWindow::handleTemperatureTitleButton(VaporView::CommandId command)
{
    if (isRemoteSkyMode())
    {
        sendRemoteDeviceCommand(command, VaporView::SkyDeviceId::TemperatureController);
        return;
    }

    if (command == VaporView::CommandId::DisconnectDevice)
    {
        if (disconnect_btn_ && disconnect_btn_->isEnabled())
        {
            disconnect_btn_->trigger();
        }
        return;
    }

    if (command == VaporView::CommandId::ReconnectDevice)
    {
        if (homeDeviceConnected(VaporView::SkyDeviceId::TemperatureController) &&
            disconnect_btn_ && disconnect_btn_->isEnabled())
        {
            disconnect_btn_->trigger();
            QTimer::singleShot(0, this, [this]() {
                if (connect_btn_ && connect_btn_->isEnabled())
                {
                    connect_btn_->trigger();
                }
            });
            return;
        }
        command = VaporView::CommandId::ConnectDevice;
    }

    if (command == VaporView::CommandId::ConnectDevice &&
        connect_btn_ && connect_btn_->isEnabled())
    {
        connect_btn_->trigger();
    }
}

void MainWindow::sendRemoteDeviceCommand(VaporView::CommandId command, VaporView::SkyDeviceId device)
{
    if (!ground_telemetry_service_ || !ground_telemetry_service_->isOpen())
    {
        log(is_english_ ? "Remote Sky telemetry link is not connected" : "天空端数传链路未连接");
        return;
    }
    ground_telemetry_service_->sendDeviceCommand(command, device);
}

void MainWindow::sendRemotePeakSearchRange(quint32 startIndex, quint32 endIndex)
{
    if (!ground_telemetry_service_ || !ground_telemetry_service_->isOpen())
    {
        log(is_english_ ? "Remote Sky telemetry link is not connected" : "天空端数传链路未连接");
        if (tcp_wave_panel_)
        {
            tcp_wave_panel_->rejectRemotePeakSearchRange(is_english_ ? QStringLiteral("link is not connected") : QStringLiteral("数传链路未连接"));
        }
        return;
    }
    const quint16 seq = ground_telemetry_service_->sendPeakSearchRangeCommand(startIndex, endIndex);
    VaporView::PeakSearchRange range;
    range.start_index = startIndex;
    range.end_index = endIndex;
    remote_peak_search_commands_.insert(seq, range);
    log(QString(is_english_
            ? "Peak search range sent to sky: [%1, %2), seq=%3"
            : "峰值搜索区间已下发到天空端：[%1, %2)，序号=%3")
            .arg(startIndex)
            .arg(endIndex == 0 ? QStringLiteral("end") : QString::number(endIndex))
            .arg(seq));
}

void MainWindow::sendTemperatureCommand(VaporView::CommandId command, const VaporView::TemperatureControllerCommand& payload)
{
    if (isRemoteSkyMode())
    {
        sendRemoteTemperatureCommand(command, payload);
        return;
    }

    const quint8 channel = payload.channel == 0 ? 1 : payload.channel;
    const CollectorSnapshot collectors = snapshotCollectors();
    if (collectors.temperature_controller && collectors.temperature_controller->isRunning())
    {
        if (temperature_controller_panel_)
        {
            temperature_controller_panel_->markCommandPending(command, payload);
        }
        auto applyConfirmedLocalCommand = [this, command, channel, &payload]() {
            current_temperature_controller_.valid = true;
            current_temperature_controller_.timestamp = std::chrono::steady_clock::now();
            const int channelIndex = static_cast<int>(channel == 0 ? 0 : channel - 1);
            if (channelIndex >= 0 && channelIndex < static_cast<int>(current_temperature_controller_.channels.size()))
            {
                auto& channelData = current_temperature_controller_.channels[channelIndex];
                switch (command)
                {
                case VaporView::CommandId::SetTemperatureTarget:
                    channelData.target_temperature_c = payload.target_temperature_c;
                    break;
                case VaporView::CommandId::SetTemperatureOutputEnabled:
                    channelData.output_enabled = payload.output_enabled;
                    break;
                case VaporView::CommandId::SetTemperatureOutputMode:
                    channelData.output_mode = static_cast<int>(payload.output_mode);
                    break;
                case VaporView::CommandId::SetTemperatureMaxOutputPercent:
                    channelData.max_output_percent = static_cast<int>(payload.max_output_percent);
                    break;
                case VaporView::CommandId::SetTemperaturePid:
                    channelData.kp = static_cast<int>(payload.kp);
                    channelData.ki = static_cast<int>(payload.ki);
                    channelData.kd = static_cast<int>(payload.kd);
                    break;
                case VaporView::CommandId::SetTemperatureAutoPid:
                    channelData.auto_pid_mode = static_cast<int>(payload.auto_pid_mode);
                    break;
                default:
                    break;
                }
            }
            switch (command)
            {
            case VaporView::CommandId::SetTemperatureControllerMode:
                current_temperature_controller_.controller_mode = static_cast<int>(payload.controller_mode);
                break;
            case VaporView::CommandId::SetTemperatureDeviceAddress:
                current_temperature_controller_.device_address = static_cast<int>(payload.device_address);
                QSettings(QStringLiteral("VaporView"), QStringLiteral("MainWindow"))
                    .setValue(QStringLiteral("serial/temperature_slave_address"), static_cast<int>(payload.device_address));
                break;
            case VaporView::CommandId::SetTemperatureRs485Baud:
                current_temperature_controller_.rs485_baud_index = static_cast<int>(payload.rs485_baud_index);
                {
                    const QString baudText = QString::number(temperatureRs485BaudRateForIndex(payload.rs485_baud_index));
                    QSettings(QStringLiteral("VaporView"), QStringLiteral("MainWindow"))
                        .setValue(QStringLiteral("serial/temperature_baud"), baudText);
                    if (temperature_baud_combo_)
                    {
                        const QSignalBlocker blocker(temperature_baud_combo_);
                        if (temperature_baud_combo_->findText(baudText) < 0)
                        {
                            temperature_baud_combo_->addItem(baudText);
                        }
                        temperature_baud_combo_->setCurrentText(baudText);
                    }
                }
                break;
            case VaporView::CommandId::SetTemperatureOvertempOutputMode:
                current_temperature_controller_.overtemp_output_mode = static_cast<int>(payload.overtemp_output_mode);
                break;
            case VaporView::CommandId::RestoreTemperatureFactoryDefaults:
                current_temperature_controller_.device_address = 1;
                current_temperature_controller_.rs485_baud_index = 1;
                current_temperature_controller_.overtemp_output_mode = 1;
                {
                    QSettings settings(QStringLiteral("VaporView"), QStringLiteral("MainWindow"));
                    settings.setValue(QStringLiteral("serial/temperature_slave_address"), 1);
                    settings.setValue(QStringLiteral("serial/temperature_baud"), QStringLiteral("9600"));
                    if (temperature_baud_combo_)
                    {
                        const QSignalBlocker blocker(temperature_baud_combo_);
                        temperature_baud_combo_->setCurrentText(QStringLiteral("9600"));
                    }
                }
                break;
            default:
                break;
            }
        };

        bool ok = false;
        switch (command)
        {
        case VaporView::CommandId::SetTemperatureTarget:
            ok = collectors.temperature_controller->setTargetTemperature(channel, payload.target_temperature_c);
            break;
        case VaporView::CommandId::SetTemperatureOutputEnabled:
            ok = collectors.temperature_controller->setOutputEnabled(channel, payload.output_enabled);
            break;
        case VaporView::CommandId::SetTemperatureOutputMode:
            ok = collectors.temperature_controller->setOutputMode(channel, payload.output_mode);
            break;
        case VaporView::CommandId::SetTemperatureMaxOutputPercent:
            ok = collectors.temperature_controller->setMaxOutputPercent(channel, payload.max_output_percent);
            break;
        case VaporView::CommandId::SetTemperaturePid:
            ok = collectors.temperature_controller->setPid(channel, payload.kp, payload.ki, payload.kd);
            break;
        case VaporView::CommandId::SetTemperatureAutoPid:
            ok = collectors.temperature_controller->setAutoPid(channel, payload.auto_pid_mode);
            break;
        case VaporView::CommandId::SetTemperatureControllerMode:
            ok = collectors.temperature_controller->setControllerMode(payload.controller_mode);
            break;
        case VaporView::CommandId::SetTemperatureDeviceAddress:
            ok = collectors.temperature_controller->setDeviceAddress(payload.device_address);
            break;
        case VaporView::CommandId::SetTemperatureRs485Baud:
            ok = collectors.temperature_controller->setRs485BaudIndex(payload.rs485_baud_index);
            break;
        case VaporView::CommandId::SetTemperatureOvertempOutputMode:
            ok = collectors.temperature_controller->setOvertempOutputMode(payload.overtemp_output_mode);
            break;
        case VaporView::CommandId::RestoreTemperatureFactoryDefaults:
            ok = collectors.temperature_controller->restoreFactoryDefaults();
            break;
        default:
            break;
        }

        if (ok)
        {
            const VaporView::TemperatureControllerData latest = collectors.temperature_controller->getLatestData();
            if (latest.valid && latest.timestamp >= current_temperature_controller_.timestamp)
            {
                current_temperature_controller_ = latest;
            }
            applyConfirmedLocalCommand();
            if (temperature_controller_panel_)
            {
                temperature_controller_panel_->clearCommandPending(command, channel);
                temperature_controller_panel_->setCommandStatus(temperatureCommandStatusText(command, channel, false));
                temperature_controller_panel_->updateData(current_temperature_controller_);
            }
            if (temperature_overview_panel_)
            {
                temperature_overview_panel_->updateData(current_temperature_controller_);
            }
            log(isTemperatureCommonCommand(command)
                    ? QString(is_english_
                          ? "RD105 local command confirmed: %1"
                          : "RD105 本地命令已确认：%1")
                          .arg(VaporView::commandIdName(command))
                    : QString(is_english_
                          ? "RD105 local command confirmed: %1 channel=%2"
                          : "RD105 本地命令已确认：%1 通道=%2")
                          .arg(VaporView::commandIdName(command))
                          .arg(channel));
            restoreTemperatureCommandUi(command, channel);
            return;
        }

        const QString failedDetail = is_english_
            ? QStringLiteral("write/read-back confirmation failed")
            : QStringLiteral("写入或读回确认失败");
        log(is_english_
            ? QStringLiteral("RD105 local command failed: write/read-back confirmation failed")
            : QStringLiteral("RD105 本地命令失败：写入或读回确认失败"));
        if (temperature_controller_panel_)
        {
            temperature_controller_panel_->clearCommandPending(command, channel);
            temperature_controller_panel_->setCommandStatus(
                temperatureCommandStatusText(command, channel, false, failedDetail),
                true);
        }
        const VaporView::TemperatureControllerData latest = collectors.temperature_controller->getLatestData();
        if (latest.timestamp >= current_temperature_controller_.timestamp)
        {
            current_temperature_controller_ = latest;
        }
        restoreTemperatureCommandUi(command, channel);
        return;
    }

    const QString detail = is_english_
        ? QStringLiteral("local RD105 controller is not connected")
        : QStringLiteral("本地 RD105 温控器未连接");
    log(is_english_
        ? QStringLiteral("Local RD105 temperature controller is not connected")
        : QStringLiteral("本地 RD105 温控器未连接，无法下发温控命令"));
    if (temperature_controller_panel_)
    {
        temperature_controller_panel_->clearCommandPending(command, channel);
        temperature_controller_panel_->setCommandStatus(
            temperatureCommandStatusText(command, channel, false, detail),
            true);
    }
    restoreTemperatureCommandUi(command, channel);
}

void MainWindow::sendRemoteTemperatureCommand(VaporView::CommandId command, const VaporView::TemperatureControllerCommand& payload)
{
    if (!ground_telemetry_service_ || !ground_telemetry_service_->isOpen())
    {
        log(is_english_ ? "Remote Sky telemetry link is not connected" : "天空端数传链路未连接");
        const quint8 channel = payload.channel == 0 ? 1 : payload.channel;
        if (temperature_controller_panel_)
        {
            temperature_controller_panel_->setCommandStatus(
                temperatureCommandStatusText(command,
                                             channel,
                                             false,
                                             is_english_ ? QStringLiteral("Remote Sky telemetry link is not connected")
                                                         : QStringLiteral("天空端数传链路未连接")),
                true);
        }
        restoreTemperatureCommandUi(command, channel);
        return;
    }
    const quint16 seq = ground_telemetry_service_->sendCommand(command, VaporView::TelemetryCodec::serializeTemperatureControllerCommand(payload));
    remote_temperature_commands_.insert(seq, payload);
    if (temperature_controller_panel_)
    {
        temperature_controller_panel_->markCommandPending(command, payload);
        temperature_controller_panel_->setCommandStatus(temperatureCommandStatusText(command, payload.channel, true));
    }
    restoreTemperatureCommandUi(command, payload.channel == 0 ? 1 : payload.channel);
    if (isTemperatureCommonCommand(command))
    {
        log(QString(is_english_
                ? "RD105 command sent: %1 seq=%2"
                : "RD105 命令已下发：%1 序号=%2")
                .arg(VaporView::commandIdName(command))
                .arg(seq));
    }
    else
    {
        log(QString(is_english_
                ? "RD105 command sent: %1 channel=%2 seq=%3"
                : "RD105 命令已下发：%1 通道=%2 序号=%3")
                .arg(VaporView::commandIdName(command))
                .arg(payload.channel)
                .arg(seq));
    }
}

void MainWindow::restoreTemperatureCommandUi(VaporView::CommandId command, quint8 channel)
{
    if (command != VaporView::CommandId::SetTemperatureOutputEnabled)
    {
        return;
    }

    const int channelIndex = static_cast<int>(channel == 0 ? 0 : channel - 1);
    if (channelIndex < 0 || channelIndex >= static_cast<int>(current_temperature_controller_.channels.size()))
    {
        return;
    }

    const bool outputEnabled =
        current_temperature_controller_.valid &&
        current_temperature_controller_.channels[channelIndex].output_enabled;
    if (temperature_controller_panel_)
    {
        temperature_controller_panel_->setOutputEnabledControl(static_cast<quint8>(channelIndex + 1), outputEnabled);
    }
    if (temperature_overview_panel_)
    {
        temperature_overview_panel_->updateData(current_temperature_controller_);
    }
}

bool MainWindow::isTemperatureCommand(VaporView::CommandId command) const
{
    return command == VaporView::CommandId::SetTemperatureTarget ||
           command == VaporView::CommandId::SetTemperatureOutputEnabled ||
           command == VaporView::CommandId::SetTemperatureOutputMode ||
           command == VaporView::CommandId::SetTemperatureMaxOutputPercent ||
           command == VaporView::CommandId::SetTemperaturePid ||
           command == VaporView::CommandId::SetTemperatureAutoPid ||
           command == VaporView::CommandId::SetTemperatureControllerMode ||
           command == VaporView::CommandId::SetTemperatureDeviceAddress ||
           command == VaporView::CommandId::SetTemperatureRs485Baud ||
           command == VaporView::CommandId::SetTemperatureOvertempOutputMode ||
           command == VaporView::CommandId::RestoreTemperatureFactoryDefaults;
}

QString MainWindow::temperatureCommandStatusText(VaporView::CommandId command, quint8 channel, bool pending, const QString& detail) const
{
    QString action;
    switch (command)
    {
    case VaporView::CommandId::SetTemperatureTarget:
        action = is_english_ ? QStringLiteral("target temperature") : QStringLiteral("目标温度");
        break;
    case VaporView::CommandId::SetTemperatureOutputEnabled:
        action = is_english_ ? QStringLiteral("output enable") : QStringLiteral("输出使能");
        break;
    case VaporView::CommandId::SetTemperatureOutputMode:
        action = is_english_ ? QStringLiteral("output mode") : QStringLiteral("输出模式");
        break;
    case VaporView::CommandId::SetTemperatureMaxOutputPercent:
        action = is_english_ ? QStringLiteral("max output") : QStringLiteral("最大输出");
        break;
    case VaporView::CommandId::SetTemperaturePid:
        action = is_english_ ? QStringLiteral("PID") : QStringLiteral("PID");
        break;
    case VaporView::CommandId::SetTemperatureAutoPid:
        action = is_english_ ? QStringLiteral("auto PID") : QStringLiteral("自动 PID");
        break;
    case VaporView::CommandId::SetTemperatureControllerMode:
        action = is_english_ ? QStringLiteral("controller mode") : QStringLiteral("温控器模式");
        break;
    case VaporView::CommandId::SetTemperatureDeviceAddress:
        action = is_english_ ? QStringLiteral("RS485 address") : QStringLiteral("485站号");
        break;
    case VaporView::CommandId::SetTemperatureRs485Baud:
        action = is_english_ ? QStringLiteral("RS485 baud") : QStringLiteral("485波特率");
        break;
    case VaporView::CommandId::SetTemperatureOvertempOutputMode:
        action = is_english_ ? QStringLiteral("over-temp output mode") : QStringLiteral("过温输出模式");
        break;
    case VaporView::CommandId::RestoreTemperatureFactoryDefaults:
        action = is_english_ ? QStringLiteral("factory reset") : QStringLiteral("恢复出厂设置");
        break;
    default:
        action = VaporView::commandIdName(command);
        break;
    }
    if (isTemperatureCommonCommand(command))
    {
        if (pending)
        {
            return is_english_
                ? QStringLiteral("%1 command sent; waiting for ACK and read-back confirmation...").arg(action)
                : QStringLiteral("%1命令已下发，等待 ACK 和读回确认...").arg(action);
        }
        if (detail.isEmpty())
        {
            return is_english_
                ? QStringLiteral("%1 command confirmed.").arg(action)
                : QStringLiteral("%1命令已确认成功。").arg(action);
        }
        return is_english_
            ? QStringLiteral("%1 command failed: %2").arg(action, detail)
            : QStringLiteral("%1命令失败：%2").arg(action, detail);
    }
    if (pending)
    {
        return is_english_
            ? QStringLiteral("Channel %1 %2 command sent; waiting for ACK and read-back confirmation...").arg(channel).arg(action)
            : QStringLiteral("通道%1%2命令已下发，等待 ACK 和读回确认...").arg(channel).arg(action);
    }
    if (detail.isEmpty())
    {
        return is_english_
            ? QStringLiteral("Channel %1 %2 command confirmed.").arg(channel).arg(action)
            : QStringLiteral("通道%1%2命令已确认成功。" ).arg(channel).arg(action);
    }
    return is_english_
        ? QStringLiteral("Channel %1 %2 command failed: %3").arg(channel).arg(action, detail)
        : QStringLiteral("通道%1%2命令失败：%3").arg(channel).arg(action, detail);
}

void MainWindow::updateRemoteDeviceButtonText(VaporView::SkyDeviceId device, VaporView::DeviceState state)
{
    QPushButton *connectButton = nullptr;
    QPushButton *disconnectButton = nullptr;
    QPushButton *reconnectButton = nullptr;
    switch (device)
    {
    case VaporView::SkyDeviceId::Epsilon:
        connectButton = epsilon_remote_connect_btn_; disconnectButton = epsilon_remote_disconnect_btn_; reconnectButton = epsilon_remote_reconnect_btn_;
        break;
    case VaporView::SkyDeviceId::Ptb:
        connectButton = ptb_remote_connect_btn_; disconnectButton = ptb_remote_disconnect_btn_; reconnectButton = ptb_remote_reconnect_btn_;
        break;
    case VaporView::SkyDeviceId::Hmp:
        connectButton = hmp_remote_connect_btn_; disconnectButton = hmp_remote_disconnect_btn_; reconnectButton = hmp_remote_reconnect_btn_;
        break;
    case VaporView::SkyDeviceId::Lidar:
        connectButton = lidar_remote_connect_btn_; disconnectButton = lidar_remote_disconnect_btn_; reconnectButton = lidar_remote_reconnect_btn_;
        break;
    case VaporView::SkyDeviceId::TemperatureController:
        connectButton = temperature_remote_connect_btn_; disconnectButton = temperature_remote_disconnect_btn_; reconnectButton = temperature_remote_reconnect_btn_;
        break;
    case VaporView::SkyDeviceId::WaveTcp:
        if (tcp_wave_panel_)
        {
            tcp_wave_panel_->setRemoteWaveTcpState(remote_wave_stream_requested_ && state == VaporView::DeviceState::Connected
                ? VaporView::DeviceState::Connected
                : VaporView::DeviceState::Disconnected);
        }
        updateHomeDeviceStatusCapsules();
        return;
    case VaporView::SkyDeviceId::All:
        return;
    }
    const QString stateText = VaporView::deviceStateName(state);
    if (connectButton) connectButton->setToolTip(QStringLiteral("请求天空端连接 %1（当前：%2）").arg(skyDeviceDisplayName(device), stateText));
    if (disconnectButton) disconnectButton->setToolTip(QStringLiteral("请求天空端断开 %1（当前：%2）").arg(skyDeviceDisplayName(device), stateText));
    if (reconnectButton) reconnectButton->setToolTip(QStringLiteral("请求天空端重连 %1（当前：%2）").arg(skyDeviceDisplayName(device), stateText));
    updateHomeDeviceStatusCapsules();
}

void MainWindow::setImuFormatSelection(const QString& format)
{
    applyComboText(imu_format_combo_, format);
}

void MainWindow::setImuBaudSelection(int baud)
{
    applyComboText(imu_baud_combo_, QString::number(baud));
}

void MainWindow::setImuRateSelection(int rate)
{
    applyComboText(imu_rate_combo_, QString::number(rate));
    imu_sample_rate_ = parseRate(imu_rate_combo_->currentText());
}

bool MainWindow::restartImuCollector(const std::shared_ptr<VaporView::ImuCollector>& collector, const QString& port, int baud, int rate)
{
    if (!collector)
    {
        return false;
    }

    collector->setSampleRate(rate);
    if (!collector->start(port.toStdString(), VaporView::SerialConfig::N81(baud)))
    {
        log(QString(is_english_ ? "[IMU] Failed to reopen IMU port: %1" : "[IMU] 重新打开 IMU 串口失败: %1")
                .arg(QString::fromStdString(collector->getLastError())));
        return false;
    }
    collector->setOutputMessageType(imu_format_combo_ ? imu_format_combo_->currentText().toStdString() : std::string("HI91"));
    if (!collector->checkDeviceResponse())
    {
        log(is_english_ ? "[IMU] No response after reopening IMU port" : "[IMU] 重新打开 IMU 串口后未收到设备响应");
        collector->stop();
        return false;
    }
    if (!collector->startStreaming())
    {
        log(is_english_ ? "[IMU] Failed to restart IMU data stream" : "[IMU] 重新启动 IMU 数据流失败");
        collector->stop();
        return false;
    }
    log(QString(is_english_ ? "[IMU] Reconnected at %1 baud, %2 Hz, %3" : "[IMU] 已按 %1 波特率、%2 Hz、%3 重新连接")
            .arg(baud)
            .arg(rate)
            .arg(imu_format_combo_ ? imu_format_combo_->currentText() : QStringLiteral("HI91")));
    return true;
}

bool MainWindow::applyImuDeviceProfile(const QString& requestedFormat, int requestedBaud, int requestedRate)
{
    if (connection_attempt_in_progress_ || port_detection_in_progress_)
    {
        return false;
    }

    const QString selectText = is_english_ ? "-- Select --" : "-- 选择 --";
    const QString port = imu_port_combo_ ? imu_port_combo_->currentText().trimmed() : QString();
    if (port.isEmpty() || port == selectText)
    {
        log(is_english_ ? "Select an IMU serial port first" : "请先选择 IMU 串口");
        return false;
    }

    bool baudOk = false;
    const int currentBaud = (imu_baud_combo_ ? imu_baud_combo_->currentText() : QStringLiteral("921600")).toInt(&baudOk);
    const int effectiveCurrentBaud = baudOk && currentBaud > 0 ? currentBaud : 921600;
    const QString currentFormat = imu_format_combo_ ? imu_format_combo_->currentText().trimmed().toUpper() : QStringLiteral("HI91");
    const int currentRate = parseRate(imu_rate_combo_ ? imu_rate_combo_->currentText() : QStringLiteral("200"));

    const QString targetFormat = requestedFormat.isEmpty() ? currentFormat : requestedFormat.trimmed().toUpper();
    const int targetBaud = requestedBaud > 0 ? requestedBaud : effectiveCurrentBaud;
    const int targetRate = requestedRate > 0 ? requestedRate : currentRate;
    const QString targetPeriod = imuRatePeriodText(targetRate);

    if ((targetFormat != QStringLiteral("HI91") && targetFormat != QStringLiteral("HI92")) || targetPeriod.isEmpty())
    {
        log(is_english_ ? "Unsupported IMU format or rate" : "IMU 输出格式或频率不受支持");
        return false;
    }

    setImuFormatSelection(targetFormat);
    setImuBaudSelection(targetBaud);
    setImuRateSelection(targetRate);
    saveRememberedInputState();

    const CollectorSnapshot collectors = snapshotCollectors();
    const auto imuCollector = collectors.imu;
    const bool collectorRunning = imuCollector && imuCollector->isRunning();

    auto sendCommand = [this](auto&& sender, const QString& command, int waitMs = 80) -> bool {
        const std::string stdCommand = command.toStdString();
        if (!sender(stdCommand, waitMs))
        {
            return false;
        }
        log(QString("[IMU 发送] %1").arg(command.trimmed()));
        return true;
    };

    bool configured = false;
    bool needRestart = false;

    if (collectorRunning)
    {
        imuCollector->setOutputMessageType(targetFormat.toStdString());
        if (!sendCommand([&](const std::string& cmd, int waitMs) { return imuCollector->sendAsciiCommand(cmd, waitMs); }, QStringLiteral("LOG HI91 ONTIME 0\r\n")))
        {
            return false;
        }
        if (!sendCommand([&](const std::string& cmd, int waitMs) { return imuCollector->sendAsciiCommand(cmd, waitMs); }, QStringLiteral("LOG HI92 ONTIME 0\r\n")))
        {
            return false;
        }
        if (!sendCommand([&](const std::string& cmd, int waitMs) { return imuCollector->sendAsciiCommand(cmd, waitMs); },
                         QStringLiteral("LOG %1 ONTIME %2\r\n").arg(targetFormat, targetPeriod)))
        {
            return false;
        }
        if (!sendCommand([&](const std::string& cmd, int waitMs) { return imuCollector->sendAsciiCommand(cmd, waitMs); },
                         QStringLiteral("SAVECONFIG\r\n"), 120))
        {
            return false;
        }
        configured = true;

        if (targetBaud != effectiveCurrentBaud)
        {
            if (!sendCommand([&](const std::string& cmd, int waitMs) { return imuCollector->sendAsciiCommand(cmd, waitMs); },
                             QStringLiteral("SERIALCONFIG %1\r\n").arg(targetBaud), 150))
            {
                return false;
            }
            needRestart = true;
        }
    }
    else
    {
        VaporView::SerialPort tempPort;
        if (!tempPort.open(port.toStdString(), VaporView::SerialConfig::N81(effectiveCurrentBaud)))
        {
            log(QString(is_english_
                ? "[IMU] Unable to open %1 for direct configuration, saved for next connection"
                : "[IMU] 无法打开 %1 直接配置，已保存到下次连接时应用").arg(port));
            return true;
        }

        auto directSend = [&](const std::string& cmd, int waitMs) -> bool {
            const bool ok = tempPort.write(cmd.c_str(), cmd.size()) == static_cast<ssize_t>(cmd.size());
            if (ok && waitMs > 0)
            {
                QThread::msleep(waitMs);
            }
            return ok;
        };

        if (!sendCommand(directSend, QStringLiteral("LOG HI91 ONTIME 0\r\n")))
        {
            return false;
        }
        if (!sendCommand(directSend, QStringLiteral("LOG HI92 ONTIME 0\r\n")))
        {
            return false;
        }
        if (!sendCommand(directSend, QStringLiteral("LOG %1 ONTIME %2\r\n").arg(targetFormat, targetPeriod)))
        {
            return false;
        }
        if (!sendCommand(directSend, QStringLiteral("SAVECONFIG\r\n"), 120))
        {
            return false;
        }
        configured = true;
        if (targetBaud != effectiveCurrentBaud)
        {
            if (!sendCommand(directSend, QStringLiteral("SERIALCONFIG %1\r\n").arg(targetBaud), 150))
            {
                return false;
            }
            tempPort.close();
            if (tempPort.open(port.toStdString(), VaporView::SerialConfig::N81(targetBaud)))
            {
                if (!sendCommand(directSend, QStringLiteral("SAVECONFIG\r\n"), 120))
                {
                    return false;
                }
            }
        }
        tempPort.close();
    }

    if (collectorRunning && needRestart)
    {
        imuCollector->stop();
        if (!restartImuCollector(imuCollector, port, targetBaud, targetRate))
        {
            return false;
        }
        if (!imuCollector->sendAsciiCommand("SAVECONFIG\r\n", 120))
        {
            log(is_english_ ? "[IMU] Failed to persist baud rate after reconnect" : "[IMU] 重连后保存波特率配置失败");
        }
    }
    else if (collectorRunning)
    {
        imuCollector->setSampleRate(targetRate);
    }

    if (configured)
    {
        log(QString(is_english_
            ? "IMU profile applied: %1, %2 baud, %3 Hz"
            : "IMU 配置已应用: %1, %2 波特率, %3 Hz")
            .arg(targetFormat)
            .arg(targetBaud)
            .arg(targetRate));
    }
    return configured;
}

void MainWindow::setupMenuBar()
{
    data_menu_ = menuBar()->addMenu("");

    recording_directory_action_ = new QAction(this);
    connect(recording_directory_action_, &QAction::triggered, this, &MainWindow::onChooseRecordingDirectoryClicked);
    data_menu_->addAction(recording_directory_action_);

    recording_rate_menu_ = data_menu_->addMenu("");
    rebuildRecordingRateMenu();

    devices_menu_ = menuBar()->addMenu("");

    epsilon_packet_rates_action_ = new QAction(this);
    connect(epsilon_packet_rates_action_, &QAction::triggered, this, &MainWindow::onConfigureEpsilonPacketRatesClicked);
    devices_menu_->addAction(epsilon_packet_rates_action_);

    epsilon_rtcm_port_action_ = new QAction(this);
    connect(epsilon_rtcm_port_action_, &QAction::triggered, this, &MainWindow::onConfigureEpsilonRtcmPortClicked);
    devices_menu_->addAction(epsilon_rtcm_port_action_);

    epsilon_reconfigure_action_ = new QAction(this);
    connect(epsilon_reconfigure_action_, &QAction::triggered, this, &MainWindow::onReconfigureEpsilonClicked);
    devices_menu_->addAction(epsilon_reconfigure_action_);

    session_viewer_action_ = new QAction(this);
    connect(session_viewer_action_, &QAction::triggered, this, &MainWindow::onOpenSessionViewerClicked);

    view_menu_ = menuBar()->addMenu("");
    view_menu_->addAction(session_viewer_action_);

#ifdef VAPORVIEW_HAS_OSGEARTH
    map3d_action_ = new QAction(this);
    connect(map3d_action_, &QAction::triggered, this, &MainWindow::onOpenMap3DWindowClicked);

    map3d_diagnostics_action_ = new QAction(this);
    connect(map3d_diagnostics_action_, &QAction::triggered, this, &MainWindow::onOpenMap3DDiagnosticsClicked);

    view_menu_->addAction(map3d_action_);
    view_menu_->addAction(map3d_diagnostics_action_);
#endif

    exit_action_ = new QAction(this);
    exit_action_->setShortcut(QKeySequence::Quit);
    connect(exit_action_, &QAction::triggered, this, &QMainWindow::close);
    data_menu_->addAction(exit_action_);

    font_menu_ = menuBar()->addMenu("");
    font_scale_group_ = new QActionGroup(this);
    font_scale_group_->setExclusive(true);

    font_tiny_action_ = new QAction(this);
    font_tiny_action_->setData(70);
    font_scale_group_->addAction(font_tiny_action_);
    font_menu_->addAction(font_tiny_action_);

    font_extra_small_action_ = new QAction(this);
    font_extra_small_action_->setData(80);
    font_scale_group_->addAction(font_extra_small_action_);
    font_menu_->addAction(font_extra_small_action_);

    font_small_action_ = new QAction(this);
    font_small_action_->setData(90);
    font_scale_group_->addAction(font_small_action_);
    font_menu_->addAction(font_small_action_);

    font_normal_action_ = new QAction(this);
    font_normal_action_->setData(100);
    font_scale_group_->addAction(font_normal_action_);
    font_menu_->addAction(font_normal_action_);

    font_large_action_ = new QAction(this);
    font_large_action_->setData(115);
    font_scale_group_->addAction(font_large_action_);
    font_menu_->addAction(font_large_action_);

    font_extra_large_action_ = new QAction(this);
    font_extra_large_action_->setData(130);
    font_scale_group_->addAction(font_extra_large_action_);
    font_menu_->addAction(font_extra_large_action_);

    connect(font_scale_group_, &QActionGroup::triggered, this, &MainWindow::onFontScaleTriggered);

    updateFontScaleMenuCheckIcons();

    language_menu_ = menuBar()->addMenu("");

    lang_action_ = new QAction(this);
    lang_action_->setIcon(createLanguageIcon());
    connect(lang_action_, &QAction::triggered, this, &MainWindow::onSwitchLanguage);
    language_menu_->addAction(lang_action_);

    help_menu_ = menuBar()->addMenu("");

    about_action_ = new QAction(this);
    connect(about_action_, &QAction::triggered, this, &MainWindow::showAboutDialog);
    help_menu_->addAction(about_action_);
}

void MainWindow::setupToolBar()
{
    refresh_ports_btn_ = new QAction(this);
    refresh_ports_btn_->setIcon(createRefreshIcon());
    connect(refresh_ports_btn_, &QAction::triggered, this, &MainWindow::onRefreshPortsClicked);

    connect_btn_ = new QAction(this);
    connect_btn_->setIcon(createConnectIcon());
    connect(connect_btn_, &QAction::triggered, this, &MainWindow::onConnectClicked);

    cancel_connect_btn_ = new QAction(this);
    cancel_connect_btn_->setIcon(createCancelIcon());
    cancel_connect_btn_->setEnabled(false);
    connect(cancel_connect_btn_, &QAction::triggered, this, &MainWindow::onCancelConnectClicked);

    disconnect_btn_ = new QAction(this);
    disconnect_btn_->setIcon(createDisconnectIcon());
    disconnect_btn_->setEnabled(false);
    connect(disconnect_btn_, &QAction::triggered, this, &MainWindow::onDisconnectClicked);

    scheduled_recording_action_ = new QAction(this);
    scheduled_recording_action_->setIcon(createTimerIcon());
    connect(scheduled_recording_action_, &QAction::triggered, this, &MainWindow::onScheduledRecordingClicked);

    start_recording_btn_ = new QAction(this);
    start_recording_btn_->setIcon(createPlayIcon());
    start_recording_btn_->setEnabled(false);
    connect(start_recording_btn_, &QAction::triggered, this, &MainWindow::onStartRecordingClicked);

    pause_recording_btn_ = new QAction(this);
    pause_recording_btn_->setIcon(createPauseIcon());
    pause_recording_btn_->setEnabled(false);
    connect(pause_recording_btn_, &QAction::triggered, this, &MainWindow::onPauseRecordingClicked);

    stop_recording_btn_ = new QAction(this);
    stop_recording_btn_->setIcon(createStopIcon());
    stop_recording_btn_->setEnabled(false);
    connect(stop_recording_btn_, &QAction::triggered, this, &MainWindow::onStopRecordingClicked);

    rtk_config_action_ = new QAction(this);
    updateRtkConfigIcon();
    connect(rtk_config_action_, &QAction::triggered, this, &MainWindow::onRtkConfigClicked);
    if (devices_menu_)
    {
        devices_menu_->addAction(rtk_config_action_);
    }

    clear_log_action_ = new QAction(this);
    clear_log_action_->setIcon(createClearLogIcon());
    connect(clear_log_action_, &QAction::triggered, this, &MainWindow::onClearLogClicked);

    log_filter_menu_ = new SingleLevelPopupMenu(this);
    log_filter_menu_->setObjectName(QStringLiteral("logFilterMenu"));
    log_filter_menu_->setPanelPadding(12);
    log_filter_menu_->setCornerRadius(10);
    log_filter_menu_->refreshTheme();

    auto createLogFilterAction = [this](bool *enabled) {
        auto *row = new SingleLevelPopupMenuRow(log_filter_menu_);
        row->setTextAlignment(SingleLevelPopupTextAlignment::Left);
        row->setHorizontalPadding(scalePixels(18), scalePixels(14));
        row->setRowSpacing(scalePixels(6));
        row->setCheckSlotWidth(scalePixels(18));
        row->setCheckIconSize(QSize(scalePixels(16), scalePixels(16)));
        row->setRowHeight(scalePixels(36));
        row->setMinimumRowWidth(scalePixels(120));
        row->setCloseOnClick(true);
        auto *action = log_filter_menu_->addRow(row);
        action->setCheckable(false);
        connect(action, &QAction::triggered, this, [this, enabled]() {
            *enabled = !*enabled;
            updateLogFilterAction();
            renderLogView();
        });
        return action;
    };

    log_filter_ack_action_ = createLogFilterAction(&log_filter_ack_enabled_);
    log_filter_config_action_ = createLogFilterAction(&log_filter_config_enabled_);
    log_filter_connection_action_ = createLogFilterAction(&log_filter_connection_enabled_);
    log_filter_recording_action_ = createLogFilterAction(&log_filter_recording_enabled_);

    session_viewer_action_->setIcon(createWaveformViewerIcon());
#ifdef VAPORVIEW_HAS_OSGEARTH
    if (map3d_action_)
    {
        map3d_action_->setIcon(QIcon(QStringLiteral("resources/lucide/earth.svg")));
    }
    if (map3d_diagnostics_action_)
    {
        map3d_diagnostics_action_->setIcon(createLucideIcon(QStringLiteral("activity"), toolbarColor(AppThemeColor::ToolbarBlue)));
    }
#endif

    theme_toggle_action_ = new QAction(this);
    connect(theme_toggle_action_, &QAction::triggered, this, &MainWindow::onToggleTheme);

    setupCustomTitleBar();
    updateThemeAction();
}

void MainWindow::setupCustomTitleBar()
{
    custom_title_bar_ = new QWidget(this);
    custom_title_bar_->setObjectName(QStringLiteral("customTitleBar"));
    custom_title_bar_->installEventFilter(this);

    auto *titleLayout = new QHBoxLayout(custom_title_bar_);
    titleLayout->setContentsMargins(kAppSidebarVisualPadding, 0, 8, 0);
    titleLayout->setSpacing(6);

    custom_logo_label_ = new QLabel(custom_title_bar_);
    custom_logo_label_->setObjectName(QStringLiteral("customTitleLogo"));
    custom_logo_label_->setFixedSize(24, 24);
    custom_logo_label_->setAlignment(Qt::AlignCenter);
    custom_logo_label_->setCursor(Qt::PointingHandCursor);
    custom_logo_label_->setFocusPolicy(Qt::StrongFocus);
    custom_logo_label_->setAttribute(Qt::WA_Hover, true);
    custom_logo_label_->setProperty(kCustomLogoStateProperty, QStringLiteral("logo"));
    custom_logo_label_->setProperty("titleBarHover", false);
    custom_logo_label_->installEventFilter(this);
    titleLayout->addWidget(custom_logo_label_, 0, Qt::AlignVCenter);

    title_menu_btn_ = createTitleBarIconButton(QStringLiteral("titleBarMenuButton"), custom_title_bar_);
    connect(title_menu_btn_, &QToolButton::clicked, this, &MainWindow::showTitleApplicationMenu);
    titleLayout->addWidget(title_menu_btn_, 0, Qt::AlignVCenter);

    custom_title_label_ = new QLabel(QStringLiteral("VaporView"), custom_title_bar_);
    custom_title_label_->setObjectName(QStringLiteral("customTitleLabel"));
    custom_title_label_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    custom_title_label_->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
    custom_title_label_->installEventFilter(this);
    titleLayout->addWidget(custom_title_label_, 0);

    titleLayout->addWidget(createTitleBarActionButton(refresh_ports_btn_, custom_title_bar_), 0, Qt::AlignVCenter);
    addTitleBarSeparator(titleLayout);
    titleLayout->addWidget(createTitleBarActionButton(connect_btn_, custom_title_bar_), 0, Qt::AlignVCenter);
    titleLayout->addWidget(createTitleBarActionButton(cancel_connect_btn_, custom_title_bar_), 0, Qt::AlignVCenter);
    titleLayout->addWidget(createTitleBarActionButton(disconnect_btn_, custom_title_bar_), 0, Qt::AlignVCenter);
    addTitleBarSeparator(titleLayout);
    titleLayout->addWidget(createTitleBarActionButton(scheduled_recording_action_, custom_title_bar_), 0, Qt::AlignVCenter);
    titleLayout->addWidget(createTitleBarActionButton(start_recording_btn_, custom_title_bar_), 0, Qt::AlignVCenter);
    titleLayout->addWidget(createTitleBarActionButton(pause_recording_btn_, custom_title_bar_), 0, Qt::AlignVCenter);
    titleLayout->addWidget(createTitleBarActionButton(stop_recording_btn_, custom_title_bar_), 0, Qt::AlignVCenter);
    addTitleBarSeparator(titleLayout);
    titleLayout->addWidget(createTitleBarActionButton(session_viewer_action_, custom_title_bar_), 0, Qt::AlignVCenter);
#ifdef VAPORVIEW_HAS_OSGEARTH
    titleLayout->addWidget(createTitleBarActionButton(map3d_action_, custom_title_bar_), 0, Qt::AlignVCenter);
    titleLayout->addWidget(createTitleBarActionButton(map3d_diagnostics_action_, custom_title_bar_), 0, Qt::AlignVCenter);
#endif
    addTitleBarSeparator(titleLayout);
    title_language_btn_ = createTitleBarIconButton(QStringLiteral("titleBarButton"), custom_title_bar_);
    title_language_btn_->setAccessibleName(QStringLiteral("titleLanguageButton"));
    connect(title_language_btn_, &QToolButton::clicked, this, &MainWindow::onSwitchLanguage);
    titleLayout->addWidget(title_language_btn_, 0, Qt::AlignVCenter);
    titleLayout->addWidget(createTitleBarActionButton(theme_toggle_action_, custom_title_bar_), 0, Qt::AlignVCenter);
    titleLayout->addStretch(1);
    log_side_panel_toggle_btn_ = createTitleBarIconButton(QStringLiteral("titleBarButton"), custom_title_bar_);
    log_side_panel_toggle_btn_->setAccessibleName(QStringLiteral("logSidePanelToggleButton"));
    connect(log_side_panel_toggle_btn_, &QToolButton::clicked, this, &MainWindow::toggleLogSidePanel);
    titleLayout->addWidget(log_side_panel_toggle_btn_, 0, Qt::AlignVCenter);
    addTitleBarSeparator(titleLayout);

    window_minimize_btn_ = createTitleBarIconButton(QStringLiteral("windowMinimizeButton"), custom_title_bar_);
    connect(window_minimize_btn_, &QToolButton::clicked, this, &QWidget::showMinimized);
    titleLayout->addWidget(window_minimize_btn_, 0, Qt::AlignVCenter);

    window_maximize_btn_ = createTitleBarIconButton(QStringLiteral("windowMaximizeButton"), custom_title_bar_);
    connect(window_maximize_btn_, &QToolButton::clicked, this, &MainWindow::toggleWindowMaximized);
    titleLayout->addWidget(window_maximize_btn_, 0, Qt::AlignVCenter);

    window_close_btn_ = createTitleBarIconButton(QStringLiteral("windowCloseButton"), custom_title_bar_);
    connect(window_close_btn_, &QToolButton::clicked, this, &QWidget::close);
    titleLayout->addWidget(window_close_btn_, 0, Qt::AlignVCenter);

    menuBar()->hide();
    setMenuWidget(custom_title_bar_);
    updateCustomTitleBarTexts();
    updateCustomTitleBarStyle();
}

QToolButton *MainWindow::createTitleBarActionButton(QAction *action, QWidget *parent)
{
    auto *button = new QToolButton(parent);
    button->setObjectName(QStringLiteral("titleBarButton"));
    button->setToolButtonStyle(Qt::ToolButtonIconOnly);
    button->setAutoRaise(false);
    button->setFocusPolicy(Qt::NoFocus);
    button->setProperty(kTitleBarHoverProperty, false);
    configureHoverParticipant(button, kTitleBarHoverParticipantProperty, this);
    if (!action)
    {
        button->setEnabled(false);
        return button;
    }

    auto syncFromAction = [button, action]() {
        button->setIcon(action->icon());
        button->setEnabled(action->isEnabled());
        button->setVisible(action->isVisible());
        button->setCheckable(action->isCheckable());
        button->setChecked(action->isChecked());
        button->setToolTip(action->toolTip());
        button->setStatusTip(action->statusTip());
        button->setWhatsThis(action->whatsThis());
        button->setProperty(kTooltipShortcutProperty, shortcutTextFromAction(action));
    };
    syncFromAction();
    connect(action, &QAction::changed, button, syncFromAction);
    connect(button, &QToolButton::clicked, action, [action]() {
        if (action && action->isEnabled())
        {
            action->trigger();
        }
    });
    return button;
}

QToolButton *MainWindow::createTitleBarIconButton(const QString& objectName, QWidget *parent)
{
    auto *button = new QToolButton(parent);
    button->setObjectName(objectName);
    button->setToolButtonStyle(Qt::ToolButtonIconOnly);
    button->setAutoRaise(false);
    button->setFocusPolicy(Qt::NoFocus);
    button->setProperty(kTitleBarHoverProperty, false);
    configureHoverParticipant(button, kTitleBarHoverParticipantProperty, this);
    return button;
}

void MainWindow::addTitleBarSeparator(QHBoxLayout *layout)
{
    auto *separator = new QFrame(custom_title_bar_);
    separator->setObjectName(QStringLiteral("titleBarSeparator"));
    separator->setFixedWidth(1);
    separator->setFixedHeight(scalePixels(28));
    separator->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    layout->addWidget(separator, 0, Qt::AlignVCenter);
}

void MainWindow::discardTitleApplicationMenuPanel()
{
    if (!title_application_panel_ && !title_application_sub_panel_ && !title_application_nested_panel_)
    {
        return;
    }

    QFrame *panel = title_application_panel_;
    QFrame *subPanel = title_application_sub_panel_;
    QFrame *nestedPanel = title_application_nested_panel_;
    title_application_panel_ = nullptr;
    title_application_sub_panel_ = nullptr;
    title_application_nested_panel_ = nullptr;
    if (panel)
    {
        panel->hide();
        panel->deleteLater();
    }
    if (subPanel)
    {
        subPanel->hide();
        subPanel->deleteLater();
    }
    if (nestedPanel)
    {
        nestedPanel->hide();
        nestedPanel->deleteLater();
    }
}

void MainWindow::createTitleApplicationMenuPanel()
{
    if (title_application_panel_ || !central_widget_)
    {
        return;
    }

    auto *panel = new FloatingTitleMenuPanel(this);
    panel->setObjectName(QStringLiteral("titleApplicationPanel"));
    panel->hide();
    title_application_panel_ = panel;
    panel->raise();

    auto *subPanel = new FloatingTitleMenuPanel(this);
    subPanel->setObjectName(QStringLiteral("titleApplicationSubPanel"));
    subPanel->hide();
    title_application_sub_panel_ = subPanel;
    subPanel->raise();

    auto *nestedPanel = new FloatingTitleMenuPanel(this);
    nestedPanel->setObjectName(QStringLiteral("titleApplicationNestedPanel"));
    nestedPanel->hide();
    title_application_nested_panel_ = nestedPanel;
    nestedPanel->raise();

    auto closePanel = [this]() {
        if (title_application_panel_)
        {
            title_application_panel_->hide();
        }
        if (title_application_sub_panel_)
        {
            title_application_sub_panel_->hide();
        }
        if (title_application_nested_panel_)
        {
            title_application_nested_panel_->hide();
        }
    };

    const bool uiBusy = connection_attempt_in_progress_ || port_detection_in_progress_ || epsilon_reconfigure_in_progress_;

    struct TitleMenuCommand
    {
        QString text;
        QString shortcut;
        bool enabled = true;
        bool checked = false;
        bool separatorBefore = false;
        std::function<void()> handler;
        QVector<TitleMenuCommand> submenu;
    };
    struct TitleMenuSection
    {
        QString title;
        QVector<TitleMenuCommand> commands;
    };

    QFont menuFont = qApp->font();
    menuFont.setPixelSize(std::max(1, scalePixels(16)));
    menuFont.setWeight(QFont::Medium);
    panel->setFont(menuFont);
    subPanel->setFont(menuFont);
    const QFontMetrics menuMetrics(menuFont);
    const int rowVerticalPadding = scalePixels(4);
    const int rowHeight = std::max(scalePixels(28), menuMetrics.height() + rowVerticalPadding * 2);
    const int menuVerticalPadding = scalePixels(12);
    panel->setStyleSheet(titleApplicationPanelStyleSheet(dark_theme_enabled_));
    const int rowLeftPadding = scalePixels(18);
    const int rowRightPadding = scalePixels(14);
    const int rowSpacing = scalePixels(6);
    const int checkColumnWidth = scalePixels(18);
    const int checkIconSize = scalePixels(16);
    const int arrowFontSize = std::max(scalePixels(20), menuFont.pixelSize() + scalePixels(4));
    const int arrowColumnWidth = std::max(scalePixels(18), arrowFontSize);
    const int shortcutGap = scalePixels(24);
    const int mainMenuMinWidth = scalePixels(72);
    const int subMenuMinWidth = scalePixels(72);
    subPanel->setStyleSheet(titleApplicationPanelStyleSheet(dark_theme_enabled_));
    nestedPanel->setStyleSheet(titleApplicationPanelStyleSheet(dark_theme_enabled_));
    auto commandRowsHeight = [menuVerticalPadding, rowHeight](const QVector<TitleMenuCommand>& commands) {
        return menuVerticalPadding * 2 + rowHeight * static_cast<int>(commands.size());
    };

    QVector<TitleMenuSection> sections;
    TitleMenuSection fileSection{
        is_english_ ? QStringLiteral("File") : QStringLiteral("文件"),
        {
            {is_english_ ? QStringLiteral("Recording Folder...") : QStringLiteral("记录目录..."),
             QStringLiteral("Ctrl+R"),
             true,
             false,
             false,
             [this]() { onChooseRecordingDirectoryClicked(); }},
            {is_english_ ? QStringLiteral("Data Viewer...") : QStringLiteral("数据查看器..."),
             QString(),
             true,
             false,
             false,
             [this]() { onOpenSessionViewerClicked(); }},
            {is_english_ ? QStringLiteral("Exit") : QStringLiteral("退出"),
             QStringLiteral("Ctrl+Q"),
             true,
             false,
             true,
             [this]() { close(); }}
        }
    };

    TitleMenuSection viewSection{
        is_english_ ? QStringLiteral("View") : QStringLiteral("视图"),
        {
            {is_english_ ? QStringLiteral("Tiny (70%)") : QStringLiteral("超小 (70%)"),
             QString(),
             true,
             font_scale_percent_ == 70,
             true,
             [this]() { setFontScale(70); }},
            {is_english_ ? QStringLiteral("Extra Small (80%)") : QStringLiteral("特小 (80%)"),
             QString(),
             true,
             font_scale_percent_ == 80,
             false,
             [this]() { setFontScale(80); }},
            {is_english_ ? QStringLiteral("Small (90%)") : QStringLiteral("小号 (90%)"),
             QString(),
             true,
             font_scale_percent_ == 90,
             false,
             [this]() { setFontScale(90); }},
            {is_english_ ? QStringLiteral("Normal (100%)") : QStringLiteral("标准 (100%)"),
             QString(),
             true,
             font_scale_percent_ == 100,
             false,
             [this]() { setFontScale(100); }},
            {is_english_ ? QStringLiteral("Large (115%)") : QStringLiteral("大号 (115%)"),
             QString(),
             true,
             font_scale_percent_ == 115,
             false,
             [this]() { setFontScale(115); }},
            {is_english_ ? QStringLiteral("Extra Large (130%)") : QStringLiteral("超大 (130%)"),
             QString(),
             true,
             font_scale_percent_ == 130,
             false,
             [this]() { setFontScale(130); }}
#ifdef VAPORVIEW_HAS_OSGEARTH
            ,
            {is_english_ ? QStringLiteral("3D Map...") : QStringLiteral("三维地图..."),
             QString(),
             true,
             false,
             true,
             [this]() { onOpenMap3DWindowClicked(); }}
#endif
        }
    };

    TitleMenuSection developerSection{is_english_ ? QStringLiteral("Developer") : QStringLiteral("开发者"), {}};
    developerSection.commands.push_back(
        {is_english_ ? QStringLiteral("TCP wave: record every raw frame") : QStringLiteral("TCP波形：记录完整原始帧"),
         QString(),
         true,
         waveform_recording_rate_hz_ == 0,
         false,
         [this]() { setWaveformRecordingRateHz(0); }});
    developerSection.commands.push_back(
        {is_english_ ? QStringLiteral("EPSILON: record verified raw frames") : QStringLiteral("EPSILON：记录已校验原始帧"),
         QString(),
         true,
         imu_recording_rate_hz_ == 0,
         false,
         [this]() { setImuRecordingRateHz(0); }});
    QVector<TitleMenuCommand> csvRateCommands;
    for (int rate : QVector<int>{1, 2, 5, 10, 20, 50, 100, 200})
    {
        csvRateCommands.push_back({
            QStringLiteral("%1 Hz").arg(rate),
            QString(),
            true,
            rate == std::clamp(recording_export_rate_hz_, 1, 200),
            false,
            [this, rate]() { setRecordingExportRateHz(rate); }
        });
    }
    developerSection.commands.push_back(
        {is_english_ ? QStringLiteral("Device CSV recording rate") : QStringLiteral("设备CSV记录频率"),
         QStringLiteral("%1 Hz").arg(std::clamp(recording_export_rate_hz_, 1, 200)),
         true,
         false,
         true,
         {},
         csvRateCommands});
    developerSection.commands.push_back(
        {is_english_ ? QStringLiteral("EPSILON Packet Rates...") : QStringLiteral("设置EPSILON包频率..."),
         QString(),
         !uiBusy,
         false,
         true,
         [this]() { onConfigureEpsilonPacketRatesClicked(); }});
    developerSection.commands.push_back(
        {is_english_ ? QStringLiteral("Configure EPSILON RTCM Port...") : QStringLiteral("配置EPSILON RTCM串口..."),
         QString(),
         !uiBusy,
         false,
         false,
         [this]() { onConfigureEpsilonRtcmPortClicked(); }});
    developerSection.commands.push_back(
        {is_english_ ? QStringLiteral("Reconfigure EPSILON Output...") : QStringLiteral("重新配置EPSILON输出..."),
         QString(),
         !uiBusy,
         false,
         false,
         [this]() { onReconfigureEpsilonClicked(); }});
    developerSection.commands.push_back(
        {is_english_ ? QStringLiteral("RTK Config") : QStringLiteral("RTK配置"),
         QString(),
         true,
         false,
         true,
         [this]() { onRtkConfigClicked(); }});

    TitleMenuSection helpSection{
        is_english_ ? QStringLiteral("Help") : QStringLiteral("帮助"),
        {
            {is_english_ ? QStringLiteral("About") : QStringLiteral("关于"),
             QString(),
             true,
             false,
             false,
             [this]() { showAboutDialog(); }}
        }
    };

    sections.push_back(fileSection);
    sections.push_back(viewSection);
    sections.push_back(developerSection);
    sections.push_back(helpSection);

    int mainMenuWidth = mainMenuMinWidth;
    QVector<int> subMenuWidths;
    subMenuWidths.reserve(sections.size());
    QVector<bool> subMenuNeedsCheckColumn;
    subMenuNeedsCheckColumn.reserve(sections.size());
    for (const TitleMenuSection& section : sections)
    {
        mainMenuWidth = std::max(mainMenuWidth,
                                 rowLeftPadding +
                                      menuMetrics.horizontalAdvance(section.title) +
                                      rowSpacing +
                                      arrowColumnWidth +
                                      rowRightPadding);

        bool needsCheckColumn = false;
        for (const TitleMenuCommand& command : section.commands)
        {
            needsCheckColumn = needsCheckColumn || command.checked;
        }
        subMenuNeedsCheckColumn.push_back(needsCheckColumn);

        int sectionWidth = subMenuMinWidth;
        for (const TitleMenuCommand& command : section.commands)
        {
            int commandWidth = rowLeftPadding + menuMetrics.horizontalAdvance(command.text) + rowRightPadding;
            if (needsCheckColumn)
            {
                commandWidth += checkColumnWidth + rowSpacing;
            }
            if (!command.shortcut.isEmpty())
            {
                commandWidth += shortcutGap + menuMetrics.horizontalAdvance(command.shortcut);
            }
            if (!command.submenu.isEmpty())
            {
                commandWidth += rowSpacing + arrowColumnWidth;
            }
            sectionWidth = std::max(sectionWidth, commandWidth);
        }
        subMenuWidths.push_back(sectionWidth);
    }

    auto *mainMenu = new QFrame(panel);
    mainMenu->setObjectName(QStringLiteral("titleApplicationMainMenu"));
    mainMenu->setAttribute(Qt::WA_StyledBackground, true);
    mainMenu->setFixedSize(mainMenuWidth, menuVerticalPadding * 2 + rowHeight * sections.size());
    mainMenu->move(0, 0);

    auto *mainLayout = new QVBoxLayout(mainMenu);
    mainLayout->setContentsMargins(0, menuVerticalPadding, 0, menuVerticalPadding);
    mainLayout->setSpacing(0);

    auto *subMenu = new QFrame(subPanel);
    subMenu->setObjectName(QStringLiteral("titleApplicationSubMenu"));
    subMenu->setAttribute(Qt::WA_StyledBackground, true);
    subMenu->setFixedWidth(subMenuWidths.value(0, subMenuMinWidth));
    subMenu->move(0, 0);
    subMenu->hide();

    auto *nestedMenu = new QFrame(nestedPanel);
    nestedMenu->setObjectName(QStringLiteral("titleApplicationNestedMenu"));
    nestedMenu->setAttribute(Qt::WA_StyledBackground, true);
    nestedMenu->hide();

    auto *subLayout = new QVBoxLayout(subMenu);
    subLayout->setContentsMargins(0, 0, 0, 0);
    subLayout->setSpacing(0);
    auto *stack = new QStackedWidget(subMenu);
    stack->setObjectName(QStringLiteral("titleApplicationSubStack"));
    stack->setAttribute(Qt::WA_StyledBackground, false);
    subLayout->addWidget(stack);

    auto *nestedLayout = new QVBoxLayout(nestedMenu);
    nestedLayout->setContentsMargins(0, menuVerticalPadding, 0, menuVerticalPadding);
    nestedLayout->setSpacing(0);

    std::function<QFrame *(QWidget *,
                           const QString&,
                           const QString&,
                           bool,
                           bool,
                           bool,
                           const QString&,
                           const std::function<void()>&)> createRow =
        [closePanel,
         rowHeight,
         rowLeftPadding,
         rowRightPadding,
         rowSpacing,
         checkColumnWidth,
         checkIconSize,
         arrowFontSize,
         arrowColumnWidth](QWidget *parent,
                           const QString& text,
                           const QString& trailingText,
                           bool enabled,
                           bool checked,
                           bool reserveCheckColumn,
                           const QString& arrow,
                           const std::function<void()>& clickHandler) {
        auto *row = new QFrame(parent);
        row->setObjectName(QStringLiteral("titleApplicationMenuItem"));
        row->setAttribute(Qt::WA_StyledBackground, true);
        row->setEnabled(enabled);
        row->setFixedHeight(rowHeight);
        row->setCursor(enabled ? Qt::PointingHandCursor : Qt::ArrowCursor);
        row->setMouseTracking(true);

        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(rowLeftPadding, 0, rowRightPadding, 0);
        rowLayout->setSpacing(rowSpacing);

        if (reserveCheckColumn || checked)
        {
            auto *checkLabel = new QLabel(row);
            checkLabel->setObjectName(QStringLiteral("titleApplicationMenuCheck"));
            checkLabel->setEnabled(enabled);
            checkLabel->setFixedWidth(checkColumnWidth);
            checkLabel->setAlignment(Qt::AlignCenter);
            checkLabel->setMargin(0);
            checkLabel->setIndent(0);
            checkLabel->setAttribute(Qt::WA_TransparentForMouseEvents, true);
            if (checked)
            {
                checkLabel->setPixmap(createMenuCheckIcon(qApp->property(kAppDarkThemeProperty).toBool()).pixmap(checkIconSize, checkIconSize));
            }
            rowLayout->addWidget(checkLabel);
        }

        auto *textLabel = new QLabel(text, row);
        textLabel->setObjectName(QStringLiteral("titleApplicationMenuText"));
        textLabel->setEnabled(enabled);
        textLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        textLabel->setMargin(0);
        textLabel->setIndent(0);
        textLabel->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        textLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        rowLayout->addWidget(textLabel, 1);

        if (!trailingText.isEmpty())
        {
            auto *shortcutLabel = new QLabel(trailingText, row);
            shortcutLabel->setObjectName(QStringLiteral("titleApplicationMenuShortcut"));
            shortcutLabel->setEnabled(enabled);
            shortcutLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            shortcutLabel->setMargin(0);
            shortcutLabel->setIndent(0);
            shortcutLabel->setAttribute(Qt::WA_TransparentForMouseEvents, true);
            rowLayout->addWidget(shortcutLabel);
        }

        if (!arrow.isEmpty())
        {
            auto *arrowLabel = new QLabel(row);
            arrowLabel->setObjectName(QStringLiteral("titleApplicationMenuArrow"));
            arrowLabel->setEnabled(enabled);
            arrowLabel->setFixedSize(arrowColumnWidth, rowHeight);
            arrowLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            arrowLabel->setMargin(0);
            arrowLabel->setIndent(0);
            const bool dark = qApp->property(kAppDarkThemeProperty).toBool();
            const QColor arrowColor = appThemeColor(enabled ? AppThemeColor::MenuText
                                                            : AppThemeColor::MenuDisabledText,
                                                   dark);
            const QSize arrowIconSize(arrowFontSize, arrowFontSize);
            arrowLabel->setPixmap(createLucideIcon(QStringLiteral("chevron-right"), arrowColor).pixmap(arrowIconSize));
            arrowLabel->setProperty("usesLucideChevron", true);
            arrowLabel->setProperty("iconSize", arrowFontSize);
            arrowLabel->setAttribute(Qt::WA_TransparentForMouseEvents, true);
            rowLayout->addWidget(arrowLabel);
        }

        if (clickHandler)
        {
            row->installEventFilter(new MenuItemEventFilter({}, [closePanel, clickHandler]() {
                closePanel();
                clickHandler();
            }, row));
        }
        return row;
    };

    auto sectionRows = std::make_shared<QVector<QFrame *>>();
    auto activeNestedSource = std::make_shared<QFrame *>(nullptr);

    auto hideNestedMenu = [subPanel, subMenu, nestedPanel, nestedMenu, activeNestedSource]() {
        *activeNestedSource = nullptr;
        nestedMenu->hide();
        nestedPanel->hide();
        setFloatingMenuContentFixedSize(subPanel, subMenu->size());
    };

    auto clearLayout = [](QLayout *layout) {
        while (QLayoutItem *item = layout->takeAt(0))
        {
            if (QWidget *widget = item->widget())
            {
                widget->hide();
                widget->deleteLater();
            }
            delete item;
        }
    };

    auto showNestedMenu = [=](const QVector<TitleMenuCommand>& commands, QFrame *sourceRow) {
        if (commands.isEmpty() || !sourceRow)
        {
            hideNestedMenu();
            return;
        }

        if (*activeNestedSource == sourceRow && nestedPanel->isVisible() && nestedMenu->isVisible())
        {
            nestedPanel->raise();
            return;
        }
        *activeNestedSource = sourceRow;

        clearLayout(nestedLayout);
        bool needsCheckColumn = false;
        int nestedWidth = subMenuMinWidth;
        for (const TitleMenuCommand& command : commands)
        {
            needsCheckColumn = needsCheckColumn || command.checked;
        }
        for (const TitleMenuCommand& command : commands)
        {
            int commandWidth = rowLeftPadding + menuMetrics.horizontalAdvance(command.text) + rowRightPadding;
            if (needsCheckColumn)
            {
                commandWidth += checkColumnWidth + rowSpacing;
            }
            if (!command.shortcut.isEmpty())
            {
                commandWidth += shortcutGap + menuMetrics.horizontalAdvance(command.shortcut);
            }
            nestedWidth = std::max(nestedWidth, commandWidth);
        }

        for (const TitleMenuCommand& command : commands)
        {
            nestedLayout->addWidget(createRow(nestedMenu,
                                             command.text,
                                             command.shortcut,
                                             command.enabled,
                                             command.checked,
                                             needsCheckColumn,
                                             QString(),
                                             command.enabled ? command.handler : std::function<void()>{}));
        }

        const int nestedHeight = commandRowsHeight(commands);
        const int submenuOverlap = std::max(6, rowSpacing + 2);
        const int sourceY = sourceRow->mapTo(subMenu, QPoint(0, 0)).y();
        const int nestedY = std::clamp(sourceY,
                                       0,
                                       std::max(0, std::max(subMenu->height(), nestedHeight) - nestedHeight));
        nestedMenu->setFixedSize(nestedWidth, nestedHeight);
        setFloatingMenuContentFixedSize(nestedPanel, nestedMenu->size());
        nestedMenu->move(floatingMenuContentRect(nestedPanel).topLeft());

        const QRect screenRect = screen() ? screen()->availableGeometry() : QRect(QPoint(0, 0), size());
        const int popupMargin = scalePixels(4);
        const QPoint desiredContentTopLeft =
            subMenu->mapToGlobal(QPoint(subMenu->width() - submenuOverlap, nestedY));
        const QPoint nestedPanelContentOffset = floatingMenuContentRect(nestedPanel).topLeft();
        QPoint nestedPanelPos = desiredContentTopLeft - nestedPanelContentOffset;
        if (nestedPanelPos.x() + nestedPanel->width() > screenRect.right() - popupMargin)
        {
            const QPoint leftDesiredContentTopLeft =
                subMenu->mapToGlobal(QPoint(-nestedWidth + submenuOverlap, nestedY));
            nestedPanelPos = leftDesiredContentTopLeft - nestedPanelContentOffset;
        }
        nestedPanelPos.setX(std::clamp(nestedPanelPos.x(),
                                       screenRect.left() + popupMargin,
                                       std::max(screenRect.left() + popupMargin,
                                                screenRect.right() - nestedPanel->width() - popupMargin)));
        nestedPanelPos.setY(std::clamp(nestedPanelPos.y(),
                                       screenRect.top() + popupMargin,
                                       std::max(screenRect.top() + popupMargin,
                                                screenRect.bottom() - nestedPanel->height() - popupMargin)));
        nestedPanel->move(nestedPanelPos);
        nestedMenu->show();
        nestedMenu->raise();
        nestedPanel->show();
        nestedPanel->raise();
    };

    for (int sectionIndex = 0; sectionIndex < sections.size(); ++sectionIndex)
    {
        QWidget *page = new QWidget(stack);
        page->setObjectName(QStringLiteral("titleApplicationSubPage"));
        page->setAttribute(Qt::WA_StyledBackground, false);
        page->setFixedSize(subMenuWidths.value(sectionIndex, subMenuMinWidth),
                           commandRowsHeight(sections[sectionIndex].commands));
        auto *pageLayout = new QVBoxLayout(page);
        pageLayout->setContentsMargins(0, 0, 0, 0);
        pageLayout->setSpacing(0);
        auto *pageContent = new QWidget(page);
        pageContent->setObjectName(QStringLiteral("titleApplicationSubPageContent"));
        pageContent->setAttribute(Qt::WA_StyledBackground, false);
        auto *contentLayout = new QVBoxLayout(pageContent);
        contentLayout->setContentsMargins(0, menuVerticalPadding, 0, menuVerticalPadding);
        contentLayout->setSpacing(0);
        pageLayout->addWidget(pageContent);

        for (const TitleMenuCommand& command : sections[sectionIndex].commands)
        {
            QFrame *commandRow = createRow(pageContent,
                                           command.text,
                                           command.shortcut,
                                           command.enabled,
                                           command.checked,
                                           subMenuNeedsCheckColumn.value(sectionIndex, false),
                                           command.submenu.isEmpty() ? QString() : QStringLiteral("›"),
                                           command.enabled && command.submenu.isEmpty() ? command.handler : std::function<void()>{});
            contentLayout->addWidget(commandRow);
            if (!command.submenu.isEmpty())
            {
                commandRow->installEventFilter(new MenuItemEventFilter([showNestedMenu, command, commandRow]() {
                    showNestedMenu(command.submenu, commandRow);
                }, {}, commandRow));
            }
            else
            {
                commandRow->installEventFilter(new MenuItemEventFilter(hideNestedMenu, {}, commandRow));
            }
        }
        stack->addWidget(page);

        auto *sectionRow = createRow(mainMenu,
                                     sections[sectionIndex].title,
                                      QString(),
                                      true,
                                      false,
                                      false,
                                      QStringLiteral("›"),
                                      {});
        sectionRow->setProperty("selected", false);
        sectionRows->push_back(sectionRow);
        mainLayout->addWidget(sectionRow);
        sectionRow->installEventFilter(new MenuItemEventFilter([this, stack, subMenu, mainMenu, panel, subPanel, nestedPanel, mainMenuWidth, menuVerticalPadding, rowSpacing, subMenuWidths, sectionRows, sectionRow, sectionIndex, nestedMenu, activeNestedSource]() {
            *activeNestedSource = nullptr;
            nestedMenu->hide();
            nestedPanel->hide();
            stack->setCurrentIndex(sectionIndex);
            if (QWidget *currentPage = stack->currentWidget())
            {
                const int subMenuWidth = subMenuWidths.value(sectionIndex, currentPage->width());
                const int subMenuBorderWidth = std::max(1, subMenu->frameWidth());
                const int subMenuTop = std::max(0, sectionRow->y() - menuVerticalPadding - subMenuBorderWidth);
                subMenu->setFixedSize(subMenuWidth, currentPage->height());
                setFloatingMenuContentFixedSize(subPanel, subMenu->size());
                const int popupMargin = scalePixels(4);
                const QRect screenRect = screen() ? screen()->availableGeometry() : QRect(QPoint(0, 0), size());
                const int submenuOverlap = std::max(6, rowSpacing + 2);
                const QPoint subPanelContentOffset = floatingMenuContentRect(subPanel).topLeft();
                const QPoint desiredContentTopLeft =
                    mainMenu->mapToGlobal(QPoint(mainMenuWidth - submenuOverlap, subMenuTop));
                QPoint subPanelPos = desiredContentTopLeft - subPanelContentOffset;
                if (subPanelPos.x() + subPanel->width() > screenRect.right() - popupMargin)
                {
                    const QPoint leftDesiredContentTopLeft =
                        mainMenu->mapToGlobal(QPoint(-subMenuWidth + submenuOverlap, subMenuTop));
                    subPanelPos = leftDesiredContentTopLeft - subPanelContentOffset;
                }
                subPanelPos.setX(std::clamp(subPanelPos.x(),
                                            screenRect.left() + popupMargin,
                                            std::max(screenRect.left() + popupMargin,
                                                     screenRect.right() - subPanel->width() - popupMargin)));
                subPanelPos.setY(std::clamp(subPanelPos.y(),
                                            screenRect.top() + popupMargin,
                                            std::max(screenRect.top() + popupMargin,
                                                     screenRect.bottom() - subPanel->height() - popupMargin)));
                subPanel->move(subPanelPos);
                subMenu->move(floatingMenuContentRect(subPanel).topLeft());
                subMenu->raise();
                subMenu->show();
                subPanel->show();
                subPanel->raise();
            }
            for (QFrame *row : *sectionRows)
            {
                if (row)
                {
                    row->setProperty("selected", row == sectionRow);
                    row->style()->unpolish(row);
                    row->style()->polish(row);
                    row->update();
                }
            }
        }, {}, sectionRow));
    }
    mainLayout->addStretch(1);
    stack->setCurrentIndex(0);
    const QRect panelContentRect = floatingMenuContentRect(panel);
    mainMenu->move(panelContentRect.topLeft());
    setFloatingMenuContentFixedSize(panel, mainMenu->size());
    setFloatingMenuContentFixedSize(subPanel, QSize(subMenu->width(), 0));
    setFloatingMenuContentFixedSize(nestedPanel, QSize(nestedMenu->width(), 0));
}

void MainWindow::showTitleApplicationMenu()
{
    if (!title_menu_btn_)
    {
        return;
    }

    createTitleApplicationMenuPanel();
    if (!title_application_panel_)
    {
        return;
    }

    if (title_application_panel_->isVisible())
    {
        title_application_panel_->hide();
        if (title_application_sub_panel_)
        {
            title_application_sub_panel_->hide();
        }
        if (title_application_nested_panel_)
        {
            title_application_nested_panel_->hide();
        }
        return;
    }

    const QPoint anchor = title_menu_btn_->mapToGlobal(QPoint(0, title_menu_btn_->height() + scalePixels(4)));
    const int popupMargin = scalePixels(4);
    const QRect screenRect = screen() ? screen()->availableGeometry() : QRect(mapToGlobal(QPoint(0, 0)), size());
    const int x = std::clamp(anchor.x() - scalePixels(kFloatingMenuShadowMarginPx),
                             screenRect.left() + popupMargin,
                             std::max(screenRect.left() + popupMargin,
                                      screenRect.right() - title_application_panel_->width() - popupMargin));
    const int y = std::max(anchor.y() - scalePixels(kFloatingMenuShadowMarginPx),
                           screenRect.top() + popupMargin);
    title_application_panel_->move(x, y);
    if (title_application_sub_panel_)
    {
        title_application_sub_panel_->hide();
    }
    if (title_application_nested_panel_)
    {
        title_application_nested_panel_->hide();
    }
    title_application_panel_->show();
    title_application_panel_->raise();
}

void MainWindow::setupStatusBar()
{
    statusBar()->setSizeGripEnabled(false);

    status_label_ = new QLabel(this);
    statusBar()->addWidget(status_label_);

    status_task_progress_bar_ = new QProgressBar(this);
    status_task_progress_bar_->setVisible(false);
    status_task_progress_bar_->setTextVisible(true);
    status_task_progress_bar_->setFixedHeight(18);
    status_task_progress_bar_->setMinimumWidth(180);
    status_task_progress_bar_->setMaximumWidth(260);
    status_task_progress_bar_->setRange(0, 100);
    status_task_progress_bar_->setValue(0);
    status_task_progress_bar_->setFormat(QString());
    statusBar()->addWidget(status_task_progress_bar_);

    status_task_spinner_label_ = new QLabel(this);
    status_task_spinner_label_->setObjectName("statusTaskSpinner");
    status_task_spinner_label_->setAlignment(Qt::AlignCenter);
    status_task_spinner_label_->setFixedSize(18, 18);
    status_task_spinner_label_->setVisible(false);
    statusBar()->addWidget(status_task_spinner_label_);

    status_task_spinner_timer_ = new QTimer(this);
    status_task_spinner_timer_->setInterval(120);
    connect(status_task_spinner_timer_, &QTimer::timeout, this, [this]() {
        if (!status_task_spinner_label_ || !status_task_spinner_label_->isVisible())
        {
            return;
        }
        static const QStringList frames = {
            QStringLiteral("◐"),
            QStringLiteral("◓"),
            QStringLiteral("◑"),
            QStringLiteral("◒"),
        };
        status_task_spinner_label_->setText(frames.at(status_task_spinner_index_ % frames.size()));
        status_task_spinner_index_ = (status_task_spinner_index_ + 1) % frames.size();
    });

}

void MainWindow::startStatusTaskSpinner()
{
    if (!status_task_spinner_label_ || !status_task_spinner_timer_)
    {
        return;
    }

    status_task_spinner_label_->setVisible(true);
    if (!status_task_spinner_timer_->isActive())
    {
        status_task_spinner_index_ = 0;
        status_task_spinner_label_->setText(QStringLiteral("◐"));
        status_task_spinner_timer_->start();
    }
}

void MainWindow::stopStatusTaskSpinner()
{
    if (status_task_spinner_timer_)
    {
        status_task_spinner_timer_->stop();
    }
    if (status_task_spinner_label_)
    {
        status_task_spinner_label_->clear();
        status_task_spinner_label_->setToolTip(QString());
        status_task_spinner_label_->setVisible(false);
    }
    status_task_spinner_index_ = 0;
}

void MainWindow::showStatusTaskProgress(const QString& label, int value, int maximum)
{
    if (!status_task_progress_bar_)
    {
        return;
    }

    const int normalizedMaximum = std::max(1, maximum);
    const int normalizedValue = std::clamp(value, 0, normalizedMaximum);
    status_task_progress_bar_->setVisible(true);
    status_task_progress_bar_->setRange(0, normalizedMaximum);
    status_task_progress_bar_->setValue(normalizedValue);
    status_task_progress_bar_->setFormat(QStringLiteral("%1 %p%").arg(label));
    status_task_progress_bar_->setToolTip(label);
    if (status_task_spinner_label_)
    {
        status_task_spinner_label_->setToolTip(label);
    }
    startStatusTaskSpinner();
}

void MainWindow::showBusyStatusTaskProgress(const QString& label)
{
    if (!status_task_progress_bar_)
    {
        return;
    }

    status_task_progress_bar_->setVisible(true);
    status_task_progress_bar_->setRange(0, 100);
    status_task_progress_bar_->setValue(0);
    status_task_progress_bar_->setFormat(label);
    status_task_progress_bar_->setToolTip(label);
    if (status_task_spinner_label_)
    {
        status_task_spinner_label_->setToolTip(label);
    }
    startStatusTaskSpinner();
}

void MainWindow::hideStatusTaskProgress()
{
    if (!status_task_progress_bar_)
    {
        return;
    }

    status_task_progress_bar_->setVisible(false);
    status_task_progress_bar_->setRange(0, 100);
    status_task_progress_bar_->setValue(0);
    status_task_progress_bar_->setFormat(QString());
    status_task_progress_bar_->setToolTip(QString());
    stopStatusTaskSpinner();
}

void MainWindow::setupCentralWidget()
{
    central_widget_ = new QWidget(this);
    central_widget_->setObjectName("appCentralWidget");
    central_widget_->setAttribute(Qt::WA_StyledBackground, true);
    central_widget_->setAutoFillBackground(true);
    setCentralWidget(central_widget_);

    auto *main_h_layout = new QHBoxLayout(central_widget_);
    main_h_layout->setSpacing(0);
    main_h_layout->setContentsMargins(kAppSidebarVisualPadding,
                                      kAppSidebarVisualPadding,
                                      kAppSidebarVisualPadding,
                                      0);

    main_page_stack_ = new QStackedWidget(central_widget_);
    main_page_stack_->setObjectName(QStringLiteral("mainPageStack"));
    main_page_stack_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    connect(main_page_stack_, &QStackedWidget::currentChanged, this, [this]() {
        updateCustomTitleBarTexts();
    });

    auto *left_widget = new QWidget(this);
    left_widget->setObjectName("mainCardsPane");
    left_widget->setAttribute(Qt::WA_StyledBackground, true);
    left_widget->setAutoFillBackground(true);
    main_layout_ = new QVBoxLayout(left_widget);
    main_layout_->setSpacing(0);
    main_layout_->setContentsMargins(0, 0, 0, 0);

    setupConfigPanel();
    setupDataPanels();

    main_cards_scroll_area_ = new QScrollArea(this);
    main_cards_scroll_area_->setObjectName("mainCardsScrollArea");
    main_cards_scroll_area_->setAttribute(Qt::WA_StyledBackground, true);
    main_cards_scroll_area_->setAutoFillBackground(true);
    main_cards_scroll_area_->viewport()->setObjectName("mainCardsViewport");
    main_cards_scroll_area_->viewport()->setAttribute(Qt::WA_StyledBackground, true);
    main_cards_scroll_area_->viewport()->setAutoFillBackground(true);
    main_cards_scroll_area_->setWidgetResizable(true);
    main_cards_scroll_area_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    main_cards_scroll_area_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    main_cards_scroll_area_->setFrameShape(QFrame::NoFrame);
    main_cards_scroll_area_->setMinimumWidth(0);
    main_cards_scroll_area_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
    main_cards_scroll_area_->setWidget(left_widget);

    setupLogPanel();

    app_sidebar_ = new AppSidebarFrame(central_widget_);
    app_sidebar_->setObjectName(QStringLiteral("appSidebar"));
    app_sidebar_->setAttribute(Qt::WA_StyledBackground, true);
    app_sidebar_->setAutoFillBackground(true);
    app_sidebar_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    app_sidebar_->setMinimumWidth(0);
    app_sidebar_->setMaximumWidth(QWIDGETSIZE_MAX);
    auto *sidebarLayout = new QVBoxLayout(app_sidebar_);
    sidebarLayout->setContentsMargins(0,
                                      kAppSidebarTopBottomPadding,
                                      kAppSidebarVisualPadding,
                                      kAppSidebarTopBottomPadding);
    sidebarLayout->setSpacing(6);
    app_nav_button_group_ = new QButtonGroup(this);
    app_nav_button_group_->setExclusive(true);
    auto createNavButton = [this, sidebarLayout](const QString& text, const QString& iconName) {
        auto *button = new QPushButton(text, app_sidebar_);
        button->setObjectName(QStringLiteral("appSidebarButton"));
        button->setProperty(kSidebarIconNameProperty, iconName);
        button->setProperty(kSidebarCompactProperty, false);
        button->setProperty(kSidebarHoverProperty, false);
        configureHoverParticipant(button, kSidebarHoverParticipantProperty, this);
        button->setCheckable(true);
        button->setFocusPolicy(Qt::NoFocus);
        button->setMinimumWidth(0);
        button->setMaximumWidth(QWIDGETSIZE_MAX);
        button->setFixedHeight(kAppSidebarButtonHeight);
        button->setIconSize(QSize(kAppSidebarFullIconSize, kAppSidebarFullIconSize));
        button->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        button->setToolTip(text);
        button->setStatusTip(text);
        button->setAccessibleName(text);
        sidebarLayout->addWidget(button);
        return button;
    };
    home_nav_btn_ = createNavButton(QStringLiteral("首页"), QStringLiteral("square-activity"));
    device_config_nav_btn_ = createNavButton(QStringLiteral("设备配置"), QStringLiteral("sliders-vertical"));
    temperature_nav_btn_ = createNavButton(QStringLiteral("温控"), QStringLiteral("thermometer"));
    rtk_config_nav_btn_ = createNavButton(QStringLiteral("RTK配置"), QStringLiteral("satellite"));
    app_nav_button_group_->addButton(home_nav_btn_, 0);
    app_nav_button_group_->addButton(device_config_nav_btn_, 1);
    app_nav_button_group_->addButton(temperature_nav_btn_, 2);
    app_nav_button_group_->addButton(rtk_config_nav_btn_, 3);
    sidebarLayout->addStretch(1);
    home_nav_btn_->setChecked(true);
    updateSidebarNavIcons();

    app_layout_splitter_ = new QSplitter(Qt::Horizontal, central_widget_);
    app_layout_splitter_->setObjectName(QStringLiteral("appLayoutSplitter"));
    app_layout_splitter_->setAttribute(Qt::WA_StyledBackground, true);
    app_layout_splitter_->setAutoFillBackground(true);
    app_layout_splitter_->setChildrenCollapsible(true);
    app_layout_splitter_->setHandleWidth(8);
    app_layout_splitter_->addWidget(app_sidebar_);

    main_content_splitter_ = new QSplitter(Qt::Horizontal, central_widget_);
    main_content_splitter_->setObjectName("mainContentSplitter");
    main_content_splitter_->setAttribute(Qt::WA_StyledBackground, true);
    main_content_splitter_->setAutoFillBackground(true);
    main_content_splitter_->setChildrenCollapsible(true);
    main_content_splitter_->setCollapsible(0, true);
    main_content_splitter_->setCollapsible(1, true);
    main_content_splitter_->setHandleWidth(1);
    main_content_splitter_->addWidget(main_page_stack_);
    main_content_splitter_->addWidget(log_side_panel_);
    if (QSplitterHandle *handle = main_content_splitter_->handle(1))
    {
        handle->installEventFilter(this);
    }
    main_content_splitter_->setStretchFactor(0, 8);
    main_content_splitter_->setStretchFactor(1, 1);
    main_content_splitter_->setSizes({1600, minimumLogSidePanelWidth()});
    connect(main_content_splitter_, &QSplitter::splitterMoved, this, [this]() {
        if (!main_content_splitter_ || log_side_panel_collapsed_)
        {
            return;
        }
        const QList<int> sizes = main_content_splitter_->sizes();
        const int minimumLogWidth = minimumLogSidePanelWidth();
        if (sizes.size() >= 2 && sizes.at(1) >= minimumLogWidth)
        {
            last_log_side_panel_width_ = sizes.at(1);
            log_side_panel_width_initialized_ = true;
        }
        updateResponsiveHomeLayout();
        queueResponsiveHomeLayoutRefresh();
    });

    app_layout_splitter_->addWidget(main_content_splitter_);
    if (QSplitterHandle *handle = app_layout_splitter_->handle(1))
    {
        handle->installEventFilter(this);
    }
    app_layout_splitter_->setCollapsible(0, true);
    app_layout_splitter_->setCollapsible(1, false);
    app_layout_splitter_->setStretchFactor(0, 0);
    app_layout_splitter_->setStretchFactor(1, 1);
    {
        QSettings settings("VaporView", "MainWindow");
        const int restoredAppSidebarWidth = std::max(0, settings.value(
            QStringLiteral("app_sidebar_width"),
            appSidebarDefaultWidth()).toInt());
        const int initialAppSidebarWidth = snappedAppSidebarWidth(restoredAppSidebarWidth);
        if (initialAppSidebarWidth > 0)
        {
            last_app_sidebar_visible_width_ = initialAppSidebarWidth;
        }
        app_sidebar_mode_ = appSidebarModeForWidth(initialAppSidebarWidth);
        updateAppSidebarButtonTexts();
        app_layout_splitter_->setSizes({initialAppSidebarWidth, 1600});
    }
    connect(app_layout_splitter_, &QSplitter::splitterMoved, this, [this]() {
        if (app_sidebar_adjusting_ || !app_layout_splitter_)
        {
            return;
        }
        const QList<int> sizes = app_layout_splitter_->sizes();
        if (sizes.size() < 2)
        {
            return;
        }
        updateAppSidebarForWidth(sizes.at(0), false);
    });

    home_page_ = main_cards_scroll_area_;
    main_page_stack_->addWidget(home_page_);
    setupDeviceConfigPage();

    temperature_page_ = new QWidget(this);
    temperature_page_->setObjectName(QStringLiteral("temperaturePage"));
    auto *temperaturePageLayout = new QVBoxLayout(temperature_page_);
    temperaturePageLayout->setContentsMargins(8, 0, 8, 8);
    temperaturePageLayout->setSpacing(8);
    auto *temperatureScrollArea = new QScrollArea(temperature_page_);
    temperatureScrollArea->setObjectName(QStringLiteral("mainCardsScrollArea"));
    temperatureScrollArea->setWidgetResizable(true);
    temperatureScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    temperatureScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    temperatureScrollArea->setFrameShape(QFrame::NoFrame);
    auto *temperatureContent = new QWidget(temperatureScrollArea);
    auto *temperatureContentLayout = new QVBoxLayout(temperatureContent);
    temperatureContentLayout->setContentsMargins(0, 0, 0, 0);
    temperatureContentLayout->setSpacing(8);
    temperatureContentLayout->addWidget(temperature_controller_group_, 0);
    temperatureContentLayout->addStretch(1);
    temperatureScrollArea->setWidget(temperatureContent);
    temperaturePageLayout->addWidget(temperatureScrollArea, 1);
    main_page_stack_->addWidget(temperature_page_);

    rtk_config_dialog_ = new RtkConfigDialog(main_page_stack_, true);
    rtk_config_dialog_->setAttribute(Qt::WA_QuitOnClose, false);
    connect(rtk_config_dialog_, &RtkConfigDialog::rtkRunningChanged, this, [this](bool running) {
        rtk_service_running_ = running;
        updateRtkConfigIcon();
        updateSidebarNavIcons();
    });
    syncRtkConfigPageState();
    main_page_stack_->addWidget(rtk_config_dialog_);

    connect(app_nav_button_group_, &QButtonGroup::idClicked, this, [this](int id) {
        if (id == 3)
        {
            syncRtkConfigPageState();
        }
        if (main_page_stack_)
        {
            main_page_stack_->setCurrentIndex(std::clamp(id, 0, main_page_stack_->count() - 1));
        }
        updateSidebarNavIcons();
        updateCustomTitleBarTexts();
    });
    main_h_layout->addWidget(app_layout_splitter_, 1);
    updateAppSidebarForWidth(currentAppSidebarWidth(), true);
    updateCustomTitleBarTexts();
}

void MainWindow::setupDeviceConfigPage()
{
    device_config_.page = new QWidget(this);
    device_config_.page->setObjectName(QStringLiteral("deviceConfigPage"));
    auto *pageLayout = new QVBoxLayout(device_config_.page);
    pageLayout->setContentsMargins(8, 0, 8, 8);
    pageLayout->setSpacing(8);

    auto *scrollArea = new QScrollArea(device_config_.page);
    scrollArea->setObjectName(QStringLiteral("mainCardsScrollArea"));
    scrollArea->setWidgetResizable(true);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scrollArea->setFrameShape(QFrame::NoFrame);

    auto *content = new QWidget(scrollArea);
    auto *contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(8);
    auto *topRowLayout = new QHBoxLayout;
    topRowLayout->setContentsMargins(0, 0, 0, 0);
    topRowLayout->setSpacing(8);
    topRowLayout->setAlignment(Qt::AlignTop);

    auto createCard = [](QWidget *parent) {
        auto *card = new QGroupBox(parent);
        card->setObjectName(QStringLiteral("sensorGroupBox"));
        card->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        auto *layout = new QVBoxLayout(card);
        layout->setContentsMargins(1, 0, 1, 1);
        layout->setSpacing(0);
        return card;
    };

    auto *serialCard = createCard(content);
    serialCard->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    auto *serialLayout = qobject_cast<QVBoxLayout *>(serialCard->layout());
    auto *serialTitleBar = new QWidget(serialCard);
    serialTitleBar->setObjectName(QStringLiteral("sectionTitleBar"));
    serialTitleBar->setFixedHeight(kMainPageTitleBarHeight);
    auto *serialTitleLayout = new QHBoxLayout(serialTitleBar);
    serialTitleLayout->setContentsMargins(8, 2, 8, 2);
    serialTitleLayout->setSpacing(5);
    QWidget *serialTitleCluster = nullptr;
    device_config_.serial_title_lbl = createSectionTitleCluster(serialTitleBar,
                                                                QStringLiteral("usb"),
                                                                kMainPageButtonHeight,
                                                                &serialTitleCluster);
    serialTitleLayout->addWidget(serialTitleCluster, 0, Qt::AlignVCenter | Qt::AlignLeft);

    device_config_.auto_detect_ports_btn = new QPushButton(serialTitleBar);
    device_config_.auto_detect_ports_btn->setFixedHeight(kMainPageButtonHeight);
    device_config_.auto_detect_ports_btn->setFocusPolicy(Qt::TabFocus);
    device_config_.auto_detect_ports_btn->setMinimumWidth(kDeviceConfigAutoDetectButtonMinWidth);
    connect(device_config_.auto_detect_ports_btn, &QPushButton::clicked, this, &MainWindow::onAutoDetectPortsClicked);
    serialTitleLayout->addWidget(device_config_.auto_detect_ports_btn, 0, Qt::AlignVCenter | Qt::AlignLeft);

    device_config_.data_source_mode_lbl = new QLabel(serialTitleBar);
    device_config_.data_source_mode_lbl->setObjectName(QStringLiteral("fieldLabel"));
    device_config_.data_source_mode_combo = new SingleLevelPopupComboBox(serialTitleBar);
    device_config_.data_source_mode_combo->setFixedHeight(kMainPageInputHeight);
    device_config_.data_source_mode_combo->setFixedWidth(kDeviceConfigSourceModeComboWidth);
    device_config_.data_source_mode_combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    serialTitleLayout->addWidget(device_config_.data_source_mode_lbl, 0, Qt::AlignVCenter | Qt::AlignRight);
    serialTitleLayout->addWidget(device_config_.data_source_mode_combo, 0, Qt::AlignVCenter);

    device_config_.sky_device_config_btn = new QPushButton(serialTitleBar);
    device_config_.sky_device_config_btn->setFixedHeight(kMainPageButtonHeight);
    device_config_.sky_device_config_btn->setFocusPolicy(Qt::TabFocus);
    device_config_.sky_device_config_btn->setMinimumWidth(kDeviceConfigSkyDeviceButtonMinWidth);
    connect(device_config_.sky_device_config_btn, &QPushButton::clicked, this, &MainWindow::onSkyDeviceConfigClicked);
    serialTitleLayout->addWidget(device_config_.sky_device_config_btn, 0, Qt::AlignVCenter | Qt::AlignLeft);
    serialTitleLayout->addStretch(1);
    serialLayout->addWidget(serialTitleBar);

    auto *skyTelemetryRow = new QWidget(serialCard);
    device_config_.sky_telemetry_row_widget = skyTelemetryRow;
    auto *skyTelemetryLayout = new QHBoxLayout(skyTelemetryRow);
    skyTelemetryLayout->setContentsMargins(8, 2, 8, 2);
    skyTelemetryLayout->setSpacing(6);

    device_config_.sky_telemetry_transport_lbl = new QLabel(skyTelemetryRow);
    device_config_.sky_telemetry_transport_lbl->setObjectName(QStringLiteral("fieldLabel"));
    device_config_.sky_telemetry_transport_combo = new QComboBox(skyTelemetryRow);
    device_config_.sky_telemetry_transport_combo->setFixedHeight(kMainPageInputHeight);
    device_config_.sky_telemetry_transport_combo->setFixedWidth(110);

    device_config_.sky_telemetry_tcp_host_lbl = new QLabel(skyTelemetryRow);
    device_config_.sky_telemetry_tcp_host_lbl->setObjectName(QStringLiteral("fieldLabel"));
    device_config_.sky_telemetry_tcp_host_edit = new QLineEdit(skyTelemetryRow);
    device_config_.sky_telemetry_tcp_host_edit->setFixedHeight(kMainPageInputHeight);
    device_config_.sky_telemetry_tcp_host_edit->setMinimumWidth(132);
    device_config_.sky_telemetry_tcp_host_edit->setMaximumWidth(160);

    device_config_.sky_telemetry_tcp_port_lbl = new QLabel(skyTelemetryRow);
    device_config_.sky_telemetry_tcp_port_lbl->setObjectName(QStringLiteral("fieldLabel"));
    device_config_.sky_telemetry_tcp_port_spin = new QSpinBox(skyTelemetryRow);
    device_config_.sky_telemetry_tcp_port_spin->setRange(1, 65535);
    device_config_.sky_telemetry_tcp_port_spin->setFixedHeight(kMainPageInputHeight);
    device_config_.sky_telemetry_tcp_port_spin->setFixedWidth(100);

    device_config_.sky_telemetry_port_lbl = new QLabel(skyTelemetryRow);
    device_config_.sky_telemetry_port_lbl->setObjectName(QStringLiteral("fieldLabel"));
    device_config_.sky_telemetry_port_combo = new QComboBox(skyTelemetryRow);
    device_config_.sky_telemetry_port_combo->setEditable(true);
    device_config_.sky_telemetry_port_combo->setFixedHeight(kMainPageInputHeight);
    device_config_.sky_telemetry_port_combo->setFixedWidth(108);

    device_config_.sky_telemetry_baud_lbl = new QLabel(skyTelemetryRow);
    device_config_.sky_telemetry_baud_lbl->setObjectName(QStringLiteral("fieldLabel"));
    device_config_.sky_telemetry_baud_combo = new QComboBox(skyTelemetryRow);
    device_config_.sky_telemetry_baud_combo->setFixedHeight(kMainPageInputHeight);
    device_config_.sky_telemetry_baud_combo->setFixedWidth(100);

    skyTelemetryLayout->addWidget(device_config_.sky_telemetry_transport_lbl, 0, Qt::AlignVCenter | Qt::AlignLeft);
    skyTelemetryLayout->addWidget(device_config_.sky_telemetry_transport_combo, 0, Qt::AlignVCenter);
    skyTelemetryLayout->addWidget(device_config_.sky_telemetry_tcp_host_lbl, 0, Qt::AlignVCenter | Qt::AlignLeft);
    skyTelemetryLayout->addWidget(device_config_.sky_telemetry_tcp_host_edit, 0, Qt::AlignVCenter);
    skyTelemetryLayout->addWidget(device_config_.sky_telemetry_tcp_port_lbl, 0, Qt::AlignVCenter | Qt::AlignLeft);
    skyTelemetryLayout->addWidget(device_config_.sky_telemetry_tcp_port_spin, 0, Qt::AlignVCenter);
    skyTelemetryLayout->addWidget(device_config_.sky_telemetry_port_lbl, 0, Qt::AlignVCenter | Qt::AlignLeft);
    skyTelemetryLayout->addWidget(device_config_.sky_telemetry_port_combo, 0, Qt::AlignVCenter);
    skyTelemetryLayout->addWidget(device_config_.sky_telemetry_baud_lbl, 0, Qt::AlignVCenter | Qt::AlignLeft);
    skyTelemetryLayout->addWidget(device_config_.sky_telemetry_baud_combo, 0, Qt::AlignVCenter);
    skyTelemetryLayout->addStretch(1);
    serialLayout->addWidget(skyTelemetryRow);

    auto *formRowWidget = new QWidget(serialCard);
    auto *formRowLayout = new QVBoxLayout(formRowWidget);
    formRowLayout->setContentsMargins(0, 0, 0, 0);
    formRowLayout->setSpacing(6);

    auto *formWidget = new QWidget(formRowWidget);
    formWidget->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Minimum);
    auto *formLayout = new QGridLayout(formWidget);
    formLayout->setContentsMargins(6, 4, 6, 8);
    formLayout->setHorizontalSpacing(6);
    formLayout->setVerticalSpacing(5);
    constexpr int kDeviceConfigPortComboWidth = 108;
    constexpr int kDeviceConfigBaudComboWidth = 100;
    constexpr int kDeviceConfigRateComboWidth = 88;

    auto createCombo = [this, formWidget](int width, bool editable = false) {
        auto *combo = new QComboBox(formWidget);
        combo->setEditable(editable);
        combo->setFixedHeight(kMainPageInputHeight);
        combo->setFixedWidth(width);
        combo->setMaxVisibleItems(15);
        configureComboPopup(combo);
        return combo;
    };

    auto addPortRow = [this, formLayout, formWidget, &createCombo](
            QLabel *&label,
            QComboBox *&portCombo,
            QComboBox *&baudCombo,
            QLabel *&rateLabel,
            QComboBox *&rateCombo,
            int row) {
        label = new QLabel(formWidget);
        label->setObjectName(QStringLiteral("fieldLabel"));
        label->setFixedHeight(kMainPageInputHeight);
        label->setFixedWidth(76);
        formLayout->addWidget(label, row, 0, Qt::AlignVCenter | Qt::AlignLeft);

        portCombo = createCombo(kDeviceConfigPortComboWidth, true);
        portCombo->setMinimumContentsLength(6);
        portCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        formLayout->addWidget(portCombo, row, 1, Qt::AlignVCenter);

        baudCombo = createCombo(kDeviceConfigBaudComboWidth);
        formLayout->addWidget(baudCombo, row, 2, Qt::AlignVCenter);

        rateLabel = new QLabel(formWidget);
        rateLabel->setObjectName(QStringLiteral("fieldLabel"));
        rateLabel->setFixedHeight(kMainPageInputHeight);
        formLayout->addWidget(rateLabel, row, 3, Qt::AlignVCenter | Qt::AlignRight);

        if (row == 0)
        {
            rateLabel->setVisible(false);
        }
        else
        {
            rateCombo = createCombo(kDeviceConfigRateComboWidth, true);
            rateCombo->setMinimumContentsLength(4);
            rateCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
            formLayout->addWidget(rateCombo, row, 4, Qt::AlignVCenter);
        }
    };

    QComboBox *unusedEpsilonRateCombo = nullptr;
    addPortRow(device_config_.epsilon_lbl, device_config_.epsilon_port_combo, device_config_.epsilon_baud_combo,
               device_config_.epsilon_rate_lbl, unusedEpsilonRateCombo, 0);
    addPortRow(device_config_.ptb_lbl, device_config_.ptb_port_combo, device_config_.ptb_baud_combo,
               device_config_.ptb_rate_lbl, device_config_.ptb_rate_combo, 1);
    addPortRow(device_config_.hmp_lbl, device_config_.hmp_port_combo, device_config_.hmp_baud_combo,
               device_config_.hmp_rate_lbl, device_config_.hmp_rate_combo, 2);
    addPortRow(device_config_.lidar_lbl, device_config_.lidar_port_combo, device_config_.lidar_baud_combo,
               device_config_.lidar_rate_lbl, device_config_.lidar_rate_combo, 3);
    addPortRow(device_config_.temperature_lbl, device_config_.temperature_port_combo, device_config_.temperature_baud_combo,
               device_config_.temperature_rate_lbl, device_config_.temperature_rate_combo, 4);
    if (device_config_.temperature_port_combo)
    {
        device_config_.temperature_port_combo->setObjectName(QStringLiteral("deviceTemperaturePortCombo"));
    }
    if (device_config_.temperature_baud_combo)
    {
        device_config_.temperature_baud_combo->setObjectName(QStringLiteral("deviceTemperatureBaudCombo"));
    }

    auto addDeviceRemoteButtons = [this, formLayout, formWidget](
            int row,
            QWidget *&buttonsWidget,
            QPushButton *&connectButton,
            QPushButton *&disconnectButton,
            QPushButton *&reconnectButton,
            VaporView::SkyDeviceId device) {
        buttonsWidget = new QWidget(formWidget);
        buttonsWidget->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        auto *layout = new QHBoxLayout(buttonsWidget);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(2);
        auto createButton = [this, buttonsWidget, device](const QString& text, VaporView::CommandId command) {
            auto *button = new QPushButton(text, buttonsWidget);
            button->setFixedHeight(kMainPageButtonHeight);
            button->setFocusPolicy(Qt::TabFocus);
            button->setMinimumWidth(0);
            button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
            connect(button, &QPushButton::clicked, this, [this, command, device]() {
                sendRemoteDeviceCommand(command, device);
            });
            return button;
        };
        connectButton = createButton(QStringLiteral("连接"), VaporView::CommandId::ConnectDevice);
        disconnectButton = createButton(QStringLiteral("断开"), VaporView::CommandId::DisconnectDevice);
        reconnectButton = createButton(QStringLiteral("重连"), VaporView::CommandId::ReconnectDevice);
        layout->addWidget(connectButton);
        layout->addWidget(disconnectButton);
        layout->addWidget(reconnectButton);
        formLayout->addWidget(buttonsWidget, row, 5, Qt::AlignVCenter | Qt::AlignLeft);
    };
    addDeviceRemoteButtons(0, device_config_.epsilon_remote_buttons_widget,
                           device_config_.epsilon_remote_connect_btn,
                           device_config_.epsilon_remote_disconnect_btn,
                           device_config_.epsilon_remote_reconnect_btn,
                           VaporView::SkyDeviceId::Epsilon);
    addDeviceRemoteButtons(1, device_config_.ptb_remote_buttons_widget,
                           device_config_.ptb_remote_connect_btn,
                           device_config_.ptb_remote_disconnect_btn,
                           device_config_.ptb_remote_reconnect_btn,
                           VaporView::SkyDeviceId::Ptb);
    addDeviceRemoteButtons(2, device_config_.hmp_remote_buttons_widget,
                           device_config_.hmp_remote_connect_btn,
                           device_config_.hmp_remote_disconnect_btn,
                           device_config_.hmp_remote_reconnect_btn,
                           VaporView::SkyDeviceId::Hmp);
    addDeviceRemoteButtons(3, device_config_.lidar_remote_buttons_widget,
                           device_config_.lidar_remote_connect_btn,
                           device_config_.lidar_remote_disconnect_btn,
                           device_config_.lidar_remote_reconnect_btn,
                           VaporView::SkyDeviceId::Lidar);
    addDeviceRemoteButtons(4, device_config_.temperature_remote_buttons_widget,
                           device_config_.temperature_remote_connect_btn,
                           device_config_.temperature_remote_disconnect_btn,
                           device_config_.temperature_remote_reconnect_btn,
                           VaporView::SkyDeviceId::TemperatureController);

    device_config_.epsilon_config_card = new QFrame(content);
    device_config_.epsilon_config_card->setObjectName(QStringLiteral("epsilonSectionCard"));
    device_config_.epsilon_config_card->setMinimumWidth(520);
    device_config_.epsilon_config_card->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    auto *epsilonConfigLayout = new QVBoxLayout(device_config_.epsilon_config_card);
    epsilonConfigLayout->setContentsMargins(1, 0, 1, 1);
    epsilonConfigLayout->setSpacing(0);
    auto *epsilonTitleBar = new QWidget(device_config_.epsilon_config_card);
    epsilonTitleBar->setObjectName(QStringLiteral("sectionTitleBar"));
    epsilonTitleBar->setFixedHeight(kMainPageTitleBarHeight);
    auto *epsilonTitleLayout = new QHBoxLayout(epsilonTitleBar);
    epsilonTitleLayout->setContentsMargins(8, 2, 8, 2);
    epsilonTitleLayout->setSpacing(8);
    QWidget *epsilonTitleCluster = nullptr;
    device_config_.epsilon_config_title_lbl = createSectionTitleCluster(epsilonTitleBar,
                                                                        QStringLiteral("sliders-vertical"),
                                                                        kMainPageButtonHeight,
                                                                        &epsilonTitleCluster);
    epsilonTitleLayout->addWidget(epsilonTitleCluster, 0, Qt::AlignVCenter | Qt::AlignLeft);
    epsilonTitleLayout->addStretch(1);
    epsilonConfigLayout->addWidget(epsilonTitleBar);

    auto *epsilonBodyWidget = new QWidget(device_config_.epsilon_config_card);
    auto *epsilonBodyLayout = new QVBoxLayout(epsilonBodyWidget);
    epsilonBodyLayout->setContentsMargins(8, 8, 8, 8);
    epsilonBodyLayout->setSpacing(7);

    device_config_.epsilon_config_hint_lbl = new QLabel(epsilonBodyWidget);
    device_config_.epsilon_config_hint_lbl->setObjectName(QStringLiteral("fieldLabel"));
    device_config_.epsilon_config_hint_lbl->setWordWrap(true);
    device_config_.epsilon_config_hint_lbl->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
    epsilonBodyLayout->addWidget(device_config_.epsilon_config_hint_lbl);

    device_config_.epsilon_packet_custom_check = new QCheckBox(epsilonBodyWidget);
    device_config_.epsilon_packet_custom_check->setFocusPolicy(Qt::TabFocus);
    epsilonBodyLayout->addWidget(device_config_.epsilon_packet_custom_check);

    auto *packetGridWidget = new QWidget(epsilonBodyWidget);
    auto *packetGrid = new QGridLayout(packetGridWidget);
    packetGrid->setContentsMargins(0, 0, 0, 0);
    packetGrid->setHorizontalSpacing(8);
    packetGrid->setVerticalSpacing(4);
    constexpr int kDeviceConfigPacketColumnCount = 2;
    constexpr int kDeviceConfigPacketGroupGapColumn = 2;
    constexpr int kDeviceConfigPacketRightLabelColumn = 3;
    constexpr int kDeviceConfigPacketTrailingColumn = 5;
    int packetComboWidth = 0;
    {
        QComboBox comboProbe(device_config_.epsilon_config_card);
        const QFontMetrics metrics(comboProbe.font());
        for (const EpsilonPacketConfigOption& option : epsilonPacketConfigOptions())
        {
            for (int rateHz : option.supported_rates_hz)
            {
                packetComboWidth = std::max(packetComboWidth,
                                            metrics.horizontalAdvance(epsilonPacketRateDisplayText(rateHz, is_english_)));
            }
        }
    }
    packetComboWidth = std::clamp(packetComboWidth + 50, 126, 160);
    device_config_.epsilon_packet_rate_labels.clear();
    device_config_.epsilon_packet_rate_combos.clear();
    int packetIndex = 0;
    for (const EpsilonPacketConfigOption& option : epsilonPacketConfigOptions())
    {
        const int row = packetIndex / kDeviceConfigPacketColumnCount;
        const int side = packetIndex % kDeviceConfigPacketColumnCount;
        const int labelColumn = side == 0 ? 0 : kDeviceConfigPacketRightLabelColumn;
        const int comboColumn = labelColumn + 1;

        auto *label = new QLabel(packetGridWidget);
        label->setObjectName(QStringLiteral("fieldLabel"));
        label->setProperty("epsilonPacketId", static_cast<uint>(option.packet_id));
        label->setProperty("epsilonPacketGridColumn", side);
        label->setWordWrap(false);
        label->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        packetGrid->addWidget(label, row, labelColumn, Qt::AlignLeft | Qt::AlignVCenter);

        auto *combo = new QComboBox(packetGridWidget);
        combo->setProperty("epsilonPacketId", static_cast<uint>(option.packet_id));
        combo->setProperty("epsilonPacketGridColumn", side);
        combo->setFixedHeight(kMainPageInputHeight);
        combo->setFixedWidth(packetComboWidth);
        combo->setMaxVisibleItems(15);
        for (int rateHz : option.supported_rates_hz)
        {
            combo->addItem(epsilonPacketRateDisplayText(rateHz, is_english_), rateHz);
        }
        packetGrid->addWidget(combo, row, comboColumn, Qt::AlignLeft | Qt::AlignVCenter);

        device_config_.epsilon_packet_rate_labels.append(label);
        device_config_.epsilon_packet_rate_combos.append(combo);

        ++packetIndex;
    }
    packetGrid->setColumnMinimumWidth(kDeviceConfigPacketGroupGapColumn, 20);
    packetGrid->addItem(new QSpacerItem(0, 0, QSizePolicy::Expanding, QSizePolicy::Minimum),
                        0,
                        kDeviceConfigPacketTrailingColumn);
    packetGrid->setColumnStretch(0, 0);
    packetGrid->setColumnStretch(1, 0);
    packetGrid->setColumnStretch(kDeviceConfigPacketGroupGapColumn, 0);
    packetGrid->setColumnStretch(kDeviceConfigPacketRightLabelColumn, 0);
    packetGrid->setColumnStretch(kDeviceConfigPacketRightLabelColumn + 1, 0);
    packetGrid->setColumnStretch(kDeviceConfigPacketTrailingColumn, 1);

    auto createInlineButton = [this](QWidget *parent) {
        auto *button = new QPushButton(parent);
        button->setFixedHeight(kMainPageButtonHeight);
        button->setFocusPolicy(Qt::TabFocus);
        button->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        return button;
    };
    auto *packetButtonPanel = new QWidget(packetGridWidget);
    packetButtonPanel->setObjectName(QStringLiteral("epsilonPacketActionPanel"));
    packetButtonPanel->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Minimum);
    auto *packetButtonLayout = new QGridLayout(packetButtonPanel);
    packetButtonLayout->setContentsMargins(0, 0, 0, 0);
    packetButtonLayout->setHorizontalSpacing(4);
    packetButtonLayout->setVerticalSpacing(4);
    device_config_.epsilon_packet_defaults_btn = createInlineButton(packetButtonPanel);
    device_config_.epsilon_packet_grouped_btn = createInlineButton(packetButtonPanel);
    device_config_.epsilon_packet_save_btn = createInlineButton(packetButtonPanel);
    device_config_.epsilon_rtcm_port_btn = createInlineButton(packetButtonPanel);
    device_config_.epsilon_reconfigure_btn = createInlineButton(packetButtonPanel);
    device_config_.rtk_config_btn = createInlineButton(packetButtonPanel);
    packetButtonLayout->addWidget(device_config_.epsilon_packet_defaults_btn, 0, 0);
    packetButtonLayout->addWidget(device_config_.epsilon_packet_grouped_btn, 0, 1);
    packetButtonLayout->addWidget(device_config_.epsilon_packet_save_btn, 1, 0);
    packetButtonLayout->addWidget(device_config_.epsilon_rtcm_port_btn, 1, 1);
    packetButtonLayout->addWidget(device_config_.epsilon_reconfigure_btn, 2, 0);
    packetButtonLayout->addWidget(device_config_.rtk_config_btn, 2, 1);
    packetGrid->addWidget(packetButtonPanel,
                          0,
                          kDeviceConfigPacketTrailingColumn,
                          4,
                          1,
                          Qt::AlignLeft | Qt::AlignTop);

    connect(device_config_.epsilon_packet_defaults_btn, &QPushButton::clicked, this, [this]() {
        if (device_config_.epsilon_packet_custom_check)
        {
            device_config_.epsilon_packet_custom_check->setChecked(true);
        }
        setDeviceConfigEpsilonPacketRates(defaultEpsilonPacketRates());
    });
    connect(device_config_.epsilon_packet_grouped_btn, &QPushButton::clicked, this, [this]() {
        const QString epsilonRateText = epsilon_rate_combo_ ? epsilon_rate_combo_->currentText() : QStringLiteral("100");
        const int groupedRateHz = effectiveRateOrDefault(epsilonRateText, kDefaultEpsilonSampleRateHz, 200);
        if (device_config_.epsilon_packet_custom_check)
        {
            device_config_.epsilon_packet_custom_check->setChecked(false);
        }
        setDeviceConfigEpsilonPacketRates(groupedEpsilonPacketRates(groupedRateHz));
    });
    connect(device_config_.epsilon_packet_save_btn, &QPushButton::clicked, this, [this]() {
        saveDeviceConfigEpsilonPacketRates(true);
    });
    connect(device_config_.epsilon_rtcm_port_btn, &QPushButton::clicked, this, &MainWindow::onConfigureEpsilonRtcmPortClicked);
    connect(device_config_.epsilon_reconfigure_btn, &QPushButton::clicked, this, &MainWindow::onReconfigureEpsilonClicked);
    connect(device_config_.rtk_config_btn, &QPushButton::clicked, this, &MainWindow::onRtkConfigClicked);
    epsilonBodyLayout->addWidget(packetGridWidget);
    epsilonConfigLayout->addWidget(epsilonBodyWidget);
    formRowLayout->addWidget(formWidget, 0, Qt::AlignTop | Qt::AlignLeft);
    serialLayout->addWidget(formRowWidget, 0, Qt::AlignTop);

    device_config_.data_telemetry_summary_card = new QFrame(content);
    device_config_.data_telemetry_summary_card->setObjectName(QStringLiteral("epsilonSectionCard"));
    device_config_.data_telemetry_summary_card->setMinimumWidth(0);
    device_config_.data_telemetry_summary_card->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Minimum);
    auto *summaryLayout = new QVBoxLayout(device_config_.data_telemetry_summary_card);
    summaryLayout->setContentsMargins(1, 0, 1, 1);
    summaryLayout->setSpacing(0);
    auto *summaryTitleBar = new QWidget(device_config_.data_telemetry_summary_card);
    summaryTitleBar->setObjectName(QStringLiteral("sectionTitleBar"));
    summaryTitleBar->setFixedHeight(kMainPageTitleBarHeight);
    auto *summaryTitleLayout = new QHBoxLayout(summaryTitleBar);
    summaryTitleLayout->setContentsMargins(8, 2, 8, 2);
    summaryTitleLayout->setSpacing(8);
    QWidget *summaryTitleCluster = nullptr;
    device_config_.data_telemetry_summary_title_lbl = createSectionTitleCluster(summaryTitleBar,
                                                                               QStringLiteral("satellite"),
                                                                               kMainPageButtonHeight,
                                                                               &summaryTitleCluster);
    summaryTitleLayout->addWidget(summaryTitleCluster, 0, Qt::AlignVCenter | Qt::AlignLeft);
    summaryTitleLayout->addStretch(1);
    summaryLayout->addWidget(summaryTitleBar);

    auto *summaryBodyWidget = new QWidget(device_config_.data_telemetry_summary_card);
    summaryBodyWidget->setObjectName(QStringLiteral("homeTelemetrySummaryContainer"));
    auto *summaryBodyLayout = new QVBoxLayout(summaryBodyWidget);
    summaryBodyLayout->setContentsMargins(8, 6, 8, 6);
    summaryBodyLayout->setSpacing(2);
    auto createDeviceTelemetrySection = [summaryBodyWidget, summaryBodyLayout](QVBoxLayout *&sectionContentLayout) {
        auto *section = new QFrame(summaryBodyWidget);
        section->setObjectName(QStringLiteral("homeTelemetrySectionCard"));
        section->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        section->setToolTip(QString());
        sectionContentLayout = new QVBoxLayout(section);
        sectionContentLayout->setContentsMargins(0, 0, 0, 0);
        sectionContentLayout->setSpacing(0);
        summaryBodyLayout->addWidget(section, 0);
    };
    createDeviceTelemetrySection(device_config_.data_telemetry_rate_summary_layout);
    createDeviceTelemetrySection(device_config_.data_telemetry_link_summary_layout);
    createDeviceTelemetrySection(device_config_.data_telemetry_device_summary_layout);
    summaryLayout->addWidget(summaryBodyWidget);
    topRowLayout->addWidget(serialCard, 0, Qt::AlignTop | Qt::AlignLeft);
    topRowLayout->addWidget(device_config_.data_telemetry_summary_card, 1, Qt::AlignTop);
    contentLayout->addLayout(topRowLayout);
    contentLayout->addWidget(device_config_.epsilon_config_card, 0, Qt::AlignTop);
    contentLayout->addStretch(1);

    scrollArea->setWidget(content);
    pageLayout->addWidget(scrollArea, 1);
    main_page_stack_->addWidget(device_config_.page);

    auto comboItemsMatch = [](const QComboBox *left, const QComboBox *right) {
        if (!left || !right || left->count() != right->count())
        {
            return false;
        }
        for (int i = 0; i < left->count(); ++i)
        {
            if (left->itemText(i) != right->itemText(i) ||
                left->itemData(i) != right->itemData(i))
            {
                return false;
            }
        }
        return true;
    };

    auto mirrorComboToHome = [this, comboItemsMatch](QComboBox *deviceCombo, QComboBox *homeCombo) {
        if (!deviceCombo || !homeCombo)
        {
            return;
        }
        connect(deviceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [deviceCombo, homeCombo](int index) {
            if (!homeCombo || index < 0 || index >= deviceCombo->count())
            {
                return;
            }
            const QVariant itemData = deviceCombo->itemData(index);
            const int dataIndex = itemData.isValid() ? homeCombo->findData(itemData) : -1;
            const int textIndex = homeCombo->findText(deviceCombo->itemText(index));
            const int targetIndex = dataIndex >= 0 ? dataIndex : textIndex;
            if (targetIndex >= 0 && targetIndex != homeCombo->currentIndex())
            {
                homeCombo->setCurrentIndex(targetIndex);
            }
        });
        connect(deviceCombo, &QComboBox::currentTextChanged, this, [deviceCombo, homeCombo](const QString& text) {
            if (!homeCombo || homeCombo->currentText() == text)
            {
                return;
            }
            const int index = homeCombo->findText(text);
            if (index >= 0)
            {
                homeCombo->setCurrentIndex(index);
            }
            else if (homeCombo->isEditable())
            {
                homeCombo->setEditText(text);
            }
        });
        connect(homeCombo, &QComboBox::currentTextChanged, this, [this, deviceCombo, homeCombo, comboItemsMatch]() {
            if (deviceCombo &&
                homeCombo &&
                deviceCombo->isEditable() == homeCombo->isEditable() &&
                deviceCombo->currentText() == homeCombo->currentText() &&
                comboItemsMatch(homeCombo, deviceCombo))
            {
                return;
            }
            syncDeviceConfigPageFromHome();
        });
    };
    mirrorComboToHome(device_config_.data_source_mode_combo, data_source_mode_combo_);
    mirrorComboToHome(device_config_.sky_telemetry_transport_combo, sky_telemetry_transport_combo_);
    mirrorComboToHome(device_config_.sky_telemetry_port_combo, sky_telemetry_port_combo_);
    mirrorComboToHome(device_config_.sky_telemetry_baud_combo, sky_telemetry_baud_combo_);
    mirrorComboToHome(device_config_.epsilon_port_combo, epsilon_port_combo_);
    mirrorComboToHome(device_config_.epsilon_baud_combo, epsilon_baud_combo_);
    mirrorComboToHome(device_config_.ptb_port_combo, ptb_port_combo_);
    mirrorComboToHome(device_config_.ptb_baud_combo, ptb_baud_combo_);
    mirrorComboToHome(device_config_.hmp_port_combo, hmp_port_combo_);
    mirrorComboToHome(device_config_.hmp_baud_combo, hmp_baud_combo_);
    mirrorComboToHome(device_config_.lidar_port_combo, lidar_port_combo_);
    mirrorComboToHome(device_config_.lidar_baud_combo, lidar_baud_combo_);
    mirrorComboToHome(device_config_.temperature_port_combo, temperature_port_combo_);
    mirrorComboToHome(device_config_.temperature_baud_combo, temperature_baud_combo_);
    mirrorComboToHome(device_config_.ptb_rate_combo, ptb_rate_combo_);
    mirrorComboToHome(device_config_.hmp_rate_combo, hmp_rate_combo_);
    mirrorComboToHome(device_config_.lidar_rate_combo, lidar_rate_combo_);
    mirrorComboToHome(device_config_.temperature_rate_combo, temperature_rate_combo_);

    connect(device_config_.sky_telemetry_tcp_host_edit, &QLineEdit::textChanged, this, [this](const QString& text) {
        if (sky_telemetry_tcp_host_edit_ && sky_telemetry_tcp_host_edit_->text() != text)
        {
            sky_telemetry_tcp_host_edit_->setText(text);
        }
    });
    connect(device_config_.sky_telemetry_tcp_port_spin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
        if (sky_telemetry_tcp_port_spin_ && sky_telemetry_tcp_port_spin_->value() != value)
        {
            sky_telemetry_tcp_port_spin_->setValue(value);
        }
    });
    if (sky_telemetry_tcp_host_edit_)
    {
        connect(sky_telemetry_tcp_host_edit_, &QLineEdit::textChanged, this, [this]() {
            syncDeviceConfigPageFromHome();
        });
    }
    if (sky_telemetry_tcp_port_spin_)
    {
        connect(sky_telemetry_tcp_port_spin_, QOverload<int>::of(&QSpinBox::valueChanged), this, [this]() {
            syncDeviceConfigPageFromHome();
        });
    }

    updateDeviceConfigTexts();
    syncDeviceConfigPageFromHome();
    updateDeviceConfigState();
}

void MainWindow::updateSidebarNavIcons()
{
    const bool dark = dark_theme_enabled_;
    const QColor normalColor = appThemeColor(AppThemeColor::Text, dark);
    const QColor activeColor = QColor(255, 255, 255);
    for (QPushButton *button : {home_nav_btn_, temperature_nav_btn_, rtk_config_nav_btn_, device_config_nav_btn_})
    {
        if (!button)
        {
            continue;
        }
        const QString iconName = button->property(kSidebarIconNameProperty).toString();
        if (iconName.isEmpty())
        {
            button->setIcon(QIcon());
            continue;
        }
        QColor iconColor = button->isChecked() ? activeColor : normalColor;
        if (button == rtk_config_nav_btn_ && rtk_service_running_)
        {
            iconColor = toolbarColor(AppThemeColor::ToolbarGreen);
        }
        button->setIcon(createLucideIcon(iconName, iconColor));
    }
}

void MainWindow::syncDeviceConfigPageFromHome()
{
    if (!device_config_.page)
    {
        return;
    }

    auto copyCombo = [](QComboBox *source, QComboBox *target) {
        if (!source || !target)
        {
            return;
        }
        const QSignalBlocker blocker(target);
        const QString currentText = source->currentText();
        const int currentIndex = source->currentIndex();
        target->setEditable(source->isEditable());
        target->clear();
        for (int i = 0; i < source->count(); ++i)
        {
            target->addItem(source->itemIcon(i), source->itemText(i), source->itemData(i));
        }
        const QVariant currentData = currentIndex >= 0 ? source->itemData(currentIndex) : QVariant();
        int targetIndex = currentData.isValid() ? target->findData(currentData) : -1;
        if (targetIndex < 0)
        {
            targetIndex = target->findText(currentText);
        }
        if (targetIndex >= 0)
        {
            target->setCurrentIndex(targetIndex);
        }
        else if (target->isEditable())
        {
            target->setCurrentText(currentText);
        }
        else
        {
            target->setCurrentIndex(std::clamp(currentIndex, -1, target->count() - 1));
        }
    };

    copyCombo(data_source_mode_combo_, device_config_.data_source_mode_combo);
    copyCombo(sky_telemetry_transport_combo_, device_config_.sky_telemetry_transport_combo);
    copyCombo(sky_telemetry_port_combo_, device_config_.sky_telemetry_port_combo);
    copyCombo(sky_telemetry_baud_combo_, device_config_.sky_telemetry_baud_combo);
    copyCombo(epsilon_port_combo_, device_config_.epsilon_port_combo);
    copyCombo(epsilon_baud_combo_, device_config_.epsilon_baud_combo);
    copyCombo(ptb_port_combo_, device_config_.ptb_port_combo);
    copyCombo(ptb_baud_combo_, device_config_.ptb_baud_combo);
    copyCombo(hmp_port_combo_, device_config_.hmp_port_combo);
    copyCombo(hmp_baud_combo_, device_config_.hmp_baud_combo);
    copyCombo(lidar_port_combo_, device_config_.lidar_port_combo);
    copyCombo(lidar_baud_combo_, device_config_.lidar_baud_combo);
    copyCombo(temperature_port_combo_, device_config_.temperature_port_combo);
    copyCombo(temperature_baud_combo_, device_config_.temperature_baud_combo);
    copyCombo(ptb_rate_combo_, device_config_.ptb_rate_combo);
    copyCombo(hmp_rate_combo_, device_config_.hmp_rate_combo);
    copyCombo(lidar_rate_combo_, device_config_.lidar_rate_combo);
    copyCombo(temperature_rate_combo_, device_config_.temperature_rate_combo);

    if (sky_telemetry_tcp_host_edit_ && device_config_.sky_telemetry_tcp_host_edit)
    {
        const QSignalBlocker blocker(device_config_.sky_telemetry_tcp_host_edit);
        device_config_.sky_telemetry_tcp_host_edit->setText(sky_telemetry_tcp_host_edit_->text());
    }
    if (sky_telemetry_tcp_port_spin_ && device_config_.sky_telemetry_tcp_port_spin)
    {
        const QSignalBlocker blocker(device_config_.sky_telemetry_tcp_port_spin);
        device_config_.sky_telemetry_tcp_port_spin->setValue(sky_telemetry_tcp_port_spin_->value());
    }
    if (device_config_.data_telemetry_summary_card)
    {
        device_config_.data_telemetry_summary_card->setVisible(true);
    }
    updateRemoteTelemetrySummaryLabel();
    syncDeviceConfigEpsilonPanelFromSettings();

    updateDeviceConfigState();
}

void MainWindow::updateDeviceConfigTexts()
{
    if (!device_config_.page)
    {
        return;
    }

    if (device_config_.serial_title_lbl) device_config_.serial_title_lbl->setText(is_english_ ? "Serial Port Configuration" : "串口配置");
    if (device_config_.data_source_mode_lbl) device_config_.data_source_mode_lbl->setText(is_english_ ? "Source:" : "数据源:");
    if (device_config_.sky_telemetry_transport_lbl) device_config_.sky_telemetry_transport_lbl->setText(is_english_ ? "Link:" : "链路:");
    updateSkyTelemetryTransportComboTexts(device_config_.sky_telemetry_transport_combo, is_english_);
    if (device_config_.sky_telemetry_tcp_host_lbl) device_config_.sky_telemetry_tcp_host_lbl->setText(is_english_ ? "Sky IP:" : "天空端IP:");
    if (device_config_.sky_telemetry_tcp_port_lbl) device_config_.sky_telemetry_tcp_port_lbl->setText(is_english_ ? "Port:" : "端口:");
    if (device_config_.sky_telemetry_port_lbl) device_config_.sky_telemetry_port_lbl->setText(is_english_ ? "Serial:" : "串口:");
    if (device_config_.sky_telemetry_baud_lbl) device_config_.sky_telemetry_baud_lbl->setText(is_english_ ? "Baud:" : "波特率:");
    if (device_config_.sky_device_config_btn) device_config_.sky_device_config_btn->setText(is_english_ ? "Sky Device Config" : "天空端设备配置");
    fitButtonFixedWidth(device_config_.sky_device_config_btn,
                        kDeviceConfigSkyDeviceButtonMinWidth,
                        kDeviceConfigTopButtonPadding);
    if (device_config_.epsilon_lbl) device_config_.epsilon_lbl->setText(QStringLiteral("EPSILON:"));
    if (device_config_.ptb_lbl) device_config_.ptb_lbl->setText(QStringLiteral("PTB210:"));
    if (device_config_.hmp_lbl) device_config_.hmp_lbl->setText(QStringLiteral("HMP3:"));
    if (device_config_.lidar_lbl) device_config_.lidar_lbl->setText(QStringLiteral("TFA1500-L:"));
    if (device_config_.temperature_lbl) device_config_.temperature_lbl->setText(QStringLiteral("RD105:"));
    if (device_config_.epsilon_rate_lbl) device_config_.epsilon_rate_lbl->setText(QString());
    if (device_config_.ptb_rate_lbl) device_config_.ptb_rate_lbl->setText(is_english_ ? "Rate:" : "频率:");
    if (device_config_.hmp_rate_lbl) device_config_.hmp_rate_lbl->setText(is_english_ ? "Rate:" : "频率:");
    if (device_config_.lidar_rate_lbl) device_config_.lidar_rate_lbl->setText(is_english_ ? "Rate:" : "频率:");
    if (device_config_.temperature_rate_lbl) device_config_.temperature_rate_lbl->setText(is_english_ ? "Poll:" : "轮询:");
    if (device_config_.epsilon_config_title_lbl)
    {
        device_config_.epsilon_config_title_lbl->setText(is_english_ ? "EPSILON Configuration" : "EPSILON 配置");
    }
    if (device_config_.data_telemetry_summary_title_lbl)
    {
        device_config_.data_telemetry_summary_title_lbl->setText(is_english_
            ? "Sky-ground Communication Link Status"
            : "天地通信链路状态");
    }
    if (device_config_.epsilon_config_hint_lbl)
    {
        device_config_.epsilon_config_hint_lbl->setText(is_english_
            ? "Packet rates are saved for future connect/reconfigure operations. Save applies the profile immediately when an EPSILON port is selected."
            : "包频率会用于后续连接和重配；已选择 EPSILON 串口时，保存后会立即应用。");
    }
    if (device_config_.epsilon_packet_custom_check)
    {
        device_config_.epsilon_packet_custom_check->setText(is_english_
            ? "Use this custom EPSILON packet-rate profile"
            : "使用这组自定义 EPSILON 包频率");
    }
    for (int i = 0;
         i < device_config_.epsilon_packet_rate_labels.size() &&
         i < static_cast<int>(epsilonPacketConfigOptions().size());
         ++i)
    {
        if (QLabel *label = device_config_.epsilon_packet_rate_labels.at(i))
        {
            label->setText(epsilonPacketDialogRowLabel(epsilonPacketConfigOptions().at(i), is_english_));
            label->setToolTip(label->text());
        }
    }
    for (QComboBox *combo : device_config_.epsilon_packet_rate_combos)
    {
        if (!combo)
        {
            continue;
        }
        const QSignalBlocker blocker(combo);
        for (int i = 0; i < combo->count(); ++i)
        {
            combo->setItemText(i, epsilonPacketRateDisplayText(combo->itemData(i).toInt(), is_english_));
        }
    }
    if (device_config_.epsilon_packet_defaults_btn)
    {
        device_config_.epsilon_packet_defaults_btn->setText(is_english_ ? "Recommended" : "恢复推荐");
        device_config_.epsilon_packet_defaults_btn->setToolTip(is_english_ ? "Use the recommended default packet rates" : "恢复推荐默认包频率");
        fitButtonMinimumWidth(device_config_.epsilon_packet_defaults_btn, 100);
    }
    if (device_config_.epsilon_packet_grouped_btn)
    {
        device_config_.epsilon_packet_grouped_btn->setText(is_english_ ? "Grouped" : "分组模式");
        device_config_.epsilon_packet_grouped_btn->setToolTip(is_english_ ? "Use the grouped output-rate profile" : "切换到分组输出频率模式");
        fitButtonMinimumWidth(device_config_.epsilon_packet_grouped_btn, 100);
    }
    if (device_config_.epsilon_packet_save_btn)
    {
        device_config_.epsilon_packet_save_btn->setText(is_english_ ? "Save + Apply" : "保存并应用");
        device_config_.epsilon_packet_save_btn->setToolTip(is_english_ ? "Save the packet-rate profile and apply it now when possible" : "保存包频率配置，并在可用时立即应用");
        fitButtonMinimumWidth(device_config_.epsilon_packet_save_btn, 118);
    }
    if (device_config_.epsilon_rtcm_port_btn)
    {
        device_config_.epsilon_rtcm_port_btn->setText(is_english_ ? "RTCM Port" : "配置RTCM串口");
        device_config_.epsilon_rtcm_port_btn->setToolTip(is_english_ ? "Configure EPSILON communication port 2 as RTCM input" : "配置 EPSILON 第二通信串口为 RTCM 输入口");
        fitButtonMinimumWidth(device_config_.epsilon_rtcm_port_btn, 128);
    }
    if (device_config_.epsilon_reconfigure_btn)
    {
        device_config_.epsilon_reconfigure_btn->setText(is_english_ ? "Reconfigure Output" : "重新配置输出");
        device_config_.epsilon_reconfigure_btn->setToolTip(is_english_ ? "Apply the current EPSILON output profile" : "应用当前 EPSILON 输出配置");
        fitButtonMinimumWidth(device_config_.epsilon_reconfigure_btn, 128);
    }
    if (device_config_.rtk_config_btn)
    {
        device_config_.rtk_config_btn->setText(is_english_ ? "RTK Config" : "RTK配置");
        device_config_.rtk_config_btn->setToolTip(is_english_ ? "Open RTK config" : "打开 RTK 配置");
        fitButtonMinimumWidth(device_config_.rtk_config_btn, 100);
    }

    const QString connectText = is_english_ ? "Connect" : "连接";
    const QString disconnectText = is_english_ ? "Disconnect" : "断开";
    const QString reconnectText = is_english_ ? "Reconnect" : "重连";
    for (QPushButton *button : {device_config_.epsilon_remote_connect_btn,
                                device_config_.ptb_remote_connect_btn,
                                device_config_.hmp_remote_connect_btn,
                                device_config_.lidar_remote_connect_btn,
                                device_config_.temperature_remote_connect_btn})
    {
        if (button)
        {
            button->setText(connectText);
            fitButtonMinimumWidth(button, 64);
        }
    }
    for (QPushButton *button : {device_config_.epsilon_remote_disconnect_btn,
                                device_config_.ptb_remote_disconnect_btn,
                                device_config_.hmp_remote_disconnect_btn,
                                device_config_.lidar_remote_disconnect_btn,
                                device_config_.temperature_remote_disconnect_btn})
    {
        if (button)
        {
            button->setText(disconnectText);
            fitButtonMinimumWidth(button, 64);
        }
    }
    for (QPushButton *button : {device_config_.epsilon_remote_reconnect_btn,
                                device_config_.ptb_remote_reconnect_btn,
                                device_config_.hmp_remote_reconnect_btn,
                                device_config_.lidar_remote_reconnect_btn,
                                device_config_.temperature_remote_reconnect_btn})
    {
        if (button)
        {
            button->setText(reconnectText);
            fitButtonMinimumWidth(button, 64);
        }
    }

    updateDeviceConfigState();
}

void MainWindow::updateDeviceConfigState()
{
    if (!device_config_.page)
    {
        return;
    }

    const bool remote = isRemoteSkyMode();
    const bool tcpTelemetry = isRemoteSkyTcpMode();
    const bool localInputsEnabled = !remote && !is_connected_ &&
        !connection_attempt_in_progress_ && !port_detection_in_progress_ && !epsilon_reconfigure_in_progress_;
    const bool remoteInputsEnabled = remote && !is_connected_ && !connection_attempt_in_progress_;
    const bool remoteCommandEnabled = remote && ground_telemetry_service_ && ground_telemetry_service_->isOpen();
    const bool epsilonConfigEnabled = !remote && !connection_attempt_in_progress_ &&
        !port_detection_in_progress_ && !epsilon_reconfigure_in_progress_;

    if (device_config_.auto_detect_ports_btn)
    {
        device_config_.auto_detect_ports_btn->setEnabled(auto_detect_ports_btn_ && auto_detect_ports_btn_->isEnabled());
        device_config_.auto_detect_ports_btn->setText(auto_detect_ports_btn_ ? auto_detect_ports_btn_->text() : QString());
        device_config_.auto_detect_ports_btn->setToolTip(auto_detect_ports_btn_ ? auto_detect_ports_btn_->toolTip() : QString());
        fitButtonFixedWidth(device_config_.auto_detect_ports_btn,
                            kDeviceConfigAutoDetectButtonMinWidth,
                            kDeviceConfigTopButtonPadding);
    }
    if (device_config_.sky_device_config_btn)
    {
        device_config_.sky_device_config_btn->setEnabled(sky_device_config_btn_ && sky_device_config_btn_->isEnabled());
        device_config_.sky_device_config_btn->setToolTip(sky_device_config_btn_ ? sky_device_config_btn_->toolTip() : QString());
    }
    if (device_config_.epsilon_config_card)
    {
        device_config_.epsilon_config_card->setVisible(true);
        device_config_.epsilon_config_card->setEnabled(epsilonConfigEnabled);
    }

    const QList<QWidget *> localWidgets = {
        device_config_.epsilon_port_combo,
        device_config_.epsilon_baud_combo,
        device_config_.ptb_port_combo,
        device_config_.ptb_baud_combo,
        device_config_.hmp_port_combo,
        device_config_.hmp_baud_combo,
        device_config_.lidar_port_combo,
        device_config_.lidar_baud_combo,
        device_config_.temperature_port_combo,
        device_config_.temperature_baud_combo,
        device_config_.ptb_rate_combo,
        device_config_.hmp_rate_combo,
        device_config_.lidar_rate_combo,
        device_config_.temperature_rate_combo
    };
    for (QWidget *widget : localWidgets)
    {
        if (widget)
        {
            widget->setEnabled(localInputsEnabled);
        }
    }

    if (device_config_.data_source_mode_combo) device_config_.data_source_mode_combo->setEnabled(data_source_mode_combo_ && data_source_mode_combo_->isEnabled());
    if (device_config_.sky_telemetry_transport_combo) device_config_.sky_telemetry_transport_combo->setEnabled(remoteInputsEnabled);
    if (device_config_.sky_telemetry_port_combo) device_config_.sky_telemetry_port_combo->setEnabled(remoteInputsEnabled && !tcpTelemetry);
    if (device_config_.sky_telemetry_baud_combo) device_config_.sky_telemetry_baud_combo->setEnabled(remoteInputsEnabled && !tcpTelemetry);
    if (device_config_.sky_telemetry_tcp_host_edit) device_config_.sky_telemetry_tcp_host_edit->setEnabled(remoteInputsEnabled && tcpTelemetry);
    if (device_config_.sky_telemetry_tcp_port_spin) device_config_.sky_telemetry_tcp_port_spin->setEnabled(remoteInputsEnabled && tcpTelemetry);

    if (device_config_.sky_telemetry_row_widget) device_config_.sky_telemetry_row_widget->setVisible(true);
    if (device_config_.sky_telemetry_transport_lbl) device_config_.sky_telemetry_transport_lbl->setVisible(true);
    if (device_config_.sky_telemetry_transport_combo) device_config_.sky_telemetry_transport_combo->setVisible(true);
    if (device_config_.sky_telemetry_port_lbl) device_config_.sky_telemetry_port_lbl->setVisible(!tcpTelemetry);
    if (device_config_.sky_telemetry_port_combo) device_config_.sky_telemetry_port_combo->setVisible(!tcpTelemetry);
    if (device_config_.sky_telemetry_baud_lbl) device_config_.sky_telemetry_baud_lbl->setVisible(!tcpTelemetry);
    if (device_config_.sky_telemetry_baud_combo) device_config_.sky_telemetry_baud_combo->setVisible(!tcpTelemetry);
    if (device_config_.sky_telemetry_tcp_host_lbl) device_config_.sky_telemetry_tcp_host_lbl->setVisible(tcpTelemetry);
    if (device_config_.sky_telemetry_tcp_host_edit) device_config_.sky_telemetry_tcp_host_edit->setVisible(tcpTelemetry);
    if (device_config_.sky_telemetry_tcp_port_lbl) device_config_.sky_telemetry_tcp_port_lbl->setVisible(tcpTelemetry);
    if (device_config_.sky_telemetry_tcp_port_spin) device_config_.sky_telemetry_tcp_port_spin->setVisible(tcpTelemetry);

    for (QWidget *widget : {device_config_.epsilon_remote_buttons_widget,
                            device_config_.ptb_remote_buttons_widget,
                            device_config_.hmp_remote_buttons_widget,
                            device_config_.lidar_remote_buttons_widget,
                            device_config_.temperature_remote_buttons_widget})
    {
        if (widget)
        {
            widget->setVisible(true);
        }
    }
    for (QPushButton *button : {device_config_.epsilon_remote_connect_btn,
                                device_config_.epsilon_remote_disconnect_btn,
                                device_config_.epsilon_remote_reconnect_btn,
                                device_config_.ptb_remote_connect_btn,
                                device_config_.ptb_remote_disconnect_btn,
                                device_config_.ptb_remote_reconnect_btn,
                                device_config_.hmp_remote_connect_btn,
                                device_config_.hmp_remote_disconnect_btn,
                                device_config_.hmp_remote_reconnect_btn,
                                device_config_.lidar_remote_connect_btn,
                                device_config_.lidar_remote_disconnect_btn,
                                device_config_.lidar_remote_reconnect_btn,
                                device_config_.temperature_remote_connect_btn,
                                device_config_.temperature_remote_disconnect_btn,
                                device_config_.temperature_remote_reconnect_btn})
    {
        if (button)
        {
            button->setEnabled(remoteCommandEnabled);
        }
    }
}

QStringList MainWindow::getAvailablePorts()
{
    QStringList ports;
    const auto infos = QSerialPortInfo::availablePorts();
    for (const QSerialPortInfo& info : infos)
    {
#ifdef _WIN32
        ports.append(info.portName());
#else
        const QString path = info.systemLocation();
        ports.append(path.isEmpty() ? info.portName() : path);
#endif
    }

    ports.removeDuplicates();
    ports.sort();
    return ports;
}

void MainWindow::setupConfigPanel()
{
    config_group_ = new QGroupBox(this);
    config_group_->setObjectName("sensorGroupBox");
    config_group_->setMinimumWidth(kHomeOverviewDeviceMinWidth);
    config_group_->setMinimumHeight(kConfigCardMinHeight);
    config_group_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    auto *config_root_layout = new QVBoxLayout(config_group_);
    config_root_layout->setSpacing(0);
    config_root_layout->setContentsMargins(kHomeOverviewCardOuterPadding,
                                           0,
                                           kHomeOverviewCardOuterPadding,
                                           kConfigCardBottomPadding);

    auto *config_form_widget = new QWidget(config_group_);
    config_form_widget->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    config_form_widget->hide();
    auto *config_layout = new QGridLayout(config_form_widget);
    config_layout->setVerticalSpacing(4);
    config_layout->setHorizontalSpacing(8);
    config_layout->setContentsMargins(8, 0, 8, kConfigFormBottomPadding);
    config_layout->setColumnStretch(0, 0);
    config_layout->setColumnStretch(1, 0);
    config_layout->setColumnStretch(2, 0);
    config_layout->setColumnStretch(3, 0);
    config_layout->setColumnStretch(4, 0);
    config_layout->setColumnStretch(5, 0);
    config_layout->setColumnStretch(6, 1);
    config_layout->setColumnMinimumWidth(0, 110);
    config_layout->setColumnMinimumWidth(1, 170);
    config_layout->setColumnMinimumWidth(2, 100);
    config_layout->setColumnMinimumWidth(3, 80);
    config_layout->setColumnMinimumWidth(4, 100);

    QStringList baudRates = {"9600", "19200", "38400", "57600", "115200", "230400", "460800", "500000", "921600"};
    QStringList ports = getAvailablePorts();

    auto createRateCombo = [this, config_form_widget](int maxRate = 500) {
        auto *combo = new QComboBox(config_form_widget);
        const QList<int> supportedRates = {1, 2, 5, 10, 20, 50, 70, 100, 200, 250, 500, 1000};
        for (int rate : supportedRates)
        {
            if (rate <= maxRate)
            {
                combo->addItem(QString::number(rate));
            }
        }
        const int preferredIndex = combo->findText(maxRate >= 200 ? QStringLiteral("200") : QStringLiteral("20"));
        combo->setCurrentIndex(preferredIndex >= 0 ? preferredIndex : 0);
        combo->setEditable(true);
        combo->setFixedHeight(kMainPageInputHeight);
        combo->setFixedWidth(100);
        combo->setValidator(new QIntValidator(1, maxRate, combo));
        configureComboPopup(combo);
        return combo;
    };

    auto addNoSetRateOption = [this](QComboBox *combo) {
        if (!combo)
        {
            return;
        }
        if (combo->findText(QStringLiteral("No Set")) < 0 &&
            combo->findText(QStringLiteral("不设定")) < 0)
        {
            combo->addItem(is_english_ ? QStringLiteral("No Set") : QStringLiteral("不设定"));
        }
        combo->setValidator(nullptr);
    };

    auto createPortRow = [this, config_form_widget, config_layout, &baudRates, &ports, &createRateCombo](QLabel*& lbl, QComboBox*& portCombo, QComboBox*& baudCombo, QLabel*& rateLbl, QComboBox*& rateCombo, const QString& defaultPort, const QString& defaultBaud, int row, int maxRate = 500) {
        lbl = new QLabel(config_form_widget);
        lbl->setObjectName("fieldLabel");
        lbl->setFixedHeight(kMainPageInputHeight);
        lbl->setFixedWidth(80);
        config_layout->addWidget(lbl, row, 0, Qt::AlignVCenter | Qt::AlignLeft);

        portCombo = new QComboBox(config_form_widget);
        portCombo->addItem(is_english_ ? "-- Select --" : "-- 选择 --");
        portCombo->addItems(ports);
        portCombo->setEditable(true);
        portCombo->setMinimumContentsLength(10);
        portCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        portCombo->setFixedHeight(kMainPageInputHeight);
        portCombo->setMinimumWidth(160);
        portCombo->setMaximumWidth(190);
        portCombo->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        portCombo->setMaxVisibleItems(15);
        configureComboPopup(portCombo);

        int defaultIdx = portCombo->findText(defaultPort);
        if (defaultIdx >= 0)
        {
            portCombo->setCurrentIndex(defaultIdx);
        }
        else
        {
            portCombo->setEditText(defaultPort);
        }
        config_layout->addWidget(portCombo, row, 1, Qt::AlignVCenter);

        baudCombo = new QComboBox(config_form_widget);
        baudCombo->addItems(baudRates);
        baudCombo->setCurrentText(defaultBaud);
        baudCombo->setFixedHeight(kMainPageInputHeight);
        baudCombo->setFixedWidth(100);
        configureComboPopup(baudCombo);
        config_layout->addWidget(baudCombo, row, 2, Qt::AlignVCenter);

        rateLbl = new QLabel(config_form_widget);
        rateLbl->setObjectName("fieldLabel");
        rateLbl->setFixedHeight(kMainPageInputHeight);
        config_layout->addWidget(rateLbl, row, 3, Qt::AlignVCenter | Qt::AlignRight);

        rateCombo = createRateCombo(maxRate);
        config_layout->addWidget(rateCombo, row, 4, Qt::AlignVCenter);
    };

    auto *configTitleBar = new QWidget(config_group_);
    configTitleBar->setObjectName("sectionTitleBar");
    configTitleBar->setFixedHeight(kMainPageTitleBarHeight);
    auto *configTitleLayout = new QHBoxLayout(configTitleBar);
    configTitleLayout->setContentsMargins(8, 2, 8, 2);
    configTitleLayout->setSpacing(8);

    QWidget *configTitleCluster = nullptr;
    config_inline_title_lbl_ = createSectionTitleCluster(configTitleBar,
                                                         QStringLiteral("usb"),
                                                         kMainPageButtonHeight,
                                                         &configTitleCluster);
    configTitleLayout->addWidget(configTitleCluster, 0, Qt::AlignVCenter | Qt::AlignLeft);

    configTitleLayout->addStretch(1);

    source_mode_switch_ = new SourceModeOverviewSwitchButton(configTitleBar);
    source_mode_switch_->setFixedSize(128, kMainPageButtonHeight);
    source_mode_switch_->setEnglish(is_english_);
    connect(source_mode_switch_, &QPushButton::clicked, this, [this]() {
        if (!data_source_mode_combo_)
        {
            return;
        }
        data_source_mode_combo_->setCurrentIndex(source_mode_switch_->switchChecked() ? 0 : 1);
    });
    configTitleLayout->addWidget(source_mode_switch_, 0, Qt::AlignVCenter | Qt::AlignRight);

    auto_detect_ports_btn_ = new QPushButton(config_form_widget);
    auto_detect_ports_btn_->setFixedHeight(kMainPageButtonHeight);
    auto_detect_ports_btn_->setMinimumWidth(120);
    connect(auto_detect_ports_btn_, &QPushButton::clicked, this, &MainWindow::onAutoDetectPortsClicked);

    data_source_mode_lbl_ = new QLabel(config_form_widget);
    data_source_mode_lbl_->setObjectName("fieldLabel");
    data_source_mode_combo_ = new SingleLevelPopupComboBox(config_form_widget);
    data_source_mode_combo_->addItem(sourceModeDisplayText(false, 0));
    data_source_mode_combo_->addItem(sourceModeDisplayText(false, 1));
    data_source_mode_combo_->setFixedHeight(kMainPageInputHeight);
    data_source_mode_combo_->setMinimumWidth(180);
    data_source_mode_combo_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    connect(data_source_mode_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onDataSourceModeChanged);

    sky_device_config_btn_ = new QPushButton(config_form_widget);
    sky_device_config_btn_->setFixedHeight(kMainPageButtonHeight);
    sky_device_config_btn_->setMinimumWidth(150);
    connect(sky_device_config_btn_, &QPushButton::clicked, this, &MainWindow::onSkyDeviceConfigClicked);
    config_root_layout->addWidget(configTitleBar);

    sky_telemetry_row_widget_ = new QWidget(config_form_widget);
    sky_telemetry_transport_lbl_ = new QLabel(sky_telemetry_row_widget_);
    sky_telemetry_transport_lbl_->setObjectName("fieldLabel");
    sky_telemetry_transport_combo_ = new QComboBox(sky_telemetry_row_widget_);
    sky_telemetry_transport_combo_->addItem(skyTelemetryTransportDisplayText(false, QStringLiteral("tcp")), QStringLiteral("tcp"));
    sky_telemetry_transport_combo_->addItem(skyTelemetryTransportDisplayText(false, QStringLiteral("serial")), QStringLiteral("serial"));
    sky_telemetry_transport_combo_->setFixedHeight(kMainPageInputHeight);
    sky_telemetry_transport_combo_->setFixedWidth(110);
    connect(sky_telemetry_transport_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) {
                updateSourceModeUi();
                saveRememberedInputState();
            });

    sky_telemetry_tcp_host_lbl_ = new QLabel(sky_telemetry_row_widget_);
    sky_telemetry_tcp_host_lbl_->setObjectName("fieldLabel");
    sky_telemetry_tcp_host_edit_ = new QLineEdit(sky_telemetry_row_widget_);
    sky_telemetry_tcp_host_edit_->setText(QStringLiteral("192.168.1.2"));
    sky_telemetry_tcp_host_edit_->setFixedHeight(kMainPageInputHeight);
    sky_telemetry_tcp_host_edit_->setMinimumWidth(150);
    sky_telemetry_tcp_host_edit_->setMaximumWidth(180);

    sky_telemetry_tcp_port_lbl_ = new QLabel(sky_telemetry_row_widget_);
    sky_telemetry_tcp_port_lbl_->setObjectName("fieldLabel");
    sky_telemetry_tcp_port_spin_ = new QSpinBox(sky_telemetry_row_widget_);
    sky_telemetry_tcp_port_spin_->setRange(1, 65535);
    sky_telemetry_tcp_port_spin_->setValue(39100);
    sky_telemetry_tcp_port_spin_->setFixedHeight(kMainPageInputHeight);
    sky_telemetry_tcp_port_spin_->setFixedWidth(100);

    sky_telemetry_port_lbl_ = new QLabel(sky_telemetry_row_widget_);
    sky_telemetry_port_lbl_->setObjectName("fieldLabel");
    sky_telemetry_port_combo_ = new QComboBox(sky_telemetry_row_widget_);
    sky_telemetry_port_combo_->addItems(ports);
    sky_telemetry_port_combo_->setEditable(true);
    sky_telemetry_port_combo_->setFixedHeight(kMainPageInputHeight);
    sky_telemetry_port_combo_->setMinimumWidth(160);
#ifdef _WIN32
    sky_telemetry_port_combo_->setEditText(QStringLiteral("COM11"));
#else
    sky_telemetry_port_combo_->setEditText(QStringLiteral("/tmp/vapor_ground"));
#endif
    sky_telemetry_baud_lbl_ = new QLabel(sky_telemetry_row_widget_);
    sky_telemetry_baud_lbl_->setObjectName("fieldLabel");
    sky_telemetry_baud_combo_ = new QComboBox(sky_telemetry_row_widget_);
    sky_telemetry_baud_combo_->addItems(baudRates);
    sky_telemetry_baud_combo_->setCurrentText(QStringLiteral("921600"));
    sky_telemetry_baud_combo_->setFixedHeight(kMainPageInputHeight);
    sky_telemetry_baud_combo_->setFixedWidth(100);
    auto *skyTelemetryLayout = new QHBoxLayout(sky_telemetry_row_widget_);
    skyTelemetryLayout->setContentsMargins(8, 2, 8, 2);
    skyTelemetryLayout->setSpacing(8);
    skyTelemetryLayout->addWidget(sky_telemetry_transport_lbl_, 0, Qt::AlignVCenter | Qt::AlignLeft);
    skyTelemetryLayout->addWidget(sky_telemetry_transport_combo_, 0, Qt::AlignVCenter);
    skyTelemetryLayout->addWidget(sky_telemetry_tcp_host_lbl_, 0, Qt::AlignVCenter | Qt::AlignLeft);
    skyTelemetryLayout->addWidget(sky_telemetry_tcp_host_edit_, 0, Qt::AlignVCenter);
    skyTelemetryLayout->addWidget(sky_telemetry_tcp_port_lbl_, 0, Qt::AlignVCenter | Qt::AlignLeft);
    skyTelemetryLayout->addWidget(sky_telemetry_tcp_port_spin_, 0, Qt::AlignVCenter);
    skyTelemetryLayout->addWidget(sky_telemetry_port_lbl_, 0, Qt::AlignVCenter | Qt::AlignLeft);
    skyTelemetryLayout->addWidget(sky_telemetry_port_combo_, 0, Qt::AlignVCenter);
    skyTelemetryLayout->addWidget(sky_telemetry_baud_lbl_, 0, Qt::AlignVCenter | Qt::AlignLeft);
    skyTelemetryLayout->addWidget(sky_telemetry_baud_combo_, 0, Qt::AlignVCenter);
    skyTelemetryLayout->addStretch(1);
    sky_telemetry_row_widget_->setVisible(true);

    int row = 0;

#ifdef _WIN32
    createPortRow(epsilon_lbl_, epsilon_port_combo_, epsilon_baud_combo_, epsilon_rate_lbl_, epsilon_rate_combo_, "COM3", "921600", row++, 200);
    createPortRow(ptb_lbl_, ptb_port_combo_, ptb_baud_combo_, ptb_rate_lbl_, ptb_rate_combo_, "COM5", "9600", row++, kPtbMaxSampleRateHz);
    createPortRow(hmp_lbl_, hmp_port_combo_, hmp_baud_combo_, hmp_rate_lbl_, hmp_rate_combo_, "COM6", "19200", row++);
    createPortRow(lidar_lbl_, lidar_port_combo_, lidar_baud_combo_, lidar_rate_lbl_, lidar_rate_combo_, "COM7", "500000", row++, 100);
    createPortRow(temperature_lbl_, temperature_port_combo_, temperature_baud_combo_, temperature_rate_lbl_, temperature_rate_combo_, "COM9", "38400", row++, kMaxTemperatureSampleRateHz);
#else
    createPortRow(epsilon_lbl_, epsilon_port_combo_, epsilon_baud_combo_, epsilon_rate_lbl_, epsilon_rate_combo_, "/dev/ttyEPSILON", "921600", row++, 200);
    createPortRow(ptb_lbl_, ptb_port_combo_, ptb_baud_combo_, ptb_rate_lbl_, ptb_rate_combo_, "/dev/ttyBARO", "9600", row++, kPtbMaxSampleRateHz);
    createPortRow(hmp_lbl_, hmp_port_combo_, hmp_baud_combo_, hmp_rate_lbl_, hmp_rate_combo_, "/dev/ttyHMP", "19200", row++);
    createPortRow(lidar_lbl_, lidar_port_combo_, lidar_baud_combo_, lidar_rate_lbl_, lidar_rate_combo_, "/dev/ttyLidar", "500000", row++, 100);
    createPortRow(temperature_lbl_, temperature_port_combo_, temperature_baud_combo_, temperature_rate_lbl_, temperature_rate_combo_, "/dev/ttyRD105", "38400", row++, kMaxTemperatureSampleRateHz);
#endif
    if (temperature_port_combo_) temperature_port_combo_->setObjectName(QStringLiteral("temperaturePortCombo"));
    if (temperature_baud_combo_) temperature_baud_combo_->setObjectName(QStringLiteral("temperatureBaudCombo"));
    if (temperature_rate_combo_) temperature_rate_combo_->setObjectName(QStringLiteral("temperatureRateCombo"));
    if (temperature_rate_combo_)
    {
        temperature_rate_combo_->setCurrentText(QString::number(kDefaultTemperatureSampleRateHz));
    }

    auto addRemoteButtons = [this, config_form_widget, config_layout](int rowIndex,
                                                  QWidget*& buttonsWidget,
                                                  QPushButton*& connectButton,
                                                  QPushButton*& disconnectButton,
                                                  QPushButton*& reconnectButton,
                                                  VaporView::SkyDeviceId device) {
        auto *buttons = new QWidget(config_form_widget);
        buttonsWidget = buttons;
        auto *layout = new QHBoxLayout(buttons);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(4);
        connectButton = createRemoteDeviceButton(QStringLiteral("连接"), VaporView::CommandId::ConnectDevice, device);
        disconnectButton = createRemoteDeviceButton(QStringLiteral("断开"), VaporView::CommandId::DisconnectDevice, device);
        reconnectButton = createRemoteDeviceButton(QStringLiteral("重连"), VaporView::CommandId::ReconnectDevice, device);
        layout->addWidget(connectButton);
        layout->addWidget(disconnectButton);
        layout->addWidget(reconnectButton);
        layout->addStretch();
        config_layout->addWidget(buttons, rowIndex, 5, Qt::AlignVCenter | Qt::AlignLeft);
    };
    addRemoteButtons(0, epsilon_remote_buttons_widget_, epsilon_remote_connect_btn_, epsilon_remote_disconnect_btn_, epsilon_remote_reconnect_btn_, VaporView::SkyDeviceId::Epsilon);
    addRemoteButtons(1, ptb_remote_buttons_widget_, ptb_remote_connect_btn_, ptb_remote_disconnect_btn_, ptb_remote_reconnect_btn_, VaporView::SkyDeviceId::Ptb);
    addRemoteButtons(2, hmp_remote_buttons_widget_, hmp_remote_connect_btn_, hmp_remote_disconnect_btn_, hmp_remote_reconnect_btn_, VaporView::SkyDeviceId::Hmp);
    addRemoteButtons(3, lidar_remote_buttons_widget_, lidar_remote_connect_btn_, lidar_remote_disconnect_btn_, lidar_remote_reconnect_btn_, VaporView::SkyDeviceId::Lidar);
    addRemoteButtons(4, temperature_remote_buttons_widget_, temperature_remote_connect_btn_, temperature_remote_disconnect_btn_, temperature_remote_reconnect_btn_, VaporView::SkyDeviceId::TemperatureController);

    if (epsilon_rate_combo_)
    {
        config_layout->removeWidget(epsilon_rate_combo_);
        delete epsilon_rate_combo_;
        epsilon_rate_combo_ = nullptr;
        epsilon_packet_rates_btn_ = new QPushButton(config_form_widget);
        epsilon_packet_rates_btn_->setFixedHeight(kMainPageButtonHeight);
        epsilon_packet_rates_btn_->setMinimumWidth(140);
        connect(epsilon_packet_rates_btn_, &QPushButton::clicked, this, &MainWindow::onConfigureEpsilonPacketRatesClicked);
        config_layout->addWidget(epsilon_packet_rates_btn_, 0, 4, Qt::AlignVCenter);
    }

    for (QComboBox *combo : {ptb_rate_combo_, hmp_rate_combo_, lidar_rate_combo_, temperature_rate_combo_})
    {
        addNoSetRateOption(combo);
    }

    connect(ptb_rate_combo_, &QComboBox::currentTextChanged, this, &MainWindow::onPtbRateChanged);
    connect(hmp_rate_combo_, &QComboBox::currentTextChanged, this, &MainWindow::onHmpRateChanged);
    connect(lidar_rate_combo_, &QComboBox::currentTextChanged, this, &MainWindow::onLidarRateChanged);
    connect(temperature_rate_combo_, &QComboBox::currentTextChanged, this, &MainWindow::onTemperatureRateChanged);

    data_telemetry_summary_card_ = new QWidget(config_group_);
    data_telemetry_summary_card_->setObjectName(QStringLiteral("homeTelemetrySummaryContainer"));
    data_telemetry_summary_card_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    data_telemetry_summary_card_->setToolTip(QString());
    auto *telemetrySummaryLayout = new QVBoxLayout(data_telemetry_summary_card_);
    telemetrySummaryLayout->setContentsMargins(0, 0, 0, 0);
    telemetrySummaryLayout->setSpacing(2);

    auto createTelemetrySection = [this, telemetrySummaryLayout](QVBoxLayout *&sectionContentLayout) {
        auto *section = new QFrame(data_telemetry_summary_card_);
        section->setObjectName(QStringLiteral("homeTelemetrySectionCard"));
        section->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        section->setToolTip(QString());
        sectionContentLayout = new QVBoxLayout(section);
        sectionContentLayout->setContentsMargins(6, 2, 6, 2);
        sectionContentLayout->setSpacing(2);
        telemetrySummaryLayout->addWidget(section, 0);
    };
    createTelemetrySection(data_telemetry_summary_layout_);
    createTelemetrySection(data_telemetry_link_summary_layout_);
    createTelemetrySection(data_telemetry_device_summary_layout_);
    data_telemetry_summary_card_->setVisible(true);

    auto *homeBodyWidget = new QWidget(config_group_);
    homeBodyWidget->setObjectName(QStringLiteral("homeOverviewDeviceBody"));
    auto *homeBodyLayout = new QVBoxLayout(homeBodyWidget);
    homeBodyLayout->setContentsMargins(kHomeOverviewBodyPadding,
                                       kHomeOverviewBodyPadding,
                                       kHomeOverviewBodyPadding,
                                       kConfigHomeBodyBottomPadding);
    homeBodyLayout->setSpacing(2);

    auto *homeDevicesWidget = new QWidget(homeBodyWidget);
    homeDevicesWidget->setObjectName(QStringLiteral("homeOverviewDeviceGrid"));
    homeDevicesWidget->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    auto *homeDevicesLayout = new QGridLayout(homeDevicesWidget);
    homeDevicesLayout->setContentsMargins(0, 0, 0, 0);
    homeDevicesLayout->setHorizontalSpacing(kHomeDeviceItemGap);
    homeDevicesLayout->setVerticalSpacing(kHomeDeviceGridRowGap);
    auto createHomeDeviceCapsule = [homeDevicesWidget]() {
        auto *label = new QLabel(homeDevicesWidget);
        label->setObjectName(QStringLiteral("homeDeviceStatusCapsule"));
        label->setAlignment(Qt::AlignCenter);
        label->setTextFormat(Qt::PlainText);
        label->setMinimumHeight(kHomeDeviceCapsuleHeight);
        label->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
        label->setWordWrap(false);
        return label;
    };

    auto createHomeDeviceActionButton = [this, homeDevicesWidget](VaporView::SkyDeviceId device) {
        auto *button = new QToolButton(homeDevicesWidget);
        button->setObjectName(QStringLiteral("homeDeviceActionButton"));
        button->setToolButtonStyle(Qt::ToolButtonIconOnly);
        button->setIconSize(QSize(kHomeDeviceIconSize, kHomeDeviceIconSize));
        button->setFixedSize(kHomeDeviceButtonSize, kHomeDeviceButtonSize);
        button->setFocusPolicy(Qt::StrongFocus);
        connect(button, &QToolButton::clicked, this, [this, device]() {
            triggerHomeDeviceAction(device);
        });
        return button;
    };

    int homeDeviceIndex = 0;
    auto addHomeDevice = [&](QLabel *&label, QToolButton *&button, VaporView::SkyDeviceId device) {
        auto *item = new QWidget(homeDevicesWidget);
        item->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        auto *itemLayout = new QHBoxLayout(item);
        itemLayout->setContentsMargins(0, 0, 0, 0);
        itemLayout->setSpacing(4);
        label = createHomeDeviceCapsule();
        button = createHomeDeviceActionButton(device);
        itemLayout->addWidget(label, 0, Qt::AlignVCenter);
        itemLayout->addWidget(button, 0, Qt::AlignVCenter);
        const int row = homeDeviceIndex / kHomeDeviceGridColumns;
        const int column = homeDeviceIndex % kHomeDeviceGridColumns;
        homeDevicesLayout->addWidget(item, row, column, Qt::AlignLeft | Qt::AlignVCenter);
        ++homeDeviceIndex;
    };

    addHomeDevice(home_epsilon_status_lbl_, home_epsilon_action_btn_, VaporView::SkyDeviceId::Epsilon);
    addHomeDevice(home_ptb_status_lbl_, home_ptb_action_btn_, VaporView::SkyDeviceId::Ptb);
    addHomeDevice(home_hmp_status_lbl_, home_hmp_action_btn_, VaporView::SkyDeviceId::Hmp);
    addHomeDevice(home_lidar_status_lbl_, home_lidar_action_btn_, VaporView::SkyDeviceId::Lidar);
    addHomeDevice(home_temperature_status_lbl_, home_temperature_action_btn_, VaporView::SkyDeviceId::TemperatureController);
    addHomeDevice(home_wave_status_lbl_, home_wave_action_btn_, VaporView::SkyDeviceId::WaveTcp);
    home_device_action_spinner_timer_ = new QTimer(this);
    home_device_action_spinner_timer_->setTimerType(Qt::PreciseTimer);
    home_device_action_spinner_timer_->setInterval(kHomeDeviceActionSpinnerIntervalMs);
    connect(home_device_action_spinner_timer_, &QTimer::timeout, this, [this]() {
        home_device_action_spinner_step_ = (home_device_action_spinner_step_ + 1) % kHomeDeviceActionSpinnerFrames;
        updateHomeDeviceActionSpinnerIcons();
    });
    homeDevicesLayout->setColumnStretch(kHomeDeviceGridColumns, 1);
    updateHomeDeviceStatusCapsules();
    homeDevicesLayout->activate();
    const QMargins homeBodyMargins = homeBodyLayout->contentsMargins();
    const int deviceOverviewDefaultWidth = homeDevicesWidget->sizeHint().width() +
                                           homeBodyMargins.left() +
                                           homeBodyMargins.right() + 2;
    config_group_->setMinimumWidth(std::max(kHomeOverviewDeviceMinWidth, deviceOverviewDefaultWidth));

    homeBodyLayout->addWidget(homeDevicesWidget, 0, Qt::AlignTop | Qt::AlignLeft);
    homeBodyLayout->addWidget(data_telemetry_summary_card_, 0, Qt::AlignTop);
    config_root_layout->addWidget(homeBodyWidget, 0, Qt::AlignTop);
    config_form_widget->setVisible(false);
    updateHomeDeviceStatusCapsules();
    updateRemoteTelemetrySummaryLabel();
}

void MainWindow::setupDataPanels()
{
    data_group_ = new QGroupBox(this);
    data_group_->setObjectName("sensorRowContainer");
    data_group_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto *data_layout = new QVBoxLayout(data_group_);
    data_layout->setSpacing(0);
    data_layout->setContentsMargins(0, 0, 0, 0);

    sensor_row_widget_ = new QWidget(data_group_);
    sensor_layout_ = new QHBoxLayout(sensor_row_widget_);
    sensor_layout_->setContentsMargins(0, 0, 0, 0);
    sensor_layout_->setSpacing(2);

    epsilon_group_ = new QGroupBox(this);
    epsilon_group_->setObjectName("sensorGroupBox");
    epsilon_group_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto *epsilon_layout = new QVBoxLayout(epsilon_group_);
    epsilon_layout->setContentsMargins(1, 0, 1, 1);
    epsilon_layout->setSpacing(0);

    auto *epsilonTitleBar = new QWidget(epsilon_group_);
    epsilonTitleBar->setObjectName("sectionTitleBar");
    epsilonTitleBar->setFixedHeight(kMainPageTitleBarHeight);
    auto *epsilonTitleLayout = new QHBoxLayout(epsilonTitleBar);
    epsilonTitleLayout->setContentsMargins(8, 2, 8, 2);
    epsilonTitleLayout->setSpacing(8);

    QWidget *epsilonTitleCluster = nullptr;
    epsilon_inline_title_lbl_ = createSectionTitleCluster(epsilonTitleBar,
                                                          QStringLiteral("earth"),
                                                          kMainPageButtonHeight,
                                                          &epsilonTitleCluster);
    epsilonTitleLayout->addWidget(epsilonTitleCluster, 0, Qt::AlignVCenter | Qt::AlignLeft);

    auto *epsilonRateTitleLabel = new VaporView::VisualTextLabel(epsilonTitleBar);
    epsilonRateTitleLabel->setObjectName(QStringLiteral("rateLabel"));
    epsilonRateTitleLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    epsilonRateTitleLabel->setMargin(0);
    epsilonRateTitleLabel->setContentsMargins(0, 0, 0, 0);
    epsilonRateTitleLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    epsilonRateTitleLabel->setWordWrap(false);
    epsilonRateTitleLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    epsilonRateTitleLabel->setFixedHeight(kMainPageButtonHeight);
    epsilonTitleLayout->addWidget(epsilonRateTitleLabel, 1, Qt::AlignVCenter | Qt::AlignLeft);

    epsilon_layout->addWidget(epsilonTitleBar);
    epsilon_panel_ = new EpsilonPanel(epsilonRateTitleLabel, this);
    epsilon_layout->addWidget(epsilon_panel_);
    sensor_layout_->addWidget(epsilon_group_, kSensorNavigationStretch);

    gnss_group_ = nullptr;
    imu_group_ = nullptr;
    gnss_panel_ = nullptr;
    imu_panel_ = nullptr;
    gnss_inline_title_lbl_ = nullptr;
    imu_inline_title_lbl_ = nullptr;

    auto *env_group = new QGroupBox(this);
    env_group->setObjectName("sensorGroupBox");
    env_group->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto *env_layout = new QVBoxLayout(env_group);
    env_layout->setContentsMargins(1, 0, 1, 1);
    env_layout->setSpacing(0);

    auto *envTitleBar = new QWidget(env_group);
    envTitleBar->setObjectName("environmentSectionTitleBar");
    envTitleBar->setFixedHeight(kEnvironmentTitleBarHeight);
    auto *envTitleLayout = new QHBoxLayout(envTitleBar);
    envTitleLayout->setContentsMargins(8, 0, 8, 0);
    envTitleLayout->setSpacing(8);

    QWidget *envTitleCluster = nullptr;
    env_inline_title_lbl_ = createSectionTitleCluster(envTitleBar,
                                                      QStringLiteral("mountain-snow"),
                                                      kMainPageButtonHeight,
                                                      &envTitleCluster);
    envTitleLayout->addWidget(envTitleCluster, 0, Qt::AlignVCenter | Qt::AlignLeft);
    envTitleLayout->addStretch(1);

    auto createStatusIcon = [envTitleBar]() {
        auto *label = new QLabel(envTitleBar);
        label->setObjectName(QStringLiteral("envStatusIcon"));
        label->setAlignment(Qt::AlignCenter);
        label->setFixedSize(kMainPageButtonHeight, kMainPageButtonHeight);
        label->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        return label;
    };
    env_lidar_status_icon_ = createStatusIcon();
    env_ptb_status_icon_ = createStatusIcon();
    env_hmp_status_icon_ = createStatusIcon();
    envTitleLayout->addWidget(env_lidar_status_icon_, 0, Qt::AlignVCenter);
    envTitleLayout->addWidget(env_ptb_status_icon_, 0, Qt::AlignVCenter);
    envTitleLayout->addWidget(env_hmp_status_icon_, 0, Qt::AlignVCenter);
    env_layout->addWidget(envTitleBar);

    lidar_panel_ = new LidarPanel(this);
    env_layout->addWidget(lidar_panel_);

    ptb_panel_ = new PtbPanel(this);
    env_layout->addWidget(ptb_panel_);

    hmp_panel_ = new HmpPanel(this);
    env_layout->addWidget(hmp_panel_);
    updateEnvironmentStatusIcons(false, false, false);

    const int sensorCardHeight = std::max({
        epsilon_group_->sizeHint().height(),
        epsilon_group_->minimumSizeHint().height()
    });
    epsilon_group_->setFixedHeight(sensorCardHeight);
    env_group->setFixedHeight(sensorCardHeight);
    sensor_row_widget_->setMinimumHeight(sensorCardHeight);

    sensor_layout_->addWidget(env_group, kSensorEnvironmentStretch);

    data_layout->addWidget(sensor_row_widget_, 0);
    data_layout->addStretch(1);
    const int dataCardMinHeight = data_group_->minimumSizeHint().height();
    data_group_->setMinimumHeight(dataCardMinHeight);
    data_group_->setFixedHeight(dataCardMinHeight);
    env_group_ = env_group;

    lidar_group_ = nullptr;
    ptb_group_ = nullptr;
    hmp_group_ = nullptr;

    temperature_overview_group_ = new QGroupBox(this);
    temperature_overview_group_->setObjectName("sensorGroupBox");
    temperature_overview_group_->setMinimumWidth(kHomeOverviewTemperatureMinWidth);
    temperature_overview_group_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    auto *temperatureOverviewLayout = new QVBoxLayout(temperature_overview_group_);
    temperatureOverviewLayout->setContentsMargins(kHomeOverviewCardOuterPadding,
                                                 0,
                                                 kHomeOverviewCardOuterPadding,
                                                 kHomeOverviewCardOuterPadding);
    temperatureOverviewLayout->setSpacing(0);

    auto *temperatureOverviewTitleBar = new QWidget(temperature_overview_group_);
    temperatureOverviewTitleBar->setObjectName("sectionTitleBar");
    temperatureOverviewTitleBar->setFixedHeight(kMainPageTitleBarHeight);
    auto *temperatureOverviewTitleLayout = new QHBoxLayout(temperatureOverviewTitleBar);
    temperatureOverviewTitleLayout->setContentsMargins(8, 2, 8, 2);
    temperatureOverviewTitleLayout->setSpacing(8);
    QWidget *temperatureOverviewTitleCluster = nullptr;
    temperature_overview_inline_title_lbl_ = createSectionTitleCluster(temperatureOverviewTitleBar,
                                                                       QStringLiteral("thermometer"),
                                                                       kMainPageButtonHeight,
                                                                       &temperatureOverviewTitleCluster);
    temperature_overview_inline_title_lbl_->setText(is_english_
        ? QStringLiteral("Laser Driver Temperature Overview")
        : QStringLiteral("激光驱动温控概览"));
    temperatureOverviewTitleLayout->addWidget(temperatureOverviewTitleCluster, 0, Qt::AlignVCenter | Qt::AlignLeft);
    temperatureOverviewTitleLayout->addStretch(1);
    temperatureOverviewLayout->addWidget(temperatureOverviewTitleBar);

    temperature_overview_panel_ = new TemperatureControllerOverviewPanel(temperature_overview_group_);
    temperature_overview_panel_->setOutputEnabledCallback([this](quint8 channel, bool enabled) {
        if (enabled)
        {
            const QMessageBox::StandardButton answer = QMessageBox::question(
                this,
                is_english_ ? QStringLiteral("Enable Temperature Output") : QStringLiteral("开启温控输出"),
                is_english_
                    ? QStringLiteral("Enable RD105 output for channel %1? Confirm the target temperature is safe.").arg(channel)
                    : QStringLiteral("确定开启 RD105 通道%1输出？请确认目标温度安全。").arg(channel));
            if (answer != QMessageBox::Yes)
            {
                if (temperature_overview_panel_)
                {
                    temperature_overview_panel_->updateData(current_temperature_controller_);
                }
                return;
            }
        }
        VaporView::TemperatureControllerCommand command;
        command.channel = channel;
        command.output_enabled = enabled;
        sendTemperatureCommand(VaporView::CommandId::SetTemperatureOutputEnabled, command);
    });
    temperatureOverviewLayout->addWidget(temperature_overview_panel_, 1);

    home_overview_splitter_ = new QSplitter(Qt::Horizontal, this);
    home_overview_splitter_->setObjectName(QStringLiteral("homeOverviewSplitter"));
    home_overview_splitter_->setAttribute(Qt::WA_StyledBackground, true);
    home_overview_splitter_->setAutoFillBackground(true);
    home_overview_splitter_->setChildrenCollapsible(false);
    home_overview_splitter_->setCollapsible(0, false);
    home_overview_splitter_->setCollapsible(1, false);
    home_overview_splitter_->setHandleWidth(kHomeOverviewSplitterHandleWidth);
    home_overview_splitter_->setOpaqueResize(true);
    home_overview_splitter_->setMinimumWidth(0);
    home_overview_splitter_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    home_overview_splitter_->addWidget(config_group_);
    home_overview_splitter_->addWidget(temperature_overview_group_);
    home_overview_splitter_->setStretchFactor(0, 0);
    home_overview_splitter_->setStretchFactor(1, 1);
    home_overview_splitter_->setSizes({config_group_->minimumWidth(), kHomeOverviewTemperatureMinWidth});
    main_layout_->addWidget(home_overview_splitter_, 0);
    main_layout_->addWidget(new MainCardResizeHandle(home_overview_splitter_, kConfigCardMinHeight, this), 0);
    main_layout_->addWidget(data_group_, 0);
    updateConfigCardHeightForSourceMode();

    main_layout_->addWidget(new MainCardResizeHandle(data_group_, dataCardMinHeight, this), 0);

    temperature_controller_group_ = new QGroupBox(this);
    temperature_controller_group_->setObjectName("sensorGroupBox");
    temperature_controller_group_->setMinimumWidth(0);
    temperature_controller_group_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto *temperatureLayout = new QVBoxLayout(temperature_controller_group_);
    temperatureLayout->setContentsMargins(1, 0, 1, 1);
    temperatureLayout->setSpacing(0);

    auto *temperatureTitleBar = new QWidget(temperature_controller_group_);
    temperatureTitleBar->setObjectName("sectionTitleBar");
    temperatureTitleBar->setFixedHeight(kMainPageTitleBarHeight);
    auto *temperatureTitleLayout = new QHBoxLayout(temperatureTitleBar);
    temperatureTitleLayout->setContentsMargins(8, 2, 8, 2);
    temperatureTitleLayout->setSpacing(8);
    QWidget *temperatureTitleCluster = nullptr;
    temperature_controller_inline_title_lbl_ = createSectionTitleCluster(temperatureTitleBar,
                                                                         QStringLiteral("thermometer"),
                                                                         kMainPageButtonHeight,
                                                                         &temperatureTitleCluster);
    temperatureTitleCluster->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    temperature_controller_inline_title_lbl_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    temperatureTitleLayout->addWidget(temperatureTitleCluster, 1, Qt::AlignVCenter | Qt::AlignLeft);
    temperature_remote_buttons_widget_ = new QWidget(temperatureTitleBar);
    temperature_remote_buttons_widget_->setObjectName(QStringLiteral("temperatureTitleButtons"));
    auto *temperatureRemoteLayout = new QHBoxLayout(temperature_remote_buttons_widget_);
    temperatureRemoteLayout->setContentsMargins(0, 0, 0, 0);
    temperatureRemoteLayout->setSpacing(4);
    temperature_remote_connect_btn_ = createRemoteDeviceButton(QStringLiteral("连接"), VaporView::CommandId::ConnectDevice, VaporView::SkyDeviceId::TemperatureController);
    temperature_remote_disconnect_btn_ = createRemoteDeviceButton(QStringLiteral("断开"), VaporView::CommandId::DisconnectDevice, VaporView::SkyDeviceId::TemperatureController);
    temperature_remote_reconnect_btn_ = createRemoteDeviceButton(QStringLiteral("重连"), VaporView::CommandId::ReconnectDevice, VaporView::SkyDeviceId::TemperatureController);
    temperature_remote_connect_btn_->setObjectName(QStringLiteral("temperatureTitleConnectButton"));
    temperature_remote_disconnect_btn_->setObjectName(QStringLiteral("temperatureTitleDisconnectButton"));
    temperature_remote_reconnect_btn_->setObjectName(QStringLiteral("temperatureTitleReconnectButton"));
    for (QPushButton *button : {temperature_remote_connect_btn_,
                                temperature_remote_disconnect_btn_,
                                temperature_remote_reconnect_btn_})
    {
        QObject::disconnect(button, nullptr, this, nullptr);
    }
    connect(temperature_remote_connect_btn_, &QPushButton::clicked, this, [this]() {
        handleTemperatureTitleButton(VaporView::CommandId::ConnectDevice);
    });
    connect(temperature_remote_disconnect_btn_, &QPushButton::clicked, this, [this]() {
        handleTemperatureTitleButton(VaporView::CommandId::DisconnectDevice);
    });
    connect(temperature_remote_reconnect_btn_, &QPushButton::clicked, this, [this]() {
        handleTemperatureTitleButton(VaporView::CommandId::ReconnectDevice);
    });
    temperatureRemoteLayout->addWidget(temperature_remote_connect_btn_);
    temperatureRemoteLayout->addWidget(temperature_remote_disconnect_btn_);
    temperatureRemoteLayout->addWidget(temperature_remote_reconnect_btn_);
    temperatureTitleLayout->addWidget(temperature_remote_buttons_widget_, 0, Qt::AlignVCenter | Qt::AlignRight);
    temperatureLayout->addWidget(temperatureTitleBar);

    temperature_controller_panel_ = new TemperatureControllerPanel(this);
    connect(temperature_controller_panel_, &TemperatureControllerPanel::targetTemperatureRequested, this, [this](quint8 channel, double celsius) {
        VaporView::TemperatureControllerCommand command;
        command.channel = channel;
        command.target_temperature_c = celsius;
        sendTemperatureCommand(VaporView::CommandId::SetTemperatureTarget, command);
    });
    connect(temperature_controller_panel_, &TemperatureControllerPanel::outputEnabledRequested, this, [this](quint8 channel, bool enabled) {
        if (enabled)
        {
            const QMessageBox::StandardButton answer = QMessageBox::question(
                this,
                is_english_ ? QStringLiteral("Enable Temperature Output") : QStringLiteral("开启温控输出"),
                is_english_
                    ? QStringLiteral("Enable RD105 output for channel %1? Confirm the target temperature and output limit are safe.").arg(channel)
                    : QStringLiteral("确定开启 RD105 通道%1输出？请确认目标温度和最大输出上限安全。" ).arg(channel));
            if (answer != QMessageBox::Yes)
            {
                return;
            }
        }
        VaporView::TemperatureControllerCommand command;
        command.channel = channel;
        command.output_enabled = enabled;
        sendTemperatureCommand(VaporView::CommandId::SetTemperatureOutputEnabled, command);
    });
    connect(temperature_controller_panel_, &TemperatureControllerPanel::outputModeRequested, this, [this](quint8 channel, quint16 mode) {
        VaporView::TemperatureControllerCommand command;
        command.channel = channel;
        command.output_mode = mode;
        sendTemperatureCommand(VaporView::CommandId::SetTemperatureOutputMode, command);
    });
    connect(temperature_controller_panel_, &TemperatureControllerPanel::maxOutputPercentRequested, this, [this](quint8 channel, quint16 percent) {
        VaporView::TemperatureControllerCommand command;
        command.channel = channel;
        command.max_output_percent = percent;
        sendTemperatureCommand(VaporView::CommandId::SetTemperatureMaxOutputPercent, command);
    });
    connect(temperature_controller_panel_, &TemperatureControllerPanel::pidRequested, this, [this](quint8 channel, quint32 kp, quint32 ki, quint32 kd) {
        VaporView::TemperatureControllerCommand command;
        command.channel = channel;
        command.kp = kp;
        command.ki = ki;
        command.kd = kd;
        sendTemperatureCommand(VaporView::CommandId::SetTemperaturePid, command);
    });
    connect(temperature_controller_panel_, &TemperatureControllerPanel::autoPidRequested, this, [this](quint8 channel, quint16 mode) {
        VaporView::TemperatureControllerCommand command;
        command.channel = channel;
        command.auto_pid_mode = mode;
        sendTemperatureCommand(VaporView::CommandId::SetTemperatureAutoPid, command);
    });
    connect(temperature_controller_panel_, &TemperatureControllerPanel::controllerModeRequested, this, [this](quint16 mode) {
        VaporView::TemperatureControllerCommand command;
        command.channel = 1;
        command.controller_mode = mode;
        sendTemperatureCommand(VaporView::CommandId::SetTemperatureControllerMode, command);
    });
    connect(temperature_controller_panel_, &TemperatureControllerPanel::deviceAddressRequested, this, [this](quint16 address) {
        VaporView::TemperatureControllerCommand command;
        command.channel = 1;
        command.device_address = address;
        sendTemperatureCommand(VaporView::CommandId::SetTemperatureDeviceAddress, command);
    });
    connect(temperature_controller_panel_, &TemperatureControllerPanel::rs485BaudRequested, this, [this](quint16 baudIndex) {
        VaporView::TemperatureControllerCommand command;
        command.channel = 1;
        command.rs485_baud_index = baudIndex;
        sendTemperatureCommand(VaporView::CommandId::SetTemperatureRs485Baud, command);
    });
    connect(temperature_controller_panel_, &TemperatureControllerPanel::overtempOutputModeRequested, this, [this](quint16 mode) {
        VaporView::TemperatureControllerCommand command;
        command.channel = 1;
        command.overtemp_output_mode = mode;
        sendTemperatureCommand(VaporView::CommandId::SetTemperatureOvertempOutputMode, command);
    });
    connect(temperature_controller_panel_, &TemperatureControllerPanel::factoryResetRequested, this, [this]() {
        const QMessageBox::StandardButton answer = QMessageBox::question(
            this,
            is_english_ ? QStringLiteral("Restore Factory Settings") : QStringLiteral("恢复出厂设置"),
            is_english_
                ? QStringLiteral("Restore RD105 factory settings? This resets the address, baud rate, and temperature parameters.")
                : QStringLiteral("确定恢复 RD105 出厂设置？这会重置站号、波特率和温控参数。"));
        if (answer != QMessageBox::Yes)
        {
            return;
        }
        VaporView::TemperatureControllerCommand command;
        command.channel = 1;
        sendTemperatureCommand(VaporView::CommandId::RestoreTemperatureFactoryDefaults, command);
    });
    temperatureLayout->addWidget(temperature_controller_panel_);

    tcp_wave_group_ = new QGroupBox(this);
    tcp_wave_group_->setObjectName("sensorGroupBox");
    tcp_wave_group_->setMinimumHeight(kTcpWaveCardMinHeight);
    tcp_wave_group_->setFixedHeight(kTcpWaveCardMinHeight);
    tcp_wave_group_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto *tcpWaveLayout = new QVBoxLayout(tcp_wave_group_);
    tcpWaveLayout->setContentsMargins(0, 0, 0, 0);
    tcpWaveLayout->setSpacing(0);

    tcp_wave_panel_ = new TcpWavePanel(this);
    tcp_wave_panel_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    connect(tcp_wave_panel_, &TcpWavePanel::rawWaveFrameReady,
            this, &MainWindow::onTcpRawWaveFrameReady);
    connect(tcp_wave_panel_, &TcpWavePanel::logMessageRequested,
            this, &MainWindow::log);
    connect(tcp_wave_panel_, &TcpWavePanel::connectionStateChanged, this, [this](bool) {
        updateRecordingActionStates();
        updateHomeDeviceStatusCapsules();
    });
    connect(tcp_wave_panel_, &TcpWavePanel::remoteWaveTcpConnectionRequested, this, [this](bool connectRequested) {
        requestRemoteWaveTcpConnection(connectRequested);
    });
    connect(tcp_wave_panel_, &TcpWavePanel::remotePeakSearchRangeRequested,
            this, &MainWindow::sendRemotePeakSearchRange);
    connect(tcp_wave_panel_, &TcpWavePanel::preferredPanelHeightChanged,
            this, &MainWindow::updateResponsiveHomeLayout);
    tcpWaveLayout->addWidget(tcp_wave_panel_);
    main_layout_->addWidget(tcp_wave_group_, 0);
    main_layout_->addStretch(1);
}

void MainWindow::setupLogPanel()
{
    log_side_panel_ = new ShrinkablePanel(this);
    log_side_panel_->setObjectName(QStringLiteral("logSidePanel"));
    log_side_panel_->setAttribute(Qt::WA_StyledBackground, true);
    log_side_panel_->setAutoFillBackground(true);
    log_side_panel_->setMouseTracking(true);
    log_side_panel_->installEventFilter(this);
    log_side_panel_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    auto *logSideLayout = new QVBoxLayout(log_side_panel_);
    logSideLayout->setContentsMargins(0, 0, 0, 0);
    logSideLayout->setSpacing(8);

    recording_status_card_ = new QFrame(log_side_panel_);
    recording_status_card_->setObjectName(QStringLiteral("recordingStatusCard"));
    recording_status_card_->setFrameShape(QFrame::NoFrame);
    recording_status_card_->setAttribute(Qt::WA_StyledBackground, true);
    recording_status_card_->setAutoFillBackground(true);
    recording_status_card_->setMinimumWidth(0);
    recording_status_card_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Minimum);
    auto *recordingCardLayout = new QVBoxLayout(recording_status_card_);
    recordingCardLayout->setContentsMargins(1, 1, 1, 1);
    recordingCardLayout->setSpacing(0);

    auto *recordingTitleBar = new QWidget(recording_status_card_);
    recordingTitleBar->setObjectName("sectionTitleBar");
    recordingTitleBar->setFixedHeight(kMainPageTitleBarHeight);
    auto *recordingTitleLayout = new QHBoxLayout(recordingTitleBar);
    recordingTitleLayout->setContentsMargins(8, 2, 8, 2);
    recordingTitleLayout->setSpacing(8);

    QWidget *recordingTitleCluster = nullptr;
    recording_status_title_lbl_ = createSectionTitleCluster(recordingTitleBar,
                                                            QStringLiteral("pencil"),
                                                            kMainPageButtonHeight,
                                                            &recordingTitleCluster);
    recordingTitleLayout->addWidget(recordingTitleCluster, 0, Qt::AlignVCenter | Qt::AlignLeft);
    recordingTitleLayout->addStretch(1);
    recordingCardLayout->addWidget(recordingTitleBar);

    auto *recordingBody = new QWidget(recording_status_card_);
    recordingBody->setObjectName(QStringLiteral("recordingStatusBody"));
    recordingBody->setAttribute(Qt::WA_StyledBackground, true);
    recordingBody->setAutoFillBackground(true);
    auto *recordingStatusLayout = new QHBoxLayout(recordingBody);
    recordingStatusLayout->setContentsMargins(10, 8, 10, 8);
    recordingStatusLayout->setSpacing(0);

    recording_status_label_ = new QLabel(recordingBody);
    recording_status_label_->setObjectName(QStringLiteral("recordingStatusLabel"));
    recording_status_label_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    recording_status_label_->setWordWrap(true);
    recording_status_label_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    recording_status_label_->setMinimumWidth(0);
    recording_status_label_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Minimum);
    recordingStatusLayout->addWidget(recording_status_label_);
    recordingCardLayout->addWidget(recordingBody);
    logSideLayout->addWidget(recording_status_card_, 0);

    log_group_ = new QFrame(log_side_panel_);
    log_group_->setObjectName("logPanelFrame");
    log_group_->setFrameShape(QFrame::NoFrame);
    log_group_->setAttribute(Qt::WA_StyledBackground, true);
    log_group_->setAutoFillBackground(true);
    log_group_->setMinimumWidth(0);
    log_group_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
    auto *log_layout = new QVBoxLayout(log_group_);
    log_layout->setContentsMargins(1, 1, 1, 1);
    log_layout->setSpacing(0);

    auto *logTitleBar = new QWidget(log_group_);
    logTitleBar->setObjectName("sectionTitleBar");
    logTitleBar->setFixedHeight(kMainPageTitleBarHeight);
    auto *logTitleLayout = new QHBoxLayout(logTitleBar);
    logTitleLayout->setContentsMargins(8, 2, 8, 2);
    logTitleLayout->setSpacing(8);

    QWidget *logTitleCluster = nullptr;
    log_inline_title_lbl_ = createSectionTitleCluster(logTitleBar,
                                                      QStringLiteral("scroll-text"),
                                                      kMainPageButtonHeight,
                                                      &logTitleCluster);
    logTitleLayout->addWidget(logTitleCluster, 0, Qt::AlignVCenter | Qt::AlignLeft);
    logTitleLayout->addStretch(1);
    log_filter_btn_ = new QToolButton(logTitleBar);
    log_filter_btn_->setObjectName(QStringLiteral("titleBarButton"));
    log_filter_btn_->setToolButtonStyle(Qt::ToolButtonIconOnly);
    log_filter_btn_->setAutoRaise(false);
    log_filter_btn_->setFocusPolicy(Qt::NoFocus);
    log_filter_btn_->setProperty(kTitleBarHoverProperty, false);
    configureHoverParticipant(log_filter_btn_, kTitleBarHoverParticipantProperty, this);
    log_filter_btn_->setIcon(createLogFilterIcon());
    log_filter_btn_->setStyleSheet(QStringLiteral("QToolButton::menu-indicator { image: none; width: 0px; height: 0px; }"));
    log_filter_btn_->setPopupMode(QToolButton::DelayedPopup);
    if (log_filter_menu_)
    {
        connect(log_filter_btn_, &QToolButton::clicked, this, [this]() {
            if (!log_filter_btn_ || !log_filter_menu_)
            {
                return;
            }
            const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
            const qint64 lastHideMs = log_filter_btn_->property("logFilterMenuHideMs").toLongLong();
            if (log_filter_menu_->isVisible() || (lastHideMs > 0 && nowMs - lastHideMs < 250))
            {
                log_filter_menu_->hide();
                log_filter_btn_->setDown(false);
                return;
            }
            log_filter_btn_->setDown(true);
            updateLogFilterAction();
            log_filter_menu_->popupFrom(log_filter_btn_, SingleLevelPopupAnchor::Right);
        });
        connect(log_filter_menu_, &QMenu::aboutToHide, log_filter_btn_, [button = log_filter_btn_]() {
            button->setProperty("logFilterMenuHideMs", QDateTime::currentMSecsSinceEpoch());
            QTimer::singleShot(0, button, [button]() {
                button->setDown(false);
                button->setChecked(false);
                button->setProperty("titleBarHover", false);
                button->clearFocus();
                button->style()->unpolish(button);
                button->style()->polish(button);
                button->update();
            });
        });
    }
    log_filter_btn_->setFixedSize(kMainPageButtonHeight, kMainPageButtonHeight);
    log_filter_btn_->setIconSize(QSize(kMainPageButtonHeight - 12, kMainPageButtonHeight - 12));
    logTitleLayout->addWidget(log_filter_btn_, 0, Qt::AlignVCenter | Qt::AlignRight);
    log_clear_btn_ = createTitleBarActionButton(clear_log_action_, logTitleBar);
    log_clear_btn_->setFixedSize(kMainPageButtonHeight, kMainPageButtonHeight);
    log_clear_btn_->setIconSize(QSize(kMainPageButtonHeight - 12, kMainPageButtonHeight - 12));
    logTitleLayout->addWidget(log_clear_btn_, 0, Qt::AlignVCenter | Qt::AlignRight);
    log_layout->addWidget(logTitleBar);

    log_text_edit_ = new QTextEdit(log_group_);
    log_text_edit_->setObjectName(QStringLiteral("logTextEdit"));
    log_text_edit_->viewport()->setObjectName(QStringLiteral("logTextViewport"));
    log_text_edit_->setFrameShape(QFrame::NoFrame);
    log_text_edit_->setLineWidth(0);
    log_text_edit_->setAutoFillBackground(false);
    log_text_edit_->viewport()->setAutoFillBackground(false);
    log_text_edit_->setReadOnly(true);
    log_text_edit_->setMinimumWidth(0);
    log_text_edit_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
    log_layout->addWidget(log_text_edit_);
    logSideLayout->addWidget(log_group_, 1);
    log_side_panel_->setMinimumWidth(minimumLogSidePanelWidth());
}

void MainWindow::setEnglish(bool english)
{
    auto setNativeMenuTitle = [this](QMenu *menu, const QString& title) {
        if (!menu || custom_title_bar_)
        {
            return;
        }
        menu->setTitle(title);
    };

    is_english_ = english;
    if (qApp)
    {
        qApp->setProperty(kEnglishProperty, is_english_);
    }

    setNativeMenuTitle(data_menu_, english ? QStringLiteral("&Data") : QStringLiteral("数据(&D)"));
    recording_directory_action_->setText(english ? "Recording Folder..." : "记录目录...");
    setNativeMenuTitle(recording_rate_menu_, english ? QStringLiteral("Record Rates") : QStringLiteral("记录频率"));
    rebuildRecordingRateMenu();
    setNativeMenuTitle(devices_menu_, english ? QStringLiteral("&Devices") : QStringLiteral("设备(&E)"));
    if (epsilon_packet_rates_action_)
    {
        epsilon_packet_rates_action_->setText(english ? "EPSILON Packet Rates..." : "设置EPSILON包频率...");
    }
    if (epsilon_rtcm_port_action_)
    {
        epsilon_rtcm_port_action_->setText(english ? "Configure EPSILON RTCM Port..." : "配置EPSILON RTCM串口...");
    }
    if (epsilon_reconfigure_action_)
    {
        epsilon_reconfigure_action_->setText(english ? "Reconfigure EPSILON Output..." : "重新配置EPSILON输出...");
    }
    session_viewer_action_->setText(english ? "Data Viewer..." : "数据查看器...");
    setNativeMenuTitle(view_menu_, english ? QStringLiteral("&View") : QStringLiteral("视图(&V)"));
#ifdef VAPORVIEW_HAS_OSGEARTH
    if (map3d_action_)
    {
        map3d_action_->setText(english ? "3D Map..." : "三维地图...");
        map3d_action_->setToolTip(english ? "Open 3D map" : "打开三维地图");
        map3d_action_->setStatusTip(map3d_action_->toolTip());
    }
    if (map3d_diagnostics_action_)
    {
        map3d_diagnostics_action_->setText(english ? "Map Data Diagnostics..." : "地图数据诊断...");
        map3d_diagnostics_action_->setToolTip(english ? "Open 3D map data diagnostics" : "打开三维地图数据诊断");
        map3d_diagnostics_action_->setStatusTip(map3d_diagnostics_action_->toolTip());
    }
#endif
    exit_action_->setText(english ? "E&xit" : "退出(&X)");

    setNativeMenuTitle(font_menu_, english ? QStringLiteral("Font &Size") : QStringLiteral("字号(&S)"));
    font_tiny_action_->setText(english ? "Tiny (70%)" : "超小 (70%)");
    font_extra_small_action_->setText(english ? "Extra Small (80%)" : "特小 (80%)");
    font_small_action_->setText(english ? "Small (90%)" : "小号 (90%)");
    font_normal_action_->setText(english ? "Normal (100%)" : "标准 (100%)");
    font_large_action_->setText(english ? "Large (115%)" : "大号 (115%)");
    font_extra_large_action_->setText(english ? "Extra Large (130%)" : "超大 (130%)");

    setNativeMenuTitle(language_menu_, english ? QStringLiteral("&Language") : QStringLiteral("语言(&L)"));
    lang_action_->setText(english ? "Switch to Chinese" : "切换到英文");
    lang_action_->setToolTip(english ? "Switch to Chinese" : "切换到英文");
    lang_action_->setStatusTip(english ? "Switch interface language" : "切换界面语言");
    updateThemeAction();
    updateCustomTitleBarTexts();
    discardTitleApplicationMenuPanel();

    setNativeMenuTitle(help_menu_, english ? QStringLiteral("&Help") : QStringLiteral("帮助(&H)"));
    about_action_->setText(english ? "&About" : "关于(&A)");

    refresh_ports_btn_->setText(english ? "Refresh" : "刷新");
    refresh_ports_btn_->setToolTip(english ? "Refresh ports" : "刷新串口");
    refresh_ports_btn_->setStatusTip(refresh_ports_btn_->toolTip());
    connect_btn_->setText(english ? "Connect" : "连接");
    connect_btn_->setToolTip(english ? "Connect" : "连接");
    connect_btn_->setStatusTip(connect_btn_->toolTip());
    cancel_connect_btn_->setText(english ? "Cancel" : "取消");
    cancel_connect_btn_->setToolTip(english ? "Cancel connection" : "取消连接");
    cancel_connect_btn_->setStatusTip(cancel_connect_btn_->toolTip());
    disconnect_btn_->setText(english ? "Disconnect" : "断开");
    disconnect_btn_->setToolTip(english ? "Disconnect" : "断开连接");
    disconnect_btn_->setStatusTip(disconnect_btn_->toolTip());
    updateScheduledRecordingAction();
    start_recording_btn_->setText(english ? "Start Recording" : "开始记录");
    start_recording_btn_->setToolTip(english ? "Start recording" : "开始记录");
    start_recording_btn_->setStatusTip(start_recording_btn_->toolTip());
    pause_recording_btn_->setText(english ? "Pause Recording" : "暂停记录");
    pause_recording_btn_->setToolTip(english ? "Pause recording" : "暂停记录");
    pause_recording_btn_->setStatusTip(pause_recording_btn_->toolTip());
    stop_recording_btn_->setText(english ? "Stop Recording" : "结束记录");
    stop_recording_btn_->setToolTip(english ? "Stop recording" : "结束记录");
    stop_recording_btn_->setStatusTip(stop_recording_btn_->toolTip());
    clear_log_action_->setText(english ? "Clear Log" : "清空日志");
    clear_log_action_->setToolTip(english ? "Clear Log" : "清空日志");
    clear_log_action_->setStatusTip(english ? "Clear Log" : "清空日志");
    updateLogFilterAction();
    rtk_config_action_->setText(english ? "RTK Config" : "RTK配置");
    updateRtkConfigIcon();
    session_viewer_action_->setToolTip(english ? "Data viewer" : "数据查看器");
    session_viewer_action_->setStatusTip(session_viewer_action_->toolTip());

    status_label_->setText(english ? "Ready" : "就绪");

    config_group_->setTitle(QString());
    data_group_->setTitle(QString());
    tcp_wave_group_->setTitle(QString());

    if (epsilon_group_) epsilon_group_->setTitle(QString());
    if (gnss_group_) gnss_group_->setTitle(QString());
    if (imu_group_) imu_group_->setTitle(QString());
    if (env_group_) env_group_->setTitle(QString());

    if (epsilon_lbl_) epsilon_lbl_->setText(english ? "EPSILON:" : "EPSILON:");
    if (gnss_lbl_) gnss_lbl_->setText(english ? "GNSS:" : "GNSS:");
    if (imu_lbl_) imu_lbl_->setText(english ? "IMU:" : "IMU:");
    if (ptb_lbl_) ptb_lbl_->setText(english ? "PTB210:" : "PTB210:");
    if (hmp_lbl_) hmp_lbl_->setText(english ? "HMP3:" : "HMP3:");
    if (lidar_lbl_) lidar_lbl_->setText(english ? "TFA1500-L:" : "TFA1500-L:");
    if (temperature_lbl_) temperature_lbl_->setText(QStringLiteral("RD105:"));

    if (config_inline_title_lbl_)
    {
        config_inline_title_lbl_->setText(english ? "Device Overview" : "设备概览");
    }
    if (data_source_mode_lbl_) data_source_mode_lbl_->setText(english ? "Source:" : "数据源:");
    if (source_mode_switch_) source_mode_switch_->setEnglish(english);
    if (sky_telemetry_transport_lbl_) sky_telemetry_transport_lbl_->setText(english ? "Link:" : "链路:");
    updateSkyTelemetryTransportComboTexts(sky_telemetry_transport_combo_, english);
    if (sky_telemetry_tcp_host_lbl_) sky_telemetry_tcp_host_lbl_->setText(english ? "Sky IP:" : "天空端IP:");
    if (sky_telemetry_tcp_port_lbl_) sky_telemetry_tcp_port_lbl_->setText(english ? "Port:" : "端口:");
    if (sky_telemetry_port_lbl_) sky_telemetry_port_lbl_->setText(english ? "Serial:" : "串口:");
    if (sky_telemetry_baud_lbl_) sky_telemetry_baud_lbl_->setText(english ? "Baud:" : "波特率:");
    if (sky_device_config_btn_) sky_device_config_btn_->setText(english ? "Sky Device Config" : "天空端设备配置");
    if (data_source_mode_combo_)
    {
        const QSignalBlocker blocker(data_source_mode_combo_);
        data_source_mode_combo_->setItemText(0, sourceModeDisplayText(english, 0));
        data_source_mode_combo_->setItemText(1, sourceModeDisplayText(english, 1));
    }
    if (auto_detect_ports_btn_)
    {
        auto_detect_ports_btn_->setText(port_detection_in_progress_
            ? (english ? "Cancel Auto Detect" : "取消自动识别")
            : (english ? "Auto Detect Ports" : "自动识别串口"));
        auto_detect_ports_btn_->setToolTip(port_detection_in_progress_
            ? (english ? "Stop the current serial-port detection task." : "停止当前串口自动识别任务。")
            : (english ? "Probe available serial ports and automatically assign detected devices."
                       : "扫描可用串口，并将识别出的设备自动填入对应端口。"));
    }
    if (log_inline_title_lbl_)
    {
        log_inline_title_lbl_->setText(english ? "Log" : "日志");
    }
    updateAppSidebarButtonTexts();
    if (recording_status_title_lbl_)
    {
        recording_status_title_lbl_->setText(english ? "Recording Status" : "记录状态");
    }
    if (log_side_panel_)
    {
        log_side_panel_->setMinimumWidth(minimumLogSidePanelWidth());
    }
    if (epsilon_inline_title_lbl_)
    {
        epsilon_inline_title_lbl_->setText(english ? "EPSILON Integrated Navigation" : "EPSILON组合导航");
    }
    if (gnss_inline_title_lbl_)
    {
        gnss_inline_title_lbl_->setText(english ? "GNSS / RTK" : "GNSS / RTK");
    }
    if (imu_inline_title_lbl_)
    {
        imu_inline_title_lbl_->setText(english ? "IMU" : "IMU");
    }
    if (env_inline_title_lbl_)
    {
        env_inline_title_lbl_->setText(english ? "Environment / Range" : "环境与测距");
    }
    if (temperature_overview_inline_title_lbl_)
    {
        temperature_overview_inline_title_lbl_->setText(english ? "Laser Driver Temperature Overview" : "激光驱动温控概览");
    }
    if (temperature_overview_panel_)
    {
        temperature_overview_panel_->setEnglish(english);
    }
    if (temperature_controller_inline_title_lbl_)
    {
        updateTemperatureControllerTitleText();
    }
    updateTemperatureTitleButtonsState();
    if (global_rate_lbl_) global_rate_lbl_->setText(english ? "Global Rate:" : "统一频率:");
    if (epsilon_rate_lbl_) epsilon_rate_lbl_->setText(english ? "Packets:" : "包频率:");
    if (epsilon_packet_rates_btn_)
    {
        epsilon_packet_rates_btn_->setText(english ? "Packet Rates..." : "配置EPSILON包频率...");
        epsilon_packet_rates_btn_->setToolTip(english
            ? "Configure EPSILON packet output rates"
            : "配置 EPSILON 各数据包输出频率");
    }
    if (gnss_rate_lbl_) gnss_rate_lbl_->setText(english ? "Rate:" : "频率:");
    if (imu_rate_lbl_) imu_rate_lbl_->setText(english ? "Rate:" : "频率:");
    if (imu_apply_btn_)
    {
        imu_apply_btn_->setText(english ? "Apply IMU" : "应用IMU");
        imu_apply_btn_->setToolTip(english ? "Apply the selected IMU format, baud rate, and output frequency" : "应用当前选择的 IMU 输出格式、波特率和输出频率");
    }
    if (imu_hi91_btn_)
    {
        imu_hi91_btn_->setToolTip(english ? "Switch IMU output to HI91 immediately" : "立即切换 IMU 输出为 HI91");
    }
    if (imu_hi92_btn_)
    {
        imu_hi92_btn_->setToolTip(english ? "Switch IMU output to HI92 immediately" : "立即切换 IMU 输出为 HI92");
    }
    if (imu_baud_115200_btn_)
    {
        imu_baud_115200_btn_->setToolTip(english ? "Switch IMU baud rate to 115200" : "一键切换 IMU 波特率到 115200");
    }
    if (imu_baud_921600_btn_)
    {
        imu_baud_921600_btn_->setToolTip(english ? "Switch IMU baud rate to 921600" : "一键切换 IMU 波特率到 921600");
    }
    if (imu_rate_100_btn_)
    {
        imu_rate_100_btn_->setToolTip(english ? "Switch IMU output frequency to 100 Hz" : "一键切换 IMU 输出频率到 100 Hz");
    }
    if (imu_rate_200_btn_)
    {
        imu_rate_200_btn_->setToolTip(english ? "Switch IMU output frequency to 200 Hz" : "一键切换 IMU 输出频率到 200 Hz");
    }
    if (imu_rate_500_btn_)
    {
        imu_rate_500_btn_->setToolTip(english ? "Switch IMU output frequency to 500 Hz" : "一键切换 IMU 输出频率到 500 Hz");
    }
    if (imu_rate_1000_btn_)
    {
        imu_rate_1000_btn_->setToolTip(english ? "Switch IMU output frequency to 1000 Hz" : "一键切换 IMU 输出频率到 1000 Hz");
    }
    ptb_rate_lbl_->setText(english ? "Rate:" : "频率:");
    hmp_rate_lbl_->setText(english ? "Rate:" : "频率:");
    lidar_rate_lbl_->setText(english ? "Rate:" : "频率:");
    if (temperature_rate_lbl_) temperature_rate_lbl_->setText(english ? "Poll:" : "轮询:");
    for (QComboBox *combo : {ptb_rate_combo_, hmp_rate_combo_, lidar_rate_combo_, temperature_rate_combo_})
    {
        if (!combo)
        {
            continue;
        }
        const QSignalBlocker blocker(combo);
        const QString oldText = english ? QStringLiteral("不设定") : QStringLiteral("No Set");
        const QString newText = english ? QStringLiteral("No Set") : QStringLiteral("不设定");
        const int idx = combo->findText(oldText);
        if (idx >= 0)
        {
            combo->setItemText(idx, newText);
        }
        else if (combo->findText(newText) < 0)
        {
            combo->addItem(newText);
        }
        if (isRateUnspecified(combo->currentText()))
        {
            combo->setCurrentText(newText);
        }
    }

    if (epsilon_panel_) epsilon_panel_->setEnglish(english);
    if (gnss_panel_) gnss_panel_->setEnglish(english);
    if (imu_panel_) imu_panel_->setEnglish(english);
    if (ptb_panel_) ptb_panel_->setEnglish(english);
    if (hmp_panel_) hmp_panel_->setEnglish(english);
    if (lidar_panel_) lidar_panel_->setEnglish(english);
    if (tcp_wave_panel_) tcp_wave_panel_->setEnglish(english);
    if (sky_device_config_dialog_) sky_device_config_dialog_->setEnglish(english);

    const CollectorSnapshot collectors = snapshotCollectors();
    if (collectors.epsilon) collectors.epsilon->setEnglish(english);
    if (collectors.gnss) collectors.gnss->setEnglish(english);
    if (collectors.imu) collectors.imu->setEnglish(english);
    if (collectors.ptb) collectors.ptb->setEnglish(english);
    if (collectors.hmp) collectors.hmp->setEnglish(english);
    if (collectors.lidar) collectors.lidar->setEnglish(english);
    if (collectors.temperature_controller) collectors.temperature_controller->setEnglish(english);

    if (rtk_config_dialog_)
    {
        rtk_config_dialog_->setEnglish(english);
    }
    if (session_viewer_window_)
    {
        session_viewer_window_->setEnglish(english);
    }

    if (isRemoteSkyMode())
    {
        refreshRemoteSkyDataUi();
    }
    else
    {
        updateEnvironmentStatusIcons(current_lidar_.valid, current_ptb_.valid, current_hmp_.valid);
    }
    updateSourceModeUi();
    updateDeviceConfigTexts();
    updateSidebarNavIcons();
    updateRecordingStatusLabel();
}

void MainWindow::onOpenSessionViewerClicked()
{
    showBusyStatusTaskProgress(is_english_ ? "Opening Data Viewer..." : "正在打开数据查看器...");
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

    if (!session_viewer_window_)
    {
        session_viewer_window_ = new SessionViewerWindow();
        session_viewer_window_->setAttribute(Qt::WA_QuitOnClose, false);
        connect(session_viewer_window_, &QObject::destroyed, this, [this]() {
            session_viewer_window_ = nullptr;
        });
        session_viewer_window_->setEnglish(is_english_);
    }

    session_viewer_window_->setDefaultDataDirectory(
        recording_directory_.isEmpty() ? defaultRecordingDirectory() : recording_directory_);

    const bool wasMinimized =
        session_viewer_window_->isMinimized() ||
        session_viewer_window_->windowState().testFlag(Qt::WindowMinimized);
    const bool restoreMaximized =
        session_viewer_window_->isMaximized() ||
        session_viewer_window_->windowState().testFlag(Qt::WindowMaximized);
    if (!wasMinimized)
    {
        VaporView::centerWindowOnScreen(session_viewer_window_, this);
    }
    if (wasMinimized)
    {
        session_viewer_window_->setWindowState(
            session_viewer_window_->windowState() & ~Qt::WindowMinimized);
        if (restoreMaximized)
        {
            session_viewer_window_->showMaximized();
        }
        else
        {
            session_viewer_window_->showNormal();
        }
    }
    else
    {
        session_viewer_window_->show();
    }
    session_viewer_window_->raise();
    session_viewer_window_->activateWindow();
    hideStatusTaskProgress();
}

#ifdef VAPORVIEW_HAS_OSGEARTH
void MainWindow::onOpenMap3DWindowClicked()
{
    if (!map3d_window_)
    {
        map3d_window_ = new VaporView::Map3D::Map3DWindow(this);
        map3d_window_->setAttribute(Qt::WA_QuitOnClose, false);
        connect(map3d_window_, &QObject::destroyed, this, [this]() {
            map3d_window_ = nullptr;
            pending_map3d_samples_.clear();
            if (map3d_flush_timer_)
            {
                map3d_flush_timer_->stop();
            }
        });
    }

    map3d_window_->show();
    map3d_window_->raise();
    map3d_window_->activateWindow();
}

void MainWindow::onOpenMap3DDiagnosticsClicked()
{
    onOpenMap3DWindowClicked();
    if (map3d_window_)
    {
        map3d_window_->showMapDiagnostics();
    }
}
#else
void MainWindow::onOpenMap3DWindowClicked()
{
    QMessageBox::information(this,
                             QStringLiteral("VaporView 3D Map"),
                             is_english_
                                 ? QStringLiteral("3D map module is not enabled. Rebuild with -DVAPORVIEW_ENABLE_OSGEARTH=ON.")
                                 : QStringLiteral("三维地图模块未启用。请使用 -DVAPORVIEW_ENABLE_OSGEARTH=ON 重新构建。"));
}

void MainWindow::onOpenMap3DDiagnosticsClicked()
{
    onOpenMap3DWindowClicked();
}
#endif

void MainWindow::onSwitchLanguage()
{
    if (language_switch_in_progress_)
    {
        return;
    }

    language_switch_in_progress_ = true;
    QTimer::singleShot(0, this, [this]() {
        is_english_ = !is_english_;
        setEnglish(is_english_);
        log(is_english_ ? "Language switched to English" : "语言已切换为中文");
        language_switch_in_progress_ = false;
    });
}
void MainWindow::showAboutDialog()
{
    const QString title = is_english_ ? QStringLiteral("About VaporView") : QStringLiteral("关于 VaporView");
    const QString text = is_english_
        ? QStringLiteral(
              "VaporView Application\n\n"
              "Version 1.0.1\n\n"
              "Integrated navigation and environment monitoring system.\n\n"
              "Supported devices:\n"
              "- EPSILON Integrated Navigation (FDILink)\n"
              "- PTB210 Barometer\n"
              "- HMP3 Temperature/Humidity Sensor\n"
              "- TFA1500-L Laser Rangefinder")
        : QStringLiteral(
              "VaporView 应用程序\n\n"
              "版本 1.0.1\n\n"
              "组合导航与环境监控系统。\n\n"
              "支持的设备:\n"
              "- EPSILON 组合导航一体机 (FDILink)\n"
              "- PTB210 气压计\n"
              "- HMP3 温湿度传感器\n"
              "- TFA1500-L 激光测距模块");
    QMessageBox::about(this, title, text);
}

void MainWindow::updateThemeAction()
{
    if (!theme_toggle_action_)
    {
        return;
    }

    const bool targetLight = dark_theme_enabled_;
    theme_toggle_action_->setIcon(targetLight ? createLightThemeIcon() : createDarkThemeIcon());
    theme_toggle_action_->setText(targetLight
        ? (is_english_ ? "Light Theme" : "亮色模式")
        : (is_english_ ? "Dark Theme" : "暗色模式"));
    theme_toggle_action_->setToolTip(targetLight
        ? (is_english_ ? "Switch to light theme" : "切换到亮色模式")
        : (is_english_ ? "Switch to dark theme" : "切换到暗色模式"));
    theme_toggle_action_->setStatusTip(theme_toggle_action_->toolTip());
}

void MainWindow::updateThemedIcons()
{
    if (lang_action_)
    {
        lang_action_->setIcon(createLanguageIcon());
    }
    if (refresh_ports_btn_)
    {
        refresh_ports_btn_->setIcon(createRefreshIcon());
    }
    if (connect_btn_)
    {
        connect_btn_->setIcon(createConnectIcon());
    }
    if (cancel_connect_btn_)
    {
        cancel_connect_btn_->setIcon(createCancelIcon());
    }
    if (disconnect_btn_)
    {
        disconnect_btn_->setIcon(createDisconnectIcon());
    }
    if (scheduled_recording_action_)
    {
        scheduled_recording_action_->setIcon(createTimerIcon());
    }
    if (start_recording_btn_)
    {
        start_recording_btn_->setIcon(createPlayIcon());
    }
    if (pause_recording_btn_)
    {
        pause_recording_btn_->setIcon(createPauseIcon());
    }
    if (stop_recording_btn_)
    {
        stop_recording_btn_->setIcon(createStopIcon());
    }
    updateRtkConfigIcon();
    if (clear_log_action_)
    {
        clear_log_action_->setIcon(createClearLogIcon());
    }
    if (session_viewer_action_)
    {
        session_viewer_action_->setIcon(createWaveformViewerIcon());
    }
#ifdef VAPORVIEW_HAS_OSGEARTH
    if (map3d_action_)
    {
        map3d_action_->setIcon(QIcon(QStringLiteral("resources/lucide/earth.svg")));
    }
    if (map3d_diagnostics_action_)
    {
        map3d_diagnostics_action_->setIcon(createLucideIcon(QStringLiteral("activity"), toolbarColor(AppThemeColor::ToolbarBlue)));
    }
#endif
    if (temperature_overview_panel_)
    {
        temperature_overview_panel_->updateThemedIcons();
    }
    updateSidebarNavIcons();
    updateCustomLogoPixmap();
    if (log_filter_btn_)
    {
        log_filter_btn_->setIcon(createLogFilterIcon());
    }
    updateFontScaleMenuCheckIcons();
    updateLogSidePanelToggleButton();
    updateSectionTitleIcons(this, dark_theme_enabled_);
    updateLogFilterAction();
}

void MainWindow::updateRtkConfigIcon()
{
    const QString baseText = is_english_ ? QStringLiteral("RTK config") : QStringLiteral("RTK配置");
    const QString stateText = rtk_service_running_
        ? (is_english_ ? QStringLiteral("running") : QStringLiteral("运行中"))
        : (is_english_ ? QStringLiteral("stopped") : QStringLiteral("未启动"));
    const QString statusTip = QStringLiteral("%1 (%2)").arg(baseText, stateText);
    if (rtk_config_action_)
    {
        rtk_config_action_->setIcon(createRtkSatelliteIcon(rtk_service_running_));
        rtk_config_action_->setToolTip(statusTip);
        rtk_config_action_->setStatusTip(statusTip);
    }
    if (rtk_config_nav_btn_)
    {
        rtk_config_nav_btn_->setToolTip(statusTip);
        rtk_config_nav_btn_->setStatusTip(statusTip);
    }
}

void MainWindow::updateFontScaleMenuCheckIcons()
{
    const QIcon checkIcon = createMenuCheckIcon(dark_theme_enabled_);
    const auto applyIcon = [this, &checkIcon](QAction *action, int minPercent, int maxPercent) {
        if (!action)
        {
            return;
        }
        action->setIcon(font_scale_percent_ >= minPercent && font_scale_percent_ <= maxPercent ? checkIcon : QIcon());
    };

    applyIcon(font_tiny_action_, 70, 75);
    applyIcon(font_extra_small_action_, 76, 85);
    applyIcon(font_small_action_, 86, 95);
    applyIcon(font_normal_action_, 96, 107);
    applyIcon(font_large_action_, 108, 122);
    applyIcon(font_extra_large_action_, 123, 150);
}

QString MainWindow::currentMainPageTitleText() const
{
    const int pageIndex = main_page_stack_ ? main_page_stack_->currentIndex() : 0;
    if (app_nav_button_group_)
    {
        if (QAbstractButton *button = app_nav_button_group_->button(pageIndex))
        {
            const QString accessibleName = button->accessibleName().trimmed();
            if (!accessibleName.isEmpty())
            {
                return accessibleName;
            }
            const QString text = button->text().trimmed();
            if (!text.isEmpty())
            {
                return text;
            }
            const QString toolTip = button->toolTip().trimmed();
            if (!toolTip.isEmpty())
            {
                return toolTip;
            }
        }
    }

    switch (pageIndex)
    {
    case 1:
        return is_english_ ? QStringLiteral("Device") : QStringLiteral("设备配置");
    case 2:
        return is_english_ ? QStringLiteral("Thermal") : QStringLiteral("温控");
    case 0:
    default:
        return is_english_ ? QStringLiteral("Home") : QStringLiteral("首页");
    }
}

void MainWindow::updateCustomTitleBarTexts()
{
    if (custom_title_label_)
    {
        custom_title_label_->setText(currentMainPageTitleText());
    }
    if (title_menu_btn_)
    {
        title_menu_btn_->setToolTip(is_english_ ? "Menu" : "菜单");
        title_menu_btn_->setStatusTip(title_menu_btn_->toolTip());
    }
    updateCustomLogoTooltip();
    if (title_language_btn_)
    {
        title_language_btn_->setToolTip(is_english_ ? "Switch to Chinese" : "切换到英文");
        title_language_btn_->setStatusTip(is_english_ ? "Switch interface language" : "切换界面语言");
    }
    updateLogSidePanelToggleButton();
    if (window_minimize_btn_)
    {
        window_minimize_btn_->setToolTip(is_english_ ? "Minimize" : "最小化");
        window_minimize_btn_->setStatusTip(window_minimize_btn_->toolTip());
    }
    if (window_close_btn_)
    {
        window_close_btn_->setToolTip(is_english_ ? "Close" : "关闭");
        window_close_btn_->setStatusTip(window_close_btn_->toolTip());
    }
    updateWindowControlButtons();
}

void MainWindow::updateCustomTitleBarStyle()
{
    if (!custom_title_bar_)
    {
        return;
    }

    custom_title_bar_->setFixedHeight(scalePixels(48));
    const QSize actionButtonSize(scalePixels(34), scalePixels(34));
    const QSize windowButtonSize(scalePixels(34), scalePixels(34));
    const QSize iconSize(scalePixels(24), scalePixels(24));
    const QSize maximizeIconSize(scalePixels(21), scalePixels(21));

    const auto buttons = custom_title_bar_->findChildren<QToolButton *>();
    for (QToolButton *button : buttons)
    {
        if (!button)
        {
            continue;
        }
        const bool windowButton = button == window_minimize_btn_ ||
                                  button == window_maximize_btn_ ||
                                  button == window_close_btn_;
        button->setFixedSize(windowButton ? windowButtonSize : actionButtonSize);
        button->setIconSize(iconSize);
    }

    if (window_maximize_btn_)
    {
        window_maximize_btn_->setIconSize(maximizeIconSize);
    }
    if (log_clear_btn_)
    {
        log_clear_btn_->setFixedSize(actionButtonSize);
        log_clear_btn_->setIconSize(iconSize);
    }
    if (log_filter_btn_)
    {
        log_filter_btn_->setFixedSize(actionButtonSize);
        log_filter_btn_->setIconSize(iconSize);
    }
    updateCustomLogoPixmap();
    updateCustomLogoTooltip();
    const QIcon logoIcon = createVaporViewLogoIcon(dark_theme_enabled_);
    if (!logoIcon.isNull())
    {
        setWindowIcon(logoIcon);
        qApp->setWindowIcon(logoIcon);
    }

    if (title_menu_btn_)
    {
        title_menu_btn_->setIcon(createTitleBarIcon(QStringLiteral("menu"), dark_theme_enabled_));
    }
    if (title_language_btn_)
    {
        title_language_btn_->setIcon(createLanguageIcon());
    }
    updateLogSidePanelToggleButton();
    if (title_application_panel_)
    {
        title_application_panel_->hide();
        title_application_panel_->setStyleSheet(titleApplicationPanelStyleSheet(dark_theme_enabled_));
    }
    if (title_application_sub_panel_)
    {
        title_application_sub_panel_->hide();
        title_application_sub_panel_->setStyleSheet(titleApplicationPanelStyleSheet(dark_theme_enabled_));
    }
    if (title_application_nested_panel_)
    {
        title_application_nested_panel_->hide();
        title_application_nested_panel_->setStyleSheet(titleApplicationPanelStyleSheet(dark_theme_enabled_));
    }
    if (window_minimize_btn_)
    {
        window_minimize_btn_->setIcon(createTitleBarIcon(QStringLiteral("minus"), dark_theme_enabled_));
    }
    if (window_close_btn_)
    {
        window_close_btn_->setIcon(createTitleBarIcon(QStringLiteral("x"), dark_theme_enabled_));
    }
    updateWindowControlButtons();
}

void MainWindow::updateWindowControlButtons()
{
    if (!window_maximize_btn_)
    {
        return;
    }

    const bool shouldRestore = isWindowMaximizedForUi();
    window_maximize_btn_->setIcon(createTitleBarIcon(shouldRestore ? QStringLiteral("copy") : QStringLiteral("square"),
                                                     dark_theme_enabled_));
    window_maximize_btn_->setToolTip(shouldRestore
        ? (is_english_ ? "Restore" : "还原")
        : (is_english_ ? "Maximize" : "最大化"));
    window_maximize_btn_->setStatusTip(window_maximize_btn_->toolTip());
}

void MainWindow::toggleWindowMaximized()
{
    if (isFullScreen())
    {
        return;
    }

    if (isWindowMaximizedForUi())
    {
        const QRect restoreGeometry = normal_window_geometry_.isValid()
            ? normal_window_geometry_
            : fallbackNormalWindowGeometry();
        setWindowState(windowState() & ~Qt::WindowMaximized);
        showNormal();
        if (restoreGeometry.isValid())
        {
            setGeometry(restoreGeometry);
        }
    }
    else
    {
        rememberNormalWindowGeometry();
        showMaximized();
    }

    updateWindowControlButtons();
    updateWindowBorderFrames();
    updateWindowResizeHandles();
    QTimer::singleShot(0, this, &MainWindow::updateWindowControlButtons);
    QTimer::singleShot(60, this, &MainWindow::updateWindowControlButtons);
}

bool MainWindow::isWindowMaximizedForUi() const
{
    if (isMaximized() || windowState().testFlag(Qt::WindowMaximized))
    {
        return true;
    }
    if (isFullScreen())
    {
        return false;
    }

    const QRect availableGeometry = currentScreenAvailableGeometry();
    if (!availableGeometry.isValid())
    {
        return false;
    }

    const int tolerance = std::max(3, scalePixels(3));
    auto coversAvailableGeometry = [&](const QRect& rect) {
        return rect.isValid() &&
               rect.left() <= availableGeometry.left() + tolerance &&
               rect.top() <= availableGeometry.top() + tolerance &&
               rect.right() >= availableGeometry.right() - tolerance &&
               rect.bottom() >= availableGeometry.bottom() - tolerance &&
               rect.width() >= availableGeometry.width() - tolerance &&
               rect.height() >= availableGeometry.height() - tolerance;
    };

    return coversAvailableGeometry(frameGeometry()) || coversAvailableGeometry(geometry());
}

void MainWindow::rememberNormalWindowGeometry()
{
    if (isFullScreen() || isMaximized() || windowState().testFlag(Qt::WindowMaximized))
    {
        return;
    }

    const QRect currentGeometry = geometry();
    if (!currentGeometry.isValid() || currentGeometry.width() <= 0 || currentGeometry.height() <= 0)
    {
        return;
    }

    const QRect availableGeometry = currentScreenAvailableGeometry();
    if (availableGeometry.isValid())
    {
        const int tolerance = std::max(3, scalePixels(3));
        const bool visuallyMaximized =
            currentGeometry.left() <= availableGeometry.left() + tolerance &&
            currentGeometry.top() <= availableGeometry.top() + tolerance &&
            currentGeometry.right() >= availableGeometry.right() - tolerance &&
            currentGeometry.bottom() >= availableGeometry.bottom() - tolerance &&
            currentGeometry.width() >= availableGeometry.width() - tolerance &&
            currentGeometry.height() >= availableGeometry.height() - tolerance;
        if (visuallyMaximized)
        {
            return;
        }
    }

    normal_window_geometry_ = currentGeometry;
}

QRect MainWindow::fallbackNormalWindowGeometry() const
{
    const QRect availableGeometry = currentScreenAvailableGeometry();
    const QSize minimumSize = this->minimumSize().expandedTo(minimumSizeHint());
    QSize targetSize = base_window_size_.expandedTo(minimumSize);
    if (availableGeometry.isValid())
    {
        targetSize = targetSize.boundedTo(availableGeometry.size()).expandedTo(minimumSize.boundedTo(availableGeometry.size()));
        const QPoint topLeft(
            availableGeometry.left() + std::max(0, (availableGeometry.width() - targetSize.width()) / 2),
            availableGeometry.top() + std::max(0, (availableGeometry.height() - targetSize.height()) / 2));
        return QRect(topLeft, targetSize);
    }
    return QRect(QPoint(80, 80), targetSize);
}

QRect MainWindow::currentScreenAvailableGeometry() const
{
    const QScreen *targetScreen = screen();
    if (!targetScreen && windowHandle())
    {
        targetScreen = windowHandle()->screen();
    }
    if (!targetScreen)
    {
        targetScreen = QGuiApplication::primaryScreen();
    }
    return targetScreen ? targetScreen->availableGeometry() : QRect();
}

void MainWindow::setupWindowBorderFrames()
{
    auto createBorder = [this]() {
        auto *border = new QFrame(this);
        border->setAttribute(Qt::WA_TransparentForMouseEvents);
        border->setFocusPolicy(Qt::NoFocus);
        border->setFrameShape(QFrame::NoFrame);
        border->setLineWidth(0);
        border->setAutoFillBackground(false);
        return border;
    };

    window_border_top_ = createBorder();
    window_border_right_ = createBorder();
    window_border_bottom_ = createBorder();
    window_border_left_ = createBorder();

    window_border_top_->hide();
    window_border_bottom_->setStyleSheet(QStringLiteral("background-color: %1; border: none;")
        .arg(appThemeColorName(AppThemeColor::SurfaceSunken, dark_theme_enabled_)));
    const QString verticalBorderStyle = QStringLiteral("background-color: %1; border: none;")
        .arg(appThemeColorName(AppThemeColor::SurfaceSunken, dark_theme_enabled_));
    window_border_left_->setStyleSheet(verticalBorderStyle);
    window_border_right_->setStyleSheet(verticalBorderStyle);

    updateWindowBorderFrames();
}

void MainWindow::updateWindowBorderFrames()
{
    const bool visible = !isFullScreen() && !isWindowMaximizedForUi();
    const int borderThickness = 1;
    if (window_border_top_)
    {
        window_border_top_->setVisible(false);
    }
    if (window_border_left_)
    {
        window_border_left_->setVisible(visible);
        window_border_left_->setGeometry(0, 0, borderThickness, height());
        window_border_left_->raise();
    }
    if (window_border_right_)
    {
        window_border_right_->setVisible(visible);
        window_border_right_->setGeometry(std::max(0, width() - borderThickness), 0, borderThickness, height());
        window_border_right_->raise();
    }
    if (window_border_bottom_)
    {
        window_border_bottom_->setVisible(visible);
        window_border_bottom_->setGeometry(0, std::max(0, height() - borderThickness), width(), borderThickness);
        window_border_bottom_->raise();
    }
}

void MainWindow::setupWindowResizeHandles()
{
    const QVector<Qt::Edges> edges = {
        Qt::TopEdge | Qt::LeftEdge,
        Qt::TopEdge,
        Qt::TopEdge | Qt::RightEdge,
        Qt::LeftEdge,
        Qt::RightEdge,
        Qt::BottomEdge | Qt::LeftEdge,
        Qt::BottomEdge,
        Qt::BottomEdge | Qt::RightEdge,
    };

    window_resize_handles_.reserve(edges.size());
    for (Qt::Edges edgeSet : edges)
    {
        auto *handle = new WindowResizeHandle(edgeSet, this);
        handle->setObjectName(QStringLiteral("windowResizeHandle"));
        window_resize_handles_.append(handle);
    }

    updateWindowResizeHandles();
}

void MainWindow::updateWindowResizeHandles()
{
    if (window_resize_handles_.size() != 8)
    {
        return;
    }

    const bool visible = !isFullScreen() && !isWindowMaximizedForUi();
    const int thickness = scalePixels(8);
    const int w = width();
    const int h = height();
    const int rightX = std::max(0, w - thickness);
    const int bottomY = std::max(0, h - thickness);

    const QVector<QRect> geometries = {
        QRect(0, 0, thickness, thickness),
        QRect(thickness, 0, std::max(0, w - thickness * 2), thickness),
        QRect(rightX, 0, thickness, thickness),
        QRect(0, thickness, thickness, std::max(0, h - thickness * 2)),
        QRect(rightX, thickness, thickness, std::max(0, h - thickness * 2)),
        QRect(0, bottomY, thickness, thickness),
        QRect(thickness, bottomY, std::max(0, w - thickness * 2), thickness),
        QRect(rightX, bottomY, thickness, thickness),
    };

    for (int i = 0; i < window_resize_handles_.size(); ++i)
    {
        QWidget *handle = window_resize_handles_.at(i);
        handle->setVisible(visible);
        handle->setGeometry(geometries.at(i));
        handle->raise();
    }

    updateWindowBorderFrames();
    if (title_application_panel_ && title_application_panel_->isVisible())
    {
        title_application_panel_->raise();
    }
    if (title_application_sub_panel_ && title_application_sub_panel_->isVisible())
    {
        title_application_sub_panel_->raise();
    }
    if (title_application_nested_panel_ && title_application_nested_panel_->isVisible())
    {
        title_application_nested_panel_->raise();
    }
}

void MainWindow::onToggleTheme()
{
    dark_theme_enabled_ = !dark_theme_enabled_;
    if (qApp)
    {
        qApp->setProperty(kAppDarkThemeProperty, dark_theme_enabled_);
    }
    discardTitleApplicationMenuPanel();
    applyStyleConfiguration();
    updateThemeAction();

    QSettings settings("VaporView", "MainWindow");
    settings.setValue("dark_theme_enabled", dark_theme_enabled_);

    log(dark_theme_enabled_
        ? (is_english_ ? "Theme switched to dark" : "已切换为暗色模式")
        : (is_english_ ? "Theme switched to light" : "已切换为亮色模式"));
}

void MainWindow::onFontScaleTriggered(QAction *action)
{
    if (!action)
    {
        return;
    }

    const int percent = action->data().toInt();
    if (percent == font_scale_percent_)
    {
        return;
    }

    setFontScale(percent);
    log(QString(is_english_ ? "Font size set to %1%" : "字体大小已设置为 %1%").arg(percent));
}

int MainWindow::parseRate(const QString& text) const
{
    bool ok;
    int rate = text.toInt(&ok);
    if (ok && rate >= 1 && rate <= 1000)
    {
        return rate;
    }
    return 20;
}

bool MainWindow::isRateUnspecified(const QString& text) const
{
    const QString trimmed = text.trimmed();
    return trimmed.compare(QStringLiteral("No Set"), Qt::CaseInsensitive) == 0
        || trimmed == QStringLiteral("不设定");
}

int MainWindow::effectiveRateOrDefault(const QString& text, int defaultRate, int maxRate) const
{
    const int boundedDefault = std::clamp(defaultRate, 1, std::max(1, maxRate));
    if (isRateUnspecified(text))
    {
        return boundedDefault;
    }
    return std::clamp(parseRate(text), 1, std::max(1, maxRate));
}

void MainWindow::onGlobalRateChanged(const QString& text)
{
    int rate = parseRate(text);
    const bool skipEpsilonDeviceRate = epsilon_rate_combo_ && isRateUnspecified(epsilon_rate_combo_->currentText());
    const bool skipPtbDeviceRate = ptb_rate_combo_ && isRateUnspecified(ptb_rate_combo_->currentText());
    const bool skipHmpDeviceRate = hmp_rate_combo_ && isRateUnspecified(hmp_rate_combo_->currentText());
    const bool skipLidarDeviceRate = lidar_rate_combo_ && isRateUnspecified(lidar_rate_combo_->currentText());
    const bool skipTemperatureDeviceRate = temperature_rate_combo_ && isRateUnspecified(temperature_rate_combo_->currentText());

    epsilon_sample_rate_ = skipEpsilonDeviceRate ? kDefaultEpsilonSampleRateHz : std::clamp(rate, 20, 200);
    ptb_sample_rate_ = skipPtbDeviceRate ? kDefaultPtbSampleRateHz : clampPtbSampleRate(rate);
    hmp_sample_rate_ = skipHmpDeviceRate ? kDefaultHmpSampleRateHz : rate;
    lidar_sample_rate_ = skipLidarDeviceRate ? kDefaultLidarSampleRateHz : std::min(rate, 100);
    temperature_sample_rate_ = skipTemperatureDeviceRate ? kDefaultTemperatureSampleRateHz : std::min(rate, kMaxTemperatureSampleRateHz);

    if (epsilon_rate_combo_) epsilon_rate_combo_->blockSignals(true);
    if (ptb_rate_combo_) ptb_rate_combo_->blockSignals(true);
    if (hmp_rate_combo_) hmp_rate_combo_->blockSignals(true);
    if (lidar_rate_combo_) lidar_rate_combo_->blockSignals(true);
    if (temperature_rate_combo_) temperature_rate_combo_->blockSignals(true);

    if (epsilon_rate_combo_ && !skipEpsilonDeviceRate) epsilon_rate_combo_->setCurrentText(QString::number(epsilon_sample_rate_));
    if (ptb_rate_combo_ && !skipPtbDeviceRate) ptb_rate_combo_->setCurrentText(QString::number(ptb_sample_rate_));
    if (hmp_rate_combo_ && !skipHmpDeviceRate) hmp_rate_combo_->setCurrentText(text);
    if (lidar_rate_combo_ && !skipLidarDeviceRate) lidar_rate_combo_->setCurrentText(QString::number(lidar_sample_rate_));
    if (temperature_rate_combo_ && !skipTemperatureDeviceRate) temperature_rate_combo_->setCurrentText(QString::number(temperature_sample_rate_));

    if (epsilon_rate_combo_) epsilon_rate_combo_->blockSignals(false);
    if (ptb_rate_combo_) ptb_rate_combo_->blockSignals(false);
    if (hmp_rate_combo_) hmp_rate_combo_->blockSignals(false);
    if (lidar_rate_combo_) lidar_rate_combo_->blockSignals(false);
    if (temperature_rate_combo_) temperature_rate_combo_->blockSignals(false);

    QSettings settings("VaporView", "MainWindow");
    bool epsilonUsesCustomPacketRates = false;
    const std::map<uint8_t, int> epsilonDesiredPacketRates =
        effectiveEpsilonPacketRates(settings, epsilon_sample_rate_, &epsilonUsesCustomPacketRates);
    const int epsilonCallbackRate = epsilonPacketCallbackRate(epsilonDesiredPacketRates, epsilon_sample_rate_);
    const CollectorSnapshot collectors = snapshotCollectors();

    if (collectors.epsilon && collectors.epsilon->isRunning())
    {
        collectors.epsilon->setSampleRate(epsilonCallbackRate);
        if (!skipEpsilonDeviceRate)
        {
            collectors.epsilon->setOutputPacketRates(epsilonDesiredPacketRates);
        }
    }
    if (collectors.ptb && collectors.ptb->isRunning())
    {
        collectors.ptb->setSampleRate(ptb_sample_rate_);
        if (!skipPtbDeviceRate && !collectors.ptb->setDeviceSampleRate(ptb_sample_rate_))
        {
            log(QString(is_english_
                ? "PTB sample rate command failed for %1 Hz"
                : "PTB采样频率命令下发失败：%1 Hz").arg(ptb_sample_rate_));
        }
    }
    if (collectors.hmp && collectors.hmp->isRunning())
    {
        collectors.hmp->setSampleRate(hmp_sample_rate_);
    }
    if (collectors.lidar && collectors.lidar->isRunning())
    {
        collectors.lidar->setSampleRate(lidar_sample_rate_);
        if (!skipLidarDeviceRate)
        {
            collectors.lidar->setDeviceSampleRate(lidar_sample_rate_);
        }
    }
    if (collectors.temperature_controller && collectors.temperature_controller->isRunning())
    {
        collectors.temperature_controller->setSampleRate(temperature_sample_rate_);
    }
    
    if (epsilonUsesCustomPacketRates)
    {
        log(QString(is_english_
                        ? "All rates set to %1 Hz; EPSILON keeps the saved custom packet-rate profile."
                        : "所有频率已设置为 %1 Hz；EPSILON 保持已保存的自定义包频率配置。")
                .arg(rate));
    }
    else
    {
        log(QString(is_english_ ? "All rates set to %1 Hz" : "所有频率已设置为 %1 Hz").arg(rate));
    }
    if (skipEpsilonDeviceRate || skipPtbDeviceRate || skipHmpDeviceRate || skipLidarDeviceRate || skipTemperatureDeviceRate)
    {
        log(is_english_
            ? "Devices set to No Set keep their output-rate commands disabled."
            : "已选择“不设定”的设备保持不下发输出频率命令。");
    }
    if (!skipPtbDeviceRate && ptb_sample_rate_ != rate)
    {
        log(QString(is_english_
            ? "PTB sample rate capped at %1 Hz"
            : "PTB采样频率已限制为 %1 Hz").arg(ptb_sample_rate_));
    }
    if (!skipTemperatureDeviceRate && temperature_sample_rate_ != rate)
    {
        log(QString(is_english_
            ? "RD105 polling rate capped at %1 Hz"
            : "RD105 轮询频率已限制为 %1 Hz").arg(temperature_sample_rate_));
    }
}

void MainWindow::onGnssRateChanged(const QString& text)
{
    const bool skipDeviceRate = isRateUnspecified(text);
    epsilon_sample_rate_ = effectiveRateOrDefault(text, kDefaultEpsilonSampleRateHz, 200);
    QSettings settings("VaporView", "MainWindow");
    bool epsilonUsesCustomPacketRates = false;
    const std::map<uint8_t, int> epsilonDesiredPacketRates =
        effectiveEpsilonPacketRates(settings, epsilon_sample_rate_, &epsilonUsesCustomPacketRates);
    const int epsilonCallbackRate = epsilonPacketCallbackRate(epsilonDesiredPacketRates, epsilon_sample_rate_);
    const CollectorSnapshot collectors = snapshotCollectors();
    if (collectors.epsilon)
    {
        collectors.epsilon->setSampleRate(epsilonCallbackRate);
        if (collectors.epsilon->isRunning() && !skipDeviceRate)
        {
            collectors.epsilon->setOutputPacketRates(epsilonDesiredPacketRates);
        }
    }
    if (skipDeviceRate)
    {
        log(is_english_
            ? "EPSILON output-rate command disabled; using the current device output."
            : "已禁用 EPSILON 输出频率下发，使用设备当前输出。");
    }
    else if (epsilonUsesCustomPacketRates)
    {
        log(QString(is_english_
                        ? "EPSILON grouped rate was set to %1 Hz, but the saved custom packet-rate profile remains active."
                        : "EPSILON 分组频率已设置为 %1 Hz，但当前仍启用已保存的自定义包频率配置。")
                .arg(epsilon_sample_rate_));
    }
    else
    {
        log(QString(is_english_ ? "EPSILON output rate set to %1 Hz" : "EPSILON 输出频率已设置为 %1 Hz").arg(epsilon_sample_rate_));
    }
}

void MainWindow::onImuRateChanged(const QString& text)
{
    Q_UNUSED(text);
}

void MainWindow::onPtbRateChanged(const QString& text)
{
    const bool skipDeviceRate = isRateUnspecified(text);
    const int requestedRate = parseRate(text);
    ptb_sample_rate_ = skipDeviceRate ? kDefaultPtbSampleRateHz : clampPtbSampleRate(requestedRate);
    if (skipDeviceRate)
    {
        const CollectorSnapshot collectors = snapshotCollectors();
        if (collectors.ptb)
        {
            collectors.ptb->setSampleRate(ptb_sample_rate_);
        }
        log(is_english_
            ? "PTB sample-rate command disabled; using the current device output."
            : "已禁用 PTB 采样频率下发，使用设备当前输出。");
        return;
    }

    if (ptb_rate_combo_ && ptb_rate_combo_->currentText() != QString::number(ptb_sample_rate_))
    {
        QSignalBlocker blocker(ptb_rate_combo_);
        ptb_rate_combo_->setCurrentText(QString::number(ptb_sample_rate_));
    }

    const CollectorSnapshot collectors = snapshotCollectors();
    if (collectors.ptb)
    {
        collectors.ptb->setSampleRate(ptb_sample_rate_);
        if (collectors.ptb->isRunning())
        {
            if (!collectors.ptb->setDeviceSampleRate(ptb_sample_rate_))
            {
                log(QString(is_english_
                    ? "PTB sample rate command failed for %1 Hz"
                    : "PTB采样频率命令下发失败：%1 Hz").arg(ptb_sample_rate_));
                return;
            }
        }
    }
    if (requestedRate != ptb_sample_rate_)
    {
        log(QString(is_english_
            ? "PTB sample rate set to %1 Hz (capped from %2 Hz)"
            : "PTB采样频率已设置为 %1 Hz（由 %2 Hz 限制）")
            .arg(ptb_sample_rate_)
            .arg(requestedRate));
    }
    else
    {
        log(QString(is_english_ ? "PTB sample rate set to %1 Hz" : "PTB采样频率已设置为 %1 Hz").arg(ptb_sample_rate_));
    }
}

void MainWindow::onHmpRateChanged(const QString& text)
{
    const bool skipDeviceRate = isRateUnspecified(text);
    hmp_sample_rate_ = effectiveRateOrDefault(text, kDefaultHmpSampleRateHz);
    const CollectorSnapshot collectors = snapshotCollectors();
    if (collectors.hmp) collectors.hmp->setSampleRate(hmp_sample_rate_);
    if (skipDeviceRate)
    {
        log(is_english_
            ? "HMP polling-rate selection left unset; using the default host polling rate."
            : "HMP 轮询频率保持不设定，使用默认主机轮询频率。");
    }
    else
    {
        log(QString(is_english_ ? "HMP sample rate set to %1 Hz" : "HMP采样频率已设置为 %1 Hz").arg(hmp_sample_rate_));
    }
}

void MainWindow::onLidarRateChanged(const QString& text)
{
    const bool skipDeviceRate = isRateUnspecified(text);
    lidar_sample_rate_ = effectiveRateOrDefault(text, kDefaultLidarSampleRateHz, 100);
    const CollectorSnapshot collectors = snapshotCollectors();
    if (collectors.lidar)
    {
        collectors.lidar->setSampleRate(lidar_sample_rate_);
        if (collectors.lidar->isRunning() && !skipDeviceRate)
        {
            collectors.lidar->setDeviceSampleRate(lidar_sample_rate_);
        }
    }
    if (skipDeviceRate)
    {
        log(is_english_ ? "Lidar output-rate command disabled; using device default/adaptive output" : "已禁用激光测距仪输出频率下发，使用设备默认/自适应输出");
    }
    else
    {
        log(QString(is_english_ ? "Lidar sample rate set to %1 Hz" : "激光测距仪采样频率已设置为 %1 Hz").arg(lidar_sample_rate_));
    }
}

void MainWindow::onTemperatureRateChanged(const QString& text)
{
    const bool skipDeviceRate = isRateUnspecified(text);
    temperature_sample_rate_ = effectiveRateOrDefault(text, kDefaultTemperatureSampleRateHz, kMaxTemperatureSampleRateHz);
    const CollectorSnapshot collectors = snapshotCollectors();
    if (collectors.temperature_controller)
    {
        collectors.temperature_controller->setSampleRate(temperature_sample_rate_);
    }
    if (skipDeviceRate)
    {
        log(is_english_
            ? "RD105 polling-rate selection left unset; using the default host polling rate."
            : "RD105 轮询频率保持不设定，使用默认主机轮询频率。");
    }
    else
    {
        log(QString(is_english_ ? "RD105 polling rate set to %1 Hz" : "RD105 轮询频率已设置为 %1 Hz").arg(temperature_sample_rate_));
    }
}

void MainWindow::applyAllSampleRates()
{
    int rate = parseRate(global_rate_combo_ ? global_rate_combo_->currentText() : QString::number(kDefaultHmpSampleRateHz));
    const bool skipEpsilonDeviceRate = epsilon_rate_combo_ && isRateUnspecified(epsilon_rate_combo_->currentText());
    const bool skipPtbDeviceRate = ptb_rate_combo_ && isRateUnspecified(ptb_rate_combo_->currentText());
    const bool skipHmpDeviceRate = hmp_rate_combo_ && isRateUnspecified(hmp_rate_combo_->currentText());
    const bool skipLidarDeviceRate = lidar_rate_combo_ && isRateUnspecified(lidar_rate_combo_->currentText());
    const bool skipTemperatureDeviceRate = temperature_rate_combo_ && isRateUnspecified(temperature_rate_combo_->currentText());
    const CollectorSnapshot collectors = snapshotCollectors();
    QSettings settings("VaporView", "MainWindow");
    bool epsilonUsesCustomPacketRates = false;
    const int epsilonRate = skipEpsilonDeviceRate ? kDefaultEpsilonSampleRateHz : std::clamp(rate, 20, 200);
    const int ptbRate = skipPtbDeviceRate ? kDefaultPtbSampleRateHz : clampPtbSampleRate(rate);
    const int hmpRate = skipHmpDeviceRate ? kDefaultHmpSampleRateHz : rate;
    const int lidarRate = skipLidarDeviceRate ? kDefaultLidarSampleRateHz : std::min(rate, 100);
    const int temperatureRate = skipTemperatureDeviceRate ? kDefaultTemperatureSampleRateHz : std::min(rate, kMaxTemperatureSampleRateHz);
    const std::map<uint8_t, int> epsilonDesiredPacketRates =
        effectiveEpsilonPacketRates(settings, epsilonRate, &epsilonUsesCustomPacketRates);
    const int epsilonCallbackRate = epsilonPacketCallbackRate(epsilonDesiredPacketRates, epsilonRate);

    if (collectors.epsilon && collectors.epsilon->isRunning())
    {
        collectors.epsilon->setSampleRate(epsilonCallbackRate);
        if (!skipEpsilonDeviceRate)
        {
            collectors.epsilon->setOutputPacketRates(epsilonDesiredPacketRates);
        }
    }
    if (collectors.ptb && collectors.ptb->isRunning())
    {
        collectors.ptb->setSampleRate(ptbRate);
        if (!skipPtbDeviceRate && !collectors.ptb->setDeviceSampleRate(ptbRate))
        {
            log(QString(is_english_
                ? "PTB sample rate command failed for %1 Hz"
                : "PTB采样频率命令下发失败：%1 Hz").arg(ptbRate));
        }
    }
    if (collectors.hmp && collectors.hmp->isRunning())
    {
        collectors.hmp->setSampleRate(hmpRate);
    }
    if (collectors.lidar && collectors.lidar->isRunning())
    {
        collectors.lidar->setSampleRate(lidarRate);
        if (!skipLidarDeviceRate)
        {
            collectors.lidar->setDeviceSampleRate(lidarRate);
        }
    }
    if (collectors.temperature_controller && collectors.temperature_controller->isRunning())
    {
        collectors.temperature_controller->setSampleRate(temperatureRate);
    }

    if (epsilon_rate_combo_) epsilon_rate_combo_->blockSignals(true);
    if (ptb_rate_combo_) ptb_rate_combo_->blockSignals(true);
    if (hmp_rate_combo_) hmp_rate_combo_->blockSignals(true);
    if (lidar_rate_combo_) lidar_rate_combo_->blockSignals(true);
    if (temperature_rate_combo_) temperature_rate_combo_->blockSignals(true);

    if (epsilon_rate_combo_ && !skipEpsilonDeviceRate) epsilon_rate_combo_->setCurrentText(QString::number(epsilonRate));
    if (ptb_rate_combo_ && !skipPtbDeviceRate) ptb_rate_combo_->setCurrentText(QString::number(ptbRate));
    if (hmp_rate_combo_ && !skipHmpDeviceRate) hmp_rate_combo_->setCurrentText(QString::number(rate));
    if (lidar_rate_combo_ && !skipLidarDeviceRate) lidar_rate_combo_->setCurrentText(QString::number(lidarRate));
    if (temperature_rate_combo_ && !skipTemperatureDeviceRate) temperature_rate_combo_->setCurrentText(QString::number(temperatureRate));

    if (epsilon_rate_combo_) epsilon_rate_combo_->blockSignals(false);
    if (ptb_rate_combo_) ptb_rate_combo_->blockSignals(false);
    if (hmp_rate_combo_) hmp_rate_combo_->blockSignals(false);
    if (lidar_rate_combo_) lidar_rate_combo_->blockSignals(false);
    if (temperature_rate_combo_) temperature_rate_combo_->blockSignals(false);

    gnss_sample_rate_ = rate;
    imu_sample_rate_ = rate;
    ptb_sample_rate_ = ptbRate;
    hmp_sample_rate_ = hmpRate;
    lidar_sample_rate_ = lidarRate;
    temperature_sample_rate_ = temperatureRate;

    log(QString(is_english_ ? "All rates set to %1 Hz" : "所有频率已设置为 %1 Hz").arg(rate));
    if (skipEpsilonDeviceRate || skipPtbDeviceRate || skipHmpDeviceRate || skipLidarDeviceRate || skipTemperatureDeviceRate)
    {
        log(is_english_
            ? "Devices set to No Set keep their output-rate commands disabled."
            : "已选择“不设定”的设备保持不下发输出频率命令。");
    }
    if (!skipPtbDeviceRate && ptbRate != rate)
    {
        log(QString(is_english_
            ? "PTB sample rate capped at %1 Hz"
            : "PTB采样频率已限制为 %1 Hz").arg(ptbRate));
    }
    if (!skipTemperatureDeviceRate && temperatureRate != rate)
    {
        log(QString(is_english_
            ? "RD105 polling rate capped at %1 Hz"
            : "RD105 轮询频率已限制为 %1 Hz").arg(temperatureRate));
    }
}

void MainWindow::log(const QString& message)
{
    auto scrollLogToBottom = [this]() {
        if (log_text_edit_)
        {
            log_text_edit_->ensureCursorVisible();
            if (QScrollBar *scrollBar = log_text_edit_->verticalScrollBar())
            {
                scrollBar->setValue(scrollBar->maximum());
            }
        }
    };

    if (message.startsWith('\r'))
    {
        if (!log_text_edit_)
        {
            return;
        }
        const QString inlineMessage = message.mid(1);
        QTextCursor cursor = log_text_edit_->textCursor();
        cursor.movePosition(QTextCursor::End);

        if (!has_inline_progress_log_)
        {
            if (!log_text_edit_->document()->isEmpty())
            {
                cursor.insertBlock();
            }
            cursor.insertText(inlineMessage);
            has_inline_progress_log_ = true;
        }
        else
        {
            cursor.movePosition(QTextCursor::StartOfBlock, QTextCursor::KeepAnchor);
            cursor.removeSelectedText();
            cursor.insertText(inlineMessage);
        }

        log_text_edit_->setTextCursor(cursor);
        scrollLogToBottom();
        return;
    }

    QString timestamp = QDateTime::currentDateTime().toString("hh:mm:ss");
    const QString displayLine = QString("[%1] %2").arg(timestamp, message);
    log_entries_.append(displayLine);
    if (log_text_edit_ && shouldShowLogLine(displayLine))
    {
        log_text_edit_->append(displayLine);
        scrollLogToBottom();
    }
    has_inline_progress_log_ = false;

    appendEventLogLine(QStringLiteral("info"), message);
    if (shouldMirrorToErrorLog(message))
    {
        appendErrorLogLine(message);
    }
}

bool MainWindow::shouldShowLogLine(const QString& line) const
{
    if (log_filter_ack_enabled_ && line.contains(QStringLiteral("ACK"), Qt::CaseInsensitive))
    {
        return false;
    }
    if (log_filter_config_enabled_ &&
        (line.contains(QStringLiteral("配置"), Qt::CaseInsensitive) ||
         line.contains(QStringLiteral("config"), Qt::CaseInsensitive) ||
         line.contains(QStringLiteral("频率"), Qt::CaseInsensitive) ||
         line.contains(QStringLiteral("波特率"), Qt::CaseInsensitive) ||
         line.contains(QStringLiteral("output rate"), Qt::CaseInsensitive) ||
         line.contains(QStringLiteral("sample rate"), Qt::CaseInsensitive) ||
         line.contains(QStringLiteral("rate"), Qt::CaseInsensitive)))
    {
        return false;
    }
    if (log_filter_connection_enabled_ &&
        (line.contains(QStringLiteral("连接"), Qt::CaseInsensitive) ||
         line.contains(QStringLiteral("断开"), Qt::CaseInsensitive) ||
         line.contains(QStringLiteral("串口"), Qt::CaseInsensitive) ||
         line.contains(QStringLiteral("端口"), Qt::CaseInsensitive) ||
         line.contains(QStringLiteral("connect"), Qt::CaseInsensitive) ||
         line.contains(QStringLiteral("disconnect"), Qt::CaseInsensitive) ||
         line.contains(QStringLiteral("port"), Qt::CaseInsensitive) ||
         line.contains(QStringLiteral("telemetry link"), Qt::CaseInsensitive) ||
         line.contains(QStringLiteral("handshake"), Qt::CaseInsensitive)))
    {
        return false;
    }
    if (log_filter_recording_enabled_ &&
        (line.contains(QStringLiteral("记录"), Qt::CaseInsensitive) ||
         line.contains(QStringLiteral("定时"), Qt::CaseInsensitive) ||
         line.contains(QStringLiteral("recording"), Qt::CaseInsensitive) ||
         line.contains(QStringLiteral("record"), Qt::CaseInsensitive) ||
         line.contains(QStringLiteral("session"), Qt::CaseInsensitive) ||
         line.contains(QStringLiteral("schedule"), Qt::CaseInsensitive)))
    {
        return false;
    }
    return true;
}

void MainWindow::renderLogView()
{
    if (!log_text_edit_)
    {
        return;
    }

    log_text_edit_->clear();
    for (const QString& line : log_entries_)
    {
        if (shouldShowLogLine(line))
        {
            log_text_edit_->append(line);
        }
    }
    has_inline_progress_log_ = false;
    if (QScrollBar *scrollBar = log_text_edit_->verticalScrollBar())
    {
        scrollBar->setValue(scrollBar->maximum());
    }
}

void MainWindow::updateLogFilterAction()
{
    if (!log_filter_ack_action_)
    {
        return;
    }

    const QIcon checkIcon = createMenuCheckIcon(dark_theme_enabled_);
    const QStringList filterTexts = is_english_
        ? QStringList{
              QStringLiteral("Filter ACK logs"),
              QStringLiteral("Filter config and rate logs"),
              QStringLiteral("Filter connection and port logs"),
              QStringLiteral("Filter recording and schedule logs")}
        : QStringList{
              QStringLiteral("过滤 ACK 日志"),
              QStringLiteral("过滤配置和频率日志"),
              QStringLiteral("过滤连接和端口日志"),
              QStringLiteral("过滤记录和定时日志")};
    const QFontMetrics filterTextMetrics(qApp ? qApp->font() : font());
    int filterTextWidth = 0;
    for (const QString& text : filterTexts)
    {
        filterTextWidth = std::max(filterTextWidth, filterTextMetrics.horizontalAdvance(text));
    }
    const int rowLeftPadding = scalePixels(18);
    const int rowRightPadding = scalePixels(14);
    const int rowSpacing = scalePixels(6);
    const int checkSlotWidth = scalePixels(18);
    const int rowHeight = scalePixels(36);
    const int menuItemWidth = rowLeftPadding + filterTextWidth + rowSpacing + checkSlotWidth + rowRightPadding;

    const auto updateAction = [this, &checkIcon, filterTextWidth, checkSlotWidth, menuItemWidth, rowHeight, rowLeftPadding, rowRightPadding, rowSpacing](QAction *action,
                                                             bool enabled,
                                                             const QString& englishText,
                                                             const QString& chineseText,
                                                            const QString& englishDetail,
                                                            const QString& chineseDetail) {
        if (!action)
        {
            return;
        }
        const QString text = is_english_ ? englishText : chineseText;
        const QString detail = is_english_ ? englishDetail : chineseDetail;
        action->setText(text);
        action->setToolTip(detail);
        action->setStatusTip(action->toolTip());

        auto *widgetAction = qobject_cast<QWidgetAction *>(action);
        auto *row = widgetAction ? qobject_cast<SingleLevelPopupMenuRow *>(widgetAction->defaultWidget()) : nullptr;
        if (!row)
        {
            action->setIcon(enabled ? checkIcon : QIcon());
            return;
        }

        row->setHorizontalPadding(rowLeftPadding, rowRightPadding);
        row->setRowSpacing(rowSpacing);
        row->setCheckSlotWidth(checkSlotWidth);
        row->setCheckIconSize(QSize(scalePixels(16), scalePixels(16)));
        row->setRowHeight(rowHeight);
        row->setMinimumRowWidth(menuItemWidth);
        row->setFixedWidth(menuItemWidth);
        row->setTextFixedWidth(filterTextWidth);
        row->setText(text);
        row->setToolTip(detail);
        row->setStatusTip(detail);
        row->setCheckIcon(checkIcon);
        row->setChecked(enabled);
        row->refreshTheme();
    };

    updateAction(log_filter_ack_action_,
                 log_filter_ack_enabled_,
                 QStringLiteral("Filter ACK logs"),
                 QStringLiteral("过滤 ACK 日志"),
                 QStringLiteral("Hide remote ACK command result logs from the display"),
                 QStringLiteral("仅从显示中隐藏远程 ACK 命令结果日志"));
    updateAction(log_filter_config_action_,
                 log_filter_config_enabled_,
                 QStringLiteral("Filter config and rate logs"),
                 QStringLiteral("过滤配置和频率日志"),
                 QStringLiteral("Hide configuration, baud-rate, and output-rate logs from the display"),
                 QStringLiteral("仅从显示中隐藏配置、波特率和输出频率日志"));
    updateAction(log_filter_connection_action_,
                 log_filter_connection_enabled_,
                 QStringLiteral("Filter connection and port logs"),
                 QStringLiteral("过滤连接和端口日志"),
                 QStringLiteral("Hide connection, disconnection, port, and handshake logs from the display"),
                 QStringLiteral("仅从显示中隐藏连接、断开、端口和握手日志"));
    updateAction(log_filter_recording_action_,
                 log_filter_recording_enabled_,
                 QStringLiteral("Filter recording and schedule logs"),
                 QStringLiteral("过滤记录和定时日志"),
                 QStringLiteral("Hide recording session and scheduled-recording logs from the display"),
                 QStringLiteral("仅从显示中隐藏记录会话和定时记录日志"));

    if (log_filter_menu_)
    {
        log_filter_menu_->setTitle(is_english_ ? QStringLiteral("Log Filters")
                                               : QStringLiteral("日志过滤"));
        log_filter_menu_->refreshTheme();
        log_filter_menu_->setPanelContentWidth(menuItemWidth);
    }
    if (log_filter_btn_)
    {
        log_filter_btn_->setIcon(createLogFilterIcon());
        log_filter_btn_->setToolTip(is_english_ ? QStringLiteral("Log filters")
                                                : QStringLiteral("日志过滤"));
        log_filter_btn_->setStatusTip(log_filter_btn_->toolTip());
    }
}

void MainWindow::updateRecordingStatusLabel()
{
    if (!recording_status_label_)
    {
        return;
    }

    auto setVisualStatus = [this](const char *status) {
        const QString statusValue = QString::fromLatin1(status);
        recording_status_label_->setProperty("status", statusValue);
        if (recording_status_card_)
        {
            recording_status_card_->setProperty("status", statusValue);
        }
    };
    auto polishVisualStatus = [this]() {
        recording_status_label_->style()->unpolish(recording_status_label_);
        recording_status_label_->style()->polish(recording_status_label_);
        if (recording_status_card_)
        {
            recording_status_card_->style()->unpolish(recording_status_card_);
            recording_status_card_->style()->polish(recording_status_card_);
        }
    };
    auto localDetailText = [this](const QString& session,
                                  qlonglong sensorRows,
                                  qlonglong waveformFrames,
                                  qulonglong rawEpsilon,
                                  qulonglong rawPtb,
                                  qulonglong rawHmp,
                                  qulonglong rawLidar,
                                  qulonglong rawTcpWave) {
        return is_english_
            ? QStringLiteral("Session: %1\nSensor rows: %2\nWaveform frames: %3\nRaw EPSILON: %4\nRaw PTB: %5\nRaw HMP: %6\nRaw Lidar: %7\nRaw TCP wave: %8")
                  .arg(session)
                  .arg(sensorRows)
                  .arg(waveformFrames)
                  .arg(rawEpsilon)
                  .arg(rawPtb)
                  .arg(rawHmp)
                  .arg(rawLidar)
                  .arg(rawTcpWave)
            : QStringLiteral("会话：%1\n设备行数：%2\n波形帧数：%3\nRaw EPSILON：%4\nRaw PTB：%5\nRaw HMP：%6\nRaw Lidar：%7\nRaw TCP 波形：%8")
                  .arg(session)
                  .arg(sensorRows)
                  .arg(waveformFrames)
                  .arg(rawEpsilon)
                  .arg(rawPtb)
                  .arg(rawHmp)
                  .arg(rawLidar)
                  .arg(rawTcpWave);
    };
    auto appendScheduledLine = [this](const QString& text) {
        const QString scheduledLine = scheduledRecordingStatusLine();
        return scheduledLine.isEmpty() ? text : QStringLiteral("%1\n%2").arg(text, scheduledLine);
    };
    auto setRecordingTitleIcon = [this](bool recordingActive) {
        setSectionTitleIconName(recording_status_title_lbl_,
                                recordingActive ? QStringLiteral("pencil-sparkles") : QStringLiteral("pencil"),
                                dark_theme_enabled_);
    };

    if (isRemoteSkyMode())
    {
        const bool useLastRemoteStatus =
            remote_status_.session_name.isEmpty() &&
            remote_status_.telemetry_record_count == 0 &&
            remote_status_.raw_tcp_wave_record_count == 0 &&
            has_last_remote_recording_status_;
        const VaporView::TelemetryStatus& displayStatus = useLastRemoteStatus
            ? last_remote_recording_status_
            : remote_status_;
        const quint64 rawTotal =
            displayStatus.raw_epsilon_record_count +
            displayStatus.raw_ptb_record_count +
            displayStatus.raw_hmp_record_count +
            displayStatus.raw_lidar_record_count +
            displayStatus.raw_tcp_wave_record_count;
        const QString elapsed = formatElapsedCompact(displayStatus.recording_elapsed_ms);
        const QString session = displayStatus.session_name.isEmpty()
            ? QStringLiteral("--")
            : displayStatus.session_name;
        const QString detail = is_english_
            ? QStringLiteral("Session: %1\nElapsed: %2\nTelemetry rows: %3\nWave features: %4\nWave snapshots: %5\nRaw EPSILON: %6\nRaw PTB: %7\nRaw HMP: %8\nRaw Lidar: %9\nRaw TCP wave: %10")
                  .arg(session)
                  .arg(elapsed)
                  .arg(displayStatus.telemetry_record_count)
                  .arg(displayStatus.waveform_feature_record_count)
                  .arg(displayStatus.waveform_snapshot_record_count)
                  .arg(displayStatus.raw_epsilon_record_count)
                  .arg(displayStatus.raw_ptb_record_count)
                  .arg(displayStatus.raw_hmp_record_count)
                  .arg(displayStatus.raw_lidar_record_count)
                  .arg(displayStatus.raw_tcp_wave_record_count)
            : QStringLiteral("会话：%1\n时长：%2\n遥测行数：%3\n波形特征：%4\n波形快照：%5\nRaw EPSILON：%6\nRaw PTB：%7\nRaw HMP：%8\nRaw Lidar：%9\nRaw TCP 波形：%10")
                  .arg(session)
                  .arg(elapsed)
                  .arg(displayStatus.telemetry_record_count)
                  .arg(displayStatus.waveform_feature_record_count)
                  .arg(displayStatus.waveform_snapshot_record_count)
                  .arg(displayStatus.raw_epsilon_record_count)
                  .arg(displayStatus.raw_ptb_record_count)
                  .arg(displayStatus.raw_hmp_record_count)
                  .arg(displayStatus.raw_lidar_record_count)
                  .arg(displayStatus.raw_tcp_wave_record_count);
        const QString detailWithSchedule = appendScheduledLine(detail);
        recording_status_label_->setToolTip(detailWithSchedule);
        if (recording_status_card_)
        {
            recording_status_card_->setToolTip(detailWithSchedule);
        }
        if (remote_recording_state_ == 1)
        {
            setRecordingTitleIcon(true);
            recording_status_label_->setText(
                QString(is_english_ ? "Sky Recording: On\n%1\nRaw total: %2"
                                    : "天空端记录：进行中\n%1\nRaw 总数：%2")
                    .arg(detailWithSchedule)
                    .arg(rawTotal));
            setVisualStatus("connected");
        }
        else if (remote_recording_state_ == 2)
        {
            setRecordingTitleIcon(false);
            recording_status_label_->setText(
                QString(is_english_ ? "Sky Recording: Paused\n%1\nRaw total: %2"
                                    : "天空端记录：已暂停\n%1\nRaw 总数：%2")
                    .arg(detailWithSchedule)
                    .arg(rawTotal));
            setVisualStatus("connecting");
        }
        else
        {
            setRecordingTitleIcon(false);
            recording_status_label_->setText(
                QString(is_english_ ? "Sky Recording: Off\n%1\nRaw total: %2"
                                    : "天空端记录：未记录\n%1\nRaw 总数：%2")
                    .arg(detailWithSchedule)
                    .arg(rawTotal));
            setVisualStatus("disconnected");
        }
        polishVisualStatus();
        updateRecordingActionStates();
        return;
    }

    if (sensors_file_ && sensors_file_->isOpen())
    {
        const QFileInfo info(session_directory_);
        const QString session = info.fileName().isEmpty() ? QStringLiteral("--") : info.fileName();
        const QString detail = localDetailText(
            session,
            static_cast<qlonglong>(recording_entry_count_.load()),
            static_cast<qlonglong>(waveform_frame_count_.load()),
            static_cast<qulonglong>(raw_epsilon_record_count_.load()),
            static_cast<qulonglong>(raw_ptb_record_count_.load()),
            static_cast<qulonglong>(raw_hmp_record_count_.load()),
            static_cast<qulonglong>(raw_lidar_record_count_.load()),
            static_cast<qulonglong>(raw_tcp_wave_record_count_.load()));
        if (recording_paused_)
        {
            setRecordingTitleIcon(false);
            recording_status_label_->setText(
                QString(is_english_ ? "Recording: Paused\n%1" : "记录：已暂停\n%1")
                    .arg(appendScheduledLine(detail)));
            setVisualStatus("connecting");
        }
        else
        {
            setRecordingTitleIcon(true);
            recording_status_label_->setText(
                QString(is_english_ ? "Recording: On\n%1" : "记录：进行中\n%1")
                    .arg(appendScheduledLine(detail)));
            setVisualStatus("connected");
        }
    }
    else
    {
        const QString session = last_recording_session_name_.isEmpty()
            ? QStringLiteral("--")
            : last_recording_session_name_;
        const QString detail = localDetailText(
            session,
            static_cast<qlonglong>(last_recording_entry_count_),
            static_cast<qlonglong>(last_recording_waveform_frame_count_),
            static_cast<qulonglong>(last_raw_epsilon_record_count_),
            static_cast<qulonglong>(last_raw_ptb_record_count_),
            static_cast<qulonglong>(last_raw_hmp_record_count_),
            static_cast<qulonglong>(last_raw_lidar_record_count_),
            static_cast<qulonglong>(last_raw_tcp_wave_record_count_));
        recording_status_label_->setText(
            QString(is_english_ ? "Recording: Off\n%1" : "记录：未记录\n%1")
                .arg(appendScheduledLine(detail)));
        setRecordingTitleIcon(false);
        setVisualStatus("disconnected");
    }

    const QString summary = recording_status_label_->text();
    recording_status_label_->setToolTip(summary);
    if (recording_status_card_)
    {
        recording_status_card_->setToolTip(summary);
    }
    polishVisualStatus();
    updateRecordingActionStates();
}

QString MainWindow::defaultRecordingDirectory() const
{
    QDir dir(QCoreApplication::applicationDirPath());
    for (int i = 0; i < 6; ++i)
    {
        if (QFileInfo::exists(dir.filePath("CMakeLists.txt")) && QFileInfo::exists(dir.filePath("README.md")))
        {
            return dir.filePath("data");
        }
        if (!dir.cdUp())
        {
            break;
        }
    }

    return QDir(QCoreApplication::applicationDirPath()).filePath("data");
}

QString MainWindow::locateRepositoryRoot() const
{
    QDir dir(QCoreApplication::applicationDirPath());
    for (int i = 0; i < 6; ++i)
    {
        if (QFileInfo::exists(dir.filePath("CMakeLists.txt")) && QFileInfo::exists(dir.filePath("README.md")))
        {
            return dir.path();
        }
        if (!dir.cdUp())
        {
            break;
        }
    }

    return QString();
}

bool MainWindow::prepareRecordingSessionLayout(const QString& recordsPath, const QString& sessionName)
{
    QDir recordsDir(recordsPath);
    if (!recordsDir.exists() && !recordsDir.mkpath("."))
    {
        return false;
    }

    QString finalSessionName = sessionName;
    QString finalSessionDirectory = recordsDir.filePath(finalSessionName);
    int suffix = 1;
    while (QFileInfo::exists(finalSessionDirectory))
    {
        finalSessionName = QString("%1_%2").arg(sessionName).arg(suffix++);
        finalSessionDirectory = recordsDir.filePath(finalSessionName);
    }

    QDir sessionDir(finalSessionDirectory);
    if (!recordsDir.mkpath(finalSessionName) ||
        !sessionDir.mkpath("sensors") ||
        !sessionDir.mkpath("raw") ||
        !sessionDir.mkpath("logs") ||
        !sessionDir.mkpath("config"))
    {
        return false;
    }

    session_name_ = finalSessionName;
    session_directory_ = QDir::fromNativeSeparators(finalSessionDirectory);
    sensors_filename_ = QDir::fromNativeSeparators(sessionDir.filePath("sensors/devices.csv"));
    raw_epsilon_filename_ = QDir::fromNativeSeparators(sessionDir.filePath("raw/epsilon.dat"));
    raw_ptb_filename_ = QDir::fromNativeSeparators(sessionDir.filePath("raw/ptb.dat"));
    raw_hmp_filename_ = QDir::fromNativeSeparators(sessionDir.filePath("raw/hmp.dat"));
    raw_lidar_filename_ = QDir::fromNativeSeparators(sessionDir.filePath("raw/lidar.dat"));
    raw_tcp_wave_filename_ = QDir::fromNativeSeparators(sessionDir.filePath("raw/tcp_wave.dat"));
    raw_tcp_wave_peak_index_filename_ = QDir::fromNativeSeparators(sessionDir.filePath("raw/tcp_wave_peaks.csv"));
    raw_dat_doc_filename_ = QDir::fromNativeSeparators(sessionDir.filePath("raw_dat_format.md"));
    session_metadata_filename_ = QDir::fromNativeSeparators(sessionDir.filePath("session.json"));
    event_log_filename_ = QDir::fromNativeSeparators(sessionDir.filePath("logs/event_log.csv"));
    error_log_filename_ = QDir::fromNativeSeparators(sessionDir.filePath("logs/error_log.txt"));
    device_config_filename_ = QDir::fromNativeSeparators(sessionDir.filePath("config/device_config.json"));
    return true;
}

bool MainWindow::copyRawDatFormatDocumentToSession()
{
    if (session_directory_.isEmpty() || raw_dat_doc_filename_.isEmpty())
    {
        return false;
    }

    const QString repositoryRoot = locateRepositoryRoot();
    if (repositoryRoot.isEmpty())
    {
        return false;
    }

    const QString sourcePath = QDir(repositoryRoot).filePath("docs/raw_dat_format.md");
    if (!QFileInfo::exists(sourcePath))
    {
        return false;
    }

    QFile::remove(raw_dat_doc_filename_);
    return QFile::copy(sourcePath, raw_dat_doc_filename_);
}

bool MainWindow::openUnifiedRawDatFile(std::unique_ptr<QFile>& file, const QString& filename, quint16 sourceId)
{
    if (filename.isEmpty())
    {
        return false;
    }

    file = std::make_unique<QFile>(filename);
    if (!file->open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        file.reset();
        return false;
    }

    UnifiedRawFileHeader header{};
    std::memcpy(header.magic, kUnifiedRawMagic, sizeof(header.magic));
    header.version = qToLittleEndian(kUnifiedRawFormatVersion);
    header.header_size = qToLittleEndian(static_cast<quint32>(sizeof(UnifiedRawFileHeader)));
    header.source_id = qToLittleEndian(sourceId);
    header.reserved = 0;

    if (file->write(reinterpret_cast<const char*>(&header), sizeof(header)) != static_cast<qint64>(sizeof(header)))
    {
        file->close();
        file.reset();
        return false;
    }

    file->flush();
    return true;
}

bool MainWindow::writeUnifiedRawRecord(QFile *file,
                                       std::atomic<quint64>& recordCount,
                                       quint16 sourceId,
                                       quint16 recordType,
                                       quint32 flags,
                                       quint64 hostTimestampUs,
                                       const void *payload,
                                       size_t payloadSize)
{
    if (!file || !file->isOpen() || (payloadSize > 0 && !payload) ||
        payloadSize > static_cast<size_t>(std::numeric_limits<quint32>::max()))
    {
        return false;
    }

    std::lock_guard<std::mutex> lock(recording_files_mutex_);
    if (!file->isOpen())
    {
        return false;
    }

    const quint64 sequence = recordCount.load(std::memory_order_relaxed);
    UnifiedRawRecordHeader header{};
    header.marker = qToLittleEndian(kUnifiedRawRecordMarker);
    header.header_size = qToLittleEndian(static_cast<quint32>(sizeof(UnifiedRawRecordHeader)));
    header.host_timestamp_us = qToLittleEndian(hostTimestampUs);
    header.payload_size = qToLittleEndian(static_cast<quint32>(payloadSize));
    header.source_id = qToLittleEndian(sourceId);
    header.record_type = qToLittleEndian(recordType);
    header.flags = qToLittleEndian(flags);
    header.sequence = qToLittleEndian(sequence);

    if (file->write(reinterpret_cast<const char*>(&header), sizeof(header)) != static_cast<qint64>(sizeof(header)))
    {
        return false;
    }
    if (payloadSize > 0 &&
        file->write(reinterpret_cast<const char*>(payload), static_cast<qint64>(payloadSize)) != static_cast<qint64>(payloadSize))
    {
        return false;
    }

    recordCount.store(sequence + 1, std::memory_order_relaxed);
    return true;
}

void MainWindow::startTcpRawRecordingWorker()
{
    stopTcpRawRecordingWorker();

    {
        std::lock_guard<std::mutex> lock(tcp_raw_record_queue_mutex_);
        tcp_raw_record_queue_.clear();
        tcp_raw_record_queue_bytes_ = 0;
        tcp_raw_record_dropped_count_ = 0;
        last_tcp_raw_queue_warning_ms_ = 0;
        tcp_raw_recording_worker_running_ = true;
    }

    tcp_raw_recording_thread_ = std::thread([this]() {
        while (true)
        {
            TcpRawRecord record;
            {
                std::unique_lock<std::mutex> lock(tcp_raw_record_queue_mutex_);
                tcp_raw_record_queue_cv_.wait(lock, [this]() {
                    return !tcp_raw_record_queue_.empty() || !tcp_raw_recording_worker_running_;
                });
                if (tcp_raw_record_queue_.empty() && !tcp_raw_recording_worker_running_)
                {
                    break;
                }

                record = std::move(tcp_raw_record_queue_.front());
                tcp_raw_record_queue_.pop_front();
                tcp_raw_record_queue_bytes_ -= static_cast<quint64>(record.payload.size());
            }

            if (writeUnifiedRawRecord(raw_tcp_wave_file_.get(),
                                      raw_tcp_wave_record_count_,
                                      kRawSourceTcpWave,
                                      kRawRecordTypeGeneric,
                                      record.flags,
                                      record.timestamp_us,
                                      record.payload.constData(),
                                      static_cast<size_t>(record.payload.size())))
            {
                appendTcpWavePeakIndexLine(record);
                waveform_frame_count_.fetch_add(1);
                waveform_file_count_.store(1);
                const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
                const qint64 lastStatusUpdateMs = last_tcp_recording_status_update_ms_.load();
                if (lastStatusUpdateMs <= 0 ||
                    nowMs - lastStatusUpdateMs >= kTcpRecordingStatusRefreshMs)
                {
                    last_tcp_recording_status_update_ms_.store(nowMs);
                    QMetaObject::invokeMethod(this, [this]() {
                        updateRecordingStatusLabel();
                    }, Qt::QueuedConnection);
                }
            }
        }
    });
}

void MainWindow::stopTcpRawRecordingWorker()
{
    {
        std::lock_guard<std::mutex> lock(tcp_raw_record_queue_mutex_);
        tcp_raw_recording_worker_running_ = false;
    }
    tcp_raw_record_queue_cv_.notify_all();

    if (tcp_raw_recording_thread_.joinable())
    {
        tcp_raw_recording_thread_.join();
    }

    quint64 droppedCount = 0;
    {
        std::lock_guard<std::mutex> lock(tcp_raw_record_queue_mutex_);
        droppedCount = tcp_raw_record_dropped_count_;
        tcp_raw_record_queue_.clear();
        tcp_raw_record_queue_bytes_ = 0;
        tcp_raw_record_dropped_count_ = 0;
        last_tcp_raw_queue_warning_ms_ = 0;
    }

    if (droppedCount > 0)
    {
        log(QString(is_english_
            ? "Warning: dropped %1 TCP raw frames because the recording queue was full."
            : "警告：TCP 原始记录队列已满，已丢弃 %1 帧。")
            .arg(droppedCount));
    }
}

bool MainWindow::enqueueTcpRawRecord(TcpRawRecord record)
{
    const quint64 payloadBytes = static_cast<quint64>(record.payload.size());
    QString warningMessage;
    bool enqueued = false;
    {
        std::lock_guard<std::mutex> lock(tcp_raw_record_queue_mutex_);
        if (!tcp_raw_recording_worker_running_)
        {
            return false;
        }

        const quint64 nextBytes = tcp_raw_record_queue_bytes_ + payloadBytes;
        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
        if (nextBytes > kTcpRawRecordQueueMaxBytes)
        {
            ++tcp_raw_record_dropped_count_;
            if (last_tcp_raw_queue_warning_ms_ <= 0 ||
                nowMs - last_tcp_raw_queue_warning_ms_ >= kTcpRawRecordQueueWarningIntervalMs)
            {
                last_tcp_raw_queue_warning_ms_ = nowMs;
                warningMessage = QString(is_english_
                    ? "TCP raw recording queue is full (%1 MiB). Dropping incoming raw frames to keep the TCP link responsive."
                    : "TCP 原始记录队列已满（%1 MiB）。为保持 TCP 链路响应，正在丢弃新到原始帧。")
                    .arg(tcp_raw_record_queue_bytes_ / (1024ULL * 1024ULL));
            }
        }
        else
        {
            tcp_raw_record_queue_bytes_ = nextBytes;
            tcp_raw_record_queue_.push_back(std::move(record));
            enqueued = true;
            if (nextBytes >= kTcpRawRecordQueueWarningBytes &&
                (last_tcp_raw_queue_warning_ms_ <= 0 ||
                 nowMs - last_tcp_raw_queue_warning_ms_ >= kTcpRawRecordQueueWarningIntervalMs))
            {
                last_tcp_raw_queue_warning_ms_ = nowMs;
                warningMessage = QString(is_english_
                    ? "TCP raw recording queue backlog is %1 MiB. Disk writes may be slower than the incoming stream."
                    : "TCP 原始记录队列积压 %1 MiB，磁盘写入可能慢于数据流。")
                    .arg(nextBytes / (1024ULL * 1024ULL));
            }
        }
    }

    if (!warningMessage.isEmpty())
    {
        QMetaObject::invokeMethod(this, [this, warningMessage]() {
            log(warningMessage);
        }, Qt::QueuedConnection);
    }

    if (enqueued)
    {
        tcp_raw_record_queue_cv_.notify_one();
    }
    return enqueued;
}

void MainWindow::closeUnifiedRawDatFiles()
{
    std::lock_guard<std::mutex> lock(recording_files_mutex_);
    for (QFile *file : {raw_epsilon_file_.get(),
                       raw_ptb_file_.get(),
                       raw_hmp_file_.get(),
                       raw_lidar_file_.get(),
                       raw_tcp_wave_file_.get(),
                       raw_tcp_wave_peak_index_file_.get()})
    {
        if (file && file->isOpen())
        {
            file->flush();
            file->close();
        }
    }
}

void MainWindow::resetUnifiedRawDatFiles()
{
    raw_epsilon_file_.reset();
    raw_ptb_file_.reset();
    raw_hmp_file_.reset();
    raw_lidar_file_.reset();
    raw_tcp_wave_file_.reset();
    raw_tcp_wave_peak_index_file_.reset();
}

void MainWindow::appendTcpWavePeakIndexLine(const TcpRawRecord& record)
{
    const TcpWavePeakSummary summary = summarizeTcpWavePeakRecordPayload(record.payload, record.flags);

    std::lock_guard<std::mutex> lock(recording_files_mutex_);
    if (!raw_tcp_wave_peak_index_file_ || !raw_tcp_wave_peak_index_file_->isOpen())
    {
        return;
    }

    QTextStream out(raw_tcp_wave_peak_index_file_.get());
    out.setEncoding(QStringConverter::Utf8);
    out << record.timestamp_us << ','
        << peakValueCsvText(summary.value) << ','
        << summary.index << ','
        << summary.point_count << ",0,0\n";
    out.flush();
}

void MainWindow::appendEventLogLine(const QString& level, const QString& message)
{
    std::lock_guard<std::mutex> lock(recording_files_mutex_);
    if (!event_log_file_ || !event_log_file_->isOpen())
    {
        return;
    }

    QTextStream out(event_log_file_.get());
    out.setEncoding(QStringConverter::Utf8);
    out << csvEscape(recordingTimestampUtc()) << ','
        << currentTimestampUs() << ','
        << csvEscape(level) << ','
        << csvEscape(message) << '\n';
    out.flush();
}

void MainWindow::appendErrorLogLine(const QString& message)
{
    std::lock_guard<std::mutex> lock(recording_files_mutex_);
    if (!error_log_file_ || !error_log_file_->isOpen())
    {
        return;
    }

    QTextStream out(error_log_file_.get());
    out.setEncoding(QStringConverter::Utf8);
    out << '[' << recordingTimestampUtc() << "] " << message << '\n';
    out.flush();
}

quint64 MainWindow::currentTimestampUs() const
{
    return static_cast<quint64>(QDateTime::currentDateTimeUtc().toMSecsSinceEpoch()) * 1000ULL;
}

quint64 MainWindow::steadyToEpochUs(const std::chrono::steady_clock::time_point& timePoint) const
{
    if (timePoint == std::chrono::steady_clock::time_point{})
    {
        return 0;
    }

    const auto delta = timePoint - steady_clock_anchor_;
    const auto systemPoint = system_clock_anchor_ + std::chrono::duration_cast<std::chrono::system_clock::duration>(delta);
    return static_cast<quint64>(std::chrono::duration_cast<std::chrono::microseconds>(systemPoint.time_since_epoch()).count());
}

bool MainWindow::writeSessionMetadata(const QString& endTimeUtc)
{
    if (session_metadata_filename_.isEmpty() || session_directory_.isEmpty())
    {
        log(is_english_
            ? "Warning: session metadata path is empty"
            : "警告：会话元数据路径为空");
        return false;
    }

    QDir sessionDir(session_directory_);
    QJsonObject root;
    root["session_name"] = session_name_;
    root["start_time_utc"] = session_start_time_utc_;
    root["start_time_us"] = QString::number(session_start_time_us_);
    root["end_time_utc"] = endTimeUtc;
    root["software_version"] = QCoreApplication::applicationVersion().isEmpty()
        ? QStringLiteral("dev")
        : QCoreApplication::applicationVersion();
    root["epsilon_schema_version"] = QStringLiteral("epsilon.v1");
    root["waveform_points_per_frame"] = 50000;
    root["sensor_export_rate_hz"] = recording_export_rate_hz_;
    root["other_devices_export_rate_hz"] = recording_export_rate_hz_;
    root["raw_export_mode"] = QStringLiteral("unified_raw_dat");
    root["raw_dat_format_version"] = static_cast<int>(kUnifiedRawFormatVersion);
    root["waveform_export_rate_hz"] = 0;
    root["waveform_export_mode"] = QStringLiteral("per_frame");
    root["waveform_value_type"] = QStringLiteral("float32");
    root["waveform_timestamp_type"] = QStringLiteral("uint64");
    root["timestamp_unit"] = QStringLiteral("microseconds");
    root["sensor_rows"] = QString::number(recording_entry_count_.load());
    root["waveform_frames"] = QString::number(waveform_frame_count_.load());
    root["waveform_file_count"] = QString::number(waveform_file_count_.load());

    QJsonObject rawFiles;
    auto addRawFile = [&rawFiles, &sessionDir](const QString& name,
                                               const QString& filename,
                                               quint16 sourceId,
                                               quint64 recordCount) {
        QJsonObject raw;
        raw["path"] = sessionDir.relativeFilePath(filename);
        raw["source_id"] = static_cast<int>(sourceId);
        raw["format_version"] = static_cast<int>(kUnifiedRawFormatVersion);
        raw["record_count"] = QString::number(recordCount);
        rawFiles[name] = raw;
    };
    addRawFile(QStringLiteral("epsilon"), raw_epsilon_filename_, kRawSourceEpsilon, raw_epsilon_record_count_.load());
    addRawFile(QStringLiteral("ptb"), raw_ptb_filename_, kRawSourcePtb, raw_ptb_record_count_.load());
    addRawFile(QStringLiteral("hmp"), raw_hmp_filename_, kRawSourceHmp, raw_hmp_record_count_.load());
    addRawFile(QStringLiteral("lidar"), raw_lidar_filename_, kRawSourceLidar, raw_lidar_record_count_.load());
    addRawFile(QStringLiteral("tcp_wave"), raw_tcp_wave_filename_, kRawSourceTcpWave, raw_tcp_wave_record_count_.load());
    root["raw_files"] = rawFiles;

    QJsonObject paths;
    paths["raw_directory"] = QStringLiteral("raw");
    paths["devices_csv"] = sessionDir.relativeFilePath(sensors_filename_);
    paths["waveform_peak_index"] = sessionDir.relativeFilePath(raw_tcp_wave_peak_index_filename_);
    paths["raw_format_doc"] = sessionDir.relativeFilePath(raw_dat_doc_filename_);
    paths["event_log"] = sessionDir.relativeFilePath(event_log_filename_);
    paths["error_log"] = sessionDir.relativeFilePath(error_log_filename_);
    paths["device_config"] = sessionDir.relativeFilePath(device_config_filename_);
    root["paths"] = paths;

    QString error;
    if (!writeJsonFileAtomically(session_metadata_filename_, root, &error))
    {
        log(QString(is_english_
            ? "Warning: failed to save session metadata %1: %2"
            : "警告：保存会话元数据失败 %1：%2")
            .arg(session_metadata_filename_, error));
        return false;
    }
    return true;
}

bool MainWindow::writeDeviceConfigSnapshot()
{
    if (device_config_filename_.isEmpty())
    {
        log(is_english_
            ? "Warning: device configuration snapshot path is empty"
            : "警告：设备配置快照路径为空");
        return false;
    }

    QJsonObject root;
    root["recording_directory"] = recording_directory_;
    root["session_directory"] = session_directory_;
    root["epsilon_schema_version"] = QStringLiteral("epsilon.v1");
    root["sensor_export_rate_hz"] = recording_export_rate_hz_;
    root["other_devices_export_rate_hz"] = recording_export_rate_hz_;
    root["raw_export_mode"] = QStringLiteral("unified_raw_dat");
    root["raw_dat_format_version"] = static_cast<int>(kUnifiedRawFormatVersion);
    root["waveform_export_rate_hz"] = 0;
    root["waveform_export_mode"] = QStringLiteral("per_frame");

    QJsonObject waveform;
    waveform["host"] = tcp_wave_panel_ ? tcp_wave_panel_->host() : QStringLiteral("127.0.0.1");
    waveform["port"] = tcp_wave_panel_ ? tcp_wave_panel_->port() : 8888;
    waveform["frame_rate_hz"] = 0;
    waveform["frame_rate_mode"] = QStringLiteral("per_frame");
    waveform["points_per_frame"] = 50000;
    waveform["value_type"] = QStringLiteral("float32");
    waveform["timestamp_type"] = QStringLiteral("uint64");
    root["waveform"] = waveform;

    QJsonObject raw;
    raw["directory"] = QStringLiteral("raw");
    raw["format_doc"] = QStringLiteral("raw_dat_format.md");
    raw["mode"] = QStringLiteral("per_verified_raw_frame_or_response");
    root["raw_dat"] = raw;

    QJsonObject sensors;
    auto addSerialConfig = [&sensors](const QString& name, QComboBox* port, QComboBox* baud, QComboBox* rate) {
        QJsonObject obj;
        obj["port"] = port ? port->currentText() : QString();
        obj["baud"] = baud ? baud->currentText() : QString();
        obj["rate_hz"] = rate ? rate->currentText() : QString();
        sensors[name] = obj;
    };
    addSerialConfig("epsilon", epsilon_port_combo_, epsilon_baud_combo_, nullptr);
    addSerialConfig("ptb", ptb_port_combo_, ptb_baud_combo_, ptb_rate_combo_);
    addSerialConfig("hmp", hmp_port_combo_, hmp_baud_combo_, hmp_rate_combo_);
    addSerialConfig("lidar", lidar_port_combo_, lidar_baud_combo_, lidar_rate_combo_);
    addSerialConfig("rd105", temperature_port_combo_, temperature_baud_combo_, temperature_rate_combo_);
    root["sensors"] = sensors;

    QString error;
    if (!writeJsonFileAtomically(device_config_filename_, root, &error))
    {
        log(QString(is_english_
            ? "Warning: failed to save device configuration snapshot %1: %2"
            : "警告：保存设备配置快照失败 %1：%2")
            .arg(device_config_filename_, error));
        return false;
    }
    return true;
}

void MainWindow::startRecordingWorkers()
{
    if (recording_thread_running_.load())
    {
        return;
    }

    recording_paused_ = false;
    last_imu_record_timestamp_us_.store(0);
    startTcpRawRecordingWorker();

    QFile *filePtr = sensors_file_.get();
    recording_thread_running_.store(true);
    recording_thread_ = std::thread([this, filePtr]() {
        const int exportRateHz = std::max(1, recording_export_rate_hz_);
        const auto exportPeriod = std::chrono::microseconds(1000000 / exportRateHz);
        auto nextTick = std::chrono::steady_clock::now();
        while (recording_thread_running_.load())
        {
            const auto tickTime = std::chrono::steady_clock::now();
            const quint64 recordTimestampUs = currentTimestampUs();
            const CollectorSnapshot collectors = snapshotCollectors();
            const VaporView::EpsilonData epsilonSample = collectors.epsilon ? collectors.epsilon->getLatestData() : VaporView::EpsilonData();
            const VaporView::PtbData ptbSample = collectors.ptb ? collectors.ptb->getLatestData() : VaporView::PtbData();
            const VaporView::HmpData hmpSample = collectors.hmp ? collectors.hmp->getLatestData() : VaporView::HmpData();
            const VaporView::LidarData lidarSample = collectors.lidar ? collectors.lidar->getLatestData() : VaporView::LidarData();

            QStringList row;
            row.reserve(72);
            row << QString::number(recordTimestampUs);

            auto appendEmptyColumns = [&row](int count) {
                for (int i = 0; i < count; ++i)
                {
                    row << QString();
                }
            };
            auto appendBool = [&row](bool value) {
                row << csvBool(value);
            };
            auto isFresh = [tickTime](auto* collector, const auto& sample) {
                if (!collector || sample.timestamp == std::chrono::steady_clock::time_point{})
                {
                    return false;
                }
                const int rate = std::max(1, collector->getSampleRate());
                const int timeoutMs = std::max(250, static_cast<int>(std::ceil(3000.0 / rate)));
                const auto ageMs = std::chrono::duration_cast<std::chrono::milliseconds>(tickTime - sample.timestamp).count();
                return ageMs >= 0 && ageMs <= timeoutMs;
            };

            if (isFresh(collectors.epsilon.get(), epsilonSample))
            {
                row
                    << QString::number(steadyToEpochUs(epsilonSample.timestamp))
                    << QString::number(epsilonSample.device_timestamp_us)
                    << QString::number(epsilonSample.utc_unix_s)
                    << QString::number(epsilonSample.utc_microseconds)
                    << QString::number(epsilonSample.latitude_deg, 'f', 9)
                    << QString::number(epsilonSample.longitude_deg, 'f', 9)
                    << QString::number(epsilonSample.height_m, 'f', 6)
                    << QString::number(epsilonSample.ecef_x_m, 'f', 6)
                    << QString::number(epsilonSample.ecef_y_m, 'f', 6)
                    << QString::number(epsilonSample.ecef_z_m, 'f', 6)
                    << QString::number(epsilonSample.ned_n_m, 'f', 6)
                    << QString::number(epsilonSample.ned_e_m, 'f', 6)
                    << QString::number(epsilonSample.ned_d_m, 'f', 6)
                    << QString::number(epsilonSample.vel_n_mps, 'f', 6)
                    << QString::number(epsilonSample.vel_e_mps, 'f', 6)
                    << QString::number(epsilonSample.vel_d_mps, 'f', 6)
                    << QString::number(epsilonSample.body_vel_x_mps, 'f', 6)
                    << QString::number(epsilonSample.body_vel_y_mps, 'f', 6)
                    << QString::number(epsilonSample.body_vel_z_mps, 'f', 6)
                    << QString::number(epsilonSample.body_acc_x_mps2, 'f', 6)
                    << QString::number(epsilonSample.body_acc_y_mps2, 'f', 6)
                    << QString::number(epsilonSample.body_acc_z_mps2, 'f', 6)
                    << QString::number(epsilonSample.roll_deg, 'f', 6)
                    << QString::number(epsilonSample.pitch_deg, 'f', 6)
                    << QString::number(epsilonSample.yaw_deg, 'f', 6)
                    << QString::number(epsilonSample.quat_w, 'f', 8)
                    << QString::number(epsilonSample.quat_x, 'f', 8)
                    << QString::number(epsilonSample.quat_y, 'f', 8)
                    << QString::number(epsilonSample.quat_z, 'f', 8)
                    << QString::number(epsilonSample.ang_vel_x_radps, 'f', 8)
                    << QString::number(epsilonSample.ang_vel_y_radps, 'f', 8)
                    << QString::number(epsilonSample.ang_vel_z_radps, 'f', 8)
                    << QString::number(epsilonSample.imu_acc_x_mps2, 'f', 6)
                    << QString::number(epsilonSample.imu_acc_y_mps2, 'f', 6)
                    << QString::number(epsilonSample.imu_acc_z_mps2, 'f', 6)
                    << QString::number(epsilonSample.imu_gyr_x_radps, 'f', 8)
                    << QString::number(epsilonSample.imu_gyr_y_radps, 'f', 8)
                    << QString::number(epsilonSample.imu_gyr_z_radps, 'f', 8)
                    << QString::number(epsilonSample.mag_x_mg, 'f', 6)
                    << QString::number(epsilonSample.mag_y_mg, 'f', 6)
                    << QString::number(epsilonSample.mag_z_mg, 'f', 6)
                    << QString::fromStdString(epsilonSample.gnss_fix_text)
                    << QString::number(epsilonSample.gnss_satellites)
                    << QString::number(epsilonSample.hdop, 'f', 4)
                    << QString::number(epsilonSample.vdop, 'f', 4)
                    << QString::number(epsilonSample.hacc_m, 'f', 4)
                    << QString::number(epsilonSample.vacc_m, 'f', 4)
                    << QString::number(epsilonSample.lat_std_m, 'f', 4)
                    << QString::number(epsilonSample.lon_std_m, 'f', 4)
                    << QString::number(epsilonSample.height_std_m, 'f', 4)
                    << (std::isfinite(epsilonSample.diff_age_s) ? QString::number(epsilonSample.diff_age_s, 'f', 4) : QString())
                    << csvBool(epsilonSample.heading_valid)
                    << QString::number(epsilonSample.system_status_bits)
                    << QString::number(epsilonSample.filter_status_bits)
                    << QString::number(epsilonSample.update_status_bits)
                    << QString::number(epsilonSample.imu_packet_rate_hz, 'f', 4)
                    << QString::number(epsilonSample.ahrs_packet_rate_hz, 'f', 4)
                    << QString::number(epsilonSample.insgps_packet_rate_hz, 'f', 4)
                    << QString::number(epsilonSample.sys_state_packet_rate_hz, 'f', 4)
                    << QString::number(epsilonSample.raw_gnss_packet_rate_hz, 'f', 4)
                    << QString::number(epsilonSample.satellite_packet_rate_hz, 'f', 4)
                    << QString::number(epsilonSample.geodetic_packet_rate_hz, 'f', 4)
                    << QString::number(epsilonSample.ecef_packet_rate_hz, 'f', 4);
                appendBool(epsilonSample.valid);
                row << QString::fromStdString(epsilonSample.error_message);
            }
            else
            {
                appendEmptyColumns(65);
            }

            if (isFresh(collectors.hmp.get(), hmpSample))
            {
                row
                    << QString::number(hmpSample.temperature, 'f', 6)
                    << QString::number(hmpSample.humidity, 'f', 6);
            }
            else
            {
                appendEmptyColumns(2);
            }

            if (isFresh(collectors.ptb.get(), ptbSample))
            {
                row << QString::number(ptbSample.pressure_hpa, 'f', 6);
            }
            else
            {
                appendEmptyColumns(1);
            }

            if (isFresh(collectors.lidar.get(), lidarSample))
            {
                row
                    << QString::number(lidarSample.distance_m, 'f', 6)
                    << QString::number(lidarSample.signal_strength);
                appendBool(lidarSample.valid);
            }
            else
            {
                appendEmptyColumns(3);
            }

            {
                std::lock_guard<std::mutex> lock(recording_files_mutex_);
                QTextStream out(filePtr);
                out.setEncoding(QStringConverter::Utf8);
                for (int i = 0; i < row.size(); ++i)
                {
                    if (i > 0)
                    {
                        out << ',';
                    }
                    out << csvEscape(row.at(i));
                }
                out << '\n';
                out.flush();
            }

            recording_entry_count_.fetch_add(1);
            QMetaObject::invokeMethod(this, [this]() {
                updateRecordingStatusLabel();
            }, Qt::QueuedConnection);

            nextTick += exportPeriod;
            std::this_thread::sleep_until(nextTick);
        }
    });
}

void MainWindow::stopRecordingWorkers()
{
    recording_thread_running_.store(false);
    if (recording_thread_.joinable())
    {
        recording_thread_.join();
    }
    stopTcpRawRecordingWorker();
}

bool MainWindow::startRecordingSession()
{
    if (sensors_file_ && sensors_file_->isOpen())
    {
        if (!recording_paused_)
        {
            return true;
        }

        startRecordingWorkers();
        updateRecordingStatusLabel();
        log(QString(is_english_ ? "Resumed recording session: %1" : "已继续记录会话: %1").arg(session_directory_));
        return true;
    }

    QString recordsPath = recording_directory_.trimmed();
    if (recordsPath.isEmpty())
    {
        recordsPath = defaultRecordingDirectory();
        recording_directory_ = recordsPath;
    }

    const QString sessionName = QStringLiteral("session_%1").arg(recordingSessionDirectoryTimestamp());
    if (!prepareRecordingSessionLayout(recordsPath, sessionName))
    {
        QMessageBox::warning(
            this,
            is_english_ ? "Error" : "错误",
            is_english_ ? "Failed to create session directories" : "无法创建会话目录结构");
        return false;
    }

    sensors_file_ = std::make_unique<QFile>(sensors_filename_);
    event_log_file_ = std::make_unique<QFile>(event_log_filename_);
    error_log_file_ = std::make_unique<QFile>(error_log_filename_);
    raw_tcp_wave_peak_index_file_ = std::make_unique<QFile>(raw_tcp_wave_peak_index_filename_);
    if (!sensors_file_->open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate) ||
        !event_log_file_->open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate) ||
        !error_log_file_->open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate) ||
        !raw_tcp_wave_peak_index_file_->open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate) ||
        !openUnifiedRawDatFile(raw_epsilon_file_, raw_epsilon_filename_, kRawSourceEpsilon) ||
        !openUnifiedRawDatFile(raw_ptb_file_, raw_ptb_filename_, kRawSourcePtb) ||
        !openUnifiedRawDatFile(raw_hmp_file_, raw_hmp_filename_, kRawSourceHmp) ||
        !openUnifiedRawDatFile(raw_lidar_file_, raw_lidar_filename_, kRawSourceLidar) ||
        !openUnifiedRawDatFile(raw_tcp_wave_file_, raw_tcp_wave_filename_, kRawSourceTcpWave))
    {
        sensors_file_.reset();
        resetUnifiedRawDatFiles();
        event_log_file_.reset();
        error_log_file_.reset();
        QMessageBox::warning(
            this,
            is_english_ ? "Error" : "错误",
            is_english_ ? "Failed to open session files for writing" : "无法打开会话文件进行写入");
        return false;
    }

    session_start_time_utc_ = recordingTimestampUtc();
    session_start_time_us_ = currentTimestampUs();
    recording_entry_count_.store(0);
    waveform_frame_count_.store(0);
    waveform_file_count_.store(0);
    raw_epsilon_record_count_.store(0);
    raw_ptb_record_count_.store(0);
    raw_hmp_record_count_.store(0);
    raw_lidar_record_count_.store(0);
    raw_tcp_wave_record_count_.store(0);
    last_tcp_recording_status_update_ms_.store(0);
    {
        QTextStream eventOut(event_log_file_.get());
        eventOut.setEncoding(QStringConverter::Utf8);
        eventOut << "timestamp_utc,timestamp_us,level,message\n";
        eventOut.flush();
    }
    {
        QTextStream peakOut(raw_tcp_wave_peak_index_file_.get());
        peakOut.setEncoding(QStringConverter::Utf8);
        peakOut << kTcpWavePeakIndexCsvHeader;
        peakOut.flush();
    }

    writeSensorsHeader();
    if (!copyRawDatFormatDocumentToSession())
    {
        log(QString(is_english_
            ? "Warning: failed to copy unified raw DAT format document into session folder"
            : "警告：未能将统一 raw DAT 格式说明复制到当前会话目录"));
    }
    if (!writeSessionMetadata())
    {
        closeUnifiedRawDatFiles();
        if (sensors_file_ && sensors_file_->isOpen()) sensors_file_->close();
        if (event_log_file_ && event_log_file_->isOpen()) event_log_file_->close();
        if (error_log_file_ && error_log_file_->isOpen()) error_log_file_->close();
        sensors_file_.reset();
        resetUnifiedRawDatFiles();
        event_log_file_.reset();
        error_log_file_.reset();
        QMessageBox::warning(
            this,
            is_english_ ? "Error" : "错误",
            is_english_ ? "Failed to save session metadata" : "无法保存会话元数据");
        return false;
    }
    writeDeviceConfigSnapshot();
    startRecordingWorkers();
    updateRecordingStatusLabel();
    log(QString(is_english_ ? "Started recording session: %1" : "已开始记录会话: %1").arg(session_directory_));
    return true;
}

void MainWindow::onChooseRecordingDirectoryClicked()
{
    const QString currentDirectory = recording_directory_.isEmpty() ? defaultRecordingDirectory() : recording_directory_;
    const QString selectedDirectory = QFileDialog::getExistingDirectory(
        this,
        is_english_ ? "Select Recording Folder" : "选择记录目录",
        currentDirectory,
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (selectedDirectory.isEmpty())
    {
        return;
    }

    recording_directory_ = QDir::fromNativeSeparators(selectedDirectory);
    QSettings settings("VaporView", "MainWindow");
    settings.setValue("recording_directory", recording_directory_);
    log(QString(is_english_ ? "Recording folder set to: %1" : "记录目录已设置为: %1").arg(recording_directory_));
}

QString MainWindow::formatScheduledDateTime(const QDateTime& dateTime) const
{
    return dateTime.isValid()
        ? dateTime.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))
        : QStringLiteral("--");
}

QString MainWindow::formatScheduledDuration(int seconds) const
{
    seconds = std::max(0, seconds);
    const int hours = seconds / 3600;
    const int minutes = (seconds % 3600) / 60;
    const int secs = seconds % 60;
    if (is_english_)
    {
        return QStringLiteral("%1h %2m %3s").arg(hours).arg(minutes).arg(secs);
    }
    return QStringLiteral("%1小时%2分%3秒").arg(hours).arg(minutes).arg(secs);
}

QString MainWindow::scheduledRecordingSummary() const
{
    if (scheduled_recording_mode_ == ScheduledRecordingMode::None)
    {
        return is_english_ ? QStringLiteral("Configure scheduled recording")
                           : QStringLiteral("配置定时记录");
    }

    const QString runText = scheduled_recording_fixed_count_enabled_
        ? (is_english_
              ? QStringLiteral("%1 / %2 rounds").arg(scheduled_recording_completed_runs_).arg(scheduled_recording_total_runs_)
              : QStringLiteral("已完成 %1 / %2 次").arg(scheduled_recording_completed_runs_).arg(scheduled_recording_total_runs_))
        : (is_english_ ? QStringLiteral("loop until canceled") : QStringLiteral("循环直到取消"));
    const QString duration = formatScheduledDuration(scheduled_recording_duration_seconds_);

    if (scheduled_recording_phase_ == ScheduledRecordingPhase::Recording)
    {
        return is_english_
            ? QStringLiteral("Scheduled recording: recording until %1, duration %2, %3")
                  .arg(formatScheduledDateTime(scheduled_recording_stop_time_), duration, runText)
            : QStringLiteral("定时记录：记录中，计划 %1 停止，时长 %2，%3")
                  .arg(formatScheduledDateTime(scheduled_recording_stop_time_), duration, runText);
    }

    if (scheduled_recording_mode_ == ScheduledRecordingMode::FixedTime)
    {
        return is_english_
            ? QStringLiteral("Scheduled recording: starts at %1, duration %2")
                  .arg(formatScheduledDateTime(scheduled_recording_next_start_), duration)
            : QStringLiteral("定时记录：%1 开始，记录 %2")
                  .arg(formatScheduledDateTime(scheduled_recording_next_start_), duration);
    }

    return is_english_
        ? QStringLiteral("Scheduled recording: next start %1, duration %2, interval %3, %4")
              .arg(formatScheduledDateTime(scheduled_recording_next_start_),
                   duration,
                   formatScheduledDuration(scheduled_recording_interval_seconds_),
                   runText)
        : QStringLiteral("定时记录：下次 %1 开始，记录 %2，间隔 %3，%4")
              .arg(formatScheduledDateTime(scheduled_recording_next_start_),
                   duration,
                   formatScheduledDuration(scheduled_recording_interval_seconds_),
                   runText);
}

QString MainWindow::scheduledRecordingStatusLine() const
{
    if (scheduled_recording_mode_ == ScheduledRecordingMode::None)
    {
        return QString();
    }

    const QString countText = scheduled_recording_fixed_count_enabled_
        ? (is_english_
              ? QStringLiteral(" (%1/%2)").arg(scheduled_recording_completed_runs_).arg(scheduled_recording_total_runs_)
              : QStringLiteral("（%1/%2）").arg(scheduled_recording_completed_runs_).arg(scheduled_recording_total_runs_))
        : QString();

    if (scheduled_recording_phase_ == ScheduledRecordingPhase::Recording)
    {
        return is_english_
            ? QStringLiteral("Schedule: recording, stops at %1%2")
                  .arg(formatScheduledDateTime(scheduled_recording_stop_time_), countText)
            : QStringLiteral("定时：记录中，%1 停止%2")
                  .arg(formatScheduledDateTime(scheduled_recording_stop_time_), countText);
    }

    return is_english_
        ? QStringLiteral("Schedule: next start %1%2")
              .arg(formatScheduledDateTime(scheduled_recording_next_start_), countText)
        : QStringLiteral("定时：下次 %1 开始%2")
              .arg(formatScheduledDateTime(scheduled_recording_next_start_), countText);
}

void MainWindow::updateScheduledRecordingAction()
{
    if (!scheduled_recording_action_)
    {
        return;
    }

    const bool active = scheduled_recording_mode_ != ScheduledRecordingMode::None;
    scheduled_recording_action_->setText(is_english_ ? "Scheduled Recording" : "定时记录");
    scheduled_recording_action_->setToolTip(active ? scheduledRecordingSummary()
                                                   : (is_english_ ? QStringLiteral("Configure scheduled recording")
                                                                  : QStringLiteral("配置定时记录")));
    scheduled_recording_action_->setStatusTip(scheduled_recording_action_->toolTip());
    scheduled_recording_action_->setCheckable(false);
    scheduled_recording_action_->setChecked(false);
}

void MainWindow::configureScheduledRecording(ScheduledRecordingMode mode,
                                             int durationSeconds,
                                             int intervalSeconds,
                                             bool fixedCountEnabled,
                                             int totalRuns,
                                             const QDateTime& firstStartTime)
{
    if (mode == ScheduledRecordingMode::None)
    {
        cancelScheduledRecording(false);
        return;
    }

    scheduled_recording_mode_ = mode;
    scheduled_recording_phase_ = ScheduledRecordingPhase::WaitingToStart;
    scheduled_recording_duration_seconds_ = std::max(1, durationSeconds);
    scheduled_recording_interval_seconds_ = std::max(1, intervalSeconds);
    scheduled_recording_fixed_count_enabled_ = fixedCountEnabled;
    scheduled_recording_total_runs_ = std::clamp(totalRuns, 1, 999);
    scheduled_recording_completed_runs_ = 0;
    scheduled_recording_next_start_ = firstStartTime.isValid() ? firstStartTime : QDateTime::currentDateTime();
    scheduled_recording_stop_time_ = QDateTime();
    scheduled_recording_round_observed_session_ = false;

    if (scheduled_recording_timer_ && !scheduled_recording_timer_->isActive())
    {
        scheduled_recording_timer_->start();
    }

    log(QString(is_english_ ? "Scheduled recording configured: %1"
                            : "定时记录已配置：%1")
            .arg(scheduledRecordingSummary()));
    updateScheduledRecordingAction();
    updateRecordingStatusLabel();
    onScheduledRecordingTick();
}

void MainWindow::cancelScheduledRecording(bool announce)
{
    const bool wasActive = scheduled_recording_mode_ != ScheduledRecordingMode::None;
    scheduled_recording_mode_ = ScheduledRecordingMode::None;
    scheduled_recording_phase_ = ScheduledRecordingPhase::Idle;
    scheduled_recording_next_start_ = QDateTime();
    scheduled_recording_stop_time_ = QDateTime();
    scheduled_recording_completed_runs_ = 0;
    scheduled_recording_round_observed_session_ = false;

    if (scheduled_recording_timer_)
    {
        scheduled_recording_timer_->stop();
    }

    if (announce && wasActive)
    {
        log(is_english_ ? "Scheduled recording canceled" : "定时记录已取消");
    }
    updateScheduledRecordingAction();
    updateRecordingStatusLabel();
}

void MainWindow::scheduleNextIntervalRecording(const QDateTime& fromTime)
{
    scheduled_recording_phase_ = ScheduledRecordingPhase::WaitingToStart;
    scheduled_recording_next_start_ = fromTime.addSecs(std::max(1, scheduled_recording_interval_seconds_));
    scheduled_recording_stop_time_ = QDateTime();
    scheduled_recording_round_observed_session_ = false;
}

void MainWindow::completeScheduledRecordingRound(bool counted)
{
    if (counted)
    {
        ++scheduled_recording_completed_runs_;
    }

    const bool fixedDone = scheduled_recording_fixed_count_enabled_ &&
                           scheduled_recording_completed_runs_ >= scheduled_recording_total_runs_;
    if (scheduled_recording_mode_ == ScheduledRecordingMode::FixedTime || fixedDone)
    {
        log(QString(is_english_ ? "Scheduled recording completed: %1"
                                : "定时记录已完成：%1")
                .arg(scheduledRecordingSummary()));
        cancelScheduledRecording(false);
        return;
    }

    scheduleNextIntervalRecording(QDateTime::currentDateTime());
    log(QString(is_english_ ? "Scheduled recording next start: %1"
                            : "定时记录下次开始：%1")
            .arg(formatScheduledDateTime(scheduled_recording_next_start_)));
    updateScheduledRecordingAction();
    updateRecordingStatusLabel();
}

bool MainWindow::canStartScheduledRecordingNow() const
{
    return scheduledRecordingStartBlockReason().isEmpty();
}

QString MainWindow::scheduledRecordingStartBlockReason() const
{
    if (isRemoteSkyMode())
    {
        if (!ground_telemetry_service_)
        {
            return is_english_
                ? QStringLiteral("Remote Sky telemetry service is not initialized.")
                : QStringLiteral("天空端数传服务未初始化。");
        }
        if (!ground_telemetry_service_->isOpen())
        {
            return is_english_
                ? QStringLiteral("Remote Sky telemetry is not connected.")
                : QStringLiteral("天空端数传未连接。");
        }
        if (remote_recording_state_ == 1)
        {
            const QString sessionName = has_last_remote_recording_status_ &&
                                            !last_remote_recording_status_.session_name.isEmpty()
                                        ? last_remote_recording_status_.session_name
                                        : QString();
            if (!sessionName.isEmpty())
            {
                return is_english_
                    ? QStringLiteral("Remote Sky is already recording session %1.").arg(sessionName)
                    : QStringLiteral("天空端已经在记录中，会话：%1。").arg(sessionName);
            }
            return is_english_
                ? QStringLiteral("Remote Sky is already recording.")
                : QStringLiteral("天空端已经在记录中。");
        }
        return QString();
    }

    const bool tcpConnected = tcp_wave_panel_ && tcp_wave_panel_->isConnected();
    const bool recordingSourceAvailable = is_connected_ || tcpConnected;
    if (!recordingSourceAvailable)
    {
        return is_english_
            ? QStringLiteral("No local recording source is connected.")
            : QStringLiteral("本地记录源未连接。");
    }
    if (connection_attempt_in_progress_)
    {
        return is_english_
            ? QStringLiteral("A connection attempt is still in progress.")
            : QStringLiteral("连接流程正在进行。");
    }
    if (port_detection_in_progress_)
    {
        return is_english_
            ? QStringLiteral("Serial-port auto detection is still in progress.")
            : QStringLiteral("串口自动识别正在进行。");
    }
    if (epsilon_reconfigure_in_progress_)
    {
        return is_english_
            ? QStringLiteral("EPSILON reconfiguration is still in progress.")
            : QStringLiteral("EPSILON 配置流程正在进行。");
    }
    if (sensors_file_ && sensors_file_->isOpen() && !recording_paused_)
    {
        return is_english_
            ? QStringLiteral("A local recording session is already running.")
            : QStringLiteral("本地记录已经在进行中。");
    }
    return QString();
}

bool MainWindow::scheduledRecordingSessionOpen() const
{
    if (isRemoteSkyMode())
    {
        return remote_recording_state_ == 1 || remote_recording_state_ == 2;
    }
    return sensors_file_ && sensors_file_->isOpen();
}

bool MainWindow::tryStartScheduledRecording(QString *failureReason)
{
    const QString blockReason = scheduledRecordingStartBlockReason();
    if (!blockReason.isEmpty())
    {
        if (failureReason)
        {
            *failureReason = blockReason;
        }
        return false;
    }

    if (isRemoteSkyMode())
    {
        const quint16 seq = ground_telemetry_service_
            ? ground_telemetry_service_->sendCommand(VaporView::CommandId::StartRecording)
            : 0;
        if (seq != 0)
        {
            log(is_english_ ? "Scheduled recording start command sent"
                            : "定时记录开始命令已发送");
        }
        else if (failureReason)
        {
            *failureReason = is_english_
                ? QStringLiteral("Failed to send the remote start command.")
                : QStringLiteral("远程开始记录命令发送失败。");
        }
        return seq != 0;
    }

    const bool started = startRecordingSession() && sensors_file_ && sensors_file_->isOpen() && !recording_paused_;
    if (!started && failureReason)
    {
        *failureReason = is_english_
            ? QStringLiteral("Local recording session failed to open.")
            : QStringLiteral("本地记录会话打开失败。");
    }
    return started;
}

bool MainWindow::tryStopScheduledRecording()
{
    if (isRemoteSkyMode())
    {
        if (!ground_telemetry_service_ || !ground_telemetry_service_->isOpen())
        {
            return false;
        }
        const quint16 seq = ground_telemetry_service_->sendCommand(VaporView::CommandId::StopRecording);
        if (seq != 0)
        {
            log(is_english_ ? "Scheduled recording stop command sent"
                            : "定时记录停止命令已发送");
        }
        return seq != 0;
    }

    if (sensors_file_ && sensors_file_->isOpen())
    {
        stopRecording(true);
    }
    return true;
}

void MainWindow::onScheduledRecordingTick()
{
    if (scheduled_recording_mode_ == ScheduledRecordingMode::None)
    {
        return;
    }

    const QDateTime now = QDateTime::currentDateTime();
    if (scheduled_recording_phase_ == ScheduledRecordingPhase::WaitingToStart)
    {
        if (!scheduled_recording_next_start_.isValid() || now < scheduled_recording_next_start_)
        {
            updateScheduledRecordingAction();
            return;
        }

        QString failureReason;
        if (tryStartScheduledRecording(&failureReason))
        {
            scheduled_recording_phase_ = ScheduledRecordingPhase::Recording;
            scheduled_recording_stop_time_ = now.addSecs(std::max(1, scheduled_recording_duration_seconds_));
            scheduled_recording_round_observed_session_ = scheduledRecordingSessionOpen();
            updateScheduledRecordingAction();
            updateRecordingStatusLabel();
            return;
        }

        log(QString(is_english_
                ? "Scheduled recording could not start: %1"
                : "定时记录未能启动：%1")
                .arg(failureReason.isEmpty()
                         ? (is_english_ ? QStringLiteral("Unknown reason.")
                                        : QStringLiteral("未知原因。"))
                         : failureReason));
        if (scheduled_recording_mode_ == ScheduledRecordingMode::FixedTime)
        {
            cancelScheduledRecording(false);
            return;
        }

        scheduleNextIntervalRecording(now);
        updateScheduledRecordingAction();
        updateRecordingStatusLabel();
        return;
    }

    if (scheduled_recording_phase_ != ScheduledRecordingPhase::Recording)
    {
        return;
    }

    if (scheduledRecordingSessionOpen())
    {
        scheduled_recording_round_observed_session_ = true;
    }
    else if (scheduled_recording_round_observed_session_)
    {
        completeScheduledRecordingRound(true);
        return;
    }

    if (scheduled_recording_stop_time_.isValid() && now >= scheduled_recording_stop_time_)
    {
        if (tryStopScheduledRecording())
        {
            completeScheduledRecordingRound(true);
        }
        else
        {
            log(is_english_
                    ? "Scheduled recording could not stop because the recording link is unavailable."
                    : "定时记录未能停止：当前记录链路不可用。");
        }
    }

    updateScheduledRecordingAction();
    updateRecordingStatusLabel();
}

void MainWindow::onScheduledRecordingClicked()
{
    QDialog dialog(this);
    dialog.setWindowTitle(is_english_ ? QStringLiteral("Scheduled Recording")
                                      : QStringLiteral("定时记录"));

    auto *dialogLayout = new QVBoxLayout(&dialog);
    dialogLayout->setContentsMargins(0, 0, 0, 0);
    dialogLayout->setSpacing(0);

    auto *bodyWidget = new QWidget(&dialog);
    auto *rootLayout = new QVBoxLayout(bodyWidget);
    rootLayout->setContentsMargins(16, 14, 16, 14);
    rootLayout->setSpacing(12);
    dialogLayout->addWidget(bodyWidget);

    if (scheduled_recording_mode_ != ScheduledRecordingMode::None)
    {
        auto *summaryLabel = new QLabel(scheduledRecordingSummary(), bodyWidget);
        summaryLabel->setWordWrap(true);
        summaryLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        rootLayout->addWidget(summaryLabel);
    }

    const int scheduledLabelWidth = scalePixels(is_english_ ? 142 : 84);
    auto createScheduledRowLabel = [scheduledLabelWidth](const QString& text, QWidget *parent) {
        auto *label = new QLabel(text, parent);
        label->setFixedWidth(scheduledLabelWidth);
        label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        label->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        return label;
    };
    auto createScheduledRow = [&createScheduledRowLabel](QWidget *parent,
                                                         const QString& labelText,
                                                         QWidget *field,
                                                         bool expandField = true) {
        auto *row = new QWidget(parent);
        auto *layout = new QHBoxLayout(row);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(8);
        layout->addWidget(createScheduledRowLabel(labelText, row), 0, Qt::AlignVCenter);
        if (expandField)
        {
            QSizePolicy policy = field->sizePolicy();
            policy.setHorizontalPolicy(QSizePolicy::Expanding);
            field->setSizePolicy(policy);
            layout->addWidget(field, 1);
        }
        else
        {
            layout->addWidget(field, 0, Qt::AlignLeft | Qt::AlignVCenter);
            layout->addStretch(1);
        }
        return row;
    };

    auto *modeCombo = new QComboBox(bodyWidget);
    modeCombo->addItem(is_english_ ? QStringLiteral("Interval schedule") : QStringLiteral("周期执行"));
    modeCombo->addItem(is_english_ ? QStringLiteral("Start at local time") : QStringLiteral("指定时间点"));
    modeCombo->setMaxVisibleItems(2);
    for (int i = 0; i < modeCombo->count(); ++i)
    {
        modeCombo->setItemData(i, QSize(0, scalePixels(42)), Qt::SizeHintRole);
    }
    auto applyModeComboPopupStyle = [this, modeCombo]() {
        configureComboPopup(modeCombo);
    };
    applyModeComboPopupStyle();
    rootLayout->addWidget(createScheduledRow(bodyWidget,
                                             is_english_ ? QStringLiteral("Mode:") : QStringLiteral("模式:"),
                                             modeCombo));

    auto makeScheduledSpinStyle = [this]() {
        const QString hoverColor = appThemeColorName(AppThemeColor::Border, dark_theme_enabled_);
        const QString pressedColor = appThemeColorName(AppThemeColor::PopupHighlightPressed, dark_theme_enabled_);
        return QStringLiteral(
            "QSpinBox::up-button:hover, QSpinBox::down-button:hover, QDateTimeEdit::up-button:hover, QDateTimeEdit::down-button:hover { background-color: %1; }"
            "QSpinBox::up-button:pressed, QSpinBox::down-button:pressed, QDateTimeEdit::up-button:pressed, QDateTimeEdit::down-button:pressed { background-color: %2; }"
            "QSpinBox::up-button:disabled, QSpinBox::down-button:disabled, QDateTimeEdit::up-button:disabled, QDateTimeEdit::down-button:disabled { background-color: transparent; }")
                .arg(hoverColor, pressedColor);
    };
    const QString scheduledSpinStyle = makeScheduledSpinStyle();
    auto createDurationInput = [this, scheduledSpinStyle](QWidget *parent, QSpinBox *&hours, QSpinBox *&minutes, QSpinBox *&seconds) {
        auto *widget = new QWidget(parent);
        auto *layout = new QHBoxLayout(widget);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(6);
        hours = new QSpinBox(widget);
        minutes = new QSpinBox(widget);
        seconds = new QSpinBox(widget);
        for (QSpinBox *spin : {hours, minutes, seconds})
        {
            spin->setButtonSymbols(QAbstractSpinBox::UpDownArrows);
            spin->setAlignment(Qt::AlignRight);
            spin->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
            spin->setStyleSheet(scheduledSpinStyle);
        }
        hours->setRange(0, 999);
        minutes->setRange(0, 59);
        seconds->setRange(0, 59);
        hours->setSuffix(is_english_ ? QStringLiteral(" h") : QStringLiteral(" 时"));
        minutes->setSuffix(is_english_ ? QStringLiteral(" m") : QStringLiteral(" 分"));
        seconds->setSuffix(is_english_ ? QStringLiteral(" s") : QStringLiteral(" 秒"));
        layout->addWidget(hours, 1);
        layout->addWidget(minutes, 1);
        layout->addWidget(seconds, 1);
        return widget;
    };

    QSpinBox *durationHours = nullptr;
    QSpinBox *durationMinutes = nullptr;
    QSpinBox *durationSeconds = nullptr;
    rootLayout->addWidget(createScheduledRow(bodyWidget,
                                             is_english_ ? QStringLiteral("Record duration:")
                                                         : QStringLiteral("记录时长:"),
                                             createDurationInput(bodyWidget, durationHours, durationMinutes, durationSeconds)));

    auto setDurationValue = [](QSpinBox *hours, QSpinBox *minutes, QSpinBox *seconds, int totalSeconds) {
        totalSeconds = std::max(0, totalSeconds);
        if (hours) hours->setValue(totalSeconds / 3600);
        if (minutes) minutes->setValue((totalSeconds % 3600) / 60);
        if (seconds) seconds->setValue(totalSeconds % 60);
    };
    auto readDurationValue = [](QSpinBox *hours, QSpinBox *minutes, QSpinBox *seconds) {
        return (hours ? hours->value() : 0) * 3600 +
               (minutes ? minutes->value() : 0) * 60 +
               (seconds ? seconds->value() : 0);
    };
    setDurationValue(durationHours, durationMinutes, durationSeconds, scheduled_recording_duration_seconds_);

    auto *stack = new QStackedWidget(bodyWidget);

    auto *intervalPage = new QWidget(stack);
    auto *intervalLayout = new QVBoxLayout(intervalPage);
    intervalLayout->setContentsMargins(0, 0, 0, 0);
    intervalLayout->setSpacing(12);
    auto makeScheduledToggleStyle = [this]() {
        const QString textColor = appThemeColorName(dark_theme_enabled_ ? AppThemeColor::TextStrong : AppThemeColor::Text,
                                                    dark_theme_enabled_);
        return QStringLiteral(
            "QToolButton { background-color: transparent; border: 0px; border-radius: 0px; color: %1; padding: 2px 4px; margin: 0px; }"
            "QToolButton:hover, QToolButton:checked, QToolButton:pressed, QToolButton:focus { background-color: transparent; border: 0px; }"
            "QToolButton::menu-indicator { image: none; width: 0px; height: 0px; }")
                .arg(textColor);
    };
    const QString scheduledToggleStyle = makeScheduledToggleStyle();
    auto updateScheduledToggleIcon = [this](QToolButton *button) {
        if (!button)
        {
            return;
        }
        const QColor checkedColor = dark_theme_enabled_
            ? appThemeColor(AppThemeColor::Primary, true)
            : appThemeColor(AppThemeColor::ToolbarBlue, false);
        button->setIcon(createLucideIcon(button->isChecked()
            ? QStringLiteral("square-check-big")
            : QStringLiteral("square"),
            button->isChecked() ? checkedColor : toolbarColor(AppThemeColor::ToolbarDisabled)));
        button->setIconSize(QSize(26, 26));
    };
    auto createScheduledToggle = [scheduledToggleStyle, updateScheduledToggleIcon](const QString& text, QWidget *parent) {
        auto *button = new QToolButton(parent);
        button->setText(text);
        button->setCheckable(true);
        button->setAutoRaise(true);
        button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        button->setFocusPolicy(Qt::NoFocus);
        button->setCursor(Qt::PointingHandCursor);
        button->setStyleSheet(scheduledToggleStyle);
        updateScheduledToggleIcon(button);
        QObject::connect(button, &QToolButton::toggled, button, [button, updateScheduledToggleIcon]() {
            updateScheduledToggleIcon(button);
        });
        return button;
    };

    QSpinBox *intervalHours = nullptr;
    QSpinBox *intervalMinutes = nullptr;
    QSpinBox *intervalSeconds = nullptr;
    intervalLayout->addWidget(createScheduledRow(intervalPage,
                                                 is_english_ ? QStringLiteral("Interval:")
                                                             : QStringLiteral("间隔时长:"),
                                                 createDurationInput(intervalPage, intervalHours, intervalMinutes, intervalSeconds)));
    setDurationValue(intervalHours, intervalMinutes, intervalSeconds, scheduled_recording_interval_seconds_);

    auto *immediateCheck = createScheduledToggle(is_english_ ? QStringLiteral("Start first round immediately")
                                                             : QStringLiteral("首次立即开始"),
                                                 intervalPage);
    immediateCheck->setChecked(true);
    intervalLayout->addWidget(createScheduledRow(intervalPage, QString(), immediateCheck, false));

    auto *countWidget = new QWidget(intervalPage);
    auto *countLayout = new QHBoxLayout(countWidget);
    countLayout->setContentsMargins(0, 0, 0, 0);
    countLayout->setSpacing(10);
    auto *loopCheck = createScheduledToggle(is_english_ ? QStringLiteral("Loop until canceled")
                                                        : QStringLiteral("循环直到取消"),
                                            countWidget);
    auto *fixedCheck = createScheduledToggle(is_english_ ? QStringLiteral("Fixed count")
                                                         : QStringLiteral("固定次数"),
                                             countWidget);
    auto *countGroup = new QButtonGroup(countWidget);
    countGroup->setExclusive(true);
    countGroup->addButton(loopCheck);
    countGroup->addButton(fixedCheck);
    auto *countSpin = new QSpinBox(countWidget);
    countSpin->setRange(1, 999);
    countSpin->setValue(std::clamp(scheduled_recording_total_runs_, 1, 999));
    countSpin->setEnabled(scheduled_recording_fixed_count_enabled_);
    countSpin->setStyleSheet(scheduledSpinStyle);
    loopCheck->setChecked(!scheduled_recording_fixed_count_enabled_);
    fixedCheck->setChecked(scheduled_recording_fixed_count_enabled_);
    QObject::connect(fixedCheck, &QToolButton::toggled, countSpin, &QWidget::setEnabled);
    countLayout->addWidget(loopCheck);
    countLayout->addWidget(fixedCheck);
    countLayout->addWidget(countSpin);
    countLayout->addStretch(1);
    intervalLayout->addWidget(createScheduledRow(intervalPage,
                                                 is_english_ ? QStringLiteral("Repeat:")
                                                             : QStringLiteral("重复:"),
                                                 countWidget));
    intervalLayout->addStretch(1);
    stack->addWidget(intervalPage);

    auto *fixedTimePage = new QWidget(stack);
    auto *fixedTimeLayout = new QVBoxLayout(fixedTimePage);
    fixedTimeLayout->setContentsMargins(0, 0, 0, 0);
    fixedTimeLayout->setSpacing(12);
    auto *fixedTimeEdit = new QDateTimeEdit(fixedTimePage);
    fixedTimeEdit->setCalendarPopup(true);
    fixedTimeEdit->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    fixedTimeEdit->setDateTime(scheduled_recording_mode_ == ScheduledRecordingMode::FixedTime &&
                                   scheduled_recording_next_start_.isValid()
                               ? scheduled_recording_next_start_
                               : QDateTime::currentDateTime().addSecs(5 * 60));
    fixedTimeEdit->setStyleSheet(scheduledSpinStyle);
    fixedTimeLayout->addWidget(createScheduledRow(fixedTimePage,
                                                  is_english_ ? QStringLiteral("Start time:")
                                                              : QStringLiteral("开始时间:"),
                                                  fixedTimeEdit));
    fixedTimeLayout->addStretch(1);
    stack->addWidget(fixedTimePage);
    rootLayout->addWidget(stack);

    auto applyScheduledDialogLocalStyle = [applyModeComboPopupStyle,
                                           makeScheduledSpinStyle,
                                           makeScheduledToggleStyle,
                                           durationHours,
                                           durationMinutes,
                                           durationSeconds,
                                           intervalHours,
                                           intervalMinutes,
                                           intervalSeconds,
                                           countSpin,
                                           fixedTimeEdit,
                                           immediateCheck,
                                           loopCheck,
                                           fixedCheck,
                                           updateScheduledToggleIcon]() {
        applyModeComboPopupStyle();

        const QString spinStyle = makeScheduledSpinStyle();
        for (QSpinBox *spin : {durationHours, durationMinutes, durationSeconds,
                               intervalHours, intervalMinutes, intervalSeconds,
                               countSpin})
        {
            if (spin)
            {
                spin->setStyleSheet(spinStyle);
            }
        }
        if (fixedTimeEdit)
        {
            fixedTimeEdit->setStyleSheet(spinStyle);
        }

        const QString toggleStyle = makeScheduledToggleStyle();
        for (QToolButton *button : {immediateCheck, loopCheck, fixedCheck})
        {
            if (button)
            {
                button->setStyleSheet(toggleStyle);
                updateScheduledToggleIcon(button);
            }
        }
    };
    applyScheduledDialogLocalStyle();
    if (theme_toggle_action_)
    {
        QObject::connect(theme_toggle_action_, &QAction::changed, &dialog,
                         [applyScheduledDialogLocalStyle]() {
                             applyScheduledDialogLocalStyle();
                         });
    }

    modeCombo->setCurrentIndex(scheduled_recording_mode_ == ScheduledRecordingMode::FixedTime ? 1 : 0);
    stack->setCurrentIndex(modeCombo->currentIndex());
    QObject::connect(modeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                     stack, &QStackedWidget::setCurrentIndex);

    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, bodyWidget);
    if (!is_english_)
    {
        if (auto *okButton = buttonBox->button(QDialogButtonBox::Ok))
        {
            okButton->setText(QStringLiteral("确定"));
        }
        if (auto *cancelButton = buttonBox->button(QDialogButtonBox::Cancel))
        {
            cancelButton->setText(QStringLiteral("取消"));
        }
    }
    QPushButton *cancelScheduleButton = nullptr;
    if (scheduled_recording_mode_ != ScheduledRecordingMode::None)
    {
        cancelScheduleButton = buttonBox->addButton(is_english_ ? QStringLiteral("Cancel Schedule")
                                                                : QStringLiteral("取消定时"),
                                                    QDialogButtonBox::DestructiveRole);
        QObject::connect(cancelScheduleButton, &QPushButton::clicked, &dialog, [this, &dialog]() {
            cancelScheduledRecording(true);
            dialog.reject();
        });
    }
    QObject::connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttonBox, &QDialogButtonBox::accepted, &dialog, [this,
                                                                       &dialog,
                                                                       modeCombo,
                                                                       durationHours,
                                                                       durationMinutes,
                                                                       durationSeconds,
                                                                       intervalHours,
                                                                       intervalMinutes,
                                                                       intervalSeconds,
                                                                       immediateCheck,
                                                                       fixedCheck,
                                                                       countSpin,
                                                                       fixedTimeEdit,
                                                                       readDurationValue]() {
        const int recordDuration = readDurationValue(durationHours, durationMinutes, durationSeconds);
        if (recordDuration <= 0)
        {
            QMessageBox::warning(&dialog,
                                 is_english_ ? QStringLiteral("Scheduled Recording") : QStringLiteral("定时记录"),
                                 is_english_ ? QStringLiteral("Record duration must be at least 1 second.")
                                             : QStringLiteral("记录时长至少需要 1 秒。"));
            return;
        }

        const QDateTime now = QDateTime::currentDateTime();
        if (modeCombo->currentIndex() == 0)
        {
            const int intervalDuration = readDurationValue(intervalHours, intervalMinutes, intervalSeconds);
            if (intervalDuration <= 0)
            {
                QMessageBox::warning(&dialog,
                                     is_english_ ? QStringLiteral("Scheduled Recording") : QStringLiteral("定时记录"),
                                     is_english_ ? QStringLiteral("Interval must be at least 1 second.")
                                                 : QStringLiteral("间隔时长至少需要 1 秒。"));
                return;
            }
            configureScheduledRecording(ScheduledRecordingMode::Interval,
                                        recordDuration,
                                        intervalDuration,
                                        fixedCheck && fixedCheck->isChecked(),
                                        countSpin ? countSpin->value() : 1,
                                        immediateCheck && immediateCheck->isChecked()
                                            ? now
                                            : now.addSecs(intervalDuration));
            dialog.accept();
            return;
        }

        const QDateTime startTime = fixedTimeEdit ? fixedTimeEdit->dateTime() : QDateTime();
        if (!startTime.isValid() || startTime < now)
        {
            QMessageBox::warning(&dialog,
                                 is_english_ ? QStringLiteral("Scheduled Recording") : QStringLiteral("定时记录"),
                                 is_english_ ? QStringLiteral("Start time must be in the future.")
                                             : QStringLiteral("开始时间必须晚于当前时间。"));
            return;
        }
        configureScheduledRecording(ScheduledRecordingMode::FixedTime,
                                    recordDuration,
                                    1,
                                    true,
                                    1,
                                    startTime);
        dialog.accept();
    });

    rootLayout->addWidget(buttonBox);
    VaporView::installCustomTitleBar(&dialog, false);
    dialog.resize(scalePixels(is_english_ ? 470 : 400), dialog.sizeHint().height());
    dialog.exec();
    updateScheduledRecordingAction();
}

void MainWindow::onStartRecordingClicked()
{
    if (isRemoteSkyMode())
    {
        if (!ground_telemetry_service_ || !ground_telemetry_service_->isOpen())
        {
            log(is_english_ ? "Connect Remote Sky telemetry before recording" : "开始记录前请先连接天空端数传");
            return;
        }
        ground_telemetry_service_->sendCommand(VaporView::CommandId::StartRecording);
        return;
    }

    const bool tcpConnected = tcp_wave_panel_ && tcp_wave_panel_->isConnected();
    if (!is_connected_ && !tcpConnected)
    {
        log(is_english_ ? "At least one serial device or the TCP wave link must be connected before recording"
                        : "开始记录前，至少需要一个串口设备在线或 TCP 波形链路已连接");
        return;
    }

    if (!startRecordingSession())
    {
        log(is_english_ ? "Failed to start recording session" : "启动记录会话失败");
    }
}

void MainWindow::onPauseRecordingClicked()
{
    if (isRemoteSkyMode())
    {
        if (ground_telemetry_service_) ground_telemetry_service_->sendCommand(VaporView::CommandId::PauseRecording);
        return;
    }
    pauseRecordingSession(true);
}

void MainWindow::onStopRecordingClicked()
{
    if (isRemoteSkyMode())
    {
        if (ground_telemetry_service_) ground_telemetry_service_->sendCommand(VaporView::CommandId::StopRecording);
        return;
    }
    stopRecording(true);
}

void MainWindow::pauseRecordingSession(bool announce)
{
    if (!sensors_file_ || !sensors_file_->isOpen() || recording_paused_)
    {
        return;
    }

    stopRecordingWorkers();
    recording_paused_ = true;
    writeSessionMetadata();
    updateRecordingStatusLabel();

    if (announce)
    {
        log(QString(is_english_ ? "Paused recording session: %1" : "已暂停记录会话: %1").arg(session_directory_));
    }
}

void MainWindow::stopRecording(bool announce)
{
    const bool hadOpenSession = sensors_file_ && sensors_file_->isOpen();
    stopRecordingWorkers();

    if (!hadOpenSession)
    {
        recording_paused_ = false;
        updateRecordingStatusLabel();
        return;
    }

    const qint64 entryCount = recording_entry_count_.load();
    const qint64 waveformCount = waveform_frame_count_.load();
    const QString sessionPath = session_directory_;
    last_recording_session_name_ = QFileInfo(sessionPath).fileName();
    if (last_recording_session_name_.isEmpty())
    {
        last_recording_session_name_ = session_name_;
    }
    last_recording_entry_count_ = entryCount;
    last_recording_waveform_frame_count_ = waveformCount;
    last_raw_epsilon_record_count_ = raw_epsilon_record_count_.load();
    last_raw_ptb_record_count_ = raw_ptb_record_count_.load();
    last_raw_hmp_record_count_ = raw_hmp_record_count_.load();
    last_raw_lidar_record_count_ = raw_lidar_record_count_.load();
    last_raw_tcp_wave_record_count_ = raw_tcp_wave_record_count_.load();

    if (sensors_file_ && sensors_file_->isOpen())
    {
        writeSessionMetadata(recordingTimestampUtc());
    }

    if (announce)
    {
        log(QString(is_english_
            ? "Stopped recording (%1 sensor rows, %2 waveform frames): %3"
            : "记录已结束（设备 %1 行，波形 %2 帧）: %3")
            .arg(entryCount)
            .arg(waveformCount)
            .arg(sessionPath));
    }

    closeUnifiedRawDatFiles();

    {
        std::lock_guard<std::mutex> lock(recording_files_mutex_);
        if (sensors_file_ && sensors_file_->isOpen())
        {
            sensors_file_->flush();
            sensors_file_->close();
        }
        if (event_log_file_ && event_log_file_->isOpen())
        {
            event_log_file_->flush();
            event_log_file_->close();
        }
        if (error_log_file_ && error_log_file_->isOpen())
        {
            error_log_file_->flush();
            error_log_file_->close();
        }
    }

    sensors_file_.reset();
    resetUnifiedRawDatFiles();
    event_log_file_.reset();
    error_log_file_.reset();
    recording_entry_count_.store(0);
    waveform_frame_count_.store(0);
    waveform_file_count_.store(0);
    raw_epsilon_record_count_.store(0);
    raw_ptb_record_count_.store(0);
    raw_hmp_record_count_.store(0);
    raw_lidar_record_count_.store(0);
    raw_tcp_wave_record_count_.store(0);
    last_tcp_recording_status_update_ms_.store(0);
    recording_paused_ = false;
    session_directory_.clear();
    session_name_.clear();
    session_start_time_utc_.clear();
    session_start_time_us_ = 0;
    sensors_filename_.clear();
    raw_epsilon_filename_.clear();
    raw_ptb_filename_.clear();
    raw_hmp_filename_.clear();
    raw_lidar_filename_.clear();
    raw_tcp_wave_filename_.clear();
    raw_tcp_wave_peak_index_filename_.clear();
    raw_dat_doc_filename_.clear();
    session_metadata_filename_.clear();
    event_log_filename_.clear();
    error_log_filename_.clear();
    device_config_filename_.clear();
    updateRecordingStatusLabel();

}

void MainWindow::writeSensorsHeader()
{
    if (!sensors_file_ || !sensors_file_->isOpen())
    {
        return;
    }

    QTextStream out(sensors_file_.get());
    out.setEncoding(QStringConverter::Utf8);
    out.setGenerateByteOrderMark(true);
    out
        << "record_timestamp_us,"
        << "epsilon_host_timestamp_us,epsilon_device_timestamp_us,epsilon_utc_unix_s,epsilon_utc_microseconds,"
        << "nav_lat_deg,nav_lon_deg,nav_height_m,"
        << "ecef_x_m,ecef_y_m,ecef_z_m,"
        << "ned_n_m,ned_e_m,ned_d_m,"
        << "vel_n_mps,vel_e_mps,vel_d_mps,"
        << "body_vel_x_mps,body_vel_y_mps,body_vel_z_mps,"
        << "body_acc_x_mps2,body_acc_y_mps2,body_acc_z_mps2,"
        << "roll_deg,pitch_deg,yaw_deg,"
        << "quat_w,quat_x,quat_y,quat_z,"
        << "ang_vel_x_radps,ang_vel_y_radps,ang_vel_z_radps,"
        << "imu_acc_x_mps2,imu_acc_y_mps2,imu_acc_z_mps2,"
        << "imu_gyr_x_radps,imu_gyr_y_radps,imu_gyr_z_radps,"
        << "mag_x_mg,mag_y_mg,mag_z_mg,"
        << "gnss_fix,gnss_satellites,hdop,vdop,hacc_m,vacc_m,"
        << "lat_std_m,lon_std_m,height_std_m,diff_age_s,"
        << "heading_valid,system_status_bits,filter_status_bits,update_status_bits,"
        << "epsilon_imu_packet_rate_hz,epsilon_ahrs_packet_rate_hz,"
        << "epsilon_insgps_packet_rate_hz,epsilon_sys_state_packet_rate_hz,"
        << "epsilon_raw_gnss_packet_rate_hz,epsilon_satellite_packet_rate_hz,"
        << "epsilon_geodetic_packet_rate_hz,epsilon_ecef_packet_rate_hz,"
        << "epsilon_valid,epsilon_error_message,"
        << "hmp_temperature_c,hmp_humidity_rh,ptb_pressure_hpa,lidar_distance_m,lidar_signal_strength,lidar_valid\n";
    out.flush();
}

void MainWindow::onTcpRawWaveFrameReady(quint64 timestampUs,
                                        QByteArray rawSignalPayload,
                                        QByteArray harmonicPayload,
                                        VaporView::TcpFloatEncoding floatEncoding)
{
    if (!recording_thread_running_.load() || recording_paused_)
    {
        return;
    }

    if (static_cast<quint64>(rawSignalPayload.size()) > std::numeric_limits<quint32>::max() ||
        static_cast<quint64>(harmonicPayload.size()) > std::numeric_limits<quint32>::max())
    {
        return;
    }

    QByteArray payload;
    payload.resize(static_cast<int>(sizeof(quint32) * 2 + rawSignalPayload.size() + harmonicPayload.size()));
    char *cursor = payload.data();
    const quint32 rawSize = qToLittleEndian(static_cast<quint32>(rawSignalPayload.size()));
    const quint32 harmonicSize = qToLittleEndian(static_cast<quint32>(harmonicPayload.size()));
    std::memcpy(cursor, &rawSize, sizeof(rawSize));
    cursor += sizeof(rawSize);
    std::memcpy(cursor, &harmonicSize, sizeof(harmonicSize));
    cursor += sizeof(harmonicSize);
    if (!rawSignalPayload.isEmpty())
    {
        std::memcpy(cursor, rawSignalPayload.constData(), rawSignalPayload.size());
        cursor += rawSignalPayload.size();
    }
    if (!harmonicPayload.isEmpty())
    {
        std::memcpy(cursor, harmonicPayload.constData(), harmonicPayload.size());
    }

    TcpRawRecord record;
    record.timestamp_us = timestampUs;
    record.flags = kRawTcpWaveCombinedPayloadFlag | VaporView::tcpFloatEncodingToRawDatFlags(floatEncoding);
    record.payload = std::move(payload);
    enqueueTcpRawRecord(std::move(record));
}

void MainWindow::updateRecordingActionStates()
{
    if (isRemoteSkyMode())
    {
        const bool linkOpen = ground_telemetry_service_ && ground_telemetry_service_->isOpen();
        const bool recordingActive = remote_recording_state_ == 1;
        const bool recordingPaused = remote_recording_state_ == 2;
        if (start_recording_btn_) start_recording_btn_->setEnabled(linkOpen && !recordingActive);
        if (pause_recording_btn_) pause_recording_btn_->setEnabled(linkOpen && recordingActive);
        if (stop_recording_btn_) stop_recording_btn_->setEnabled(linkOpen && (recordingActive || recordingPaused));
        updateScheduledRecordingAction();
        return;
    }

    const bool tcpConnected = tcp_wave_panel_ && tcp_wave_panel_->isConnected();
    const bool recordingSourceAvailable = is_connected_ || tcpConnected;
    const bool sessionOpen = sensors_file_ && sensors_file_->isOpen();
    const bool recordingActive = sessionOpen && !recording_paused_ && recording_thread_running_.load();
    const bool uiBusy = connection_attempt_in_progress_ || port_detection_in_progress_ || epsilon_reconfigure_in_progress_;
    const bool canStart = recordingSourceAvailable && !uiBusy && (!sessionOpen || recording_paused_);
    const bool canPause = !uiBusy && recordingActive;
    const bool canStop = sessionOpen && !uiBusy;

    if (start_recording_btn_)
    {
        start_recording_btn_->setEnabled(canStart);
    }
    if (pause_recording_btn_)
    {
        pause_recording_btn_->setEnabled(canPause);
    }
    if (stop_recording_btn_)
    {
        stop_recording_btn_->setEnabled(canStop);
    }
    updateScheduledRecordingAction();
}

void MainWindow::updateConnectionStatus(bool connected)
{
    is_connected_ = connected;
    const bool uiBusy = connection_attempt_in_progress_ || port_detection_in_progress_ || epsilon_reconfigure_in_progress_;
    const bool inputsEnabled = !connected && !uiBusy;

    connect_btn_->setEnabled(inputsEnabled);
    cancel_connect_btn_->setEnabled(connection_attempt_in_progress_);
    disconnect_btn_->setEnabled(connected && !connection_attempt_in_progress_ && !epsilon_reconfigure_in_progress_);
    refresh_ports_btn_->setEnabled(inputsEnabled);
    if (epsilon_reconfigure_action_)
    {
        epsilon_reconfigure_action_->setEnabled(!uiBusy);
    }
    if (epsilon_rtcm_port_action_)
    {
        epsilon_rtcm_port_action_->setEnabled(!uiBusy);
    }
    if (epsilon_packet_rates_action_)
    {
        epsilon_packet_rates_action_->setEnabled(!uiBusy);
    }
    if (auto_detect_ports_btn_)
    {
        auto_detect_ports_btn_->setEnabled(!connected && !connection_attempt_in_progress_ && !epsilon_reconfigure_in_progress_);
        auto_detect_ports_btn_->setText(port_detection_in_progress_
            ? (is_english_ ? "Cancel Auto Detect" : "取消自动识别")
            : (is_english_ ? "Auto Detect Ports" : "自动识别串口"));
        auto_detect_ports_btn_->setToolTip(port_detection_in_progress_
            ? (is_english_ ? "Stop the current serial-port detection task." : "停止当前串口自动识别任务。")
            : (is_english_ ? "Probe available serial ports and automatically assign detected devices."
                           : "扫描可用串口，并将识别出的设备自动填入对应端口。"));
    }

    if (epsilon_port_combo_) epsilon_port_combo_->setEnabled(inputsEnabled);
    if (ptb_port_combo_) ptb_port_combo_->setEnabled(inputsEnabled);
    if (hmp_port_combo_) hmp_port_combo_->setEnabled(inputsEnabled);
    if (lidar_port_combo_) lidar_port_combo_->setEnabled(inputsEnabled);
    if (temperature_port_combo_) temperature_port_combo_->setEnabled(inputsEnabled);
    if (epsilon_baud_combo_) epsilon_baud_combo_->setEnabled(inputsEnabled);
    if (ptb_baud_combo_) ptb_baud_combo_->setEnabled(inputsEnabled);
    if (hmp_baud_combo_) hmp_baud_combo_->setEnabled(inputsEnabled);
    if (lidar_baud_combo_) lidar_baud_combo_->setEnabled(inputsEnabled);
    if (temperature_baud_combo_) temperature_baud_combo_->setEnabled(inputsEnabled);
    for (QPushButton* button : {imu_apply_btn_, imu_hi91_btn_, imu_hi92_btn_, imu_baud_115200_btn_, imu_baud_921600_btn_,
                                imu_rate_100_btn_, imu_rate_200_btn_, imu_rate_500_btn_, imu_rate_1000_btn_})
    {
        if (button)
        {
            button->setEnabled(!connection_attempt_in_progress_ && !port_detection_in_progress_ && !epsilon_reconfigure_in_progress_);
        }
    }

    if (port_detection_in_progress_)
    {
        status_label_->setText(is_english_ ? "Detecting Ports..." : "正在识别串口...");
        status_label_->setProperty("status", "connecting");
    }
    else if (epsilon_reconfigure_in_progress_)
    {
        status_label_->setText(is_english_ ? "Reconfiguring EPSILON..." : "正在重配 EPSILON...");
        status_label_->setProperty("status", "connecting");
    }
    else if (connection_attempt_in_progress_)
    {
        status_label_->setText(is_english_ ? "Connecting..." : "正在连接...");
        status_label_->setProperty("status", "connecting");
    }
    else if (connected)
    {
        status_label_->setText(is_english_ ? "Connected" : "已连接");
        status_label_->setProperty("status", "connected");
    }
    else
    {
        status_label_->setText(is_english_ ? "Disconnected" : "未连接");
        status_label_->setProperty("status", "disconnected");
    }
    status_label_->style()->unpolish(status_label_);
    status_label_->style()->polish(status_label_);
    updateSourceModeUi();
    updateRecordingActionStates();
    updateHomeDeviceStatusCapsules();
    updateDeviceConfigState();
}

bool MainWindow::homeDeviceConnected(VaporView::SkyDeviceId device) const
{
    if (isRemoteSkyMode())
    {
        return remote_device_states_.value(device, VaporView::DeviceState::Disconnected) == VaporView::DeviceState::Connected;
    }

    const CollectorSnapshot collectors = snapshotCollectors();
    switch (device)
    {
    case VaporView::SkyDeviceId::Epsilon:
        return collectors.epsilon && collectors.epsilon->isRunning();
    case VaporView::SkyDeviceId::Ptb:
        return collectors.ptb && collectors.ptb->isRunning();
    case VaporView::SkyDeviceId::Hmp:
        return collectors.hmp && collectors.hmp->isRunning();
    case VaporView::SkyDeviceId::Lidar:
        return collectors.lidar && collectors.lidar->isRunning();
    case VaporView::SkyDeviceId::TemperatureController:
        return collectors.temperature_controller && collectors.temperature_controller->isRunning();
    case VaporView::SkyDeviceId::WaveTcp:
        return tcp_wave_panel_ && tcp_wave_panel_->isConnected();
    case VaporView::SkyDeviceId::All:
        return false;
    }
    return false;
}

bool MainWindow::homeDevicePortSelected(VaporView::SkyDeviceId device) const
{
    if (device == VaporView::SkyDeviceId::WaveTcp)
    {
        return tcp_wave_panel_ != nullptr;
    }

    auto portSelected = [](const QComboBox *combo) {
        if (!combo)
        {
            return false;
        }
        const QString text = combo->currentText().trimmed();
        return !text.isEmpty() && !text.startsWith(QStringLiteral("--"));
    };

    switch (device)
    {
    case VaporView::SkyDeviceId::Epsilon:
        return portSelected(epsilon_port_combo_);
    case VaporView::SkyDeviceId::Ptb:
        return portSelected(ptb_port_combo_);
    case VaporView::SkyDeviceId::Hmp:
        return portSelected(hmp_port_combo_);
    case VaporView::SkyDeviceId::Lidar:
        return portSelected(lidar_port_combo_);
    case VaporView::SkyDeviceId::TemperatureController:
        return portSelected(temperature_port_combo_);
    case VaporView::SkyDeviceId::All:
    case VaporView::SkyDeviceId::WaveTcp:
        return false;
    }
    return false;
}

VaporView::DeviceState MainWindow::homeDeviceActionState(VaporView::SkyDeviceId device) const
{
    if (isRemoteSkyMode())
    {
        if (!ground_telemetry_service_ || !ground_telemetry_service_->isOpen())
        {
            return VaporView::DeviceState::Disabled;
        }
        if (device == VaporView::SkyDeviceId::WaveTcp && remote_wave_stream_enable_pending_)
        {
            return VaporView::DeviceState::Connecting;
        }
        const VaporView::DeviceState state = remote_device_states_.value(device, VaporView::DeviceState::Disconnected);
        return state == VaporView::DeviceState::Reconnecting ? VaporView::DeviceState::Connecting : state;
    }

    if (device == VaporView::SkyDeviceId::WaveTcp)
    {
        if (!tcp_wave_panel_)
        {
            return VaporView::DeviceState::Disabled;
        }
        if (tcp_wave_panel_->isConnecting())
        {
            return VaporView::DeviceState::Connecting;
        }
        return tcp_wave_panel_->isConnected() ? VaporView::DeviceState::Connected : VaporView::DeviceState::Disconnected;
    }

    if (homeDeviceConnected(device))
    {
        return VaporView::DeviceState::Connected;
    }
    if (!homeDevicePortSelected(device))
    {
        return VaporView::DeviceState::Disabled;
    }
    if (connection_attempt_in_progress_)
    {
        return VaporView::DeviceState::Connecting;
    }
    return VaporView::DeviceState::Disconnected;
}

void MainWindow::triggerHomeDeviceAction(VaporView::SkyDeviceId device)
{
    const VaporView::DeviceState state = homeDeviceActionState(device);
    if (state == VaporView::DeviceState::Disabled ||
        state == VaporView::DeviceState::Connecting ||
        state == VaporView::DeviceState::Reconnecting)
    {
        return;
    }
    const bool connected = state == VaporView::DeviceState::Connected;
    const bool connectRequested = !connected;
    if (isRemoteSkyMode())
    {
        if (!ground_telemetry_service_ || !ground_telemetry_service_->isOpen())
        {
            return;
        }
        if (connectRequested)
        {
            startHomeDeviceActionSpinner(device);
        }
        if (device == VaporView::SkyDeviceId::WaveTcp)
        {
            requestRemoteWaveTcpConnection(connectRequested);
            return;
        }
        sendRemoteDeviceCommand(connectRequested ? VaporView::CommandId::ConnectDevice : VaporView::CommandId::DisconnectDevice,
                                device);
        remote_device_states_.insert(device, connectRequested
            ? VaporView::DeviceState::Connecting
            : VaporView::DeviceState::Disconnected);
        updateRemoteDeviceButtonText(device, remote_device_states_.value(device));
        updateHomeDeviceStatusCapsules();
        return;
    }

    if (device == VaporView::SkyDeviceId::WaveTcp)
    {
        if (tcp_wave_panel_)
        {
            if (connectRequested)
            {
                startHomeDeviceActionSpinner(device);
            }
            tcp_wave_panel_->toggleConnection();
            updateHomeDeviceStatusCapsules();
        }
        return;
    }

    QAction *action = connected ? disconnect_btn_ : connect_btn_;
    if (action && action->isEnabled())
    {
        if (connectRequested)
        {
            for (VaporView::SkyDeviceId candidate : {VaporView::SkyDeviceId::Epsilon,
                                                     VaporView::SkyDeviceId::Ptb,
                                                     VaporView::SkyDeviceId::Hmp,
                                                     VaporView::SkyDeviceId::Lidar,
                                                     VaporView::SkyDeviceId::TemperatureController})
            {
                if (homeDevicePortSelected(candidate) && !homeDeviceConnected(candidate))
                {
                    startHomeDeviceActionSpinner(candidate);
                }
            }
        }
        action->trigger();
    }
}

void MainWindow::startHomeDeviceActionSpinner(VaporView::SkyDeviceId device)
{
    const qint64 untilMs = QDateTime::currentMSecsSinceEpoch() + kHomeDeviceActionSpinnerMinimumMs;
    const qint64 currentUntilMs = home_device_action_spinner_until_ms_.value(device, 0);
    home_device_action_spinner_until_ms_.insert(device, std::max(currentUntilMs, untilMs));
    if (home_device_action_spinner_timer_ && !home_device_action_spinner_timer_->isActive())
    {
        home_device_action_spinner_timer_->start();
    }
}

bool MainWindow::homeDeviceActionSpinnerActive(VaporView::SkyDeviceId device, qint64 nowMs) const
{
    return home_device_action_spinner_until_ms_.value(device, 0) > nowMs;
}

void MainWindow::updateHomeDeviceStatusCapsules()
{
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    bool anySpinnerActive = false;
    auto updateCapsule = [this, &anySpinnerActive, nowMs](QLabel *label, QToolButton *button, VaporView::SkyDeviceId device) {
        if (!label)
        {
            return;
        }
        const qint64 spinnerUntilMs = home_device_action_spinner_until_ms_.value(device, 0);
        if (spinnerUntilMs > 0 && spinnerUntilMs <= nowMs)
        {
            home_device_action_spinner_until_ms_.remove(device);
        }
        const VaporView::DeviceState state = homeDeviceActionState(device);
        const bool connected = state == VaporView::DeviceState::Connected;
        const bool connecting = state == VaporView::DeviceState::Connecting || state == VaporView::DeviceState::Reconnecting;
        const bool spinnerActive = connecting || homeDeviceActionSpinnerActive(device, nowMs);
        if (spinnerActive)
        {
            anySpinnerActive = true;
        }
        const QString stateKey = connected
            ? QStringLiteral("connected")
            : connecting
                ? QStringLiteral("connecting")
                : state == VaporView::DeviceState::Disabled
                    ? QStringLiteral("disabled")
                    : QStringLiteral("disconnected");
        const QString stateText = connected
            ? (is_english_ ? QStringLiteral("Connected") : QStringLiteral("已连接"))
            : connecting
                ? (is_english_ ? QStringLiteral("Connecting") : QStringLiteral("连接中"))
                : state == VaporView::DeviceState::Disabled
                    ? (is_english_ ? QStringLiteral("Not ready") : QStringLiteral("未就绪"))
                    : (is_english_ ? QStringLiteral("Ready to connect") : QStringLiteral("可以连接"));
        const QString deviceName = homeDeviceDisplayName(device, is_english_);
        label->setText(QStringLiteral("• %1").arg(deviceName));
        label->setProperty("connected", connected);
        label->setProperty("state", stateKey);
        label->setToolTip(is_english_
            ? QStringLiteral("%1 status: %2").arg(deviceName, stateText)
            : QStringLiteral("%1状态：%2").arg(deviceName, stateText));
        label->style()->unpolish(label);
        label->style()->polish(label);

        if (!button)
        {
            return;
        }
        const bool remoteMode = isRemoteSkyMode();
        const bool linkOpen = ground_telemetry_service_ && ground_telemetry_service_->isOpen();
        const bool enabled = state == VaporView::DeviceState::Disabled || spinnerActive
            ? false
            : remoteMode
                ? linkOpen
                : (device == VaporView::SkyDeviceId::WaveTcp
                    ? tcp_wave_panel_ != nullptr
                    : ((connected && disconnect_btn_ && disconnect_btn_->isEnabled()) ||
                       (!connected && connect_btn_ && connect_btn_->isEnabled())));
        const QString actionText = [&]() {
            if (spinnerActive)
            {
                return is_english_ ? QStringLiteral("Connecting") : QStringLiteral("连接中");
            }
            if (connected)
            {
                return is_english_ ? QStringLiteral("Disconnect") : QStringLiteral("断开");
            }
            if (state == VaporView::DeviceState::Disabled)
            {
                if (remoteMode)
                {
                    return is_english_ ? QStringLiteral("Connect telemetry first") : QStringLiteral("请先连接数传");
                }
                if (device == VaporView::SkyDeviceId::WaveTcp)
                {
                    return is_english_ ? QStringLiteral("TCP wave panel unavailable") : QStringLiteral("TCP 波形面板未就绪");
                }
                return is_english_ ? QStringLiteral("Select port first") : QStringLiteral("请先选择串口");
            }
            return is_english_ ? QStringLiteral("Connect") : QStringLiteral("连接");
        }();
        QString modeHint;
        if (remoteMode)
        {
            modeHint = is_english_ ? QStringLiteral("remote Sky device") : QStringLiteral("天空端设备");
        }
        else if (device == VaporView::SkyDeviceId::WaveTcp)
        {
            modeHint = is_english_ ? QStringLiteral("local TCP wave source") : QStringLiteral("本地 TCP 波形源");
        }
        else
        {
            modeHint = is_english_ ? QStringLiteral("local serial devices") : QStringLiteral("本地串口设备");
        }
        button->setEnabled(enabled);
        if (spinnerActive)
        {
            button->setIcon(createRotatedLucideIcon(QStringLiteral("link"),
                                                    toolbarColor(AppThemeColor::HomeDeviceSuccess),
                                                    (home_device_action_spinner_step_ * 360) / kHomeDeviceActionSpinnerFrames));
        }
        else
        {
            const QString iconName = connected ? QStringLiteral("unlink") : QStringLiteral("link");
            const QColor iconColor = connected
                ? toolbarColor(AppThemeColor::HomeDeviceDanger)
                : state == VaporView::DeviceState::Disabled
                    ? toolbarColor(AppThemeColor::ToolbarDisabled)
                    : toolbarColor(AppThemeColor::HomeDeviceSuccess);
            button->setIcon(createLucideIcon(iconName, iconColor));
        }
        button->setToolTip(is_english_
            ? QStringLiteral("%1 %2 (%3)").arg(actionText, deviceName, modeHint)
            : QStringLiteral("%1%2（%3）").arg(actionText, deviceName, modeHint));
        button->setStatusTip(button->toolTip());
        button->setAccessibleName(button->toolTip());
        button->setProperty("connected", connected);
        button->setProperty("state", spinnerActive ? QStringLiteral("connecting") : stateKey);
        button->style()->unpolish(button);
        button->style()->polish(button);
    };

    updateCapsule(home_epsilon_status_lbl_, home_epsilon_action_btn_, VaporView::SkyDeviceId::Epsilon);
    updateCapsule(home_ptb_status_lbl_, home_ptb_action_btn_, VaporView::SkyDeviceId::Ptb);
    updateCapsule(home_hmp_status_lbl_, home_hmp_action_btn_, VaporView::SkyDeviceId::Hmp);
    updateCapsule(home_lidar_status_lbl_, home_lidar_action_btn_, VaporView::SkyDeviceId::Lidar);
    updateCapsule(home_temperature_status_lbl_, home_temperature_action_btn_, VaporView::SkyDeviceId::TemperatureController);
    updateCapsule(home_wave_status_lbl_, home_wave_action_btn_, VaporView::SkyDeviceId::WaveTcp);
    if (home_device_action_spinner_timer_)
    {
        if (anySpinnerActive)
        {
            if (!home_device_action_spinner_timer_->isActive())
            {
                home_device_action_spinner_timer_->start();
            }
        }
        else
        {
            home_device_action_spinner_timer_->stop();
            home_device_action_spinner_step_ = 0;
        }
    }
}

void MainWindow::updateHomeDeviceActionSpinnerIcons()
{
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    bool anySpinnerActive = false;
    bool needsFullRefresh = false;
    auto updateButton = [this, nowMs, &anySpinnerActive, &needsFullRefresh](QToolButton *button, VaporView::SkyDeviceId device) {
        if (!button)
        {
            return;
        }

        const qint64 spinnerUntilMs = home_device_action_spinner_until_ms_.value(device, 0);
        if (spinnerUntilMs > 0 && spinnerUntilMs <= nowMs)
        {
            home_device_action_spinner_until_ms_.remove(device);
            needsFullRefresh = true;
        }
        const VaporView::DeviceState state = homeDeviceActionState(device);
        const bool spinnerActive =
            state == VaporView::DeviceState::Connecting ||
            state == VaporView::DeviceState::Reconnecting ||
            homeDeviceActionSpinnerActive(device, nowMs);
        if (!spinnerActive)
        {
            return;
        }

        anySpinnerActive = true;
        button->setIcon(createRotatedLucideIcon(QStringLiteral("link"),
                                                toolbarColor(AppThemeColor::HomeDeviceSuccess),
                                                (home_device_action_spinner_step_ * 360) / kHomeDeviceActionSpinnerFrames));
        button->update();
    };

    updateButton(home_epsilon_action_btn_, VaporView::SkyDeviceId::Epsilon);
    updateButton(home_ptb_action_btn_, VaporView::SkyDeviceId::Ptb);
    updateButton(home_hmp_action_btn_, VaporView::SkyDeviceId::Hmp);
    updateButton(home_lidar_action_btn_, VaporView::SkyDeviceId::Lidar);
    updateButton(home_temperature_action_btn_, VaporView::SkyDeviceId::TemperatureController);
    updateButton(home_wave_action_btn_, VaporView::SkyDeviceId::WaveTcp);
    if (needsFullRefresh)
    {
        updateHomeDeviceStatusCapsules();
        return;
    }
    if (!anySpinnerActive && home_device_action_spinner_timer_)
    {
        home_device_action_spinner_timer_->stop();
        home_device_action_spinner_step_ = 0;
    }
}

bool MainWindow::anyCollectorRunning() const
{
    const CollectorSnapshot collectors = snapshotCollectors();
    return (collectors.epsilon && collectors.epsilon->isRunning()) ||
        (collectors.gnss && collectors.gnss->isRunning()) ||
        (collectors.imu && collectors.imu->isRunning()) ||
        (collectors.ptb && collectors.ptb->isRunning()) ||
        (collectors.hmp && collectors.hmp->isRunning()) ||
        (collectors.lidar && collectors.lidar->isRunning()) ||
        (collectors.temperature_controller && collectors.temperature_controller->isRunning());
}

MainWindow::CollectorSnapshot MainWindow::snapshotCollectors() const
{
    std::lock_guard<std::mutex> lock(collector_mutex_);
    return {epsilon_collector_, gnss_collector_, imu_collector_, ptb_collector_, hmp_collector_, lidar_collector_,
            temperature_controller_collector_};
}

void MainWindow::setCollectors(CollectorSnapshot collectors)
{
    std::lock_guard<std::mutex> lock(collector_mutex_);
    epsilon_collector_ = std::move(collectors.epsilon);
    gnss_collector_ = std::move(collectors.gnss);
    imu_collector_ = std::move(collectors.imu);
    ptb_collector_ = std::move(collectors.ptb);
    hmp_collector_ = std::move(collectors.hmp);
    lidar_collector_ = std::move(collectors.lidar);
    temperature_controller_collector_ = std::move(collectors.temperature_controller);

    if (epsilon_collector_) epsilon_collector_->setEnglish(is_english_);
    if (gnss_collector_) gnss_collector_->setEnglish(is_english_);
    if (imu_collector_) imu_collector_->setEnglish(is_english_);
    if (ptb_collector_) ptb_collector_->setEnglish(is_english_);
    if (hmp_collector_) hmp_collector_->setEnglish(is_english_);
    if (lidar_collector_) lidar_collector_->setEnglish(is_english_);
    if (temperature_controller_collector_) temperature_controller_collector_->setEnglish(is_english_);
}

void MainWindow::invalidateTemperatureControllerDataUi()
{
    current_temperature_controller_ = VaporView::TemperatureControllerData();
    if (temperature_controller_panel_)
    {
        temperature_controller_panel_->updateRate(0.0);
        temperature_controller_panel_->updateData(current_temperature_controller_);
    }
    if (temperature_overview_panel_)
    {
        temperature_overview_panel_->updateData(current_temperature_controller_);
    }
}

void MainWindow::stopAllCollectors()
{
    CollectorSnapshot collectors;
    {
        std::lock_guard<std::mutex> lock(collector_mutex_);
        collectors.epsilon = std::move(epsilon_collector_);
        collectors.gnss = std::move(gnss_collector_);
        collectors.imu = std::move(imu_collector_);
        collectors.ptb = std::move(ptb_collector_);
        collectors.hmp = std::move(hmp_collector_);
        collectors.lidar = std::move(lidar_collector_);
        collectors.temperature_controller = std::move(temperature_controller_collector_);
    }

    if (collectors.epsilon)
    {
        collectors.epsilon->stop();
    }
    if (collectors.gnss)
    {
        collectors.gnss->stop();
    }
    if (collectors.imu)
    {
        collectors.imu->stop();
    }
    if (collectors.ptb)
    {
        collectors.ptb->stop();
    }
    if (collectors.hmp)
    {
        collectors.hmp->stop();
    }
    if (collectors.lidar)
    {
        collectors.lidar->stop();
    }
    if (collectors.temperature_controller)
    {
        collectors.temperature_controller->stop();
    }
}

bool MainWindow::shouldAbortConnectionAttempt()
{
    return cancel_connection_requested_.load();
}

void MainWindow::finishConnectionAttempt(bool connected)
{
    connection_attempt_in_progress_ = false;
    cancel_connection_requested_.store(false);
    if (!connected && sensors_file_ && sensors_file_->isOpen())
    {
        stopRecording(true);
    }
    hideStatusTaskProgress();
    updateConnectionStatus(connected);
}

void MainWindow::onRefreshPortsClicked()
{
    QStringList ports = getAvailablePorts();

    auto updateCombo = [this, &ports](QComboBox* combo) {
        if (!combo)
        {
            return;
        }
        QString current = combo->currentText();
        combo->clear();
        combo->addItem(is_english_ ? "-- Select --" : "-- 选择 --");
        combo->addItems(ports);
        int idx = combo->findText(current);
        if (idx >= 0)
        {
            combo->setCurrentIndex(idx);
        }
        else
        {
            combo->setEditText(current);
        }
    };

    updateCombo(epsilon_port_combo_);
    updateCombo(ptb_port_combo_);
    updateCombo(hmp_port_combo_);
    updateCombo(lidar_port_combo_);
    updateCombo(temperature_port_combo_);
    syncDeviceConfigPageFromHome();
    updateTemperatureControllerTitleText();
    updateTemperatureTitleButtonsState();

    log(QString(is_english_ ? "Ports refreshed: %1 serial ports"
                            : "端口已刷新: %1 个串口")
            .arg(ports.size()));
}

void MainWindow::onAutoDetectPortsClicked()
{
    if (port_detection_in_progress_)
    {
        cancel_connection_requested_.store(true);
        log(is_english_ ? "Cancel requested, stopping automatic serial-port detection..." : "已请求取消，正在停止自动识别串口...");
        showBusyStatusTaskProgress(is_english_ ? "Canceling port detection..." : "正在取消串口识别...");
        updateConnectionStatus(is_connected_);
        QApplication::processEvents(QEventLoop::AllEvents);
        return;
    }

    if (is_connected_ || connection_attempt_in_progress_)
    {
        return;
    }

    if (port_detection_thread_.joinable())
    {
        port_detection_thread_.join();
    }

    onRefreshPortsClicked();
    port_detection_in_progress_ = true;
    cancel_connection_requested_.store(false);
    updateConnectionStatus(is_connected_);
    log(is_english_ ? "Starting automatic serial-port detection..." : "开始自动识别串口...");
    showBusyStatusTaskProgress(is_english_ ? "Detecting Ports..." : "正在识别串口...");

    const QString selectedEpsilonPort = epsilon_port_combo_ ? epsilon_port_combo_->currentText().trimmed() : QString();
    const QString selectedPtbPort = ptb_port_combo_ ? ptb_port_combo_->currentText().trimmed() : QString();
    const QString selectedHmpPort = hmp_port_combo_ ? hmp_port_combo_->currentText().trimmed() : QString();
    const QString selectedLidarPort = lidar_port_combo_ ? lidar_port_combo_->currentText().trimmed() : QString();
    const QString selectedTemperaturePort = temperature_port_combo_ ? temperature_port_combo_->currentText().trimmed() : QString();
    const QString selectedEpsilonBaud = epsilon_baud_combo_ ? epsilon_baud_combo_->currentText().trimmed() : QStringLiteral("921600");
    const QString selectedPtbBaud = ptb_baud_combo_ ? ptb_baud_combo_->currentText().trimmed() : QStringLiteral("9600");
    const QString selectedHmpBaud = hmp_baud_combo_ ? hmp_baud_combo_->currentText().trimmed() : QStringLiteral("19200");
    const QString selectedLidarBaud = lidar_baud_combo_ ? lidar_baud_combo_->currentText().trimmed() : QStringLiteral("500000");
    const QString selectedTemperatureBaud = temperature_baud_combo_ ? temperature_baud_combo_->currentText().trimmed() : QStringLiteral("38400");
    const bool english = is_english_;

    port_detection_thread_ = std::thread([this,
                                          english,
                                          selectedEpsilonPort,
                                          selectedPtbPort,
                                          selectedHmpPort,
                                          selectedLidarPort,
                                          selectedTemperaturePort,
                                          selectedEpsilonBaud,
                                          selectedPtbBaud,
                                          selectedHmpBaud,
                                          selectedLidarBaud,
                                          selectedTemperatureBaud]() {
        struct ProbeSpec
        {
            QString key;
            QString label;
            QString baud_text;
            std::function<bool(const QString&)> probe;
        };

        struct SelectedProbeSpec
        {
            ProbeSpec spec;
            QString port_name;
        };

        struct DetectionResult
        {
            QString key;
            QString port_name;
            QString baud_text;
        };

        const auto cancelRequested = [this]() { return cancel_connection_requested_.load(); };
        auto postLog = [this](const QString& message) {
            QMetaObject::invokeMethod(this, [this, message]() { log(message); }, Qt::QueuedConnection);
        };
        auto finishOnUi = [this](QVector<DetectionResult> detections) {
            QMetaObject::invokeMethod(this, [this, detections = std::move(detections)]() {
                const QString selectText = is_english_ ? "-- Select --" : "-- 选择 --";
                auto applySelection = [&selectText](QComboBox* combo, const QString& value) {
                    if (!combo)
                    {
                        return;
                    }
                    const int idx = combo->findText(value);
                    if (idx >= 0)
                    {
                        combo->setCurrentIndex(idx);
                    }
                    else if (!value.isEmpty() && value != selectText)
                    {
                        combo->setEditText(value);
                    }
                };
                auto normalizePort = [&selectText](const QString& value) {
                    return (value.isEmpty() || value == selectText) ? selectText : value;
                };

                QHash<QString, QComboBox*> portCombos{
                    {"epsilon", epsilon_port_combo_},
                    {"ptb", ptb_port_combo_},
                    {"hmp", hmp_port_combo_},
                    {"lidar", lidar_port_combo_},
                    {"temperature", temperature_port_combo_},
                };
                QHash<QString, QComboBox*> baudCombos{
                    {"epsilon", epsilon_baud_combo_},
                    {"ptb", ptb_baud_combo_},
                    {"hmp", hmp_baud_combo_},
                    {"lidar", lidar_baud_combo_},
                    {"temperature", temperature_baud_combo_},
                };
                QHash<QString, QString> detectedPorts;
                QHash<QString, QString> detectedBauds;
                QSet<QString> detectedKeys;
                QSet<QString> detectedPortNames;
                for (const DetectionResult& detection : detections)
                {
                    const QString portName = normalizePort(detection.port_name);
                    if (portName == selectText)
                    {
                        continue;
                    }
                    if (!portCombos.contains(detection.key))
                    {
                        continue;
                    }
                    detectedPorts[detection.key] = portName;
                    detectedBauds[detection.key] = detection.baud_text;
                    detectedKeys.insert(detection.key);
                    detectedPortNames.insert(portName);
                }

                for (auto it = portCombos.cbegin(); it != portCombos.cend(); ++it)
                {
                    if (!detectedKeys.contains(it.key()) && detectedPortNames.contains(normalizePort(it.value() ? it.value()->currentText() : QString())))
                    {
                        applySelection(it.value(), selectText);
                    }
                }

                for (const DetectionResult& detection : detections)
                {
                    const QString portName = detectedPorts.value(detection.key);
                    if (portName.isEmpty())
                    {
                        continue;
                    }
                    applySelection(portCombos.value(detection.key), portName);
                    if (QComboBox *baudCombo = baudCombos.value(detection.key, nullptr))
                    {
                        baudCombo->setCurrentText(detectedBauds.value(detection.key));
                    }
                }

                port_detection_in_progress_ = false;
                cancel_connection_requested_.store(false);
                hideStatusTaskProgress();
                updateConnectionStatus(is_connected_);
            }, Qt::QueuedConnection);
        };

        auto probeCollector = [cancelRequested](const QString& port_name, auto&& collector, const VaporView::SerialConfig& config) {
            collector->setCancelCallback(cancelRequested);
            if (!collector->start(port_name.toStdString(), config))
            {
                return false;
            }

            const bool responded = collector->checkDeviceResponse();
            collector->stop();
            return responded;
        };

        const QString epsilonDefaultBaud = QStringLiteral("921600");
        const QString ptbDefaultBaud = QStringLiteral("9600");
        const QString hmpDefaultBaud = QStringLiteral("19200");
        const QString lidarDefaultBaud = QStringLiteral("500000");
        const QString temperatureDefaultBaud = QStringLiteral("38400");
        auto normalizeBaud = [](const QString& baud, const QString& fallback) {
            const QString trimmed = baud.trimmed();
            bool ok = false;
            const int value = trimmed.toInt(&ok);
            return ok && value > 0 ? trimmed : fallback;
        };
        const QHash<QString, QString> deviceLabels{
            {"epsilon", "EPSILON"},
            {"ptb", "PTB210"},
            {"hmp", "HMP3"},
            {"lidar", "TFA1500-L"},
            {"temperature", "RD105"},
        };

        auto makeEpsilonProbe = [probeCollector](const QString& baudText) {
            const int baud = baudText.toInt();
            return ProbeSpec{"epsilon", "EPSILON", baudText, [probeCollector, baud](const QString& port_name) {
                auto collector = std::make_unique<VaporView::EpsilonCollector>();
                return probeCollector(port_name, std::move(collector), VaporView::SerialConfig::N81(baud));
            }};
        };
        auto makePtbProbe = [probeCollector](const QString& baudText) {
            const int baud = baudText.toInt();
            return ProbeSpec{"ptb", "PTB210", baudText, [probeCollector, baud](const QString& port_name) {
                auto collector = std::make_unique<VaporView::PtbCollector>();
                return probeCollector(port_name, std::move(collector), VaporView::SerialConfig::E71(baud));
            }};
        };
        auto makeHmpProbe = [probeCollector](const QString& baudText) {
            const int baud = baudText.toInt();
            return ProbeSpec{"hmp", "HMP3", baudText, [probeCollector, baud](const QString& port_name) {
                auto collector = std::make_unique<VaporView::HmpCollector>();
                return probeCollector(port_name, std::move(collector), VaporView::SerialConfig::N82(baud));
            }};
        };
        auto makeLidarProbe = [probeCollector](const QString& baudText) {
            const int baud = baudText.toInt();
            return ProbeSpec{"lidar", "TFA1500-L", baudText, [probeCollector, baud](const QString& port_name) {
                auto collector = std::make_unique<VaporView::LidarCollector>();
                return probeCollector(port_name, std::move(collector), VaporView::SerialConfig::N81(baud));
            }};
        };
        auto makeTemperatureProbe = [probeCollector](const QString& baudText) {
            const int baud = baudText.toInt();
            return ProbeSpec{"temperature", "RD105", baudText, [probeCollector, baud](const QString& port_name) {
                auto collector = std::make_unique<VaporView::TemperatureControllerCollector>();
                collector->setSlaveAddress(static_cast<uint8_t>(rememberedTemperatureSlaveAddress()));
                return probeCollector(port_name, std::move(collector), VaporView::SerialConfig::N81(baud));
            }};
        };
        auto addUniqueProbe = [](QVector<ProbeSpec>& specs, QSet<QString>& seen, ProbeSpec spec) {
            const QString id = spec.key + QLatin1Char('@') + spec.baud_text;
            if (seen.contains(id))
            {
                return;
            }
            seen.insert(id);
            specs.push_back(std::move(spec));
        };

        auto addSelectedProbe = [](QVector<SelectedProbeSpec>& specs, QSet<QString>& seen, ProbeSpec spec, const QString& portName) {
            const QString normalizedPort = portName.trimmed();
            if (normalizedPort.isEmpty() || normalizedPort.startsWith(QStringLiteral("--")))
            {
                return;
            }
            const QString id = spec.key + QLatin1Char('@') + normalizedPort + QLatin1Char('@') + spec.baud_text;
            if (seen.contains(id))
            {
                return;
            }
            seen.insert(id);
            specs.push_back({std::move(spec), normalizedPort});
        };

        QVector<SelectedProbeSpec> selected_probe_specs;
        QSet<QString> seenSelectedProbeIds;
        addSelectedProbe(selected_probe_specs, seenSelectedProbeIds, makeEpsilonProbe(normalizeBaud(selectedEpsilonBaud, epsilonDefaultBaud)), selectedEpsilonPort);
        addSelectedProbe(selected_probe_specs, seenSelectedProbeIds, makePtbProbe(normalizeBaud(selectedPtbBaud, ptbDefaultBaud)), selectedPtbPort);
        addSelectedProbe(selected_probe_specs, seenSelectedProbeIds, makeHmpProbe(normalizeBaud(selectedHmpBaud, hmpDefaultBaud)), selectedHmpPort);
        addSelectedProbe(selected_probe_specs, seenSelectedProbeIds, makeLidarProbe(normalizeBaud(selectedLidarBaud, lidarDefaultBaud)), selectedLidarPort);
        addSelectedProbe(selected_probe_specs, seenSelectedProbeIds, makeTemperatureProbe(normalizeBaud(selectedTemperatureBaud, temperatureDefaultBaud)), selectedTemperaturePort);

        QVector<ProbeSpec> default_probe_specs;
        QSet<QString> seenDefaultProbeIds;
        addUniqueProbe(default_probe_specs, seenDefaultProbeIds, makeEpsilonProbe(epsilonDefaultBaud));
        addUniqueProbe(default_probe_specs, seenDefaultProbeIds, makePtbProbe(ptbDefaultBaud));
        addUniqueProbe(default_probe_specs, seenDefaultProbeIds, makeHmpProbe(hmpDefaultBaud));
        addUniqueProbe(default_probe_specs, seenDefaultProbeIds, makeLidarProbe(lidarDefaultBaud));
        addUniqueProbe(default_probe_specs, seenDefaultProbeIds, makeTemperatureProbe(temperatureDefaultBaud));

        QStringList port_names = getAvailablePorts();
        if (port_names.isEmpty() && selected_probe_specs.isEmpty())
        {
            postLog(english ? "Auto detect stopped: no serial ports found." : "自动识别结束：当前没有发现可用串口。");
            finishOnUi({});
            return;
        }

        QVector<DetectionResult> detections;
        QSet<QString> detectedKeys;
        QSet<QString> detectedPorts;
        QSet<QString> attemptedProbeIds;
        postLog(QString(english ? "Auto detect: selected settings first, then %1 serial port(s) with default bauds."
                                 : "自动识别：先探测已选配置，再用默认波特率探测 %1 个串口。")
                    .arg(port_names.size()));

        auto probeAttemptId = [](const ProbeSpec& spec, const QString& portName) {
            return spec.key + QLatin1Char('@') + portName + QLatin1Char('@') + spec.baud_text;
        };
        auto recordDetection = [&](const ProbeSpec& spec, const QString& portName) {
            detections.push_back({spec.key, portName, spec.baud_text});
            detectedKeys.insert(spec.key);
            detectedPorts.insert(portName);
            postLog(QString(english ? "[Auto Detect] Identified %1 on %2 @ %3" : "[自动识别] 已识别 %1: %2 @ %3")
                        .arg(spec.label, portName, spec.baud_text));
        };
        auto finishCanceled = [&]() {
            postLog(QString(english
                ? "Auto detect canceled; keeping %1 identified device(s)."
                : "自动识别已取消；保留已识别出的 %1 个设备。")
                .arg(detections.size()));
            finishOnUi(std::move(detections));
        };

        auto runSelectedProbePhase = [&](const QString& phaseLabel, const QVector<SelectedProbeSpec>& specs) {
            if (specs.isEmpty())
            {
                return true;
            }

            postLog(QString(english ? "[Auto Detect] %1" : "[自动识别] %1").arg(phaseLabel));
            for (const SelectedProbeSpec& selectedSpec : specs)
            {
                const ProbeSpec& spec = selectedSpec.spec;
                if (detectedKeys.contains(spec.key))
                {
                    continue;
                }
                if (cancelRequested())
                {
                    finishCanceled();
                    return false;
                }
                if (detectedPorts.contains(selectedSpec.port_name))
                {
                    continue;
                }

                const QString attemptId = probeAttemptId(spec, selectedSpec.port_name);
                attemptedProbeIds.insert(attemptId);
                postLog(QString(english ? "[Auto Detect] Probing selected %1 on %2 @ %3..." : "[自动识别] 正在探测已选 %1: %2 @ %3 ...")
                            .arg(spec.label, selectedSpec.port_name, spec.baud_text));

                if (spec.probe(selectedSpec.port_name))
                {
                    recordDetection(spec, selectedSpec.port_name);
                }
            }
            return true;
        };

        auto runDefaultProbePhase = [&](const QString& phaseLabel, const QVector<ProbeSpec>& specs) {
            if (specs.isEmpty() || port_names.isEmpty())
            {
                return true;
            }

            postLog(QString(english ? "[Auto Detect] %1" : "[自动识别] %1").arg(phaseLabel));
            for (const ProbeSpec& spec : specs)
            {
                if (detectedKeys.contains(spec.key))
                {
                    continue;
                }

                for (const QString& port_name : port_names)
                {
                    if (cancelRequested())
                    {
                        finishCanceled();
                        return false;
                    }
                    if (detectedPorts.contains(port_name))
                    {
                        continue;
                    }
                    const QString attemptId = probeAttemptId(spec, port_name);
                    if (attemptedProbeIds.contains(attemptId))
                    {
                        continue;
                    }
                    attemptedProbeIds.insert(attemptId);

                    postLog(QString(english ? "[Auto Detect] Probing %1 on %2 @ %3..." : "[自动识别] 正在探测 %1: %2 @ %3 ...")
                                .arg(spec.label, port_name, spec.baud_text));

                    if (!spec.probe(port_name))
                    {
                        continue;
                    }

                    recordDetection(spec, port_name);
                    break;
                }
            }
            return true;
        };

        if (!runSelectedProbePhase(english
                ? "Selected port/baud pass: probing the current configured port for each device."
                : "已选串口/波特率阶段：先探测每个设备当前配置的串口。",
                selected_probe_specs))
        {
            return;
        }
        if (!runDefaultProbePhase(english
                ? "Default baud pass: probing remaining devices on available ports."
                : "默认波特率阶段：使用各设备默认波特率探测剩余串口。",
                default_probe_specs))
        {
            return;
        }

        for (auto it = deviceLabels.cbegin(); it != deviceLabels.cend(); ++it)
        {
            if (!detectedKeys.contains(it.key()))
            {
                postLog(QString(english ? "[Auto Detect] %1 not found" : "[自动识别] 未找到 %1").arg(it.value()));
            }
        }

        postLog(QString(english ? "Auto detect finished: identified %1 device(s)." : "自动识别完成：共识别出 %1 个设备。")
                    .arg(detections.size()));
        finishOnUi(std::move(detections));
    });
}

void MainWindow::onConnectClicked()
{
    if (isRemoteSkyMode())
    {
        clearRemoteSkyDataUi();
        const bool tcpTelemetry = isRemoteSkyTcpMode();
        bool opened = false;
        QString openedText;
        if (tcpTelemetry)
        {
            const QString host = sky_telemetry_tcp_host_edit_ ? sky_telemetry_tcp_host_edit_->text().trimmed() : QString();
            const int tcpPort = sky_telemetry_tcp_port_spin_ ? sky_telemetry_tcp_port_spin_->value() : 39100;
            if (host.isEmpty())
            {
                log(is_english_ ? "Enter the Sky telemetry IP first" : "请先输入天空端数传 IP");
                return;
            }
            openedText = QStringLiteral("%1:%2").arg(host).arg(tcpPort);
            log(QString(is_english_ ? "Connecting Sky telemetry TCP: %1" : "正在连接天空端 TCP 数传：%1").arg(openedText));
            opened = ground_telemetry_service_ && ground_telemetry_service_->openTcp(host, static_cast<quint16>(tcpPort));
        }
        else
        {
            const QString port = sky_telemetry_port_combo_ ? sky_telemetry_port_combo_->currentText().trimmed() : QString();
            const int baud = sky_telemetry_baud_combo_ ? sky_telemetry_baud_combo_->currentText().toInt() : 921600;
            if (port.isEmpty())
            {
                log(is_english_ ? "Select the Sky telemetry port first" : "请先选择天空端数传串口");
                return;
            }
            openedText = QStringLiteral("%1 @ %2").arg(port).arg(baud);
            log(QString(is_english_ ? "Opening Sky telemetry serial port: %1" : "正在打开天空端数传串口：%1").arg(openedText));
            opened = ground_telemetry_service_ && ground_telemetry_service_->open(port, baud);
        }
        if (opened)
        {
            updateConnectionStatus(true);
            ground_telemetry_service_->sendCommand(VaporView::CommandId::DisableWaveformStreaming);
            ground_telemetry_service_->sendCommand(VaporView::CommandId::RequestStatus);
            status_label_->setText(is_english_ ? "Telemetry link open, waiting for Sky handshake" : "数传链路已打开，等待天空端握手");
            status_label_->setProperty("status", "connecting");
            status_label_->style()->unpolish(status_label_);
            status_label_->style()->polish(status_label_);
            log(QString(is_english_ ? "Telemetry link opened (%1); waiting for Sky handshake..." : "数传链路已打开（%1），正在等待天空端握手...").arg(openedText));
        }
        else
        {
            updateConnectionStatus(false);
            log(is_english_ ? "Failed to open Remote Sky telemetry link" : "打开天空端数传链路失败");
        }
        return;
    }

    if (connection_thread_.joinable())
    {
        connection_thread_.join();
    }

    connection_attempt_in_progress_ = true;
    cancel_connection_requested_.store(false);
    updateConnectionStatus(false);

    log(is_english_ ? "Connecting..." : "正在连接...");

    current_epsilon_ = VaporView::EpsilonData();
    current_gnss_ = VaporView::GnssData();
    current_imu_ = VaporView::ImuData();
    current_ptb_ = VaporView::PtbData();
    current_hmp_ = VaporView::HmpData();
    current_lidar_ = VaporView::LidarData();
    current_temperature_controller_ = VaporView::TemperatureControllerData();

    if (epsilon_panel_) epsilon_panel_->updateData(current_epsilon_);
    if (ptb_panel_) ptb_panel_->updateData(current_ptb_);
    if (hmp_panel_) hmp_panel_->updateData(current_hmp_);
    if (lidar_panel_) lidar_panel_->updateData(current_lidar_);
    invalidateTemperatureControllerDataUi();
    updateEnvironmentStatusIcons(false, false, false);

    if (epsilon_panel_) epsilon_panel_->updateRate(0.0);
    if (ptb_panel_) ptb_panel_->updateRate(0.0);
    if (hmp_panel_) hmp_panel_->updateRate(0.0);
    if (lidar_panel_) lidar_panel_->updateRate(0.0);

    const bool english = is_english_;
    const QString selectText = english ? "-- Select --" : "-- 选择 --";
    const QString epsilonPort = epsilon_port_combo_ ? epsilon_port_combo_->currentText().trimmed() : QString();
    const QString ptbPort = ptb_port_combo_->currentText();
    const QString hmpPort = hmp_port_combo_->currentText();
    const QString lidarPort = lidar_port_combo_->currentText();
    const QString temperaturePort = temperature_port_combo_ ? temperature_port_combo_->currentText() : QString();
    const QString epsilonBaudText = epsilon_baud_combo_ ? epsilon_baud_combo_->currentText().trimmed() : QStringLiteral("921600");
    const QString ptbBaudText = ptb_baud_combo_->currentText();
    const QString hmpBaudText = hmp_baud_combo_->currentText();
    const QString lidarBaudText = lidar_baud_combo_->currentText();
    const QString temperatureBaudText = temperature_baud_combo_ ? temperature_baud_combo_->currentText().trimmed() : QStringLiteral("38400");
    const QString epsilonRateText = epsilon_rate_combo_ ? epsilon_rate_combo_->currentText() : QStringLiteral("100");
    const QString ptbRateText = ptb_rate_combo_ ? ptb_rate_combo_->currentText() : QStringLiteral("20");
    const QString hmpRateText = hmp_rate_combo_ ? hmp_rate_combo_->currentText() : QStringLiteral("20");
    const QString lidarRateText = lidar_rate_combo_ ? lidar_rate_combo_->currentText() : QStringLiteral("100");
    const QString temperatureRateText = temperature_rate_combo_ ? temperature_rate_combo_->currentText() : QString::number(kDefaultTemperatureSampleRateHz);
    const bool skipEpsilonDeviceRate = isRateUnspecified(epsilonRateText);
    const bool skipPtbDeviceRate = isRateUnspecified(ptbRateText);
    const bool skipHmpDeviceRate = isRateUnspecified(hmpRateText);
    const bool skipLidarDeviceRate = isRateUnspecified(lidarRateText);
    const bool skipTemperatureDeviceRate = isRateUnspecified(temperatureRateText);
    const int epsilonRate = effectiveRateOrDefault(epsilonRateText, kDefaultEpsilonSampleRateHz, 200);
    const int ptbRate = clampPtbSampleRate(effectiveRateOrDefault(ptbRateText, kDefaultPtbSampleRateHz, kPtbMaxSampleRateHz));
    const int hmpRate = effectiveRateOrDefault(hmpRateText, kDefaultHmpSampleRateHz);
    const int lidarRate = effectiveRateOrDefault(lidarRateText, kDefaultLidarSampleRateHz, 100);
    const int temperatureRate = effectiveRateOrDefault(temperatureRateText, kDefaultTemperatureSampleRateHz, kMaxTemperatureSampleRateHz);
    QSettings settings("VaporView", "MainWindow");
    bool epsilonUsesCustomPacketRates = false;
    const std::map<uint8_t, int> epsilonDesiredPacketRates =
        effectiveEpsilonPacketRates(settings, epsilonRate, &epsilonUsesCustomPacketRates);
    const int epsilonCallbackRate = epsilonPacketCallbackRate(epsilonDesiredPacketRates, epsilonRate);
    const QString epsilonDesiredPacketSignature = epsilonPacketRatesSignature(epsilonDesiredPacketRates);
    const QString epsilonDesiredPacketSummary = epsilonPacketRatesSummary(epsilonDesiredPacketRates);
    const bool epsilonConfigLikelyMatches =
        !epsilonPort.isEmpty() &&
        epsilonPort != selectText &&
        settings.value("epsilon_last_config_apply_version").toInt() == kEpsilonPacketConfigApplyVersion &&
        settings.value("epsilon_last_config_port").toString() == epsilonPort &&
        settings.value("epsilon_last_config_baud").toString() == epsilonBaudText &&
        settings.value("epsilon_last_config_signature").toString() == epsilonDesiredPacketSignature;
    const int selectedDeviceCount =
        ((epsilonPort != selectText && !epsilonPort.isEmpty()) ? 1 : 0) +
        ((ptbPort != selectText && !ptbPort.isEmpty()) ? 1 : 0) +
        ((hmpPort != selectText && !hmpPort.isEmpty()) ? 1 : 0) +
        ((lidarPort != selectText && !lidarPort.isEmpty()) ? 1 : 0) +
        ((temperaturePort != selectText && !temperaturePort.isEmpty()) ? 1 : 0);
    epsilon_sample_rate_ = epsilonRate;
    ptb_sample_rate_ = ptbRate;
    hmp_sample_rate_ = hmpRate;
    lidar_sample_rate_ = lidarRate;
    temperature_sample_rate_ = temperatureRate;

    stopAllCollectors();

    const int connectionProgressSteps = std::max(1, selectedDeviceCount * 4 + 1);
    showStatusTaskProgress(is_english_ ? "Connecting devices..." : "正在连接设备...", 0, connectionProgressSteps);

    connection_thread_ = std::thread([this,
                                      english,
                                      selectText,
                                      epsilonPort,
                                      ptbPort,
                                      hmpPort,
                                      lidarPort,
                                      temperaturePort,
                                      epsilonBaudText,
                                      ptbBaudText,
                                      hmpBaudText,
                                      lidarBaudText,
                                      temperatureBaudText,
                                      epsilonConfigLikelyMatches,
                                      epsilonUsesCustomPacketRates,
                                      epsilonDesiredPacketRates,
                                      epsilonDesiredPacketSignature,
                                      epsilonDesiredPacketSummary,
                                      epsilonCallbackRate,
                                      epsilonRate,
                                      ptbRate,
                                      hmpRate,
                                      lidarRate,
                                      temperatureRate,
                                      skipEpsilonDeviceRate,
                                      skipPtbDeviceRate,
                                      skipHmpDeviceRate,
                                      skipLidarDeviceRate,
                                      skipTemperatureDeviceRate,
                                      connectionProgressSteps]() {
        auto postLog = [this](const QString& message) {
            QMetaObject::invokeMethod(this, [this, message]() { log(message); }, Qt::QueuedConnection);
        };
        auto postProgress = [this, connectionProgressSteps](const QString& label, int value) {
            QMetaObject::invokeMethod(this, [this, label, value, connectionProgressSteps]() {
                showStatusTaskProgress(label, value, connectionProgressSteps);
            }, Qt::QueuedConnection);
        };
        auto finishOnUi = [this](bool connected) {
            QMetaObject::invokeMethod(this, [this, connected]() { finishConnectionAttempt(connected); }, Qt::QueuedConnection);
        };
        auto persistEpsilonConfig = [epsilonPort, epsilonBaudText, epsilonRate, epsilonDesiredPacketSignature]() {
            QSettings settings("VaporView", "MainWindow");
            settings.setValue("epsilon_last_config_port", epsilonPort);
            settings.setValue("epsilon_last_config_baud", epsilonBaudText);
            settings.setValue("epsilon_last_config_rate_hz", epsilonRate);
            settings.setValue("epsilon_last_config_signature", epsilonDesiredPacketSignature);
            settings.setValue("epsilon_last_config_apply_version", kEpsilonPacketConfigApplyVersion);
        };

        CollectorSnapshot collectors;
        collectors.epsilon = std::make_shared<VaporView::EpsilonCollector>();
        collectors.ptb = std::make_shared<VaporView::PtbCollector>();
        collectors.hmp = std::make_shared<VaporView::HmpCollector>();
        collectors.lidar = std::make_shared<VaporView::LidarCollector>();
        collectors.temperature_controller = std::make_shared<VaporView::TemperatureControllerCollector>();
        setCollectors(collectors);

        auto logCallback = [this](const std::string& msg) {
            const QString qmsg = QString::fromStdString(msg);
            QMetaObject::invokeMethod(this, [this, qmsg]() { log(qmsg); }, Qt::QueuedConnection);
        };
        auto cancelCallback = [this]() { return cancel_connection_requested_.load(); };

        collectors.epsilon->setSampleRate(epsilonCallbackRate);
        collectors.ptb->setSampleRate(ptbRate);
        collectors.hmp->setSampleRate(hmpRate);
        collectors.lidar->setSampleRate(lidarRate);
        collectors.temperature_controller->setSampleRate(temperatureRate);
        collectors.temperature_controller->setSlaveAddress(static_cast<uint8_t>(rememberedTemperatureSlaveAddress()));

        collectors.epsilon->setLogCallback(logCallback);
        collectors.ptb->setLogCallback(logCallback);
        collectors.hmp->setLogCallback(logCallback);
        collectors.lidar->setLogCallback(logCallback);
        collectors.temperature_controller->setLogCallback(logCallback);
        collectors.epsilon->setCancelCallback(cancelCallback);
        collectors.ptb->setCancelCallback(cancelCallback);
        collectors.hmp->setCancelCallback(cancelCallback);
        collectors.lidar->setCancelCallback(cancelCallback);
        collectors.temperature_controller->setCancelCallback(cancelCallback);
        collectors.epsilon->setRawFrameCallback([this](uint64_t hostTimestampUs, uint8_t packetId, uint8_t serialNumber, const uint8_t* packet_data, size_t size) {
            if (!recording_thread_running_.load())
            {
                return;
            }

            writeUnifiedRawRecord(raw_epsilon_file_.get(),
                                  raw_epsilon_record_count_,
                                  kRawSourceEpsilon,
                                  packetId,
                                  serialNumber,
                                  static_cast<quint64>(hostTimestampUs),
                                  packet_data,
                                  size);
        });
        collectors.ptb->setRawResponseCallback([this](uint64_t hostTimestampUs, const uint8_t* responseData, size_t size) {
            if (!recording_thread_running_.load())
            {
                return;
            }
            writeUnifiedRawRecord(raw_ptb_file_.get(),
                                  raw_ptb_record_count_,
                                  kRawSourcePtb,
                                  kRawRecordTypeGeneric,
                                  0u,
                                  static_cast<quint64>(hostTimestampUs),
                                  responseData,
                                  size);
        });
        collectors.hmp->setRawResponseCallback([this](uint64_t hostTimestampUs, const uint8_t* responseData, size_t size) {
            if (!recording_thread_running_.load())
            {
                return;
            }
            writeUnifiedRawRecord(raw_hmp_file_.get(),
                                  raw_hmp_record_count_,
                                  kRawSourceHmp,
                                  0x03u,
                                  0u,
                                  static_cast<quint64>(hostTimestampUs),
                                  responseData,
                                  size);
        });
        collectors.lidar->setRawFrameCallback([this](uint64_t hostTimestampUs, VaporView::LidarProtocol protocol, const uint8_t* frame, size_t size) {
            if (!recording_thread_running_.load())
            {
                return;
            }
            writeUnifiedRawRecord(raw_lidar_file_.get(),
                                  raw_lidar_record_count_,
                                  kRawSourceLidar,
                                  static_cast<quint16>(protocol),
                                  0u,
                                  static_cast<quint64>(hostTimestampUs),
                                  frame,
                                  size);
        });

        int total_devices = 0;
        int connected_devices = 0;
        int progressStep = 0;

        auto cancelAttempt = [&]() {
            stopAllCollectors();
            postLog(english ? "Connection canceled" : "连接已取消");
            finishOnUi(false);
        };
        auto abortIfRequested = [&]() {
            if (!shouldAbortConnectionAttempt())
            {
                return false;
            }
            cancelAttempt();
            return true;
        };
        auto connectCollector = [&](const QString& tag,
                                    const QString& port,
                                    const QString& baudText,
                                    auto* collector,
                                    const VaporView::SerialConfig& config,
                                    auto&& onReady) -> int {
            if (port == selectText || port.isEmpty())
            {
                postLog(QString(english ? "[%1] Skipped (not selected)" : "[%1] 跳过 (未选择)").arg(tag));
                return 0;
            }

            total_devices++;
            postProgress(english ? QString("Connecting %1...").arg(tag) : QString("正在连接 %1...").arg(tag), ++progressStep);
            postLog(QString(english ? "[%1] Checking port: %2" : "[%1] 检查端口: %2").arg(tag, port));
            if (abortIfRequested()) return -1;

            postProgress(english ? QString("Opening %1...").arg(tag) : QString("正在打开 %1...").arg(tag), ++progressStep);
            postLog(QString(english ? "[%1] Port selected, connecting..." : "[%1] 已选择端口，正在连接...").arg(tag));
            if (abortIfRequested()) return -1;

            if (!collector->start(port.toStdString(), config))
            {
                postLog(QString(english ? "[%1] Failed to open port: %2" : "[%1] 打开端口失败: %2")
                            .arg(tag, QString::fromStdString(collector->getLastError())));
                return 0;
            }

            postProgress(english ? QString("Checking %1 response...").arg(tag) : QString("正在检测 %1 响应...").arg(tag), ++progressStep);
            postLog(QString(english ? "[%1] Serial port opened, checking device response..." : "[%1] 串口已打开，正在检测设备响应...").arg(tag));
            if (abortIfRequested()) return -1;

            if (!collector->checkDeviceResponse())
            {
                if (abortIfRequested()) return -1;
                postLog(QString(english ? "[%1] Device not responding! Check power and cables." : "[%1] 设备无响应！请检查电源和连接线。").arg(tag));
                collector->stop();
                return 0;
            }

            postProgress(english ? QString("Starting %1 stream...").arg(tag) : QString("正在启动 %1 数据流...").arg(tag), ++progressStep);
            postLog(QString(english ? "[%1] Device responding, connected: %2 @ %3 baud" : "[%1] 设备响应正常，连接成功: %2 @ %3 波特率")
                        .arg(tag, port, baudText));
            if (!onReady())
            {
                collector->stop();
                return 0;
            }

            connected_devices++;
            return 1;
        };

        postLog(english ? "========== Starting Connection ==========" : "========== 开始连接 ==========");
        if (abortIfRequested()) return;

        if (connectCollector("EPSILON", epsilonPort, epsilonBaudText, collectors.epsilon.get(),
                             VaporView::SerialConfig::N81(epsilonBaudText.toInt()),
                             [&]() {
                                 collectors.epsilon->setDataCallback([this]() { QMetaObject::invokeMethod(this, "onEpsilonDataReady", Qt::QueuedConnection); });
                                 collectors.epsilon->setSampleRate(epsilonCallbackRate);
                                 if (skipEpsilonDeviceRate)
                                 {
                                     postLog(english ? "[EPSILON] Skip output-rate command; using the current device output." : "[EPSILON] 跳过输出频率下发，使用设备当前输出。");
                                 }
                                 else if (epsilonConfigLikelyMatches)
                                 {
                                     postLog(QString(english
                                                         ? "[EPSILON] Using the last saved %1 profile; skipping automatic reconfiguration."
                                                         : "[EPSILON] 使用上次已保存的%1配置，跳过自动重配。")
                                                 .arg(epsilonUsesCustomPacketRates
                                                          ? (english ? "custom packet-rate" : "自定义包频率")
                                                          : (english ? "grouped output-rate" : "分组输出频率")));
                                 }
                                 else if (!collectors.epsilon->setOutputPacketRates(epsilonDesiredPacketRates))
                                 {
                                     postLog(QString(english
                                                         ? "[EPSILON] Failed to configure the %1 profile: %2"
                                                         : "[EPSILON] 配置%1失败：%2")
                                                 .arg(epsilonUsesCustomPacketRates
                                                          ? (english ? "custom packet-rate" : "自定义包频率")
                                                          : (english ? "grouped output-rate" : "分组输出频率"))
                                                 .arg(epsilonDesiredPacketSummary));
                                     return false;
                                 }
                                 else
                                 {
                                     persistEpsilonConfig();
                                     postLog(QString(english
                                                         ? "[EPSILON] Applied %1 profile: %2"
                                                         : "[EPSILON] 已应用%1配置：%2")
                                                 .arg(epsilonUsesCustomPacketRates
                                                          ? (english ? "custom packet-rate" : "自定义包频率")
                                                          : (english ? "grouped output-rate" : "分组输出频率"))
                                                 .arg(epsilonDesiredPacketSummary));
                                 }
                                 if (collectors.epsilon->startStreaming()) return true;
                                 postLog(english ? "[EPSILON] Failed to start navigation stream." : "[EPSILON] 启动导航数据流失败。");
                                 return false;
                             }) < 0) return;

        if (connectCollector("PTB", ptbPort, ptbBaudText, collectors.ptb.get(),
                             VaporView::SerialConfig::E71(ptbBaudText.toInt()),
                             [&]() {
                                 collectors.ptb->setDataCallback([this]() { QMetaObject::invokeMethod(this, "onPtbDataReady", Qt::QueuedConnection); });
                                 collectors.ptb->setSampleRate(ptbRate);
                                 if (skipPtbDeviceRate)
                                 {
                                     postLog(english ? "[PTB] Skip sample-rate command; using the current device output." : "[PTB] 跳过采样频率下发，使用设备当前输出。");
                                 }
                                 else if (!collectors.ptb->setDeviceSampleRate(ptbRate))
                                 {
                                     postLog(QString(english
                                                       ? "[PTB] Failed to set sample rate to %1 Hz."
                                                       : "[PTB] 采样频率设置为 %1 Hz 失败。")
                                                 .arg(ptbRate));
                                     return false;
                                 }
                                 else
                                 {
                                     postLog(QString(english ? "[PTB] Sample rate set to %1 Hz" : "[PTB] 采样频率设置为 %1 Hz").arg(ptbRate));
                                 }
                                 if (collectors.ptb->startStreaming()) return true;
                                 postLog(english ? "[PTB] Failed to start data stream." : "[PTB] 启动数据流失败。");
                                 return false;
                             }) < 0) return;

        if (connectCollector("HMP", hmpPort, hmpBaudText, collectors.hmp.get(),
                             VaporView::SerialConfig::N82(hmpBaudText.toInt()),
                             [&]() {
                                 collectors.hmp->setDataCallback([this]() { QMetaObject::invokeMethod(this, "onHmpDataReady", Qt::QueuedConnection); });
                                 collectors.hmp->setSampleRate(hmpRate);
                                 if (skipHmpDeviceRate)
                                 {
                                     postLog(english ? "[HMP] Polling-rate selection left unset; using the default host polling rate." : "[HMP] 轮询频率保持不设定，使用默认主机轮询频率。");
                                 }
                                 else
                                 {
                                     postLog(QString(english ? "[HMP] Sample rate set to %1 Hz" : "[HMP] 采样频率设置为 %1 Hz").arg(hmpRate));
                                 }
                                 if (collectors.hmp->startStreaming()) return true;
                                 postLog(english ? "[HMP] Failed to start data stream." : "[HMP] 启动数据流失败。");
                                 return false;
                             }) < 0) return;

        if (connectCollector("LIDAR", lidarPort, lidarBaudText, collectors.lidar.get(),
                             VaporView::SerialConfig::N81(lidarBaudText.toInt()),
                             [&]() {
                                 collectors.lidar->setDataCallback([this]() { QMetaObject::invokeMethod(this, "onLidarDataReady", Qt::QueuedConnection); });
                                 collectors.lidar->setSampleRate(lidarRate);
                                 if (skipLidarDeviceRate)
                                 {
                                     postLog(english ? "[Lidar] Skip output-rate command; using device default/adaptive output." : "[Lidar] 跳过输出频率下发，使用设备默认/自适应输出。");
                                 }
                                 else if (!collectors.lidar->setDeviceSampleRate(lidarRate))
                                 {
                                     postLog(QString(english ? "[Lidar] Failed to apply output rate %1 Hz, using device default." : "[Lidar] 应用 %1 Hz 输出频率失败，使用设备默认输出。").arg(lidarRate));
                                 }
                                 else
                                 {
                                     postLog(QString(english ? "[Lidar] Output rate set to %1 Hz or host-side limit updated" : "[Lidar] 输出频率已设置为 %1 Hz，或已更新主机侧限频").arg(lidarRate));
                                 }
                                 if (collectors.lidar->startStreaming()) return true;
                                 postLog(english ? "[Lidar] Failed to start data stream." : "[Lidar] 启动数据流失败。");
                                 return false;
                             }) < 0) return;

        if (connectCollector("RD105", temperaturePort, temperatureBaudText, collectors.temperature_controller.get(),
                             VaporView::SerialConfig::N81(temperatureBaudText.toInt()),
                             [&]() {
                                 collectors.temperature_controller->setDataCallback([this]() { QMetaObject::invokeMethod(this, "onTemperatureControllerDataReady", Qt::QueuedConnection); });
                                 collectors.temperature_controller->setSampleRate(temperatureRate);
                                 if (skipTemperatureDeviceRate)
                                 {
                                     postLog(english ? "[RD105] Polling-rate selection left unset; using the default host polling rate." : "[RD105] 轮询频率保持不设定，使用默认主机轮询频率。");
                                 }
                                 else
                                 {
                                     postLog(QString(english ? "[RD105] Polling rate set to %1 Hz" : "[RD105] 轮询频率设置为 %1 Hz").arg(temperatureRate));
                                 }
                                 if (collectors.temperature_controller->startStreaming()) return true;
                                 postLog(english ? "[RD105] Failed to start temperature controller polling." : "[RD105] 启动温控器轮询失败。");
                                 return false;
                             }) < 0) return;

        postProgress(english ? "Finalizing connection..." : "正在完成连接...", connectionProgressSteps);
        postLog(QString(english ? "========== Connection Summary: %1/%2 devices connected ==========" : "========== 连接摘要: %1/%2 设备已连接 ==========").arg(connected_devices).arg(total_devices));
        if (connected_devices == 0)
        {
            postLog(english ? "No ports connected" : "没有端口连接成功");
            finishOnUi(false);
            return;
        }

        finishOnUi(true);
    });
}

void MainWindow::onDisconnectClicked()
{
    if (isRemoteSkyMode())
    {
        log(is_english_ ? "Disconnecting Remote Sky telemetry..." : "正在断开天空端数传...");
        if (ground_telemetry_service_)
        {
            ground_telemetry_service_->close();
        }
        remote_recording_state_ = 0;
        clearRemoteSkyDataUi();
        updateConnectionStatus(false);
        log(is_english_ ? "Remote Sky disconnected" : "天空端数传已断开");
        return;
    }

    log(is_english_ ? "Disconnecting..." : "正在断开...");

    stopRecording(true);
    stopAllCollectors();
    invalidateTemperatureControllerDataUi();
    if (connection_thread_.joinable())
    {
        connection_thread_.join();
    }
    finishConnectionAttempt(false);
    log(is_english_ ? "Disconnected" : "已断开");
}

void MainWindow::onCancelConnectClicked()
{
    if (!connection_attempt_in_progress_)
    {
        return;
    }

    cancel_connection_requested_.store(true);
    log(is_english_ ? "Cancel requested, stopping connection attempt..." : "已请求取消，正在停止连接流程...");
    QApplication::processEvents(QEventLoop::AllEvents);
}

void MainWindow::onGnssDataReady()
{
    const CollectorSnapshot collectors = snapshotCollectors();
    if (collectors.gnss)
    {
        current_gnss_ = collectors.gnss->getLatestData();
    }
}

void MainWindow::onEpsilonDataReady()
{
    const CollectorSnapshot collectors = snapshotCollectors();
    if (collectors.epsilon)
    {
        current_epsilon_ = collectors.epsilon->getLatestData();
    }
#ifdef VAPORVIEW_HAS_OSGEARTH
    maybeForwardMap3DSample(current_epsilon_, currentTimestampUs());
#endif
}

void MainWindow::onImuDataReady()
{
    const CollectorSnapshot collectors = snapshotCollectors();
    if (collectors.imu)
    {
        current_imu_ = collectors.imu->getLatestData();
    }
}

void MainWindow::onPtbDataReady()
{
    const CollectorSnapshot collectors = snapshotCollectors();
    if (collectors.ptb)
    {
        current_ptb_ = collectors.ptb->getLatestData();
    }
}

void MainWindow::onHmpDataReady()
{
    const CollectorSnapshot collectors = snapshotCollectors();
    if (collectors.hmp)
    {
        current_hmp_ = collectors.hmp->getLatestData();
    }
}

void MainWindow::onLidarDataReady()
{
    const CollectorSnapshot collectors = snapshotCollectors();
    if (collectors.lidar)
    {
        current_lidar_ = collectors.lidar->getLatestData();
    }
}

void MainWindow::onTemperatureControllerDataReady()
{
    const CollectorSnapshot collectors = snapshotCollectors();
    if (collectors.temperature_controller)
    {
        const VaporView::TemperatureControllerData latest = collectors.temperature_controller->getLatestData();
        if (latest.timestamp < current_temperature_controller_.timestamp)
        {
            return;
        }
        current_temperature_controller_ = latest;
    }
}

void MainWindow::onRefreshTimer()
{
    if (isRemoteSkyMode())
    {
        refreshRemoteSkyDataUi();
        return;
    }

    const CollectorSnapshot collectors = snapshotCollectors();

    if (epsilon_panel_) epsilon_panel_->updateData(current_epsilon_);
    if (ptb_panel_) ptb_panel_->updateData(current_ptb_);
    if (hmp_panel_) hmp_panel_->updateData(current_hmp_);
    if (lidar_panel_) lidar_panel_->updateData(current_lidar_);
    updateEnvironmentStatusIcons(current_lidar_.valid, current_ptb_.valid, current_hmp_.valid);

    if (collectors.epsilon && epsilon_panel_)
    {
        epsilon_panel_->updateRate(collectors.epsilon->getActualRate());
    }
    if (collectors.ptb)
    {
        const double rate = collectors.ptb->getActualRate();
        ptb_panel_->updateRate(rate);
    }
    if (collectors.hmp)
    {
        const double rate = collectors.hmp->getActualRate();
        hmp_panel_->updateRate(rate);
    }
    if (collectors.lidar)
    {
        const double rate = collectors.lidar->getActualRate();
        lidar_panel_->updateRate(rate);
    }
    if (collectors.temperature_controller && temperature_controller_panel_)
    {
        temperature_controller_panel_->updateRate(collectors.temperature_controller->getActualRate());
    }
    if (temperature_controller_panel_)
    {
        temperature_controller_panel_->updateData(current_temperature_controller_);
        if (temperature_overview_panel_) temperature_overview_panel_->updateData(current_temperature_controller_);
    }
    updateHomeDeviceStatusCapsules();
}

void MainWindow::onRemoteBasicTelemetryUpdated(const VaporView::TelemetryBasic& telemetry)
{
    noteRemotePacket(VaporView::MsgType::TelemetryBasic);
    const auto now = std::chrono::steady_clock::now();
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    auto hasFlag = [&telemetry](quint32 flag) {
        return (telemetry.validity_flags & flag) != 0;
    };

    current_epsilon_ = VaporView::EpsilonData();
    const double unknown = std::numeric_limits<double>::quiet_NaN();
    current_epsilon_.vel_n_mps = unknown;
    current_epsilon_.vel_e_mps = unknown;
    current_epsilon_.vel_d_mps = unknown;
    current_epsilon_.imu_acc_x_mps2 = unknown;
    current_epsilon_.imu_acc_y_mps2 = unknown;
    current_epsilon_.imu_acc_z_mps2 = unknown;
    current_epsilon_.imu_gyr_x_radps = unknown;
    current_epsilon_.imu_gyr_y_radps = unknown;
    current_epsilon_.imu_gyr_z_radps = unknown;
    current_epsilon_.roll_deg = unknown;
    current_epsilon_.pitch_deg = unknown;
    current_epsilon_.yaw_deg = unknown;
    current_epsilon_.hacc_m = unknown;
    current_epsilon_.vacc_m = unknown;
    if (hasFlag(VaporView::BasicHasEpsilonTime) ||
        hasFlag(VaporView::BasicHasPosition) ||
        hasFlag(VaporView::BasicHasEcef))
    {
        current_epsilon_.valid = true;
        current_epsilon_.timestamp = now;
        current_epsilon_.device_timestamp_us = telemetry.epsilon_time_us;
        current_epsilon_.utc_unix_s = telemetry.host_time_us / 1000000ULL;
        current_epsilon_.utc_microseconds = static_cast<quint32>(telemetry.host_time_us % 1000000ULL);
        int gnssFixCode = static_cast<int>(telemetry.gnss_fix_code);
        if (gnssFixCode == 0 && telemetry.filter_status_bits != 0)
        {
            gnssFixCode = static_cast<int>((telemetry.filter_status_bits >> 4) & 0x0F);
        }
        current_epsilon_.gnss_fix_code = gnssFixCode;
        current_epsilon_.gnss_fix_text = epsilonGnssFixTextForCode(gnssFixCode);
        if (hasFlag(VaporView::BasicHasPosition))
        {
            current_epsilon_.latitude_deg = telemetry.latitude_deg;
            current_epsilon_.longitude_deg = telemetry.longitude_deg;
            current_epsilon_.height_m = telemetry.height_m;
        }
        if (hasFlag(VaporView::BasicHasEcef))
        {
            current_epsilon_.ecef_x_m = telemetry.ecef_x_m;
            current_epsilon_.ecef_y_m = telemetry.ecef_y_m;
            current_epsilon_.ecef_z_m = telemetry.ecef_z_m;
        }
        current_epsilon_.system_status_bits = telemetry.status_bits;
        current_epsilon_.filter_status_bits = telemetry.filter_status_bits;
        current_epsilon_.update_status_bits = telemetry.update_status_bits;
        if (hasFlag(VaporView::BasicHasGnssQuality))
        {
            current_epsilon_.gnss_satellites = telemetry.gnss_satellites;
            current_epsilon_.hdop = telemetry.hdop;
            current_epsilon_.vdop = telemetry.vdop;
            current_epsilon_.hacc_m = telemetry.hacc_m;
            current_epsilon_.vacc_m = telemetry.vacc_m;
            current_epsilon_.heading_valid = telemetry.heading_valid;
        }
        if (hasFlag(VaporView::BasicHasNedVelocity))
        {
            current_epsilon_.vel_n_mps = telemetry.vel_n_mps;
            current_epsilon_.vel_e_mps = telemetry.vel_e_mps;
            current_epsilon_.vel_d_mps = telemetry.vel_d_mps;
        }
        if (hasFlag(VaporView::BasicHasImu))
        {
            current_epsilon_.imu_acc_x_mps2 = telemetry.imu_acc_x_mps2;
            current_epsilon_.imu_acc_y_mps2 = telemetry.imu_acc_y_mps2;
            current_epsilon_.imu_acc_z_mps2 = telemetry.imu_acc_z_mps2;
            current_epsilon_.imu_gyr_x_radps = telemetry.imu_gyr_x_radps;
            current_epsilon_.imu_gyr_y_radps = telemetry.imu_gyr_y_radps;
            current_epsilon_.imu_gyr_z_radps = telemetry.imu_gyr_z_radps;
        }
        if (hasFlag(VaporView::BasicHasAttitude))
        {
            current_epsilon_.roll_deg = telemetry.roll_deg;
            current_epsilon_.pitch_deg = telemetry.pitch_deg;
            current_epsilon_.yaw_deg = telemetry.yaw_deg;
        }
        if (hasFlag(VaporView::BasicHasEpsilonDiagnostics))
        {
            current_epsilon_.raw_frame_count = telemetry.raw_frame_count;
            current_epsilon_.dropped_frame_count = telemetry.dropped_frame_count;
            current_epsilon_.imu_packet_rate_hz = telemetry.imu_packet_rate_hz;
            current_epsilon_.ahrs_packet_rate_hz = telemetry.ahrs_packet_rate_hz;
            current_epsilon_.insgps_packet_rate_hz = telemetry.insgps_packet_rate_hz;
            current_epsilon_.sys_state_packet_rate_hz = telemetry.sys_state_packet_rate_hz;
            current_epsilon_.raw_gnss_packet_rate_hz = telemetry.raw_gnss_packet_rate_hz;
            current_epsilon_.satellite_packet_rate_hz = telemetry.satellite_packet_rate_hz;
            current_epsilon_.geodetic_packet_rate_hz = telemetry.geodetic_packet_rate_hz;
            current_epsilon_.ecef_packet_rate_hz = telemetry.ecef_packet_rate_hz;
        }
        remote_last_data_ms_.insert(VaporView::SkyDeviceId::Epsilon, nowMs);
#ifdef VAPORVIEW_HAS_OSGEARTH
        if (hasFlag(VaporView::BasicHasPosition))
        {
            maybeForwardMap3DSample(current_epsilon_, telemetry.host_time_us);
        }
        else
        {
            pending_map3d_samples_.clear();
            if (map3d_flush_timer_)
            {
                map3d_flush_timer_->stop();
            }
            noteMap3DSampleDrop(QStringLiteral("Remote"),
                                QStringLiteral("missing BasicHasPosition"),
                                telemetry.host_time_us);
        }
#endif
    }
    else
    {
        remote_last_data_ms_.remove(VaporView::SkyDeviceId::Epsilon);
    }

    current_lidar_ = VaporView::LidarData();
    if (hasFlag(VaporView::BasicHasLidar))
    {
        current_lidar_.valid = true;
        current_lidar_.timestamp = now;
        current_lidar_.distance_m = telemetry.lidar_height_m;
        if (hasFlag(VaporView::BasicHasLidarStrength))
        {
            current_lidar_.signal_strength = telemetry.lidar_signal_strength;
        }
        remote_last_data_ms_.insert(VaporView::SkyDeviceId::Lidar, nowMs);
    }
    else
    {
        current_lidar_.error_message = remoteNoDataText(is_english_).toStdString();
        remote_last_data_ms_.remove(VaporView::SkyDeviceId::Lidar);
    }

    current_hmp_ = VaporView::HmpData();
    if (hasFlag(VaporView::BasicHasTemperature) && hasFlag(VaporView::BasicHasHumidity))
    {
        current_hmp_.valid = true;
        current_hmp_.timestamp = now;
        current_hmp_.temperature = telemetry.temperature_c;
        current_hmp_.humidity = telemetry.humidity_percent;
        remote_last_data_ms_.insert(VaporView::SkyDeviceId::Hmp, nowMs);
    }
    else
    {
        current_hmp_.error_message = remoteNoDataText(is_english_).toStdString();
        remote_last_data_ms_.remove(VaporView::SkyDeviceId::Hmp);
    }

    current_ptb_ = VaporView::PtbData();
    if (hasFlag(VaporView::BasicHasPressure))
    {
        current_ptb_.valid = true;
        current_ptb_.timestamp = now;
        current_ptb_.pressure_hpa = telemetry.pressure_hpa;
        remote_last_data_ms_.insert(VaporView::SkyDeviceId::Ptb, nowMs);
    }
    else
    {
        current_ptb_.error_message = remoteNoDataText(is_english_).toStdString();
        remote_last_data_ms_.remove(VaporView::SkyDeviceId::Ptb);
    }

    refreshRemoteSkyDataUi();
}

#ifdef VAPORVIEW_HAS_OSGEARTH
VaporView::Geo::NavSample MainWindow::map3DSampleFromEpsilon(const VaporView::EpsilonData& epsilonData,
                                                             quint64 recordTimestampUs) const
{
    VaporView::Geo::NavSample sample;
    sample.recordTimestampUs = static_cast<qint64>(recordTimestampUs);
    sample.deviceTimestampUs = static_cast<qint64>(epsilonData.device_timestamp_us);
    sample.latDeg = epsilonData.latitude_deg;
    sample.lonDeg = epsilonData.longitude_deg;
    sample.heightM = epsilonData.height_m;
    sample.ecefXM = epsilonData.ecef_x_m;
    sample.ecefYM = epsilonData.ecef_y_m;
    sample.ecefZM = epsilonData.ecef_z_m;
    if (std::isfinite(epsilonData.ned_n_m) &&
        std::isfinite(epsilonData.ned_e_m) &&
        std::isfinite(epsilonData.ned_d_m) &&
        (std::abs(epsilonData.ned_n_m) > 1e-6 ||
         std::abs(epsilonData.ned_e_m) > 1e-6 ||
         std::abs(epsilonData.ned_d_m) > 1e-6))
    {
        sample.nedNM = epsilonData.ned_n_m;
        sample.nedEM = epsilonData.ned_e_m;
        sample.nedDM = epsilonData.ned_d_m;
    }
    sample.velNMps = epsilonData.vel_n_mps;
    sample.velEMps = epsilonData.vel_e_mps;
    sample.velDMps = epsilonData.vel_d_mps;
    sample.rollDeg = epsilonData.roll_deg;
    sample.pitchDeg = epsilonData.pitch_deg;
    sample.yawDeg = epsilonData.yaw_deg;
    sample.quatW = epsilonData.quat_w;
    sample.quatX = epsilonData.quat_x;
    sample.quatY = epsilonData.quat_y;
    sample.quatZ = epsilonData.quat_z;
    sample.satellites = epsilonData.gnss_satellites;
    sample.hdop = epsilonData.hdop;
    sample.vdop = epsilonData.vdop;
    sample.diffAgeS = epsilonData.diff_age_s;

    if (epsilonData.gnss_fix_code <= 0)
    {
        sample.fixQuality = VaporView::Geo::FixQuality::Invalid;
    }
    else if (epsilonData.gnss_fix_code >= 6)
    {
        sample.fixQuality = VaporView::Geo::FixQuality::Fixed;
    }
    else if (epsilonData.gnss_fix_code == 5)
    {
        sample.fixQuality = VaporView::Geo::FixQuality::Float;
    }
    else if (epsilonData.gnss_fix_code == 2)
    {
        sample.fixQuality = VaporView::Geo::FixQuality::Dgps;
    }
    else
    {
        sample.fixQuality = VaporView::Geo::FixQuality::Single;
    }

    return sample;
}

void MainWindow::maybeForwardMap3DSample(const VaporView::EpsilonData& epsilonData, quint64 recordTimestampUs)
{
    if (!map3d_window_ || !map3d_window_->isVisible())
    {
        pending_map3d_samples_.clear();
        if (map3d_flush_timer_)
        {
            map3d_flush_timer_->stop();
        }
        return;
    }

    if (!epsilonData.valid)
    {
        pending_map3d_samples_.clear();
        if (map3d_flush_timer_)
        {
            map3d_flush_timer_->stop();
        }
        noteMap3DSampleDrop(QStringLiteral("Live"),
                            QStringLiteral("epsilon invalid"),
                            recordTimestampUs);
        return;
    }

    const VaporView::Geo::NavSample sample = map3DSampleFromEpsilon(epsilonData, recordTimestampUs);
    if (!sample.hasLlh())
    {
        pending_map3d_samples_.clear();
        if (map3d_flush_timer_)
        {
            map3d_flush_timer_->stop();
        }
        noteMap3DSampleDrop(QStringLiteral("Live"),
                            QStringLiteral("missing LLH"),
                            recordTimestampUs);
        return;
    }
    last_map3d_drop_reason_.clear();
    if (pending_map3d_samples_.empty())
    {
        pending_map3d_samples_.push_back(sample);
    }
    else
    {
        pending_map3d_samples_.back() = sample;
    }
    if (map3d_flush_timer_ && !map3d_flush_timer_->isActive())
    {
        map3d_flush_timer_->start();
    }
}

void MainWindow::noteMap3DSampleDrop(const QString& source, const QString& reason, quint64 recordTimestampUs)
{
    last_map3d_drop_reason_ = reason;
    if (map3d_window_ && map3d_window_->isVisible())
    {
        map3d_window_->noteLiveSampleDrop(source, reason, static_cast<qint64>(recordTimestampUs));
    }
}

void MainWindow::flushMap3DSamples()
{
    if (!map3d_window_ || !map3d_window_->isVisible())
    {
        pending_map3d_samples_.clear();
        if (map3d_flush_timer_)
        {
            map3d_flush_timer_->stop();
        }
        return;
    }

    if (pending_map3d_samples_.empty())
    {
        if (map3d_flush_timer_)
        {
            map3d_flush_timer_->stop();
        }
        return;
    }

    std::vector<VaporView::Geo::NavSample> samples;
    samples.swap(pending_map3d_samples_);
    map3d_window_->appendSamples(samples);

    if (map3d_flush_timer_ && pending_map3d_samples_.empty())
    {
        map3d_flush_timer_->stop();
    }
}

#ifdef VAPORVIEW_MAIN_WINDOW_TESTING
int MainWindow::testPendingMap3DSampleCount() const
{
    return static_cast<int>(pending_map3d_samples_.size());
}

qint64 MainWindow::testLatestPendingMap3DRecordTimestampUs() const
{
    return pending_map3d_samples_.empty() ? -1 : pending_map3d_samples_.back().recordTimestampUs;
}

bool MainWindow::testMap3DFlushTimerActive() const
{
    return map3d_flush_timer_ && map3d_flush_timer_->isActive();
}

QString MainWindow::testLastMap3DDropReason() const
{
    return last_map3d_drop_reason_;
}

void MainWindow::testMaybeForwardMap3DSampleForMap3D(const VaporView::EpsilonData& epsilonData,
                                                     quint64 recordTimestampUs)
{
    maybeForwardMap3DSample(epsilonData, recordTimestampUs);
}

void MainWindow::testOnRemoteBasicTelemetryUpdatedForMap3D(const VaporView::TelemetryBasic& telemetry)
{
    onRemoteBasicTelemetryUpdated(telemetry);
}
#endif
#endif

void MainWindow::onRemoteWaveformUpdated(const VaporView::DownsampledWaveform& waveform)
{
    noteRemotePacket(VaporView::MsgType::WaveformDownsampled);
    noteRemoteWaveformPacket(waveform.channel_id);
    if (!remote_wave_stream_requested_)
    {
        return;
    }
    remote_last_data_ms_.insert(VaporView::SkyDeviceId::WaveTcp, QDateTime::currentMSecsSinceEpoch());
    if (tcp_wave_panel_)
    {
        if (waveform.channel_id == 1)
        {
            tcp_wave_panel_->injectRemoteRawSignalFrame(waveform.host_time_us, waveform.samples);
        }
        else if (waveform.channel_id == 4)
        {
            tcp_wave_panel_->injectRemoteSecondHarmonicFrame(waveform.host_time_us, waveform.samples);
        }
    }
}

void MainWindow::onRemoteWaveformFeatureUpdated(const VaporView::WaveformFeature& feature)
{
    noteRemotePacket(VaporView::MsgType::WaveformFeature);
    if (!remote_wave_stream_requested_)
    {
        return;
    }
    remote_last_data_ms_.insert(VaporView::SkyDeviceId::WaveTcp, QDateTime::currentMSecsSinceEpoch());
    if (tcp_wave_panel_)
    {
        tcp_wave_panel_->injectRemoteWaveformFeature(feature);
    }
}

void MainWindow::onRemoteTelemetryStatusUpdated(const VaporView::TelemetryStatus& status)
{
    noteRemotePacket(VaporView::MsgType::TelemetryStatus);
    if (!remote_sky_online_)
    {
        remote_sky_online_ = true;
        log(is_english_ ? "Remote Sky handshake confirmed" : "天空端握手成功");
    }
    remote_status_ = status;
    const quint64 rawTotal =
        status.raw_epsilon_record_count +
        status.raw_ptb_record_count +
        status.raw_hmp_record_count +
        status.raw_lidar_record_count +
        status.raw_tcp_wave_record_count;
    if (!status.session_name.isEmpty() ||
        status.telemetry_record_count > 0 ||
        status.waveform_feature_record_count > 0 ||
        status.waveform_snapshot_record_count > 0 ||
        rawTotal > 0)
    {
        last_remote_recording_status_ = status;
        has_last_remote_recording_status_ = true;
    }
    if (tcp_wave_panel_)
    {
        tcp_wave_panel_->setRemoteFeatureRateHz(status.feature_rate_hz);
    }
    remote_last_status_ms_ = QDateTime::currentMSecsSinceEpoch();
    remote_recording_state_ = status.recording_state;
    for (const VaporView::DeviceStatusItem& item : status.devices)
    {
        remote_device_states_.insert(item.device_id, item.state);
        updateRemoteDeviceButtonText(item.device_id, item.state);
        if (item.state != VaporView::DeviceState::Connected)
        {
            remote_last_data_ms_.remove(item.device_id);
            if (item.device_id == VaporView::SkyDeviceId::Epsilon)
            {
                current_epsilon_ = VaporView::EpsilonData();
            }
            else if (item.device_id == VaporView::SkyDeviceId::Ptb)
            {
                current_ptb_ = VaporView::PtbData();
                current_ptb_.error_message = remoteDisconnectedText(is_english_).toStdString();
            }
            else if (item.device_id == VaporView::SkyDeviceId::Hmp)
            {
                current_hmp_ = VaporView::HmpData();
                current_hmp_.error_message = remoteDisconnectedText(is_english_).toStdString();
            }
            else if (item.device_id == VaporView::SkyDeviceId::Lidar)
            {
                current_lidar_ = VaporView::LidarData();
                current_lidar_.error_message = remoteDisconnectedText(is_english_).toStdString();
            }
            else if (item.device_id == VaporView::SkyDeviceId::TemperatureController)
            {
                invalidateTemperatureControllerDataUi();
            }
        }
    }
    if (remote_wave_stream_auto_start_ &&
        !remote_wave_stream_requested_ &&
        !remote_wave_stream_enable_pending_ &&
        remote_device_states_.value(VaporView::SkyDeviceId::WaveTcp, VaporView::DeviceState::Disconnected) == VaporView::DeviceState::Connected &&
        ground_telemetry_service_ && ground_telemetry_service_->isOpen())
    {
        remote_wave_stream_enable_pending_ = true;
        ground_telemetry_service_->sendCommand(VaporView::CommandId::EnableWaveformStreaming);
    }
    refreshRemoteSkyDataUi();
    status_label_->setText(QString(is_english_ ? "Remote Sky Online | %1" : "天空端在线 | %1").arg(status.session_name));
    status_label_->setProperty("status", "connected");
    status_label_->style()->unpolish(status_label_);
    status_label_->style()->polish(status_label_);
    updateRecordingStatusLabel();
    updateSourceModeUi();
}

void MainWindow::onRemoteTemperatureControllerStatusUpdated(const VaporView::TemperatureControllerData& controllerData)
{
    noteRemotePacket(VaporView::MsgType::TemperatureControllerStatus);
    current_temperature_controller_ = controllerData;
    remote_last_data_ms_.insert(VaporView::SkyDeviceId::TemperatureController, QDateTime::currentMSecsSinceEpoch());
    if (temperature_controller_panel_)
    {
        temperature_controller_panel_->updateData(current_temperature_controller_);
        if (temperature_overview_panel_) temperature_overview_panel_->updateData(current_temperature_controller_);
    }
}

void MainWindow::onRemoteCommandAckReceived(const VaporView::CommandAck& ack)
{
    const bool ok = ack.result == 0;
    const QString commandName = VaporView::commandIdName(ack.command_id);
    const QString errorText = VaporView::commandErrorCodeText(ack.error_code, is_english_);
    const bool noError = ack.error_code == VaporView::CommandErrorCode::Ok;
    const QString detailLabel = ok && noError
        ? (is_english_ ? QStringLiteral("detail") : QStringLiteral("详情"))
        : (is_english_ ? QStringLiteral("error") : QStringLiteral("错误"));
    const QString detailText = ok && noError
        ? (is_english_ ? QStringLiteral("no error") : QStringLiteral("无错误"))
        : errorText;
    log(QString(is_english_ ? "Remote ACK command=%1(%2) seq=%3 result=%4 %5=%6(%7)"
                            : "远程ACK 命令=%1(%2) 序号=%3 结果=%4 %5=%6(%7)")
            .arg(commandName)
            .arg(static_cast<quint16>(ack.command_id))
            .arg(ack.command_seq)
            .arg(ok ? (is_english_ ? QStringLiteral("ok") : QStringLiteral("成功"))
                    : (is_english_ ? QStringLiteral("error") : QStringLiteral("失败")))
            .arg(detailLabel)
            .arg(detailText)
            .arg(static_cast<quint32>(ack.error_code)));

    if (ack.command_id == VaporView::CommandId::EnableWaveformStreaming)
    {
        remote_wave_stream_enable_pending_ = false;
        remote_wave_stream_requested_ = ok;
        updateRemoteDeviceButtonText(VaporView::SkyDeviceId::WaveTcp,
                                     remote_device_states_.value(VaporView::SkyDeviceId::WaveTcp,
                                                                 VaporView::DeviceState::Disconnected));
        if (!ok)
        {
            log(is_english_ ? "Remote waveform stream was not enabled" : "远程波形流启用失败");
        }
    }
    else if (ack.command_id == VaporView::CommandId::DisableWaveformStreaming)
    {
        remote_wave_stream_enable_pending_ = false;
        remote_wave_stream_requested_ = false;
        updateRemoteDeviceButtonText(VaporView::SkyDeviceId::WaveTcp,
                                     remote_device_states_.value(VaporView::SkyDeviceId::WaveTcp,
                                                                 VaporView::DeviceState::Disconnected));
    }

    if (ack.command_id == VaporView::CommandId::SetPeakSearchRange)
    {
        const auto it = remote_peak_search_commands_.find(ack.command_seq);
        if (it != remote_peak_search_commands_.end())
        {
            const VaporView::PeakSearchRange range = it.value();
            remote_peak_search_commands_.erase(it);
            if (tcp_wave_panel_)
            {
                if (ok)
                {
                    tcp_wave_panel_->applyRemotePeakSearchRange(range.start_index, range.end_index);
                    log(is_english_
                            ? QStringLiteral("Peak search range accepted. Old remote peak trend was cleared.")
                            : QStringLiteral("峰值搜索区间已生效，旧远程峰值趋势已清空。"));
                }
                else
                {
                    tcp_wave_panel_->rejectRemotePeakSearchRange(errorText);
                }
            }
        }
    }
    else if (isTemperatureCommand(ack.command_id))
    {
        const auto it = remote_temperature_commands_.find(ack.command_seq);
        const VaporView::TemperatureControllerCommand request = it != remote_temperature_commands_.end()
            ? it.value()
            : VaporView::TemperatureControllerCommand();
        if (it != remote_temperature_commands_.end())
        {
            remote_temperature_commands_.erase(it);
        }
        if (temperature_controller_panel_)
        {
            if (!(ok && noError))
            {
                temperature_controller_panel_->clearCommandPending(ack.command_id, request.channel == 0 ? 1 : request.channel);
            }
            temperature_controller_panel_->setCommandStatus(
                temperatureCommandStatusText(ack.command_id,
                                             request.channel == 0 ? 1 : request.channel,
                                             ok && noError,
                                             ok && noError ? QString() : errorText),
                !(ok && noError));
        }
        restoreTemperatureCommandUi(ack.command_id, request.channel == 0 ? 1 : request.channel);
    }
}

void MainWindow::onRemoteLinkOpenChanged(bool open)
{
    if (isRemoteSkyMode())
    {
        if (!open)
        {
            markRemoteSkyLinkClosed();
        }
        updateConnectionStatus(open);
    }
}

void MainWindow::onSkyDeviceConfigClicked()
{
    if (!ground_telemetry_service_ || !ground_telemetry_service_->isOpen())
    {
        log(is_english_ ? "Connect Remote Sky telemetry before opening the Sky Device Config dialog"
                        : "打开天空端设备配置前，请先连接天空端数传");
        return;
    }
    if (!sky_device_config_dialog_)
    {
        sky_device_config_dialog_ = new VaporView::SkyDeviceConfigDialog(ground_telemetry_service_);
        sky_device_config_dialog_->setAttribute(Qt::WA_QuitOnClose, false);
        sky_device_config_dialog_->setEnglish(is_english_);
        sky_device_config_dialog_->setFontScale(font_scale_percent_);
    }
    VaporView::centerWindowOnScreen(sky_device_config_dialog_, this);
    sky_device_config_dialog_->show();
    sky_device_config_dialog_->raise();
    sky_device_config_dialog_->activateWindow();
    ground_telemetry_service_->requestSkyConfig();
}

void MainWindow::onClearLogClicked()
{
    log_entries_.clear();
    log_text_edit_->clear();
    has_inline_progress_log_ = false;
    log(is_english_ ? "Log cleared" : "日志已清空");
}

bool MainWindow::applyEpsilonMainAntennaLeverArm(double xM, double yM, double zM, QString *errorMessage)
{
    auto fail = [errorMessage](const QString& message) {
        if (errorMessage)
        {
            *errorMessage = message;
        }
        return false;
    };

    if (connection_attempt_in_progress_ || port_detection_in_progress_ || epsilon_reconfigure_in_progress_)
    {
        return fail(is_english_
            ? QStringLiteral("EPSILON is busy. Wait for the current connection or configuration task to finish.")
            : QStringLiteral("EPSILON 当前正忙，请等待连接或配置任务结束后再试。"));
    }

    if (recording_thread_running_.load())
    {
        return fail(is_english_
            ? QStringLiteral("Stop recording before configuring the EPSILON main antenna lever arm.")
            : QStringLiteral("请先结束记录，再配置 EPSILON 主天线杆臂。"));
    }

    const QString epsilonPort = epsilon_port_combo_ ? epsilon_port_combo_->currentText().trimmed() : QString();
    if (epsilonPort.isEmpty() || epsilonPort.startsWith(QStringLiteral("--")))
    {
        return fail(is_english_
            ? QStringLiteral("Select the EPSILON main serial port first.")
            : QStringLiteral("请先选择 EPSILON 主串口。"));
    }

    const QString epsilonBaudText = epsilon_baud_combo_ ? epsilon_baud_combo_->currentText().trimmed() : QStringLiteral("921600");
    bool baudOk = false;
    const int epsilonBaud = epsilonBaudText.toInt(&baudOk);
    if (!baudOk || epsilonBaud <= 0)
    {
        return fail(QString(is_english_ ? "Invalid EPSILON baud rate: %1" : "EPSILON 波特率无效: %1").arg(epsilonBaudText));
    }

    const bool english = is_english_;
    const QString values = QStringLiteral("X=%1 m, Y=%2 m, Z=%3 m")
        .arg(QString::number(xM, 'f', 4),
             QString::number(yM, 'f', 4),
             QString::number(zM, 'f', 4));
    const std::shared_ptr<VaporView::EpsilonCollector> liveCollector = snapshotCollectors().epsilon;
    const bool shouldRestartCollector = liveCollector && liveCollector->isRunning();

    epsilon_reconfigure_in_progress_ = true;
    showBusyStatusTaskProgress(english ? "Configuring EPSILON Lever Arm..." : "正在配置 EPSILON 主天线杆臂...");
    updateConnectionStatus(is_connected_);
    QApplication::processEvents();

    log(QString(english
                    ? "[EPSILON] Applying main antenna lever arm on %1 @ %2: %3"
                    : "[EPSILON] 正在通过主串口 %1 @ %2 下发主天线杆臂：%3")
            .arg(epsilonPort, epsilonBaudText, values));

    std::shared_ptr<VaporView::EpsilonCollector> collector = shouldRestartCollector && liveCollector
        ? liveCollector
        : std::make_shared<VaporView::EpsilonCollector>();

    collector->setEnglish(english);
    collector->setLogCallback([this](const std::string& msg) {
        const QString qmsg = QString::fromStdString(msg);
        QMetaObject::invokeMethod(this, [this, qmsg]() { log(qmsg); }, Qt::QueuedConnection);
    });

    if (shouldRestartCollector)
    {
        log(english
                ? "[EPSILON] Temporarily stopping the live stream for main antenna lever-arm configuration."
                : "[EPSILON] 为配置主天线杆臂临时停止当前数据流。");
        collector->stop();
    }

    bool commandSucceeded = false;
    QString failureMessage;
    if (!collector->start(epsilonPort.toStdString(), VaporView::SerialConfig::N81(epsilonBaud)))
    {
        failureMessage = QString(english
                ? "[EPSILON] Failed to open %1 for main antenna lever-arm configuration: %2"
                : "[EPSILON] 打开 %1 进行主天线杆臂配置失败: %2")
            .arg(epsilonPort, QString::fromStdString(collector->getLastError()));
        log(failureMessage);
    }
    else if (!collector->configureMainAntennaLeverArm(xM, yM, zM))
    {
        failureMessage = QString(english
                ? "[EPSILON] Failed to configure main antenna lever arm on %1 @ %2."
                : "[EPSILON] 在 %1 @ %2 上配置主天线杆臂失败。")
            .arg(epsilonPort, epsilonBaudText);
        log(failureMessage);
    }
    else
    {
        commandSucceeded = true;
    }

    bool restartSucceeded = true;
    if (shouldRestartCollector)
    {
        if (!collector->startStreaming())
        {
            restartSucceeded = false;
            log(english
                    ? "[EPSILON] Main antenna lever-arm command finished, but failed to restart the live navigation stream."
                    : "[EPSILON] 主天线杆臂命令已结束，但重新启动实时导航流失败。");
            collector->stop();
        }
        else
        {
            log(QString(english
                            ? "[EPSILON] Main antenna lever arm applied and live stream restarted on %1."
                            : "[EPSILON] 主天线杆臂已下发，并已在 %1 上恢复实时数据流。")
                    .arg(epsilonPort));
        }
    }
    else
    {
        collector->stop();
        if (commandSucceeded)
        {
            log(QString(english
                            ? "[EPSILON] Main antenna lever arm applied on %1."
                            : "[EPSILON] 已在 %1 上完成主天线杆臂配置。")
                    .arg(epsilonPort));
        }
    }

    epsilon_reconfigure_in_progress_ = false;
    hideStatusTaskProgress();
    updateConnectionStatus(anyCollectorRunning());
    QApplication::processEvents();

    if (!commandSucceeded)
    {
        return fail(failureMessage.isEmpty()
            ? (english
                ? QStringLiteral("Failed to apply EPSILON main antenna lever arm.")
                : QStringLiteral("EPSILON 主天线杆臂下发失败。"))
            : failureMessage);
    }

    if (!restartSucceeded)
    {
        return fail(english
            ? QStringLiteral("The lever arm was sent, but the EPSILON live stream could not be restarted. Reconnect EPSILON manually.")
            : QStringLiteral("杆臂已下发，但 EPSILON 实时数据流未能恢复。请手动重新连接 EPSILON。"));
    }

    return true;
}

void MainWindow::syncRtkConfigPageState()
{
    if (!rtk_config_dialog_)
    {
        return;
    }

    rtk_config_dialog_->setEpsilonDataProvider([this]() {
        const CollectorSnapshot collectors = snapshotCollectors();
        return collectors.epsilon ? collectors.epsilon->getLatestData() : current_epsilon_;
    });
    rtk_config_dialog_->setEpsilonMainAntennaLeverArmApplier([this](double x, double y, double z, QString *errorMessage) {
        return applyEpsilonMainAntennaLeverArm(x, y, z, errorMessage);
    });
    {
        const QString epsilonPort = epsilon_port_combo_ ? epsilon_port_combo_->currentText().trimmed() : QString();
        const QString epsilonBaud = epsilon_baud_combo_ ? epsilon_baud_combo_->currentText().trimmed() : QStringLiteral("921600");
        rtk_config_dialog_->setEpsilonMainPortAndBaud(epsilonPort, epsilonBaud);
    }
    {
        QSettings settings("VaporView", "MainWindow");
        const QString preferredOutputPort = settings.value("epsilon_rtcm_forward_port").toString().trimmed();
        const QString preferredBaud = settings.value("epsilon_rtcm_forward_baud", "115200").toString().trimmed();
        if (!preferredOutputPort.isEmpty())
        {
            rtk_config_dialog_->setPreferredOutputPortAndBaud(preferredOutputPort, preferredBaud);
        }
    }
    rtk_config_dialog_->setFontScale(font_scale_percent_);
    rtk_config_dialog_->setEnglish(is_english_);
}

void MainWindow::onRtkConfigClicked()
{
    syncRtkConfigPageState();
    if (main_page_stack_ && rtk_config_dialog_ && main_page_stack_->indexOf(rtk_config_dialog_) >= 0)
    {
        main_page_stack_->setCurrentWidget(rtk_config_dialog_);
    }
    if (rtk_config_nav_btn_)
    {
        rtk_config_nav_btn_->setChecked(true);
    }
    updateSidebarNavIcons();
    updateCustomTitleBarTexts();
}

void MainWindow::onConfigureEpsilonRtcmPortClicked()
{
    if (connection_attempt_in_progress_ || port_detection_in_progress_ || epsilon_reconfigure_in_progress_)
    {
        return;
    }

    if (recording_thread_running_.load())
    {
        log(is_english_ ? "Stop recording before configuring the EPSILON RTCM port."
                        : "请先结束记录，再配置 EPSILON RTCM 串口。");
        return;
    }

    const QString selectText = is_english_ ? "-- Select --" : "-- 选择 --";
    const QString epsilonPort = epsilon_port_combo_ ? epsilon_port_combo_->currentText().trimmed() : QString();
    if (epsilonPort.isEmpty() || epsilonPort == selectText)
    {
        log(is_english_ ? "Select the EPSILON main serial port first."
                        : "请先选择 EPSILON 主串口。");
        return;
    }

    const QString epsilonBaudText = epsilon_baud_combo_ ? epsilon_baud_combo_->currentText().trimmed() : QStringLiteral("921600");
    bool epsilonBaudOk = false;
    const int epsilonBaud = epsilonBaudText.toInt(&epsilonBaudOk);
    if (!epsilonBaudOk || epsilonBaud <= 0)
    {
        log(QString(is_english_ ? "Invalid EPSILON baud rate: %1" : "EPSILON 波特率无效: %1").arg(epsilonBaudText));
        return;
    }

    QSettings settings("VaporView", "MainWindow");
    const QStringList availablePorts = getAvailablePorts();

    QDialog dialog(this);
    dialog.setModal(true);
    dialog.setWindowTitle(is_english_ ? "Configure EPSILON RTCM Port" : "配置 EPSILON RTCM 串口");

    auto *layout = new QVBoxLayout(&dialog);
    auto *hintLabel = new QLabel(
        is_english_
            ? QStringLiteral("This configures EPSILON communication port 2 as an RTCM input port, saves the output-forwarding serial port on this PC, and prepares the RTK dialog to stream RTCM continuously into EPSILON.")
            : QStringLiteral("这个功能会把 EPSILON 的第二通信串口配置为 RTCM 输入口，同时保存本机用于转发 RTCM 的串口与波特率，并为后续 RTK 配置对话框做好预填。"),
        &dialog);
    hintLabel->setWordWrap(true);
    layout->addWidget(hintLabel);

    auto *formLayout = new QFormLayout();
    formLayout->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    layout->addLayout(formLayout);

    auto *mainPortValue = new QLabel(QStringLiteral("%1 @ %2").arg(epsilonPort, epsilonBaudText), &dialog);
    formLayout->addRow(is_english_ ? "EPSILON Main Port:" : "EPSILON 主串口：", mainPortValue);

    auto *deviceRtcmPortValue = new QLabel(is_english_ ? "COMM2 (RTCM)" : "串口2（RTCM）", &dialog);
    formLayout->addRow(is_english_ ? "Device RTCM Port:" : "设备 RTCM 串口：", deviceRtcmPortValue);

    auto *forwardPortCombo = new QComboBox(&dialog);
    forwardPortCombo->setEditable(true);
    forwardPortCombo->addItem(selectText);
    forwardPortCombo->addItems(availablePorts);
    configureComboPopup(forwardPortCombo);
    const QString savedForwardPort = settings.value("epsilon_rtcm_forward_port").toString().trimmed();
    if (!savedForwardPort.isEmpty())
    {
        forwardPortCombo->setCurrentText(savedForwardPort);
    }
    formLayout->addRow(is_english_ ? "PC RTCM Forward Port:" : "本机 RTCM 转发串口：", forwardPortCombo);

    auto *forwardBaudCombo = new QComboBox(&dialog);
    forwardBaudCombo->addItems({QStringLiteral("115200"),
                                QStringLiteral("230400"),
                                QStringLiteral("460800"),
                                QStringLiteral("921600")});
    forwardBaudCombo->setCurrentText(settings.value("epsilon_rtcm_forward_baud", "115200").toString());
    configureComboPopup(forwardBaudCombo);
    formLayout->addRow(is_english_ ? "RTCM Port Baud:" : "RTCM 串口波特率：", forwardBaudCombo);

    auto *openRtkConfigCheck = new QCheckBox(
        is_english_ ? "Open RTK Config after success" : "成功后打开 RTK 配置",
        &dialog);
    openRtkConfigCheck->setChecked(true);
    layout->addWidget(openRtkConfigCheck);

    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttonBox);

    VaporView::installCustomTitleBar(&dialog, false);
    if (dialog.exec() != QDialog::Accepted)
    {
        return;
    }

    const QString forwardPort = forwardPortCombo->currentText().trimmed();
    if (forwardPort.isEmpty() || forwardPort == selectText)
    {
        log(is_english_ ? "Select the PC serial port that is wired to EPSILON port 2."
                        : "请选择连接到 EPSILON 第二串口的本机串口。");
        return;
    }

    bool forwardBaudOk = false;
    const QString forwardBaudText = forwardBaudCombo->currentText().trimmed();
    const int forwardBaud = forwardBaudText.toInt(&forwardBaudOk);
    if (!forwardBaudOk || forwardBaud <= 0)
    {
        log(QString(is_english_ ? "Invalid RTCM forwarding baud rate: %1" : "RTCM 转发波特率无效: %1").arg(forwardBaudText));
        return;
    }

    if (epsilon_reconfigure_thread_.joinable())
    {
        epsilon_reconfigure_thread_.join();
    }

    const std::shared_ptr<VaporView::EpsilonCollector> liveCollector = snapshotCollectors().epsilon;
    const bool shouldRestartCollector = liveCollector && liveCollector->isRunning();
    const bool shouldOpenRtkDialog = openRtkConfigCheck->isChecked();
    const bool english = is_english_;

    epsilon_reconfigure_in_progress_ = true;
    showBusyStatusTaskProgress(english ? "Configuring EPSILON RTCM Port..." : "正在配置 EPSILON RTCM 串口...");
    updateConnectionStatus(is_connected_);
    log(QString(english
                    ? "[EPSILON] Configuring communication port 2 as RTCM: main %1 @ %2, RTCM forward port %3 @ %4"
                    : "[EPSILON] 正在把第二通信串口配置为 RTCM：主串口 %1 @ %2，RTCM 转发串口 %3 @ %4")
            .arg(epsilonPort, epsilonBaudText, forwardPort, forwardBaudText));

    epsilon_reconfigure_thread_ = std::thread([this,
                                               english,
                                               epsilonPort,
                                               epsilonBaud,
                                               epsilonBaudText,
                                               forwardPort,
                                               forwardBaud,
                                               forwardBaudText,
                                               liveCollector,
                                               shouldRestartCollector,
                                               shouldOpenRtkDialog]() {
        auto postLog = [this](const QString& message) {
            QMetaObject::invokeMethod(this, [this, message]() { log(message); }, Qt::QueuedConnection);
        };
        auto finishOnUi = [this](bool openRtkDialog) {
            QMetaObject::invokeMethod(this, [this, openRtkDialog]() {
                epsilon_reconfigure_in_progress_ = false;
                hideStatusTaskProgress();
                updateConnectionStatus(anyCollectorRunning());
                if (openRtkDialog)
                {
                    onRtkConfigClicked();
                }
            }, Qt::QueuedConnection);
        };

        std::shared_ptr<VaporView::EpsilonCollector> collector = shouldRestartCollector && liveCollector
            ? liveCollector
            : std::make_shared<VaporView::EpsilonCollector>();

        collector->setEnglish(english);
        collector->setLogCallback([this](const std::string& msg) {
            const QString qmsg = QString::fromStdString(msg);
            QMetaObject::invokeMethod(this, [this, qmsg]() { log(qmsg); }, Qt::QueuedConnection);
        });

        if (shouldRestartCollector)
        {
            postLog(english
                        ? "[EPSILON] Temporarily stopping the live stream for RTCM-port configuration."
                        : "[EPSILON] 为配置 RTCM 串口临时停止当前数据流。");
            collector->stop();
        }

        if (!collector->start(epsilonPort.toStdString(), VaporView::SerialConfig::N81(epsilonBaud)))
        {
            postLog(QString(english ? "[EPSILON] Failed to open %1 for RTCM-port configuration: %2"
                                    : "[EPSILON] 打开 %1 进行 RTCM 串口配置失败: %2")
                        .arg(epsilonPort, QString::fromStdString(collector->getLastError())));
            finishOnUi(false);
            return;
        }

        if (!collector->configureRtcmPort(2, forwardBaud))
        {
            postLog(QString(english ? "[EPSILON] Failed to configure communication port 2 as RTCM on %1 @ %2."
                                    : "[EPSILON] 在 %1 @ %2 上把第二通信串口配置为 RTCM 失败。")
                        .arg(epsilonPort, epsilonBaudText));
            collector->stop();
            finishOnUi(false);
            return;
        }

        {
            QSettings mainSettings("VaporView", "MainWindow");
            mainSettings.setValue("epsilon_rtcm_forward_port", forwardPort);
            mainSettings.setValue("epsilon_rtcm_forward_baud", forwardBaudText);
            QSettings rtkSettings("VaporView", "RtkConfig");
            rtkSettings.setValue("output_port", forwardPort);
            rtkSettings.setValue("baudrate", forwardBaudText);
        }

        if (shouldRestartCollector)
        {
            if (!collector->startStreaming())
            {
                postLog(english
                            ? "[EPSILON] RTCM-port configuration succeeded, but failed to restart the live navigation stream."
                            : "[EPSILON] RTCM 串口配置已完成，但重新启动实时导航流失败。");
                collector->stop();
                finishOnUi(false);
                return;
            }

            postLog(QString(english
                                ? "[EPSILON] RTCM port is ready. EPSILON live stream restarted on %1, and RTK forwarding is prefilled for %2 @ %3."
                                : "[EPSILON] RTCM 串口已就绪，已在 %1 上恢复 EPSILON 实时数据流，并为 %2 @ %3 预填 RTK 转发配置。")
                        .arg(epsilonPort, forwardPort, forwardBaudText));
        }
        else
        {
            collector->stop();
            postLog(QString(english
                                ? "[EPSILON] RTCM port is ready on %1. RTK forwarding is prefilled for %2 @ %3."
                                : "[EPSILON] 已在 %1 上完成 RTCM 串口配置，并为 %2 @ %3 预填 RTK 转发配置。")
                        .arg(epsilonPort, forwardPort, forwardBaudText));
        }

        finishOnUi(shouldOpenRtkDialog);
    });
}

void MainWindow::onConfigureEpsilonPacketRatesClicked()
{
    if (connection_attempt_in_progress_ || port_detection_in_progress_ || epsilon_reconfigure_in_progress_)
    {
        return;
    }

    const QString epsilonRateText = epsilon_rate_combo_ ? epsilon_rate_combo_->currentText() : QStringLiteral("100");
    const int groupedRateHz = effectiveRateOrDefault(epsilonRateText, kDefaultEpsilonSampleRateHz, 200);
    QSettings settings("VaporView", "MainWindow");
    const bool customEnabled = settings.value("epsilon_custom_packet_rates_enabled", false).toBool();
    const std::map<uint8_t, int> defaultRates = defaultEpsilonPacketRates();
    const std::map<uint8_t, int> groupedRates = groupedEpsilonPacketRates(groupedRateHz);
    const std::map<uint8_t, int> initialRates = customEnabled
        ? loadCustomEpsilonPacketRates(settings, groupedRateHz)
        : groupedRates;

    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("epsilonPacketRatesDialog"));
    dialog.setModal(true);
    dialog.setWindowTitle(is_english_ ? "EPSILON Packet Rates" : "EPSILON 包频率设置");
    dialog.setStyleSheet(applyAppThemeTokens(QStringLiteral(
        "QDialog#epsilonPacketRatesDialog,"
        "QDialog#epsilonPacketRatesDialog QWidget#epsilonPacketRatesContent,"
        "QDialog#epsilonPacketRatesDialog QWidget#epsilonPacketRatesCell { background-color: @vv-surface; }"
        "QDialog#epsilonPacketRatesDialog QLabel,"
        "QDialog#epsilonPacketRatesDialog QCheckBox { background-color: transparent; }"),
        dark_theme_enabled_));

    auto *layout = new QVBoxLayout(&dialog);
    layout->setSpacing(10);

    auto *hintLabel = new QLabel(
        is_english_
            ? QStringLiteral("Configured from the local EPSILON ground-station profile. The recommended default profile prioritizes stable time and 3D navigation output. Rate limits are reflected by each selector's available options. If any packet differs from the grouped profile, the custom profile will be enabled automatically when you save.")
            : QStringLiteral("配置范围来自本地 EPSILON 官方地面站配置。推荐默认配置优先保证稳定的时间与三维导航输出。频率上限由各选择框的可选项体现。只要任一数据包偏离分组模式，保存时就会自动启用自定义配置。"),
        &dialog);
    hintLabel->setWordWrap(true);
    hintLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
    layout->addWidget(hintLabel);

    auto *enableCustomCheck = new QCheckBox(
        is_english_
            ? QStringLiteral("Use custom EPSILON packet rates for future connect/reconfigure operations")
            : QStringLiteral("后续连接和重配时使用这组自定义 EPSILON 包频率"),
        &dialog);
    enableCustomCheck->setChecked(customEnabled);
    layout->addWidget(enableCustomCheck);

    auto *formWidget = new QWidget(&dialog);
    formWidget->setObjectName(QStringLiteral("epsilonPacketRatesContent"));
    formWidget->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    auto *formLayout = new QGridLayout(formWidget);
    formLayout->setContentsMargins(0, 0, 0, 0);
    formLayout->setHorizontalSpacing(16);
    formLayout->setVerticalSpacing(10);
    formLayout->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    layout->addWidget(formWidget, 0, Qt::AlignLeft);

    auto packetRateText = [this](int rateHz) {
        return epsilonPacketRateDisplayText(rateHz, is_english_);
    };
    int packetRateComboWidth = 0;
    {
        QComboBox comboProbe(&dialog);
        const QFontMetrics comboMetrics(comboProbe.font());
        for (const EpsilonPacketConfigOption& option : epsilonPacketConfigOptions())
        {
            for (int rateHz : option.supported_rates_hz)
            {
                packetRateComboWidth = std::max(packetRateComboWidth,
                                               comboMetrics.horizontalAdvance(packetRateText(rateHz)));
            }
        }
    }
    packetRateComboWidth = std::max(128, packetRateComboWidth + 64);
    const QFontMetrics rowLabelMetrics(hintLabel->font());

    constexpr int kPacketRateDialogColumnCount = 3;
    std::map<uint8_t, QComboBox*> packetCombos;
    int packetIndex = 0;
    for (const EpsilonPacketConfigOption& option : epsilonPacketConfigOptions())
    {
        const QString rowLabelText = epsilonPacketDialogRowLabel(option, is_english_);
        const int cellWidth = std::max(packetRateComboWidth, rowLabelMetrics.horizontalAdvance(rowLabelText) + 8);
        auto *cell = new QWidget(formWidget);
        cell->setObjectName(QStringLiteral("epsilonPacketRatesCell"));
        cell->setFixedWidth(cellWidth);
        auto *cellLayout = new QVBoxLayout(cell);
        cellLayout->setContentsMargins(0, 0, 0, 0);
        cellLayout->setSpacing(4);

        auto *label = new QLabel(rowLabelText, cell);
        label->setWordWrap(false);
        label->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
        cellLayout->addWidget(label);

        auto *combo = new QComboBox(cell);
        combo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
        combo->setFixedWidth(packetRateComboWidth);
        combo->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        for (int rateHz : option.supported_rates_hz)
        {
            combo->addItem(packetRateText(rateHz), rateHz);
        }
        configureComboPopup(combo);
        const int initialRateHz = initialRates.count(option.packet_id) ? initialRates.at(option.packet_id) : groupedRates.at(option.packet_id);
        const int comboIndex = combo->findData(initialRateHz);
        if (comboIndex >= 0)
        {
            combo->setCurrentIndex(comboIndex);
        }
        packetCombos[option.packet_id] = combo;
        cellLayout->addWidget(combo, 0, Qt::AlignLeft);
        const int row = packetIndex / kPacketRateDialogColumnCount;
        const int column = packetIndex % kPacketRateDialogColumnCount;
        formLayout->addWidget(cell, row, column, Qt::AlignTop);
        ++packetIndex;
    }

    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    QPushButton *recommendedDefaultsButton = buttonBox->addButton(
        is_english_ ? QStringLiteral("Use Recommended Defaults") : QStringLiteral("恢复推荐默认值"),
        QDialogButtonBox::ResetRole);
    connect(recommendedDefaultsButton, &QPushButton::clicked, &dialog, [&packetCombos, defaultRates, enableCustomCheck]() {
        enableCustomCheck->setChecked(true);
        for (const auto& entry : packetCombos)
        {
            const auto it = defaultRates.find(entry.first);
            if (it == defaultRates.end())
            {
                continue;
            }
            QComboBox *combo = entry.second;
            const int index = combo ? combo->findData(it->second) : -1;
            if (combo && index >= 0)
            {
                combo->setCurrentIndex(index);
            }
        }
    });
    QPushButton *groupedDefaultsButton = buttonBox->addButton(
        is_english_ ? QStringLiteral("Use Grouped Profile") : QStringLiteral("切换到分组模式"),
        QDialogButtonBox::ActionRole);
    connect(groupedDefaultsButton, &QPushButton::clicked, &dialog, [&packetCombos, groupedRates, enableCustomCheck]() {
        enableCustomCheck->setChecked(false);
        for (const auto& entry : packetCombos)
        {
            const auto it = groupedRates.find(entry.first);
            if (it == groupedRates.end())
            {
                continue;
            }
            QComboBox *combo = entry.second;
            const int index = combo ? combo->findData(it->second) : -1;
            if (combo && index >= 0)
            {
                combo->setCurrentIndex(index);
            }
        }
    });
    connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttonBox);

    VaporView::installCustomTitleBar(&dialog, false);
    if (QLayout *dialogLayout = dialog.layout())
    {
        dialogLayout->invalidate();
    }
    const QSize targetMinimumSize(is_english_ ? QSize(700, 360) : QSize(720, 360));
    const QSize preferredSize = dialog.sizeHint().expandedTo(targetMinimumSize);
    const QSize targetSize = VaporView::defaultWindowSizeWithinScreenFraction(
        this,
        preferredSize,
        0.85,
        targetMinimumSize);
    dialog.setMinimumSize(targetSize);
    dialog.resize(targetSize);
    VaporView::centerWindowOnScreen(&dialog, this);
    if (dialog.exec() != QDialog::Accepted)
    {
        return;
    }

    std::map<uint8_t, int> savedPacketRates;
    for (const EpsilonPacketConfigOption& option : epsilonPacketConfigOptions())
    {
        QComboBox *combo = packetCombos[option.packet_id];
        const int rateHz = combo ? combo->currentData().toInt() : groupedRates.at(option.packet_id);
        savedPacketRates[option.packet_id] = rateHz;
        settings.setValue(epsilonPacketRateSettingsKey(option.packet_id), rateHz);
    }
    bool hasCustomOverrides = false;
    for (const auto& entry : savedPacketRates)
    {
        const auto groupedIt = groupedRates.find(entry.first);
        if (groupedIt != groupedRates.end() && groupedIt->second != entry.second)
        {
            hasCustomOverrides = true;
            break;
        }
    }
    const bool effectiveCustomEnabled = enableCustomCheck->isChecked() || hasCustomOverrides;
    settings.setValue("epsilon_custom_packet_rates_enabled", effectiveCustomEnabled);
    settings.setValue("epsilon_custom_packet_rates_user_saved", effectiveCustomEnabled);
    settings.remove("epsilon_last_config_signature");
    settings.remove("epsilon_last_config_apply_version");

    if (hasCustomOverrides && !enableCustomCheck->isChecked())
    {
        log(is_english_
                ? "[EPSILON] Packet-rate overrides detected, so the custom packet-rate profile has been enabled automatically."
                : "[EPSILON] 检测到包频率已偏离分组模式，已自动启用自定义包频率配置。");
    }

    if (effectiveCustomEnabled)
    {
        log(QString(is_english_
                        ? ((savedPacketRates == defaultRates)
                               ? "[EPSILON] Recommended default packet-rate profile saved: %1"
                               : "[EPSILON] Custom packet-rate profile saved: %1")
                        : ((savedPacketRates == defaultRates)
                               ? "[EPSILON] 已保存推荐默认包频率配置: %1"
                               : "[EPSILON] 已保存自定义包频率配置: %1"))
                .arg(epsilonPacketRatesSummary(savedPacketRates)));
    }
    else
    {
        log(QString(is_english_
                        ? "[EPSILON] Custom packet-rate profile disabled. The grouped %1 Hz profile will be used."
                        : "[EPSILON] 已关闭自定义包频率，后续将使用分组 %1 Hz 配置。")
                .arg(groupedRateHz));
    }

    const QString selectText = is_english_ ? "-- Select --" : "-- 选择 --";
    const QString epsilonPort = epsilon_port_combo_ ? epsilon_port_combo_->currentText().trimmed() : QString();
    if (!recording_thread_running_.load() &&
        !epsilonPort.isEmpty() &&
        epsilonPort != selectText &&
        !isRateUnspecified(epsilonRateText))
    {
        log(is_english_
                ? "[EPSILON] Applying the saved packet-rate profile now..."
                : "[EPSILON] 正在应用刚保存的包频率配置...");
        onReconfigureEpsilonClicked();
    }
    else
    {
        log(is_english_
                ? "[EPSILON] Packet-rate profile saved. It will be used on the next connect/reconfigure."
                : "[EPSILON] 包频率配置已保存，将在下次连接或重配时生效。");
    }
    syncDeviceConfigEpsilonPanelFromSettings();
}

void MainWindow::onReconfigureEpsilonClicked()
{
    if (connection_attempt_in_progress_ || port_detection_in_progress_ || epsilon_reconfigure_in_progress_)
    {
        return;
    }

    if (recording_thread_running_.load())
    {
        log(is_english_ ? "Stop recording before reconfiguring EPSILON output."
                        : "请先结束记录，再重新配置 EPSILON 输出。");
        return;
    }

    const QString selectText = is_english_ ? "-- Select --" : "-- 选择 --";
    const QString epsilonPort = epsilon_port_combo_ ? epsilon_port_combo_->currentText().trimmed() : QString();
    if (epsilonPort.isEmpty() || epsilonPort == selectText)
    {
        log(is_english_ ? "Select an EPSILON serial port first." : "请先选择 EPSILON 串口。");
        return;
    }

    const QString epsilonBaudText = epsilon_baud_combo_ ? epsilon_baud_combo_->currentText().trimmed() : QStringLiteral("921600");
    bool baudOk = false;
    const int epsilonBaud = epsilonBaudText.toInt(&baudOk);
    if (!baudOk || epsilonBaud <= 0)
    {
        log(QString(is_english_ ? "Invalid EPSILON baud rate: %1" : "EPSILON 波特率无效: %1").arg(epsilonBaudText));
        return;
    }

    const QString epsilonRateText = epsilon_rate_combo_ ? epsilon_rate_combo_->currentText() : QStringLiteral("100");
    if (isRateUnspecified(epsilonRateText))
    {
        log(is_english_
            ? "[EPSILON] Output-rate command is disabled because the EPSILON rate is set to No Set."
            : "[EPSILON] EPSILON 频率为“不设定”，已跳过输出频率下发。");
        return;
    }

    const int epsilonRate = effectiveRateOrDefault(epsilonRateText, kDefaultEpsilonSampleRateHz, 200);
    epsilon_sample_rate_ = epsilonRate;
    QSettings settings("VaporView", "MainWindow");
    bool usingCustomPacketProfile = false;
    const std::map<uint8_t, int> desiredPacketRates = effectiveEpsilonPacketRates(settings, epsilonRate, &usingCustomPacketProfile);
    const int epsilonCallbackRate = epsilonPacketCallbackRate(desiredPacketRates, epsilonRate);
    const QString desiredPacketRateSignature = epsilonPacketRatesSignature(desiredPacketRates);
    const QString desiredPacketRateSummary = epsilonPacketRatesSummary(desiredPacketRates);

    if (epsilon_reconfigure_thread_.joinable())
    {
        epsilon_reconfigure_thread_.join();
    }

    const std::shared_ptr<VaporView::EpsilonCollector> liveCollector = snapshotCollectors().epsilon;
    const bool shouldRestartCollector = liveCollector && liveCollector->isRunning();
    const bool english = is_english_;

    epsilon_reconfigure_in_progress_ = true;
    showBusyStatusTaskProgress(english ? "Reconfiguring EPSILON..." : "正在重配 EPSILON...");
    updateConnectionStatus(is_connected_);
    log(QString(english ? "[EPSILON] Starting manual output reconfiguration: %1 @ %2, %3 profile (%4)"
                        : "[EPSILON] 开始手动重配输出: %1 @ %2，使用%3配置（%4）")
            .arg(epsilonPort, epsilonBaudText)
            .arg(usingCustomPacketProfile ? (english ? "custom packet-rate" : "自定义包频率")
                                          : (english ? "grouped output-rate" : "分组输出频率"))
            .arg(desiredPacketRateSummary));

    epsilon_reconfigure_thread_ = std::thread([this,
                                               english,
                                               epsilonPort,
                                               epsilonBaud,
                                               epsilonBaudText,
                                               epsilonRate,
                                               epsilonCallbackRate,
                                               desiredPacketRates,
                                               desiredPacketRateSignature,
                                               liveCollector,
                                               shouldRestartCollector]() {
        auto postLog = [this](const QString& message) {
            QMetaObject::invokeMethod(this, [this, message]() { log(message); }, Qt::QueuedConnection);
        };
        auto finishOnUi = [this]() {
            QMetaObject::invokeMethod(this, [this]() {
                epsilon_reconfigure_in_progress_ = false;
                hideStatusTaskProgress();
                updateConnectionStatus(anyCollectorRunning());
            }, Qt::QueuedConnection);
        };

        std::shared_ptr<VaporView::EpsilonCollector> collector = shouldRestartCollector && liveCollector
            ? liveCollector
            : std::make_shared<VaporView::EpsilonCollector>();

        collector->setEnglish(english);
        collector->setLogCallback([this](const std::string& msg) {
            const QString qmsg = QString::fromStdString(msg);
            QMetaObject::invokeMethod(this, [this, qmsg]() { log(qmsg); }, Qt::QueuedConnection);
        });
        collector->setSampleRate(epsilonCallbackRate);

        if (shouldRestartCollector)
        {
            postLog(english
                        ? "[EPSILON] Temporarily stopping the live stream for manual reconfiguration."
                        : "[EPSILON] 为手动重配临时停止当前数据流。");
            collector->stop();
        }

        if (!collector->start(epsilonPort.toStdString(), VaporView::SerialConfig::N81(epsilonBaud)))
        {
            postLog(QString(english ? "[EPSILON] Failed to open %1 for manual reconfiguration: %2"
                                    : "[EPSILON] 打开 %1 进行手动重配失败: %2")
                        .arg(epsilonPort, QString::fromStdString(collector->getLastError())));
            finishOnUi();
            return;
        }

        if (!collector->setOutputPacketRates(desiredPacketRates, true))
        {
            postLog(QString(english ? "[EPSILON] Manual reconfiguration failed on %1 @ %2."
                                    : "[EPSILON] 在 %1 @ %2 上执行手动重配失败。")
                        .arg(epsilonPort, epsilonBaudText));
            collector->stop();
            finishOnUi();
            return;
        }

        {
            QSettings settings("VaporView", "MainWindow");
            settings.setValue("epsilon_last_config_port", epsilonPort);
            settings.setValue("epsilon_last_config_baud", epsilonBaudText);
            settings.setValue("epsilon_last_config_rate_hz", epsilonRate);
            settings.setValue("epsilon_last_config_signature", desiredPacketRateSignature);
            settings.setValue("epsilon_last_config_apply_version", kEpsilonPacketConfigApplyVersion);
        }

        if (shouldRestartCollector)
        {
            if (!collector->startStreaming())
            {
                postLog(english
                            ? "[EPSILON] Reconfiguration succeeded, but failed to restart the live navigation stream."
                            : "[EPSILON] 重配已完成，但重新启动实时导航流失败。");
                collector->stop();
                finishOnUi();
                return;
            }

            postLog(QString(english ? "[EPSILON] Manual reconfiguration completed and live stream restarted on %1."
                                    : "[EPSILON] 手动重配完成，已在 %1 上恢复实时数据流。")
                        .arg(epsilonPort));
        }
        else
        {
            collector->stop();
            postLog(QString(english ? "[EPSILON] Manual reconfiguration completed on %1. You can connect normally now."
                                    : "[EPSILON] 已在 %1 上完成手动重配，现在可以正常连接。")
                        .arg(epsilonPort));
        }

        finishOnUi();
    });
}
