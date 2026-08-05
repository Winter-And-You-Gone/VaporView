#include "shared/theme/AppTheme.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QComboBox>
#include <QEvent>
#include <QFrame>
#include <QPainterPath>
#include <QRegion>
#include <QTimer>
#include <QVariant>
#include <QWidget>

#include <algorithm>

namespace VaporView
{
namespace
{

constexpr int kComboPopupCornerRadius = 10;
constexpr int kComboPopupAnchorGap = 0;
constexpr int kComboPopupBorderWidth = 1;
constexpr const char *kComboPopupContainerMaskFilterProperty = "vaporViewComboPopupContainerMaskFilterInstalled";
constexpr const char *kComboPopupContainerUpdateQueuedProperty = "vaporViewComboPopupContainerUpdateQueued";
constexpr const char *kComboPopupOwnerProperty = "vaporViewComboPopupOwner";
constexpr const char *kComboPopupContainerAligningProperty = "vaporViewComboPopupContainerAligning";
constexpr const char *kComboPopupContainerName = "vaporViewComboPopupContainer";
constexpr const char *kComboPopupBorderLayerName = "vaporViewComboPopupBorderLayer";

QColor hexColor(const char *value)
{
    return QColor(QString::fromLatin1(value));
}

QString cssColor(const QColor& color)
{
    if (color.alpha() == 0)
    {
        return QStringLiteral("transparent");
    }
    return color.name(QColor::HexRgb);
}

QString cssRgba(const QColor& color, qreal alpha)
{
    const qreal bounded = std::max<qreal>(0.0, std::min<qreal>(1.0, alpha));
    return QStringLiteral("rgba(%1, %2, %3, %4)")
        .arg(color.red())
        .arg(color.green())
        .arg(color.blue())
        .arg(bounded, 0, 'f', 2);
}

struct ThemeReplacement
{
    const char *token;
    AppThemeColor color;
};

void applyRoundedWidgetMask(QWidget *widget, const char *appliedProperty)
{
    if (!widget || widget->size().isEmpty())
    {
        return;
    }

    QPainterPath path;
    path.addRoundedRect(QRectF(widget->rect()),
                        kComboPopupCornerRadius,
                        kComboPopupCornerRadius);
    widget->setMask(QRegion(path.toFillPolygon().toPolygon()));
    widget->setProperty(appliedProperty, !widget->mask().isEmpty());
}

QWidget *comboPopupAnchorForView(QAbstractItemView *view)
{
    if (!view)
    {
        return nullptr;
    }
    return qobject_cast<QWidget *>(view->property(kComboPopupOwnerProperty).value<QObject *>());
}

void updateComboPopupBorderLayer(QWidget *container, QAbstractItemView *view)
{
    if (!container || !view)
    {
        return;
    }

    const bool dark = view->property("vaporViewComboPopupDarkTheme").toBool();
    const QColor popupBase = appThemeColor(AppThemeColor::MenuPanel, dark);
    const QColor popupBorder = appThemeColor(AppThemeColor::Border, dark);

    auto *borderLayer = container->findChild<QWidget *>(QString::fromLatin1(kComboPopupBorderLayerName),
                                                       Qt::FindDirectChildrenOnly);
    if (!borderLayer)
    {
        borderLayer = new QWidget(container);
        borderLayer->setObjectName(QString::fromLatin1(kComboPopupBorderLayerName));
        borderLayer->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        borderLayer->setAttribute(Qt::WA_StyledBackground, true);
        borderLayer->setAutoFillBackground(false);
    }

    borderLayer->setProperty("vaporViewComboPopupBorderLayer", true);
    borderLayer->setProperty("vaporViewComboPopupBorderWidth", kComboPopupBorderWidth);
    borderLayer->setProperty("cornerRadius", kComboPopupCornerRadius);
    borderLayer->setGeometry(container->rect());

    const QString borderLayerStyle = QStringLiteral(
        "QWidget#%1 { background-color: transparent; border: %2px solid %3; border-radius: %4px; }")
        .arg(QString::fromLatin1(kComboPopupBorderLayerName),
             QString::number(kComboPopupBorderWidth),
             popupBorder.name(),
             QString::number(kComboPopupCornerRadius));
    if (borderLayer->styleSheet() != borderLayerStyle)
    {
        borderLayer->setStyleSheet(borderLayerStyle);
    }
    borderLayer->raise();
    borderLayer->show();

    QPalette layerPalette = borderLayer->palette();
    layerPalette.setColor(QPalette::Window, popupBase);
    layerPalette.setColor(QPalette::Base, popupBase);
    borderLayer->setPalette(layerPalette);
}

void applyComboPopupContainerStyle(QWidget *container, QAbstractItemView *view)
{
    if (!container || !view)
    {
        return;
    }

    const bool dark = view->property("vaporViewComboPopupDarkTheme").toBool();
    const QColor popupBase = appThemeColor(AppThemeColor::MenuPanel, dark);

    container->setObjectName(QString::fromLatin1(kComboPopupContainerName));
    container->setProperty("vaporViewComboPopupBorderWidth", kComboPopupBorderWidth);
    container->setAutoFillBackground(true);
    container->setAttribute(Qt::WA_StyledBackground, true);
    container->setAttribute(Qt::WA_TranslucentBackground, false);
    container->setAttribute(Qt::WA_NoSystemBackground, false);

    QPalette palette = container->palette();
    palette.setColor(QPalette::Window, popupBase);
    palette.setColor(QPalette::Base, popupBase);
    container->setPalette(palette);

    const QString containerStyle = QStringLiteral(
        "QFrame#%1 { background-color: %2; border: none; border-radius: %3px; }")
        .arg(QString::fromLatin1(kComboPopupContainerName),
             popupBase.name(),
             QString::number(kComboPopupCornerRadius));
    if (container->styleSheet() != containerStyle)
    {
        container->setStyleSheet(containerStyle);
    }
    updateComboPopupBorderLayer(container, view);
}

void alignComboPopupContainerToAnchor(QWidget *container, QAbstractItemView *view)
{
    QWidget *anchor = comboPopupAnchorForView(view);
    if (!container || !anchor || !container->isVisible() || container->property(kComboPopupContainerAligningProperty).toBool())
    {
        return;
    }

    const QRect anchorRect(anchor->mapToGlobal(QPoint(0, 0)), anchor->size());
    QRect popupRect = container->geometry();
    if (anchorRect.isEmpty() || popupRect.isEmpty())
    {
        return;
    }

    const bool opensBelow = popupRect.center().y() >= anchorRect.center().y();
    const int desiredTop = anchorRect.bottom() + 1 + kComboPopupAnchorGap;
    const int desiredBottom = anchorRect.top() - 1 - kComboPopupAnchorGap;
    const int targetY = opensBelow ? desiredTop : desiredBottom - popupRect.height() + 1;

    if (targetY == popupRect.y())
    {
        return;
    }

    container->setProperty(kComboPopupContainerAligningProperty, true);
    container->move(popupRect.x(), targetY);
    container->setProperty(kComboPopupContainerAligningProperty, false);
}

void updateComboPopupContainer(QWidget *container)
{
    if (!container)
    {
        return;
    }

    if (auto *view = container->findChild<QAbstractItemView *>(QStringLiteral("vaporViewComboPopupView")))
    {
        applyComboPopupContainerStyle(container, view);
        applyRoundedWidgetMask(container, "vaporViewComboPopupContainerRoundedMaskApplied");
        alignComboPopupContainerToAnchor(container, view);
    }
}

void queueComboPopupContainerUpdate(QWidget *container)
{
    if (!container || container->property(kComboPopupContainerUpdateQueuedProperty).toBool())
    {
        return;
    }

    container->setProperty(kComboPopupContainerUpdateQueuedProperty, true);
    QTimer::singleShot(0, container, [container]() {
        container->setProperty(kComboPopupContainerUpdateQueuedProperty, false);
        updateComboPopupContainer(container);
    });
}

class ComboPopupContainerMaskFilter final : public QObject
{
public:
    explicit ComboPopupContainerMaskFilter(QObject *parent)
        : QObject(parent)
    {
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        const QEvent::Type type = event->type();
        if (type == QEvent::Show || type == QEvent::Resize || type == QEvent::Polish)
        {
            if (auto *container = qobject_cast<QWidget *>(watched))
            {
                queueComboPopupContainerUpdate(container);
            }
        }
        return QObject::eventFilter(watched, event);
    }
};

void configureComboPopupContainerMask(QAbstractItemView *view)
{
    QWidget *container = view ? view->window() : nullptr;
    if (!container)
    {
        return;
    }

    container->setProperty("vaporViewComboPopupRoundedMaskEnabled", true);
    container->setProperty("cornerRadius", kComboPopupCornerRadius);
    container->setProperty("vaporViewComboPopupAnchorGap", kComboPopupAnchorGap);
    container->setProperty("vaporViewComboPopupNativeDropShadowDisabled", true);
    applyComboPopupContainerStyle(container, view);
    if (!container->isVisible())
    {
        container->setWindowFlag(Qt::NoDropShadowWindowHint, true);
    }
    if (!container->property(kComboPopupContainerMaskFilterProperty).toBool())
    {
        container->installEventFilter(new ComboPopupContainerMaskFilter(container));
        container->setProperty(kComboPopupContainerMaskFilterProperty, true);
    }
    applyRoundedWidgetMask(container, "vaporViewComboPopupContainerRoundedMaskApplied");
    queueComboPopupContainerUpdate(container);
}

void applyComboPopupOpaqueBackground(QAbstractItemView *view)
{
    if (!view)
    {
        return;
    }

    const bool dark = view->property("vaporViewComboPopupDarkTheme").toBool();
    const QColor popupBase = appThemeColor(AppThemeColor::MenuPanel, dark);

    auto applyOpaqueFill = [popupBase](QWidget *widget) {
        if (!widget)
        {
            return;
        }

        QPalette palette = widget->palette();
        palette.setColor(QPalette::Window, popupBase);
        palette.setColor(QPalette::Base, popupBase);
        widget->setPalette(palette);
        widget->setAutoFillBackground(true);
        widget->setAttribute(Qt::WA_StyledBackground, true);
        widget->setAttribute(Qt::WA_TranslucentBackground, false);
        widget->setAttribute(Qt::WA_NoSystemBackground, false);
    };

    applyOpaqueFill(view->window());
    applyOpaqueFill(view);
    applyOpaqueFill(view->viewport());
    if (QWidget *viewport = view->viewport())
    {
        viewport->setStyleSheet(QStringLiteral("background-color: %1; border: none;")
                                    .arg(popupBase.name()));
    }
}

constexpr const char *kBrandDark = "#141413";
constexpr const char *kBrandLight = "#FAF9F5";
constexpr const char *kBrandMidGray = "#B0AEA5";
constexpr const char *kBrandLightGray = "#E8E6DC";
constexpr const char *kBrandOrange = "#D97757";
constexpr const char *kBrandBlue = "#6A9BCC";
constexpr const char *kBrandGreen = "#788C5D";
constexpr const char *kAppDarkCanvas = "#0D0D0D";
constexpr const char *kAppDarkSurface = "#171717";
constexpr const char *kAppDarkRaised = "#1F1F1F";
constexpr const char *kAppDarkAlt = "#242424";
constexpr const char *kAppDarkSubtle = "#2D2D2D";
constexpr const char *kAppDarkSunken = "#090909";
constexpr const char *kAppDarkBorder = "#2A2A2A";
constexpr const char *kAppDarkBorderStrong = "#3D3D3D";
constexpr const char *kLightPrimary = "#2A78D6";
constexpr const char *kLightPrimaryHover = "#236ABD";
constexpr const char *kLightPrimaryPressed = "#1D5AA3";
constexpr const char *kLightPrimarySubtle = "#E7F0FC";
constexpr const char *kLightPrimarySubtlePressed = "#D4E5F8";
constexpr const char *kLightToolbarBlue = kLightPrimary;
constexpr const char *kLightToolbarGreen = "#40C977";
constexpr const char *kLightToolbarRed = "#D24A30";
constexpr const char *kLightToolbarAmber = "#CA5223";
constexpr const char *kLightToolbarDisabled = "#7B786D";
constexpr const char *kClaudeLightCanvas = "#FDFDFC";
constexpr const char *kClaudeLightSurface = "#FFFFFF";
constexpr const char *kClaudeLightAlt = "#F7F7F6";
constexpr const char *kClaudeLightSubtle = "#F2F2F2";
constexpr const char *kClaudeLightPressed = "#E8E8E5";
constexpr const char *kClaudeLightBorder = "#E5E5E2";
constexpr const char *kClaudeLightBorderStrong = "#B8B7B0";
constexpr const char *kClaudeLightTextSecondary = "#5F5F5A";
constexpr const char *kClaudeLightTextMuted = "#8B8A84";
constexpr const char *kClaudeLightTextDisabled = "#A6A49D";

const ThemeReplacement kColorTokens[] = {
    {"@vv-window", AppThemeColor::Window},
    {"@vv-surface-raised", AppThemeColor::SurfaceRaised},
    {"@vv-surface-subtle", AppThemeColor::SurfaceSubtle},
    {"@vv-surface-sunken", AppThemeColor::SurfaceSunken},
    {"@vv-surface-alt", AppThemeColor::SurfaceAlt},
    {"@vv-surface", AppThemeColor::Surface},
    {"@vv-border-strong", AppThemeColor::BorderStrong},
    {"@vv-border", AppThemeColor::Border},
    {"@vv-text-title", AppThemeColor::TextTitle},
    {"@vv-text-strong", AppThemeColor::TextStrong},
    {"@vv-text-secondary", AppThemeColor::TextSecondary},
    {"@vv-text-muted", AppThemeColor::TextMuted},
    {"@vv-text-disabled-strong", AppThemeColor::TextDisabledStrong},
    {"@vv-text-disabled", AppThemeColor::TextDisabled},
    {"@vv-text-inverse", AppThemeColor::TextInverse},
    {"@vv-field-bg", AppThemeColor::FieldBackground},
    {"@vv-field-border", AppThemeColor::FieldBorder},
    {"@vv-config-window", AppThemeColor::ConfigWindow},
    {"@vv-config-surface", AppThemeColor::ConfigSurface},
    {"@vv-config-border", AppThemeColor::ConfigBorder},
    {"@vv-config-title-text", AppThemeColor::ConfigTitleText},
    {"@vv-config-muted-text", AppThemeColor::ConfigMutedText},
    {"@vv-config-toggle-inactive-text", AppThemeColor::ConfigToggleInactiveText},
    {"@vv-config-text", AppThemeColor::ConfigText},
    {"@vv-text", AppThemeColor::Text},
    {"@vv-primary-subtle-pressed", AppThemeColor::PrimarySubtlePressed},
    {"@vv-primary-subtle", AppThemeColor::PrimarySubtle},
    {"@vv-primary-pressed", AppThemeColor::PrimaryPressed},
    {"@vv-primary-hover", AppThemeColor::PrimaryHover},
    {"@vv-primary", AppThemeColor::Primary},
    {"@vv-focus", AppThemeColor::Focus},
    {"@vv-link", AppThemeColor::Link},
    {"@vv-tooltip-bg", AppThemeColor::TooltipBackground},
    {"@vv-disabled-fill", AppThemeColor::DisabledFill},
    {"@vv-control-arrow", AppThemeColor::ControlArrow},
    {"@vv-scrollbar-handle-hover", AppThemeColor::ScrollbarHandleHover},
    {"@vv-scrollbar-handle", AppThemeColor::ScrollbarHandle},
    {"@vv-title-hover", AppThemeColor::TitleBarHover},
    {"@vv-close-hover", AppThemeColor::CloseHover},
    {"@vv-menu-panel", AppThemeColor::MenuPanel},
    {"@vv-menu-hover", AppThemeColor::MenuHover},
    {"@vv-menu-text", AppThemeColor::MenuText},
    {"@vv-menu-meta", AppThemeColor::MenuMetaText},
    {"@vv-menu-check", AppThemeColor::MenuCheckText},
    {"@vv-menu-disabled", AppThemeColor::MenuDisabledText},
    {"@vv-accent-warm-hover", AppThemeColor::AccentWarmHover},
    {"@vv-accent-warm", AppThemeColor::AccentWarm},
    {"@vv-toolbar-blue", AppThemeColor::ToolbarBlue},
    {"@vv-toolbar-green", AppThemeColor::ToolbarGreen},
    {"@vv-toolbar-red", AppThemeColor::ToolbarRed},
    {"@vv-toolbar-amber", AppThemeColor::ToolbarAmber},
    {"@vv-toolbar-disabled", AppThemeColor::ToolbarDisabled},
    {"@vv-success-bg", AppThemeColor::SuccessBackground},
    {"@vv-success", AppThemeColor::Success},
    {"@vv-danger-bg", AppThemeColor::DangerBackground},
    {"@vv-danger", AppThemeColor::Danger},
    {"@vv-hd-ok-bg", AppThemeColor::HomeDeviceSuccessBackground},
    {"@vv-hd-ok", AppThemeColor::HomeDeviceSuccess},
    {"@vv-hd-bad-bg", AppThemeColor::HomeDeviceDangerBackground},
    {"@vv-hd-bad", AppThemeColor::HomeDeviceDanger},
    {"@vv-warning-bg", AppThemeColor::WarningBackground},
    {"@vv-warning", AppThemeColor::Warning},
    {"@vv-orange-bg", AppThemeColor::OrangeBackground},
    {"@vv-orange", AppThemeColor::Orange},
    {"@vv-error-text", AppThemeColor::ErrorText},
    {"@vv-error-highlight", AppThemeColor::ErrorHighlight},
    {"@vv-search-highlight", AppThemeColor::SearchHighlight},
    {"@vv-plot-light-guide-border", AppThemeColor::PlotLightGuideBorder},
    {"@vv-plot-light-guide", AppThemeColor::PlotLightGuide},
    {"@vv-plot-grid", AppThemeColor::PlotGrid},
    {"@vv-plot-border", AppThemeColor::PlotBorder},
    {"@vv-plot-text", AppThemeColor::PlotText},
    {"@vv-plot-muted", AppThemeColor::PlotMutedText},
    {"@vv-plot-axis-strong", AppThemeColor::PlotAxisStrong},
    {"@vv-plot-positive", AppThemeColor::PlotPositive},
    {"@vv-plot-sky", AppThemeColor::PlotSeriesSky},
    {"@vv-plot-temperature", AppThemeColor::PlotSeriesTemperature},
    {"@vv-plot-humidity", AppThemeColor::PlotSeriesHumidity},
    {"@vv-plot-pressure", AppThemeColor::PlotSeriesPressure},
    {"@vv-plot-wave-blue", AppThemeColor::PlotSeriesWaveBlue},
    {"@vv-plot-wave-orange", AppThemeColor::PlotSeriesWaveOrange},
    {"@vv-plot-current-guide-line", AppThemeColor::PlotCurrentGuideLine},
    {"@vv-plot-current-guide-label-fill", AppThemeColor::PlotCurrentGuideLabelFill},
    {"@vv-plot-current-guide-label-border", AppThemeColor::PlotCurrentGuideLabelBorder},
    {"@vv-plot-current-guide-label-text", AppThemeColor::PlotCurrentGuideLabelText},
    {"@vv-progress-chunk", AppThemeColor::ProgressChunk},
    {"@vv-range-selector-bg", AppThemeColor::RangeSelectorBackground},
    {"@vv-range-selector-handle-active", AppThemeColor::RangeSelectorHandleActive},
    {"@vv-range-selector-handle", AppThemeColor::RangeSelectorHandle},
    {"@vv-table-default-row", AppThemeColor::TableDefaultRow},
    {"@vv-table-text", AppThemeColor::TableText},
    {"@vv-table-grid", AppThemeColor::TableGrid},
    {"@vv-table-highlight-row", AppThemeColor::TableHighlightedRow},
    {"@vv-table-secondary-highlight-row", AppThemeColor::TableSecondaryHighlightedRow},
    {"@vv-table-dark-secondary-highlight-row", AppThemeColor::TableDarkSecondaryHighlight},
    {"@vv-popup-highlight-pressed", AppThemeColor::PopupHighlightPressed},
    {"@vv-popup-highlight", AppThemeColor::PopupHighlight},
    {"@vv-map-canvas", AppThemeColor::MapCanvas},
    {"@vv-map-viewport", AppThemeColor::MapViewport},
    {"@vv-map-tile-bg", AppThemeColor::MapTileBackground},
    {"@vv-map-tile-border", AppThemeColor::MapTileBorder},
    {"@vv-map-muted", AppThemeColor::MapMutedText},
    {"@vv-map-text", AppThemeColor::MapText},
    {"@vv-map-boundary", AppThemeColor::MapBoundary},
    {"@vv-map-grid", AppThemeColor::MapGrid},
    {"@vv-track-default", AppThemeColor::TrackDefault},
    {"@vv-track-start", AppThemeColor::TrackStart},
    {"@vv-track-end", AppThemeColor::TrackEnd},
    {"@vv-heatmap-0", AppThemeColor::Heatmap0},
    {"@vv-heatmap-1", AppThemeColor::Heatmap1},
    {"@vv-heatmap-2", AppThemeColor::Heatmap2},
    {"@vv-heatmap-3", AppThemeColor::Heatmap3},
    {"@vv-heatmap-4", AppThemeColor::Heatmap4},
    {"@vv-heatmap-5", AppThemeColor::Heatmap5},
    {"@vv-heatmap-6", AppThemeColor::Heatmap6},
    {"@vv-heatmap-7", AppThemeColor::Heatmap7},
    {"@vv-rtk-healthy", AppThemeColor::RtkHealthy},
    {"@vv-rtk-warning", AppThemeColor::RtkWarning},
    {"@vv-help-icon-hover", AppThemeColor::HelpIconHover},
    {"@vv-help-icon", AppThemeColor::HelpIcon},
    {"@vv-tui-background", AppThemeColor::TuiBackground},
    {"@vv-tui-accent", AppThemeColor::TuiAccent},
    {"@vv-tui-muted", AppThemeColor::TuiMuted},
    {"@vv-tui-green", AppThemeColor::TuiGreen},
    {"@vv-tui-yellow", AppThemeColor::TuiYellow},
    {"@vv-tui-red", AppThemeColor::TuiRed},
    {"@vv-tui-blue", AppThemeColor::TuiBlue},
    {"@vv-tui-gradient-0", AppThemeColor::TuiGradient0},
    {"@vv-tui-gradient-1", AppThemeColor::TuiGradient1},
    {"@vv-tui-gradient-2", AppThemeColor::TuiGradient2},
    {"@vv-tui-gradient-3", AppThemeColor::TuiGradient3},
    {"@vv-tui-gradient-4", AppThemeColor::TuiGradient4},
    {"@vv-tui-gradient-5", AppThemeColor::TuiGradient5},
    {"@vv-white", AppThemeColor::White},
    {"@vv-black", AppThemeColor::Black},
    {"@vv-transparent", AppThemeColor::Transparent}
};

}

QColor appThemeColor(AppThemeColor color, bool dark)
{
    switch (color)
    {
    case AppThemeColor::Window:
        return hexColor(dark ? kAppDarkCanvas : kClaudeLightCanvas);
    case AppThemeColor::Surface:
        return hexColor(dark ? kAppDarkSurface : kClaudeLightCanvas);
    case AppThemeColor::SurfaceRaised:
        return hexColor(dark ? kAppDarkRaised : kClaudeLightSurface);
    case AppThemeColor::SurfaceAlt:
        return hexColor(dark ? kAppDarkAlt : kClaudeLightAlt);
    case AppThemeColor::SurfaceSubtle:
        return hexColor(dark ? kAppDarkSubtle : kClaudeLightSubtle);
    case AppThemeColor::SurfaceSunken:
        return hexColor(dark ? kAppDarkSunken : kClaudeLightAlt);
    case AppThemeColor::Border:
        return hexColor(dark ? kAppDarkBorder : kClaudeLightBorder);
    case AppThemeColor::BorderStrong:
        return hexColor(dark ? kAppDarkBorderStrong : kClaudeLightBorderStrong);
    case AppThemeColor::Text:
        return hexColor(dark ? kBrandLight : kBrandDark);
    case AppThemeColor::TextStrong:
        return hexColor(dark ? "#FFFFFF" : kBrandDark);
    case AppThemeColor::TextTitle:
        return hexColor(dark ? kBrandLight : kBrandDark);
    case AppThemeColor::TextSecondary:
        return hexColor(dark ? "#D3D0C6" : kClaudeLightTextSecondary);
    case AppThemeColor::TextMuted:
        return hexColor(dark ? kBrandMidGray : kClaudeLightTextMuted);
    case AppThemeColor::TextDisabled:
        return hexColor(dark ? "#7D7A70" : kClaudeLightTextDisabled);
    case AppThemeColor::TextDisabledStrong:
        return hexColor(dark ? kBrandLightGray : kClaudeLightCanvas);
    case AppThemeColor::TextInverse:
        return hexColor(kClaudeLightSurface);
    case AppThemeColor::FieldBackground:
        return hexColor(dark ? kAppDarkSurface : kClaudeLightSurface);
    case AppThemeColor::FieldBorder:
        return hexColor(dark ? kAppDarkBorder : kClaudeLightBorder);
    case AppThemeColor::ConfigWindow:
        return hexColor(dark ? kAppDarkCanvas : kClaudeLightCanvas);
    case AppThemeColor::ConfigSurface:
        return hexColor(dark ? kAppDarkSurface : kClaudeLightSurface);
    case AppThemeColor::ConfigBorder:
        return hexColor(dark ? kAppDarkBorder : kClaudeLightBorder);
    case AppThemeColor::ConfigTitleText:
        return hexColor(dark ? kBrandLight : kBrandDark);
    case AppThemeColor::ConfigText:
        return hexColor(dark ? "#E8E6DC" : "#292825");
    case AppThemeColor::ConfigMutedText:
        return hexColor(dark ? kBrandMidGray : kClaudeLightTextMuted);
    case AppThemeColor::ConfigToggleInactiveText:
        return hexColor(dark ? kBrandLightGray : kClaudeLightTextSecondary);
    case AppThemeColor::Primary:
        return hexColor(dark ? kBrandOrange : kLightPrimary);
    case AppThemeColor::PrimaryHover:
        return hexColor(dark ? "#E98A67" : kLightPrimaryHover);
    case AppThemeColor::PrimaryPressed:
        return hexColor(dark ? "#F09A7D" : kLightPrimaryPressed);
    case AppThemeColor::PrimarySubtle:
        return hexColor(dark ? "#3A211A" : kLightPrimarySubtle);
    case AppThemeColor::PrimarySubtlePressed:
        return hexColor(dark ? "#5A3024" : kLightPrimarySubtlePressed);
    case AppThemeColor::Focus:
        return hexColor(dark ? kBrandOrange : kBrandBlue);
    case AppThemeColor::Link:
        return hexColor(kBrandBlue);
    case AppThemeColor::TooltipBackground:
        return hexColor(dark ? kAppDarkSurface : kBrandDark);
    case AppThemeColor::DisabledFill:
        return hexColor(dark ? kAppDarkBorder : kClaudeLightPressed);
    case AppThemeColor::ControlArrow:
        return hexColor(dark ? kBrandMidGray : kClaudeLightTextMuted);
    case AppThemeColor::ScrollbarHandle:
        return hexColor("#E2E2E2");
    case AppThemeColor::ScrollbarHandleHover:
        return hexColor("#57595A");
    case AppThemeColor::TitleBarHover:
        return hexColor(dark ? kAppDarkAlt : kClaudeLightSubtle);
    case AppThemeColor::CloseHover:
        return hexColor(dark ? "#3A211A" : "#F1DAD2");
    case AppThemeColor::MenuPanel:
        return hexColor(dark ? kAppDarkSurface : kClaudeLightSurface);
    case AppThemeColor::MenuHover:
        return hexColor(dark ? kAppDarkAlt : kClaudeLightSubtle);
    case AppThemeColor::MenuText:
        return hexColor(dark ? kBrandLight : kBrandDark);
    case AppThemeColor::MenuMetaText:
        return hexColor(dark ? "#D3D0C6" : kClaudeLightTextSecondary);
    case AppThemeColor::MenuCheckText:
        return hexColor(dark ? kBrandLight : kBrandDark);
    case AppThemeColor::MenuDisabledText:
        return hexColor(dark ? "#7D7A70" : kClaudeLightTextDisabled);
    case AppThemeColor::AccentWarm:
        return hexColor(kBrandOrange);
    case AppThemeColor::AccentWarmHover:
        return hexColor("#E98A67");
    case AppThemeColor::ToolbarBlue:
        return hexColor(dark ? kBrandOrange : kLightToolbarBlue);
    case AppThemeColor::ToolbarGreen:
        return hexColor(kLightToolbarGreen);
    case AppThemeColor::ToolbarRed:
        return hexColor(kLightToolbarRed);
    case AppThemeColor::ToolbarAmber:
        return hexColor(kLightToolbarAmber);
    case AppThemeColor::ToolbarDisabled:
        return hexColor(kLightToolbarDisabled);
    case AppThemeColor::Success:
        return hexColor(dark ? "#AFC38B" : kBrandGreen);
    case AppThemeColor::SuccessBackground:
        return hexColor(dark ? "#202719" : "#EEF1E8");
    case AppThemeColor::Danger:
        return hexColor(dark ? "#F09A7D" : "#C8543D");
    case AppThemeColor::DangerBackground:
        return hexColor(dark ? "#341C17" : "#F5DED6");
    case AppThemeColor::HomeDeviceSuccess:
        return hexColor(dark ? "#4ADE80" : "#22C55E");
    case AppThemeColor::HomeDeviceSuccessBackground:
        return hexColor(dark ? "#12331F" : "#E4F8EB");
    case AppThemeColor::HomeDeviceDanger:
        return hexColor(dark ? "#FB7185" : "#EF4444");
    case AppThemeColor::HomeDeviceDangerBackground:
        return hexColor(dark ? "#3F1518" : "#FDECEC");
    case AppThemeColor::Warning:
        return hexColor(dark ? "#E9A07D" : kBrandOrange);
    case AppThemeColor::WarningBackground:
        return hexColor(dark ? "#352016" : "#F4E3D8");
    case AppThemeColor::Orange:
        return hexColor(kBrandOrange);
    case AppThemeColor::OrangeBackground:
        return hexColor("#F4E3D8");
    case AppThemeColor::ErrorText:
        return hexColor(dark ? "#F09A7D" : "#A6422F");
    case AppThemeColor::ErrorHighlight:
        return hexColor("#F4D5CB");
    case AppThemeColor::SearchHighlight:
        return hexColor("#F3D1C4");
    case AppThemeColor::PlotGrid:
        return hexColor(dark ? kAppDarkBorder : kClaudeLightPressed);
    case AppThemeColor::PlotBorder:
        return hexColor(dark ? kAppDarkBorder : kClaudeLightBorder);
    case AppThemeColor::PlotText:
        return hexColor(dark ? "#D3D0C6" : kClaudeLightTextSecondary);
    case AppThemeColor::PlotMutedText:
        return hexColor(dark ? kBrandMidGray : kClaudeLightTextMuted);
    case AppThemeColor::PlotAxisStrong:
        return hexColor(dark ? kBrandLight : kBrandDark);
    case AppThemeColor::PlotLightGuide:
        return hexColor(kBrandOrange);
    case AppThemeColor::PlotLightGuideBorder:
        return hexColor("#B96449");
    case AppThemeColor::PlotPositive:
        return hexColor(dark ? "#AFC38B" : kBrandGreen);
    case AppThemeColor::PlotSeriesSky:
        return hexColor(kBrandBlue);
    case AppThemeColor::PlotSeriesTemperature:
        return hexColor("#C8543D");
    case AppThemeColor::PlotSeriesHumidity:
        return hexColor(kBrandBlue);
    case AppThemeColor::PlotSeriesPressure:
        return hexColor("#40C977");
    case AppThemeColor::PlotSeriesWaveBlue:
        return hexColor(dark ? kBrandBlue : kLightPrimary);
    case AppThemeColor::PlotSeriesWaveOrange:
        return hexColor(kBrandOrange);
    case AppThemeColor::PlotCurrentGuideLine:
        return hexColor(kBrandOrange);
    case AppThemeColor::PlotCurrentGuideLabelFill:
        return hexColor("#7A3D2D");
    case AppThemeColor::PlotCurrentGuideLabelBorder:
        return hexColor("#5B2F25");
    case AppThemeColor::PlotCurrentGuideLabelText:
        return hexColor(kBrandLight);
    case AppThemeColor::ProgressChunk:
        return hexColor(dark ? kBrandOrange : kLightPrimary);
    case AppThemeColor::RangeSelectorBackground:
        return hexColor(dark ? kAppDarkBorder : kClaudeLightSubtle);
    case AppThemeColor::RangeSelectorHandle:
        return hexColor(kBrandBlue);
    case AppThemeColor::RangeSelectorHandleActive:
        return hexColor(dark ? kBrandBlue : kLightPrimary);
    case AppThemeColor::TableDefaultRow:
        return hexColor("#FFFFFF");
    case AppThemeColor::TableText:
        return hexColor(kBrandDark);
    case AppThemeColor::TableGrid:
        return hexColor(kClaudeLightBorder);
    case AppThemeColor::TableHighlightedRow:
        return hexColor(dark ? "#5A3024" : "#D7E8FA");
    case AppThemeColor::TableSecondaryHighlightedRow:
        return hexColor(dark ? "#3A211A" : "#EEF6FF");
    case AppThemeColor::TableDarkSecondaryHighlight:
        return hexColor("#3A211A");
    case AppThemeColor::PopupHighlight:
        return hexColor(dark ? kAppDarkAlt : kClaudeLightSubtle);
    case AppThemeColor::PopupHighlightPressed:
        return hexColor(dark ? kAppDarkBorder : kClaudeLightPressed);
    case AppThemeColor::MapCanvas:
        return hexColor(kClaudeLightCanvas);
    case AppThemeColor::MapViewport:
        return hexColor(kClaudeLightAlt);
    case AppThemeColor::MapTileBackground:
        return hexColor(kClaudeLightSubtle);
    case AppThemeColor::MapTileBorder:
        return hexColor(kClaudeLightBorder);
    case AppThemeColor::MapText:
        return hexColor(kClaudeLightTextSecondary);
    case AppThemeColor::MapMutedText:
        return hexColor(kClaudeLightTextMuted);
    case AppThemeColor::MapBoundary:
        return hexColor(kBrandMidGray);
    case AppThemeColor::MapGrid:
        return QColor(253, 253, 252, 120);
    case AppThemeColor::TrackDefault:
        return hexColor(kBrandBlue);
    case AppThemeColor::TrackStart:
        return hexColor(kBrandGreen);
    case AppThemeColor::TrackEnd:
        return hexColor("#C8543D");
    case AppThemeColor::Heatmap0:
        return hexColor("#2F5F91");
    case AppThemeColor::Heatmap1:
        return hexColor(kBrandBlue);
    case AppThemeColor::Heatmap2:
        return hexColor("#93B5D8");
    case AppThemeColor::Heatmap3:
        return hexColor(kBrandGreen);
    case AppThemeColor::Heatmap4:
        return hexColor("#A8B68A");
    case AppThemeColor::Heatmap5:
        return hexColor("#D7B982");
    case AppThemeColor::Heatmap6:
        return hexColor(kBrandOrange);
    case AppThemeColor::Heatmap7:
        return hexColor("#C8543D");
    case AppThemeColor::RtkHealthy:
        return hexColor(kBrandGreen);
    case AppThemeColor::RtkWarning:
        return hexColor(kBrandOrange);
    case AppThemeColor::HelpIcon:
        return hexColor(kBrandBlue);
    case AppThemeColor::HelpIconHover:
        return hexColor(dark ? "#93B5D8" : kBrandBlue);
    case AppThemeColor::TuiBackground:
        return hexColor(kBrandDark);
    case AppThemeColor::TuiAccent:
        return hexColor(kBrandBlue);
    case AppThemeColor::TuiMuted:
        return hexColor(kBrandMidGray);
    case AppThemeColor::TuiGreen:
        return hexColor(kBrandGreen);
    case AppThemeColor::TuiYellow:
        return hexColor(kBrandOrange);
    case AppThemeColor::TuiRed:
        return hexColor("#C8543D");
    case AppThemeColor::TuiBlue:
        return hexColor(kBrandBlue);
    case AppThemeColor::TuiGradient0:
        return hexColor(kBrandLight);
    case AppThemeColor::TuiGradient1:
        return hexColor(kBrandLightGray);
    case AppThemeColor::TuiGradient2:
        return hexColor(kBrandMidGray);
    case AppThemeColor::TuiGradient3:
        return hexColor(kBrandGreen);
    case AppThemeColor::TuiGradient4:
        return hexColor(kBrandBlue);
    case AppThemeColor::TuiGradient5:
        return hexColor(kBrandOrange);
    case AppThemeColor::White:
        return hexColor("#FFFFFF");
    case AppThemeColor::Black:
        return hexColor("#000000");
    case AppThemeColor::Transparent:
        return QColor(0, 0, 0, 0);
    }
    return QColor();
}

QString appThemeColorName(AppThemeColor color, bool dark)
{
    return cssColor(appThemeColor(color, dark));
}

QString appThemeRgba(AppThemeColor color, bool dark, qreal alpha)
{
    return cssRgba(appThemeColor(color, dark), alpha);
}

bool isDarkThemePalette(const QPalette& palette)
{
    return palette.color(QPalette::Window).lightness() < 128 ||
           palette.color(QPalette::Base).lightness() < 128;
}

bool isDarkThemeEnabled()
{
    if (qApp)
    {
        const QVariant value = qApp->property(kAppDarkThemeProperty);
        if (value.isValid())
        {
            return value.toBool();
        }
        return isDarkThemePalette(qApp->palette());
    }
    return false;
}

QPalette appThemePalette(bool dark)
{
    const QPalette base = qApp && qApp->style()
        ? qApp->style()->standardPalette()
        : QPalette();
    return appThemePalette(dark, base);
}

QPalette appThemePalette(bool dark, const QPalette& basePalette)
{
    QPalette palette = basePalette;
    if (!dark)
    {
        return palette;
    }

    palette.setColor(QPalette::Window, appThemeColor(AppThemeColor::Window, true));
    palette.setColor(QPalette::WindowText, appThemeColor(AppThemeColor::TextTitle, true));
    palette.setColor(QPalette::Base, appThemeColor(AppThemeColor::Surface, true));
    palette.setColor(QPalette::AlternateBase, appThemeColor(AppThemeColor::SurfaceAlt, true));
    palette.setColor(QPalette::Text, appThemeColor(AppThemeColor::Text, true));
    palette.setColor(QPalette::Button, appThemeColor(AppThemeColor::Surface, true));
    palette.setColor(QPalette::ButtonText, appThemeColor(AppThemeColor::Text, true));
    palette.setColor(QPalette::BrightText, appThemeColor(AppThemeColor::White, true));
    palette.setColor(QPalette::Light, appThemeColor(AppThemeColor::SurfaceAlt, true));
    palette.setColor(QPalette::Midlight, appThemeColor(AppThemeColor::SurfaceAlt, true));
    palette.setColor(QPalette::Mid, appThemeColor(AppThemeColor::SurfaceAlt, true));
    palette.setColor(QPalette::Dark, appThemeColor(AppThemeColor::SurfaceSunken, true));
    palette.setColor(QPalette::Shadow, appThemeColor(AppThemeColor::SurfaceSunken, true));
    palette.setColor(QPalette::Highlight, appThemeColor(AppThemeColor::PrimarySubtlePressed, true));
    palette.setColor(QPalette::HighlightedText, appThemeColor(AppThemeColor::White, true));
    palette.setColor(QPalette::ToolTipBase, appThemeColor(AppThemeColor::Surface, true));
    palette.setColor(QPalette::ToolTipText, appThemeColor(AppThemeColor::Text, true));
    palette.setColor(QPalette::Link, appThemeColor(AppThemeColor::Link, true));
    palette.setColor(QPalette::Disabled, QPalette::WindowText, appThemeColor(AppThemeColor::TextDisabled, true));
    palette.setColor(QPalette::Disabled, QPalette::Text, appThemeColor(AppThemeColor::TextDisabled, true));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, appThemeColor(AppThemeColor::TextDisabled, true));
    return palette;
}

void configureComboBoxPopup(QComboBox *combo, bool dark)
{
    if (!combo)
    {
        return;
    }

    combo->setProperty("vaporViewComboPopupStyled", true);

    QAbstractItemView *view = combo->view();
    if (!view)
    {
        return;
    }
    view->setObjectName(QStringLiteral("vaporViewComboPopupView"));
    view->setProperty("vaporViewComboPopupStyled", true);
    view->setProperty("vaporViewComboPopupDarkTheme", dark);
    view->setProperty(kComboPopupOwnerProperty, QVariant::fromValue<QObject *>(combo));
    view->setMouseTracking(true);
    view->setFrameShape(QFrame::NoFrame);
    view->setLineWidth(0);
    view->setAutoFillBackground(true);
    view->setAttribute(Qt::WA_StyledBackground, true);

    const QColor popupBase = appThemeColor(AppThemeColor::MenuPanel, dark);
    const QColor popupText = appThemeColor(AppThemeColor::MenuText, dark);
    const QColor popupHighlight = appThemeColor(AppThemeColor::MenuHover, dark);
    const QColor popupHighlightText = appThemeColor(AppThemeColor::MenuText, dark);
    const QColor disabledText = appThemeColor(AppThemeColor::MenuDisabledText, dark);
    QPalette popupPalette = view->palette();
    popupPalette.setColor(QPalette::Base, popupBase);
    popupPalette.setColor(QPalette::Text, popupText);
    popupPalette.setColor(QPalette::Highlight, popupBase);
    popupPalette.setColor(QPalette::HighlightedText, popupText);
    popupPalette.setColor(QPalette::Disabled, QPalette::Text, disabledText);
    view->setPalette(popupPalette);
    view->setStyleSheet(QStringLiteral(
        "QAbstractItemView#vaporViewComboPopupView { "
        "background-color: %1; border: none; "
        "color: %2; outline: 0px; padding: 12px 0px; "
        "selection-background-color: transparent; selection-color: %2; }"
        "QAbstractItemView#vaporViewComboPopupView::item { "
        "background-color: transparent; border: 0px; border-radius: 0px; "
        "min-height: 30px; padding: 7px 14px; }"
        "QAbstractItemView#vaporViewComboPopupView::item:selected, "
        "QAbstractItemView#vaporViewComboPopupView::item:selected:active, "
        "QAbstractItemView#vaporViewComboPopupView::item:selected:!active { "
        "background-color: transparent; color: %2; }"
        "QAbstractItemView#vaporViewComboPopupView::item:hover { "
        "background-color: %3; color: %4; }"
        "QAbstractItemView#vaporViewComboPopupView::item:disabled { "
        "background-color: transparent; color: %5; }"
        "QAbstractItemView#vaporViewComboPopupView::item:selected:disabled { "
        "background-color: transparent; color: %5; }")
        .arg(popupBase.name(),
             popupText.name(),
             popupHighlight.name(),
             popupHighlightText.name(),
             disabledText.name()));
    applyComboPopupOpaqueBackground(view);
    configureComboPopupContainerMask(view);
}

QString applyAppThemeTokens(QString styleSheet, bool dark)
{
    for (const ThemeReplacement& replacement : kColorTokens)
    {
        styleSheet.replace(QString::fromLatin1(replacement.token), appThemeColorName(replacement.color, dark));
    }

    styleSheet.replace(QStringLiteral("@vv-resize-hover"), appThemeRgba(AppThemeColor::Primary, dark, 0.18));
    styleSheet.replace(QStringLiteral("@vv-resize-pressed"), appThemeRgba(AppThemeColor::Primary, dark, 0.28));
    styleSheet.replace(QStringLiteral("@vv-help-hover-bg"), appThemeRgba(AppThemeColor::HelpIcon, dark, dark ? 0.14 : 0.10));
    styleSheet.replace(QStringLiteral("@vv-transparent"), QStringLiteral("transparent"));
    return styleSheet;
}

QString startupAppThemeStyleSheet(bool dark)
{
    if (!dark)
    {
        return QString();
    }

    return applyAppThemeTokens(QStringLiteral(
        "QWidget, QMainWindow { background-color: @vv-window; color: @vv-text-title; }"
        "QMenuBar, QToolBar, QStatusBar { background-color: @vv-surface; color: @vv-text-title; }"
        "QPushButton { background-color: @vv-primary; color: @vv-white; }"),
        true);
}

}
