#include "SessionViewerWindow.h"
#include "ground/session/SessionMapCoordinator.h"
#include "ground/session/SessionViewerPages.h"
#include "ground/widgets/CustomTitleBar.h"
#include "ground/wave/RawDataParserWindow.h"
#include "SessionTimeFormat.h"
#include "ground/widgets/WindowSizing.h"
#include "ground/session/SessionLoader.h"
#include "ground/session/SessionIndex.h"
#include "ground/session/SessionPlaybackController.h"
#include "ground/session/SessionWaveformRepository.h"

#include <QDir>
#include <QElapsedTimer>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QMessageBox>
#include <QScrollArea>
#include <QSettings>
#include "shared/config/SettingsWriteBarrier.h"
#include <QSplitter>
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

constexpr int kDefaultPeakSearchStartIndex = 0;
constexpr int kDefaultPeakSearchEndIndex = 0;
constexpr int kSessionViewerDefaultWidth = 1280;
constexpr int kSessionViewerDefaultHeight = 800;

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
    , default_data_directory_()
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
    resize(kSessionViewerDefaultWidth, kSessionViewerDefaultHeight);
    setEnglish(false);
    VaporView::centerWindowOnScreen(this, parent);

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
    default_data_directory_ = QDir::fromNativeSeparators(settings.value("default_data_directory").toString());
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
    if (raw_data_parser_window_)
    {
        delete raw_data_parser_window_;
        raw_data_parser_window_ = nullptr;
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
    setCentralWidget(scrollArea);

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
    connect(overview_page_, &SessionOverviewWidget::trajectoryRequested,
            this, &SessionViewerWindow::onViewTrajectoryClicked);
    connect(overview_page_, &SessionOverviewWidget::rawDataParserRequested,
            this, &SessionViewerWindow::onRawDataParserClicked);
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
                  event->type() == QEvent::StyleChange))
    {
        device_data_page_->applyTheme();
        loading_dialog_->applyTheme();
    }
}

void SessionViewerWindow::setDefaultDataDirectory(const QString& directory)
{
    const QString normalized = QDir::fromNativeSeparators(directory.trimmed());
    default_data_directory_ = normalized;
    if (!normalized.isEmpty())
    {
        QSettings settings("VaporView", "SessionViewer");
        VaporView::setPersistentSetting(settings, QStringLiteral("default_data_directory"), normalized);
    }
}

void SessionViewerWindow::setUiTestMode(bool enabled)
{
    if (ui_test_mode_ == enabled)
    {
        return;
    }
    if (enabled)
    {
        ui_test_saved_default_data_directory_ = default_data_directory_;
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
    default_data_directory_ = ui_test_saved_default_data_directory_;
    peak_filter_settings_ = ui_test_saved_peak_filter_settings_;
    peak_search_start_index_ = ui_test_saved_peak_search_start_index_;
    peak_search_end_index_ = ui_test_saved_peak_search_end_index_;
    updateWaveformControls();
    updateWaveformActionTexts();
    syncPeakSettingsToTrajectoryViewer();
}

void SessionViewerWindow::updateTexts()
{
    setWindowTitle(is_english_ ? QStringLiteral("Data Viewer") : QStringLiteral("数据查看器"));
    overview_page_->setEnglish(is_english_);
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
    overview_page_->setTrajectoryAvailable(trajectory_controller_.hasTrack());
    waveform_page_->setControlsEnabled(enabled);
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
}

void SessionViewerWindow::clearLoadedData(bool clearPathEdit)
{
    cancelBackgroundWaveformPeakSeries(false);
    session_directory_.clear();
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
    overview_page_->setTrajectoryAvailable(false);
    map_coordinator_->updateTrack({}, trajectory_controller_.trackStats());
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
    QSettings settings("VaporView", "SessionViewer");
    QString initialDir = default_data_directory_;
    if (initialDir.isEmpty())
    {
        initialDir = settings.value("default_data_directory").toString();
    }
    if (initialDir.isEmpty())
    {
        const QString lastSession = settings.value("last_session_directory").toString();
        const QString lastSessionDirectory = resolveSessionDirectory(lastSession);
        if (!lastSessionDirectory.isEmpty())
        {
            initialDir = QFileInfo(lastSessionDirectory).absolutePath();
        }
    }
    if (initialDir.isEmpty() || !QFileInfo(initialDir).isDir())
    {
        initialDir = QDir::currentPath();
    }
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
    if (session_directory_.isEmpty())
    {
        QSettings settings("VaporView", "SessionViewer");
        const QString lastSessionDirectory = resolveSessionDirectory(settings.value("last_session_directory").toString());
        if (lastSessionDirectory.isEmpty())
        {
            setStatusText(is_english_ ? "No session is currently loaded." : "当前没有已加载的会话。");
            return;
        }
        session_directory_ = lastSessionDirectory;
        overview_page_->setSessionPath(session_directory_);
    }

    loadSessionDirectory(session_directory_);
}

void SessionViewerWindow::onClearViewClicked()
{
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
    if (!trajectory_controller_.hasTrack())
    {
        QMessageBox::information(this,
            is_english_ ? QStringLiteral("RTK Trajectory") : QStringLiteral("RTK轨迹"),
            is_english_ ? QStringLiteral("No valid RTK latitude/longitude samples were found in the current session.")
                        : QStringLiteral("当前会话中没有找到有效的 RTK 经纬度轨迹点。"));
        return;
    }

    if (!ensureTrajectoryPeakValuesReady())
    {
        QMessageBox::warning(this,
            is_english_ ? QStringLiteral("RTK Trajectory") : QStringLiteral("RTK轨迹"),
            is_english_ ? QStringLiteral("Failed to prepare waveform peak values for the trajectory viewer.")
                        : QStringLiteral("无法为轨迹查看器准备波形峰值。"));
        return;
    }

    syncPeakSettingsToTrajectoryViewer();
    map_coordinator_->showTrajectory(
        this,
        trajectory_controller_.trackPoints(),
        trajectory_controller_.trackStats());
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
    if (session_directory_.isEmpty())
    {
        QMessageBox::information(this,
            is_english_ ? "Raw Data Parser" : "原始数据解析器",
            is_english_ ? "Choose or restore a session directory first."
                        : "请先选择或恢复一个 session 目录。");
        return;
    }

    if (!raw_data_parser_window_)
    {
        raw_data_parser_window_ = new RawDataParserWindow();
        raw_data_parser_window_->setAttribute(Qt::WA_QuitOnClose, false);
        raw_data_parser_window_->setAttribute(Qt::WA_DeleteOnClose, false);
        connect(raw_data_parser_window_, &QObject::destroyed, this, [this]() {
            raw_data_parser_window_ = nullptr;
        });
    }

    raw_data_parser_window_->setEnglish(is_english_);
    VaporView::centerWindowOnScreen(raw_data_parser_window_, this);
    raw_data_parser_window_->show();
    raw_data_parser_window_->raise();
    raw_data_parser_window_->activateWindow();
    raw_data_parser_window_->openSessionPath(session_directory_);
}

bool SessionViewerWindow::loadSessionDirectory(QString sessionDirectory)
{
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
        loadWaveformFrame(0, false);
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
    metadata.sensorSummaryCsvFilename = sensors_csv_filename_;
    metadata.sensorRows = total_sensor_rows_;
    VaporView::Ground::SessionSensorLoadResult result =
        VaporView::Ground::SessionLoader::loadSensors(
            metadata,
            [this](quint64 rowsRead, quint64 expectedRows) {
                if (!session_loading_)
                {
                    return;
                }
                updateSessionLoadingProgress(
                    QString(is_english_ ? "Reading sensors CSV... %1 rows"
                                        : "正在读取传感器 CSV... %1 行")
                        .arg(rowsRead),
                    rangedProgressPercent(rowsRead, expectedRows, 8, 24));
            });
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
        pressure_values_);
    const bool hasEnvironmentSeries =
        std::any_of(temperature_values_.cbegin(), temperature_values_.cend(), [](double value) { return std::isfinite(value); }) ||
        std::any_of(humidity_values_.cbegin(), humidity_values_.cend(), [](double value) { return std::isfinite(value); }) ||
        std::any_of(pressure_values_.cbegin(), pressure_values_.cend(), [](double value) { return std::isfinite(value); });
    updateRtkTrackPeakValues();
    overview_page_->setTrajectoryAvailable(trajectory_controller_.hasTrack());
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

    VaporView::Ground::SessionWaveformCatalogResult result =
        VaporView::Ground::SessionWaveformRepository::loadCatalog(
            metadata,
            [this](quint64 completed, quint64 total) {
                if (!session_loading_)
                {
                    return;
                }
                updateSessionLoadingProgress(
                    QString(is_english_
                        ? "Indexing waveform data... %1/%2"
                        : "正在索引波形数据... %1/%2")
                        .arg(completed)
                        .arg(total),
                    rangedProgressPercent(completed, total, 36, 45));
            });
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
    waveform_page_->setPeakValues(waveform_peak_values_);
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
        playback_controller_->seek(value - 1);
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
    waveform_page_->setFramePreviewInfo(frameIndex, total_waveform_frames_, is_english_);
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
