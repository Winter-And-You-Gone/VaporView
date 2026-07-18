#include "ground/main/GroundMainWindowSupport.h"

#include <QApplication>
#include <QWidget>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <dwmapi.h>
#include <windows.h>
#endif

namespace VaporView::Ground::MainSupport
{

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
QPushButton#appSidebarButton:hover,
QPushButton#appSidebarButton[_vv_hover="true"] {
    background-color: @vv-title-hover;
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
    border-radius: 10px;
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
QToolButton#homeDeviceActionButton[deviceConfigAction="true"] {
    background-color: transparent;
    border: none;
}
QToolButton#homeDeviceActionButton[deviceConfigAction="true"]:hover {
    background-color: @vv-primary-subtle;
    border: none;
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
QWidget#tcpWaveCardOutline {
    background-color: transparent;
    border: 1px solid @vv-border;
    border-radius: 8px;
}
QFrame#appSidebar {
    background-color: @vv-surface;
    border: 1px solid @vv-border;
    border-radius: 8px;
}
QAbstractSpinBox[spinArrowHover="up"]::up-arrow {
    image: url(lucide/chevron-up-primary-dark.svg);
}
QAbstractSpinBox[spinArrowHover="down"]::down-arrow {
    image: url(lucide/chevron-down-primary-dark.svg);
}
QComboBox#temperatureTitlePortCombo::down-arrow {
    image: url(lucide/chevron-down-primary-dark.svg);
}
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


} // namespace VaporView::Ground::MainSupport
