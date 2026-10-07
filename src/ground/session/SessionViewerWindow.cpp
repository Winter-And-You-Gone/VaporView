#include "SessionViewerWindow.h"
#include "ground/session/SessionMapCoordinator.h"
#include "ground/session/SessionViewerPages.h"
#include "ground/session/SessionPpkWidget.h"
#include "shared/theme/TopLevelCardStyle.h"
#include "shared/theme/AppTheme.h"
#include <QButtonGroup>
#include <QCoreApplication>
#include <QFrame>
#include <QLabel>
#include <QPushButton>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QStackedWidget>
#include <QStyle>
#include <QShowEvent>
#include <QPainter>
#include <QFile>
#include "ppk/SessionNavigationSource.h"
#include "ground/widgets/CustomTitleBar.h"
#include "ground/wave/RawDataParserWindow.h"
#include "SessionTimeFormat.h"
#include "ground/widgets/WindowSizing.h"
#include "ground/session/SessionLoader.h"
#include "ground/session/SessionIndex.h"
#include "ground/session/SessionPlaybackController.h"
#include "ground/session/SessionWaveformRepository.h"
#include "ground/session/GroundRecordingService.h"

#include <QDir>
#include <QCloseEvent>
#include <QElapsedTimer>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QEventLoop>
#include <QPointer>
#include <QMessageBox>
#include <QScrollArea>
#include <QSettings>
#include "shared/config/SettingsWriteBarrier.h"
#include "shared/config/ApplicationConfig.h"
#include <QSplitter>
#include <QSplitterHandle>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <utility>

using namespace VaporView::Ground::SessionUi;

namespace
{

class SessionSidebarFrame final : public QFrame
{
public:
    using QFrame::QFrame;
    QSize minimumSizeHint() const override
    {
        QSize size = QFrame::minimumSizeHint();
        size.setWidth(0);
        return size;
    }
};

constexpr int kDefaultPeakSearchStartIndex = 0;
constexpr int kDefaultPeakSearchEndIndex = 0;
constexpr int kSessionViewerDefaultWidth = 1280;
constexpr int kSessionViewerDefaultHeight = 800;

QString normalizedDirectoryPath(const QString& directory)
{
    const QString normalized = QDir::fromNativeSeparators(directory.trimmed());
    return normalized.isEmpty()
        ? QString()
        : QDir::cleanPath(QFileInfo(normalized).absoluteFilePath());
}

QString configuredRecordingDirectory()
{
    QSettings settings(QStringLiteral("VaporView"), QStringLiteral("MainWindow"));
    const QString configured = normalizedDirectoryPath(
        settings.value(QStringLiteral("recording_directory")).toString());
    return configured.isEmpty()
        ? normalizedDirectoryPath(
              VaporView::Ground::Session::GroundRecordingService::defaultRecordingDirectory())
        : configured;
}

qint64 monotonicMilliseconds()
{
    static QElapsedTimer timer = []() {
        QElapsedTimer initialized;
        initialized.start();
        return initialized;
    }();
    return timer.elapsed();
}

int rangedProgressPercent(quint64 done, quint64 total, int startPercent, int endPercent)
{
    if (total == 0)
    {
        return std::clamp(startPercent, 0, 100);
    }
    const double ratio = std::clamp(static_cast<double>(done) / static_cast<double>(total), 0.0, 1.0);
    const int value = startPercent + static_cast<int>(std::lround(ratio * (endPercent - startPercent)));
    return std::clamp(value, 0, 100);
}

float waveformPeakValue(const QVector<float>& samples, int searchStartIndex, int searchEndIndex)
{
    return VaporView::Ground::SessionWaveformRepository::peakValue(
        samples,
        searchStartIndex,
        searchEndIndex);
}

bool isFullFramePeakSearch(int searchStartIndex, int searchEndIndex)
{
    return VaporView::Ground::SessionWaveformRepository::isFullFramePeakSearch(
        searchStartIndex,
        searchEndIndex);
}

}

SessionViewerWindow::SessionViewerWindow(QWidget *parent)
    : QMainWindow(parent)
    , overview_page_(nullptr)
    , waveform_page_(nullptr)
    , device_data_page_(nullptr)
    , loading_dialog_()
    , map_coordinator_(new VaporView::Ground::SessionMapCoordinator(this))
    , trajectory_controller_()
    , playback_controller_(new VaporView::Ground::SessionPlaybackController(this))
    , raw_data_parser_window_(nullptr)
    , session_directory_()
    , metadata_filename_()
    , recording_origin_(VaporView::Session::RecordingOrigin::Ground)
    , sensors_csv_filename_()
    , waveform_directory_()
    , waveform_index_filename_()
    , waveform_peak_index_filename_()
    , waveform_raw_filename_()
    , recording_directory_provider_()
    , session_name_()
    , start_time_utc_()
    , end_time_utc_()
    , csv_headers_()
    , csv_timestamps_us_()
    , temperature_values_()
    , humidity_values_()
    , pressure_values_()
    , waveform_timestamps_us_()
    , waveform_catalog_()
    , current_waveform_frame_samples_()
    , waveform_peak_raw_values_()
    , waveform_peak_values_()
    , peak_filter_settings_()
    , peak_search_start_index_(kDefaultPeakSearchStartIndex)
    , peak_search_end_index_(kDefaultPeakSearchEndIndex)
    , is_english_(false)
    , updating_frame_controls_(false)
    , waveform_peak_scatter_mode_(true)
    , waveform_show_filtered_frame_(false)
    , session_loading_(false)
    , peak_series_request_id_(0)
    , peak_series_watcher_(nullptr)
    , peak_series_cancel_flag_(nullptr)
    , points_per_frame_(50000)
    , sensor_export_rate_hz_(10)
    , waveform_export_rate_hz_(10)
    , waveform_export_mode_(QStringLiteral("fixed_rate"))
    , total_sensor_rows_(0)
    , total_waveform_frames_(0)
{
    setWindowFlag(Qt::Window, true);
    setupUi();
    connect(playback_controller_,
            &VaporView::Ground::SessionPlaybackController::currentFrameChanged,
            this,
            [this](int frameIndex) {
        if (frameIndex < 0)
        {
            return;
        }
        updating_frame_controls_ = true;
        waveform_page_->setFrameValueSilently(frameIndex + 1);
        updating_frame_controls_ = false;
        loadWaveformFrame(static_cast<quint64>(frameIndex));
    });
    connect(map_coordinator_,
            &VaporView::Ground::SessionMapCoordinator::trackPointActivated,
            this,
            &SessionViewerWindow::focusTrajectoryPoint);
    connect(map_coordinator_,
            &VaporView::Ground::SessionMapCoordinator::peakSettingsChangeRequested,
            this,
            &SessionViewerWindow::applyPeakSettingsFromTrajectory);
    VaporView::installCustomTitleBar(this);
    sidebar_logo_ = findChild<QLabel *>(QStringLiteral("customTitleLogo"));
    sidebar_logo_->setCursor(Qt::PointingHandCursor);
    sidebar_logo_->setFocusPolicy(Qt::TabFocus);
    sidebar_logo_->setAttribute(Qt::WA_Hover, true);
    sidebar_logo_->installEventFilter(this);
    auto *sessionControls = overview_page_->sessionControls();
    overview_page_->layout()->removeWidget(sessionControls);
    VaporView::addWidgetToCustomTitleBar(this, sessionControls);
    auto *titleLayout = qobject_cast<QHBoxLayout *>(sessionControls->parentWidget()->layout());
    for (int i = 0; i < titleLayout->count(); ++i)
        titleLayout->setStretch(i, titleLayout->itemAt(i)->widget() == sessionControls ? 1 : 0);
    resize(kSessionViewerDefaultWidth, kSessionViewerDefaultHeight);
    setEnglish(false);
    VaporView::centerWindowOnScreen(this, parent);

    VaporView::migrateLegacyApplicationConfig();
    QSettings settings("VaporView", "SessionViewer");
    const QString peakFilterMode = settings.value("peak_filter/mode", QStringLiteral("none")).toString().trimmed().toLower();
    if (peakFilterMode == QStringLiteral("iqr"))
    {
        peak_filter_settings_.mode = PeakFilterMode::IqrOutlier;
    }
    else if (peakFilterMode == QStringLiteral("keep_range"))
    {
        peak_filter_settings_.mode = PeakFilterMode::KeepRange;
    }
    else if (peakFilterMode == QStringLiteral("exclude_range"))
    {
        peak_filter_settings_.mode = PeakFilterMode::ExcludeRange;
    }
    peak_filter_settings_.minValue = settings.value("peak_filter/min_value", 0.0).toDouble();
    peak_filter_settings_.maxValue = settings.value("peak_filter/max_value", 0.0).toDouble();
    peak_search_start_index_ = std::max(0, settings.value("peak_search/start_index", kDefaultPeakSearchStartIndex).toInt());
    peak_search_end_index_ = std::max(0, settings.value("peak_search/end_index", kDefaultPeakSearchEndIndex).toInt());
    if (peak_search_end_index_ > 0 && peak_search_end_index_ <= peak_search_start_index_)
    {
        peak_search_end_index_ = peak_search_start_index_ + 1;
    }
    updateWaveformActionTexts();
    const QString lastSession = settings.value("last_session_directory").toString();
    if (!lastSession.isEmpty())
    {
        restoreLastSessionPath(lastSession);
    }
}

SessionViewerWindow::~SessionViewerWindow()
{
    cancelBackgroundWaveformPeakSeries(false);
    // The panel cancels and joins its worker before the viewer's state is destroyed.
    if (ppk_panel_)
    {
        disconnect(ppk_panel_, nullptr, this, nullptr);
        delete ppk_panel_;
        ppk_panel_ = nullptr;
    }
    if (raw_data_parser_window_)
    {
        delete raw_data_parser_window_;
        raw_data_parser_window_ = nullptr;
    }
}

void SessionViewerWindow::closeEvent(QCloseEvent *event)
{
    // Closing the workspace hides it; only destruction stops its workers.
    saveSidebarWidth();
    QMainWindow::closeEvent(event);
}

void SessionViewerWindow::showEvent(QShowEvent *event)
{
    QMainWindow::showEvent(event);
    if (!navigation_shown_)
    {
        navigation_shown_ = true;
        QSettings settings("VaporView", "SessionViewer");
        const int width = std::clamp(settings.value("sidebar_width", 62).toInt(), 0, 400);
        setSidebarWidth(width);
    }
}

void SessionViewerWindow::setupUi()
{
    setObjectName(QStringLiteral("sessionViewerWindow"));
    setAttribute(Qt::WA_StyledBackground, true);
    setAutoFillBackground(true);

    auto *scrollArea = new QScrollArea(this);
    scrollArea->setObjectName(QStringLiteral("sessionViewerScrollArea"));
    scrollArea->setAttribute(Qt::WA_StyledBackground, true);
    scrollArea->setAutoFillBackground(true);
    scrollArea->viewport()->setObjectName(QStringLiteral("sessionViewerViewport"));
    scrollArea->viewport()->setAttribute(Qt::WA_StyledBackground, true);
    scrollArea->viewport()->setAutoFillBackground(true);
    scrollArea->setWidgetResizable(true);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setupNavigation(scrollArea);

    auto *content = new QWidget(scrollArea);
    content->setObjectName(QStringLiteral("sessionViewerCentralWidget"));
    content->setAttribute(Qt::WA_StyledBackground, true);
    content->setAutoFillBackground(true);
    scrollArea->setWidget(content);

    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(8);
    auto *splitter = new QSplitter(Qt::Vertical, content);
    splitter->setObjectName(QStringLiteral("sessionViewerContentSplitter"));
    splitter->setAttribute(Qt::WA_StyledBackground, true);
    splitter->setAutoFillBackground(true);
    splitter->setChildrenCollapsible(false);

    auto *upperWidget = new QWidget(splitter);
    upperWidget->setObjectName(QStringLiteral("sessionViewerContentPane"));
    upperWidget->setAttribute(Qt::WA_StyledBackground, true);
    upperWidget->setAutoFillBackground(true);
    auto *upperLayout = new QVBoxLayout(upperWidget);
    upperLayout->setContentsMargins(0, 0, 0, 0);
    upperLayout->setSpacing(8);

    overview_page_ = new SessionOverviewWidget(upperWidget);
    waveform_page_ = new SessionWaveformWidget(upperWidget);
    device_data_page_ = new SessionDeviceDataWidget(splitter);
    loading_dialog_ = std::make_unique<SessionLoadingDialog>(this);
    upperLayout->addWidget(overview_page_);
    connect(VaporView::Ppk::SessionNavigationEvents::instance(), &VaporView::Ppk::SessionNavigationEvents::changed,
        this, [this](const QString& session) {
            if (ppk_session_available_ && QFileInfo(session_directory_).absoluteFilePath() == session)
            {
                updatePpkSummary();
                if (!session_loading_ && !ppkBusy())
                    onReloadClicked();
                else
                    navigation_refresh_pending_ = true;
            }
        }, Qt::QueuedConnection);
    upperLayout->addWidget(waveform_page_, 1);
    splitter->addWidget(upperWidget);
    splitter->addWidget(device_data_page_);
    splitter->setStretchFactor(0, 2);
    splitter->setStretchFactor(1, 3);
    layout->addWidget(splitter, 1);

    connect(overview_page_, &SessionOverviewWidget::chooseSessionRequested,
            this, &SessionViewerWindow::onChooseSessionClicked);
    connect(overview_page_, &SessionOverviewWidget::reloadRequested,
            this, &SessionViewerWindow::onReloadClicked);
    connect(overview_page_, &SessionOverviewWidget::clearRequested,
            this, &SessionViewerWindow::onClearViewClicked);
    connect(waveform_page_, &SessionWaveformWidget::frameSliderMoved,
            this, &SessionViewerWindow::onFrameSliderMoved);
    connect(waveform_page_, &SessionWaveformWidget::frameSliderChanged,
            this, &SessionViewerWindow::onFrameSliderChanged);
    connect(waveform_page_, &SessionWaveformWidget::frameSpinChanged,
            this, &SessionViewerWindow::onFrameSpinChanged);
    connect(waveform_page_, &SessionWaveformWidget::frameFilterRequested,
            this, &SessionViewerWindow::onToggleWaveformFrameFilterClicked);
    connect(waveform_page_, &SessionWaveformWidget::peakFilterRequested,
            this, &SessionViewerWindow::onConfigurePeakFilterClicked);
    connect(waveform_page_, &SessionWaveformWidget::plotModeRequested,
            this, &SessionViewerWindow::onTogglePeakPlotModeClicked);
    connect(waveform_page_, &SessionWaveformWidget::visibleRangeChanged,
            this, &SessionViewerWindow::syncEnvironmentRangeToWaveformRange);
}
void SessionViewerWindow::setupNavigation(QWidget *dataPage)
{
    auto *central = new QWidget(this);
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(8, 8, 8, 8);
    navigation_splitter_ = new QSplitter(Qt::Horizontal, central);
    navigation_splitter_->setObjectName(QStringLiteral("sessionViewerNavigationSplitter"));
    sidebar_ = new SessionSidebarFrame(navigation_splitter_);
    sidebar_->setObjectName(QStringLiteral("appSidebar"));
    sidebar_->setMinimumWidth(0);
    VaporView::configureTopLevelCard(sidebar_);
    auto *navLayout = new QVBoxLayout(sidebar_);
    navLayout->setContentsMargins(8, 12, 8, 12);
    navLayout->setSpacing(6);
    navigation_buttons_ = new QButtonGroup(this);
    navigation_buttons_->setExclusive(true);
    for (int i = 0; i < 4; ++i)
    {
        auto *button = new QPushButton(sidebar_);
        button->setObjectName(QStringLiteral("appSidebarButton"));
        button->setProperty("sessionViewerPage", i);
        button->setCheckable(true);
        button->setFocusPolicy(Qt::TabFocus);
        button->setMinimumWidth(0);
        navigation_buttons_->addButton(button, i);
        navLayout->addWidget(button);
    }
    navLayout->addStretch(1);
    navigation_buttons_->button(0)->setChecked(true);
    page_stack_ = new QStackedWidget(navigation_splitter_);
    page_stack_->setObjectName(QStringLiteral("sessionViewerPageStack"));
    page_stack_->addWidget(dataPage);
    for (int i = 0; i < 3; ++i)
    {
        tool_pages_[i] = new QWidget(page_stack_);
        tool_pages_[i]->setObjectName(QStringLiteral("sessionViewerToolPage%1").arg(i + 1));
        auto *pageLayout = new QVBoxLayout(tool_pages_[i]);
        pageLayout->setContentsMargins(0, 0, 0, 0);
        empty_pages_[i] = new QWidget(tool_pages_[i]);
        auto *emptyLayout = new QVBoxLayout(empty_pages_[i]);
        emptyLayout->addStretch();
        empty_labels_[i] = new QLabel(empty_pages_[i]);
        empty_labels_[i]->setWordWrap(true);
        empty_labels_[i]->setAlignment(Qt::AlignCenter);
        empty_actions_[i] = new QPushButton(empty_pages_[i]);
        emptyLayout->addWidget(empty_labels_[i]);
        emptyLayout->addWidget(empty_actions_[i], 0, Qt::AlignHCenter);
        emptyLayout->addStretch();
        connect(empty_actions_[i], &QPushButton::clicked, this, [this]() {
            if (session_directory_.isEmpty())
                onChooseSessionClicked();
            else
                onReloadClicked();
        });
        pageLayout->addWidget(empty_pages_[i]);
        page_stack_->addWidget(tool_pages_[i]);
    }
    navigation_splitter_->setCollapsible(0, true);
    navigation_splitter_->setCollapsible(1, false);
    navigation_splitter_->setStretchFactor(0, 0);
    navigation_splitter_->setStretchFactor(1, 1);
    QSettings settings("VaporView", "SessionViewer");
    const int width = std::clamp(settings.value("sidebar_width", 62).toInt(), 0, 400);
    setSidebarWidth(width);
    navigation_splitter_->handle(1)->installEventFilter(this);
    connect(navigation_splitter_, &QSplitter::splitterMoved, this, [this]() {
        updateNavigation();
        saveSidebarWidth();
    });
    connect(navigation_buttons_, &QButtonGroup::idClicked, this, [this](int id) {
        setCurrentPage(static_cast<Page>(id));
    });
    layout->addWidget(navigation_splitter_);
    setCentralWidget(central);
}

SessionViewerWindow::Page SessionViewerWindow::currentPage() const
{
    return static_cast<Page>(page_stack_->currentIndex());
}

void SessionViewerWindow::setCurrentPage(Page page)
{
    if (session_loading_)
    {
        navigation_buttons_->button(page_stack_->currentIndex())->setChecked(true);
        return;
    }
    if (page == Page::Trajectory)
    {
        if (trajectory_controller_.hasTrack() && !ensureTrajectoryPeakValuesReady())
            return;
        if (!trajectory_page_)
        {
            syncPeakSettingsToTrajectoryViewer();
            trajectory_page_ = map_coordinator_->embeddedPage(tool_pages_[0]);
            tool_pages_[0]->layout()->addWidget(trajectory_page_);
            map_coordinator_->updateTrack(trajectory_controller_.trackPoints(), trajectory_controller_.trackStats());
        }
    }
    if (page == Page::Ppk)
        ensurePpkPage();
    if (page == Page::RawData)
    {
        if (!raw_data_parser_window_)
        {
            raw_scroll_ = new QScrollArea(tool_pages_[2]);
            raw_scroll_->setObjectName(QStringLiteral("sessionRawDataScrollArea"));
            raw_scroll_->setWidgetResizable(true);
            raw_scroll_->setFrameShape(QFrame::NoFrame);
            raw_data_parser_window_ = new RawDataParserWindow(raw_scroll_, true);
            raw_scroll_->setWidget(raw_data_parser_window_);
            tool_pages_[2]->layout()->addWidget(raw_scroll_);
            raw_data_parser_window_->setEnglish(is_english_);
        }
        if (ppk_session_available_ && raw_session_directory_ != session_directory_)
        {
            raw_session_directory_ = session_directory_;
            raw_data_parser_window_->openSessionPath(session_directory_);
        }
    }
    page_stack_->setCurrentIndex(static_cast<int>(page));
    navigation_buttons_->button(static_cast<int>(page))->setChecked(true);
    updatePageAvailability();
    updateNavigation();
}

void SessionViewerWindow::updatePageAvailability()
{
    if (!page_stack_)
        return;
    const bool available[] = {trajectory_controller_.hasTrack(), ppk_session_available_, ppk_session_available_};
    for (int i = 0; i < 3; ++i)
    {
        empty_pages_[i]->setVisible(!available[i]);
        empty_labels_[i]->setText(i == 0 && ppk_session_available_
            ? (is_english_ ? QStringLiteral("No trajectory available. Use the title bar to load a session containing valid position data.")
                           : QStringLiteral("尚无可用轨迹，请通过标题栏加载包含有效定位数据的会话。"))
            : (is_english_ ? QStringLiteral("Use the title bar to open or reload a session.")
                           : QStringLiteral("请通过标题栏打开或重新加载会话。")));
        empty_actions_[i]->setText(session_directory_.isEmpty()
            ? (is_english_ ? QStringLiteral("Open Data") : QStringLiteral("打开数据"))
            : (is_english_ ? QStringLiteral("Reload") : QStringLiteral("重新加载")));
    }
    if (trajectory_page_)
        trajectory_page_->setVisible(available[0]);
    if (ppk_scroll_)
        ppk_scroll_->setVisible(available[1]);
    if (raw_scroll_)
        raw_scroll_->setVisible(available[2]);
}

void SessionViewerWindow::setSidebarWidth(int width)
{
    int target = width < 31 ? 0 : width < 120 ? 62 : std::max(190, width);
    updateNavigation(target);
    if (target == 62)
        target = std::max(target, sidebar_->layout()->minimumSize().width());
    sidebar_->setMinimumWidth(target);
    sidebar_->setMaximumWidth(target);
    sidebar_->layout()->activate();
    navigation_splitter_->setSizes({target, std::max(1, navigation_splitter_->width()
                                                    - navigation_splitter_->handleWidth() - target)});
    sidebar_->setMinimumWidth(0);
    sidebar_->setMaximumWidth(target < 120 ? target : QWIDGETSIZE_MAX);
    if (target > 0)
        last_sidebar_visible_width_ = target;
}

void SessionViewerWindow::toggleSidebarFromLogo()
{
    const int width = navigation_splitter_->sizes().value(0);
    if (width > 0)
        last_sidebar_visible_width_ = width;
    setSidebarWidth(width > 0 ? 0 : last_sidebar_visible_width_);
    saveSidebarWidth();
}

bool SessionViewerWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == sidebar_logo_)
    {
        const auto type = event->type();
        if (type == QEvent::Enter || type == QEvent::HoverEnter ||
            type == QEvent::Leave || type == QEvent::HoverLeave)
        {
            sidebar_logo_hovered_ = type == QEvent::Enter || type == QEvent::HoverEnter;
            updateNavigation();
        }
        else if (type == QEvent::MouseButtonPress || type == QEvent::MouseButtonDblClick)
        {
            if (static_cast<QMouseEvent *>(event)->button() == Qt::LeftButton)
            {
                toggleSidebarFromLogo();
                return true;
            }
        }
        else if (type == QEvent::MouseButtonRelease &&
                 static_cast<QMouseEvent *>(event)->button() == Qt::LeftButton)
            return true;
        else if (type == QEvent::KeyPress)
        {
            const auto key = static_cast<QKeyEvent *>(event)->key();
            if (key == Qt::Key_Return || key == Qt::Key_Enter || key == Qt::Key_Space)
            {
                toggleSidebarFromLogo();
                return true;
            }
        }
    }
    if (navigation_splitter_ && watched == navigation_splitter_->handle(1))
    {
        if (event->type() == QEvent::MouseButtonPress)
            sidebar_->setMaximumWidth(QWIDGETSIZE_MAX);
        else if (event->type() == QEvent::MouseButtonRelease)
            QTimer::singleShot(0, this, [this]() {
                setSidebarWidth(navigation_splitter_->sizes().value(0));
                saveSidebarWidth();
            });
    }
    return QMainWindow::eventFilter(watched, event);
}

void SessionViewerWindow::saveSidebarWidth()
{
    if (ui_test_mode_)
        return;
    QSettings settings("VaporView", "SessionViewer");
    VaporView::setPersistentSetting(settings, QStringLiteral("sidebar_width"), navigation_splitter_->sizes().value(0));
}

void SessionViewerWindow::updateNavigation(int sidebarWidth)
{
    if (!page_stack_)
        return;
    const QStringList labels = is_english_
        ? QStringList{QStringLiteral("Data View"), QStringLiteral("Trajectory"), QStringLiteral("PPK Processing"), QStringLiteral("Raw Data Parser")}
        : QStringList{QStringLiteral("数据查看"), QStringLiteral("轨迹查看"), QStringLiteral("PPK 后处理"), QStringLiteral("原始数据解析")};
    const QStringList icons = {QStringLiteral("audio-waveform"), QStringLiteral("route"), QStringLiteral("satellite"), QStringLiteral("scroll-text")};
    const bool compact = (sidebarWidth >= 0 ? sidebarWidth : navigation_splitter_->sizes().value(0)) < 120;
    for (int i = 0; i < 4; ++i)
    {
        auto *button = navigation_buttons_->button(i);
        button->setText(compact ? QString() : labels[i]);
        button->setToolTip(labels[i]);
        button->setAccessibleName(labels[i]);
        if (button->property("_vv_sidebar_compact").toBool() != compact)
        {
            button->setProperty("_vv_sidebar_compact", compact);
            button->style()->unpolish(button);
            button->style()->polish(button);
        }
        const QColor color = button->isChecked() ? QColor(Qt::white) : palette().color(QPalette::WindowText);
        QFile svg(QCoreApplication::applicationDirPath() + QStringLiteral("/resources/lucide/%1.svg").arg(icons[i]));
        if (svg.open(QIODevice::ReadOnly))
        {
            QPixmap pixmap = QIcon(svg.fileName()).pixmap(32, 32);
            QPainter painter(&pixmap);
            painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
            painter.fillRect(pixmap.rect(), color);
            painter.end();
            button->setIcon(QIcon(pixmap));
            button->setIconSize(QSize(20, 20));
        }
    }
    const QString title = is_english_ ? QStringLiteral("Data Viewer") : QStringLiteral("数据查看器");
    setWindowTitle(page_stack_->currentIndex() == 0 ? title : title + QStringLiteral(" · ") + labels[page_stack_->currentIndex()]);
    if (sidebar_logo_)
    {
        const bool collapsed = (sidebarWidth >= 0 ? sidebarWidth : navigation_splitter_->sizes().value(0)) == 0;
        const QString tip = collapsed
            ? (is_english_ ? QStringLiteral("Show left sidebar") : QStringLiteral("展开左侧栏"))
            : (is_english_ ? QStringLiteral("Hide left sidebar") : QStringLiteral("收起左侧栏"));
        sidebar_logo_->setToolTip(tip);
        sidebar_logo_->setAccessibleName(tip);
        VaporView::updateCustomTitleBarSidebarLogo(this, collapsed, sidebar_logo_hovered_);
    }
}

void SessionViewerWindow::setEnglish(bool english)
{
    is_english_ = english;
    updateTexts();
}

void SessionViewerWindow::changeEvent(QEvent *event)
{
    QMainWindow::changeEvent(event);
    if (event && (event->type() == QEvent::PaletteChange ||
                  event->type() == QEvent::ApplicationPaletteChange ||
                  event->type() == QEvent::StyleChange || event->type() == QEvent::FontChange))
    {
        device_data_page_->applyTheme();
        loading_dialog_->applyTheme();
        updateNavigation();
        // Wait for the new stylesheet to reach the navigation buttons before
        // measuring them, including when returning to the standard font size.
        QTimer::singleShot(0, this, [this]() {
            const int width = navigation_splitter_->sizes().value(0);
            if (width < 120)
                setSidebarWidth(width);
        });
    }
}

void SessionViewerWindow::setRecordingDirectoryProvider(RecordingDirectoryProvider provider)
{
    recording_directory_provider_ = std::move(provider);
}

QString SessionViewerWindow::dataSelectionDirectory() const
{
    QString directory;
    if (recording_directory_provider_)
    {
        directory = normalizedDirectoryPath(recording_directory_provider_());
    }
    if (directory.isEmpty())
    {
        directory = configuredRecordingDirectory();
    }
    if (directory.isEmpty() || !QFileInfo(directory).isDir())
    {
        directory = normalizedDirectoryPath(
            VaporView::Ground::Session::GroundRecordingService::defaultRecordingDirectory());
    }
    if (directory.isEmpty() || !QFileInfo(directory).isDir())
    {
        directory = QDir::currentPath();
    }
    return directory;
}

void SessionViewerWindow::setUiTestMode(bool enabled)
{
    if (ui_test_mode_ == enabled)
    {
        return;
    }
    if (enabled)
    {
        ui_test_saved_peak_filter_settings_ = peak_filter_settings_;
        ui_test_saved_peak_search_start_index_ = peak_search_start_index_;
        ui_test_saved_peak_search_end_index_ = peak_search_end_index_;
        ui_test_mode_ = true;
        setStatusText(is_english_
            ? QStringLiteral("[UI Test] Viewer settings are sandboxed; exports will not create files.")
            : QStringLiteral("[界面测试] 查看器设置已沙箱化；导出不会创建文件。"));
        return;
    }
    ui_test_mode_ = false;
    peak_filter_settings_ = ui_test_saved_peak_filter_settings_;
    peak_search_start_index_ = ui_test_saved_peak_search_start_index_;
    peak_search_end_index_ = ui_test_saved_peak_search_end_index_;
    updateWaveformControls();
    updateWaveformActionTexts();
    syncPeakSettingsToTrajectoryViewer();
}

void SessionViewerWindow::updateTexts()
{
    updateNavigation();
    updatePageAvailability();
    overview_page_->setEnglish(is_english_);
    if (ppk_panel_)
        ppk_panel_->setEnglish(is_english_);
    updatePpkSummary();
    waveform_page_->setEnglish(is_english_);
    device_data_page_->setEnglish(is_english_);
    updateWaveformActionTexts();

    if (session_directory_.isEmpty())
    {
        setStatusText(is_english_
            ? QStringLiteral("Choose a session directory to inspect recorded CSV and waveform files.")
            : QStringLiteral("请选择一个 session 目录来查看录制的 CSV 和波形文件。"));
        device_data_page_->setInfoText(is_english_ ? QStringLiteral("No CSV loaded") : QStringLiteral("尚未加载 CSV"));
        waveform_page_->setFrameInfoText(is_english_ ? QStringLiteral("No waveform frame loaded") : QStringLiteral("尚未加载波形帧"));
        waveform_page_->setEnvironmentInfoText(is_english_ ? QStringLiteral("No environmental series loaded") : QStringLiteral("尚未加载环境趋势数据"));
    }
    else
    {
        updateSummaryLabels();
        updateWaveformControls();
    }

    map_coordinator_->setEnglish(is_english_);
    if (raw_data_parser_window_)
    {
        raw_data_parser_window_->setEnglish(is_english_);
    }
}

void SessionViewerWindow::updateWaveformActionTexts()
{
    const QString frameFilterText = waveform_show_filtered_frame_
        ? (is_english_ ? QStringLiteral("Show Full Frame") : QStringLiteral("显示完整波形"))
        : (is_english_ ? QStringLiteral("Show Filtered Frame") : QStringLiteral("显示过滤波形"));
    const QString plotModeText = waveform_peak_scatter_mode_
        ? (is_english_ ? QStringLiteral("Show Polyline") : QStringLiteral("切换到折线图"))
        : (is_english_ ? QStringLiteral("Show Scatter") : QStringLiteral("切换到散点图"));
    const QString peakFilterText = QStringLiteral("%1:%2 / %3")
        .arg(is_english_ ? QStringLiteral("Peak") : QStringLiteral("峰值"))
        .arg(peakSearchRangeText())
        .arg(peakFilterModeText(peak_filter_settings_.mode));
    waveform_page_->setActionTexts(frameFilterText, peakFilterText, plotModeText);
}

QString SessionViewerWindow::peakFilterModeText(PeakFilterMode mode) const
{
    switch (mode)
    {
    case PeakFilterMode::IqrOutlier:
        return QStringLiteral("IQR");
    case PeakFilterMode::KeepRange:
        return is_english_ ? QStringLiteral("Keep Range") : QStringLiteral("保留区间");
    case PeakFilterMode::ExcludeRange:
        return is_english_ ? QStringLiteral("Exclude Range") : QStringLiteral("排除区间");
    case PeakFilterMode::None:
    default:
        return is_english_ ? QStringLiteral("Off") : QStringLiteral("关闭");
    }
}

QString SessionViewerWindow::peakSearchRangeText() const
{
    const QString searchEndText = peak_search_end_index_ <= 0
        ? (is_english_ ? QStringLiteral("end") : QStringLiteral("末尾"))
        : QString::number(peak_search_end_index_);
    return QStringLiteral("%1-%2").arg(peak_search_start_index_).arg(searchEndText);
}

void SessionViewerWindow::syncPeakSettingsToTrajectoryViewer()
{
    map_coordinator_->setPeakSettings(
        peak_search_start_index_,
        peak_search_end_index_,
        static_cast<int>(peak_filter_settings_.mode),
        peak_filter_settings_.minValue,
        peak_filter_settings_.maxValue);
}

bool SessionViewerWindow::applyPeakSettings(int searchStartIndex,
                                            int searchEndIndex,
                                            PeakFilterMode mode,
                                            double minValue,
                                            double maxValue,
                                            bool hasMinValue,
                                            bool hasMaxValue,
                                            const QString& recalculatingText,
                                            const QString& filteringText)
{
    if (searchStartIndex < 0 || (searchEndIndex > 0 && searchEndIndex <= searchStartIndex))
    {
        return false;
    }

    const bool peakSearchChanged =
        peak_search_start_index_ != searchStartIndex ||
        peak_search_end_index_ != searchEndIndex;
    peak_search_start_index_ = searchStartIndex;
    peak_search_end_index_ = searchEndIndex;
    peak_filter_settings_.mode = mode;
    if (hasMinValue)
    {
        peak_filter_settings_.minValue = minValue;
    }
    if (hasMaxValue)
    {
        peak_filter_settings_.maxValue = maxValue;
    }

    QSettings settings("VaporView", "SessionViewer");
    VaporView::setPersistentSetting(settings, QStringLiteral("peak_filter/mode"),
        mode == PeakFilterMode::IqrOutlier
            ? QStringLiteral("iqr")
            : mode == PeakFilterMode::KeepRange
                ? QStringLiteral("keep_range")
                : mode == PeakFilterMode::ExcludeRange
                    ? QStringLiteral("exclude_range")
                    : QStringLiteral("none"));
    VaporView::setPersistentSetting(settings, QStringLiteral("peak_filter/min_value"), peak_filter_settings_.minValue);
    VaporView::setPersistentSetting(settings, QStringLiteral("peak_filter/max_value"), peak_filter_settings_.maxValue);
    VaporView::setPersistentSetting(settings, QStringLiteral("peak_search/start_index"), peak_search_start_index_);
    VaporView::setPersistentSetting(settings, QStringLiteral("peak_search/end_index"), peak_search_end_index_);

    updateWaveformActionTexts();
    syncPeakSettingsToTrajectoryViewer();
    beginSessionLoading(peakSearchChanged ? recalculatingText : filteringText);
    if (peakSearchChanged &&
        !waveform_catalog_.isEmpty())
    {
        const bool loaded = loadWaveformPeakSeries();
        finishSessionLoading();
        syncPeakSettingsToTrajectoryViewer();
        return loaded;
    }

    applyPeakFilter();
    finishSessionLoading();
    syncPeakSettingsToTrajectoryViewer();
    return true;
}

void SessionViewerWindow::applyPeakSettingsFromTrajectory(int searchStartIndex,
                                                          int searchEndIndex,
                                                          int filterMode,
                                                          double minValue,
                                                          double maxValue)
{
    const PeakFilterMode mode = static_cast<PeakFilterMode>(filterMode);
    if (mode != PeakFilterMode::None &&
        mode != PeakFilterMode::IqrOutlier &&
        mode != PeakFilterMode::KeepRange &&
        mode != PeakFilterMode::ExcludeRange)
    {
        return;
    }
    if (!applyPeakSettings(searchStartIndex,
            searchEndIndex,
            mode,
            minValue,
            maxValue,
            true,
            true,
            is_english_ ? QStringLiteral("Recalculating waveform peak series...") : QStringLiteral("正在重新计算波形峰值序列..."),
            is_english_ ? QStringLiteral("Applying peak filter...") : QStringLiteral("正在应用峰值过滤...")))
    {
        syncPeakSettingsToTrajectoryViewer();
    }
}

void SessionViewerWindow::setStatusText(const QString& text)
{
    overview_page_->setStatusText(text);
}

void SessionViewerWindow::setSessionLoadingControlsEnabled(bool enabled)
{
    overview_page_->setControlsEnabled(enabled);
    waveform_page_->setControlsEnabled(enabled);
    if (ppk_panel_)
        ppk_panel_->setEnabled(enabled);
    if (enabled)
    {
        updateWaveformControls();
    }
}

void SessionViewerWindow::beginSessionLoading(const QString& text)
{
    session_loading_ = true;
    overview_page_->focusStatus();
    setSessionLoadingControlsEnabled(false);
    setStatusText(text);
    loading_dialog_->begin(text, is_english_);
}

void SessionViewerWindow::updateSessionLoadingProgress(const QString& text, int percent)
{
    setStatusText(text);
    if (session_loading_)
    {
        loading_dialog_->update(text, percent);
    }
}

void SessionViewerWindow::finishSessionLoading()
{
    loading_dialog_->finish(overview_page_->statusText());
    session_loading_ = false;
    setSessionLoadingControlsEnabled(true);
    updatePageAvailability();
    if (raw_data_parser_window_ && ppk_session_available_ && raw_session_directory_ != session_directory_)
    {
        raw_session_directory_ = session_directory_;
        raw_data_parser_window_->openSessionPath(session_directory_);
    }
    if (navigation_refresh_pending_ && !ppkBusy())
    {
        navigation_refresh_pending_ = false;
        QMetaObject::invokeMethod(this, &SessionViewerWindow::onReloadClicked, Qt::QueuedConnection);
    }
}

void SessionViewerWindow::clearLoadedData(bool clearPathEdit)
{
    cancelBackgroundWaveformPeakSeries(false);
    session_directory_.clear();
    syncPpkSession(false);
    raw_session_directory_.clear();
    if (raw_data_parser_window_)
        raw_data_parser_window_->clearSession();
    metadata_filename_.clear();
    recording_origin_ = VaporView::Session::RecordingOrigin::Ground;
    sensors_csv_filename_.clear();
    waveform_directory_.clear();
    waveform_index_filename_.clear();
    waveform_peak_index_filename_.clear();
    waveform_raw_filename_.clear();
    session_name_.clear();
    start_time_utc_.clear();
    end_time_utc_.clear();
    csv_headers_.clear();
    csv_timestamps_us_.clear();
    temperature_values_.clear();
    humidity_values_.clear();
    pressure_values_.clear();
    trajectory_controller_.clear();
    waveform_timestamps_us_.clear();
    waveform_catalog_ = {};
    playback_controller_->clear();
    current_waveform_frame_samples_.clear();
    waveform_peak_raw_values_.clear();
    waveform_peak_values_.clear();
    total_sensor_rows_ = 0;
    total_waveform_frames_ = 0;
    points_per_frame_ = 50000;
    sensor_export_rate_hz_ = 10;
    waveform_export_rate_hz_ = 10;
    waveform_export_mode_ = QStringLiteral("fixed_rate");

    device_data_page_->clear();
    waveform_page_->clear();
    map_coordinator_->updateTrack({}, trajectory_controller_.trackStats());
    updatePageAvailability();
    waveform_page_->setFrameInfoText(is_english_ ? QStringLiteral("No waveform frame loaded") : QStringLiteral("尚未加载波形帧"));
    device_data_page_->setInfoText(is_english_ ? QStringLiteral("No CSV loaded") : QStringLiteral("尚未加载 CSV"));
    waveform_page_->setEnvironmentInfoText(is_english_ ? QStringLiteral("No environmental series loaded") : QStringLiteral("尚未加载环境趋势数据"));
    updateSummaryLabels();
    updateWaveformControls();
    if (clearPathEdit)
    {
        overview_page_->clearSessionPath();
    }
    setStatusText(is_english_ ? QStringLiteral("The current page has been cleared.") : QStringLiteral("当前页面内容已清空。"));
}

void SessionViewerWindow::restoreLastSessionPath(const QString& path)
{
    const QString sessionDirectory = resolveSessionDirectory(path);
    if (sessionDirectory.isEmpty())
    {
        return;
    }

    clearLoadedData(false);
    session_directory_ = sessionDirectory;
    syncPpkSession(true);
    overview_page_->setSessionPath(session_directory_);
    setStatusText(is_english_
        ? "Restored the last session path only. Click Reload to load its CSV and waveform files."
        : "已恢复上次会话路径，尚未读取大文件；点击“重新加载”后再加载 CSV 和波形数据。");
}


QString SessionViewerWindow::resolveSessionDirectory(const QString& path) const
{
    return VaporView::Ground::SessionLoader::resolveSessionDirectory(path);
}

bool SessionViewerWindow::openSessionPath(const QString& path)
{
    const QString sessionDirectory = resolveSessionDirectory(path);
    if (sessionDirectory.isEmpty())
    {
        setStatusText(is_english_ ? "The selected path is not a session directory or session.json file."
                                  : "选择的路径不是有效的 session 目录或 session.json 文件。");
        return false;
    }

    if (!loadSessionDirectory(sessionDirectory))
    {
        return false;
    }

    QSettings settings("VaporView", "SessionViewer");
    VaporView::setPersistentSetting(settings, QStringLiteral("last_session_directory"), sessionDirectory);
    return true;
}

void SessionViewerWindow::onChooseSessionClicked()
{
    if (session_loading_ || ppkBusy())
        return;
    const QString initialDir = dataSelectionDirectory();
    const QString sessionDirectory = QFileDialog::getExistingDirectory(
        this,
        is_english_ ? "Choose Data Directory" : "选择数据目录",
        initialDir,
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);

    if (!sessionDirectory.isEmpty())
    {
        openSessionPath(sessionDirectory);
    }
}

void SessionViewerWindow::onReloadClicked()
{
    if (session_loading_ || ppkBusy())
        return;
    const QString requestedPath = overview_page_->sessionPath();
    if (requestedPath.isEmpty())
    {
        setStatusText(is_english_ ? "Enter a session path before reloading." : "请先输入会话路径，再重新加载。");
        return;
    }
    openSessionPath(requestedPath);
}

void SessionViewerWindow::onClearViewClicked()
{
    if (session_loading_ || ppkBusy())
        return;
    const QString previousSessionDirectory = session_directory_;
    clearLoadedData(previousSessionDirectory.isEmpty());
    if (!previousSessionDirectory.isEmpty())
    {
        session_directory_ = previousSessionDirectory;
        overview_page_->setSessionPath(session_directory_);
    }
}

void SessionViewerWindow::onViewTrajectoryClicked()
{
    setCurrentPage(Page::Trajectory);
}

bool SessionViewerWindow::ensureTrajectoryPeakValuesReady()
{
    const bool hasWaveformFrames =
        !waveform_catalog_.isEmpty();
    if (!hasWaveformFrames)
    {
        return true;
    }

    const bool peakSeriesReady =
        !waveform_peak_values_.isEmpty() &&
        waveform_peak_values_.size() == waveform_timestamps_us_.size() &&
        (!total_waveform_frames_ || static_cast<quint64>(waveform_peak_values_.size()) == total_waveform_frames_);
    if (peakSeriesReady)
    {
        return true;
    }

    beginSessionLoading(is_english_
        ? QStringLiteral("Preparing trajectory peak values...")
        : QStringLiteral("正在准备轨迹峰值数据..."));
    cancelBackgroundWaveformPeakSeries(false);
    const bool loaded = loadWaveformPeakSeries(false);
    finishSessionLoading();
    syncPeakSettingsToTrajectoryViewer();
    return loaded;
}

void SessionViewerWindow::onRawDataParserClicked()
{
    setCurrentPage(Page::RawData);
}

void SessionViewerWindow::onPpkProcessingClicked()
{
    setCurrentPage(Page::Ppk);
}

void SessionViewerWindow::ensurePpkPage()
{
    if (ppk_panel_)
        return;
    auto *scroll = new QScrollArea(tool_pages_[1]);
    ppk_scroll_ = scroll;
    scroll->setObjectName(QStringLiteral("sessionPpkScrollArea"));
    scroll->viewport()->setObjectName(QStringLiteral("sessionPpkViewport"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    ppk_panel_ = new SessionPpkWidget(scroll);
    scroll->setWidget(ppk_panel_);
    tool_pages_[1]->layout()->addWidget(scroll);
    ppk_panel_->setEnglish(is_english_);
    ppk_panel_->setSessionDirectory(ppk_session_available_ ? session_directory_ : QString());
    connect(ppk_panel_, &SessionPpkWidget::busyChanged, this, [this](bool busy) {
        overview_page_->setSessionChangesEnabled(!busy);
        updatePpkSummary();
        if (!busy && navigation_refresh_pending_)
        {
            navigation_refresh_pending_ = false;
            QMetaObject::invokeMethod(this, &SessionViewerWindow::onReloadClicked, Qt::QueuedConnection);
        }
    });
}

bool SessionViewerWindow::ppkBusy() const
{
    return ppk_panel_ && ppk_panel_->busy();
}

void SessionViewerWindow::syncPpkSession(bool available)
{
    ppk_session_available_ = available;
    if (!available)
        navigation_refresh_pending_ = false;
    if (ppk_panel_)
        ppk_panel_->setSessionDirectory(available ? session_directory_ : QString());
    updatePpkSummary();
    updatePageAvailability();
}

void SessionViewerWindow::updatePpkSummary()
{
    using namespace VaporView::Ppk;
    const auto status = ppk_session_available_ ? PpkProcessor::status(session_directory_) : PpkStatus{};
    QString state = ppkStatusText(status, ppk_session_available_, ppkBusy(), is_english_);
    if (status.completed && !ppkBusy())
        state += QStringLiteral(" · FIX %1%").arg(status.quality.value("fix_percent").toDouble(), 0, 'f', 1);
    const bool ppk = ppk_session_available_ && sessionNavigationSource(session_directory_) == NavigationSource::Ppk;
    const QString source = !ppk_session_available_ ? QStringLiteral("---")
        : ppk ? (is_english_ ? QStringLiteral("PPK corrected") : QStringLiteral("PPK 修正"))
        : (is_english_ ? QStringLiteral("Original") : QStringLiteral("原始"));
    overview_page_->setPpkSummary(state, source);
}

bool SessionViewerWindow::loadSessionDirectory(QString sessionDirectory)
{
    if (session_loading_ || ppkBusy())
        return false;
    beginSessionLoading(is_english_ ? "Preparing to load session data..." : "正在准备加载会话数据...");
    const qint64 loadStartedMs = monotonicMilliseconds();
    qint64 lastStageMs = loadStartedMs;
    QStringList loadTimings;
    auto recordStageTiming = [&](const QString& stageName) {
        const qint64 now = monotonicMilliseconds();
        const qint64 stageMs = std::max<qint64>(0, now - lastStageMs);
        lastStageMs = now;
        loadTimings.push_back(QStringLiteral("%1 %2 ms").arg(stageName).arg(stageMs));
    };
    auto timingSummary = [&]() {
        if (loadTimings.isEmpty())
        {
            return QString();
        }
        const qint64 totalMs = std::max<qint64>(0, monotonicMilliseconds() - loadStartedMs);
        return QStringLiteral("%1 | %2 %3 ms")
            .arg(loadTimings.join(QStringLiteral(" | ")))
            .arg(is_english_ ? QStringLiteral("Total") : QStringLiteral("总计"))
            .arg(totalMs);
    };
    clearLoadedData(false);

    const QString normalized = QDir::fromNativeSeparators(sessionDirectory);
    session_directory_ = normalized;
    syncPpkSession(true);
    session_load_warning_.clear();
    updateSessionLoadingProgress(is_english_ ? "Reading session metadata..." : "正在读取会话元数据...", 3);
    if (!loadSessionMetadata(normalized))
    {
        finishSessionLoading();
        return false;
    }
    recordStageTiming(is_english_ ? QStringLiteral("Metadata") : QStringLiteral("元数据"));
    updateSessionLoadingProgress(is_english_ ? "Reading sensors CSV..." : "正在读取传感器 CSV...", 8);
    if (!loadSensorsCsv())
    {
        finishSessionLoading();
        return false;
    }
    recordStageTiming(is_english_ ? QStringLiteral("Sensors CSV") : QStringLiteral("传感器 CSV"));
    updateSessionLoadingProgress(is_english_ ? "Indexing waveform files..." : "正在索引波形文件...", 36);
    if (!loadWaveformSegments())
    {
        finishSessionLoading();
        return false;
    }
    recordStageTiming(is_english_ ? QStringLiteral("TCP/waveform index") : QStringLiteral("TCP/波形索引"));
    updateSessionLoadingProgress(is_english_ ? "Calculating waveform peak series..." : "正在计算波形峰值序列...", 45);
    if (!loadWaveformPeakSeries(true))
    {
        finishSessionLoading();
        return false;
    }
    recordStageTiming(is_english_ ? QStringLiteral("Peak series") : QStringLiteral("峰值序列"));

    updateSessionLoadingProgress(is_english_ ? "Updating viewer..." : "正在更新显示...", 98);
    overview_page_->setSessionPath(session_directory_);
    updateSummaryLabels();
    updateWaveformControls();

    if (total_waveform_frames_ > 0)
    {
        waveform_page_->setFrameValueSilently(1);
        loadWaveformFrame(0);
    }
    else
    {
        waveform_page_->setWaveformSamples({});
        waveform_page_->setFrameInfoText(is_english_ ? "No waveform frame file was found in this session."
                                                      : "这个会话里没有找到波形帧文件。");
    }

    recordStageTiming(is_english_ ? QStringLiteral("Viewer refresh") : QStringLiteral("界面刷新"));
    const QString summary = timingSummary();
    setProperty("_vvSessionLoadTimingSummary", summary);
    QString statusText = QString(is_english_ ? "Loaded session: %1" : "已加载会话: %1").arg(session_directory_);
    QString statusToolTip = summary;
    if (!session_load_warning_.isEmpty())
    {
        statusText += is_english_
            ? QStringLiteral(" — Warning: %1").arg(session_load_warning_)
            : QStringLiteral(" —— 警告：%1").arg(session_load_warning_);
        statusToolTip = statusToolTip.isEmpty()
            ? session_load_warning_
            : QStringLiteral("%1\n%2").arg(statusToolTip, session_load_warning_);
    }
    overview_page_->setStatusToolTip(statusToolTip);
    setStatusText(statusText);
    finishSessionLoading();
    return true;
}

bool SessionViewerWindow::loadSessionMetadata(const QString& sessionDirectory)
{
    const VaporView::Ground::SessionMetadataLoadResult result =
        VaporView::Ground::SessionLoader::loadMetadata(sessionDirectory);
    if (!result.success)
    {
        QMessageBox::warning(this,
                             is_english_ ? "Open Data" : "打开数据",
                             result.error);
        setStatusText(QString(is_english_ ? "Failed to load session metadata: %1"
                                          : "加载 session 元数据失败: %1")
                          .arg(result.error));
        return false;
    }

    const VaporView::Ground::SessionMetadata& metadata = result.metadata;
    metadata_filename_ = metadata.metadataFilename;
    recording_origin_ = metadata.recordingOrigin;
    session_name_ = metadata.sessionName;
    start_time_utc_ = metadata.startTimeUtc;
    end_time_utc_ = metadata.endTimeUtc;
    total_sensor_rows_ = metadata.sensorRows;
    total_waveform_frames_ = metadata.waveformFrames;
    points_per_frame_ = metadata.waveformPointsPerFrame;
    sensor_export_rate_hz_ = metadata.sensorExportRateHz;
    waveform_export_rate_hz_ = metadata.waveformExportRateHz;
    waveform_export_mode_ = metadata.waveformExportMode;
    sensors_csv_filename_ = metadata.sensorSummaryCsvFilename;
    waveform_directory_ = metadata.waveformDirectory;
    waveform_index_filename_ = metadata.waveformIndexFilename;
    waveform_peak_index_filename_ = metadata.waveformPeaksCsvFilename;
    waveform_raw_filename_ = metadata.waveformRawFilename;
    return true;
}

bool SessionViewerWindow::loadSensorsCsv()
{
    device_data_page_->clear();
    csv_headers_.clear();
    csv_timestamps_us_.clear();
    temperature_values_.clear();
    humidity_values_.clear();
    pressure_values_.clear();
    trajectory_controller_.clear();

    VaporView::Ground::SessionMetadata metadata;
    metadata.sessionDirectory = session_directory_;
    metadata.sensorSummaryCsvFilename = sensors_csv_filename_;
    metadata.sensorRows = total_sensor_rows_;
    // Keep the existing synchronous load API while parsing CSV and the selected
    // PPK trajectory on a worker. Paint/progress events remain responsive.
    QFutureWatcher<VaporView::Ground::SessionSensorLoadResult> watcher;
    QEventLoop waiting;
    connect(&watcher, &QFutureWatcher<VaporView::Ground::SessionSensorLoadResult>::finished,
            &waiting, &QEventLoop::quit);
    const QPointer<SessionViewerWindow> self(this);
    watcher.setFuture(QtConcurrent::run([metadata, self] {
        return VaporView::Ground::SessionLoader::loadSensors(metadata,
            [self](quint64 rowsRead, quint64 expectedRows) {
                if (!self) return;
                QMetaObject::invokeMethod(self, [self, rowsRead, expectedRows] {
                    if (!self || !self->session_loading_) return;
                    self->updateSessionLoadingProgress(
                        QString(self->is_english_ ? "Reading sensors CSV... %1 rows"
                                                : "正在读取传感器 CSV... %1 行").arg(rowsRead),
                        rangedProgressPercent(rowsRead, expectedRows, 8, 24));
                }, Qt::QueuedConnection);
            });
    }));
    waiting.exec(QEventLoop::ExcludeUserInputEvents);
    VaporView::Ground::SessionSensorLoadResult result = watcher.result();
    if (!result.success)
    {
        setStatusText(result.warning);
        return false;
    }
    if (!result.fileAvailable)
    {
        setStatusText(QString(is_english_ ? "Failed to open sensors CSV: %1"
                                          : "打开传感器 CSV 失败: %1")
                          .arg(sensors_csv_filename_));
        device_data_page_->setInfoText(is_english_
            ? QStringLiteral("The session metadata is valid, but sensors/sensor_summary.csv could not be opened.")
            : QStringLiteral("session 元数据是有效的，但 sensors/sensor_summary.csv 无法打开。"));
        return true;
    }
    if (result.data.headers.isEmpty())
    {
        device_data_page_->setInfoText(is_english_
            ? QStringLiteral("sensor_summary.csv is empty.")
            : QStringLiteral("sensor_summary.csv 为空。"));
        return true;
    }

    if (session_loading_)
    {
        updateSessionLoadingProgress(is_english_
            ? QStringLiteral("Preparing virtual CSV table...")
            : QStringLiteral("正在准备虚拟 CSV 表格..."),
            36);
    }

    VaporView::Ground::SessionSensorData& sensorData = result.data;
    csv_headers_ = sensorData.headers;
    csv_timestamps_us_ = std::move(sensorData.timestamps_us);
    temperature_values_ = std::move(sensorData.temperature_values);
    humidity_values_ = std::move(sensorData.humidity_values);
    pressure_values_ = std::move(sensorData.pressure_values);
    trajectory_controller_.setTrackData(
        std::move(sensorData.track_points),
        sensorData.track_stats);

    total_sensor_rows_ = static_cast<quint64>(sensorData.rows.size());
    device_data_page_->setRows(csv_headers_, std::move(sensorData.rows));
    device_data_page_->setInfoText(QString(is_english_
        ? "Loaded %1 CSV rows from %2"
        : "已从 %2 加载 %1 行 CSV")
        .arg(total_sensor_rows_)
        .arg(QDir::toNativeSeparators(sensors_csv_filename_)));

    waveform_page_->setEnvironmentSeries(
        temperature_values_,
        humidity_values_,
        pressure_values_,
        csv_timestamps_us_);
    const bool hasEnvironmentSeries =
        std::any_of(temperature_values_.cbegin(), temperature_values_.cend(), [](double value) { return std::isfinite(value); }) ||
        std::any_of(humidity_values_.cbegin(), humidity_values_.cend(), [](double value) { return std::isfinite(value); }) ||
        std::any_of(pressure_values_.cbegin(), pressure_values_.cend(), [](double value) { return std::isfinite(value); });
    updateRtkTrackPeakValues();
    waveform_page_->setEnvironmentInfoText(hasEnvironmentSeries
        ? (is_english_
            ? QStringLiteral("Loaded temperature, humidity, and pressure trend series.")
            : QStringLiteral("已加载温度、湿度和气压趋势。"))
        : (is_english_
            ? QStringLiteral("No temperature, humidity, or pressure columns were found in this CSV.")
            : QStringLiteral("这个 CSV 中没有找到温度、湿度或气压列。")));
    return true;
}

bool SessionViewerWindow::loadWaveformSegments()
{
    VaporView::Ground::SessionMetadata metadata;
    metadata.sessionDirectory = session_directory_;
    metadata.waveformDirectory = waveform_directory_;
    metadata.waveformIndexFilename = waveform_index_filename_;
    metadata.waveformPeaksCsvFilename = waveform_peak_index_filename_;
    metadata.waveformRawFilename = waveform_raw_filename_;
    metadata.waveformPointsPerFrame = points_per_frame_;

    // Keep GUI event handling out of the scan: main-window paints and timers
    // must not delay reading the next waveform record.
    QFutureWatcher<VaporView::Ground::SessionWaveformCatalogResult> watcher;
    QEventLoop waiting;
    connect(&watcher, &QFutureWatcher<VaporView::Ground::SessionWaveformCatalogResult>::finished,
            &waiting, &QEventLoop::quit);
    const QPointer<SessionViewerWindow> self(this);
    watcher.setFuture(QtConcurrent::run([metadata, self] {
        QElapsedTimer progressTimer;
        progressTimer.start();
        qint64 lastUpdateMs = -200;
        return VaporView::Ground::SessionWaveformRepository::loadCatalog(metadata,
            [self, &progressTimer, &lastUpdateMs](quint64 completed, quint64 total) {
                if (!self)
                {
                    return;
                }
                const qint64 now = progressTimer.elapsed();
                if (completed != total && now - lastUpdateMs < 200) return;
                lastUpdateMs = now;
                QMetaObject::invokeMethod(self, [self, completed, total] {
                    if (!self || !self->session_loading_) return;
                    self->updateSessionLoadingProgress(
                        QString(self->is_english_
                            ? "Indexing waveform data... %1/%2"
                            : "正在索引波形数据... %1/%2")
                            .arg(completed)
                            .arg(total),
                        rangedProgressPercent(completed, total, 36, 45));
                }, Qt::QueuedConnection);
            });
    }));
    waiting.exec(QEventLoop::ExcludeUserInputEvents);
    VaporView::Ground::SessionWaveformCatalogResult result = watcher.result();
    if (!result.success)
    {
        setStatusText(result.error);
        return false;
    }

    session_load_warning_ = result.warning;
    waveform_catalog_ = std::move(result.catalog);
    total_waveform_frames_ = waveform_catalog_.frameCount;
    points_per_frame_ = waveform_catalog_.pointsPerFrame;
    if (waveform_catalog_.isEmpty() &&
        !QFileInfo::exists(waveform_raw_filename_) &&
        !QFileInfo::exists(waveform_directory_))
    {
        setStatusText(is_english_
            ? QStringLiteral("No raw TCP wave file or legacy waveform directory was found.")
            : QStringLiteral("没有找到 raw TCP 波形文件，也没有找到旧版 waveform 目录。"));
    }
    return true;
}
void SessionViewerWindow::applyPeakFilter(int startPercent, int endPercent)
{
    if (session_loading_)
    {
        updateSessionLoadingProgress(
            is_english_ ? "Applying peak filter..." : "正在应用峰值过滤...",
            std::clamp(startPercent, 0, 100));
    }
    waveform_peak_values_ = VaporView::Ground::SessionWaveformRepository::applyPeakFilter(
        waveform_peak_raw_values_,
        peak_filter_settings_);
    waveform_page_->setPeakValues(waveform_peak_values_, waveform_timestamps_us_);
    updateRtkTrackPeakValues();
    if (waveform_page_->frameValue() > 0)
    {
        loadWaveformFrame(static_cast<quint64>(waveform_page_->frameValue() - 1));
    }
    if (session_loading_)
    {
        updateSessionLoadingProgress(
            is_english_ ? "Refreshing filtered plots..." : "正在刷新过滤后的图表...",
            std::clamp(endPercent, 0, 100));
    }
}
bool SessionViewerWindow::loadWaveformPeakSeries(bool allowBackground)
{
    waveform_peak_raw_values_.clear();
    waveform_peak_values_.clear();
    waveform_timestamps_us_.clear();
    waveform_page_->setPeakValues({});
    waveform_page_->setCurrentPeakFrame(-1);

    if (waveform_catalog_.isEmpty())
    {
        return true;
    }

    if (isFullFramePeakSearch(peak_search_start_index_, peak_search_end_index_))
    {
        VaporView::Ground::SessionWaveformPeakSeriesResult cached =
            VaporView::Ground::SessionWaveformRepository::loadCachedPeakSeries(waveform_catalog_);
        if (cached.success)
        {
            waveform_timestamps_us_ = std::move(cached.timestampsUs);
            waveform_peak_raw_values_ = std::move(cached.peakValues);
            applyPeakFilter(90, 97);
            return true;
        }
    }

    if (allowBackground)
    {
        startBackgroundWaveformPeakSeries();
        return true;
    }

    VaporView::Ground::SessionWaveformPeakSeriesResult result =
        VaporView::Ground::SessionWaveformRepository::calculatePeakSeries(
            waveform_catalog_,
            peak_search_start_index_,
            peak_search_end_index_);
    if (!result.success)
    {
        setStatusText(QString(is_english_
            ? "Failed to calculate waveform peak series: %1"
            : "计算波形峰值序列失败: %1").arg(result.error));
        return false;
    }
    waveform_timestamps_us_ = std::move(result.timestampsUs);
    waveform_peak_raw_values_ = std::move(result.peakValues);
    if (isFullFramePeakSearch(peak_search_start_index_, peak_search_end_index_))
    {
        VaporView::Ground::SessionWaveformRepository::writeCachedPeakSeries(
            waveform_catalog_,
            waveform_timestamps_us_,
            waveform_peak_raw_values_);
    }
    applyPeakFilter(90, 97);
    return true;
}

void SessionViewerWindow::startBackgroundWaveformPeakSeries()
{
    cancelBackgroundWaveformPeakSeries(false);
    const quint64 requestId = ++peak_series_request_id_;
    const QString sessionDirectory = session_directory_;
    const int searchStartIndex = peak_search_start_index_;
    const int searchEndIndex = peak_search_end_index_;
    const bool fullFrameSearch = isFullFramePeakSearch(searchStartIndex, searchEndIndex);
    const VaporView::Ground::SessionWaveformCatalog catalog = waveform_catalog_;
    auto cancelFlag = std::make_shared<std::atomic_bool>(false);
    peak_series_cancel_flag_ = cancelFlag;

    peak_series_watcher_ =
        new QFutureWatcher<VaporView::Ground::SessionWaveformPeakSeriesResult>(this);
    connect(peak_series_watcher_,
            &QFutureWatcher<VaporView::Ground::SessionWaveformPeakSeriesResult>::finished,
            this,
            [this, requestId, sessionDirectory, fullFrameSearch]() {
        auto *watcher = peak_series_watcher_;
        if (!watcher)
        {
            return;
        }
        const VaporView::Ground::SessionWaveformPeakSeriesResult result = watcher->result();
        peak_series_watcher_ = nullptr;
        watcher->deleteLater();
        if (requestId != peak_series_request_id_ || sessionDirectory != session_directory_)
        {
            return;
        }
        if (!result.success)
        {
            if (!result.cancelled)
            {
                setStatusText(QString(is_english_
                    ? "Failed to calculate waveform peak series: %1"
                    : "计算波形峰值序列失败: %1").arg(result.error));
            }
            return;
        }

        waveform_timestamps_us_ = result.timestampsUs;
        waveform_peak_raw_values_ = result.peakValues;
        if (fullFrameSearch)
        {
            VaporView::Ground::SessionWaveformRepository::writeCachedPeakSeries(
                waveform_catalog_,
                waveform_timestamps_us_,
                waveform_peak_raw_values_);
        }
        applyPeakFilter();
        updateSummaryLabels();
        syncPeakSettingsToTrajectoryViewer();
        setStatusText(QString(is_english_
            ? "Loaded session: %1 (waveform peaks ready)"
            : "已加载会话: %1（波形峰值已就绪）").arg(session_directory_));
    });

    peak_series_watcher_->setFuture(QtConcurrent::run(
        [catalog, searchStartIndex, searchEndIndex, cancelFlag]() {
            return VaporView::Ground::SessionWaveformRepository::calculatePeakSeries(
                catalog,
                searchStartIndex,
                searchEndIndex,
                cancelFlag);
        }));
}

void SessionViewerWindow::cancelBackgroundWaveformPeakSeries(bool waitForFinished)
{
    Q_UNUSED(waitForFinished);
    ++peak_series_request_id_;
    if (peak_series_cancel_flag_)
    {
        peak_series_cancel_flag_->store(true, std::memory_order_relaxed);
        peak_series_cancel_flag_.reset();
    }
    if (!peak_series_watcher_)
    {
        return;
    }
    auto *watcher = peak_series_watcher_;
    peak_series_watcher_ = nullptr;
    disconnect(watcher, nullptr, this, nullptr);
    delete watcher;
}
void SessionViewerWindow::updateSummaryLabels()
{
    const bool hasSession = !session_name_.isEmpty() || !metadata_filename_.isEmpty();
    SessionOverviewSummary summary;
    summary.sessionName = session_name_.isEmpty() ? QStringLiteral("---") : session_name_;
    summary.recordingOrigin = hasSession
        ? VaporView::Session::recordingOriginDisplayText(recording_origin_, is_english_)
        : QStringLiteral("---");
    summary.startTime = VaporView::formatSessionMetadataTimeBeijing(start_time_utc_);
    summary.endTime = VaporView::formatSessionMetadataTimeBeijing(end_time_utc_);
    summary.duration = hasSession
        ? VaporView::formatSessionDurationText(start_time_utc_, end_time_utc_, is_english_)
        : QStringLiteral("---");
    summary.sensorRate = hasSession
        ? formatSessionMeasuredRateText(csv_timestamps_us_, sensor_export_rate_hz_, QStringLiteral("fixed_rate"), is_english_)
        : QStringLiteral("---");
    summary.sensorRows = hasSession ? QString::number(total_sensor_rows_) : QStringLiteral("---");
    summary.waveformRate = hasSession
        ? formatSessionMeasuredRateText(waveform_timestamps_us_, waveform_export_rate_hz_, waveform_export_mode_, is_english_)
        : QStringLiteral("---");
    summary.waveformFiles = hasSession
        ? QString::number(waveform_catalog_.sourceFileCount())
        : QStringLiteral("---");
    summary.waveformFrames = hasSession
        ? QString::number(total_waveform_frames_)
        : QStringLiteral("---");
    overview_page_->setSummary(summary);
    updatePpkSummary();
}

void SessionViewerWindow::updateWaveformControls()
{
    const bool hasFrames = total_waveform_frames_ > 0 && !waveform_catalog_.isEmpty();
    waveform_page_->configureFrames(hasFrames ? total_waveform_frames_ : 0);
    if (hasFrames)
    {
        const int maximum = static_cast<int>(std::min<quint64>(
            total_waveform_frames_,
            static_cast<quint64>(std::numeric_limits<int>::max())));
        playback_controller_->setTimeline(maximum, waveform_timestamps_us_);
    }
    else
    {
        playback_controller_->clear();
    }
}

void SessionViewerWindow::onFrameSliderMoved(int value)
{
    if (updating_frame_controls_)
    {
        return;
    }

    updating_frame_controls_ = true;
    waveform_page_->setFrameValueSilently(value);
    updating_frame_controls_ = false;

    if (value > 0)
    {
        previewWaveformFrame(static_cast<quint64>(value - 1));
    }
}

void SessionViewerWindow::onFrameSliderChanged(int value)
{
    if (updating_frame_controls_)
    {
        return;
    }

    updating_frame_controls_ = true;
    waveform_page_->setFrameValueSilently(value);
    updating_frame_controls_ = false;
    if (value > 0)
    {
        if (playback_controller_->currentFrame() == value - 1)
        {
            // A drag back to the committed frame still leaves preview text.
            loadWaveformFrame(static_cast<quint64>(value - 1));
        }
        else
        {
            playback_controller_->seek(value - 1);
        }
    }
}

void SessionViewerWindow::onFrameSpinChanged(int value)
{
    if (updating_frame_controls_)
    {
        return;
    }

    updating_frame_controls_ = true;
    waveform_page_->setFrameValueSilently(value);
    updating_frame_controls_ = false;
    if (value > 0)
    {
        playback_controller_->seek(value - 1);
    }
}

QVector<float> SessionViewerWindow::visibleWaveformSamples(const QVector<float>& samples, int& firstSampleIndex) const
{
    firstSampleIndex = 0;
    if (!waveform_show_filtered_frame_ || samples.isEmpty())
    {
        return samples;
    }

    const int sampleCount = static_cast<int>(samples.size());
    const int startIndex = std::clamp(peak_search_start_index_, 0, sampleCount);
    const int endIndex = std::clamp(peak_search_end_index_, 0, sampleCount);
    if (startIndex >= endIndex)
    {
        return samples;
    }

    firstSampleIndex = startIndex;
    return samples.mid(startIndex, endIndex - startIndex);
}

void SessionViewerWindow::onToggleWaveformFrameFilterClicked()
{
    waveform_show_filtered_frame_ = !waveform_show_filtered_frame_;
    updateWaveformActionTexts();
    int firstSampleIndex = 0;
    waveform_page_->setWaveformSamples(
        visibleWaveformSamples(current_waveform_frame_samples_, firstSampleIndex),
        firstSampleIndex);
}

void SessionViewerWindow::onTogglePeakPlotModeClicked()
{
    beginSessionLoading(waveform_peak_scatter_mode_
        ? (is_english_ ? "Switching to polyline plots..." : "正在切换到折线图...")
        : (is_english_ ? "Switching to scatter plots..." : "正在切换到散点图..."));
    waveform_peak_scatter_mode_ = !waveform_peak_scatter_mode_;
    updateWaveformActionTexts();
    waveform_page_->setPlotMode(waveform_peak_scatter_mode_);
    updateSessionLoadingProgress(is_english_ ? "Refreshing plots..." : "正在刷新图表...", 80);
    waveform_page_->repaintPlots();
    finishSessionLoading();
}

void SessionViewerWindow::onConfigurePeakFilterClicked()
{
    SessionPeakSettingsInput input;
    if (!editSessionPeakSettings(
            this,
            is_english_,
            peak_search_start_index_,
            peak_search_end_index_,
            peak_filter_settings_,
            input))
    {
        return;
    }

    applyPeakSettings(
        input.searchStartIndex,
        input.searchEndIndex,
        input.filter.mode,
        input.filter.minValue,
        input.filter.maxValue,
        input.hasMinValue,
        input.hasMaxValue,
        is_english_ ? QStringLiteral("Recalculating waveform peak series...") : QStringLiteral("正在重新计算波形峰值序列..."),
        is_english_ ? QStringLiteral("Applying peak filter...") : QStringLiteral("正在应用峰值过滤..."));
}

bool SessionViewerWindow::readWaveformFrameSamples(
    quint64 frameIndex,
    quint64& timestampUs,
    QVector<float>& samples)
{
    VaporView::Ground::SessionWaveformFrameResult result =
        VaporView::Ground::SessionWaveformRepository::readFrame(
            waveform_catalog_,
            frameIndex);
    if (!result.success)
    {
        setStatusText(result.error);
        timestampUs = 0;
        samples.clear();
        return false;
    }
    timestampUs = result.timestampUs;
    samples = std::move(result.samples);
    return true;
}
bool SessionViewerWindow::previewWaveformFrame(quint64 frameIndex)
{
    quint64 timestampUs = 0;
    QVector<float> samples;
    if (!readWaveformFrameSamples(frameIndex, timestampUs, samples))
    {
        return false;
    }

    current_waveform_frame_samples_ = samples;
    int firstSampleIndex = 0;
    waveform_page_->setWaveformSamples(visibleWaveformSamples(samples, firstSampleIndex), firstSampleIndex);
    waveform_page_->setCurrentPeakFrame(static_cast<int>(frameIndex));
    const int previewCsvRow = timestampUs == 0 ? -1 : findClosestCsvRow(timestampUs);
    waveform_page_->setEnvironmentCurrentIndex(previewCsvRow, is_english_);
    previewClosestSensorRow(timestampUs);
    waveform_page_->setFramePreviewInfo(frameIndex, total_waveform_frames_, is_english_, timestampUs);
    return true;
}

bool SessionViewerWindow::loadWaveformFrame(quint64 frameIndex, bool scrollToCsvRow)
{
    quint64 timestampUs = 0;
    QVector<float> samples;
    if (!readWaveformFrameSamples(frameIndex, timestampUs, samples))
    {
        return false;
    }

    current_waveform_frame_samples_ = samples;
    int firstSampleIndex = 0;
    waveform_page_->setWaveformSamples(visibleWaveformSamples(samples, firstSampleIndex), firstSampleIndex);
    waveform_page_->setCurrentPeakFrame(static_cast<int>(frameIndex));

    const auto minMax = std::minmax_element(samples.cbegin(), samples.cend());
    const float rawPeakValue = frameIndex < static_cast<quint64>(waveform_peak_raw_values_.size())
        ? waveform_peak_raw_values_.at(static_cast<int>(frameIndex))
        : waveformPeakValue(samples, peak_search_start_index_, peak_search_end_index_);
    const float filteredPeakValue = frameIndex < static_cast<quint64>(waveform_peak_values_.size())
        ? waveform_peak_values_.at(static_cast<int>(frameIndex))
        : rawPeakValue;
    const QString csvMatchText = highlightClosestSensorRow(timestampUs, scrollToCsvRow);
    const QString sourceFilename = waveform_catalog_.sourceFilename(frameIndex);
    waveform_page_->setFrameDetails(
        frameIndex,
        total_waveform_frames_,
        timestampUs,
        waveform_export_mode_,
        waveform_export_rate_hz_,
        *minMax.first,
        *minMax.second,
        filteredPeakValue,
        sourceFilename,
        csvMatchText,
        is_english_);
    return true;
}

int SessionViewerWindow::findClosestCsvRow(quint64 timestampUs) const
{
    return VaporView::Ground::Session::closestTimestampIndex(csv_timestamps_us_, timestampUs);
}

void SessionViewerWindow::updateRtkTrackPeakValues()
{
    trajectory_controller_.attachWaveformPeaks(
        waveform_timestamps_us_,
        waveform_peak_values_);
    map_coordinator_->updateTrack(
        trajectory_controller_.trackPoints(),
        trajectory_controller_.trackStats());
}

void SessionViewerWindow::focusTrajectoryPoint(int trackPointIndex)
{
    const VaporView::Ground::SessionTrajectoryFocus focus =
        trajectory_controller_.focusForPoint(trackPointIndex);
    if (!focus.valid)
    {
        return;
    }

    if (focus.waveformFrameIndex >= 0)
    {
        const int frameValue = focus.waveformFrameIndex + 1;
        if (waveform_page_->frameValueInRange(frameValue))
        {
            waveform_page_->setFrameValueSilently(frameValue);
        }
        loadWaveformFrame(static_cast<quint64>(focus.waveformFrameIndex));
    }
    else if (focus.timestampUs > 0)
    {
        highlightClosestSensorRow(focus.timestampUs, true);
    }

    setStatusText(QString(is_english_
        ? "Focused trajectory point #%1 at CSV row %2."
        : "已定位轨迹点 #%1，对应 CSV 第 %2 行。")
        .arg(trackPointIndex + 1)
        .arg(focus.csvRow >= 0 ? focus.csvRow + 1 : 0));
}

void SessionViewerWindow::syncEnvironmentRangeToWaveformRange(
    int startFrameIndex,
    int visibleFrameCount)
{
    const VaporView::Ground::SessionTimelineRange range =
        trajectory_controller_.sensorRangeForWaveformRange(
            csv_timestamps_us_,
            waveform_timestamps_us_,
            startFrameIndex,
            visibleFrameCount);
    waveform_page_->setEnvironmentRange(
        range.valid ? range.startIndex : 0,
        range.valid ? range.count : 0);
}

void SessionViewerWindow::previewClosestSensorRow(quint64 timestampUs)
{
    highlightClosestSensorRow(timestampUs, true);
}

QString SessionViewerWindow::highlightClosestSensorRow(
    quint64 timestampUs,
    bool scrollToCsvRow)
{
    const SessionCsvHighlightResult result =
        device_data_page_->highlightTimestamp(
            csv_timestamps_us_,
            timestampUs,
            scrollToCsvRow);
    waveform_page_->setEnvironmentCurrentIndex(result.primaryRow, is_english_);
    return result.description;
}
