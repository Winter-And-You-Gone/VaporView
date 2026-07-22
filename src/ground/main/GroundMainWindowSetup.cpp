#include "ground/main/GroundMainWindowImplementation.h"
#include "ground/devices/DeviceRatePolicy.h"
#include "ground/widgets/SerialPortComboSupport.h"

#include <QLinearGradient>

namespace
{

constexpr int kMainContentBottomFadeHeight = 36;

class ScrollAreaBottomFadeOverlay final : public QWidget
{
public:
    explicit ScrollAreaBottomFadeOverlay(QScrollArea *scrollArea)
        : QWidget(scrollArea->viewport())
        , vertical_scroll_bar_(scrollArea->verticalScrollBar())
    {
        setObjectName(QStringLiteral("mainContentBottomFade"));
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
        setAttribute(Qt::WA_NoSystemBackground, true);
        setAutoFillBackground(false);
        setFocusPolicy(Qt::NoFocus);

        parentWidget()->installEventFilter(this);
        connect(vertical_scroll_bar_, &QScrollBar::rangeChanged,
                this, [this]() { syncVisibility(); });
        connect(vertical_scroll_bar_, &QScrollBar::valueChanged,
                this, [this]() { syncVisibility(); });

        syncGeometry();
        syncVisibility();
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched == parentWidget() && event->type() == QEvent::Resize)
        {
            syncGeometry();
            syncVisibility();
        }
        return QWidget::eventFilter(watched, event);
    }

    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event);

        const bool dark = VaporView::isDarkThemeEnabled();
        QColor transparentSurface = appThemeColor(AppThemeColor::Surface, dark);
        QColor softShadow = appThemeColor(AppThemeColor::SurfaceSunken, dark);
        QColor middleFog = transparentSurface;
        QColor denseFog = transparentSurface;
        QColor bottomFog = transparentSurface;
        transparentSurface.setAlpha(0);
        softShadow.setAlpha(dark ? 40 : 24);
        middleFog.setAlpha(dark ? 112 : 96);
        denseFog.setAlpha(dark ? 218 : 204);
        bottomFog.setAlpha(248);

        QLinearGradient fogGradient(0.0, 0.0, 0.0, static_cast<qreal>(height()));
        fogGradient.setColorAt(0.0, transparentSurface);
        fogGradient.setColorAt(0.18, softShadow);
        fogGradient.setColorAt(0.48, middleFog);
        fogGradient.setColorAt(0.78, denseFog);
        fogGradient.setColorAt(1.0, bottomFog);

        QPainter painter(this);
        painter.fillRect(rect(), fogGradient);
    }

private:
    void syncGeometry()
    {
        QWidget *viewport = parentWidget();
        const int fadeHeight = std::min(kMainContentBottomFadeHeight, viewport->height());
        setGeometry(0,
                    std::max(0, viewport->height() - fadeHeight),
                    viewport->width(),
                    fadeHeight);
        raise();
    }

    void syncVisibility()
    {
        const bool contentContinuesBelow =
            vertical_scroll_bar_->maximum() > vertical_scroll_bar_->minimum() &&
            vertical_scroll_bar_->value() < vertical_scroll_bar_->maximum();
        setVisible(contentContinuesBelow);
        if (contentContinuesBelow)
        {
            raise();
        }
    }

    QScrollBar *vertical_scroll_bar_;
};

void installScrollAreaBottomFade(QScrollArea *scrollArea)
{
    if (!scrollArea || !scrollArea->viewport())
    {
        return;
    }
    new ScrollAreaBottomFadeOverlay(scrollArea);
}

}  // namespace

void MainWindow::setupMenuBar()
{
    state_->data_menu_ = menuBar()->addMenu("");

    state_->recording_directory_action_ = new QAction(this);
    connect(state_->recording_directory_action_, &QAction::triggered, this, &MainWindow::onChooseRecordingDirectoryClicked);
    state_->data_menu_->addAction(state_->recording_directory_action_);

    state_->recording_rate_menu_ = state_->data_menu_->addMenu("");
    rebuildRecordingRateMenu();

    state_->devices_menu_ = menuBar()->addMenu("");

    state_->epsilon_packet_rates_action_ = new QAction(this);
    connect(state_->epsilon_packet_rates_action_, &QAction::triggered, this, &MainWindow::onConfigureEpsilonPacketRatesClicked);
    state_->devices_menu_->addAction(state_->epsilon_packet_rates_action_);

    state_->epsilon_rtcm_port_action_ = new QAction(this);
    connect(state_->epsilon_rtcm_port_action_, &QAction::triggered, this, &MainWindow::onConfigureEpsilonRtcmPortClicked);
    state_->devices_menu_->addAction(state_->epsilon_rtcm_port_action_);

    state_->epsilon_reconfigure_action_ = new QAction(this);
    connect(state_->epsilon_reconfigure_action_, &QAction::triggered, this, &MainWindow::onReconfigureEpsilonClicked);
    state_->devices_menu_->addAction(state_->epsilon_reconfigure_action_);

    state_->session_viewer_action_ = new QAction(this);
    connect(state_->session_viewer_action_, &QAction::triggered, this, &MainWindow::onOpenSessionViewerClicked);

    state_->view_menu_ = menuBar()->addMenu("");
    state_->view_menu_->addAction(state_->session_viewer_action_);

#ifdef VAPORVIEW_HAS_OSGEARTH
    state_->map3d_action_ = new QAction(this);
    connect(state_->map3d_action_, &QAction::triggered, this, &MainWindow::onOpenMap3DWindowClicked);

    state_->map3d_diagnostics_action_ = new QAction(this);
    connect(state_->map3d_diagnostics_action_, &QAction::triggered, this, &MainWindow::onOpenMap3DDiagnosticsClicked);

    state_->view_menu_->addAction(state_->map3d_action_);
    state_->developer_menu_ = menuBar()->addMenu("");
    state_->developer_menu_->addAction(state_->map3d_diagnostics_action_);
#endif

    state_->exit_action_ = new QAction(this);
    state_->exit_action_->setShortcut(QKeySequence::Quit);
    connect(state_->exit_action_, &QAction::triggered, this, &QMainWindow::close);
    state_->data_menu_->addAction(state_->exit_action_);

    state_->font_menu_ = menuBar()->addMenu("");
    state_->font_scale_group_ = new QActionGroup(this);
    state_->font_scale_group_->setExclusive(true);

    state_->font_tiny_action_ = new QAction(this);
    state_->font_tiny_action_->setData(70);
    state_->font_scale_group_->addAction(state_->font_tiny_action_);
    state_->font_menu_->addAction(state_->font_tiny_action_);

    state_->font_extra_small_action_ = new QAction(this);
    state_->font_extra_small_action_->setData(80);
    state_->font_scale_group_->addAction(state_->font_extra_small_action_);
    state_->font_menu_->addAction(state_->font_extra_small_action_);

    state_->font_small_action_ = new QAction(this);
    state_->font_small_action_->setData(90);
    state_->font_scale_group_->addAction(state_->font_small_action_);
    state_->font_menu_->addAction(state_->font_small_action_);

    state_->font_normal_action_ = new QAction(this);
    state_->font_normal_action_->setData(100);
    state_->font_scale_group_->addAction(state_->font_normal_action_);
    state_->font_menu_->addAction(state_->font_normal_action_);

    state_->font_large_action_ = new QAction(this);
    state_->font_large_action_->setData(115);
    state_->font_scale_group_->addAction(state_->font_large_action_);
    state_->font_menu_->addAction(state_->font_large_action_);

    state_->font_extra_large_action_ = new QAction(this);
    state_->font_extra_large_action_->setData(130);
    state_->font_scale_group_->addAction(state_->font_extra_large_action_);
    state_->font_menu_->addAction(state_->font_extra_large_action_);

    connect(state_->font_scale_group_, &QActionGroup::triggered, this, &MainWindow::onFontScaleTriggered);

    updateFontScaleMenuCheckIcons();

    state_->language_menu_ = menuBar()->addMenu("");

    state_->lang_action_ = new QAction(this);
    state_->lang_action_->setIcon(createLanguageIcon());
    connect(state_->lang_action_, &QAction::triggered, this, &MainWindow::onSwitchLanguage);
    state_->language_menu_->addAction(state_->lang_action_);

    state_->help_menu_ = menuBar()->addMenu("");

    state_->about_action_ = new QAction(this);
    connect(state_->about_action_, &QAction::triggered, this, &MainWindow::showAboutDialog);
    state_->help_menu_->addAction(state_->about_action_);
}

void MainWindow::setupToolBar()
{
    state_->refresh_ports_btn_ = new QAction(this);
    state_->refresh_ports_btn_->setIcon(createRefreshIcon());
    connect(state_->refresh_ports_btn_, &QAction::triggered, this, &MainWindow::onRefreshPortsClicked);

    state_->connect_btn_ = new QAction(this);
    state_->connect_btn_->setIcon(createConnectIcon());
    connect(state_->connect_btn_, &QAction::triggered, this, &MainWindow::onConnectClicked);

    state_->cancel_connect_btn_ = new QAction(this);
    state_->cancel_connect_btn_->setIcon(createCancelIcon());
    state_->cancel_connect_btn_->setEnabled(false);
    connect(state_->cancel_connect_btn_, &QAction::triggered, this, &MainWindow::onCancelConnectClicked);

    state_->disconnect_btn_ = new QAction(this);
    state_->disconnect_btn_->setIcon(createDisconnectIcon());
    state_->disconnect_btn_->setEnabled(false);
    connect(state_->disconnect_btn_, &QAction::triggered, this, &MainWindow::onDisconnectClicked);

    state_->scheduled_recording_action_ = new QAction(this);
    state_->scheduled_recording_action_->setIcon(createTimerIcon());
    connect(state_->scheduled_recording_action_, &QAction::triggered, this, &MainWindow::onScheduledRecordingClicked);

    state_->start_recording_btn_ = new QAction(this);
    state_->start_recording_btn_->setIcon(createPlayIcon());
    state_->start_recording_btn_->setEnabled(false);
    connect(state_->start_recording_btn_, &QAction::triggered, this, &MainWindow::onStartRecordingClicked);

    state_->pause_recording_btn_ = new QAction(this);
    state_->pause_recording_btn_->setIcon(createPauseIcon());
    state_->pause_recording_btn_->setEnabled(false);
    connect(state_->pause_recording_btn_, &QAction::triggered, this, &MainWindow::onPauseRecordingClicked);

    state_->stop_recording_btn_ = new QAction(this);
    state_->stop_recording_btn_->setIcon(createStopIcon());
    state_->stop_recording_btn_->setEnabled(false);
    connect(state_->stop_recording_btn_, &QAction::triggered, this, &MainWindow::onStopRecordingClicked);

    state_->rtk_config_action_ = new QAction(this);
    updateRtkConfigIcon();
    connect(state_->rtk_config_action_, &QAction::triggered, this, &MainWindow::onRtkConfigClicked);
    if (state_->devices_menu_)
    {
        state_->devices_menu_->addAction(state_->rtk_config_action_);
    }

    state_->clear_log_action_ = new QAction(this);
    state_->clear_log_action_->setIcon(createClearLogIcon());
    connect(state_->clear_log_action_, &QAction::triggered, this, &MainWindow::onClearLogClicked);

    state_->log_filter_menu_ = new SingleLevelPopupMenu(this);
    state_->log_filter_menu_->setObjectName(QStringLiteral("logFilterMenu"));
    state_->log_filter_menu_->setPanelPadding(12);
    state_->log_filter_menu_->setCornerRadius(10);
    state_->log_filter_menu_->refreshTheme();

    auto createLogFilterAction = [this](bool *enabled) {
        auto *row = new SingleLevelPopupMenuRow(state_->log_filter_menu_);
        row->setTextAlignment(SingleLevelPopupTextAlignment::Left);
        row->setHorizontalPadding(scalePixels(18), scalePixels(14));
        row->setRowSpacing(scalePixels(6));
        row->setCheckSlotWidth(scalePixels(18));
        row->setCheckIconSize(QSize(scalePixels(16), scalePixels(16)));
        row->setRowHeight(scalePixels(36));
        row->setMinimumRowWidth(scalePixels(120));
        row->setCloseOnClick(true);
        auto *action = state_->log_filter_menu_->addRow(row);
        action->setCheckable(false);
        connect(action, &QAction::triggered, this, [this, enabled]() {
            *enabled = !*enabled;
            updateLogFilterAction();
            renderLogView();
        });
        return action;
    };

    state_->log_filter_ack_action_ = createLogFilterAction(&state_->log_filter_ack_enabled_);
    state_->log_filter_config_action_ = createLogFilterAction(&state_->log_filter_config_enabled_);
    state_->log_filter_connection_action_ = createLogFilterAction(&state_->log_filter_connection_enabled_);
    state_->log_filter_recording_action_ = createLogFilterAction(&state_->log_filter_recording_enabled_);

    state_->session_viewer_action_->setIcon(createWaveformViewerIcon());
#ifdef VAPORVIEW_HAS_OSGEARTH
    if (state_->map3d_action_)
    {
        state_->map3d_action_->setIcon(createLucideIcon(QStringLiteral("earth"), toolbarColor(AppThemeColor::ToolbarBlue)));
    }
    if (state_->map3d_diagnostics_action_)
    {
        state_->map3d_diagnostics_action_->setIcon(createLucideIcon(QStringLiteral("activity"), toolbarColor(AppThemeColor::ToolbarBlue)));
    }
#endif

    state_->theme_toggle_action_ = new QAction(this);
    connect(state_->theme_toggle_action_, &QAction::triggered, this, &MainWindow::onToggleTheme);

    setupCustomTitleBar();
    updateThemeAction();
}

void MainWindow::setupCustomTitleBar()
{
    state_->custom_title_bar_ = new QWidget(this);
    state_->custom_title_bar_->setObjectName(QStringLiteral("customTitleBar"));
    state_->custom_title_bar_->installEventFilter(this);

    auto *titleLayout = new QHBoxLayout(state_->custom_title_bar_);
    titleLayout->setContentsMargins(kAppSidebarVisualPadding * 2, 0, 8, 0);
    titleLayout->setSpacing(6);

    state_->custom_logo_label_ = new QLabel(state_->custom_title_bar_);
    state_->custom_logo_label_->setObjectName(QStringLiteral("customTitleLogo"));
    state_->custom_logo_label_->setFixedSize(24, 24);
    state_->custom_logo_label_->setAlignment(Qt::AlignCenter);
    state_->custom_logo_label_->setCursor(Qt::PointingHandCursor);
    state_->custom_logo_label_->setFocusPolicy(Qt::StrongFocus);
    state_->custom_logo_label_->setAttribute(Qt::WA_Hover, true);
    state_->custom_logo_label_->setProperty(kCustomLogoStateProperty, QStringLiteral("logo"));
    state_->custom_logo_label_->setProperty("titleBarHover", false);
    state_->custom_logo_label_->installEventFilter(this);
    titleLayout->addWidget(state_->custom_logo_label_, 0, Qt::AlignVCenter);

    state_->title_menu_btn_ = createTitleBarIconButton(QStringLiteral("titleBarMenuButton"), state_->custom_title_bar_);
    connect(state_->title_menu_btn_, &QToolButton::clicked, this, &MainWindow::showTitleApplicationMenu);
    titleLayout->addWidget(state_->title_menu_btn_, 0, Qt::AlignVCenter);

    state_->custom_title_label_ = new QLabel(QStringLiteral("VaporView"), state_->custom_title_bar_);
    state_->custom_title_label_->setObjectName(QStringLiteral("customTitleLabel"));
    state_->custom_title_label_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    state_->custom_title_label_->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
    state_->custom_title_label_->installEventFilter(this);
    titleLayout->addWidget(state_->custom_title_label_, 0);

    titleLayout->addWidget(createTitleBarActionButton(state_->refresh_ports_btn_, state_->custom_title_bar_), 0, Qt::AlignVCenter);
    addTitleBarSeparator(titleLayout);
    titleLayout->addWidget(createTitleBarActionButton(state_->connect_btn_, state_->custom_title_bar_), 0, Qt::AlignVCenter);
    titleLayout->addWidget(createTitleBarActionButton(state_->cancel_connect_btn_, state_->custom_title_bar_), 0, Qt::AlignVCenter);
    titleLayout->addWidget(createTitleBarActionButton(state_->disconnect_btn_, state_->custom_title_bar_), 0, Qt::AlignVCenter);
    addTitleBarSeparator(titleLayout);
    titleLayout->addWidget(createTitleBarActionButton(state_->scheduled_recording_action_, state_->custom_title_bar_), 0, Qt::AlignVCenter);
    titleLayout->addWidget(createTitleBarActionButton(state_->start_recording_btn_, state_->custom_title_bar_), 0, Qt::AlignVCenter);
    titleLayout->addWidget(createTitleBarActionButton(state_->pause_recording_btn_, state_->custom_title_bar_), 0, Qt::AlignVCenter);
    titleLayout->addWidget(createTitleBarActionButton(state_->stop_recording_btn_, state_->custom_title_bar_), 0, Qt::AlignVCenter);
    addTitleBarSeparator(titleLayout);
    titleLayout->addWidget(createTitleBarActionButton(state_->session_viewer_action_, state_->custom_title_bar_), 0, Qt::AlignVCenter);
#ifdef VAPORVIEW_HAS_OSGEARTH
    titleLayout->addWidget(createTitleBarActionButton(state_->map3d_action_, state_->custom_title_bar_), 0, Qt::AlignVCenter);
#endif
    addTitleBarSeparator(titleLayout);
    state_->title_language_btn_ = createTitleBarIconButton(QStringLiteral("titleBarButton"), state_->custom_title_bar_);
    state_->title_language_btn_->setAccessibleName(QStringLiteral("titleLanguageButton"));
    connect(state_->title_language_btn_, &QToolButton::clicked, this, &MainWindow::onSwitchLanguage);
    titleLayout->addWidget(state_->title_language_btn_, 0, Qt::AlignVCenter);
    titleLayout->addWidget(createTitleBarActionButton(state_->theme_toggle_action_, state_->custom_title_bar_), 0, Qt::AlignVCenter);
    titleLayout->addStretch(1);
    state_->log_side_panel_toggle_btn_ = createTitleBarIconButton(QStringLiteral("titleBarButton"), state_->custom_title_bar_);
    state_->log_side_panel_toggle_btn_->setAccessibleName(QStringLiteral("logSidePanelToggleButton"));
    connect(state_->log_side_panel_toggle_btn_, &QToolButton::clicked, this, &MainWindow::toggleLogSidePanel);
    titleLayout->addWidget(state_->log_side_panel_toggle_btn_, 0, Qt::AlignVCenter);
    addTitleBarSeparator(titleLayout);

    state_->window_minimize_btn_ = createTitleBarIconButton(QStringLiteral("windowMinimizeButton"), state_->custom_title_bar_);
    connect(state_->window_minimize_btn_, &QToolButton::clicked, this, &QWidget::showMinimized);
    titleLayout->addWidget(state_->window_minimize_btn_, 0, Qt::AlignVCenter);

    state_->window_maximize_btn_ = createTitleBarIconButton(QStringLiteral("windowMaximizeButton"), state_->custom_title_bar_);
    connect(state_->window_maximize_btn_, &QToolButton::clicked, this, &MainWindow::toggleWindowMaximized);
    titleLayout->addWidget(state_->window_maximize_btn_, 0, Qt::AlignVCenter);

    state_->window_close_btn_ = createTitleBarIconButton(QStringLiteral("windowCloseButton"), state_->custom_title_bar_);
    connect(state_->window_close_btn_, &QToolButton::clicked, this, &QWidget::close);
    titleLayout->addWidget(state_->window_close_btn_, 0, Qt::AlignVCenter);

    menuBar()->hide();
    setMenuWidget(state_->custom_title_bar_);
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
    auto *separator = new QFrame(state_->custom_title_bar_);
    separator->setObjectName(QStringLiteral("titleBarSeparator"));
    separator->setFixedWidth(1);
    separator->setFixedHeight(scalePixels(28));
    separator->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    layout->addWidget(separator, 0, Qt::AlignVCenter);
}

void MainWindow::discardTitleApplicationMenuPanel()
{
    if (!state_->title_application_panel_ && !state_->title_application_sub_panel_ && !state_->title_application_nested_panel_)
    {
        return;
    }

    QFrame *panel = state_->title_application_panel_;
    QFrame *subPanel = state_->title_application_sub_panel_;
    QFrame *nestedPanel = state_->title_application_nested_panel_;
    state_->title_application_panel_ = nullptr;
    state_->title_application_sub_panel_ = nullptr;
    state_->title_application_nested_panel_ = nullptr;
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
    if (state_->title_application_panel_ || !state_->central_widget_)
    {
        return;
    }

    auto *panel = createFloatingTitleMenuPanel(this);
    panel->setObjectName(QStringLiteral("titleApplicationPanel"));
    panel->hide();
    state_->title_application_panel_ = panel;
    panel->raise();

    auto *subPanel = createFloatingTitleMenuPanel(this);
    subPanel->setObjectName(QStringLiteral("titleApplicationSubPanel"));
    subPanel->hide();
    state_->title_application_sub_panel_ = subPanel;
    subPanel->raise();

    auto *nestedPanel = createFloatingTitleMenuPanel(this);
    nestedPanel->setObjectName(QStringLiteral("titleApplicationNestedPanel"));
    nestedPanel->hide();
    state_->title_application_nested_panel_ = nestedPanel;
    nestedPanel->raise();

    auto closePanel = [this]() {
        if (state_->title_application_panel_)
        {
            state_->title_application_panel_->hide();
        }
        if (state_->title_application_sub_panel_)
        {
            state_->title_application_sub_panel_->hide();
        }
        if (state_->title_application_nested_panel_)
        {
            state_->title_application_nested_panel_->hide();
        }
    };

    const bool uiBusy = state_->connection_attempt_in_progress_ || state_->port_detection_in_progress_ || state_->epsilon_reconfigure_in_progress_;

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
    panel->setStyleSheet(titleApplicationPanelStyleSheet(state_->dark_theme_enabled_));
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
    subPanel->setStyleSheet(titleApplicationPanelStyleSheet(state_->dark_theme_enabled_));
    nestedPanel->setStyleSheet(titleApplicationPanelStyleSheet(state_->dark_theme_enabled_));
    auto commandRowsHeight = [menuVerticalPadding, rowHeight](const QVector<TitleMenuCommand>& commands) {
        return menuVerticalPadding * 2 + rowHeight * static_cast<int>(commands.size());
    };

    QVector<TitleMenuSection> sections;
    TitleMenuSection fileSection{
        state_->is_english_ ? QStringLiteral("File") : QStringLiteral("文件"),
        {
            {state_->is_english_ ? QStringLiteral("Recording Folder...") : QStringLiteral("记录目录..."),
             QStringLiteral("Ctrl+R"),
             true,
             false,
             false,
             [this]() { onChooseRecordingDirectoryClicked(); }},
            {state_->is_english_ ? QStringLiteral("Data Viewer...") : QStringLiteral("数据查看器..."),
             QString(),
             true,
             false,
             false,
             [this]() { onOpenSessionViewerClicked(); }},
            {state_->is_english_ ? QStringLiteral("Exit") : QStringLiteral("退出"),
             QStringLiteral("Ctrl+Q"),
             true,
             false,
             true,
             [this]() { close(); }}
        }
    };

    TitleMenuSection viewSection{
        state_->is_english_ ? QStringLiteral("View") : QStringLiteral("视图"),
        {
            {state_->is_english_ ? QStringLiteral("Tiny (70%)") : QStringLiteral("超小 (70%)"),
             QString(),
             true,
             state_->font_scale_percent_ == 70,
             true,
             [this]() { setFontScale(70); }},
            {state_->is_english_ ? QStringLiteral("Extra Small (80%)") : QStringLiteral("特小 (80%)"),
             QString(),
             true,
             state_->font_scale_percent_ == 80,
             false,
             [this]() { setFontScale(80); }},
            {state_->is_english_ ? QStringLiteral("Small (90%)") : QStringLiteral("小号 (90%)"),
             QString(),
             true,
             state_->font_scale_percent_ == 90,
             false,
             [this]() { setFontScale(90); }},
            {state_->is_english_ ? QStringLiteral("Normal (100%)") : QStringLiteral("标准 (100%)"),
             QString(),
             true,
             state_->font_scale_percent_ == 100,
             false,
             [this]() { setFontScale(100); }},
            {state_->is_english_ ? QStringLiteral("Large (115%)") : QStringLiteral("大号 (115%)"),
             QString(),
             true,
             state_->font_scale_percent_ == 115,
             false,
             [this]() { setFontScale(115); }},
            {state_->is_english_ ? QStringLiteral("Extra Large (130%)") : QStringLiteral("超大 (130%)"),
             QString(),
             true,
             state_->font_scale_percent_ == 130,
             false,
             [this]() { setFontScale(130); }}
#ifdef VAPORVIEW_HAS_OSGEARTH
            ,
            {state_->is_english_ ? QStringLiteral("3D Map...") : QStringLiteral("三维地图..."),
             QString(),
             true,
             false,
             true,
             [this]() { onOpenMap3DWindowClicked(); }}
#endif
        }
    };

    TitleMenuSection developerSection{state_->is_english_ ? QStringLiteral("Developer") : QStringLiteral("开发者"), {}};
#ifdef VAPORVIEW_HAS_OSGEARTH
    developerSection.commands.push_back(
        {state_->is_english_ ? QStringLiteral("Map Data Diagnostics...") : QStringLiteral("地图数据诊断..."),
         QString(),
         true,
         false,
         false,
         [this]() { onOpenMap3DDiagnosticsClicked(); }});
#endif
    developerSection.commands.push_back(
        {state_->is_english_ ? QStringLiteral("TCP wave: record every raw frame") : QStringLiteral("TCP波形：记录完整原始帧"),
         QString(),
         true,
         state_->waveform_recording_rate_hz_ == 0,
         false,
         [this]() { setWaveformRecordingRateHz(0); }});
    developerSection.commands.push_back(
        {state_->is_english_ ? QStringLiteral("EPSILON: record verified raw frames") : QStringLiteral("EPSILON：记录已校验原始帧"),
         QString(),
         true,
         state_->imu_recording_rate_hz_ == 0,
         false,
         [this]() { setImuRecordingRateHz(0); }});
    QVector<TitleMenuCommand> csvRateCommands;
    for (int rate : QVector<int>{1, 2, 5, 10, 20, 50, 100, 200})
    {
        csvRateCommands.push_back({
            QStringLiteral("%1 Hz").arg(rate),
            QString(),
            true,
            rate == std::clamp(state_->recording_export_rate_hz_, 1, 200),
            false,
            [this, rate]() { setRecordingExportRateHz(rate); }
        });
    }
    developerSection.commands.push_back(
        {state_->is_english_ ? QStringLiteral("Device CSV recording rate") : QStringLiteral("设备CSV记录频率"),
         QStringLiteral("%1 Hz").arg(std::clamp(state_->recording_export_rate_hz_, 1, 200)),
         true,
         false,
         true,
         {},
         csvRateCommands});
    developerSection.commands.push_back(
        {state_->is_english_ ? QStringLiteral("EPSILON Packet Rates...") : QStringLiteral("设置EPSILON包频率..."),
         QString(),
         !uiBusy,
         false,
         true,
         [this]() { onConfigureEpsilonPacketRatesClicked(); }});
    developerSection.commands.push_back(
        {state_->is_english_ ? QStringLiteral("Configure EPSILON RTCM Port...") : QStringLiteral("配置EPSILON RTCM串口..."),
         QString(),
         !uiBusy,
         false,
         false,
         [this]() { onConfigureEpsilonRtcmPortClicked(); }});
    developerSection.commands.push_back(
        {state_->is_english_ ? QStringLiteral("Reconfigure EPSILON Output...") : QStringLiteral("重新配置EPSILON输出..."),
         QString(),
         !uiBusy,
         false,
         false,
         [this]() { onReconfigureEpsilonClicked(); }});
    developerSection.commands.push_back(
        {state_->is_english_ ? QStringLiteral("RTK Config") : QStringLiteral("RTK配置"),
         QString(),
         true,
         false,
         true,
         [this]() { onRtkConfigClicked(); }});

    TitleMenuSection helpSection{
        state_->is_english_ ? QStringLiteral("Help") : QStringLiteral("帮助"),
        {
            {state_->is_english_ ? QStringLiteral("About") : QStringLiteral("关于"),
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
            installMenuItemEventFilter(row, {}, [closePanel, clickHandler]() {
                closePanel();
                clickHandler();
            });
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
                installMenuItemEventFilter(commandRow, [showNestedMenu, command, commandRow]() {
                    showNestedMenu(command.submenu, commandRow);
                });
            }
            else
            {
                installMenuItemEventFilter(commandRow, hideNestedMenu);
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
        installMenuItemEventFilter(sectionRow, [this, stack, subMenu, mainMenu, panel, subPanel, nestedPanel, mainMenuWidth, menuVerticalPadding, rowSpacing, subMenuWidths, sectionRows, sectionRow, sectionIndex, nestedMenu, activeNestedSource]() {
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
        });
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
    if (!state_->title_menu_btn_)
    {
        return;
    }

    createTitleApplicationMenuPanel();
    if (!state_->title_application_panel_)
    {
        return;
    }

    if (state_->title_application_panel_->isVisible())
    {
        state_->title_application_panel_->hide();
        if (state_->title_application_sub_panel_)
        {
            state_->title_application_sub_panel_->hide();
        }
        if (state_->title_application_nested_panel_)
        {
            state_->title_application_nested_panel_->hide();
        }
        return;
    }

    const QPoint anchor = state_->title_menu_btn_->mapToGlobal(QPoint(0, state_->title_menu_btn_->height() + scalePixels(4)));
    const int popupMargin = scalePixels(4);
    const QRect screenRect = screen() ? screen()->availableGeometry() : QRect(mapToGlobal(QPoint(0, 0)), size());
    const int x = std::clamp(anchor.x() - scalePixels(kFloatingMenuShadowMarginPx),
                             screenRect.left() + popupMargin,
                             std::max(screenRect.left() + popupMargin,
                                      screenRect.right() - state_->title_application_panel_->width() - popupMargin));
    const int y = std::max(anchor.y() - scalePixels(kFloatingMenuShadowMarginPx),
                           screenRect.top() + popupMargin);
    state_->title_application_panel_->move(x, y);
    if (state_->title_application_sub_panel_)
    {
        state_->title_application_sub_panel_->hide();
    }
    if (state_->title_application_nested_panel_)
    {
        state_->title_application_nested_panel_->hide();
    }
    state_->title_application_panel_->show();
    state_->title_application_panel_->raise();
}

void MainWindow::setupCentralWidget()
{
    state_->central_widget_ = new QWidget(this);
    state_->central_widget_->setObjectName("appCentralWidget");
    state_->central_widget_->setAttribute(Qt::WA_StyledBackground, true);
    state_->central_widget_->setAutoFillBackground(true);
    setCentralWidget(state_->central_widget_);

    auto *main_h_layout = new QHBoxLayout(state_->central_widget_);
    main_h_layout->setSpacing(0);
    main_h_layout->setContentsMargins(kAppSidebarVisualPadding,
                                      kAppSidebarVisualPadding,
                                      kAppSidebarVisualPadding,
                                      kAppSidebarVisualPadding);

    state_->main_page_stack_ = new QStackedWidget(state_->central_widget_);
    state_->main_page_stack_->setObjectName(QStringLiteral("mainPageStack"));
    state_->main_page_stack_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    connect(state_->main_page_stack_, &QStackedWidget::currentChanged, this, [this]() {
        updateCustomTitleBarTexts();
    });

    auto *left_widget = new QWidget(this);
    left_widget->setObjectName("mainCardsPane");
    left_widget->setAttribute(Qt::WA_StyledBackground, true);
    left_widget->setAutoFillBackground(true);
    state_->main_layout_ = new QVBoxLayout(left_widget);
    state_->main_layout_->setSpacing(0);
    state_->main_layout_->setContentsMargins(0, 0, 0, 0);

    setupConfigPanel();
    setupDataPanels();

    state_->main_cards_scroll_area_ = new QScrollArea(this);
    state_->main_cards_scroll_area_->setObjectName("mainCardsScrollArea");
    state_->main_cards_scroll_area_->setAttribute(Qt::WA_StyledBackground, true);
    state_->main_cards_scroll_area_->setAutoFillBackground(true);
    state_->main_cards_scroll_area_->viewport()->setObjectName("mainCardsViewport");
    state_->main_cards_scroll_area_->viewport()->setAttribute(Qt::WA_StyledBackground, true);
    state_->main_cards_scroll_area_->viewport()->setAutoFillBackground(true);
    state_->main_cards_scroll_area_->setWidgetResizable(true);
    state_->main_cards_scroll_area_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    state_->main_cards_scroll_area_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    state_->main_cards_scroll_area_->setFrameShape(QFrame::NoFrame);
    state_->main_cards_scroll_area_->setMinimumWidth(0);
    state_->main_cards_scroll_area_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
    state_->main_cards_scroll_area_->setWidget(left_widget);
    installScrollAreaBottomFade(state_->main_cards_scroll_area_);

    setupLogPanel();

    state_->app_sidebar_ = createAppSidebarFrame(state_->central_widget_);
    state_->app_sidebar_->setObjectName(QStringLiteral("appSidebar"));
    state_->app_sidebar_->setAttribute(Qt::WA_StyledBackground, true);
    state_->app_sidebar_->setAutoFillBackground(true);
    state_->app_sidebar_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    state_->app_sidebar_->setMinimumWidth(0);
    state_->app_sidebar_->setMaximumWidth(QWIDGETSIZE_MAX);
    auto *sidebarLayout = new QVBoxLayout(state_->app_sidebar_);
    sidebarLayout->setContentsMargins(kAppSidebarVisualPadding,
                                      kAppSidebarTopBottomPadding,
                                      kAppSidebarVisualPadding,
                                      kAppSidebarTopBottomPadding);
    sidebarLayout->setSpacing(6);
    state_->app_nav_button_group_ = new QButtonGroup(this);
    state_->app_nav_button_group_->setExclusive(true);
    auto createNavButton = [this, sidebarLayout](const QString& text, const QString& iconName) {
        auto *button = new QPushButton(text, state_->app_sidebar_);
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
        button->setAccessibleName(text);
        sidebarLayout->addWidget(button);
        return button;
    };
    state_->home_nav_btn_ = createNavButton(QStringLiteral("首页"), QStringLiteral("square-activity"));
    state_->device_config_nav_btn_ = createNavButton(QStringLiteral("设备配置"), QStringLiteral("sliders-vertical"));
    state_->temperature_nav_btn_ = createNavButton(QStringLiteral("温控"), QStringLiteral("thermometer"));
    state_->rtk_config_nav_btn_ = createNavButton(QStringLiteral("RTK配置"), QStringLiteral("satellite"));
    state_->app_nav_button_group_->addButton(state_->home_nav_btn_, 0);
    state_->app_nav_button_group_->addButton(state_->device_config_nav_btn_, 1);
    state_->app_nav_button_group_->addButton(state_->temperature_nav_btn_, 2);
    state_->app_nav_button_group_->addButton(state_->rtk_config_nav_btn_, 3);
    sidebarLayout->addStretch(1);
    state_->home_nav_btn_->setChecked(true);
    updateSidebarNavIcons();

    state_->app_layout_splitter_ = new QSplitter(Qt::Horizontal, state_->central_widget_);
    state_->app_layout_splitter_->setObjectName(QStringLiteral("appLayoutSplitter"));
    state_->app_layout_splitter_->setAttribute(Qt::WA_StyledBackground, true);
    state_->app_layout_splitter_->setAutoFillBackground(true);
    state_->app_layout_splitter_->setChildrenCollapsible(true);
    state_->app_layout_splitter_->setHandleWidth(8);
    state_->app_layout_splitter_->addWidget(state_->app_sidebar_);

    state_->main_content_splitter_ = new QSplitter(Qt::Horizontal, state_->central_widget_);
    state_->main_content_splitter_->setObjectName("mainContentSplitter");
    state_->main_content_splitter_->setAttribute(Qt::WA_StyledBackground, true);
    state_->main_content_splitter_->setAutoFillBackground(true);
    state_->main_content_splitter_->setChildrenCollapsible(true);
    state_->main_content_splitter_->setCollapsible(0, true);
    state_->main_content_splitter_->setCollapsible(1, true);
    state_->main_content_splitter_->setHandleWidth(1);
    state_->main_content_splitter_->addWidget(state_->main_page_stack_);
    state_->main_content_splitter_->addWidget(state_->log_side_panel_);
    if (QSplitterHandle *handle = state_->main_content_splitter_->handle(1))
    {
        handle->installEventFilter(this);
    }
    state_->main_content_splitter_->setStretchFactor(0, 8);
    state_->main_content_splitter_->setStretchFactor(1, 1);
    state_->main_content_splitter_->setSizes({1600, minimumLogSidePanelWidth()});
    connect(state_->main_content_splitter_, &QSplitter::splitterMoved, this, [this]() {
        if (!state_->main_content_splitter_ || state_->log_side_panel_collapsed_)
        {
            return;
        }
        const QList<int> sizes = state_->main_content_splitter_->sizes();
        const int minimumLogWidth = minimumLogSidePanelWidth();
        if (sizes.size() >= 2 && sizes.at(1) >= minimumLogWidth)
        {
            state_->last_log_side_panel_width_ = sizes.at(1);
            state_->log_side_panel_width_initialized_ = true;
        }
        updateResponsiveHomeLayout();
        queueResponsiveHomeLayoutRefresh();
    });

    state_->app_layout_splitter_->addWidget(state_->main_content_splitter_);
    if (QSplitterHandle *handle = state_->app_layout_splitter_->handle(1))
    {
        handle->installEventFilter(this);
    }
    state_->app_layout_splitter_->setCollapsible(0, true);
    state_->app_layout_splitter_->setCollapsible(1, false);
    state_->app_layout_splitter_->setStretchFactor(0, 0);
    state_->app_layout_splitter_->setStretchFactor(1, 1);
    {
        QSettings settings("VaporView", "MainWindow");
        const int restoredAppSidebarWidth = std::max(0, settings.value(
            QStringLiteral("app_sidebar_width"),
            appSidebarDefaultWidth()).toInt());
        const int initialAppSidebarWidth = snappedAppSidebarWidth(restoredAppSidebarWidth);
        if (initialAppSidebarWidth > 0)
        {
            state_->last_app_sidebar_visible_width_ = initialAppSidebarWidth;
        }
        state_->app_sidebar_mode_ = appSidebarModeForWidth(initialAppSidebarWidth);
        updateAppSidebarButtonTexts();
        state_->app_layout_splitter_->setSizes({initialAppSidebarWidth, 1600});
    }
    connect(state_->app_layout_splitter_, &QSplitter::splitterMoved, this, [this]() {
        if (state_->app_sidebar_adjusting_ || !state_->app_layout_splitter_)
        {
            return;
        }
        const QList<int> sizes = state_->app_layout_splitter_->sizes();
        if (sizes.size() < 2)
        {
            return;
        }
        updateAppSidebarForWidth(sizes.at(0), false);
    });

    state_->home_page_ = state_->main_cards_scroll_area_;
    state_->main_page_stack_->addWidget(state_->home_page_);
    setupDeviceConfigPage();

    state_->temperature_page_ = new QWidget(this);
    state_->temperature_page_->setObjectName(QStringLiteral("temperaturePage"));
    auto *temperaturePageLayout = new QVBoxLayout(state_->temperature_page_);
    temperaturePageLayout->setContentsMargins(8, 0, 8, 8);
    temperaturePageLayout->setSpacing(8);
    auto *temperatureScrollArea = new QScrollArea(state_->temperature_page_);
    temperatureScrollArea->setObjectName(QStringLiteral("mainCardsScrollArea"));
    temperatureScrollArea->setWidgetResizable(true);
    temperatureScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    temperatureScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    temperatureScrollArea->setFrameShape(QFrame::NoFrame);
    auto *temperatureContent = new QWidget(temperatureScrollArea);
    auto *temperatureContentLayout = new QVBoxLayout(temperatureContent);
    temperatureContentLayout->setContentsMargins(0, 0, 0, 0);
    temperatureContentLayout->setSpacing(8);
    temperatureContentLayout->addWidget(state_->temperature_controller_group_, 0);
    temperatureContentLayout->addStretch(1);
    temperatureScrollArea->setWidget(temperatureContent);
    installScrollAreaBottomFade(temperatureScrollArea);
    temperaturePageLayout->addWidget(temperatureScrollArea, 1);
    state_->main_page_stack_->addWidget(state_->temperature_page_);

    state_->rtk_config_dialog_ = new RtkConfigDialog(state_->main_page_stack_, true);
    state_->rtk_config_dialog_->setAttribute(Qt::WA_QuitOnClose, false);
    installScrollAreaBottomFade(
        state_->rtk_config_dialog_->findChild<QScrollArea *>(QStringLiteral("rtkConfigScrollArea")));
    connect(state_->rtk_config_dialog_, &RtkConfigDialog::rtkRunningChanged, this, [this](bool running) {
        state_->rtk_service_running_ = running;
        updateRtkConfigIcon();
        updateSidebarNavIcons();
    });
    syncRtkConfigPageState();
    state_->main_page_stack_->addWidget(state_->rtk_config_dialog_);

    connect(state_->app_nav_button_group_, &QButtonGroup::idClicked, this, [this](int id) {
        if (id == 3)
        {
            syncRtkConfigPageState();
        }
        if (state_->main_page_stack_)
        {
            state_->main_page_stack_->setCurrentIndex(std::clamp(id, 0, state_->main_page_stack_->count() - 1));
        }
        updateSidebarNavIcons();
        updateCustomTitleBarTexts();
    });
    main_h_layout->addWidget(state_->app_layout_splitter_, 1);
    updateAppSidebarForWidth(currentAppSidebarWidth(), true);
    updateCustomTitleBarTexts();
}

void MainWindow::setupDeviceConfigPage()
{
    state_->device_config_.page = new QWidget(this);
    state_->device_config_.page->setObjectName(QStringLiteral("deviceConfigPage"));
    auto *pageLayout = new QVBoxLayout(state_->device_config_.page);
    pageLayout->setContentsMargins(8, 0, 8, 8);
    pageLayout->setSpacing(8);

    auto *scrollArea = new QScrollArea(state_->device_config_.page);
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
    state_->device_config_.serial_title_lbl = createSectionTitleCluster(serialTitleBar,
                                                                QStringLiteral("usb"),
                                                                kMainPageButtonHeight,
                                                                &serialTitleCluster);
    serialTitleLayout->addWidget(serialTitleCluster, 0, Qt::AlignVCenter | Qt::AlignLeft);

    state_->device_config_.auto_detect_ports_btn = new QPushButton(serialTitleBar);
    state_->device_config_.auto_detect_ports_btn->setFixedHeight(kMainPageButtonHeight);
    state_->device_config_.auto_detect_ports_btn->setFocusPolicy(Qt::TabFocus);
    state_->device_config_.auto_detect_ports_btn->setMinimumWidth(kDeviceConfigAutoDetectButtonMinWidth);
    connect(state_->device_config_.auto_detect_ports_btn, &QPushButton::clicked, this, &MainWindow::onAutoDetectPortsClicked);
    serialTitleLayout->addWidget(state_->device_config_.auto_detect_ports_btn, 0, Qt::AlignVCenter | Qt::AlignLeft);

    state_->device_config_.data_source_mode_lbl = new QLabel(serialTitleBar);
    state_->device_config_.data_source_mode_lbl->setObjectName(QStringLiteral("fieldLabel"));
    state_->device_config_.data_source_mode_combo = createSingleLevelPopupComboBox(serialTitleBar);
    state_->device_config_.data_source_mode_combo->setFixedHeight(kMainPageInputHeight);
    state_->device_config_.data_source_mode_combo->setFixedWidth(kDeviceConfigSourceModeComboWidth);
    state_->device_config_.data_source_mode_combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    serialTitleLayout->addWidget(state_->device_config_.data_source_mode_lbl, 0, Qt::AlignVCenter | Qt::AlignRight);
    serialTitleLayout->addWidget(state_->device_config_.data_source_mode_combo, 0, Qt::AlignVCenter);

    state_->device_config_.sky_device_config_btn = new QPushButton(serialTitleBar);
    state_->device_config_.sky_device_config_btn->setFixedHeight(kMainPageButtonHeight);
    state_->device_config_.sky_device_config_btn->setFocusPolicy(Qt::TabFocus);
    state_->device_config_.sky_device_config_btn->setMinimumWidth(kDeviceConfigSkyDeviceButtonMinWidth);
    connect(state_->device_config_.sky_device_config_btn, &QPushButton::clicked, this, &MainWindow::onSkyDeviceConfigClicked);
    serialTitleLayout->addWidget(state_->device_config_.sky_device_config_btn, 0, Qt::AlignVCenter | Qt::AlignLeft);
    serialTitleLayout->addStretch(1);
    serialLayout->addWidget(serialTitleBar);

    auto *skyTelemetryRow = new QWidget(serialCard);
    state_->device_config_.sky_telemetry_row_widget = skyTelemetryRow;
    auto *skyTelemetryLayout = new QHBoxLayout(skyTelemetryRow);
    skyTelemetryLayout->setContentsMargins(8, 2, 8, 2);
    skyTelemetryLayout->setSpacing(6);

    state_->device_config_.sky_telemetry_transport_lbl = new QLabel(skyTelemetryRow);
    state_->device_config_.sky_telemetry_transport_lbl->setObjectName(QStringLiteral("fieldLabel"));
    state_->device_config_.sky_telemetry_transport_combo = new QComboBox(skyTelemetryRow);
    state_->device_config_.sky_telemetry_transport_combo->setFixedHeight(kMainPageInputHeight);
    state_->device_config_.sky_telemetry_transport_combo->setFixedWidth(110);

    state_->device_config_.sky_telemetry_tcp_host_lbl = new QLabel(skyTelemetryRow);
    state_->device_config_.sky_telemetry_tcp_host_lbl->setObjectName(QStringLiteral("fieldLabel"));
    state_->device_config_.sky_telemetry_tcp_host_edit = new QLineEdit(skyTelemetryRow);
    state_->device_config_.sky_telemetry_tcp_host_edit->setFixedHeight(kMainPageInputHeight);
    state_->device_config_.sky_telemetry_tcp_host_edit->setMinimumWidth(132);
    state_->device_config_.sky_telemetry_tcp_host_edit->setMaximumWidth(160);

    state_->device_config_.sky_telemetry_tcp_port_lbl = new QLabel(skyTelemetryRow);
    state_->device_config_.sky_telemetry_tcp_port_lbl->setObjectName(QStringLiteral("fieldLabel"));
    state_->device_config_.sky_telemetry_tcp_port_spin = new QSpinBox(skyTelemetryRow);
    state_->device_config_.sky_telemetry_tcp_port_spin->setRange(1, 65535);
    state_->device_config_.sky_telemetry_tcp_port_spin->setFixedHeight(kMainPageInputHeight);
    state_->device_config_.sky_telemetry_tcp_port_spin->setFixedWidth(100);

    state_->device_config_.sky_telemetry_port_lbl = new QLabel(skyTelemetryRow);
    state_->device_config_.sky_telemetry_port_lbl->setObjectName(QStringLiteral("fieldLabel"));
    state_->device_config_.sky_telemetry_port_combo = new QComboBox(skyTelemetryRow);
    state_->device_config_.sky_telemetry_port_combo->setObjectName(QStringLiteral("deviceSkyTelemetryPortCombo"));
    installLocalSerialPortComboBehavior(state_->device_config_.sky_telemetry_port_combo);
    state_->device_config_.sky_telemetry_port_combo->setFixedHeight(kMainPageInputHeight);
    state_->device_config_.sky_telemetry_port_combo->setFixedWidth(108);

    state_->device_config_.sky_telemetry_baud_lbl = new QLabel(skyTelemetryRow);
    state_->device_config_.sky_telemetry_baud_lbl->setObjectName(QStringLiteral("fieldLabel"));
    state_->device_config_.sky_telemetry_baud_combo = new QComboBox(skyTelemetryRow);
    state_->device_config_.sky_telemetry_baud_combo->setFixedHeight(kMainPageInputHeight);
    state_->device_config_.sky_telemetry_baud_combo->setFixedWidth(100);

    skyTelemetryLayout->addWidget(state_->device_config_.sky_telemetry_transport_lbl, 0, Qt::AlignVCenter | Qt::AlignLeft);
    skyTelemetryLayout->addWidget(state_->device_config_.sky_telemetry_transport_combo, 0, Qt::AlignVCenter);
    skyTelemetryLayout->addWidget(state_->device_config_.sky_telemetry_tcp_host_lbl, 0, Qt::AlignVCenter | Qt::AlignLeft);
    skyTelemetryLayout->addWidget(state_->device_config_.sky_telemetry_tcp_host_edit, 0, Qt::AlignVCenter);
    skyTelemetryLayout->addWidget(state_->device_config_.sky_telemetry_tcp_port_lbl, 0, Qt::AlignVCenter | Qt::AlignLeft);
    skyTelemetryLayout->addWidget(state_->device_config_.sky_telemetry_tcp_port_spin, 0, Qt::AlignVCenter);
    skyTelemetryLayout->addWidget(state_->device_config_.sky_telemetry_port_lbl, 0, Qt::AlignVCenter | Qt::AlignLeft);
    skyTelemetryLayout->addWidget(state_->device_config_.sky_telemetry_port_combo, 0, Qt::AlignVCenter);
    skyTelemetryLayout->addWidget(state_->device_config_.sky_telemetry_baud_lbl, 0, Qt::AlignVCenter | Qt::AlignLeft);
    skyTelemetryLayout->addWidget(state_->device_config_.sky_telemetry_baud_combo, 0, Qt::AlignVCenter);
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
    constexpr int kDeviceConfigSourceComboWidth = 108;

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

        portCombo = createCombo(kDeviceConfigPortComboWidth);
        installLocalSerialPortComboBehavior(portCombo);
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
    addPortRow(state_->device_config_.epsilon_lbl, state_->device_config_.epsilon_port_combo, state_->device_config_.epsilon_baud_combo,
               state_->device_config_.epsilon_rate_lbl, unusedEpsilonRateCombo, 0);
    addPortRow(state_->device_config_.ptb_lbl, state_->device_config_.ptb_port_combo, state_->device_config_.ptb_baud_combo,
               state_->device_config_.ptb_rate_lbl, state_->device_config_.ptb_rate_combo, 1);
    addPortRow(state_->device_config_.hmp_lbl, state_->device_config_.hmp_port_combo, state_->device_config_.hmp_baud_combo,
               state_->device_config_.hmp_rate_lbl, state_->device_config_.hmp_rate_combo, 2);
    addPortRow(state_->device_config_.lidar_lbl, state_->device_config_.lidar_port_combo, state_->device_config_.lidar_baud_combo,
               state_->device_config_.lidar_rate_lbl, state_->device_config_.lidar_rate_combo, 3);
    addPortRow(state_->device_config_.temperature_lbl, state_->device_config_.temperature_port_combo, state_->device_config_.temperature_baud_combo,
               state_->device_config_.temperature_rate_lbl, state_->device_config_.temperature_rate_combo, 4);
    state_->device_config_.ptb_baud_combo->setObjectName(QStringLiteral("devicePressureBaudCombo"));
    state_->device_config_.hmp_baud_combo->setObjectName(QStringLiteral("deviceHumidityBaudCombo"));
    if (state_->device_config_.epsilon_port_combo)
    {
        state_->device_config_.epsilon_port_combo->setObjectName(QStringLiteral("deviceEpsilonPortCombo"));
    }
    if (state_->device_config_.ptb_port_combo)
    {
        state_->device_config_.ptb_port_combo->setObjectName(QStringLiteral("devicePressurePortCombo"));
    }
    if (state_->device_config_.hmp_port_combo)
    {
        state_->device_config_.hmp_port_combo->setObjectName(QStringLiteral("deviceHumidityPortCombo"));
    }
    if (state_->device_config_.lidar_port_combo)
    {
        state_->device_config_.lidar_port_combo->setObjectName(QStringLiteral("deviceLidarPortCombo"));
    }
    state_->device_config_.ptb_source_combo = createCombo(kDeviceConfigSourceComboWidth);
    state_->device_config_.ptb_source_combo->setObjectName(QStringLiteral("devicePressureSourceCombo"));
    state_->device_config_.ptb_source_combo->addItem(QStringLiteral("PTB210"), QStringLiteral("ptb210"));
    state_->device_config_.ptb_source_combo->addItem(QStringLiteral("BMP390"), QStringLiteral("bmp390"));
    state_->device_config_.ptb_source_combo->setToolTip(state_->is_english_
        ? QStringLiteral("Pressure source. BMP390 expects the Waveshare example serial output at 115200 8N1.")
        : QStringLiteral("气压来源。BMP390 使用微雪示例程序通过 115200 8N1 串口输出。"));
    formLayout->addWidget(state_->device_config_.ptb_source_combo, 1, 5, Qt::AlignVCenter);

    state_->device_config_.hmp_source_combo = createCombo(kDeviceConfigSourceComboWidth);
    state_->device_config_.hmp_source_combo->setObjectName(QStringLiteral("deviceHumiditySourceCombo"));
    state_->device_config_.hmp_source_combo->addItem(QStringLiteral("HMP3"), QStringLiteral("hmp3"));
    state_->device_config_.hmp_source_combo->addItem(QStringLiteral("SHT45"), QStringLiteral("sht45"));
    state_->device_config_.hmp_source_combo->setToolTip(state_->is_english_
        ? QStringLiteral("Temperature/humidity source. SHT45 expects Adafruit example serial output at 115200 8N1.")
        : QStringLiteral("温湿度来源。SHT45 使用 Adafruit 示例程序通过 115200 8N1 串口输出。"));
    formLayout->addWidget(state_->device_config_.hmp_source_combo, 2, 5, Qt::AlignVCenter);
    auto bindSensorSourceBaud = [this](QComboBox *sourceCombo,
                                       QComboBox *deviceBaudCombo,
                                       QComboBox *homeBaudCombo) {
        sourceCombo->setProperty(kSensorBaudSourceProperty, sourceCombo->currentData().toString());
        connect(sourceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                [this, sourceCombo, deviceBaudCombo, homeBaudCombo](int) {
            QSettings settings(QStringLiteral("VaporView"), QStringLiteral("MainWindow"));
            const QString previousSource = sourceCombo->property(kSensorBaudSourceProperty).toString();
            saveRememberedSensorBaud(
                settings, previousSource, homeBaudCombo ? homeBaudCombo : deviceBaudCombo);

            const QString currentSource = sourceCombo->currentData().toString();
            const QString baud = rememberedSensorBaud(settings, currentSource);
            applyComboText(homeBaudCombo, baud);
            applyComboText(deviceBaudCombo, baud);
            sourceCombo->setProperty(kSensorBaudSourceProperty, currentSource);
            saveRememberedInputState();
        });
    };
    bindSensorSourceBaud(
        state_->device_config_.ptb_source_combo, state_->device_config_.ptb_baud_combo, state_->ptb_baud_combo_);
    bindSensorSourceBaud(
        state_->device_config_.hmp_source_combo, state_->device_config_.hmp_baud_combo, state_->hmp_baud_combo_);
    if (state_->device_config_.temperature_port_combo)
    {
        state_->device_config_.temperature_port_combo->setObjectName(QStringLiteral("deviceTemperaturePortCombo"));
    }
    if (state_->device_config_.temperature_baud_combo)
    {
        state_->device_config_.temperature_baud_combo->setObjectName(QStringLiteral("deviceTemperatureBaudCombo"));
    }

    auto addDeviceRemoteButton = [this, formLayout, formWidget](
            int row,
            QWidget *&buttonsWidget,
            QToolButton *&actionButton,
            VaporView::SkyDeviceId device) {
        buttonsWidget = new QWidget(formWidget);
        buttonsWidget->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        auto *layout = new QHBoxLayout(buttonsWidget);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(2);
        actionButton = new QToolButton(buttonsWidget);
        actionButton->setFocusPolicy(Qt::TabFocus);
        applyDeviceConfigRemoteButtonPresentation(actionButton,
                                                  VaporView::CommandId::ConnectDevice,
                                                  device,
                                                  state_->is_english_,
                                                  true);
        connect(actionButton, &QToolButton::clicked, this, [this, device]() {
            triggerHomeDeviceAction(device);
        });
        layout->addWidget(actionButton);
        formLayout->addWidget(buttonsWidget, row, 6, Qt::AlignVCenter | Qt::AlignLeft);
    };
    addDeviceRemoteButton(0, state_->device_config_.epsilon_remote_buttons_widget,
                           state_->device_config_.epsilon_remote_action_btn,
                           VaporView::SkyDeviceId::Epsilon);
    addDeviceRemoteButton(1, state_->device_config_.ptb_remote_buttons_widget,
                           state_->device_config_.ptb_remote_action_btn,
                           VaporView::SkyDeviceId::Ptb);
    addDeviceRemoteButton(2, state_->device_config_.hmp_remote_buttons_widget,
                           state_->device_config_.hmp_remote_action_btn,
                           VaporView::SkyDeviceId::Hmp);
    addDeviceRemoteButton(3, state_->device_config_.lidar_remote_buttons_widget,
                           state_->device_config_.lidar_remote_action_btn,
                           VaporView::SkyDeviceId::Lidar);
    addDeviceRemoteButton(4, state_->device_config_.temperature_remote_buttons_widget,
                           state_->device_config_.temperature_remote_action_btn,
                           VaporView::SkyDeviceId::TemperatureController);

    state_->device_config_.epsilon_config_card = new QFrame(content);
    state_->device_config_.epsilon_config_card->setObjectName(QStringLiteral("epsilonSectionCard"));
    state_->device_config_.epsilon_config_card->setMinimumWidth(520);
    state_->device_config_.epsilon_config_card->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    auto *epsilonConfigLayout = new QVBoxLayout(state_->device_config_.epsilon_config_card);
    epsilonConfigLayout->setContentsMargins(1, 0, 1, 1);
    epsilonConfigLayout->setSpacing(0);
    auto *epsilonTitleBar = new QWidget(state_->device_config_.epsilon_config_card);
    epsilonTitleBar->setObjectName(QStringLiteral("sectionTitleBar"));
    epsilonTitleBar->setFixedHeight(kMainPageTitleBarHeight);
    auto *epsilonTitleLayout = new QHBoxLayout(epsilonTitleBar);
    epsilonTitleLayout->setContentsMargins(8, 2, 8, 2);
    epsilonTitleLayout->setSpacing(8);
    QWidget *epsilonTitleCluster = nullptr;
    state_->device_config_.epsilon_config_title_lbl = createSectionTitleCluster(epsilonTitleBar,
                                                                        QStringLiteral("sliders-vertical"),
                                                                        kMainPageButtonHeight,
                                                                        &epsilonTitleCluster);
    epsilonTitleLayout->addWidget(epsilonTitleCluster, 0, Qt::AlignVCenter | Qt::AlignLeft);
    epsilonTitleLayout->addStretch(1);
    epsilonConfigLayout->addWidget(epsilonTitleBar);

    auto *epsilonBodyWidget = new QWidget(state_->device_config_.epsilon_config_card);
    auto *epsilonBodyLayout = new QVBoxLayout(epsilonBodyWidget);
    epsilonBodyLayout->setContentsMargins(8, 8, 8, 8);
    epsilonBodyLayout->setSpacing(7);

    state_->device_config_.epsilon_config_hint_lbl = new QLabel(epsilonBodyWidget);
    state_->device_config_.epsilon_config_hint_lbl->setObjectName(QStringLiteral("fieldLabel"));
    state_->device_config_.epsilon_config_hint_lbl->setWordWrap(true);
    state_->device_config_.epsilon_config_hint_lbl->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
    epsilonBodyLayout->addWidget(state_->device_config_.epsilon_config_hint_lbl);

    state_->device_config_.epsilon_packet_custom_check =
        createTitleBarFeedbackCheckBox(epsilonBodyWidget);
    state_->device_config_.epsilon_packet_custom_check->setObjectName(
        QStringLiteral("epsilonPacketCustomCheck"));
    state_->device_config_.epsilon_packet_custom_check->setFocusPolicy(Qt::TabFocus);
    epsilonBodyLayout->addWidget(state_->device_config_.epsilon_packet_custom_check);

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
        QComboBox comboProbe(state_->device_config_.epsilon_config_card);
        const QFontMetrics metrics(comboProbe.font());
        for (const EpsilonPacketConfigOption& option : epsilonPacketConfigOptions())
        {
            for (int rateHz : option.supported_rates_hz)
            {
                packetComboWidth = std::max(packetComboWidth,
                                            metrics.horizontalAdvance(epsilonPacketRateDisplayText(rateHz, state_->is_english_)));
            }
        }
    }
    packetComboWidth = std::clamp(packetComboWidth + 50, 126, 160);
    state_->device_config_.epsilon_packet_rate_labels.clear();
    state_->device_config_.epsilon_packet_rate_combos.clear();
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
            combo->addItem(epsilonPacketRateDisplayText(rateHz, state_->is_english_), rateHz);
        }
        packetGrid->addWidget(combo, row, comboColumn, Qt::AlignLeft | Qt::AlignVCenter);

        state_->device_config_.epsilon_packet_rate_labels.append(label);
        state_->device_config_.epsilon_packet_rate_combos.append(combo);

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
    state_->device_config_.epsilon_packet_defaults_btn = createInlineButton(packetButtonPanel);
    state_->device_config_.epsilon_packet_grouped_btn = createInlineButton(packetButtonPanel);
    state_->device_config_.epsilon_packet_save_btn = createInlineButton(packetButtonPanel);
    state_->device_config_.epsilon_rtcm_port_btn = createInlineButton(packetButtonPanel);
    state_->device_config_.epsilon_reconfigure_btn = createInlineButton(packetButtonPanel);
    state_->device_config_.rtk_config_btn = createInlineButton(packetButtonPanel);
    packetButtonLayout->addWidget(state_->device_config_.epsilon_packet_defaults_btn, 0, 0);
    packetButtonLayout->addWidget(state_->device_config_.epsilon_packet_grouped_btn, 0, 1);
    packetButtonLayout->addWidget(state_->device_config_.epsilon_packet_save_btn, 1, 0);
    packetButtonLayout->addWidget(state_->device_config_.epsilon_rtcm_port_btn, 1, 1);
    packetButtonLayout->addWidget(state_->device_config_.epsilon_reconfigure_btn, 2, 0);
    packetButtonLayout->addWidget(state_->device_config_.rtk_config_btn, 2, 1);
    packetGrid->addWidget(packetButtonPanel,
                          0,
                          kDeviceConfigPacketTrailingColumn,
                          4,
                          1,
                          Qt::AlignLeft | Qt::AlignTop);

    connect(state_->device_config_.epsilon_packet_defaults_btn, &QPushButton::clicked, this, [this]() {
        if (state_->device_config_.epsilon_packet_custom_check)
        {
            state_->device_config_.epsilon_packet_custom_check->setChecked(true);
        }
        setDeviceConfigEpsilonPacketRates(defaultEpsilonPacketRates());
    });
    connect(state_->device_config_.epsilon_packet_grouped_btn, &QPushButton::clicked, this, [this]() {
        const QString epsilonRateText = state_->epsilon_rate_combo_ ? state_->epsilon_rate_combo_->currentText() : QStringLiteral("100");
        const int groupedRateHz = effectiveRateOrDefault(epsilonRateText, kDefaultEpsilonSampleRateHz, 200);
        if (state_->device_config_.epsilon_packet_custom_check)
        {
            state_->device_config_.epsilon_packet_custom_check->setChecked(false);
        }
        setDeviceConfigEpsilonPacketRates(groupedEpsilonPacketRates(groupedRateHz));
    });
    connect(state_->device_config_.epsilon_packet_save_btn, &QPushButton::clicked, this, [this]() {
        saveDeviceConfigEpsilonPacketRates(true);
    });
    connect(state_->device_config_.epsilon_rtcm_port_btn, &QPushButton::clicked, this, &MainWindow::onConfigureEpsilonRtcmPortClicked);
    connect(state_->device_config_.epsilon_reconfigure_btn, &QPushButton::clicked, this, &MainWindow::onReconfigureEpsilonClicked);
    connect(state_->device_config_.rtk_config_btn, &QPushButton::clicked, this, &MainWindow::onRtkConfigClicked);
    epsilonBodyLayout->addWidget(packetGridWidget);
    epsilonConfigLayout->addWidget(epsilonBodyWidget);
    formRowLayout->addWidget(formWidget, 0, Qt::AlignTop | Qt::AlignLeft);
    serialLayout->addWidget(formRowWidget, 0, Qt::AlignTop);

    state_->device_config_.data_telemetry_summary_card = new QFrame(content);
    state_->device_config_.data_telemetry_summary_card->setObjectName(QStringLiteral("epsilonSectionCard"));
    state_->device_config_.data_telemetry_summary_card->setMinimumWidth(0);
    state_->device_config_.data_telemetry_summary_card->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Minimum);
    auto *summaryLayout = new QVBoxLayout(state_->device_config_.data_telemetry_summary_card);
    summaryLayout->setContentsMargins(1, 0, 1, 1);
    summaryLayout->setSpacing(0);
    auto *summaryTitleBar = new QWidget(state_->device_config_.data_telemetry_summary_card);
    summaryTitleBar->setObjectName(QStringLiteral("sectionTitleBar"));
    summaryTitleBar->setFixedHeight(kMainPageTitleBarHeight);
    auto *summaryTitleLayout = new QHBoxLayout(summaryTitleBar);
    summaryTitleLayout->setContentsMargins(8, 2, 8, 2);
    summaryTitleLayout->setSpacing(8);
    QWidget *summaryTitleCluster = nullptr;
    state_->device_config_.data_telemetry_summary_title_lbl = createSectionTitleCluster(summaryTitleBar,
                                                                               QStringLiteral("satellite"),
                                                                               kMainPageButtonHeight,
                                                                               &summaryTitleCluster);
    summaryTitleLayout->addWidget(summaryTitleCluster, 0, Qt::AlignVCenter | Qt::AlignLeft);
    summaryTitleLayout->addStretch(1);
    summaryLayout->addWidget(summaryTitleBar);

    auto *summaryBodyWidget = new QWidget(state_->device_config_.data_telemetry_summary_card);
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
    createDeviceTelemetrySection(state_->device_config_.data_telemetry_rate_summary_layout);
    createDeviceTelemetrySection(state_->device_config_.data_telemetry_link_summary_layout);
    createDeviceTelemetrySection(state_->device_config_.data_telemetry_device_summary_layout);
    summaryLayout->addWidget(summaryBodyWidget);
    topRowLayout->addWidget(serialCard, 0, Qt::AlignTop | Qt::AlignLeft);
    topRowLayout->addWidget(state_->device_config_.data_telemetry_summary_card, 1, Qt::AlignTop);
    contentLayout->addLayout(topRowLayout);
    contentLayout->addWidget(state_->device_config_.epsilon_config_card, 0, Qt::AlignTop);
    contentLayout->addStretch(1);

    scrollArea->setWidget(content);
    installScrollAreaBottomFade(scrollArea);
    pageLayout->addWidget(scrollArea, 1);
    state_->main_page_stack_->addWidget(state_->device_config_.page);

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
        connect(deviceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, deviceCombo, homeCombo](int index) {
            if (!homeCombo || index < 0 || index >= deviceCombo->count())
            {
                return;
            }
            if (deviceCombo->property(kLocalSerialPortComboProperty).toBool() &&
                isLocalSerialPortManualOption(deviceCombo, index))
            {
                return;
            }
            if (deviceCombo->property(kLocalSerialPortComboProperty).toBool())
            {
                setLocalSerialPortComboText(
                    homeCombo,
                    localSerialPortItemValue(deviceCombo, index));
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
        connect(deviceCombo, &QComboBox::currentTextChanged, this, [this, deviceCombo, homeCombo](const QString& text) {
            if (!homeCombo || homeCombo->currentText() == text)
            {
                return;
            }
            if (deviceCombo->property(kLocalSerialPortComboProperty).toBool())
            {
                if (deviceCombo->property(kLocalSerialPortManualEntryProperty).toBool() ||
                    isLocalSerialPortManualOptionText(text))
                {
                    return;
                }
                setLocalSerialPortComboText(homeCombo, text);
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
            if (homeCombo && homeCombo->property(kLocalSerialPortManualEntryProperty).toBool())
            {
                return;
            }
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
    mirrorComboToHome(state_->device_config_.data_source_mode_combo, state_->data_source_mode_combo_);
    mirrorComboToHome(state_->device_config_.sky_telemetry_transport_combo, state_->sky_telemetry_transport_combo_);
    mirrorComboToHome(state_->device_config_.sky_telemetry_port_combo, state_->sky_telemetry_port_combo_);
    mirrorComboToHome(state_->device_config_.sky_telemetry_baud_combo, state_->sky_telemetry_baud_combo_);
    mirrorComboToHome(state_->device_config_.epsilon_port_combo, state_->epsilon_port_combo_);
    mirrorComboToHome(state_->device_config_.epsilon_baud_combo, state_->epsilon_baud_combo_);
    mirrorComboToHome(state_->device_config_.ptb_port_combo, state_->ptb_port_combo_);
    mirrorComboToHome(state_->device_config_.ptb_baud_combo, state_->ptb_baud_combo_);
    mirrorComboToHome(state_->device_config_.hmp_port_combo, state_->hmp_port_combo_);
    mirrorComboToHome(state_->device_config_.hmp_baud_combo, state_->hmp_baud_combo_);
    mirrorComboToHome(state_->device_config_.lidar_port_combo, state_->lidar_port_combo_);
    mirrorComboToHome(state_->device_config_.lidar_baud_combo, state_->lidar_baud_combo_);
    mirrorComboToHome(state_->device_config_.temperature_port_combo, state_->temperature_port_combo_);
    mirrorComboToHome(state_->device_config_.temperature_baud_combo, state_->temperature_baud_combo_);
    mirrorComboToHome(state_->device_config_.ptb_rate_combo, state_->ptb_rate_combo_);
    mirrorComboToHome(state_->device_config_.hmp_rate_combo, state_->hmp_rate_combo_);
    mirrorComboToHome(state_->device_config_.lidar_rate_combo, state_->lidar_rate_combo_);
    mirrorComboToHome(state_->device_config_.temperature_rate_combo, state_->temperature_rate_combo_);

    connect(state_->device_config_.sky_telemetry_tcp_host_edit, &QLineEdit::textChanged, this, [this](const QString& text) {
        if (state_->sky_telemetry_tcp_host_edit_ && state_->sky_telemetry_tcp_host_edit_->text() != text)
        {
            state_->sky_telemetry_tcp_host_edit_->setText(text);
        }
    });
    connect(state_->device_config_.sky_telemetry_tcp_port_spin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
        if (state_->sky_telemetry_tcp_port_spin_ && state_->sky_telemetry_tcp_port_spin_->value() != value)
        {
            state_->sky_telemetry_tcp_port_spin_->setValue(value);
        }
    });
    if (state_->sky_telemetry_tcp_host_edit_)
    {
        connect(state_->sky_telemetry_tcp_host_edit_, &QLineEdit::textChanged, this, [this]() {
            syncDeviceConfigPageFromHome();
        });
    }
    if (state_->sky_telemetry_tcp_port_spin_)
    {
        connect(state_->sky_telemetry_tcp_port_spin_, QOverload<int>::of(&QSpinBox::valueChanged), this, [this]() {
            syncDeviceConfigPageFromHome();
        });
    }

    updateDeviceConfigTexts();
    syncDeviceConfigPageFromHome();
    updateDeviceConfigState();
}

void MainWindow::updateSidebarNavIcons()
{
    const bool dark = state_->dark_theme_enabled_;
    const QColor normalColor = appThemeColor(AppThemeColor::Text, dark);
    const QColor activeColor = QColor(255, 255, 255);
    for (QPushButton *button : {state_->home_nav_btn_, state_->temperature_nav_btn_, state_->rtk_config_nav_btn_, state_->device_config_nav_btn_})
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
        if (button == state_->rtk_config_nav_btn_ && state_->rtk_service_running_)
        {
            iconColor = toolbarColor(AppThemeColor::ToolbarGreen);
        }
        button->setIcon(createLucideIcon(iconName, iconColor));
    }
}

void MainWindow::syncDeviceConfigPageFromHome()
{
    if (!state_->device_config_.page)
    {
        return;
    }

    auto copyCombo = [this](QComboBox *source, QComboBox *target) {
        if (!source || !target)
        {
            return;
        }
        const bool localSerial = source->property(kLocalSerialPortComboProperty).toBool() ||
                                 target->property(kLocalSerialPortComboProperty).toBool();
        const QSignalBlocker blocker(target);
        const QString currentText = source->currentText();
        const int currentIndex = source->currentIndex();
        if (localSerial)
        {
            installLocalSerialPortComboBehavior(target);
        }
        target->setEditable(localSerial ? false : source->isEditable());
        target->clear();
        for (int i = 0; i < source->count(); ++i)
        {
            target->addItem(source->itemIcon(i), source->itemText(i), source->itemData(i));
            target->setItemData(
                i,
                source->itemData(i, kLocalSerialPortHistoryItemRole),
                kLocalSerialPortHistoryItemRole);
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
        else if (localSerial)
        {
            setLocalSerialPortComboText(target, currentText);
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

    copyCombo(state_->data_source_mode_combo_, state_->device_config_.data_source_mode_combo);
    copyCombo(state_->sky_telemetry_transport_combo_, state_->device_config_.sky_telemetry_transport_combo);
    copyCombo(state_->sky_telemetry_port_combo_, state_->device_config_.sky_telemetry_port_combo);
    copyCombo(state_->sky_telemetry_baud_combo_, state_->device_config_.sky_telemetry_baud_combo);
    copyCombo(state_->epsilon_port_combo_, state_->device_config_.epsilon_port_combo);
    copyCombo(state_->epsilon_baud_combo_, state_->device_config_.epsilon_baud_combo);
    copyCombo(state_->ptb_port_combo_, state_->device_config_.ptb_port_combo);
    copyCombo(state_->ptb_baud_combo_, state_->device_config_.ptb_baud_combo);
    copyCombo(state_->hmp_port_combo_, state_->device_config_.hmp_port_combo);
    copyCombo(state_->hmp_baud_combo_, state_->device_config_.hmp_baud_combo);
    copyCombo(state_->lidar_port_combo_, state_->device_config_.lidar_port_combo);
    copyCombo(state_->lidar_baud_combo_, state_->device_config_.lidar_baud_combo);
    copyCombo(state_->temperature_port_combo_, state_->device_config_.temperature_port_combo);
    copyCombo(state_->temperature_baud_combo_, state_->device_config_.temperature_baud_combo);
    copyCombo(state_->ptb_rate_combo_, state_->device_config_.ptb_rate_combo);
    copyCombo(state_->hmp_rate_combo_, state_->device_config_.hmp_rate_combo);
    copyCombo(state_->lidar_rate_combo_, state_->device_config_.lidar_rate_combo);
    copyCombo(state_->temperature_rate_combo_, state_->device_config_.temperature_rate_combo);

    if (state_->sky_telemetry_tcp_host_edit_ && state_->device_config_.sky_telemetry_tcp_host_edit)
    {
        const QSignalBlocker blocker(state_->device_config_.sky_telemetry_tcp_host_edit);
        state_->device_config_.sky_telemetry_tcp_host_edit->setText(state_->sky_telemetry_tcp_host_edit_->text());
    }
    if (state_->sky_telemetry_tcp_port_spin_ && state_->device_config_.sky_telemetry_tcp_port_spin)
    {
        const QSignalBlocker blocker(state_->device_config_.sky_telemetry_tcp_port_spin);
        state_->device_config_.sky_telemetry_tcp_port_spin->setValue(state_->sky_telemetry_tcp_port_spin_->value());
    }
    if (state_->device_config_.data_telemetry_summary_card)
    {
        state_->device_config_.data_telemetry_summary_card->setVisible(true);
    }
    updateRemoteTelemetrySummaryLabel();
    syncDeviceConfigEpsilonPanelFromSettings();

    updateDeviceConfigState();
}

void MainWindow::updateDeviceConfigTexts()
{
    if (!state_->device_config_.page)
    {
        return;
    }

    refreshLocalSerialPortManualOptionTexts();
    if (state_->device_config_.serial_title_lbl) state_->device_config_.serial_title_lbl->setText(state_->is_english_ ? "Serial Port Configuration" : "串口配置");
    if (state_->device_config_.data_source_mode_lbl) state_->device_config_.data_source_mode_lbl->setText(state_->is_english_ ? "Source:" : "数据源:");
    if (state_->device_config_.sky_telemetry_transport_lbl) state_->device_config_.sky_telemetry_transport_lbl->setText(state_->is_english_ ? "Link:" : "链路:");
    updateSkyTelemetryTransportComboTexts(state_->device_config_.sky_telemetry_transport_combo, state_->is_english_);
    if (state_->device_config_.sky_telemetry_tcp_host_lbl) state_->device_config_.sky_telemetry_tcp_host_lbl->setText(state_->is_english_ ? "Sky IP:" : "天空端IP:");
    if (state_->device_config_.sky_telemetry_tcp_port_lbl) state_->device_config_.sky_telemetry_tcp_port_lbl->setText(state_->is_english_ ? "Port:" : "端口:");
    if (state_->device_config_.sky_telemetry_port_lbl) state_->device_config_.sky_telemetry_port_lbl->setText(state_->is_english_ ? "Serial:" : "串口:");
    if (state_->device_config_.sky_telemetry_baud_lbl) state_->device_config_.sky_telemetry_baud_lbl->setText(state_->is_english_ ? "Baud:" : "波特率:");
    if (state_->device_config_.sky_device_config_btn) state_->device_config_.sky_device_config_btn->setText(state_->is_english_ ? "Sky Device Config" : "天空端设备配置");
    fitButtonFixedWidth(state_->device_config_.sky_device_config_btn,
                        kDeviceConfigSkyDeviceButtonMinWidth,
                        kDeviceConfigTopButtonPadding);
    if (state_->device_config_.epsilon_lbl) state_->device_config_.epsilon_lbl->setText(QStringLiteral("EPSILON:"));
    if (state_->device_config_.ptb_lbl) state_->device_config_.ptb_lbl->setText(state_->is_english_ ? QStringLiteral("Pressure:") : QStringLiteral("气压:"));
    if (state_->device_config_.hmp_lbl) state_->device_config_.hmp_lbl->setText(state_->is_english_ ? QStringLiteral("Temp/RH:") : QStringLiteral("温湿度:"));
    if (state_->device_config_.ptb_source_combo)
    {
        const QVariant sourceData = state_->device_config_.ptb_source_combo->currentData();
        const QSignalBlocker blocker(state_->device_config_.ptb_source_combo);
        state_->device_config_.ptb_source_combo->setItemText(0, QStringLiteral("PTB210"));
        state_->device_config_.ptb_source_combo->setItemText(1, QStringLiteral("BMP390"));
        state_->device_config_.ptb_source_combo->setCurrentIndex(std::max(0, state_->device_config_.ptb_source_combo->findData(sourceData)));
    }
    if (state_->device_config_.hmp_source_combo)
    {
        const QVariant sourceData = state_->device_config_.hmp_source_combo->currentData();
        const QSignalBlocker blocker(state_->device_config_.hmp_source_combo);
        state_->device_config_.hmp_source_combo->setItemText(0, QStringLiteral("HMP3"));
        state_->device_config_.hmp_source_combo->setItemText(1, QStringLiteral("SHT45"));
        state_->device_config_.hmp_source_combo->setCurrentIndex(std::max(0, state_->device_config_.hmp_source_combo->findData(sourceData)));
    }
    if (state_->device_config_.lidar_lbl) state_->device_config_.lidar_lbl->setText(QStringLiteral("TFA1500-L:"));
    if (state_->device_config_.temperature_lbl) state_->device_config_.temperature_lbl->setText(QStringLiteral("RD105:"));
    if (state_->device_config_.epsilon_rate_lbl) state_->device_config_.epsilon_rate_lbl->setText(QString());
    if (state_->device_config_.ptb_rate_lbl) state_->device_config_.ptb_rate_lbl->setText(state_->is_english_ ? "Rate:" : "频率:");
    if (state_->device_config_.hmp_rate_lbl) state_->device_config_.hmp_rate_lbl->setText(state_->is_english_ ? "Rate:" : "频率:");
    if (state_->device_config_.lidar_rate_lbl) state_->device_config_.lidar_rate_lbl->setText(state_->is_english_ ? "Rate:" : "频率:");
    if (state_->device_config_.temperature_rate_lbl) state_->device_config_.temperature_rate_lbl->setText(state_->is_english_ ? "Poll:" : "轮询:");
    if (state_->device_config_.epsilon_config_title_lbl)
    {
        state_->device_config_.epsilon_config_title_lbl->setText(state_->is_english_ ? "EPSILON Configuration" : "EPSILON 配置");
    }
    if (state_->device_config_.data_telemetry_summary_title_lbl)
    {
        state_->device_config_.data_telemetry_summary_title_lbl->setText(state_->is_english_
            ? "Sky-ground Communication Link Status"
            : "天地通信链路状态");
    }
    if (state_->device_config_.epsilon_config_hint_lbl)
    {
        state_->device_config_.epsilon_config_hint_lbl->setText(state_->is_english_
            ? "Packet rates are saved for future connect/reconfigure operations. Save applies the profile immediately when an EPSILON port is selected."
            : "包频率会用于后续连接和重配；已选择 EPSILON 串口时，保存后会立即应用。");
    }
    if (state_->device_config_.epsilon_packet_custom_check)
    {
        state_->device_config_.epsilon_packet_custom_check->setText(state_->is_english_
            ? "Use this custom EPSILON packet-rate profile"
            : "使用这组自定义 EPSILON 包频率");
    }
    for (int i = 0;
         i < state_->device_config_.epsilon_packet_rate_labels.size() &&
         i < static_cast<int>(epsilonPacketConfigOptions().size());
         ++i)
    {
        if (QLabel *label = state_->device_config_.epsilon_packet_rate_labels.at(i))
        {
            label->setText(epsilonPacketDialogRowLabel(epsilonPacketConfigOptions().at(i), state_->is_english_));
            label->setToolTip(label->text());
        }
    }
    for (QComboBox *combo : state_->device_config_.epsilon_packet_rate_combos)
    {
        if (!combo)
        {
            continue;
        }
        const QSignalBlocker blocker(combo);
        for (int i = 0; i < combo->count(); ++i)
        {
            combo->setItemText(i, epsilonPacketRateDisplayText(combo->itemData(i).toInt(), state_->is_english_));
        }
    }
    if (state_->device_config_.epsilon_packet_defaults_btn)
    {
        state_->device_config_.epsilon_packet_defaults_btn->setText(state_->is_english_ ? "Recommended" : "恢复推荐");
        state_->device_config_.epsilon_packet_defaults_btn->setToolTip(state_->is_english_ ? "Use the recommended default packet rates" : "恢复推荐默认包频率");
        fitButtonMinimumWidth(state_->device_config_.epsilon_packet_defaults_btn, 100);
    }
    if (state_->device_config_.epsilon_packet_grouped_btn)
    {
        state_->device_config_.epsilon_packet_grouped_btn->setText(state_->is_english_ ? "Grouped" : "分组模式");
        state_->device_config_.epsilon_packet_grouped_btn->setToolTip(state_->is_english_ ? "Use the grouped output-rate profile" : "切换到分组输出频率模式");
        fitButtonMinimumWidth(state_->device_config_.epsilon_packet_grouped_btn, 100);
    }
    if (state_->device_config_.epsilon_packet_save_btn)
    {
        state_->device_config_.epsilon_packet_save_btn->setText(state_->is_english_ ? "Save + Apply" : "保存并应用");
        state_->device_config_.epsilon_packet_save_btn->setToolTip(state_->is_english_ ? "Save the packet-rate profile and apply it now when possible" : "保存包频率配置，并在可用时立即应用");
        fitButtonMinimumWidth(state_->device_config_.epsilon_packet_save_btn, 118);
    }
    if (state_->device_config_.epsilon_rtcm_port_btn)
    {
        state_->device_config_.epsilon_rtcm_port_btn->setText(state_->is_english_ ? "RTCM Port" : "配置RTCM串口");
        state_->device_config_.epsilon_rtcm_port_btn->setToolTip(state_->is_english_ ? "Configure EPSILON communication port 2 as RTCM input" : "配置 EPSILON 第二通信串口为 RTCM 输入口");
        fitButtonMinimumWidth(state_->device_config_.epsilon_rtcm_port_btn, 128);
    }
    if (state_->device_config_.epsilon_reconfigure_btn)
    {
        state_->device_config_.epsilon_reconfigure_btn->setText(state_->is_english_ ? "Reconfigure Output" : "重新配置输出");
        state_->device_config_.epsilon_reconfigure_btn->setToolTip(state_->is_english_ ? "Apply the current EPSILON output profile" : "应用当前 EPSILON 输出配置");
        fitButtonMinimumWidth(state_->device_config_.epsilon_reconfigure_btn, 128);
    }
    if (state_->device_config_.rtk_config_btn)
    {
        state_->device_config_.rtk_config_btn->setText(state_->is_english_ ? "RTK Config" : "RTK配置");
        state_->device_config_.rtk_config_btn->setToolTip(state_->is_english_ ? "Open RTK config" : "打开 RTK 配置");
        fitButtonMinimumWidth(state_->device_config_.rtk_config_btn, 100);
    }

    for (VaporView::SkyDeviceId device : {VaporView::SkyDeviceId::Epsilon,
                                          VaporView::SkyDeviceId::Ptb,
                                          VaporView::SkyDeviceId::Hmp,
                                          VaporView::SkyDeviceId::Lidar,
                                          VaporView::SkyDeviceId::TemperatureController})
    {
        updateDeviceConfigRemoteActionButton(device);
    }

    updateDeviceConfigState();
}

void MainWindow::updateDeviceConfigState()
{
    if (!state_->device_config_.page)
    {
        return;
    }

    const bool remote = isRemoteSkyMode();
    const bool tcpTelemetry = isRemoteSkyTcpMode();
    const bool localInputsEnabled = !remote && !state_->is_connected_ &&
        !state_->connection_attempt_in_progress_ && !state_->port_detection_in_progress_ && !state_->epsilon_reconfigure_in_progress_;
    const bool remoteInputsEnabled = remote && !state_->is_connected_ && !state_->connection_attempt_in_progress_;
    const bool epsilonConfigEnabled = !remote && !state_->connection_attempt_in_progress_ &&
        !state_->port_detection_in_progress_ && !state_->epsilon_reconfigure_in_progress_;

    if (state_->device_config_.auto_detect_ports_btn)
    {
        state_->device_config_.auto_detect_ports_btn->setEnabled(state_->auto_detect_ports_btn_ && state_->auto_detect_ports_btn_->isEnabled());
        state_->device_config_.auto_detect_ports_btn->setText(state_->auto_detect_ports_btn_ ? state_->auto_detect_ports_btn_->text() : QString());
        state_->device_config_.auto_detect_ports_btn->setToolTip(state_->auto_detect_ports_btn_ ? state_->auto_detect_ports_btn_->toolTip() : QString());
        fitButtonFixedWidth(state_->device_config_.auto_detect_ports_btn,
                            kDeviceConfigAutoDetectButtonMinWidth,
                            kDeviceConfigTopButtonPadding);
    }
    if (state_->device_config_.sky_device_config_btn)
    {
        state_->device_config_.sky_device_config_btn->setEnabled(state_->sky_device_config_btn_ && state_->sky_device_config_btn_->isEnabled());
        state_->device_config_.sky_device_config_btn->setToolTip(state_->sky_device_config_btn_ ? state_->sky_device_config_btn_->toolTip() : QString());
    }
    if (state_->device_config_.epsilon_config_card)
    {
        state_->device_config_.epsilon_config_card->setVisible(true);
        state_->device_config_.epsilon_config_card->setEnabled(epsilonConfigEnabled);
    }

    const QList<QWidget *> localWidgets = {
        state_->device_config_.epsilon_port_combo,
        state_->device_config_.epsilon_baud_combo,
        state_->device_config_.ptb_port_combo,
        state_->device_config_.ptb_baud_combo,
        state_->device_config_.ptb_source_combo,
        state_->device_config_.hmp_port_combo,
        state_->device_config_.hmp_baud_combo,
        state_->device_config_.hmp_source_combo,
        state_->device_config_.lidar_port_combo,
        state_->device_config_.lidar_baud_combo,
        state_->device_config_.temperature_port_combo,
        state_->device_config_.temperature_baud_combo,
        state_->device_config_.ptb_rate_combo,
        state_->device_config_.hmp_rate_combo,
        state_->device_config_.lidar_rate_combo,
        state_->device_config_.temperature_rate_combo
    };
    for (QWidget *widget : localWidgets)
    {
        if (widget)
        {
            widget->setEnabled(localInputsEnabled);
        }
    }

    if (state_->device_config_.data_source_mode_combo) state_->device_config_.data_source_mode_combo->setEnabled(state_->data_source_mode_combo_ && state_->data_source_mode_combo_->isEnabled());
    if (state_->device_config_.sky_telemetry_transport_combo) state_->device_config_.sky_telemetry_transport_combo->setEnabled(remoteInputsEnabled);
    if (state_->device_config_.sky_telemetry_port_combo) state_->device_config_.sky_telemetry_port_combo->setEnabled(remoteInputsEnabled && !tcpTelemetry);
    if (state_->device_config_.sky_telemetry_baud_combo) state_->device_config_.sky_telemetry_baud_combo->setEnabled(remoteInputsEnabled && !tcpTelemetry);
    if (state_->device_config_.sky_telemetry_tcp_host_edit) state_->device_config_.sky_telemetry_tcp_host_edit->setEnabled(remoteInputsEnabled && tcpTelemetry);
    if (state_->device_config_.sky_telemetry_tcp_port_spin) state_->device_config_.sky_telemetry_tcp_port_spin->setEnabled(remoteInputsEnabled && tcpTelemetry);

    if (state_->device_config_.sky_telemetry_row_widget) state_->device_config_.sky_telemetry_row_widget->setVisible(true);
    if (state_->device_config_.sky_telemetry_transport_lbl) state_->device_config_.sky_telemetry_transport_lbl->setVisible(true);
    if (state_->device_config_.sky_telemetry_transport_combo) state_->device_config_.sky_telemetry_transport_combo->setVisible(true);
    if (state_->device_config_.sky_telemetry_port_lbl) state_->device_config_.sky_telemetry_port_lbl->setVisible(!tcpTelemetry);
    if (state_->device_config_.sky_telemetry_port_combo) state_->device_config_.sky_telemetry_port_combo->setVisible(!tcpTelemetry);
    if (state_->device_config_.sky_telemetry_baud_lbl) state_->device_config_.sky_telemetry_baud_lbl->setVisible(!tcpTelemetry);
    if (state_->device_config_.sky_telemetry_baud_combo) state_->device_config_.sky_telemetry_baud_combo->setVisible(!tcpTelemetry);
    if (state_->device_config_.sky_telemetry_tcp_host_lbl) state_->device_config_.sky_telemetry_tcp_host_lbl->setVisible(tcpTelemetry);
    if (state_->device_config_.sky_telemetry_tcp_host_edit) state_->device_config_.sky_telemetry_tcp_host_edit->setVisible(tcpTelemetry);
    if (state_->device_config_.sky_telemetry_tcp_port_lbl) state_->device_config_.sky_telemetry_tcp_port_lbl->setVisible(tcpTelemetry);
    if (state_->device_config_.sky_telemetry_tcp_port_spin) state_->device_config_.sky_telemetry_tcp_port_spin->setVisible(tcpTelemetry);

    for (QWidget *widget : {state_->device_config_.epsilon_remote_buttons_widget,
                            state_->device_config_.ptb_remote_buttons_widget,
                            state_->device_config_.hmp_remote_buttons_widget,
                            state_->device_config_.lidar_remote_buttons_widget,
                            state_->device_config_.temperature_remote_buttons_widget})
    {
        if (widget)
        {
            widget->setVisible(true);
        }
    }
    for (VaporView::SkyDeviceId device : {VaporView::SkyDeviceId::Epsilon,
                                          VaporView::SkyDeviceId::Ptb,
                                          VaporView::SkyDeviceId::Hmp,
                                          VaporView::SkyDeviceId::Lidar,
                                          VaporView::SkyDeviceId::TemperatureController})
    {
        updateDeviceConfigRemoteActionButton(device);
    }
}

QStringList MainWindow::getAvailablePorts()
{
    return VaporView::Ground::Devices::SerialPortDetectionService::availablePorts();
}

QString MainWindow::manualLocalSerialPortOptionText() const
{
    return state_->is_english_ ? QStringLiteral("Manual Add...") : QStringLiteral("手动添加...");
}

QString MainWindow::normalizedLocalSerialPortText(const QString& text) const
{
    QString trimmed = text.trimmed();
    const QString zhHistoryPrefix = QStringLiteral("历史：");
    const QString zhAsciiHistoryPrefix = QStringLiteral("历史:");
    const QString englishHistoryPrefix = QStringLiteral("History:");
    if (trimmed.startsWith(zhHistoryPrefix))
    {
        trimmed = trimmed.mid(zhHistoryPrefix.size()).trimmed();
    }
    else if (trimmed.startsWith(zhAsciiHistoryPrefix))
    {
        trimmed = trimmed.mid(zhAsciiHistoryPrefix.size()).trimmed();
    }
    else if (trimmed.startsWith(englishHistoryPrefix, Qt::CaseInsensitive))
    {
        trimmed = trimmed.mid(englishHistoryPrefix.size()).trimmed();
    }
    return trimmed;
}

bool MainWindow::isLocalSerialPortManualOptionText(const QString& text) const
{
    const QString trimmed = text.trimmed();
    return trimmed == QStringLiteral("Manual Add...") ||
           trimmed == QStringLiteral("手动添加...");
}

bool MainWindow::isLocalSerialPortManualOption(const QComboBox *combo, int index) const
{
    if (!combo || index < 0 || index >= combo->count())
    {
        return false;
    }
    return combo->itemData(index).toString() == QString::fromLatin1(kLocalSerialPortManualOptionData) ||
           isLocalSerialPortManualOptionText(combo->itemText(index));
}

QString MainWindow::localSerialPortItemValue(const QComboBox *combo, int index) const
{
    if (!combo || index < 0 || index >= combo->count())
    {
        return QString();
    }

    const QString itemDataText = combo->itemData(index).toString().trimmed();
    if (!itemDataText.isEmpty() &&
        itemDataText != QString::fromLatin1(kLocalSerialPortManualOptionData))
    {
        return itemDataText;
    }

    const QString text = normalizedLocalSerialPortText(combo->itemText(index));
    if (text.isEmpty() ||
        text.startsWith(QStringLiteral("--")) ||
        isLocalSerialPortManualOptionText(text))
    {
        return QString();
    }
    return text;
}

QString MainWindow::localSerialPortComboValue(const QComboBox *combo) const
{
    if (!combo || combo->property(kLocalSerialPortManualEntryProperty).toBool())
    {
        return QString();
    }
    return localSerialPortItemValue(combo, combo->currentIndex());
}

void MainWindow::installLocalSerialPortComboBehavior(QComboBox *combo)
{
    if (!combo)
    {
        return;
    }

    combo->setProperty(kLocalSerialPortComboProperty, true);
    combo->setInsertPolicy(QComboBox::NoInsert);
    combo->setEditable(false);

    VaporView::installSerialPortPopupDelegate(combo);

    if (combo->property(kLocalSerialPortManualHandlerProperty).toBool())
    {
        return;
    }

    combo->setProperty(kLocalSerialPortManualHandlerProperty, true);
    const auto beginManualEntryIfSelected = [this, combo](int index) {
        if (isLocalSerialPortManualOption(combo, index))
        {
            beginManualLocalSerialPortEntry(combo);
            return;
        }
        combo->setProperty(
            kLocalSerialPortManualPreviousTextProperty,
            localSerialPortItemValue(combo, index));
    };
    connect(combo,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this,
            beginManualEntryIfSelected);
    connect(combo,
            QOverload<int>::of(&QComboBox::activated),
            this,
            beginManualEntryIfSelected);
}

void MainWindow::refreshLocalSerialPortComboOptions(QComboBox *combo,
                                                    const QStringList& ports,
                                                    const QString& preferredText)
{
    if (!combo)
    {
        return;
    }

    const QString preferredValue = normalizedLocalSerialPortText(preferredText);
    const QString previousText = preferredValue.isEmpty()
        ? localSerialPortComboValue(combo)
        : preferredValue;
    const bool previousIsRealPort =
        !previousText.isEmpty() &&
        !previousText.startsWith(QStringLiteral("--")) &&
        !isLocalSerialPortManualOptionText(previousText);
    const bool previousIsAvailable = VaporView::serialPortListContains(ports, previousText);
    const bool previousIsHistory = VaporView::isRememberedSerialPort(previousText);
    const bool shouldKeepPrevious =
        previousIsRealPort && (previousIsAvailable || previousIsHistory);

    installLocalSerialPortComboBehavior(combo);
    const QSignalBlocker blocker(combo);
    combo->setEditable(false);
    combo->clear();
    combo->addItem(state_->is_english_ ? QStringLiteral("-- Select --") : QStringLiteral("-- 选择 --"));
    for (const QString& port : ports)
    {
        const QString trimmed = port.trimmed();
        if (!trimmed.isEmpty() && combo->findText(trimmed) < 0)
        {
            combo->addItem(trimmed, trimmed);
        }
    }
    int selectedIndex = 0;
    if (shouldKeepPrevious)
    {
        for (int index = 1; index < combo->count(); ++index)
        {
            if (VaporView::serialPortNamesMatch(combo->itemData(index).toString(), previousText))
            {
                selectedIndex = index;
                break;
            }
        }
    }
    if (shouldKeepPrevious && selectedIndex == 0)
    {
        const int historyIndex = combo->count();
        combo->addItem(previousText, previousText);
        combo->setItemData(historyIndex, true, kLocalSerialPortHistoryItemRole);
        selectedIndex = historyIndex;
    }
    combo->addItem(manualLocalSerialPortOptionText(), QString::fromLatin1(kLocalSerialPortManualOptionData));

    combo->setCurrentIndex(selectedIndex >= 0 ? selectedIndex : 0);
    combo->setProperty(
        kLocalSerialPortManualPreviousTextProperty,
        shouldKeepPrevious ? previousText : QString());
}

void MainWindow::setLocalSerialPortComboText(QComboBox *combo, const QString& text)
{
    if (!combo)
    {
        return;
    }

    installLocalSerialPortComboBehavior(combo);
    combo->setEditable(false);

    const QString trimmed = normalizedLocalSerialPortText(text);
    if (trimmed.isEmpty() ||
        trimmed.startsWith(QStringLiteral("--")) ||
        isLocalSerialPortManualOptionText(trimmed))
    {
        combo->setCurrentIndex(0);
        return;
    }

    int index = combo->findData(trimmed);
    if (index < 0)
    {
        index = combo->findText(trimmed);
    }
    if (index < 0)
    {
        for (int candidateIndex = 1; candidateIndex < combo->count(); ++candidateIndex)
        {
            if (VaporView::serialPortNamesMatch(
                    localSerialPortItemValue(combo, candidateIndex),
                    trimmed))
            {
                index = candidateIndex;
                break;
            }
        }
    }
    if (index < 0)
    {
        const bool isAvailable = VaporView::serialPortListContains(getAvailablePorts(), trimmed);
        const bool isHistory = VaporView::isRememberedSerialPort(trimmed);
        if (isAvailable || isHistory)
        {
            const int manualIndex = combo->findData(QString::fromLatin1(kLocalSerialPortManualOptionData));
            index = manualIndex >= 0 ? manualIndex : combo->count();
            combo->insertItem(index, trimmed, trimmed);
            if (!isAvailable && isHistory)
            {
                combo->setItemData(index, true, kLocalSerialPortHistoryItemRole);
            }
        }
    }
    const int selectedIndex = index >= 0 ? index : 0;
    combo->setCurrentIndex(selectedIndex);
    combo->setProperty(
        kLocalSerialPortManualPreviousTextProperty,
        localSerialPortItemValue(combo, selectedIndex));
}

void MainWindow::beginManualLocalSerialPortEntry(QComboBox *combo)
{
    if (!combo || !combo->property(kLocalSerialPortComboProperty).toBool() ||
        combo->property(kLocalSerialPortManualEntryProperty).toBool())
    {
        return;
    }

    QString previousText = combo->property(kLocalSerialPortManualPreviousTextProperty).toString();
    if (previousText.isEmpty())
    {
        previousText = combo->currentText().trimmed();
    }
    if (isLocalSerialPortManualOptionText(previousText))
    {
        previousText.clear();
    }
    combo->setProperty(kLocalSerialPortManualPreviousTextProperty, previousText);
    combo->setProperty(kLocalSerialPortManualEntryProperty, true);
    combo->setEditable(true);
    combo->setInsertPolicy(QComboBox::NoInsert);
    if (QLineEdit *edit = combo->lineEdit())
    {
        edit->setProperty(kLocalSerialPortManualEntryProperty, true);
        edit->setPlaceholderText(state_->is_english_
            ? QStringLiteral("Enter serial port...")
            : QStringLiteral("输入串口名..."));
        edit->installEventFilter(this);
        edit->clear();
        edit->setFocus(Qt::OtherFocusReason);
    }
}

void MainWindow::finishManualLocalSerialPortEntry(QComboBox *combo, bool accept)
{
    if (!combo || !combo->property(kLocalSerialPortManualEntryProperty).toBool())
    {
        return;
    }

    const QString previousText = combo->property(kLocalSerialPortManualPreviousTextProperty).toString();
    const QString enteredText = combo->lineEdit() ? combo->lineEdit()->text().trimmed() : combo->currentText().trimmed();
    combo->setProperty(kLocalSerialPortManualEntryProperty, false);
    combo->setProperty(kLocalSerialPortManualPreviousTextProperty, QString());
    combo->setEditable(false);

    const bool validManualText =
        accept &&
        !enteredText.isEmpty() &&
        !enteredText.startsWith(QStringLiteral("--")) &&
        !isLocalSerialPortManualOptionText(enteredText);
    if (validManualText)
    {
        VaporView::rememberSerialPort(enteredText);
    }
    setLocalSerialPortComboText(combo, validManualText ? enteredText : previousText);

    saveRememberedInputState();
    updateHomeDeviceStatusCapsules();
    updateDeviceConfigState();
    updateTemperatureControllerTitleText();
    updateTemperatureTitleButtonsState();
    combo->setEditable(false);
    combo->setProperty(kLocalSerialPortManualEntryProperty, false);
    if (QLineEdit *edit = combo->lineEdit())
    {
        edit->setProperty(kLocalSerialPortManualEntryProperty, false);
        edit->removeEventFilter(this);
    }
}

bool MainWindow::handleLocalSerialPortManualEntryEvent(QObject *watched, QEvent *event)
{
    auto *edit = qobject_cast<QLineEdit *>(watched);
    if (!edit || !edit->property(kLocalSerialPortManualEntryProperty).toBool())
    {
        return false;
    }

    auto *combo = qobject_cast<QComboBox *>(edit->parentWidget());
    if (!combo || !combo->property(kLocalSerialPortManualEntryProperty).toBool())
    {
        return false;
    }

    if (event->type() == QEvent::KeyPress)
    {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter)
        {
            finishManualLocalSerialPortEntry(combo, true);
            return true;
        }
        if (keyEvent->key() == Qt::Key_Escape)
        {
            finishManualLocalSerialPortEntry(combo, false);
            return true;
        }
    }
    else if (event->type() == QEvent::FocusOut)
    {
        QTimer::singleShot(0, this, [this, combo]() {
            finishManualLocalSerialPortEntry(combo, true);
        });
    }
    return false;
}

void MainWindow::refreshLocalSerialPortManualOptionTexts()
{
    for (QComboBox *combo : {state_->epsilon_port_combo_,
                             state_->ptb_port_combo_,
                             state_->hmp_port_combo_,
                             state_->lidar_port_combo_,
                             state_->temperature_port_combo_,
                             state_->device_config_.epsilon_port_combo,
                             state_->device_config_.ptb_port_combo,
                             state_->device_config_.hmp_port_combo,
                             state_->device_config_.lidar_port_combo,
                             state_->device_config_.temperature_port_combo,
                             state_->sky_telemetry_port_combo_,
                             state_->device_config_.sky_telemetry_port_combo})
    {
        if (!combo || !combo->property(kLocalSerialPortComboProperty).toBool())
        {
            continue;
        }
        const QSignalBlocker blocker(combo);
        int selectIndex = combo->findText(QStringLiteral("-- Select --"));
        if (selectIndex < 0)
        {
            selectIndex = combo->findText(QStringLiteral("-- 选择 --"));
        }
        if (selectIndex >= 0)
        {
            combo->setItemText(selectIndex, state_->is_english_ ? QStringLiteral("-- Select --") : QStringLiteral("-- 选择 --"));
        }
        const int manualIndex = combo->findData(QString::fromLatin1(kLocalSerialPortManualOptionData));
        if (manualIndex >= 0)
        {
            combo->setItemText(manualIndex, manualLocalSerialPortOptionText());
        }
        else
        {
            combo->addItem(manualLocalSerialPortOptionText(), QString::fromLatin1(kLocalSerialPortManualOptionData));
        }
    }
}

void MainWindow::setupConfigPanel()
{
    state_->config_group_ = new QGroupBox(this);
    state_->config_group_->setObjectName("sensorGroupBox");
    state_->config_group_->setMinimumWidth(kHomeOverviewDeviceMinWidth);
    state_->config_group_->setMinimumHeight(kConfigCardMinHeight);
    state_->config_group_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    auto *config_root_layout = new QVBoxLayout(state_->config_group_);
    config_root_layout->setSpacing(0);
    config_root_layout->setContentsMargins(kHomeOverviewCardOuterPadding,
                                           0,
                                           kHomeOverviewCardOuterPadding,
                                           kConfigCardBottomPadding);

    auto *config_form_widget = new QWidget(state_->config_group_);
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
            combo->addItem(state_->is_english_ ? QStringLiteral("No Set") : QStringLiteral("不设定"));
        }
        combo->setValidator(nullptr);
    };

    auto createPortRow = [this, config_form_widget, config_layout, &baudRates, &ports, &createRateCombo](QLabel*& lbl, QComboBox*& portCombo, QComboBox*& baudCombo, QLabel*& rateLbl, QComboBox*& rateCombo, const QString& defaultBaud, int row, int maxRate = 500) {
        lbl = new QLabel(config_form_widget);
        lbl->setObjectName("fieldLabel");
        lbl->setFixedHeight(kMainPageInputHeight);
        lbl->setFixedWidth(80);
        config_layout->addWidget(lbl, row, 0, Qt::AlignVCenter | Qt::AlignLeft);

        portCombo = new QComboBox(config_form_widget);
        portCombo->setMinimumContentsLength(10);
        portCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        portCombo->setFixedHeight(kMainPageInputHeight);
        portCombo->setMinimumWidth(160);
        portCombo->setMaximumWidth(190);
        portCombo->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        portCombo->setMaxVisibleItems(15);
        configureComboPopup(portCombo);
        refreshLocalSerialPortComboOptions(portCombo, ports);
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

    auto *configTitleBar = new QWidget(state_->config_group_);
    configTitleBar->setObjectName("sectionTitleBar");
    configTitleBar->setFixedHeight(kMainPageTitleBarHeight);
    auto *configTitleLayout = new QHBoxLayout(configTitleBar);
    configTitleLayout->setContentsMargins(8, 2, 8, 2);
    configTitleLayout->setSpacing(8);

    QWidget *configTitleCluster = nullptr;
    state_->config_inline_title_lbl_ = createSectionTitleCluster(configTitleBar,
                                                         QStringLiteral("usb"),
                                                         kMainPageButtonHeight,
                                                         &configTitleCluster);
    configTitleLayout->addWidget(configTitleCluster, 0, Qt::AlignVCenter | Qt::AlignLeft);

    configTitleLayout->addStretch(1);

    state_->source_mode_switch_ = createSourceModeOverviewSwitchButton(configTitleBar);
    state_->source_mode_switch_->setFixedSize(128, kMainPageButtonHeight);
    state_->source_mode_switch_->setEnglish(state_->is_english_);
    connect(state_->source_mode_switch_, &QPushButton::clicked, this, [this]() {
        if (!state_->data_source_mode_combo_)
        {
            return;
        }
        state_->data_source_mode_combo_->setCurrentIndex(state_->source_mode_switch_->switchChecked() ? 0 : 1);
    });
    configTitleLayout->addWidget(state_->source_mode_switch_, 0, Qt::AlignVCenter | Qt::AlignRight);

    state_->auto_detect_ports_btn_ = new QPushButton(config_form_widget);
    state_->auto_detect_ports_btn_->setFixedHeight(kMainPageButtonHeight);
    state_->auto_detect_ports_btn_->setMinimumWidth(120);
    connect(state_->auto_detect_ports_btn_, &QPushButton::clicked, this, &MainWindow::onAutoDetectPortsClicked);

    state_->data_source_mode_lbl_ = new QLabel(config_form_widget);
    state_->data_source_mode_lbl_->setObjectName("fieldLabel");
    state_->data_source_mode_combo_ = createSingleLevelPopupComboBox(config_form_widget);
    state_->data_source_mode_combo_->addItem(sourceModeDisplayText(false, 0));
    state_->data_source_mode_combo_->addItem(sourceModeDisplayText(false, 1));
    state_->data_source_mode_combo_->setFixedHeight(kMainPageInputHeight);
    state_->data_source_mode_combo_->setMinimumWidth(180);
    state_->data_source_mode_combo_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    connect(state_->data_source_mode_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onDataSourceModeChanged);

    state_->sky_device_config_btn_ = new QPushButton(config_form_widget);
    state_->sky_device_config_btn_->setFixedHeight(kMainPageButtonHeight);
    state_->sky_device_config_btn_->setMinimumWidth(150);
    connect(state_->sky_device_config_btn_, &QPushButton::clicked, this, &MainWindow::onSkyDeviceConfigClicked);
    config_root_layout->addWidget(configTitleBar);

    state_->sky_telemetry_row_widget_ = new QWidget(config_form_widget);
    state_->sky_telemetry_transport_lbl_ = new QLabel(state_->sky_telemetry_row_widget_);
    state_->sky_telemetry_transport_lbl_->setObjectName("fieldLabel");
    state_->sky_telemetry_transport_combo_ = new QComboBox(state_->sky_telemetry_row_widget_);
    state_->sky_telemetry_transport_combo_->addItem(skyTelemetryTransportDisplayText(false, QStringLiteral("tcp")), QStringLiteral("tcp"));
    state_->sky_telemetry_transport_combo_->addItem(skyTelemetryTransportDisplayText(false, QStringLiteral("serial")), QStringLiteral("serial"));
    state_->sky_telemetry_transport_combo_->setFixedHeight(kMainPageInputHeight);
    state_->sky_telemetry_transport_combo_->setFixedWidth(110);
    connect(state_->sky_telemetry_transport_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) {
                updateSourceModeUi();
                saveRememberedInputState();
            });

    state_->sky_telemetry_tcp_host_lbl_ = new QLabel(state_->sky_telemetry_row_widget_);
    state_->sky_telemetry_tcp_host_lbl_->setObjectName("fieldLabel");
    state_->sky_telemetry_tcp_host_edit_ = new QLineEdit(state_->sky_telemetry_row_widget_);
    state_->sky_telemetry_tcp_host_edit_->setText(QStringLiteral("192.168.1.2"));
    state_->sky_telemetry_tcp_host_edit_->setFixedHeight(kMainPageInputHeight);
    state_->sky_telemetry_tcp_host_edit_->setMinimumWidth(150);
    state_->sky_telemetry_tcp_host_edit_->setMaximumWidth(180);

    state_->sky_telemetry_tcp_port_lbl_ = new QLabel(state_->sky_telemetry_row_widget_);
    state_->sky_telemetry_tcp_port_lbl_->setObjectName("fieldLabel");
    state_->sky_telemetry_tcp_port_spin_ = new QSpinBox(state_->sky_telemetry_row_widget_);
    state_->sky_telemetry_tcp_port_spin_->setRange(1, 65535);
    state_->sky_telemetry_tcp_port_spin_->setValue(39100);
    state_->sky_telemetry_tcp_port_spin_->setFixedHeight(kMainPageInputHeight);
    state_->sky_telemetry_tcp_port_spin_->setFixedWidth(100);

    state_->sky_telemetry_port_lbl_ = new QLabel(state_->sky_telemetry_row_widget_);
    state_->sky_telemetry_port_lbl_->setObjectName("fieldLabel");
    state_->sky_telemetry_port_combo_ = new QComboBox(state_->sky_telemetry_row_widget_);
    state_->sky_telemetry_port_combo_->setObjectName(QStringLiteral("skyTelemetryPortCombo"));
    state_->sky_telemetry_port_combo_->setFixedHeight(kMainPageInputHeight);
    state_->sky_telemetry_port_combo_->setMinimumWidth(160);
    refreshLocalSerialPortComboOptions(state_->sky_telemetry_port_combo_, ports);
    configureComboPopup(state_->sky_telemetry_port_combo_);
    state_->sky_telemetry_baud_lbl_ = new QLabel(state_->sky_telemetry_row_widget_);
    state_->sky_telemetry_baud_lbl_->setObjectName("fieldLabel");
    state_->sky_telemetry_baud_combo_ = new QComboBox(state_->sky_telemetry_row_widget_);
    state_->sky_telemetry_baud_combo_->addItems(baudRates);
    state_->sky_telemetry_baud_combo_->setCurrentText(QStringLiteral("921600"));
    state_->sky_telemetry_baud_combo_->setFixedHeight(kMainPageInputHeight);
    state_->sky_telemetry_baud_combo_->setFixedWidth(100);
    auto *skyTelemetryLayout = new QHBoxLayout(state_->sky_telemetry_row_widget_);
    skyTelemetryLayout->setContentsMargins(8, 2, 8, 2);
    skyTelemetryLayout->setSpacing(8);
    skyTelemetryLayout->addWidget(state_->sky_telemetry_transport_lbl_, 0, Qt::AlignVCenter | Qt::AlignLeft);
    skyTelemetryLayout->addWidget(state_->sky_telemetry_transport_combo_, 0, Qt::AlignVCenter);
    skyTelemetryLayout->addWidget(state_->sky_telemetry_tcp_host_lbl_, 0, Qt::AlignVCenter | Qt::AlignLeft);
    skyTelemetryLayout->addWidget(state_->sky_telemetry_tcp_host_edit_, 0, Qt::AlignVCenter);
    skyTelemetryLayout->addWidget(state_->sky_telemetry_tcp_port_lbl_, 0, Qt::AlignVCenter | Qt::AlignLeft);
    skyTelemetryLayout->addWidget(state_->sky_telemetry_tcp_port_spin_, 0, Qt::AlignVCenter);
    skyTelemetryLayout->addWidget(state_->sky_telemetry_port_lbl_, 0, Qt::AlignVCenter | Qt::AlignLeft);
    skyTelemetryLayout->addWidget(state_->sky_telemetry_port_combo_, 0, Qt::AlignVCenter);
    skyTelemetryLayout->addWidget(state_->sky_telemetry_baud_lbl_, 0, Qt::AlignVCenter | Qt::AlignLeft);
    skyTelemetryLayout->addWidget(state_->sky_telemetry_baud_combo_, 0, Qt::AlignVCenter);
    skyTelemetryLayout->addStretch(1);
    state_->sky_telemetry_row_widget_->setVisible(true);

    int row = 0;

    createPortRow(state_->epsilon_lbl_, state_->epsilon_port_combo_, state_->epsilon_baud_combo_, state_->epsilon_rate_lbl_, state_->epsilon_rate_combo_, "921600", row++, 200);
    createPortRow(state_->ptb_lbl_, state_->ptb_port_combo_, state_->ptb_baud_combo_, state_->ptb_rate_lbl_, state_->ptb_rate_combo_, "9600", row++, kPtbMaxSampleRateHz);
    createPortRow(state_->hmp_lbl_, state_->hmp_port_combo_, state_->hmp_baud_combo_, state_->hmp_rate_lbl_, state_->hmp_rate_combo_, "19200", row++);
    createPortRow(state_->lidar_lbl_, state_->lidar_port_combo_, state_->lidar_baud_combo_, state_->lidar_rate_lbl_, state_->lidar_rate_combo_, "500000", row++, 100);
    createPortRow(state_->temperature_lbl_, state_->temperature_port_combo_, state_->temperature_baud_combo_, state_->temperature_rate_lbl_, state_->temperature_rate_combo_, "38400", row++, kMaxTemperatureSampleRateHz);
    if (state_->epsilon_port_combo_) state_->epsilon_port_combo_->setObjectName(QStringLiteral("epsilonPortCombo"));
    if (state_->ptb_port_combo_) state_->ptb_port_combo_->setObjectName(QStringLiteral("pressurePortCombo"));
    if (state_->hmp_port_combo_) state_->hmp_port_combo_->setObjectName(QStringLiteral("humidityPortCombo"));
    if (state_->lidar_port_combo_) state_->lidar_port_combo_->setObjectName(QStringLiteral("lidarPortCombo"));
    if (state_->temperature_port_combo_) state_->temperature_port_combo_->setObjectName(QStringLiteral("temperaturePortCombo"));
    if (state_->temperature_baud_combo_) state_->temperature_baud_combo_->setObjectName(QStringLiteral("temperatureBaudCombo"));
    if (state_->temperature_rate_combo_) state_->temperature_rate_combo_->setObjectName(QStringLiteral("temperatureRateCombo"));
    if (state_->temperature_rate_combo_)
    {
        state_->temperature_rate_combo_->setCurrentText(QString::number(kDefaultTemperatureSampleRateHz));
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
    addRemoteButtons(0, state_->epsilon_remote_buttons_widget_, state_->epsilon_remote_connect_btn_, state_->epsilon_remote_disconnect_btn_, state_->epsilon_remote_reconnect_btn_, VaporView::SkyDeviceId::Epsilon);
    addRemoteButtons(1, state_->ptb_remote_buttons_widget_, state_->ptb_remote_connect_btn_, state_->ptb_remote_disconnect_btn_, state_->ptb_remote_reconnect_btn_, VaporView::SkyDeviceId::Ptb);
    addRemoteButtons(2, state_->hmp_remote_buttons_widget_, state_->hmp_remote_connect_btn_, state_->hmp_remote_disconnect_btn_, state_->hmp_remote_reconnect_btn_, VaporView::SkyDeviceId::Hmp);
    addRemoteButtons(3, state_->lidar_remote_buttons_widget_, state_->lidar_remote_connect_btn_, state_->lidar_remote_disconnect_btn_, state_->lidar_remote_reconnect_btn_, VaporView::SkyDeviceId::Lidar);
    addRemoteButtons(4, state_->temperature_remote_buttons_widget_, state_->temperature_remote_connect_btn_, state_->temperature_remote_disconnect_btn_, state_->temperature_remote_reconnect_btn_, VaporView::SkyDeviceId::TemperatureController);

    if (state_->epsilon_rate_combo_)
    {
        config_layout->removeWidget(state_->epsilon_rate_combo_);
        delete state_->epsilon_rate_combo_;
        state_->epsilon_rate_combo_ = nullptr;
        state_->epsilon_packet_rates_btn_ = new QPushButton(config_form_widget);
        state_->epsilon_packet_rates_btn_->setFixedHeight(kMainPageButtonHeight);
        state_->epsilon_packet_rates_btn_->setMinimumWidth(140);
        connect(state_->epsilon_packet_rates_btn_, &QPushButton::clicked, this, &MainWindow::onConfigureEpsilonPacketRatesClicked);
        config_layout->addWidget(state_->epsilon_packet_rates_btn_, 0, 4, Qt::AlignVCenter);
    }

    for (QComboBox *combo : {state_->ptb_rate_combo_, state_->hmp_rate_combo_, state_->lidar_rate_combo_, state_->temperature_rate_combo_})
    {
        addNoSetRateOption(combo);
    }

    connect(state_->ptb_rate_combo_, &QComboBox::currentTextChanged, this, &MainWindow::onPtbRateChanged);
    connect(state_->hmp_rate_combo_, &QComboBox::currentTextChanged, this, &MainWindow::onHmpRateChanged);
    connect(state_->lidar_rate_combo_, &QComboBox::currentTextChanged, this, &MainWindow::onLidarRateChanged);
    connect(state_->temperature_rate_combo_, &QComboBox::currentTextChanged, this, &MainWindow::onTemperatureRateChanged);

    state_->data_telemetry_summary_card_ = new QWidget(state_->config_group_);
    state_->data_telemetry_summary_card_->setObjectName(QStringLiteral("homeTelemetrySummaryContainer"));
    state_->data_telemetry_summary_card_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    state_->data_telemetry_summary_card_->setToolTip(QString());
    auto *telemetrySummaryLayout = new QVBoxLayout(state_->data_telemetry_summary_card_);
    telemetrySummaryLayout->setContentsMargins(0, 0, 0, 0);
    telemetrySummaryLayout->setSpacing(2);

    auto createTelemetrySection = [this, telemetrySummaryLayout](QVBoxLayout *&sectionContentLayout) {
        auto *section = new QFrame(state_->data_telemetry_summary_card_);
        section->setObjectName(QStringLiteral("homeTelemetrySectionCard"));
        section->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        section->setToolTip(QString());
        sectionContentLayout = new QVBoxLayout(section);
        sectionContentLayout->setContentsMargins(6, 2, 6, 2);
        sectionContentLayout->setSpacing(2);
        telemetrySummaryLayout->addWidget(section, 0);
    };
    createTelemetrySection(state_->data_telemetry_summary_layout_);
    createTelemetrySection(state_->data_telemetry_link_summary_layout_);
    createTelemetrySection(state_->data_telemetry_device_summary_layout_);
    state_->data_telemetry_summary_card_->setVisible(true);

    auto *homeBodyWidget = new QWidget(state_->config_group_);
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
        button->setFocusPolicy(Qt::TabFocus);
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

    addHomeDevice(state_->home_epsilon_status_lbl_, state_->home_epsilon_action_btn_, VaporView::SkyDeviceId::Epsilon);
    addHomeDevice(state_->home_ptb_status_lbl_, state_->home_ptb_action_btn_, VaporView::SkyDeviceId::Ptb);
    addHomeDevice(state_->home_hmp_status_lbl_, state_->home_hmp_action_btn_, VaporView::SkyDeviceId::Hmp);
    addHomeDevice(state_->home_lidar_status_lbl_, state_->home_lidar_action_btn_, VaporView::SkyDeviceId::Lidar);
    addHomeDevice(state_->home_temperature_status_lbl_, state_->home_temperature_action_btn_, VaporView::SkyDeviceId::TemperatureController);
    addHomeDevice(state_->home_wave_status_lbl_, state_->home_wave_action_btn_, VaporView::SkyDeviceId::WaveTcp);
    state_->home_device_action_spinner_timer_ = new QTimer(this);
    state_->home_device_action_spinner_timer_->setTimerType(Qt::PreciseTimer);
    state_->home_device_action_spinner_timer_->setInterval(kHomeDeviceActionSpinnerIntervalMs);
    connect(state_->home_device_action_spinner_timer_, &QTimer::timeout, this, [this]() {
        state_->home_device_action_spinner_step_ = (state_->home_device_action_spinner_step_ + 1) % kHomeDeviceActionSpinnerFrames;
        updateHomeDeviceActionSpinnerIcons();
    });
    homeDevicesLayout->setColumnStretch(kHomeDeviceGridColumns, 1);
    updateHomeDeviceStatusCapsules();
    homeDevicesLayout->activate();
    const QMargins homeBodyMargins = homeBodyLayout->contentsMargins();
    const int deviceOverviewDefaultWidth = homeDevicesWidget->sizeHint().width() +
                                           homeBodyMargins.left() +
                                           homeBodyMargins.right() + 2;
    state_->config_group_->setMinimumWidth(std::max(kHomeOverviewDeviceMinWidth, deviceOverviewDefaultWidth));

    homeBodyLayout->addWidget(homeDevicesWidget, 0, Qt::AlignTop | Qt::AlignLeft);
    homeBodyLayout->addWidget(state_->data_telemetry_summary_card_, 0, Qt::AlignTop);
    config_root_layout->addWidget(homeBodyWidget, 0, Qt::AlignTop);
    config_form_widget->setVisible(false);
    updateHomeDeviceStatusCapsules();
    updateRemoteTelemetrySummaryLabel();
}

void MainWindow::setupDataPanels()
{
    state_->data_group_ = new QGroupBox(this);
    state_->data_group_->setObjectName("sensorRowContainer");
    state_->data_group_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto *data_layout = new QVBoxLayout(state_->data_group_);
    data_layout->setSpacing(0);
    data_layout->setContentsMargins(0, 0, 0, 0);

    state_->sensor_row_widget_ = new QWidget(state_->data_group_);
    state_->sensor_layout_ = new QHBoxLayout(state_->sensor_row_widget_);
    state_->sensor_layout_->setContentsMargins(0, 0, 0, 0);
    state_->sensor_layout_->setSpacing(2);

    state_->epsilon_group_ = new QGroupBox(this);
    state_->epsilon_group_->setObjectName("sensorGroupBox");
    state_->epsilon_group_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto *epsilon_layout = new QVBoxLayout(state_->epsilon_group_);
    epsilon_layout->setContentsMargins(1, 0, 1, 1);
    epsilon_layout->setSpacing(0);

    auto *epsilonTitleBar = new QWidget(state_->epsilon_group_);
    epsilonTitleBar->setObjectName("sectionTitleBar");
    epsilonTitleBar->setFixedHeight(kMainPageTitleBarHeight);
    auto *epsilonTitleLayout = new QHBoxLayout(epsilonTitleBar);
    epsilonTitleLayout->setContentsMargins(8, 2, 8, 2);
    epsilonTitleLayout->setSpacing(8);

    QWidget *epsilonTitleCluster = nullptr;
    state_->epsilon_inline_title_lbl_ = createSectionTitleCluster(epsilonTitleBar,
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
    state_->epsilon_panel_ = new EpsilonPanel(epsilonRateTitleLabel, this);
    epsilon_layout->addWidget(state_->epsilon_panel_);
    state_->sensor_layout_->addWidget(state_->epsilon_group_, kSensorNavigationStretch);

    state_->gnss_group_ = nullptr;
    state_->imu_group_ = nullptr;
    state_->gnss_panel_ = nullptr;
    state_->imu_panel_ = nullptr;
    state_->gnss_inline_title_lbl_ = nullptr;
    state_->imu_inline_title_lbl_ = nullptr;

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
    state_->env_inline_title_lbl_ = createSectionTitleCluster(envTitleBar,
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
    state_->env_lidar_status_icon_ = createStatusIcon();
    state_->env_ptb_status_icon_ = createStatusIcon();
    state_->env_hmp_status_icon_ = createStatusIcon();
    envTitleLayout->addWidget(state_->env_lidar_status_icon_, 0, Qt::AlignVCenter);
    envTitleLayout->addWidget(state_->env_ptb_status_icon_, 0, Qt::AlignVCenter);
    envTitleLayout->addWidget(state_->env_hmp_status_icon_, 0, Qt::AlignVCenter);
    env_layout->addWidget(envTitleBar);

    state_->lidar_panel_ = new LidarPanel(this);
    env_layout->addWidget(state_->lidar_panel_);

    state_->ptb_panel_ = new PtbPanel(this);
    env_layout->addWidget(state_->ptb_panel_);

    state_->hmp_panel_ = new HmpPanel(this);
    env_layout->addWidget(state_->hmp_panel_);
    updateEnvironmentStatusIcons(false, false, false);

    const int sensorCardHeight = std::max({
        state_->epsilon_group_->sizeHint().height(),
        state_->epsilon_group_->minimumSizeHint().height()
    });
    state_->epsilon_group_->setFixedHeight(sensorCardHeight);
    env_group->setFixedHeight(sensorCardHeight);
    state_->sensor_row_widget_->setMinimumHeight(sensorCardHeight);

    state_->sensor_layout_->addWidget(env_group, kSensorEnvironmentStretch);

    data_layout->addWidget(state_->sensor_row_widget_, 0);
    data_layout->addStretch(1);
    const int dataCardMinHeight = state_->data_group_->minimumSizeHint().height();
    state_->data_group_->setMinimumHeight(dataCardMinHeight);
    state_->data_group_->setFixedHeight(dataCardMinHeight);
    state_->env_group_ = env_group;

    state_->lidar_group_ = nullptr;
    state_->ptb_group_ = nullptr;
    state_->hmp_group_ = nullptr;

    state_->temperature_overview_group_ = new QGroupBox(this);
    state_->temperature_overview_group_->setObjectName("sensorGroupBox");
    state_->temperature_overview_group_->setMinimumWidth(kHomeOverviewTemperatureMinWidth);
    state_->temperature_overview_group_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    auto *temperatureOverviewLayout = new QVBoxLayout(state_->temperature_overview_group_);
    temperatureOverviewLayout->setContentsMargins(kHomeOverviewCardOuterPadding,
                                                 0,
                                                 kHomeOverviewCardOuterPadding,
                                                 kHomeOverviewCardOuterPadding);
    temperatureOverviewLayout->setSpacing(0);

    auto *temperatureOverviewTitleBar = new QWidget(state_->temperature_overview_group_);
    temperatureOverviewTitleBar->setObjectName("sectionTitleBar");
    temperatureOverviewTitleBar->setFixedHeight(kMainPageTitleBarHeight);
    auto *temperatureOverviewTitleLayout = new QHBoxLayout(temperatureOverviewTitleBar);
    temperatureOverviewTitleLayout->setContentsMargins(8, 2, 8, 2);
    temperatureOverviewTitleLayout->setSpacing(8);
    QWidget *temperatureOverviewTitleCluster = nullptr;
    state_->temperature_overview_inline_title_lbl_ = createSectionTitleCluster(temperatureOverviewTitleBar,
                                                                       QStringLiteral("thermometer"),
                                                                       kMainPageButtonHeight,
                                                                       &temperatureOverviewTitleCluster);
    state_->temperature_overview_inline_title_lbl_->setText(state_->is_english_
        ? QStringLiteral("Laser Driver Temperature Overview")
        : QStringLiteral("激光驱动温控概览"));
    temperatureOverviewTitleLayout->addWidget(temperatureOverviewTitleCluster, 0, Qt::AlignVCenter | Qt::AlignLeft);
    temperatureOverviewTitleLayout->addStretch(1);
    temperatureOverviewLayout->addWidget(temperatureOverviewTitleBar);

    state_->temperature_overview_panel_ = createTemperatureControllerOverviewPanel(state_->temperature_overview_group_);
    state_->temperature_overview_panel_->setOutputEnabledCallback([this](quint8 channel, bool enabled) {
        if (enabled)
        {
            const QMessageBox::StandardButton answer = QMessageBox::question(
                this,
                state_->is_english_ ? QStringLiteral("Enable Temperature Output") : QStringLiteral("开启温控输出"),
                state_->is_english_
                    ? QStringLiteral("Enable RD105 output for channel %1? Confirm the target temperature is safe.").arg(channel)
                    : QStringLiteral("确定开启 RD105 通道%1输出？请确认目标温度安全。").arg(channel));
            if (answer != QMessageBox::Yes)
            {
                if (state_->temperature_overview_panel_)
                {
                    state_->temperature_overview_panel_->updateData(state_->current_temperature_controller_);
                }
                return;
            }
        }
        VaporView::TemperatureControllerCommand command;
        command.channel = channel;
        command.output_enabled = enabled;
        sendTemperatureCommand(VaporView::CommandId::SetTemperatureOutputEnabled, command);
    });
    temperatureOverviewLayout->addWidget(state_->temperature_overview_panel_, 1);

    state_->home_overview_splitter_ = new QSplitter(Qt::Horizontal, this);
    state_->home_overview_splitter_->setObjectName(QStringLiteral("homeOverviewSplitter"));
    state_->home_overview_splitter_->setAttribute(Qt::WA_StyledBackground, true);
    state_->home_overview_splitter_->setAutoFillBackground(true);
    state_->home_overview_splitter_->setChildrenCollapsible(false);
    state_->home_overview_splitter_->setCollapsible(0, false);
    state_->home_overview_splitter_->setCollapsible(1, false);
    state_->home_overview_splitter_->setHandleWidth(kHomeOverviewSplitterHandleWidth);
    state_->home_overview_splitter_->setOpaqueResize(true);
    state_->home_overview_splitter_->setMinimumWidth(0);
    state_->home_overview_splitter_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    state_->home_overview_splitter_->addWidget(state_->config_group_);
    state_->home_overview_splitter_->addWidget(state_->temperature_overview_group_);
    state_->home_overview_splitter_->setStretchFactor(0, 0);
    state_->home_overview_splitter_->setStretchFactor(1, 1);
    state_->home_overview_splitter_->setSizes({state_->config_group_->minimumWidth(), kHomeOverviewTemperatureMinWidth});
    state_->main_layout_->addWidget(state_->home_overview_splitter_, 0);
    state_->main_layout_->addWidget(
        createMainCardResizeHandle(state_->home_overview_splitter_, kConfigCardMinHeight, this), 0);
    state_->main_layout_->addWidget(state_->data_group_, 0);
    updateConfigCardHeightForSourceMode();

    state_->main_layout_->addWidget(createMainCardResizeHandle(state_->data_group_, dataCardMinHeight, this), 0);

    state_->temperature_controller_group_ = new QGroupBox(this);
    state_->temperature_controller_group_->setObjectName("sensorGroupBox");
    state_->temperature_controller_group_->setMinimumWidth(0);
    state_->temperature_controller_group_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto *temperatureLayout = new QVBoxLayout(state_->temperature_controller_group_);
    temperatureLayout->setContentsMargins(1, 0, 1, 1);
    temperatureLayout->setSpacing(0);

    auto *temperatureTitleBar = new QWidget(state_->temperature_controller_group_);
    temperatureTitleBar->setObjectName("sectionTitleBar");
    temperatureTitleBar->setFixedHeight(kMainPageTitleBarHeight);
    auto *temperatureTitleLayout = new QHBoxLayout(temperatureTitleBar);
    temperatureTitleLayout->setContentsMargins(8, 2, 8, 2);
    temperatureTitleLayout->setSpacing(8);
    QWidget *temperatureTitleCluster = nullptr;
    state_->temperature_controller_inline_title_lbl_ = createSectionTitleCluster(temperatureTitleBar,
                                                                         QStringLiteral("thermometer"),
                                                                         kMainPageButtonHeight,
                                                                         &temperatureTitleCluster);
    temperatureTitleCluster->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    state_->temperature_controller_inline_title_lbl_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    temperatureTitleLayout->addWidget(temperatureTitleCluster, 0, Qt::AlignVCenter | Qt::AlignLeft);

    auto *temperatureTitlePortCombo = createSingleLevelPopupComboBox(temperatureTitleBar, false, true);
    state_->temperature_title_port_combo_ = temperatureTitlePortCombo;
    state_->temperature_title_port_combo_->setObjectName(QStringLiteral("temperatureTitlePortCombo"));
    state_->temperature_title_port_combo_->setEditable(false);
    state_->temperature_title_port_combo_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    state_->temperature_title_port_combo_->setFixedHeight(kMainPageButtonHeight);
    state_->temperature_title_port_combo_->setCursor(Qt::PointingHandCursor);
    state_->temperature_title_port_combo_->setFocusPolicy(Qt::TabFocus);
    connect(state_->temperature_title_port_combo_,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this,
            [this](int index) {
                if (!state_->temperature_port_combo_ || !state_->temperature_title_port_combo_ || index < 0)
                {
                    return;
                }
                const QString selectedPort = state_->temperature_title_port_combo_->itemData(index).toString().trimmed();
                if (selectedPort.isEmpty() || localSerialPortComboValue(state_->temperature_port_combo_) == selectedPort)
                {
                    return;
                }
                const int sourceIndex = state_->temperature_port_combo_->findData(selectedPort);
                if (sourceIndex >= 0)
                {
                    state_->temperature_port_combo_->setCurrentIndex(sourceIndex);
                }
                else
                {
                    setLocalSerialPortComboText(state_->temperature_port_combo_, selectedPort);
                }
            });
    temperatureTitleLayout->addWidget(state_->temperature_title_port_combo_, 0, Qt::AlignVCenter | Qt::AlignLeft);
    temperatureTitleLayout->addStretch(1);
    updateTemperatureControllerTitleText();
    state_->temperature_remote_buttons_widget_ = new QWidget(temperatureTitleBar);
    state_->temperature_remote_buttons_widget_->setObjectName(QStringLiteral("temperatureTitleButtons"));
    auto *temperatureRemoteLayout = new QHBoxLayout(state_->temperature_remote_buttons_widget_);
    temperatureRemoteLayout->setContentsMargins(0, 0, 0, 0);
    temperatureRemoteLayout->setSpacing(4);
    state_->temperature_remote_connect_btn_ = createRemoteDeviceButton(QStringLiteral("连接"), VaporView::CommandId::ConnectDevice, VaporView::SkyDeviceId::TemperatureController);
    state_->temperature_remote_disconnect_btn_ = createRemoteDeviceButton(QStringLiteral("断开"), VaporView::CommandId::DisconnectDevice, VaporView::SkyDeviceId::TemperatureController);
    state_->temperature_remote_reconnect_btn_ = createRemoteDeviceButton(QStringLiteral("重连"), VaporView::CommandId::ReconnectDevice, VaporView::SkyDeviceId::TemperatureController);
    state_->temperature_remote_connect_btn_->setObjectName(QStringLiteral("temperatureTitleConnectButton"));
    state_->temperature_remote_disconnect_btn_->setObjectName(QStringLiteral("temperatureTitleDisconnectButton"));
    state_->temperature_remote_reconnect_btn_->setObjectName(QStringLiteral("temperatureTitleReconnectButton"));
    for (QPushButton *button : {state_->temperature_remote_connect_btn_,
                                state_->temperature_remote_disconnect_btn_,
                                state_->temperature_remote_reconnect_btn_})
    {
        QObject::disconnect(button, nullptr, this, nullptr);
    }
    connect(state_->temperature_remote_connect_btn_, &QPushButton::clicked, this, [this]() {
        handleTemperatureTitleButton(VaporView::CommandId::ConnectDevice);
    });
    connect(state_->temperature_remote_disconnect_btn_, &QPushButton::clicked, this, [this]() {
        handleTemperatureTitleButton(VaporView::CommandId::DisconnectDevice);
    });
    connect(state_->temperature_remote_reconnect_btn_, &QPushButton::clicked, this, [this]() {
        handleTemperatureTitleButton(VaporView::CommandId::ReconnectDevice);
    });
    temperatureRemoteLayout->addWidget(state_->temperature_remote_connect_btn_);
    temperatureRemoteLayout->addWidget(state_->temperature_remote_disconnect_btn_);
    temperatureRemoteLayout->addWidget(state_->temperature_remote_reconnect_btn_);
    temperatureTitleLayout->addWidget(state_->temperature_remote_buttons_widget_, 0, Qt::AlignVCenter | Qt::AlignRight);
    temperatureLayout->addWidget(temperatureTitleBar);

    state_->temperature_controller_panel_ = new TemperatureControllerPanel(this);
    connect(state_->temperature_controller_panel_, &TemperatureControllerPanel::targetTemperatureRequested, this, [this](quint8 channel, double celsius) {
        VaporView::TemperatureControllerCommand command;
        command.channel = channel;
        command.target_temperature_c = celsius;
        sendTemperatureCommand(VaporView::CommandId::SetTemperatureTarget, command);
    });
    connect(state_->temperature_controller_panel_, &TemperatureControllerPanel::outputEnabledRequested, this, [this](quint8 channel, bool enabled) {
        if (enabled)
        {
            const QMessageBox::StandardButton answer = QMessageBox::question(
                this,
                state_->is_english_ ? QStringLiteral("Enable Temperature Output") : QStringLiteral("开启温控输出"),
                state_->is_english_
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
    connect(state_->temperature_controller_panel_, &TemperatureControllerPanel::outputModeRequested, this, [this](quint8 channel, quint16 mode) {
        VaporView::TemperatureControllerCommand command;
        command.channel = channel;
        command.output_mode = mode;
        sendTemperatureCommand(VaporView::CommandId::SetTemperatureOutputMode, command);
    });
    connect(state_->temperature_controller_panel_, &TemperatureControllerPanel::maxOutputPercentRequested, this, [this](quint8 channel, quint16 percent) {
        VaporView::TemperatureControllerCommand command;
        command.channel = channel;
        command.max_output_percent = percent;
        sendTemperatureCommand(VaporView::CommandId::SetTemperatureMaxOutputPercent, command);
    });
    connect(state_->temperature_controller_panel_, &TemperatureControllerPanel::pidRequested, this, [this](quint8 channel, quint32 kp, quint32 ki, quint32 kd) {
        VaporView::TemperatureControllerCommand command;
        command.channel = channel;
        command.kp = kp;
        command.ki = ki;
        command.kd = kd;
        sendTemperatureCommand(VaporView::CommandId::SetTemperaturePid, command);
    });
    connect(state_->temperature_controller_panel_, &TemperatureControllerPanel::autoPidRequested, this, [this](quint8 channel, quint16 mode) {
        VaporView::TemperatureControllerCommand command;
        command.channel = channel;
        command.auto_pid_mode = mode;
        sendTemperatureCommand(VaporView::CommandId::SetTemperatureAutoPid, command);
    });
    connect(state_->temperature_controller_panel_, &TemperatureControllerPanel::overtempUpperRequested, this, [this](quint8 channel, double celsius) {
        VaporView::TemperatureControllerCommand command;
        command.channel = channel;
        command.overtemp_upper_c = celsius;
        sendTemperatureCommand(VaporView::CommandId::SetTemperatureOvertempUpper, command);
    });
    connect(state_->temperature_controller_panel_, &TemperatureControllerPanel::overtempLowerRequested, this, [this](quint8 channel, double celsius) {
        VaporView::TemperatureControllerCommand command;
        command.channel = channel;
        command.overtemp_lower_c = celsius;
        sendTemperatureCommand(VaporView::CommandId::SetTemperatureOvertempLower, command);
    });
    connect(state_->temperature_controller_panel_, &TemperatureControllerPanel::temperatureSlopeRequested, this, [this](quint8 channel, double celsiusPerSecond) {
        VaporView::TemperatureControllerCommand command;
        command.channel = channel;
        command.temperature_slope_c_per_s = celsiusPerSecond;
        sendTemperatureCommand(VaporView::CommandId::SetTemperatureSlope, command);
    });
    connect(state_->temperature_controller_panel_, &TemperatureControllerPanel::startupDelayRequested, this, [this](quint8 channel, quint16 seconds) {
        VaporView::TemperatureControllerCommand command;
        command.channel = channel;
        command.startup_delay_s = seconds;
        sendTemperatureCommand(VaporView::CommandId::SetTemperatureStartupDelay, command);
    });
    connect(state_->temperature_controller_panel_, &TemperatureControllerPanel::sensorConfigRequested, this, [this](const VaporView::TemperatureControllerCommand& command) {
        sendTemperatureCommand(VaporView::CommandId::SetTemperatureSensorConfig, command);
    });
    connect(state_->temperature_controller_panel_, &TemperatureControllerPanel::controllerModeRequested, this, [this](quint16 mode) {
        VaporView::TemperatureControllerCommand command;
        command.channel = 1;
        command.controller_mode = mode;
        sendTemperatureCommand(VaporView::CommandId::SetTemperatureControllerMode, command);
    });
    connect(state_->temperature_controller_panel_, &TemperatureControllerPanel::deviceAddressRequested, this, [this](quint16 address) {
        VaporView::TemperatureControllerCommand command;
        command.channel = 1;
        command.device_address = address;
        sendTemperatureCommand(VaporView::CommandId::SetTemperatureDeviceAddress, command);
    });
    connect(state_->temperature_controller_panel_, &TemperatureControllerPanel::rs485BaudRequested, this, [this](quint16 baudIndex) {
        VaporView::TemperatureControllerCommand command;
        command.channel = 1;
        command.rs485_baud_index = baudIndex;
        sendTemperatureCommand(VaporView::CommandId::SetTemperatureRs485Baud, command);
    });
    connect(state_->temperature_controller_panel_, &TemperatureControllerPanel::overtempOutputModeRequested, this, [this](quint16 mode) {
        VaporView::TemperatureControllerCommand command;
        command.channel = 1;
        command.overtemp_output_mode = mode;
        sendTemperatureCommand(VaporView::CommandId::SetTemperatureOvertempOutputMode, command);
    });
    connect(state_->temperature_controller_panel_, &TemperatureControllerPanel::factoryResetRequested, this, [this]() {
        const QMessageBox::StandardButton answer = QMessageBox::question(
            this,
            state_->is_english_ ? QStringLiteral("Restore Factory Settings") : QStringLiteral("恢复出厂设置"),
            state_->is_english_
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
    temperatureLayout->addWidget(state_->temperature_controller_panel_);

    state_->device_panel_coordinator_ = std::make_unique<DevicePanelCoordinator>(DevicePanelBindings{
        state_->epsilon_panel_,
        state_->gnss_panel_,
        state_->imu_panel_,
        state_->ptb_panel_,
        state_->hmp_panel_,
        state_->lidar_panel_,
        state_->temperature_controller_panel_,
        state_->temperature_overview_panel_});

    state_->tcp_wave_group_ = new QGroupBox(this);
    state_->tcp_wave_group_->setObjectName("sensorGroupBox");
    state_->tcp_wave_group_->setMinimumHeight(kTcpWaveCardMinHeight);
    state_->tcp_wave_group_->setFixedHeight(kTcpWaveCardMinHeight);
    state_->tcp_wave_group_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto *tcpWaveLayout = new QVBoxLayout(state_->tcp_wave_group_);
    tcpWaveLayout->setContentsMargins(0, 0, 0, 0);
    tcpWaveLayout->setSpacing(0);

    state_->tcp_wave_panel_ = new TcpWavePanel(this);
    state_->tcp_wave_panel_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    connect(state_->tcp_wave_panel_, &TcpWavePanel::rawWaveFrameReady,
            this, &MainWindow::onTcpRawWaveFrameReady);
    connect(state_->tcp_wave_panel_, &TcpWavePanel::logMessageRequested,
            this, &MainWindow::log);
    connect(state_->tcp_wave_panel_, &TcpWavePanel::connectionStateChanged, this, [this](bool) {
        updateRecordingActionStates();
        updateHomeDeviceStatusCapsules();
    });
    connect(state_->tcp_wave_panel_, &TcpWavePanel::remoteWaveTcpConnectionRequested, this, [this](bool connectRequested) {
        requestRemoteWaveTcpConnection(connectRequested);
    });
    connect(state_->tcp_wave_panel_, &TcpWavePanel::remotePeakSearchRangeRequested,
            this, &MainWindow::sendRemotePeakSearchRange);
    connect(state_->tcp_wave_panel_, &TcpWavePanel::preferredPanelHeightChanged,
            this, &MainWindow::updateResponsiveHomeLayout);
    tcpWaveLayout->addWidget(state_->tcp_wave_panel_);
    state_->main_layout_->addWidget(state_->tcp_wave_group_, 0);
    state_->main_layout_->addStretch(1);
}

void MainWindow::setupLogPanel()
{
    state_->log_side_panel_ = createShrinkablePanel(this);
    state_->log_side_panel_->setObjectName(QStringLiteral("logSidePanel"));
    state_->log_side_panel_->setAttribute(Qt::WA_StyledBackground, true);
    state_->log_side_panel_->setAutoFillBackground(true);
    state_->log_side_panel_->setMouseTracking(true);
    state_->log_side_panel_->installEventFilter(this);
    state_->log_side_panel_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    auto *logSideLayout = new QVBoxLayout(state_->log_side_panel_);
    logSideLayout->setContentsMargins(0, 0, 0, 0);
    logSideLayout->setSpacing(8);

    state_->recording_status_card_ = new QFrame(state_->log_side_panel_);
    state_->recording_status_card_->setObjectName(QStringLiteral("recordingStatusCard"));
    state_->recording_status_card_->setFrameShape(QFrame::NoFrame);
    state_->recording_status_card_->setAttribute(Qt::WA_StyledBackground, true);
    state_->recording_status_card_->setAutoFillBackground(true);
    state_->recording_status_card_->setMinimumWidth(0);
    state_->recording_status_card_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Minimum);
    auto *recordingCardLayout = new QVBoxLayout(state_->recording_status_card_);
    recordingCardLayout->setContentsMargins(1, 1, 1, 1);
    recordingCardLayout->setSpacing(0);

    auto *recordingTitleBar = new QWidget(state_->recording_status_card_);
    recordingTitleBar->setObjectName("sectionTitleBar");
    recordingTitleBar->setFixedHeight(kMainPageTitleBarHeight);
    auto *recordingTitleLayout = new QHBoxLayout(recordingTitleBar);
    recordingTitleLayout->setContentsMargins(8, 2, 8, 2);
    recordingTitleLayout->setSpacing(8);

    QWidget *recordingTitleCluster = nullptr;
    state_->recording_status_title_lbl_ = createSectionTitleCluster(recordingTitleBar,
                                                            QStringLiteral("pencil"),
                                                            kMainPageButtonHeight,
                                                            &recordingTitleCluster);
    recordingTitleLayout->addWidget(recordingTitleCluster, 0, Qt::AlignVCenter | Qt::AlignLeft);
    recordingTitleLayout->addStretch(1);
    recordingCardLayout->addWidget(recordingTitleBar);

    auto *recordingBody = new QWidget(state_->recording_status_card_);
    recordingBody->setObjectName(QStringLiteral("recordingStatusBody"));
    recordingBody->setAttribute(Qt::WA_StyledBackground, true);
    recordingBody->setAutoFillBackground(true);
    auto *recordingStatusLayout = new QHBoxLayout(recordingBody);
    recordingStatusLayout->setContentsMargins(10, 8, 10, 8);
    recordingStatusLayout->setSpacing(0);

    state_->recording_status_label_ = new QLabel(recordingBody);
    state_->recording_status_label_->setObjectName(QStringLiteral("recordingStatusLabel"));
    state_->recording_status_label_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    state_->recording_status_label_->setWordWrap(true);
    state_->recording_status_label_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    state_->recording_status_label_->setMinimumWidth(0);
    state_->recording_status_label_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Minimum);
    recordingStatusLayout->addWidget(state_->recording_status_label_);
    recordingCardLayout->addWidget(recordingBody);
    logSideLayout->addWidget(state_->recording_status_card_, 0);

    state_->log_group_ = new QFrame(state_->log_side_panel_);
    state_->log_group_->setObjectName("logPanelFrame");
    state_->log_group_->setFrameShape(QFrame::NoFrame);
    state_->log_group_->setAttribute(Qt::WA_StyledBackground, true);
    state_->log_group_->setAutoFillBackground(true);
    state_->log_group_->setMinimumWidth(0);
    state_->log_group_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
    auto *log_layout = new QVBoxLayout(state_->log_group_);
    log_layout->setContentsMargins(1, 1, 1, 1);
    log_layout->setSpacing(0);

    auto *logTitleBar = new QWidget(state_->log_group_);
    logTitleBar->setObjectName("sectionTitleBar");
    logTitleBar->setFixedHeight(kMainPageTitleBarHeight);
    auto *logTitleLayout = new QHBoxLayout(logTitleBar);
    logTitleLayout->setContentsMargins(8, 2, 8, 2);
    logTitleLayout->setSpacing(8);

    QWidget *logTitleCluster = nullptr;
    state_->log_inline_title_lbl_ = createSectionTitleCluster(logTitleBar,
                                                      QStringLiteral("scroll-text"),
                                                      kMainPageButtonHeight,
                                                      &logTitleCluster);
    logTitleLayout->addWidget(logTitleCluster, 0, Qt::AlignVCenter | Qt::AlignLeft);
    logTitleLayout->addStretch(1);
    state_->log_filter_btn_ = new QToolButton(logTitleBar);
    state_->log_filter_btn_->setObjectName(QStringLiteral("titleBarButton"));
    state_->log_filter_btn_->setToolButtonStyle(Qt::ToolButtonIconOnly);
    state_->log_filter_btn_->setAutoRaise(false);
    state_->log_filter_btn_->setFocusPolicy(Qt::NoFocus);
    state_->log_filter_btn_->setProperty(kTitleBarHoverProperty, false);
    configureHoverParticipant(state_->log_filter_btn_, kTitleBarHoverParticipantProperty, this);
    state_->log_filter_btn_->setIcon(createLogFilterIcon());
    state_->log_filter_btn_->setStyleSheet(QStringLiteral("QToolButton::menu-indicator { image: none; width: 0px; height: 0px; }"));
    state_->log_filter_btn_->setPopupMode(QToolButton::DelayedPopup);
    if (state_->log_filter_menu_)
    {
        connect(state_->log_filter_btn_, &QToolButton::clicked, this, [this]() {
            if (!state_->log_filter_btn_ || !state_->log_filter_menu_)
            {
                return;
            }
            const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
            const qint64 lastHideMs = state_->log_filter_btn_->property("logFilterMenuHideMs").toLongLong();
            if (state_->log_filter_menu_->isVisible() || (lastHideMs > 0 && nowMs - lastHideMs < 250))
            {
                state_->log_filter_menu_->hide();
                state_->log_filter_btn_->setDown(false);
                return;
            }
            state_->log_filter_btn_->setDown(true);
            updateLogFilterAction();
            state_->log_filter_menu_->popupFrom(state_->log_filter_btn_, SingleLevelPopupAnchor::Right);
        });
        connect(state_->log_filter_menu_, &QMenu::aboutToHide, state_->log_filter_btn_, [button = state_->log_filter_btn_]() {
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
    state_->log_filter_btn_->setFixedSize(kMainPageButtonHeight, kMainPageButtonHeight);
    state_->log_filter_btn_->setIconSize(QSize(kMainPageButtonHeight - 12, kMainPageButtonHeight - 12));
    logTitleLayout->addWidget(state_->log_filter_btn_, 0, Qt::AlignVCenter | Qt::AlignRight);
    state_->log_clear_btn_ = createTitleBarActionButton(state_->clear_log_action_, logTitleBar);
    state_->log_clear_btn_->setFixedSize(kMainPageButtonHeight, kMainPageButtonHeight);
    state_->log_clear_btn_->setIconSize(QSize(kMainPageButtonHeight - 12, kMainPageButtonHeight - 12));
    logTitleLayout->addWidget(state_->log_clear_btn_, 0, Qt::AlignVCenter | Qt::AlignRight);
    log_layout->addWidget(logTitleBar);

    state_->log_text_edit_ = new QTextEdit(state_->log_group_);
    state_->log_text_edit_->setObjectName(QStringLiteral("logTextEdit"));
    state_->log_text_edit_->viewport()->setObjectName(QStringLiteral("logTextViewport"));
    state_->log_text_edit_->setFrameShape(QFrame::NoFrame);
    state_->log_text_edit_->setLineWidth(0);
    state_->log_text_edit_->setAutoFillBackground(false);
    state_->log_text_edit_->viewport()->setAutoFillBackground(false);
    state_->log_text_edit_->setReadOnly(true);
    state_->log_text_edit_->document()->setMaximumBlockCount(kMaxLogEntryCount);
    state_->log_text_edit_->setMinimumWidth(0);
    state_->log_text_edit_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
    log_layout->addWidget(state_->log_text_edit_);
    logSideLayout->addWidget(state_->log_group_, 1);
    state_->log_side_panel_->setMinimumWidth(minimumLogSidePanelWidth());
}
