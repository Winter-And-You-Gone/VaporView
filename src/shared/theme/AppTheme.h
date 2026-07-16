#ifndef VAPORVIEW_APP_THEME_H_
#define VAPORVIEW_APP_THEME_H_

#include <QColor>
#include <QPalette>
#include <QString>

class QComboBox;

namespace VaporView
{

enum class AppThemeColor
{
    Window,
    Surface,
    SurfaceRaised,
    SurfaceAlt,
    SurfaceSubtle,
    SurfaceSunken,
    Border,
    BorderStrong,
    Text,
    TextStrong,
    TextTitle,
    TextSecondary,
    TextMuted,
    TextDisabled,
    TextDisabledStrong,
    TextInverse,
    FieldBackground,
    FieldBorder,
    ConfigWindow,
    ConfigSurface,
    ConfigBorder,
    ConfigTitleText,
    ConfigText,
    ConfigMutedText,
    ConfigToggleInactiveText,
    Primary,
    PrimaryHover,
    PrimaryPressed,
    PrimarySubtle,
    PrimarySubtlePressed,
    Focus,
    Link,
    TooltipBackground,
    DisabledFill,
    ControlArrow,
    ScrollbarHandle,
    ScrollbarHandleHover,
    TitleBarHover,
    CloseHover,
    MenuPanel,
    MenuHover,
    MenuText,
    MenuMetaText,
    MenuCheckText,
    MenuDisabledText,
    AccentWarm,
    AccentWarmHover,
    ToolbarBlue,
    ToolbarGreen,
    ToolbarRed,
    ToolbarAmber,
    ToolbarDisabled,
    Success,
    SuccessBackground,
    Danger,
    DangerBackground,
    HomeDeviceSuccess,
    HomeDeviceSuccessBackground,
    HomeDeviceDanger,
    HomeDeviceDangerBackground,
    Warning,
    WarningBackground,
    Orange,
    OrangeBackground,
    ErrorText,
    ErrorHighlight,
    SearchHighlight,
    PlotGrid,
    PlotBorder,
    PlotText,
    PlotMutedText,
    PlotAxisStrong,
    PlotLightGuide,
    PlotLightGuideBorder,
    PlotPositive,
    PlotSeriesSky,
    PlotSeriesTemperature,
    PlotSeriesHumidity,
    PlotSeriesPressure,
    PlotSeriesWaveBlue,
    PlotSeriesWaveOrange,
    PlotCurrentGuideLine,
    PlotCurrentGuideLabelFill,
    PlotCurrentGuideLabelBorder,
    PlotCurrentGuideLabelText,
    ProgressChunk,
    RangeSelectorBackground,
    RangeSelectorHandle,
    RangeSelectorHandleActive,
    TableDefaultRow,
    TableText,
    TableGrid,
    TableHighlightedRow,
    TableSecondaryHighlightedRow,
    TableDarkSecondaryHighlight,
    PopupHighlight,
    PopupHighlightPressed,
    MapCanvas,
    MapViewport,
    MapTileBackground,
    MapTileBorder,
    MapText,
    MapMutedText,
    MapBoundary,
    MapGrid,
    TrackDefault,
    TrackStart,
    TrackEnd,
    Heatmap0,
    Heatmap1,
    Heatmap2,
    Heatmap3,
    Heatmap4,
    Heatmap5,
    Heatmap6,
    Heatmap7,
    RtkHealthy,
    RtkWarning,
    HelpIcon,
    HelpIconHover,
    TuiBackground,
    TuiAccent,
    TuiMuted,
    TuiGreen,
    TuiYellow,
    TuiRed,
    TuiBlue,
    TuiGradient0,
    TuiGradient1,
    TuiGradient2,
    TuiGradient3,
    TuiGradient4,
    TuiGradient5,
    White,
    Black,
    Transparent
};

constexpr const char *kAppDarkThemeProperty = "vaporViewDarkTheme";

QColor appThemeColor(AppThemeColor color, bool dark);
QString appThemeColorName(AppThemeColor color, bool dark);
QString appThemeRgba(AppThemeColor color, bool dark, qreal alpha);

bool isDarkThemePalette(const QPalette& palette);
bool isDarkThemeEnabled();

QPalette appThemePalette(bool dark);
QPalette appThemePalette(bool dark, const QPalette& basePalette);

void configureComboBoxPopup(QComboBox *combo, bool dark);

QString applyAppThemeTokens(QString styleSheet, bool dark);
QString startupAppThemeStyleSheet(bool dark);

}

#endif
