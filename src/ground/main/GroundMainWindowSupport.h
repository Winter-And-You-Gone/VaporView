#pragma once

#include "shared/theme/AppTheme.h"
#include "shared/theme/TopLevelCardStyle.h"
#include "TelemetryTypes.h"
#include "data_types.h"

#include <QColor>
#include <QDateTime>
#include <QEvent>
#include <QFont>
#include <QIcon>
#include <QImage>
#include <QKeySequence>
#include <QPixmap>
#include <QRect>
#include <QSize>
#include <QString>
#include <QStringList>
#include <Qt>

#include <cstdint>
#include <functional>
#include <map>
#include <vector>

class QAction;
class QAbstractButton;
class QCheckBox;
class QComboBox;
class QFrame;
class QGridLayout;
class QLabel;
class QObject;
class QSettings;
class QToolButton;
class QWidget;

namespace VaporView::Ground::MainSupport
{

inline constexpr int kFloatingMenuShadowMarginPx = 22;
inline constexpr int kFloatingMenuCornerRadiusPx = 10;
inline constexpr int kMaxLogEntryCount = 5000;
inline constexpr const char *kTooltipShortcutProperty = "_vv_tooltip_shortcut";
inline constexpr const char *kBaseMinWidthProperty = "_vv_base_min_width";
inline constexpr const char *kBaseMinHeightProperty = "_vv_base_min_height";
inline constexpr const char *kBaseMaxWidthProperty = "_vv_base_max_width";
inline constexpr const char *kBaseMaxHeightProperty = "_vv_base_max_height";
inline constexpr const char *kBaseSpacingProperty = "_vv_base_spacing";
inline constexpr const char *kBaseMarginsLeftProperty = "_vv_base_margin_left";
inline constexpr const char *kBaseMarginsTopProperty = "_vv_base_margin_top";
inline constexpr const char *kBaseMarginsRightProperty = "_vv_base_margin_right";
inline constexpr const char *kBaseMarginsBottomProperty = "_vv_base_margin_bottom";
inline constexpr const char *kTextWidthCandidatesProperty = "_vv_text_width_candidates";
inline constexpr const char *kTextWidthPaddingProperty = "_vv_text_width_padding";
inline constexpr const char *kNumericWidthCandidatesProperty = "_vv_numeric_width_candidates";
inline constexpr const char *kNumericWidthPaddingProperty = "_vv_numeric_width_padding";
inline constexpr const char *kMainCardMinimumHeightProperty = "_vv_main_card_minimum_height";
inline constexpr const char *kDeviceConfigRemoteActionProperty = "deviceConfigRemoteAction";
inline constexpr const char *kDeviceConfigRemoteCommandProperty = "deviceConfigRemoteCommand";
inline constexpr const char *kDeviceConfigRemoteDeviceProperty = "deviceConfigRemoteDevice";
inline constexpr const char *kLocalSerialPortComboProperty = "_vv_local_serial_port_combo";
inline constexpr const char *kLocalSerialPortManualHandlerProperty = "_vv_local_serial_manual_handler";
inline constexpr const char *kLocalSerialPortManualEntryProperty = "_vv_local_serial_manual_entry";
inline constexpr const char *kLocalSerialPortManualPreviousTextProperty = "_vv_local_serial_manual_previous_text";
inline constexpr const char *kLocalSerialPortManualOptionData = "__vv_manual_serial_port__";
inline constexpr int kLocalSerialPortHistoryItemRole = Qt::UserRole + 1;
inline constexpr int kMainPageInputHeight = 36;
inline constexpr int kMainPageButtonHeight = kMainPageInputHeight;
inline constexpr int kDeviceConfigAutoDetectButtonMinWidth = 124;
inline constexpr int kDeviceConfigSourceModeComboWidth = 156;
inline constexpr int kDeviceConfigSkyDeviceButtonMinWidth = 132;
inline constexpr int kDeviceConfigTopButtonPadding = 24;
inline constexpr int kHomeDeviceButtonSize = 32;
inline constexpr int kHomeDeviceIconSize = 18;
inline constexpr int kHomeDeviceCapsuleHeight = 32;
inline constexpr int kHomeDeviceRowHeight = kHomeDeviceButtonSize;
inline constexpr int kHomeDeviceGridColumns = 3;
inline constexpr int kHomeDeviceGridRows = 2;
inline constexpr int kHomeDeviceGridRowGap = 2;
inline constexpr int kHomeDeviceItemGap = 12;
inline constexpr int kHomeDeviceActionSpinnerFrames = 30;
inline constexpr int kHomeDeviceActionSpinnerIntervalMs = 25;
inline constexpr int kHomeDeviceActionSpinnerMinimumMs = 1000;
inline constexpr int kMainPageTitleBarHeight = kMainPageInputHeight + 4;
inline constexpr int kEnvironmentTitleBarHeight = kMainPageButtonHeight;
inline constexpr int kHomeOverviewCardOuterPadding = 1;
inline constexpr int kHomeOverviewBodyPadding = 2;
inline constexpr int kConfigFormBottomPadding = 4;
inline constexpr int kConfigHomeBodyBottomPadding = kHomeOverviewBodyPadding;
inline constexpr int kConfigCardBottomPadding = kHomeOverviewCardOuterPadding;
inline constexpr int kHomeTelemetrySummaryHeightPadding = 4;
inline constexpr int kConfigCardMinHeight =
    kMainPageTitleBarHeight + kMainPageButtonHeight + kConfigHomeBodyBottomPadding + kConfigCardBottomPadding;
inline constexpr int kHomeOverviewDeviceMinWidth = 568;
inline constexpr int kHomeOverviewTemperatureMinWidth = 380;
inline constexpr int kHomeOverviewSplitterHandleWidth = 12;
inline constexpr const char *kHomeOverviewSplitterInitializedProperty = "_vv_home_overview_splitter_initialized";
inline constexpr int kSensorNavigationStretch = 4;
inline constexpr int kSensorEnvironmentStretch = 1;
inline constexpr int kTcpWaveCardMinHeight = 430;
inline constexpr int kCompactTcpWaveCardMinHeight = 560;
inline constexpr int kAppSidebarIconOnlyBaseWidth = 62;
inline constexpr int kAppSidebarFullBaseWidth = 122;
inline constexpr int kAppSidebarVisualPadding = 8;
inline constexpr int kAppSidebarTopBottomPadding = 6;
inline constexpr int kAppSidebarButtonHeight = 48;
inline constexpr int kAppSidebarCompactButtonSize = 44;
inline constexpr int kAppSidebarFullIconSize = 20;
inline constexpr int kAppSidebarCompactIconSize = 32;
inline constexpr int kMainCardResizeHandleHeight = 3;
inline constexpr int kTopLevelCardGap = 12;
inline constexpr int kTopLevelCardChromeInset = 12;
inline constexpr int kTopLevelCardOuterVerticalInset =
    kTopLevelCardGap - kAppSidebarVisualPadding;
inline constexpr int kMainContentBottomShadowGap = 8;
inline constexpr int kSidePanelSplitterVisualWidth = 0;
inline constexpr int kTopLevelCardSpacerAfterResizeHandle = kTopLevelCardGap - kMainCardResizeHandleHeight;
inline constexpr const char *kMainCardUserResizedHeightProperty = "_vv_main_card_user_resized_height";
inline constexpr const char *kMainCardResizeDraggingProperty = "_vv_main_card_resize_dragging";
inline constexpr int kEnvStatusIconSize = 18;
inline constexpr int kEpsilonSideTitleWidth = 24;
inline constexpr int kEpsilonTitleColumnWidth = 90;
inline constexpr int kEpsilonMotionTitleColumnWidth = 180;
inline constexpr int kEpsilonLeftValueColumnWidth = 130;
inline constexpr int kEpsilonPositionValueColumnWidth = 112;
inline constexpr int kEpsilonMotionValueColumnWidth = 145;
inline constexpr int kEpsilonFieldBaseSpacing = 2;
inline constexpr int kEpsilonMotionFieldSpacing = 8;
inline constexpr int kEpsilonFieldMinimumHeight = 20;
inline constexpr int kTemperatureControllerPlotWidth = 260;
inline constexpr int kTemperatureControllerPlotMinHeight = 190;
inline constexpr int kTemperatureTitlePortChromeWidth = 38;
inline constexpr int kTemperatureTitlePortMinimumWidth = 64;
inline constexpr int kTemperatureTitlePortMaximumWidth = 220;
inline constexpr int kTemperatureControllerInputWidth = 112;
inline constexpr int kTemperatureControllerWideInputWidth = 138;
inline constexpr int kTemperatureControllerRs485BaudWidth = 100;
inline constexpr int kTemperatureControllerTopEnableWidth = 106;
inline constexpr int kTemperatureControllerTopEnableHeight = 34;
inline constexpr int kTemperatureControllerTopModeWidth = 132;
inline constexpr int kTemperatureControllerTopTargetWidth = 172;
inline constexpr int kTemperatureControllerCompactInputWidth = 112;
inline constexpr int kTemperatureControllerCompactPidInputWidth = 82;
inline constexpr int kTemperatureControllerAdvancedInputWidth = 128;
inline constexpr int kTemperatureControllerSensorInputWidth = 82;
inline constexpr int kTemperatureControllerPtCoefficientInputWidth = 104;
inline constexpr int kTemperatureControllerPolynomialInputWidth = 62;
inline constexpr int kTemperatureControllerSensorFieldSpacing = 6;
inline constexpr int kTemperatureControllerSensorLabelPadding = 16;
inline constexpr int kTemperatureControllerMaxOutputLabelWidth = 168;
inline constexpr int kTemperatureControllerCompactLabelWidth = 72;
inline constexpr int kTemperatureControllerControlLabelWidth = 150;
inline constexpr int kTemperatureControllerConfigRowHeight = 38;
inline constexpr int kTemperatureControllerTopControlsHeight = 38;
inline constexpr int kTemperatureControllerNavigationButtonHeight = 30;
inline constexpr int kTemperatureControllerNavigationHorizontalMargin = 4;
inline constexpr int kTemperatureControllerNavigationVerticalMargin = 3;
inline constexpr int kTemperatureControllerNavigationSpacing = 4;
inline constexpr int kTemperatureControllerRowSpacing = 8;
inline constexpr int kTemperatureControllerChannelConfigSubStackHeight =
    kTemperatureControllerConfigRowHeight * 5 + kTemperatureControllerRowSpacing * 4;
inline constexpr int kTemperatureControllerChannelStackHeight =
    kTemperatureControllerChannelConfigSubStackHeight + kTemperatureControllerConfigRowHeight +
    kTemperatureControllerRowSpacing;
inline constexpr int kTemperatureControllerCommonStackHeight = kTemperatureControllerChannelStackHeight;
inline constexpr int kTemperatureControllerHistoryLimit = 240;
inline constexpr int kDefaultMainWindowWidth = 1280;
inline constexpr int kDefaultMainWindowHeight = 800;
inline constexpr int kMinimumMainWindowWidth = 1024;
inline constexpr int kMinimumMainWindowHeight = 640;
inline constexpr int kCompactHomeScreenWidth = 1600;
inline constexpr int kCompactHomeScreenHeight = 900;
inline constexpr int kCompactHomeViewportWidth = 1400;
inline constexpr quint64 kImuPpsSyncWindowUs = 2ULL * 1000ULL * 1000ULL;
inline constexpr const char *kMainWindowProperty = "vaporViewMainWindow";
inline constexpr const char *kEnglishProperty = "vaporViewEnglish";
inline constexpr const char *kSectionTitleIconNameProperty = "_vv_section_title_icon_name";
inline constexpr const char *kSidebarIconNameProperty = "_vv_sidebar_icon_name";
inline constexpr const char *kSidebarCompactProperty = "_vv_sidebar_compact";
inline constexpr const char *kSidebarHoverProperty = "_vv_hover";
inline constexpr const char *kSidebarHoverParticipantProperty = "_vv_sidebar_hover_button";
inline constexpr const char *kTitleBarHoverProperty = "titleBarHover";
inline constexpr const char *kTitleBarHoverParticipantProperty = "_vv_title_bar_hover_button";
inline constexpr const char *kCustomLogoStateProperty = "_vv_logo_state";
inline constexpr int kSectionTitleIconBoxSize = 26;
inline constexpr int kSectionTitleIconSize = 22;
inline constexpr char kSensorBaudSourceProperty[] = "sensorBaudSource";

bool isTemperatureCommonCommand(CommandId command);
int rememberedTemperatureSlaveAddress();
QString formatTemperaturePolynomial(qint64 mantissa, int exponent);
bool parseTemperaturePolynomial(const QString& text, qint64& mantissa, qint16& exponent);
QString formatTemperatureSensorDecimal(qint64 scaledValue, double scale, int decimals);
QString formatTemperatureSensorDouble(double value, int decimals);

void installMenuItemEventFilter(QObject *target,
                                std::function<void()> hoverCallback,
                                std::function<void()> clickCallback = {});
QFrame *createFloatingTitleMenuPanel(QWidget *parent);
QRect floatingMenuContentRect(QWidget *panel);
void setFloatingMenuContentFixedSize(QWidget *panel, const QSize& size);
QWidget *createAppSidebarFrame(QWidget *parent);
QWidget *createMainCardResizeHandle(QWidget *target, int minimumHeight, QWidget *parent);
QWidget *createShrinkablePanel(QWidget *parent);
QWidget *createWindowResizeHandle(Qt::Edges edges, QWidget *parent);
QCheckBox *createTitleBarFeedbackCheckBox(QWidget *parent);
void installSpinBoxArrowHoverFilter(QObject *application);

QString shortcutText(const QKeySequence& sequence);
QString shortcutTextFromAction(const QAction *action);
QString shortcutTextFromWidget(QWidget *widget);
void fitButtonMinimumWidth(QAbstractButton *button, int floorWidth = 0);
void fitButtonFixedWidth(QAbstractButton *button, int floorWidth = 0, int padding = 24);
QString shortcutTextFromTooltipSuffix(QString& text);
void hideAppTooltipPopup();
bool showAppTooltip(QObject *watched, QEvent *event, bool dark);

void configureTemperatureControllerTwoRowGrid(QGridLayout *layout, int horizontalSpacing);
QFont numericFontFrom(const QFont& base);
int widestTextWidth(const QFont& font, const QStringList& candidates);
void applyFixedNumericLabelWidth(QLabel *label, const QStringList& candidates, int padding = 0);
void setFixedNumericLabelWidth(QLabel *label, const QStringList& candidates, int padding = 0);
void applyFixedTextLabelWidth(QLabel *label, const QStringList& candidates, int padding = 0);
void setFixedTextLabelWidth(QLabel *label, const QStringList& candidates, int padding = 0);
void refreshFixedTextLabelWidth(QLabel *label);
void refreshFixedNumericLabelWidth(QLabel *label);
void polishNumericLabel(QLabel *label);
QStringList environmentFieldLabelWidthCandidates();
QStringList temperatureControllerFieldLabelWidthCandidates();
QStringList temperatureControllerStatusLabelWidthCandidates();
QStringList temperatureControllerCompactStatusLabelWidthCandidates();
QStringList temperatureControllerRateLabelWidthCandidates();
QString fixedTextField(const QString& text, int width, Qt::Alignment alignment = Qt::AlignRight);
QString fixedDecimalWithUnit(double value, int decimals, int numberWidth, const QString& unit);
QString compactDecimalWithUnit(double value, int decimals, const QString& unit);

QString skyDeviceDisplayName(SkyDeviceId device);
QString homeDeviceDisplayName(SkyDeviceId device, bool english);
QString formatBitRate(double bitsPerSecond);
QString formatFrequencyText(double hz);
QString remoteNoDataText(bool english);
QString remoteDisconnectedText(bool english);
QString remoteStaleText(bool english);
QString formatElapsedCompact(quint64 elapsedMs);

QString findResourceFile(const QString& relativePath);
QPixmap renderLucidePixmap(const QByteArray& svgData, const QColor& color, qreal devicePixelRatio);
void addLucideIconPixmaps(QIcon& icon, const QByteArray& svgData, const QColor& color, QIcon::Mode mode);
QIcon createLucideIcon(const QString& iconName, const QColor& color);
QString deviceConfigRemoteActionKey(CommandId command);
QString deviceConfigRemoteActionText(CommandId command, bool english);
QString deviceConfigRemoteIconName(CommandId command);
QColor deviceConfigRemoteIconColor(CommandId command);
void applyDeviceConfigRemoteButtonPresentation(QToolButton *button,
                                               CommandId command,
                                               SkyDeviceId device,
                                               bool english,
                                               bool applyMetrics);
QIcon createRotatedLucideIcon(const QString& iconName, const QColor& color, int degrees);
bool isHoverEnterLikeEvent(QEvent::Type type);
bool isHoverLeaveLikeEvent(QEvent::Type type);
bool widgetContainsGlobalCursor(const QWidget *widget, const QPoint& cursorPos);
void setWidgetBooleanProperty(QWidget *widget, const char *propertyName, bool enabled);
void setDangerTextPalette(QWidget *widget);
void configureHoverParticipant(QWidget *widget, const char *participantProperty, QObject *eventFilter);
QColor sectionTitleIconColor(bool dark);
void updateSectionTitleIcon(QLabel *iconLabel, bool dark);
void updateSectionTitleIcons(QWidget *root, bool dark);
void setSectionTitleIconName(QLabel *titleLabel, const QString& iconName, bool dark);
QLabel *createSectionTitleCluster(QWidget *parent,
                                  const QString& iconName,
                                  int titleHeight,
                                  QWidget **clusterOut);
QString vaporViewLogoResourcePath(bool dark);
QPixmap renderVaporViewLogo(bool dark, int size, qreal devicePixelRatio);
QIcon createVaporViewLogoIcon(bool dark);
QIcon createRefreshIcon();
QIcon createConnectIcon();
QIcon createCancelIcon();
QIcon createDisconnectIcon();
QIcon createPlayIcon();
QIcon createPauseIcon();
QIcon createStopIcon();
QIcon createTimerIcon();
QIcon createRtkSatelliteIcon(bool running);
QIcon createClearLogIcon();
QIcon createLogFilterIcon();
QIcon createLogSidePanelToggleIcon(bool collapsed);
QIcon createAppSidebarToggleIcon(bool collapsed);
QIcon createMenuCheckIcon();
QIcon createMenuCheckIcon(bool dark);
QIcon createWaveformViewerIcon();
QIcon createLanguageIcon();
QIcon createDarkThemeIcon();
QIcon createLightThemeIcon();
QIcon createTitleBarIcon(const QString& iconName, bool dark);
bool isDarkToolbarTheme();
QColor toolbarColor(AppThemeColor color);

QString titleApplicationPanelStyleSheet(bool dark);
QString customTitleBarStyleSheet(bool dark);
void setWindowsTitleBarDark(QWidget *window, bool dark);
QString darkThemeStyleSheet();
QString darkOverviewStyleSheet();
QString mainCardsScrollBarBackgroundStyleSheet(bool dark);
QString mainCardsTopLevelCardStyleSheet();
QString rtkConfigCardStyleSheet();

QString imuFrameTypeName(ImuFrameType type);
void applyComboText(QComboBox *combo, const QString& value);
QString sensorBaudSettingsKey(const QString& source);
QString sensorDefaultBaud(const QString& source);
QString rememberedSensorBaud(const QSettings& settings,
                             const QString& source,
                             const QString& legacyKey = QString());
void saveRememberedSensorBaud(QSettings& settings,
                              const QString& source,
                              const QComboBox *baudCombo);
QString sourceModeDisplayText(bool english, int index);
QString sourceModeStorageValue(int index);
QString skyTelemetryTransportDisplayText(bool english, const QString& transport);
void updateSkyTelemetryTransportComboTexts(QComboBox *combo, bool english);
int sourceModeIndexFromStoredValue(const QString& value);
bool shouldMirrorToErrorLog(const QString& message);
void rememberBaseMetric(QObject *object, const char *propertyName, int value);


} // namespace VaporView::Ground::MainSupport
