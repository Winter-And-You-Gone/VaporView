#include "AppTheme.h"

#include <QApplication>
#include <QStyle>
#include <QVariant>

#include <algorithm>

namespace VaporView
{
namespace
{

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

constexpr const char *kBrandDark = "#141413";
constexpr const char *kBrandLight = "#FAF9F5";
constexpr const char *kBrandMidGray = "#B0AEA5";
constexpr const char *kBrandLightGray = "#E8E6DC";
constexpr const char *kBrandOrange = "#D97757";
constexpr const char *kBrandBlue = "#6A9BCC";
constexpr const char *kBrandGreen = "#788C5D";
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
        return hexColor(dark ? kBrandDark : kClaudeLightCanvas);
    case AppThemeColor::Surface:
        return hexColor(dark ? "#1C1B19" : kClaudeLightCanvas);
    case AppThemeColor::SurfaceRaised:
        return hexColor(dark ? "#23221F" : kClaudeLightSurface);
    case AppThemeColor::SurfaceAlt:
        return hexColor(dark ? "#2A2925" : kClaudeLightAlt);
    case AppThemeColor::SurfaceSubtle:
        return hexColor(dark ? "#33312C" : kClaudeLightSubtle);
    case AppThemeColor::SurfaceSunken:
        return hexColor(dark ? "#0F0F0E" : kClaudeLightAlt);
    case AppThemeColor::Border:
        return hexColor(dark ? "#34322D" : kClaudeLightBorder);
    case AppThemeColor::BorderStrong:
        return hexColor(dark ? "#4C4941" : kClaudeLightBorderStrong);
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
        return hexColor(dark ? "#1C1B19" : kClaudeLightSurface);
    case AppThemeColor::FieldBorder:
        return hexColor(dark ? "#34322D" : kClaudeLightBorder);
    case AppThemeColor::ConfigWindow:
        return hexColor(dark ? kBrandDark : kClaudeLightCanvas);
    case AppThemeColor::ConfigSurface:
        return hexColor(dark ? "#1C1B19" : kClaudeLightSurface);
    case AppThemeColor::ConfigBorder:
        return hexColor(dark ? "#34322D" : kClaudeLightBorder);
    case AppThemeColor::ConfigTitleText:
        return hexColor(dark ? kBrandLight : kBrandDark);
    case AppThemeColor::ConfigText:
        return hexColor(dark ? "#E8E6DC" : "#292825");
    case AppThemeColor::ConfigMutedText:
        return hexColor(dark ? kBrandMidGray : kClaudeLightTextMuted);
    case AppThemeColor::ConfigToggleInactiveText:
        return hexColor(dark ? kBrandLightGray : kClaudeLightTextSecondary);
    case AppThemeColor::Primary:
        return hexColor(kBrandOrange);
    case AppThemeColor::PrimaryHover:
        return hexColor(dark ? "#E98A67" : "#C96747");
    case AppThemeColor::PrimaryPressed:
        return hexColor(dark ? "#F09A7D" : "#B85A3D");
    case AppThemeColor::PrimarySubtle:
        return hexColor(dark ? "#3A211A" : "#F3DFD7");
    case AppThemeColor::PrimarySubtlePressed:
        return hexColor(dark ? "#5A3024" : "#EAC7BA");
    case AppThemeColor::Focus:
        return hexColor(kBrandBlue);
    case AppThemeColor::Link:
        return hexColor(kBrandBlue);
    case AppThemeColor::TooltipBackground:
        return hexColor(dark ? "#1C1B19" : kBrandDark);
    case AppThemeColor::DisabledFill:
        return hexColor(dark ? "#34322D" : kClaudeLightPressed);
    case AppThemeColor::ControlArrow:
        return hexColor(dark ? kBrandMidGray : kClaudeLightTextMuted);
    case AppThemeColor::ScrollbarHandle:
        return hexColor(dark ? "#4C4941" : kClaudeLightBorderStrong);
    case AppThemeColor::ScrollbarHandleHover:
        return hexColor(dark ? "#68645B" : "#94938D");
    case AppThemeColor::TitleBarHover:
        return hexColor(dark ? "#23221F" : kClaudeLightSubtle);
    case AppThemeColor::CloseHover:
        return hexColor(dark ? "#3A211A" : "#F1DAD2");
    case AppThemeColor::MenuPanel:
        return hexColor(dark ? "#1C1B19" : kClaudeLightSurface);
    case AppThemeColor::MenuHover:
        return hexColor(dark ? "#2A2925" : kClaudeLightSubtle);
    case AppThemeColor::MenuText:
        return hexColor(dark ? kBrandLight : kBrandDark);
    case AppThemeColor::MenuMetaText:
        return hexColor(dark ? "#D3D0C6" : kClaudeLightTextSecondary);
    case AppThemeColor::MenuCheckText:
        return hexColor(dark ? "#AFC38B" : kBrandGreen);
    case AppThemeColor::MenuDisabledText:
        return hexColor(dark ? "#7D7A70" : kClaudeLightTextDisabled);
    case AppThemeColor::AccentWarm:
        return hexColor(kBrandOrange);
    case AppThemeColor::AccentWarmHover:
        return hexColor("#E98A67");
    case AppThemeColor::ToolbarBlue:
        return hexColor(kBrandBlue);
    case AppThemeColor::ToolbarGreen:
        return hexColor(kBrandGreen);
    case AppThemeColor::ToolbarRed:
        return hexColor("#C8543D");
    case AppThemeColor::ToolbarAmber:
        return hexColor(kBrandOrange);
    case AppThemeColor::ToolbarDisabled:
        return hexColor(kBrandMidGray);
    case AppThemeColor::Success:
        return hexColor(dark ? "#AFC38B" : kBrandGreen);
    case AppThemeColor::SuccessBackground:
        return hexColor(dark ? "#202719" : "#EEF1E8");
    case AppThemeColor::Danger:
        return hexColor(dark ? "#F09A7D" : "#C8543D");
    case AppThemeColor::DangerBackground:
        return hexColor(dark ? "#341C17" : "#F5DED6");
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
        return hexColor(dark ? "#34322D" : kClaudeLightPressed);
    case AppThemeColor::PlotBorder:
        return hexColor(dark ? "#34322D" : kClaudeLightBorder);
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
        return hexColor(kBrandGreen);
    case AppThemeColor::PlotSeriesWaveBlue:
        return hexColor(kBrandBlue);
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
        return hexColor(kBrandOrange);
    case AppThemeColor::RangeSelectorBackground:
        return hexColor(dark ? "#34322D" : kClaudeLightSubtle);
    case AppThemeColor::RangeSelectorHandle:
        return hexColor(kBrandBlue);
    case AppThemeColor::RangeSelectorHandleActive:
        return hexColor(kBrandOrange);
    case AppThemeColor::TableDefaultRow:
        return hexColor("#FFFFFF");
    case AppThemeColor::TableText:
        return hexColor(kBrandDark);
    case AppThemeColor::TableGrid:
        return hexColor(kClaudeLightBorder);
    case AppThemeColor::TableHighlightedRow:
        return hexColor("#E5EEF6");
    case AppThemeColor::TableSecondaryHighlightedRow:
        return hexColor("#EEF1E8");
    case AppThemeColor::TableDarkSecondaryHighlight:
        return hexColor("#2A3A2A");
    case AppThemeColor::PopupHighlight:
        return hexColor(dark ? "#2A2925" : kClaudeLightSubtle);
    case AppThemeColor::PopupHighlightPressed:
        return hexColor(dark ? "#34322D" : kClaudeLightPressed);
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
