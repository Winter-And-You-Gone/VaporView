#include "ground/main/GroundMainWindowImplementation.h"
#include "ground/widgets/SerialPortComboSupport.h"

void MainWindow::loadModernStyleSheet()
{
    if (!qApp->property("spinArrowHoverFilterInstalled").toBool())
    {
        installSpinBoxArrowHoverFilter(qApp);
        qApp->setProperty("spinArrowHoverFilterInstalled", true);
    }

    const QDir appDir(QCoreApplication::applicationDirPath());
    const QString chevronDownIconPath = findResourceFile(QStringLiteral("resources/lucide/chevron-down.svg")).replace('\\', '/');
    const QString chevronDownPrimaryIconPath = findResourceFile(QStringLiteral("resources/lucide/chevron-down-primary.svg")).replace('\\', '/');
    const QString chevronUpIconPath = findResourceFile(QStringLiteral("resources/lucide/chevron-up.svg")).replace('\\', '/');
    const QString chevronUpPrimaryIconPath = findResourceFile(QStringLiteral("resources/lucide/chevron-up-primary.svg")).replace('\\', '/');
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
        state_->base_style_sheet_ = QString::fromUtf8(styleFile.readAll());
        styleFile.close();

        const QFileInfo styleInfo(stylePath);
        const QString resourceDir = styleInfo.absolutePath();
        const QString comboArrowPath = QDir(resourceDir).absoluteFilePath("combo_arrow_down.xpm").replace('\\', '/');
        const QString comboArrowUpPath = QDir(resourceDir).absoluteFilePath("combo_arrow_up.xpm").replace('\\', '/');
        const QString squareIconPath = QDir(resourceDir).absoluteFilePath("lucide/square.svg").replace('\\', '/');
        const QString squareCheckIconPath = QDir(resourceDir).absoluteFilePath("lucide/square-check-big.svg").replace('\\', '/');
        state_->base_style_sheet_.replace("url(combo_arrow_down.xpm)", QString("url(%1)").arg(comboArrowPath));
        state_->base_style_sheet_.replace("url(combo_arrow_up.xpm)", QString("url(%1)").arg(comboArrowUpPath));
        state_->base_style_sheet_.replace("url(lucide/square.svg)", QString("url(%1)").arg(squareIconPath));
        state_->base_style_sheet_.replace("url(lucide/square-check-big.svg)", QString("url(%1)").arg(squareCheckIconPath));
    }
    else
    {
        state_->base_style_sheet_ =
            "* { font-family: \"Segoe UI\", \"Microsoft YaHei\", \"PingFang SC\", sans-serif; }"
            "QMainWindow { background-color: @vv-surface; }"
            "QWidget#appCentralWidget, QWidget#mainCardsPane, QFrame#appSidebar, QStackedWidget#mainPageStack, QWidget#temperaturePage, QWidget#deviceConfigPage, QWidget#logSidePanel, QMainWindow#sessionViewerWindow, QWidget#sessionViewerCentralWidget, QWidget#sessionViewerViewport, QWidget#sessionViewerContentPane, QScrollArea#mainCardsScrollArea, QScrollArea#sessionViewerScrollArea, QWidget#mainCardsViewport, QScrollArea#mainCardsScrollArea > QWidget, QScrollArea#mainCardsScrollArea > QWidget > QWidget, QScrollArea#sessionViewerScrollArea > QWidget, QScrollArea#sessionViewerScrollArea > QWidget > QWidget, QSplitter#appLayoutSplitter, QSplitter#mainContentSplitter, QSplitter#homeOverviewSplitter, QSplitter#homeOverviewSplitter > QWidget, QSplitter#sessionViewerContentSplitter { background-color: @vv-surface; }"
            "QFrame#appSidebar { background-color: @vv-surface; border: 1px solid @vv-border; border-radius: 8px; }"
            "QPushButton#appSidebarButton { background-color: transparent; border: 1px solid transparent; border-radius: 6px; color: @vv-text; font-weight: 600; min-height: 34px; max-height: 34px; padding: 6px 8px; text-align: left; outline: none; }"
            "QPushButton#appSidebarButton:focus { outline: none; }"
            "QPushButton#appSidebarButton[_vv_sidebar_compact=\"true\"] { min-width: 42px; max-width: 42px; min-height: 42px; max-height: 42px; padding: 0px; text-align: center; outline: none; }"
            "QPushButton#appSidebarButton:hover, QPushButton#appSidebarButton[_vv_hover=\"true\"] { background-color: @vv-title-hover; color: @vv-text; }"
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
            "QWidget#tcpWaveCardOutline { background-color: transparent; border: 1px solid @vv-border; border-radius: 8px; }"
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
            "TemperatureControllerPanel QLabel#rateLabel[temperatureControllerRateValue=\"true\"] { font-size: 16px; font-weight: 700; }"
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
            "QComboBox { background-color: @vv-field-bg; border: 1px solid @vv-border; border-radius: 6px; padding: 4px 10px; min-height: 26px; max-height: 26px; color: @vv-text; font-size: 14px; }"
            "QComboBox:hover { border-color: @vv-border-strong; }"
            "QComboBox:focus { border-color: @vv-primary; border-width: 1px; }"
            "QComboBox:disabled { background-color: @vv-surface-alt; color: @vv-text; }"
            "QComboBox::drop-down { border: none; width: 20px; border-top-right-radius: 4px; border-bottom-right-radius: 4px; }"
            "QComboBox::down-arrow { image: url(lucide/chevron-down.svg); width: 12px; height: 12px; margin-right: 6px; }"
            "QComboBox QAbstractItemView { background-color: @vv-menu-panel; border: none; border-radius: 10px; color: @vv-menu-text; selection-background-color: @vv-menu-hover; selection-color: @vv-menu-text; padding: 12px 0px; outline: none; }"
            "QComboBox QAbstractItemView::item { background-color: transparent; color: @vv-menu-text; padding: 7px 14px; min-height: 30px; border: 0px; border-radius: 0px; }"
            "QComboBox QAbstractItemView::item:hover, QComboBox QAbstractItemView::item:selected, QComboBox QAbstractItemView::item:selected:active, QComboBox QAbstractItemView::item:selected:!active { background-color: @vv-menu-hover; color: @vv-menu-text; }"
            "QComboBox QAbstractItemView::item:disabled { background-color: transparent; color: @vv-menu-disabled; }"
            "QComboBox QAbstractItemView::item:selected:disabled { background-color: @vv-menu-hover; color: @vv-menu-disabled; }"
            "QLineEdit { background-color: @vv-field-bg; border: 1px solid @vv-border; border-radius: 6px; padding: 4px 10px; min-height: 26px; max-height: 26px; color: @vv-text; font-size: 14px; }"
            "QLineEdit:hover { border-color: @vv-border-strong; }"
            "QLineEdit:focus { border-color: @vv-primary; border-width: 1px; }"
            "QLineEdit:disabled { background-color: @vv-surface-alt; color: @vv-text; }"
            "QAbstractSpinBox { background-color: @vv-field-bg; border: 1px solid @vv-border; border-radius: 6px; padding: 4px 0px 4px 10px; min-height: 26px; max-height: 26px; color: @vv-text; font-size: 14px; }"
            "QAbstractSpinBox QLineEdit { background-color: transparent; border: none; border-radius: 0px; padding: 0px; min-height: 0px; max-height: 16777215px; }"
            "QAbstractSpinBox:hover { border-color: @vv-border-strong; }"
            "QAbstractSpinBox:focus { border-color: @vv-primary; border-width: 1px; }"
            "QAbstractSpinBox:disabled { background-color: @vv-surface-alt; color: @vv-text; }"
            "QSpinBox::up-button, QSpinBox::down-button, QDoubleSpinBox::up-button, QDoubleSpinBox::down-button { width: 20px; border: none; background-color: transparent; subcontrol-origin: border; }"
            "QSpinBox::up-button, QDoubleSpinBox::up-button { subcontrol-position: top right; border-top-right-radius: 6px; }"
            "QSpinBox::down-button, QDoubleSpinBox::down-button { subcontrol-position: bottom right; border-bottom-right-radius: 6px; }"
            "QAbstractSpinBox::up-button:hover, QAbstractSpinBox::down-button:hover, QAbstractSpinBox::up-button:pressed, QAbstractSpinBox::down-button:pressed { background-color: transparent; }"
            "QAbstractSpinBox::up-arrow, QAbstractSpinBox::down-arrow { width: 12px; height: 12px; margin-right: 6px; }"
            "QAbstractSpinBox::up-arrow { image: url(lucide/chevron-up.svg); }"
            "QAbstractSpinBox::down-arrow { image: url(lucide/chevron-down.svg); }"
            "QAbstractSpinBox[spinArrowHover=\"up\"]::up-arrow { image: url(lucide/chevron-up-primary.svg); width: 14px; height: 14px; margin-right: 5px; }"
            "QAbstractSpinBox[spinArrowHover=\"down\"]::down-arrow { image: url(lucide/chevron-down-primary.svg); width: 14px; height: 14px; margin-right: 5px; }"
            "QPlainTextEdit, QTextEdit { background-color: @vv-field-bg; color: @vv-text; border: 1px solid @vv-border; border-radius: 6px; padding: 10px; font-family: \"Consolas\", \"Monaco\", \"Courier New\", monospace; font-size: 13px; }"
            "QTextEdit#logTextEdit { background-color: transparent; border: none; border-radius: 0px; }"
            "QWidget#logTextViewport { background-color: transparent; border: none; }"
            "QScrollBar:vertical { background-color: @vv-surface-sunken; width: 8px; border: none; border-radius: 4px; margin: 14px 0px 14px 0px; }"
            "QScrollBar::handle:vertical { background-color: @vv-scrollbar-handle; min-height: 30px; border-radius: 4px; border: none; margin: 0px; }"
            "QScrollBar::handle:vertical:hover { background-color: @vv-scrollbar-handle-hover; }"
            "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background-color: @vv-surface-sunken; border-radius: 4px; }"
            "QScrollBar::add-page:vertical:hover, QScrollBar::sub-page:vertical:hover, QScrollBar::add-page:vertical:pressed, QScrollBar::sub-page:vertical:pressed { background-color: @vv-surface-sunken; }"
            "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { background-color: @vv-surface-sunken; border: none; height: 14px; subcontrol-origin: margin; }"
            "QScrollBar::sub-line:vertical { border-top-left-radius: 4px; border-top-right-radius: 4px; subcontrol-position: top; }"
            "QScrollBar::add-line:vertical { border-bottom-left-radius: 4px; border-bottom-right-radius: 4px; subcontrol-position: bottom; }"
            "QScrollBar::add-line:vertical:hover, QScrollBar::sub-line:vertical:hover, QScrollBar::add-line:vertical:pressed, QScrollBar::sub-line:vertical:pressed { background-color: @vv-surface-sunken; }"
            "QScrollBar::up-arrow:vertical { image: url(combo_arrow_up.xpm); width: 8px; height: 8px; }"
            "QScrollBar::down-arrow:vertical { image: url(combo_arrow_down.xpm); width: 8px; height: 8px; }"
            "QScrollBar:horizontal { background-color: @vv-surface-sunken; height: 8px; border: none; border-radius: 4px; }"
            "QScrollBar::handle:horizontal { background-color: @vv-scrollbar-handle; min-width: 30px; border-radius: 4px; border: none; margin: 0px; }"
            "QScrollBar::handle:horizontal:hover { background-color: @vv-scrollbar-handle-hover; }"
            "QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { background-color: @vv-surface-sunken; border-radius: 4px; }"
            "QScrollBar::add-page:horizontal:hover, QScrollBar::sub-page:horizontal:hover, QScrollBar::add-page:horizontal:pressed, QScrollBar::sub-page:horizontal:pressed { background-color: @vv-surface-sunken; }"
            "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0px; background-color: @vv-surface-sunken; }"
            "QScrollArea#mainCardsScrollArea QScrollBar:vertical { background-color: @vv-surface; width: 8px; border-radius: 4px; margin: 14px 0px 14px 0px; } QScrollArea#mainCardsScrollArea QScrollBar:horizontal { background-color: @vv-surface; height: 8px; border-radius: 4px; margin: 0px; }"
            "QScrollArea#mainCardsScrollArea QScrollBar::add-page:vertical, QScrollArea#mainCardsScrollArea QScrollBar::sub-page:vertical, QScrollArea#mainCardsScrollArea QScrollBar::add-page:horizontal, QScrollArea#mainCardsScrollArea QScrollBar::sub-page:horizontal { background-color: @vv-surface; }"
            "QScrollArea#mainCardsScrollArea QScrollBar::add-line:vertical, QScrollArea#mainCardsScrollArea QScrollBar::sub-line:vertical { background-color: @vv-surface; border: none; height: 14px; subcontrol-origin: margin; } QScrollArea#mainCardsScrollArea QScrollBar::add-line:vertical:hover, QScrollArea#mainCardsScrollArea QScrollBar::sub-line:vertical:hover, QScrollArea#mainCardsScrollArea QScrollBar::add-line:vertical:pressed, QScrollArea#mainCardsScrollArea QScrollBar::sub-line:vertical:pressed { background-color: @vv-surface; } QScrollArea#mainCardsScrollArea QScrollBar::add-line:horizontal, QScrollArea#mainCardsScrollArea QScrollBar::sub-line:horizontal { background-color: @vv-surface; border: none; width: 0px; }"
            ""
            "QScrollArea#mainCardsScrollArea QScrollBar::handle:vertical, QScrollArea#mainCardsScrollArea QScrollBar::handle:horizontal { border: none; border-radius: 4px; margin: 0px; }"
            "QSplitter::handle { background-color: transparent; }"
            "QSplitter#appLayoutSplitter::handle:horizontal { width: 0px; background-color: transparent; }"
            "QSplitter#appLayoutSplitter::handle:horizontal:hover { background-color: transparent; }"
            "QSplitter#appLayoutSplitter::handle:horizontal:pressed { background-color: transparent; }"
            "QSplitter#mainContentSplitter::handle:horizontal { width: 0px; background-color: transparent; }"
            "QSplitter#homeOverviewSplitter::handle:horizontal { width: 12px; background-color: @vv-surface; }"
            "QSplitter#homeOverviewSplitter::handle:horizontal:hover { background-color: @vv-surface; }"
            "QSplitter#homeOverviewSplitter::handle:horizontal:pressed { background-color: @vv-surface; }"
            "QWidget#mainCardResizeHandle { min-height: 3px; max-height: 3px; background-color: transparent; }"
            "QSplitter#mainContentSplitter::handle:horizontal:hover { background-color: @vv-resize-hover; }"
            "QWidget#mainCardResizeHandle:hover { background-color: @vv-resize-hover; }"
            "QSplitter#mainContentSplitter::handle:horizontal:pressed { background-color: @vv-resize-pressed; }"
            "QWidget#mainCardResizeHandle[dragging=\"true\"] { background-color: @vv-resize-pressed; }"
            "QSplitter::handle:horizontal { width: 0px; }"
            "QSplitter::handle:vertical { height: 0px; }"
            "QSplitter#appLayoutSplitter::handle:horizontal { width: 0px; background-color: @vv-window; }"
            "QSplitter#appLayoutSplitter::handle:horizontal:hover { background-color: @vv-window; }"
            "QSplitter#appLayoutSplitter::handle:horizontal:pressed { background-color: @vv-window; }"
            "QSplitter#mainContentSplitter::handle:horizontal { width: 0px; background-color: transparent; }"
            "QSplitter#mainContentSplitter::handle:horizontal:hover { background-color: @vv-resize-hover; }"
            "QSplitter#mainContentSplitter::handle:horizontal:pressed { background-color: @vv-resize-pressed; }"
            "QSplitter#homeOverviewSplitter::handle:horizontal { width: 12px; background-color: @vv-surface; }"
            "QSplitter#homeOverviewSplitter::handle:horizontal:hover { background-color: @vv-surface; }"
            "QSplitter#homeOverviewSplitter::handle:horizontal:pressed { background-color: @vv-surface; }"
            "QPushButton { background-color: @vv-primary; color: @vv-white; border: none; border-radius: 6px; padding: 4px 16px; font-size: 15px; font-weight: 500; min-height: 28px; max-height: 28px; }"
            "QPushButton:hover { background-color: @vv-primary-hover; }"
            "QPushButton:pressed { background-color: @vv-primary-pressed; }"
            "QPushButton:disabled { background-color: @vv-border-strong; color: @vv-white; }"
            "QPushButton#compactTcpButton { padding: 4px 14px; min-height: 28px; max-height: 28px; font-size: 14px; }"
            "QPushButton#compactTcpStartButton { padding: 4px 14px; min-height: 28px; max-height: 28px; font-size: 14px; }"
            "TemperatureControllerPanel QFrame#temperatureConfigCard { background-color: @vv-surface; border: 1px solid @vv-border; border-radius: 8px; }"
            "TemperatureControllerPanel QFrame#temperatureChannelTopBar { background-color: @vv-surface-alt; border: 1px solid @vv-border; border-radius: 8px; }"
            "TemperatureControllerPanel QFrame#temperatureChannelSubTopBar { background-color: @vv-surface-alt; border: 1px solid @vv-border; border-radius: 8px; }"
            "TemperatureControllerPanel QStackedWidget#temperatureChannelStack { background-color: transparent; border: none; }"
            "TemperatureControllerPanel QPushButton[temperatureChannelSelector=\"true\"] { background-color: transparent; border: none; border-radius: 6px; color: @vv-text; font-size: 14px; font-weight: 500; min-height: 30px; max-height: 30px; padding: 0px 10px; text-align: center; outline: none; }"
            "TemperatureControllerPanel QPushButton[temperatureChannelSubSelector=\"true\"] { background-color: transparent; border: none; border-radius: 6px; color: @vv-text; font-size: 14px; font-weight: 500; min-height: 30px; max-height: 30px; padding: 0px 10px; text-align: center; outline: none; }"
            "TemperatureControllerPanel QPushButton[temperatureChannelSelector=\"true\"]:checked { background-color: @vv-surface; color: @vv-primary; font-weight: 600; }"
            "TemperatureControllerPanel QPushButton[temperatureChannelSelector=\"true\"]:!checked:hover { background-color: @vv-primary-subtle; color: @vv-primary; }"
            "TemperatureControllerPanel QPushButton[temperatureChannelSubSelector=\"true\"]:checked { background-color: @vv-surface; color: @vv-primary; font-weight: 600; }"
            "TemperatureControllerPanel QPushButton[temperatureChannelSubSelector=\"true\"]:!checked:hover { background-color: @vv-primary-subtle; color: @vv-primary; }"
            "TemperatureControllerPanel QPushButton[temperatureOutputEnableSwitch=\"true\"] { background-color: transparent; border: none; padding: 0px; margin: 0px; min-width: 106px; max-width: 106px; min-height: 34px; max-height: 34px; outline: none; }"
            "QToolTip { background-color: rgb(45, 45, 45); color: #FFFFFF; border: 1px solid #474747; border-radius: 13px; padding: 8px 16px; font-size: 16px; }"
            "QGroupBox#sensorGroupBox[vaporViewTopLevelCard=\"true\"], QFrame#epsilonSectionCard[vaporViewTopLevelCard=\"true\"], QFrame#recordingStatusCard[vaporViewTopLevelCard=\"true\"], QFrame#logPanelFrame[vaporViewTopLevelCard=\"true\"], QFrame[vaporViewTopLevelCard=\"true\"] { background-color: @vv-surface-raised; border: 1px solid rgba(0, 0, 0, 0.04); border-radius: 12px; }"
            "QGroupBox#sensorGroupBox[vaporViewTopLevelCard=\"true\"] > QWidget#sectionTitleBar, QGroupBox#sensorGroupBox[vaporViewTopLevelCard=\"true\"] > QWidget#environmentSectionTitleBar, QGroupBox#sensorGroupBox[vaporViewTopLevelCard=\"true\"] > TcpWavePanel > QWidget#sectionTitleBar, QFrame#epsilonSectionCard[vaporViewTopLevelCard=\"true\"] > QWidget#sectionTitleBar, QFrame#recordingStatusCard[vaporViewTopLevelCard=\"true\"] > QWidget#sectionTitleBar, QFrame#logPanelFrame[vaporViewTopLevelCard=\"true\"] > QWidget#sectionTitleBar { background-color: @vv-surface-raised; border-top-left-radius: 11px; border-top-right-radius: 11px; }"
            "QFrame#recordingStatusCard[vaporViewTopLevelCard=\"true\"] > QWidget#recordingStatusBody { background-color: @vv-surface-raised; border-bottom-left-radius: 11px; border-bottom-right-radius: 11px; }";
    }

    state_->base_style_sheet_.replace("url(lucide/chevron-down.svg)", QString("url(%1)").arg(chevronDownIconPath));
    state_->base_style_sheet_.replace("url(lucide/chevron-down-primary.svg)", QString("url(%1)").arg(chevronDownPrimaryIconPath));
    state_->base_style_sheet_.replace("url(lucide/chevron-up.svg)", QString("url(%1)").arg(chevronUpIconPath));
    state_->base_style_sheet_.replace("url(lucide/chevron-up-primary.svg)", QString("url(%1)").arg(chevronUpPrimaryIconPath));
}

QString temperatureControllerConfigStyleSheet()
{
    return QStringLiteral(
        "TemperatureControllerPanel QFrame#temperatureConfigCard { background-color: @vv-surface; border: 1px solid @vv-border; border-radius: 8px; }"
        "TemperatureControllerPanel QFrame#temperatureChannelTopBar { background-color: @vv-surface-alt; border: 1px solid @vv-border; border-radius: 8px; }"
        "TemperatureControllerPanel QFrame#temperatureChannelSubTopBar { background-color: @vv-surface-alt; border: 1px solid @vv-border; border-radius: 8px; }"
        "TemperatureControllerPanel QStackedWidget#temperatureChannelStack { background-color: transparent; border: none; }"
        "TemperatureControllerPanel QPushButton[temperatureChannelSelector=\"true\"] { background-color: transparent; border: none; border-radius: 6px; color: @vv-text; font-size: 14px; font-weight: 500; min-height: 30px; max-height: 30px; padding: 0px 10px; text-align: center; outline: none; }"
        "TemperatureControllerPanel QPushButton[temperatureChannelSubSelector=\"true\"] { background-color: transparent; border: none; border-radius: 6px; color: @vv-text; font-size: 14px; font-weight: 500; min-height: 30px; max-height: 30px; padding: 0px 10px; text-align: center; outline: none; }"
        "TemperatureControllerPanel QPushButton[temperatureChannelSelector=\"true\"]:checked { background-color: @vv-surface; color: @vv-primary; font-weight: 600; }"
        "TemperatureControllerPanel QPushButton[temperatureChannelSelector=\"true\"]:!checked:hover { background-color: @vv-primary-subtle; color: @vv-primary; }"
        "TemperatureControllerPanel QPushButton[temperatureChannelSubSelector=\"true\"]:checked { background-color: @vv-surface; color: @vv-primary; font-weight: 600; }"
        "TemperatureControllerPanel QPushButton[temperatureChannelSubSelector=\"true\"]:!checked:hover { background-color: @vv-primary-subtle; color: @vv-primary; }"
        "TemperatureControllerPanel QPushButton[temperatureOutputEnableSwitch=\"true\"] { background-color: transparent; border: none; padding: 0px; margin: 0px; min-width: 106px; max-width: 106px; min-height: 34px; max-height: 34px; outline: none; }"
        "TemperatureControllerPanel QPushButton#temperatureFactoryResetButton { background-color: transparent; border: 1px solid @vv-toolbar-red; border-radius: 8px; color: @vv-toolbar-red; font-size: 14px; font-weight: 600; padding: 0px 12px; text-align: center; }"
        "TemperatureControllerPanel QPushButton#temperatureFactoryResetButton:hover { background-color: rgba(210, 74, 48, 0.10); }"
        "TemperatureControllerPanel QLabel#fieldLabel[temperatureOvertempWarning=\"true\"] { color: @vv-danger; }"
        "TemperatureControllerPanel QLabel#fieldLabel[temperatureMaxOutputWarning=\"true\"] { color: @vv-danger; }"
        "TemperatureControllerPanel QSpinBox[temperatureMaxOutputWarning=\"true\"] { color: @vv-danger; }"
        "Ai8TemperatureControllerPanel QFrame#ai8NavigationBar { background-color: @vv-surface-alt; border: 1px solid @vv-border; border-radius: 8px; }"
        "Ai8TemperatureControllerPanel QPushButton[ai8PageSelector=\"true\"] { background-color: transparent; border: none; border-radius: 6px; color: @vv-text; font-size: 14px; font-weight: 500; min-height: 30px; max-height: 30px; padding: 0px 10px; text-align: center; outline: none; }"
        "Ai8TemperatureControllerPanel QPushButton[ai8PageSelector=\"true\"]:checked { background-color: @vv-surface; color: @vv-primary; font-weight: 600; }"
        "Ai8TemperatureControllerPanel QPushButton[ai8PageSelector=\"true\"]:!checked:hover { background-color: @vv-primary-subtle; color: @vv-primary; }"
        "Ai8TemperatureControllerPanel QFrame#ai8ParameterField { background-color: @vv-surface; border: 1px solid @vv-border; border-radius: 8px; }"
        "Ai8TemperatureControllerPanel QLabel#ai8ProtocolStatus[protocolReady=\"false\"] { color: @vv-text-muted; font-weight: 500; }"
        "Ai8TemperatureControllerPanel QLabel#ai8ProtocolStatus[protocolReady=\"true\"] { color: @vv-success; font-weight: 600; }"
        "Ai8TemperatureControllerPanel QLabel#ai8ProtocolStatus[operationFailed=\"true\"] { color: @vv-danger; font-weight: 600; }"
        "Ai8TemperatureControllerPanel QPushButton#ai8WriteParametersButton[primaryAction=\"true\"] { background-color: @vv-primary; border-color: @vv-primary; color: @vv-white; }"
        "Ai8TemperatureControllerPanel QPushButton#ai8WriteParametersButton[primaryAction=\"true\"]:disabled { background-color: @vv-surface-alt; border-color: @vv-border; color: @vv-text-muted; }");
}

QString MainWindow::themedStyleSheet() const
{
    const QString baseStyle = applyAppThemeTokens(state_->base_style_sheet_, false);
    QString darkStyle = applyAppThemeTokens(darkThemeStyleSheet(), true);
    const QString chevronDownDarkIconPath = findResourceFile(
        QStringLiteral("resources/lucide/chevron-down-dark.svg")).replace('\\', '/');
    const QString chevronDownPrimaryDarkIconPath = findResourceFile(
        QStringLiteral("resources/lucide/chevron-down-primary-dark.svg")).replace('\\', '/');
    const QString chevronUpDarkIconPath = findResourceFile(
        QStringLiteral("resources/lucide/chevron-up-dark.svg")).replace('\\', '/');
    const QString chevronUpPrimaryDarkIconPath = findResourceFile(
        QStringLiteral("resources/lucide/chevron-up-primary-dark.svg")).replace('\\', '/');
    darkStyle.replace("url(lucide/chevron-down-dark.svg)",
                      QString("url(%1)").arg(chevronDownDarkIconPath));
    darkStyle.replace("url(lucide/chevron-down-primary-dark.svg)",
                      QString("url(%1)").arg(chevronDownPrimaryDarkIconPath));
    darkStyle.replace("url(lucide/chevron-up-dark.svg)",
                      QString("url(%1)").arg(chevronUpDarkIconPath));
    darkStyle.replace("url(lucide/chevron-up-primary-dark.svg)",
                      QString("url(%1)").arg(chevronUpPrimaryDarkIconPath));
    const QString mainCardsScrollBarStyle =
        applyAppThemeTokens(mainCardsScrollBarBackgroundStyleSheet(state_->dark_theme_enabled_),
                            state_->dark_theme_enabled_);
    const QString mainCardsTopLevelCardStyle = mainCardsTopLevelCardStyleSheet();
    const QString rtkConfigCardStyle =
        applyAppThemeTokens(rtkConfigCardStyleSheet(), state_->dark_theme_enabled_);
    return state_->dark_theme_enabled_
        ? baseStyle +
              darkStyle +
              applyAppThemeTokens(darkOverviewStyleSheet(), true) +
              mainCardsScrollBarStyle +
              mainCardsTopLevelCardStyle +
              rtkConfigCardStyle +
              applyAppThemeTokens(customTitleBarStyleSheet(true), true) +
              applyAppThemeTokens(temperatureControllerConfigStyleSheet(), true)
        : baseStyle +
              mainCardsScrollBarStyle +
              mainCardsTopLevelCardStyle +
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
    return static_cast<int>(std::lround(pixels * state_->font_scale_percent_ / 100.0));
}

int MainWindow::minimumLogSidePanelWidth() const
{
    const QString titleText = state_->is_english_ ? QStringLiteral("Log") : QStringLiteral("日志");
    QFontMetrics titleMetrics(state_->log_inline_title_lbl_ ? state_->log_inline_title_lbl_->font() : font());
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
    if (!state_->app_layout_splitter_)
    {
        return appSidebarDefaultWidth();
    }

    const QList<int> sizes = state_->app_layout_splitter_->sizes();
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
    VaporView::setPersistentSetting(settings, QStringLiteral("app_sidebar_width"), currentAppSidebarWidth());
}

void MainWindow::setAppSidebarWidth(int width)
{
    if (!state_->app_layout_splitter_)
    {
        return;
    }

    const int sidebarWidth = std::max(0, width);
    if (state_->app_sidebar_)
    {
        state_->app_sidebar_->setMinimumWidth(sidebarWidth);
        state_->app_sidebar_->setMaximumWidth(sidebarWidth);
    }
    const int splitterWidth = state_->app_layout_splitter_->width();
    const int handleWidth = state_->app_layout_splitter_->handleWidth();
    const int contentWidth = splitterWidth > sidebarWidth + handleWidth
        ? std::max(1, splitterWidth - sidebarWidth - handleWidth)
        : 1600;

    const QSignalBlocker blocker(state_->app_layout_splitter_);
    state_->app_sidebar_adjusting_ = true;
    state_->app_layout_splitter_->setSizes({sidebarWidth, contentWidth});
    state_->app_sidebar_adjusting_ = false;
    if (state_->app_sidebar_)
    {
        state_->app_sidebar_->setMinimumWidth(0);
        state_->app_sidebar_->setMaximumWidth(QWIDGETSIZE_MAX);
    }
}

void MainWindow::updateAppSidebarButtonTexts()
{
    const bool compact = state_->app_sidebar_mode_ != AppSidebarMode::Full;
    auto applyButtonText = [this, compact](QPushButton *button, const QString& label) {
        if (!button)
        {
            return;
        }
        button->setText(compact ? QString() : label);
        button->setToolTip(label);
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

    applyButtonText(state_->home_nav_btn_, state_->is_english_ ? QStringLiteral("Home") : QStringLiteral("首页"));
    applyButtonText(state_->temperature_nav_btn_, state_->is_english_ ? QStringLiteral("Thermal") : QStringLiteral("温控"));
    applyButtonText(state_->rtk_config_nav_btn_, state_->is_english_ ? QStringLiteral("RTK Config") : QStringLiteral("RTK配置"));
    applyButtonText(state_->device_config_nav_btn_, state_->is_english_ ? QStringLiteral("Device") : QStringLiteral("设备配置"));
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
        state_->app_sidebar_drag_width_ = normalizedWidth;
        state_->app_sidebar_drag_width_valid_ = true;
        if (normalizedWidth > 0)
        {
            state_->last_app_sidebar_visible_width_ = normalizedWidth;
        }
        const AppSidebarMode dragMode = mode == AppSidebarMode::Collapsed
            ? AppSidebarMode::IconsOnly
            : mode;
        if (state_->app_sidebar_mode_ != dragMode)
        {
            state_->app_sidebar_mode_ = dragMode;
            updateAppSidebarButtonTexts();
        }
        return;
    }

    if (state_->app_sidebar_mode_ != mode)
    {
        state_->app_sidebar_mode_ = mode;
        updateAppSidebarButtonTexts();
    }

    const int snapWidth = snappedAppSidebarWidth(normalizedWidth);
    if (snapWidth > 0)
    {
        state_->last_app_sidebar_visible_width_ = snapWidth;
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
    const int targetWidth = state_->app_sidebar_drag_width_valid_
        ? state_->app_sidebar_drag_width_
        : currentAppSidebarWidth();

    state_->app_sidebar_drag_width_valid_ = false;
    updateAppSidebarForWidth(targetWidth, true);
    saveAppSidebarWidth();
}

void MainWindow::toggleAppSidebarFromLogo()
{
    if (!state_->app_layout_splitter_)
    {
        return;
    }

    if (isAppSidebarCollapsed())
    {
        int restoreWidth = state_->last_app_sidebar_visible_width_ > 0
            ? state_->last_app_sidebar_visible_width_
            : appSidebarDefaultWidth();
        restoreWidth = snappedAppSidebarWidth(restoreWidth);
        if (restoreWidth <= 0)
        {
            restoreWidth = appSidebarIconOnlyWidth();
        }
        state_->app_sidebar_mode_ = appSidebarModeForWidth(restoreWidth);
        updateAppSidebarButtonTexts();
        setAppSidebarWidth(restoreWidth);
        state_->last_app_sidebar_visible_width_ = restoreWidth;
    }
    else
    {
        const int currentWidth = currentAppSidebarWidth();
        const int rememberedWidth = snappedAppSidebarWidth(currentWidth);
        if (rememberedWidth > 0)
        {
            state_->last_app_sidebar_visible_width_ = rememberedWidth;
        }
        state_->app_sidebar_mode_ = AppSidebarMode::Collapsed;
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
    if (state_->custom_logo_hovered_ == hovered)
    {
        return;
    }

    state_->custom_logo_hovered_ = hovered;
    updateCustomLogoPixmap();
    updateCustomLogoTooltip();
}

void MainWindow::updateCustomLogoPixmap()
{
    if (!state_->custom_logo_label_)
    {
        return;
    }

    const int logoSize = scalePixels(44);
    state_->custom_logo_label_->setFixedSize(logoSize, logoSize);
    const bool collapsed = isAppSidebarCollapsed();
    const QString logoState = state_->custom_logo_hovered_
        ? (collapsed ? QStringLiteral("open-sidebar") : QStringLiteral("close-sidebar"))
        : QStringLiteral("logo");
    const QSize sidebarIconSize(scalePixels(24), scalePixels(24));
    const QPixmap pixmap = state_->custom_logo_hovered_
        ? createAppSidebarToggleIcon(collapsed).pixmap(sidebarIconSize)
        : renderVaporViewLogo(state_->dark_theme_enabled_, logoSize, state_->custom_logo_label_->devicePixelRatioF());
    state_->custom_logo_label_->setPixmap(pixmap);
    state_->custom_logo_label_->setProperty(kCustomLogoStateProperty, logoState);
    state_->custom_logo_label_->setProperty("titleBarHover", state_->custom_logo_hovered_);
    if (state_->custom_logo_label_->style())
    {
        state_->custom_logo_label_->style()->unpolish(state_->custom_logo_label_);
        state_->custom_logo_label_->style()->polish(state_->custom_logo_label_);
    }
    state_->custom_logo_label_->update();
}

void MainWindow::updateCustomLogoTooltip()
{
    if (!state_->custom_logo_label_)
    {
        return;
    }

    const QString tooltip = isAppSidebarCollapsed()
        ? (state_->is_english_ ? QStringLiteral("Show left sidebar") : QStringLiteral("展开左侧栏"))
        : (state_->is_english_ ? QStringLiteral("Hide left sidebar") : QStringLiteral("收起左侧栏"));
    state_->custom_logo_label_->setToolTip(tooltip);
    state_->custom_logo_label_->setAccessibleName(tooltip);
}

void MainWindow::setLogSidePanelToMinimumWidth()
{
    if (!state_->main_content_splitter_ || state_->log_side_panel_collapsed_)
    {
        return;
    }

    const int minimumLogWidth = minimumLogSidePanelWidth();
    const int totalWidth = state_->main_content_splitter_->width();
    if (totalWidth <= minimumLogWidth + state_->main_content_splitter_->handleWidth())
    {
        return;
    }

    const int leftWidth = std::max(1, totalWidth - minimumLogWidth - state_->main_content_splitter_->handleWidth());
    state_->main_content_splitter_->setSizes({leftWidth, minimumLogWidth});
    state_->last_log_side_panel_width_ = minimumLogWidth;
    state_->log_side_panel_width_initialized_ = true;
}

void MainWindow::toggleLogSidePanel()
{
    setLogSidePanelCollapsed(!state_->log_side_panel_collapsed_);
}

void MainWindow::setLogSidePanelCollapsed(bool collapsed)
{
    state_->log_side_panel_collapsed_ = collapsed;

    if (!state_->log_side_panel_ || !state_->main_content_splitter_)
    {
        updateLogSidePanelToggleButton();
        return;
    }

    const int minimumLogWidth = minimumLogSidePanelWidth();
    const QList<int> sizes = state_->main_content_splitter_->sizes();
    if (collapsed)
    {
        if (state_->log_side_panel_width_initialized_ && sizes.size() >= 2 && sizes.at(1) >= minimumLogWidth)
        {
            state_->last_log_side_panel_width_ = sizes.at(1);
        }
        const int totalWidth = std::max(1, state_->main_content_splitter_->width() > 1
            ? state_->main_content_splitter_->width()
            : state_->base_window_size_.width());
        state_->log_side_panel_->hide();
        state_->main_content_splitter_->setSizes({totalWidth, 0});
        updateLogSidePanelToggleButton();
        queueResponsiveHomeLayoutRefresh();
        return;
    }

    state_->log_side_panel_->setMinimumWidth(minimumLogWidth);
    state_->log_side_panel_->show();
    state_->log_side_panel_->setMaximumWidth(QWIDGETSIZE_MAX);

    const QRect availableGeometry = currentScreenAvailableGeometry();
    const int totalWidth = std::max(1, state_->main_content_splitter_->width() > 1
        ? state_->main_content_splitter_->width()
        : (availableGeometry.isValid() ? availableGeometry.width() : state_->base_window_size_.width()));
    const int maxLogWidth = std::max(minimumLogWidth, totalWidth - state_->main_content_splitter_->handleWidth() - 1);
    const int rememberedWidth = state_->last_log_side_panel_width_ > 0 ? state_->last_log_side_panel_width_ : minimumLogWidth;
    const int logWidth = std::min(std::max(rememberedWidth, minimumLogWidth), maxLogWidth);
    const int leftWidth = std::max(1, totalWidth - logWidth - state_->main_content_splitter_->handleWidth());
    state_->main_content_splitter_->setSizes({leftWidth, std::max(minimumLogWidth, totalWidth - leftWidth)});
    state_->log_side_panel_width_initialized_ = true;
    updateLogSidePanelToggleButton();
    queueResponsiveHomeLayoutRefresh();
}

void MainWindow::updateLogSidePanelToggleButton()
{
    if (!state_->log_side_panel_toggle_btn_)
    {
        return;
    }

    state_->log_side_panel_toggle_btn_->setIcon(createLogSidePanelToggleIcon(state_->log_side_panel_collapsed_));
    state_->log_side_panel_toggle_btn_->setToolTip(state_->log_side_panel_collapsed_
        ? (state_->is_english_ ? QStringLiteral("Show right panel") : QStringLiteral("展开右侧栏"))
        : (state_->is_english_ ? QStringLiteral("Hide right panel") : QStringLiteral("收起右侧栏")));
}

void MainWindow::applyScaledUiMetrics()
{
    auto applyWidgetMetrics = [this](QWidget *widget) {
        if (!widget)
        {
            return;
        }

        const int minimumWidth = widget->minimumWidth();
        const bool usesDynamicHomeOverviewWidth = widget == state_->config_group_ && state_->data_telemetry_summary_card_;
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
    const int viewportWidth = state_->main_cards_scroll_area_ && state_->main_cards_scroll_area_->viewport()
        ? state_->main_cards_scroll_area_->viewport()->width()
        : width();
    return (availableSize.isValid() &&
            (availableSize.width() <= kCompactHomeScreenWidth || availableSize.height() <= kCompactHomeScreenHeight)) ||
           (viewportWidth > 0 && viewportWidth <= kCompactHomeViewportWidth);
}

void MainWindow::updateResponsiveHomeLayout()
{
    if (!state_->sensor_layout_ || !state_->sensor_row_widget_ || !state_->data_group_)
    {
        return;
    }

    const bool compact = shouldUseCompactHomeLayout();
    const bool layoutChanged = state_->compact_home_layout_ != compact;
    state_->compact_home_layout_ = compact;
    const int currentDataCardHeight = state_->data_group_->height();
    const bool dataCardDragging =
        state_->data_group_->property(kMainCardResizeDraggingProperty).toBool();
    const bool preserveDataCardHeight =
        !layoutChanged &&
        (dataCardDragging ||
         state_->data_group_->property(kMainCardUserResizedHeightProperty).toBool());
    if (layoutChanged)
    {
        state_->data_group_->setProperty(kMainCardUserResizedHeightProperty, false);
    }

    const QBoxLayout::Direction direction = compact ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight;
    if (state_->sensor_layout_->direction() != direction)
    {
        state_->sensor_layout_->setDirection(direction);
    }
    state_->sensor_layout_->setSpacing(kTopLevelCardGap);

    if (state_->epsilon_panel_)
    {
        state_->epsilon_panel_->setCompactLayout(compact);
    }
    if (state_->tcp_wave_panel_)
    {
        state_->tcp_wave_panel_->setCompactLayout(compact);
    }
    if (state_->tcp_wave_group_ && state_->tcp_wave_panel_)
    {
        const int preferredTcpWaveHeight = state_->tcp_wave_panel_->preferredPanelHeight();
        const bool useExpandedTcpWaveHeight = state_->tcp_wave_panel_->usesExpandedPanelHeight();
        const int tcpWaveMinimumHeight = std::max(
            useExpandedTcpWaveHeight ? (compact ? kCompactTcpWaveCardMinHeight : kTcpWaveCardMinHeight) : preferredTcpWaveHeight,
            preferredTcpWaveHeight);
        state_->tcp_wave_group_->setFixedHeight(tcpWaveMinimumHeight);
    }
    if (state_->main_cards_scroll_area_ && state_->main_cards_scroll_area_->widget() && state_->main_cards_scroll_area_->viewport())
    {
        updateHomeDeviceOverviewMinimumWidth();
        QLayout *contentLayout = state_->main_cards_scroll_area_->widget()->layout();
        QMargins contentMargins = contentLayout ? contentLayout->contentsMargins() : QMargins();
        if (contentLayout)
        {
            const qreal shadowScale = std::max<qreal>(
                0.5,
                state_->font_scale_percent_ / 100.0);
            const int shadowSafeRightInset =
                static_cast<int>(std::ceil(
                    VaporView::kTopLevelCardShadowBlurRadius * shadowScale * 0.6)) + 1;
            const int targetRightMargin = shadowSafeRightInset;
            if (contentMargins.right() != targetRightMargin)
            {
                contentMargins.setRight(targetRightMargin);
                contentLayout->setContentsMargins(contentMargins);
            }
        }
        const int viewportWidth = std::max(0, state_->main_cards_scroll_area_->viewport()->width());
        const int viewportContentWidth =
            std::max(0, viewportWidth - contentMargins.left() - contentMargins.right());
        const int overviewMinimumWidth = state_->home_overview_splitter_ && state_->config_group_ && state_->temperature_overview_group_
            ? state_->config_group_->minimumWidth() + state_->temperature_overview_group_->minimumWidth() + state_->home_overview_splitter_->handleWidth()
            : (state_->config_group_ ? state_->config_group_->minimumWidth() : 0);
        const bool widthConstrained = overviewMinimumWidth > 0 && viewportWidth > 0 &&
            viewportContentWidth < overviewMinimumWidth;
        const int contentMinimumWidth = overviewMinimumWidth + contentMargins.left() + contentMargins.right();
        const int contentWidth = widthConstrained ? contentMinimumWidth : viewportWidth;
        const int overviewWidth = widthConstrained ? overviewMinimumWidth : viewportContentWidth;
        state_->main_cards_scroll_area_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        state_->main_cards_scroll_area_->widget()->setSizePolicy(widthConstrained ? QSizePolicy::Preferred : QSizePolicy::Ignored,
                                                         QSizePolicy::Preferred);
        state_->main_cards_scroll_area_->widget()->setMinimumWidth(widthConstrained ? contentMinimumWidth : 0);
        state_->main_cards_scroll_area_->widget()->setMaximumWidth(widthConstrained ? QWIDGETSIZE_MAX : viewportWidth);
        state_->main_cards_scroll_area_->widget()->resize(contentWidth, state_->main_cards_scroll_area_->widget()->height());
        if (state_->home_overview_splitter_)
        {
            state_->home_overview_splitter_->setMinimumWidth(overviewWidth);
            state_->home_overview_splitter_->setMaximumWidth(overviewWidth);
        }
        if (contentLayout)
        {
            contentLayout->invalidate();
            contentLayout->activate();
        }
        updateHomeDeviceOverviewMinimumWidth();
        if (state_->home_overview_splitter_ && state_->config_group_ && state_->temperature_overview_group_)
        {
            const QList<int> sizes = state_->home_overview_splitter_->sizes();
            const bool initialized = state_->home_overview_splitter_->property(kHomeOverviewSplitterInitializedProperty).toBool();
            const int leftMinimum = state_->config_group_->minimumWidth();
            const int rightMinimum = state_->temperature_overview_group_->minimumWidth();
            const bool invalidSizes = sizes.size() < 2 || (sizes.at(0) + sizes.at(1)) <= 0;
            const int totalWidth =
                overviewWidth > 0 ? overviewWidth : std::max(overviewMinimumWidth, state_->home_overview_splitter_->width());
            const int availableWidth = std::max(0, totalWidth - state_->home_overview_splitter_->handleWidth());
            const bool sizeTooNarrow = sizes.size() >= 2 && availableWidth >= leftMinimum + rightMinimum &&
                (sizes.at(0) < leftMinimum || sizes.at(1) < rightMinimum);
            if ((!initialized || invalidSizes || sizeTooNarrow) && viewportWidth > 0)
            {
                const int maxLeftWidth = std::max(leftMinimum, availableWidth - rightMinimum);
                const int leftWidth = std::min(leftMinimum, maxLeftWidth);
                const int rightWidth = std::max(rightMinimum, availableWidth - leftWidth);
                state_->home_overview_splitter_->setSizes({leftWidth, rightWidth});
                state_->home_overview_splitter_->setProperty(kHomeOverviewSplitterInitializedProperty, true);
            }
        }
    }

    if (state_->epsilon_group_)
    {
        state_->epsilon_group_->setMaximumWidth(QWIDGETSIZE_MAX);
        state_->epsilon_group_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        state_->sensor_layout_->setAlignment(state_->epsilon_group_, Qt::Alignment());
    }
    if (state_->env_group_)
    {
        if (compact)
        {
            state_->env_group_->setMaximumWidth(QWIDGETSIZE_MAX);
        }
        else
        {
            const int rowWidth = state_->sensor_row_widget_->contentsRect().width();
            const int gap = std::max(0, state_->sensor_layout_->spacing());
            const int availableWidth = std::max(0, rowWidth - gap);
            const int totalStretch = kSensorNavigationStretch + kSensorEnvironmentStretch;
            const int targetEnvironmentWidth = totalStretch > 0
                ? availableWidth * kSensorEnvironmentStretch / totalStretch
                : 0;
            const int environmentMinimumWidth = std::max(state_->env_group_->minimumWidth(),
                                                         state_->env_group_->minimumSizeHint().width());
            state_->env_group_->setMaximumWidth(std::max(environmentMinimumWidth, targetEnvironmentWidth));
        }
        state_->env_group_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        state_->sensor_layout_->setAlignment(state_->env_group_, Qt::Alignment());
    }
    if (state_->sensor_layout_->count() >= 2)
    {
        state_->sensor_layout_->setStretch(0, compact ? 0 : kSensorNavigationStretch);
        state_->sensor_layout_->setStretch(1, compact ? 0 : kSensorEnvironmentStretch);
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

    clearFixedHeight(state_->epsilon_group_);
    clearFixedHeight(state_->env_group_);
    clearFixedHeight(state_->sensor_row_widget_);
    if (!preserveDataCardHeight)
    {
        clearFixedHeight(state_->data_group_);
    }
    if (state_->sensor_layout_)
    {
        state_->sensor_layout_->invalidate();
        state_->sensor_layout_->activate();
    }

    const int epsilonHeight = contentHeightFor(state_->epsilon_group_);
    const int envHeight = contentHeightFor(state_->env_group_);
    int targetHeight = std::max(epsilonHeight, envHeight);
    if (compact && epsilonHeight > 0 && envHeight > 0)
    {
        targetHeight = epsilonHeight + envHeight + state_->sensor_layout_->spacing();
    }

    if (targetHeight > 0)
    {
        if (compact)
        {
            if (state_->epsilon_group_)
            {
                state_->epsilon_group_->setMinimumHeight(epsilonHeight);
            }
            if (state_->env_group_)
            {
                state_->env_group_->setMinimumHeight(envHeight);
            }
        }
        else
        {
            if (state_->epsilon_group_)
            {
                state_->epsilon_group_->setFixedHeight(targetHeight);
            }
            if (state_->env_group_)
            {
                state_->env_group_->setFixedHeight(targetHeight);
            }
        }
        state_->sensor_row_widget_->setMinimumHeight(targetHeight);
        state_->data_group_->setProperty(kMainCardMinimumHeightProperty, targetHeight);
        state_->data_group_->setMinimumHeight(targetHeight);
        const int dataCardHeight = preserveDataCardHeight
            ? std::max(currentDataCardHeight, targetHeight)
            : targetHeight;
        state_->data_group_->setFixedHeight(dataCardHeight);
    }

    if (state_->log_side_panel_)
    {
        const int minimumLogWidth = minimumLogSidePanelWidth();
        state_->log_side_panel_->setMinimumWidth(minimumLogWidth);
        state_->log_side_panel_->setMaximumWidth(QWIDGETSIZE_MAX);
    }

    if (state_->main_content_splitter_ && !state_->log_side_panel_collapsed_)
    {
        const QRect availableGeometry = currentScreenAvailableGeometry();
        const int totalWidth = std::max(1, state_->main_content_splitter_->width() > 1
            ? state_->main_content_splitter_->width()
            : (availableGeometry.isValid() ? availableGeometry.width() : state_->base_window_size_.width()));
        const int minimumLogWidth = minimumLogSidePanelWidth();
        const int logWidth = minimumLogWidth;
        const QList<int> sizes = state_->main_content_splitter_->sizes();
        const bool logPanelTooNarrow = sizes.size() >= 2 && sizes.at(1) < minimumLogWidth;
        const int initialLeftWidth = std::max(scalePixels(320), minimumLogWidth);
        const bool splitterHasRealWidth = state_->main_content_splitter_->isVisible() && state_->main_content_splitter_->width() > 1;
        const bool canInitializeLogWidth =
            splitterHasRealWidth &&
            totalWidth >= minimumLogWidth + state_->main_content_splitter_->handleWidth() + initialLeftWidth;
        if (state_->log_side_panel_width_initialized_ && sizes.size() >= 2 && sizes.at(1) >= minimumLogWidth)
        {
            state_->last_log_side_panel_width_ = sizes.at(1);
        }
        if ((!state_->log_side_panel_width_initialized_ && canInitializeLogWidth) ||
            (logPanelTooNarrow && totalWidth > minimumLogWidth + state_->main_content_splitter_->handleWidth()))
        {
            const int leftWidth = std::max(1, totalWidth - logWidth - state_->main_content_splitter_->handleWidth());
            state_->main_content_splitter_->setSizes({leftWidth, std::max(1, totalWidth - leftWidth)});
            if (canInitializeLogWidth)
            {
                state_->last_log_side_panel_width_ = minimumLogWidth;
                state_->log_side_panel_width_initialized_ = true;
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
    if (state_->responsive_home_layout_refresh_pending_)
    {
        return;
    }

    state_->responsive_home_layout_refresh_pending_ = true;
    QTimer::singleShot(0, this, [this]() {
        state_->responsive_home_layout_refresh_pending_ = false;
        updateResponsiveHomeLayout();
    });
}

void MainWindow::applyStyleConfiguration()
{
    QFont appFont = qApp->font();
    appFont.setPointSizeF(state_->base_font_point_size_ * state_->font_scale_percent_ / 100.0);
    qApp->setPalette(appThemePalette(state_->dark_theme_enabled_));
    qApp->setFont(appFont);
    qApp->setStyleSheet(scaledStyleSheet(themedStyleSheet()));
    updateTopLevelCardShadows(this, state_->font_scale_percent_ / 100.0);
    configureComboPopupsIn(this);
    setWindowsTitleBarDark(this, state_->dark_theme_enabled_);
    applyScaledUiMetrics();
    updateTemperatureControllerTitleText();
    updateAi8TemperatureTitlePortAppearance();
    if (state_->temperature_controller_panel_)
    {
        state_->temperature_controller_panel_->refreshTopControlsLayout();
    }
    if (state_->rtk_config_dialog_)
    {
        state_->rtk_config_dialog_->setFontScale(state_->font_scale_percent_);
    }
    if (state_->app_layout_splitter_)
    {
        updateAppSidebarForWidth(currentAppSidebarWidth(), true);
    }
    updateAppSidebarButtonTexts();
    updateThemedIcons();
    updateCustomTitleBarStyle();
    updateResponsiveHomeLayout();
    QTimer::singleShot(0, this, [this]() {
        if (!state_->log_side_panel_width_initialized_)
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
    releaseFixedHeight(state_->data_group_);
    releaseFixedHeight(state_->tcp_wave_group_);
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
    configureComboBoxPopup(combo, state_->dark_theme_enabled_);
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
    if (percent < 70 || percent > 150 || state_->font_scale_percent_ == percent)
    {
        return;
    }

    QSize targetSize = size();
    if (!isFullScreen() && !isMaximized())
    {
        targetSize = QSize(
            std::max(1, static_cast<int>(std::lround(state_->base_window_size_.width() * percent / 100.0))),
            std::max(1, static_cast<int>(std::lround(state_->base_window_size_.height() * percent / 100.0)))
        );
    }

    state_->font_scale_percent_ = percent;
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
    if (state_->rtk_config_dialog_)
    {
        state_->rtk_config_dialog_->setFontScale(state_->font_scale_percent_);
    }
    if (state_->sky_device_config_dialog_)
    {
        state_->sky_device_config_dialog_->setFontScale(state_->font_scale_percent_);
    }

    QSettings settings("VaporView", "MainWindow");
    VaporView::setPersistentSetting(settings, QStringLiteral("font_scale_percent"), state_->font_scale_percent_);
}

void MainWindow::rebuildRecordingRateMenu()
{
    if (!state_->recording_rate_menu_ || state_->custom_title_bar_)
    {
        return;
    }

    state_->recording_rate_menu_->clear();
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
            action->setIcon(rate == currentRate ? createMenuCheckIcon(state_->dark_theme_enabled_) : QIcon());
            connect(action, &QAction::triggered, this, [this, rate, setter]() {
                setter(rate);
            });
        };

        if (allowUnlimited)
        {
            addAction(0, state_->is_english_ ? unlimitedEnglish : unlimitedChinese);
        }

        if (currentRate > 0 && !standardRates.contains(currentRate))
        {
            addAction(currentRate,
                      QStringLiteral("%1 Hz%2").arg(currentRate).arg(state_->is_english_ ? QStringLiteral(" (Custom)") : QStringLiteral("（当前）")));
        }

        for (int rate : standardRates)
        {
            addAction(rate, QStringLiteral("%1 Hz").arg(rate));
        }
    };

    buildSubmenu(state_->recording_rate_menu_,
                 state_->is_english_ ? QStringLiteral("TCP wave raw recording") : QStringLiteral("TCP波形原始记录"),
                 {},
                 0,
                 true,
                 QStringLiteral("Record every complete TCP frame"),
                 QStringLiteral("记录完整TCP原始帧"),
                 [this](int) { setWaveformRecordingRateHz(0); });

    buildSubmenu(state_->recording_rate_menu_,
                 state_->is_english_ ? QStringLiteral("EPSILON raw recording") : QStringLiteral("EPSILON原始记录"),
                 {},
                 0,
                 true,
                 QStringLiteral("Record verified FDILink raw frames"),
                 QStringLiteral("记录已校验FDILink原始帧"),
                 [this](int) { setImuRecordingRateHz(0); });

    buildSubmenu(state_->recording_rate_menu_,
                 state_->is_english_ ? QStringLiteral("Device CSV recording rate") : QStringLiteral("设备CSV记录频率"),
                 QVector<int>{1, 2, 5, 10, 20, 50, 100, 200},
                 std::clamp(state_->recording_export_rate_hz_, 1, 200),
                 false,
                 QString(),
                 QString(),
                 [this](int rate) { setRecordingExportRateHz(rate); });
}

void MainWindow::setRecordingExportRateHz(int rate, bool should_log)
{
    const int normalizedRate = std::clamp(rate, 1, 200);
    const bool changed = state_->recording_export_rate_hz_ != normalizedRate;
    state_->recording_export_rate_hz_ = normalizedRate;
    rebuildRecordingRateMenu();
    discardTitleApplicationMenuPanel();
    saveRememberedInputState();

    if (changed && should_log)
    {
        log(QString(state_->is_english_ ? "Other-devices recording rate set to %1 Hz" : "其余设备记录频率已设置为 %1 Hz").arg(state_->recording_export_rate_hz_));
    }
}

void MainWindow::setImuRecordingRateHz(int rate, bool should_log)
{
    Q_UNUSED(rate);
    const int normalizedRate = 0;
    const bool changed = state_->imu_recording_rate_hz_ != normalizedRate;
    state_->imu_recording_rate_hz_ = normalizedRate;
    rebuildRecordingRateMenu();
    discardTitleApplicationMenuPanel();
    saveRememberedInputState();

    if (changed && should_log)
    {
        log(state_->is_english_
            ? QStringLiteral("EPSILON raw recording keeps full verified FDILink frames")
            : QStringLiteral("EPSILON 原始记录固定保存完整已校验 FDILink 帧"));
    }
}

void MainWindow::setWaveformRecordingRateHz(int rate, bool should_log)
{
    Q_UNUSED(rate);
    const int normalizedRate = 0;
    const bool changed = state_->waveform_recording_rate_hz_ != normalizedRate;
    state_->waveform_recording_rate_hz_ = normalizedRate;
    rebuildRecordingRateMenu();
    discardTitleApplicationMenuPanel();
    saveRememberedInputState();

    if (changed && should_log)
    {
        log(state_->is_english_
            ? QStringLiteral("TCP wave raw recording keeps every complete TCP frame")
            : QStringLiteral("TCP 波形原始记录固定保存每组完整 TCP 帧"));
    }
}

void MainWindow::loadRememberedInputState()
{
    QSettings settings = VaporView::applicationConfigSettings();
    settings.beginGroup(QStringLiteral("MainWindow"));

    auto loadCombo = [this, &settings](QComboBox *combo, const QString& key, const QString& fallbackKey = QString()) {
        if (!combo)
        {
            return;
        }
        QVariant fallback = combo->currentText();
        if (!fallbackKey.isEmpty())
        {
            fallback = settings.value(fallbackKey, fallback);
        }
        const QString value = settings.value(key, fallback).toString();
        if (combo->property(kLocalSerialPortComboProperty).toBool())
        {
            refreshLocalSerialPortComboOptions(
                combo,
                getAvailablePorts(),
                value.trimmed().isEmpty() ? QStringLiteral("") : value);
        }
        else
        {
            applyComboText(combo, value);
        }
    };

    loadCombo(state_->epsilon_port_combo_, QStringLiteral("serial/epsilon_port"), QStringLiteral("serial/gnss_port"));
    loadCombo(state_->ptb_port_combo_, QStringLiteral("serial/ptb_port"));
    loadCombo(state_->hmp_port_combo_, QStringLiteral("serial/hmp_port"));
    loadCombo(state_->lidar_port_combo_, QStringLiteral("serial/lidar_port"));
    loadCombo(state_->temperature_port_combo_, QStringLiteral("serial/temperature_port"));
    loadCombo(state_->device_config_.ai8_temperature_port_combo,
              QStringLiteral("serial/ai8_temperature_port"));
    loadCombo(state_->device_config_.ai8_temperature_baud_combo,
              QStringLiteral("serial/ai8_temperature_baud"));
    loadCombo(state_->device_config_.ai8_temperature_rate_combo,
              QStringLiteral("rate/ai8_temperature"));
    applyComboText(findChild<QComboBox *>(QStringLiteral("ai8BaudCombo")),
                   state_->device_config_.ai8_temperature_baud_combo
                       ? state_->device_config_.ai8_temperature_baud_combo->currentText()
                       : QString());
    refreshAi8TemperatureTitlePortOptions(
        getAvailablePorts(),
        localSerialPortComboValue(state_->device_config_.ai8_temperature_port_combo));

    loadCombo(state_->epsilon_baud_combo_, QStringLiteral("serial/epsilon_baud"), QStringLiteral("serial/gnss_baud"));
    loadCombo(state_->lidar_baud_combo_, QStringLiteral("serial/lidar_baud"));
    loadCombo(state_->temperature_baud_combo_, QStringLiteral("serial/temperature_baud"));

    loadCombo(state_->global_rate_combo_, QStringLiteral("rate/global"));
    loadCombo(state_->epsilon_rate_combo_, QStringLiteral("rate/epsilon"), QStringLiteral("rate/gnss"));
    loadCombo(state_->ptb_rate_combo_, QStringLiteral("rate/ptb"));
    loadCombo(state_->hmp_rate_combo_, QStringLiteral("rate/hmp"));
    loadCombo(state_->lidar_rate_combo_, QStringLiteral("rate/lidar"));
    loadCombo(state_->temperature_rate_combo_, QStringLiteral("rate/temperature"));
    QString pressureSource = QStringLiteral("ptb210");
    if (state_->device_config_.ptb_source_combo)
    {
        const int index = state_->device_config_.ptb_source_combo->findData(
            settings.value(QStringLiteral("sensor/pressure_source"), QStringLiteral("ptb210")).toString());
        const QSignalBlocker blocker(state_->device_config_.ptb_source_combo);
        state_->device_config_.ptb_source_combo->setCurrentIndex(index >= 0 ? index : 0);
        pressureSource = state_->device_config_.ptb_source_combo->currentData().toString();
        state_->device_config_.ptb_source_combo->setProperty(kSensorBaudSourceProperty, pressureSource);
    }
    QString humiditySource = QStringLiteral("hmp3");
    if (state_->device_config_.hmp_source_combo)
    {
        const int index = state_->device_config_.hmp_source_combo->findData(
            settings.value(QStringLiteral("sensor/humidity_source"), QStringLiteral("hmp3")).toString());
        const QSignalBlocker blocker(state_->device_config_.hmp_source_combo);
        state_->device_config_.hmp_source_combo->setCurrentIndex(index >= 0 ? index : 0);
        humiditySource = state_->device_config_.hmp_source_combo->currentData().toString();
        state_->device_config_.hmp_source_combo->setProperty(kSensorBaudSourceProperty, humiditySource);
    }
    const QString pressureBaud = rememberedSensorBaud(
        settings, pressureSource, QStringLiteral("serial/ptb_baud"));
    applyComboText(state_->ptb_baud_combo_, pressureBaud);
    applyComboText(state_->device_config_.ptb_baud_combo, pressureBaud);
    const QString humidityBaud = rememberedSensorBaud(
        settings, humiditySource, QStringLiteral("serial/hmp_baud"));
    applyComboText(state_->hmp_baud_combo_, humidityBaud);
    applyComboText(state_->device_config_.hmp_baud_combo, humidityBaud);
    if (state_->data_source_mode_combo_)
    {
        const QString value = settings.value(
            QStringLiteral("source/mode"),
            sourceModeStorageValue(state_->data_source_mode_combo_->currentIndex())).toString();
        const int index = sourceModeIndexFromStoredValue(value);
        if (index >= 0)
        {
            const QSignalBlocker blocker(state_->data_source_mode_combo_);
            state_->data_source_mode_combo_->setCurrentIndex(index);
        }
    }
    loadCombo(state_->sky_telemetry_port_combo_, QStringLiteral("telemetry/sky_port"));
    loadCombo(state_->sky_telemetry_baud_combo_, QStringLiteral("telemetry/sky_baud"));
    if (state_->sky_telemetry_transport_combo_)
    {
        const QString transport = settings.value(QStringLiteral("telemetry/transport"), QStringLiteral("tcp")).toString();
        const int index = state_->sky_telemetry_transport_combo_->findData(transport);
        state_->sky_telemetry_transport_combo_->setCurrentIndex(index >= 0 ? index : 0);
    }
    if (state_->sky_telemetry_tcp_host_edit_)
    {
        state_->sky_telemetry_tcp_host_edit_->setText(settings.value(QStringLiteral("telemetry/tcp_host"), QStringLiteral("192.168.1.2")).toString());
    }
    if (state_->sky_telemetry_tcp_port_spin_)
    {
        state_->sky_telemetry_tcp_port_spin_->setValue(settings.value(QStringLiteral("telemetry/tcp_port"), 39100).toInt());
    }

    state_->recording_export_rate_hz_ = std::clamp(settings.value("recording_export_rate_hz", state_->recording_export_rate_hz_).toInt(), 1, 200);
    state_->imu_recording_rate_hz_ = std::clamp(settings.value("imu_recording_rate_hz", state_->imu_recording_rate_hz_).toInt(), 0, 1000);
    state_->waveform_recording_rate_hz_ = 0;
    rebuildRecordingRateMenu();

    const QStringList args = QCoreApplication::arguments();
    const int sourceIndex = args.indexOf(QStringLiteral("--source"));
    if (sourceIndex >= 0 && sourceIndex + 1 < args.size() &&
        args.at(sourceIndex + 1).compare(QStringLiteral("remote"), Qt::CaseInsensitive) == 0 &&
        state_->data_source_mode_combo_)
    {
        state_->data_source_mode_combo_->setCurrentIndex(1);
    }
    const int portIndex = args.indexOf(QStringLiteral("--telemetry-port"));
    if (portIndex >= 0 && portIndex + 1 < args.size() && state_->sky_telemetry_port_combo_)
    {
        const QString port = args.at(portIndex + 1).trimmed();
        VaporView::rememberSerialPort(port);
        setLocalSerialPortComboText(state_->sky_telemetry_port_combo_, port);
    }
    const int baudIndex = args.indexOf(QStringLiteral("--telemetry-baud"));
    if (baudIndex >= 0 && baudIndex + 1 < args.size() && state_->sky_telemetry_baud_combo_)
    {
        state_->sky_telemetry_baud_combo_->setCurrentText(args.at(baudIndex + 1));
    }
    const int transportIndex = args.indexOf(QStringLiteral("--telemetry-transport"));
    if (transportIndex >= 0 && transportIndex + 1 < args.size() && state_->sky_telemetry_transport_combo_)
    {
        const QString transport = args.at(transportIndex + 1).trimmed().toLower();
        const int comboIndex = state_->sky_telemetry_transport_combo_->findData(transport);
        if (comboIndex >= 0)
        {
            state_->sky_telemetry_transport_combo_->setCurrentIndex(comboIndex);
        }
    }
    const int hostIndex = args.indexOf(QStringLiteral("--telemetry-host"));
    if (hostIndex >= 0 && hostIndex + 1 < args.size() && state_->sky_telemetry_tcp_host_edit_)
    {
        state_->sky_telemetry_tcp_host_edit_->setText(args.at(hostIndex + 1));
    }
    const int tcpPortIndex = args.indexOf(QStringLiteral("--telemetry-tcp-port"));
    if (tcpPortIndex >= 0 && tcpPortIndex + 1 < args.size() && state_->sky_telemetry_tcp_port_spin_)
    {
        state_->sky_telemetry_tcp_port_spin_->setValue(args.at(tcpPortIndex + 1).toInt());
    }
    onDataSourceModeChanged(state_->data_source_mode_combo_ ? state_->data_source_mode_combo_->currentIndex() : 0);
}

void MainWindow::saveRememberedInputState() const
{
    if (state_->restoring_persistent_settings_)
    {
        return;
    }
    QSettings settings = VaporView::applicationConfigSettings();
    settings.beginGroup(QStringLiteral("MainWindow"));

    auto saveCombo = [this, &settings](const QString& key, QComboBox *combo) {
        if (combo)
        {
            if (combo->property(kLocalSerialPortComboProperty).toBool())
            {
                if (combo->property(kLocalSerialPortManualEntryProperty).toBool())
                {
                    return;
                }
                const QString port = localSerialPortComboValue(combo);
                VaporView::rememberSerialPort(port);
                VaporView::setPersistentSetting(settings, key, port);
                return;
            }
            VaporView::setPersistentSetting(settings, key, combo->currentText().trimmed());
        }
    };

    saveCombo(QStringLiteral("serial/epsilon_port"), state_->epsilon_port_combo_);
    saveCombo(QStringLiteral("serial/ptb_port"), state_->ptb_port_combo_);
    saveCombo(QStringLiteral("serial/hmp_port"), state_->hmp_port_combo_);
    saveCombo(QStringLiteral("serial/lidar_port"), state_->lidar_port_combo_);
    saveCombo(QStringLiteral("serial/temperature_port"), state_->temperature_port_combo_);
    saveCombo(QStringLiteral("serial/ai8_temperature_port"),
              state_->device_config_.ai8_temperature_port_combo);
    saveCombo(QStringLiteral("serial/ai8_temperature_baud"),
              state_->device_config_.ai8_temperature_baud_combo);
    saveCombo(QStringLiteral("rate/ai8_temperature"),
              state_->device_config_.ai8_temperature_rate_combo);

    saveCombo(QStringLiteral("serial/epsilon_baud"), state_->epsilon_baud_combo_);
    saveCombo(QStringLiteral("serial/ptb_baud"), state_->ptb_baud_combo_);
    saveCombo(QStringLiteral("serial/hmp_baud"), state_->hmp_baud_combo_);
    saveCombo(QStringLiteral("serial/lidar_baud"), state_->lidar_baud_combo_);
    saveCombo(QStringLiteral("serial/temperature_baud"), state_->temperature_baud_combo_);
    saveRememberedSensorBaud(
        settings,
        state_->device_config_.ptb_source_combo
            ? state_->device_config_.ptb_source_combo->currentData().toString()
            : QStringLiteral("ptb210"),
        state_->ptb_baud_combo_ ? state_->ptb_baud_combo_ : state_->device_config_.ptb_baud_combo);
    saveRememberedSensorBaud(
        settings,
        state_->device_config_.hmp_source_combo
            ? state_->device_config_.hmp_source_combo->currentData().toString()
            : QStringLiteral("hmp3"),
        state_->hmp_baud_combo_ ? state_->hmp_baud_combo_ : state_->device_config_.hmp_baud_combo);

    saveCombo(QStringLiteral("rate/global"), state_->global_rate_combo_);
    saveCombo(QStringLiteral("rate/epsilon"), state_->epsilon_rate_combo_);
    saveCombo(QStringLiteral("rate/ptb"), state_->ptb_rate_combo_);
    saveCombo(QStringLiteral("rate/hmp"), state_->hmp_rate_combo_);
    saveCombo(QStringLiteral("rate/lidar"), state_->lidar_rate_combo_);
    saveCombo(QStringLiteral("rate/temperature"), state_->temperature_rate_combo_);
    if (state_->device_config_.ptb_source_combo)
    {
        VaporView::setPersistentSetting(settings, QStringLiteral("sensor/pressure_source"), state_->device_config_.ptb_source_combo->currentData());
    }
    if (state_->device_config_.hmp_source_combo)
    {
        VaporView::setPersistentSetting(settings, QStringLiteral("sensor/humidity_source"), state_->device_config_.hmp_source_combo->currentData());
    }
    if (state_->data_source_mode_combo_)
    {
        VaporView::setPersistentSetting(settings, QStringLiteral("source/mode"), sourceModeStorageValue(state_->data_source_mode_combo_->currentIndex()));
    }
    saveCombo(QStringLiteral("telemetry/sky_port"), state_->sky_telemetry_port_combo_);
    saveCombo(QStringLiteral("telemetry/sky_baud"), state_->sky_telemetry_baud_combo_);
    if (state_->sky_telemetry_transport_combo_)
    {
        VaporView::setPersistentSetting(settings, QStringLiteral("telemetry/transport"), state_->sky_telemetry_transport_combo_->currentData().toString());
    }
    if (state_->sky_telemetry_tcp_host_edit_)
    {
        VaporView::setPersistentSetting(settings, QStringLiteral("telemetry/tcp_host"), state_->sky_telemetry_tcp_host_edit_->text().trimmed());
    }
    if (state_->sky_telemetry_tcp_port_spin_)
    {
        VaporView::setPersistentSetting(settings, QStringLiteral("telemetry/tcp_port"), state_->sky_telemetry_tcp_port_spin_->value());
    }
    VaporView::setPersistentSetting(settings, QStringLiteral("recording_export_rate_hz"), state_->recording_export_rate_hz_);
    VaporView::setPersistentSetting(settings, QStringLiteral("imu_recording_rate_hz"), state_->imu_recording_rate_hz_);
    VaporView::setPersistentSetting(settings, QStringLiteral("waveform_recording_rate_hz"), state_->waveform_recording_rate_hz_);
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
            updateDeviceConfigState();
            updateTemperatureControllerTitleText();
            updateTemperatureTitleButtonsState();
        });
    };

    bindCombo(state_->epsilon_port_combo_);
    bindCombo(state_->ptb_port_combo_);
    bindCombo(state_->hmp_port_combo_);
    bindCombo(state_->lidar_port_combo_);
    bindCombo(state_->temperature_port_combo_);
    bindCombo(state_->device_config_.ai8_temperature_port_combo);
    bindCombo(state_->device_config_.ai8_temperature_baud_combo);
    bindCombo(state_->device_config_.ai8_temperature_rate_combo);
    bindCombo(state_->epsilon_baud_combo_);
    bindCombo(state_->ptb_baud_combo_);
    bindCombo(state_->hmp_baud_combo_);
    bindCombo(state_->lidar_baud_combo_);
    bindCombo(state_->temperature_baud_combo_);
    bindCombo(state_->global_rate_combo_);
    bindCombo(state_->epsilon_rate_combo_);
    bindCombo(state_->ptb_rate_combo_);
    bindCombo(state_->hmp_rate_combo_);
    bindCombo(state_->lidar_rate_combo_);
    bindCombo(state_->temperature_rate_combo_);
    bindCombo(state_->device_config_.ptb_source_combo);
    bindCombo(state_->device_config_.hmp_source_combo);
    bindCombo(state_->data_source_mode_combo_);
    bindCombo(state_->sky_telemetry_transport_combo_);
    bindCombo(state_->sky_telemetry_port_combo_);
    bindCombo(state_->sky_telemetry_baud_combo_);
    if (state_->sky_telemetry_tcp_host_edit_)
    {
        connect(state_->sky_telemetry_tcp_host_edit_, &QLineEdit::textChanged, this, [this](const QString&) {
            saveRememberedInputState();
        });
    }
    if (state_->sky_telemetry_tcp_port_spin_)
    {
        connect(state_->sky_telemetry_tcp_port_spin_, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) {
            saveRememberedInputState();
        });
    }

}
