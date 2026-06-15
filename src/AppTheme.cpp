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
        return hexColor(dark ? "#0D0D0D" : "#FDFDFC");
    case AppThemeColor::Surface:
        return hexColor(dark ? "#121212" : "#FDFDFC");
    case AppThemeColor::SurfaceRaised:
        return hexColor(dark ? "#181818" : "#FFFFFF");
    case AppThemeColor::SurfaceAlt:
        return hexColor(dark ? "#202020" : "#F8F8F7");
    case AppThemeColor::SurfaceSubtle:
        return hexColor(dark ? "#242424" : "#EEEEEC");
    case AppThemeColor::SurfaceSunken:
        return hexColor(dark ? "#0C0C0C" : "#FCFCFB");
    case AppThemeColor::Border:
        return hexColor(dark ? "#202020" : "#EAEAE9");
    case AppThemeColor::BorderStrong:
        return hexColor(dark ? "#2A2A2A" : "#BDBDBD");
    case AppThemeColor::Text:
        return hexColor(dark ? "#E5E7EB" : "#000000");
    case AppThemeColor::TextStrong:
        return hexColor(dark ? "#F9FAFB" : "#111827");
    case AppThemeColor::TextTitle:
        return hexColor(dark ? "#D8DEE9" : "#000000");
    case AppThemeColor::TextSecondary:
        return hexColor(dark ? "#A7B4C2" : "#5E6B78");
    case AppThemeColor::TextMuted:
        return hexColor(dark ? "#8FA1B3" : "#7A8899");
    case AppThemeColor::TextDisabled:
        return hexColor(dark ? "#64748B" : "#9CA3AF");
    case AppThemeColor::TextDisabledStrong:
        return hexColor(dark ? "#CBD5E1" : "#FFFFFF");
    case AppThemeColor::TextInverse:
        return hexColor("#FFFFFF");
    case AppThemeColor::FieldBackground:
        return hexColor(dark ? "#121212" : "#EEF0F3");
    case AppThemeColor::FieldBorder:
        return hexColor(dark ? "#202020" : "#D8DDE5");
    case AppThemeColor::ConfigWindow:
        return hexColor(dark ? "#0D0D0D" : "#F3F5F7");
    case AppThemeColor::ConfigSurface:
        return hexColor(dark ? "#121212" : "#FBFCFE");
    case AppThemeColor::ConfigBorder:
        return hexColor(dark ? "#202020" : "#DFE4EA");
    case AppThemeColor::ConfigTitleText:
        return hexColor(dark ? "#E5E7EB" : "#1F2937");
    case AppThemeColor::ConfigText:
        return hexColor(dark ? "#D8DEE9" : "#1F2A35");
    case AppThemeColor::ConfigMutedText:
        return hexColor(dark ? "#94A3B8" : "#64748B");
    case AppThemeColor::ConfigToggleInactiveText:
        return hexColor(dark ? "#D8DEE9" : "#475569");
    case AppThemeColor::Primary:
        return hexColor(dark ? "#D97757" : "#1976D2");
    case AppThemeColor::PrimaryHover:
        return hexColor(dark ? "#D97757" : "#1565C0");
    case AppThemeColor::PrimaryPressed:
        return hexColor(dark ? "#D97757" : "#0D47A1");
    case AppThemeColor::PrimarySubtle:
        return hexColor(dark ? "#1F3F66" : "#E3F2FD");
    case AppThemeColor::PrimarySubtlePressed:
        return hexColor(dark ? "#245B8F" : "#BBDEFB");
    case AppThemeColor::Focus:
        return hexColor(dark ? "#3B82F6" : "#1976D2");
    case AppThemeColor::Link:
        return hexColor(dark ? "#7DB7FF" : "#1976D2");
    case AppThemeColor::TooltipBackground:
        return hexColor(dark ? "#121212" : "#323232");
    case AppThemeColor::DisabledFill:
        return hexColor(dark ? "#202020" : "#BDBDBD");
    case AppThemeColor::ControlArrow:
        return hexColor(dark ? "#8FA1B3" : "#757575");
    case AppThemeColor::ScrollbarHandle:
        return hexColor(dark ? "#475569" : "#BDBDBD");
    case AppThemeColor::ScrollbarHandleHover:
        return hexColor(dark ? "#64748B" : "#9E9E9E");
    case AppThemeColor::TitleBarHover:
        return hexColor(dark ? "#121212" : "#EFEEEB");
    case AppThemeColor::CloseHover:
        return hexColor(dark ? "#121212" : "#FEE2E2");
    case AppThemeColor::MenuPanel:
        return hexColor(dark ? "#121212" : "#FDFDFC");
    case AppThemeColor::MenuHover:
        return hexColor(dark ? "#202020" : "#EEEEEE");
    case AppThemeColor::MenuText:
        return hexColor(dark ? "#F3F6FB" : "#000000");
    case AppThemeColor::MenuMetaText:
        return hexColor(dark ? "#D7DCE2" : "#4B5563");
    case AppThemeColor::MenuCheckText:
        return hexColor(dark ? "#9AA0A6" : "#6B7280");
    case AppThemeColor::MenuDisabledText:
        return hexColor(dark ? "#777777" : "#9CA3AF");
    case AppThemeColor::AccentWarm:
        return hexColor("#D97757");
    case AppThemeColor::AccentWarmHover:
        return hexColor("#EF8F35");
    case AppThemeColor::ToolbarBlue:
        return QColor(40, 105, 190);
    case AppThemeColor::ToolbarGreen:
        return QColor(35, 150, 95);
    case AppThemeColor::ToolbarRed:
        return QColor(205, 72, 72);
    case AppThemeColor::ToolbarAmber:
        return QColor(220, 150, 35);
    case AppThemeColor::ToolbarDisabled:
        return QColor(145, 150, 158);
    case AppThemeColor::Success:
        return hexColor(dark ? "#68D391" : "#43A047");
    case AppThemeColor::SuccessBackground:
        return hexColor(dark ? "#123423" : "#E8F5E9");
    case AppThemeColor::Danger:
        return hexColor(dark ? "#F87171" : "#E53935");
    case AppThemeColor::DangerBackground:
        return hexColor(dark ? "#3A171B" : "#FFEBEE");
    case AppThemeColor::Warning:
        return hexColor(dark ? "#F6AD55" : "#EF6C00");
    case AppThemeColor::WarningBackground:
        return hexColor(dark ? "#3A2A12" : "#FFF3E0");
    case AppThemeColor::Orange:
        return hexColor("#FB8C00");
    case AppThemeColor::OrangeBackground:
        return hexColor("#FFF3E0");
    case AppThemeColor::ErrorText:
        return hexColor(dark ? "#FCA5A5" : "#B42318");
    case AppThemeColor::ErrorHighlight:
        return hexColor("#FFE1E1");
    case AppThemeColor::SearchHighlight:
        return hexColor("#FFEF9A");
    case AppThemeColor::PlotGrid:
        return hexColor(dark ? "#202020" : "#E3E8EF");
    case AppThemeColor::PlotBorder:
        return hexColor(dark ? "#202020" : "#CFD7E3");
    case AppThemeColor::PlotText:
        return hexColor(dark ? "#A7B4C2" : "#5E6B78");
    case AppThemeColor::PlotMutedText:
        return hexColor(dark ? "#8FA1B3" : "#7A8899");
    case AppThemeColor::PlotAxisStrong:
        return hexColor(dark ? "#A7B4C2" : "#334155");
    case AppThemeColor::PlotLightGuide:
        return hexColor("#F0D000");
    case AppThemeColor::PlotLightGuideBorder:
        return hexColor("#C9B53A");
    case AppThemeColor::PlotPositive:
        return hexColor(dark ? "#56D364" : "#1B6416");
    case AppThemeColor::PlotSeriesSky:
        return hexColor("#66D0FF");
    case AppThemeColor::PlotSeriesTemperature:
        return hexColor("#D14343");
    case AppThemeColor::PlotSeriesHumidity:
        return hexColor("#2F7FD3");
    case AppThemeColor::PlotSeriesPressure:
        return hexColor("#2F9D57");
    case AppThemeColor::PlotSeriesWaveBlue:
        return hexColor("#4E79C7");
    case AppThemeColor::PlotSeriesWaveOrange:
        return hexColor("#EF8F35");
    case AppThemeColor::PlotCurrentGuideLine:
        return hexColor("#FFB347");
    case AppThemeColor::PlotCurrentGuideLabelFill:
        return hexColor("#8B4A00");
    case AppThemeColor::PlotCurrentGuideLabelBorder:
        return hexColor("#5F3000");
    case AppThemeColor::PlotCurrentGuideLabelText:
        return hexColor("#FFF7EA");
    case AppThemeColor::ProgressChunk:
        return hexColor(dark ? "#D97757" : "#245B8F");
    case AppThemeColor::RangeSelectorBackground:
        return hexColor(dark ? "#202020" : "#E7EDF5");
    case AppThemeColor::RangeSelectorHandle:
        return hexColor("#7FB3FF");
    case AppThemeColor::RangeSelectorHandleActive:
        return hexColor("#2F6FD6");
    case AppThemeColor::TableDefaultRow:
        return hexColor("#FFFFFF");
    case AppThemeColor::TableText:
        return hexColor("#1F2933");
    case AppThemeColor::TableGrid:
        return hexColor("#E5E7EB");
    case AppThemeColor::TableHighlightedRow:
        return hexColor("#C7E3FF");
    case AppThemeColor::TableSecondaryHighlightedRow:
        return hexColor("#E8F3FF");
    case AppThemeColor::TableDarkSecondaryHighlight:
        return hexColor("#17384F");
    case AppThemeColor::PopupHighlight:
        return hexColor(dark ? "#242424" : "#EEEEEC");
    case AppThemeColor::PopupHighlightPressed:
        return hexColor(dark ? "#2A2A2A" : "#DEDEDC");
    case AppThemeColor::MapCanvas:
        return hexColor("#F7FAFC");
    case AppThemeColor::MapViewport:
        return hexColor("#EEF4FB");
    case AppThemeColor::MapTileBackground:
        return hexColor("#EDF2F7");
    case AppThemeColor::MapTileBorder:
        return hexColor("#D7DEE7");
    case AppThemeColor::MapText:
        return hexColor("#4A5568");
    case AppThemeColor::MapMutedText:
        return hexColor("#718096");
    case AppThemeColor::MapBoundary:
        return hexColor("#CBD5E1");
    case AppThemeColor::MapGrid:
        return QColor(255, 255, 255, 120);
    case AppThemeColor::TrackDefault:
        return hexColor("#2563EB");
    case AppThemeColor::TrackStart:
        return hexColor("#16A34A");
    case AppThemeColor::TrackEnd:
        return hexColor("#DC2626");
    case AppThemeColor::Heatmap0:
        return hexColor("#1D4ED8");
    case AppThemeColor::Heatmap1:
        return hexColor("#2563EB");
    case AppThemeColor::Heatmap2:
        return hexColor("#38BDF8");
    case AppThemeColor::Heatmap3:
        return hexColor("#22D3EE");
    case AppThemeColor::Heatmap4:
        return hexColor("#67E8F9");
    case AppThemeColor::Heatmap5:
        return hexColor("#A3E635");
    case AppThemeColor::Heatmap6:
        return hexColor("#FDE047");
    case AppThemeColor::Heatmap7:
        return hexColor("#DC2626");
    case AppThemeColor::RtkHealthy:
        return hexColor("#2E7D32");
    case AppThemeColor::RtkWarning:
        return hexColor("#A26A00");
    case AppThemeColor::HelpIcon:
        return hexColor(dark ? "#8AB4F8" : "#1976D2");
    case AppThemeColor::HelpIconHover:
        return hexColor(dark ? "#8AB4F8" : "#1976D2");
    case AppThemeColor::TuiBackground:
        return QColor(0, 0, 0);
    case AppThemeColor::TuiAccent:
        return QColor(100, 155, 255);
    case AppThemeColor::TuiMuted:
        return QColor(135, 143, 156);
    case AppThemeColor::TuiGreen:
        return QColor(95, 220, 150);
    case AppThemeColor::TuiYellow:
        return QColor(255, 218, 55);
    case AppThemeColor::TuiRed:
        return QColor(255, 110, 110);
    case AppThemeColor::TuiBlue:
        return QColor(100, 155, 255);
    case AppThemeColor::TuiGradient0:
        return QColor(255, 238, 80);
    case AppThemeColor::TuiGradient1:
        return QColor(255, 218, 55);
    case AppThemeColor::TuiGradient2:
        return QColor(255, 185, 55);
    case AppThemeColor::TuiGradient3:
        return QColor(245, 135, 95);
    case AppThemeColor::TuiGradient4:
        return QColor(190, 110, 190);
    case AppThemeColor::TuiGradient5:
        return QColor(100, 155, 255);
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
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, hexColor("#94A3B8"));
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
