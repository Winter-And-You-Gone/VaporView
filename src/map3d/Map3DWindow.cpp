#include "map3d/Map3DWindow.h"

#include "geo/SessionTrackReader.h"
#include "geo/TrajectoryQuality.h"
#include "map3d/OsgEarthViewWidget.h"

#include <QAction>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QSettings>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <limits>

namespace VaporView::Map3D {
namespace {

constexpr qint64 kStatusUpdateIntervalMs = 200;

QString heightReferenceUncheckedNote()
{
    return QStringLiteral("height reference unchecked; no AGL/terrain-clearance decision");
}

QString heightReferenceLabel(VaporView::Geo::HeightReference reference)
{
    switch (reference)
    {
    case VaporView::Geo::HeightReference::Wgs84Ellipsoid:
        return QStringLiteral("WGS84 ellipsoid");
    case VaporView::Geo::HeightReference::MeanSeaLevel:
        return QStringLiteral("MSL");
    case VaporView::Geo::HeightReference::Egm2008:
        return QStringLiteral("EGM2008");
    case VaporView::Geo::HeightReference::LocalNed:
        return QStringLiteral("local NED");
    case VaporView::Geo::HeightReference::Unknown:
        return QStringLiteral("unknown");
    }
    return QStringLiteral("unknown");
}

QString attitudeSourceLabel(const VaporView::Geo::NavSample* sample)
{
    if (!sample)
    {
        return QStringLiteral("none");
    }
    if (sample->hasQuaternion())
    {
        return QStringLiteral("Quaternion");
    }
    if (std::isfinite(sample->rollDeg)
        || std::isfinite(sample->pitchDeg)
        || std::isfinite(sample->yawDeg))
    {
        return QStringLiteral("Euler");
    }
    return QStringLiteral("none");
}

bool isMap3DHeadlessTest()
{
    return qEnvironmentVariableIsSet("VAPORVIEW_MAP3D_HEADLESS_TEST");
}

QString availabilityLabel(bool available)
{
    return available ? QStringLiteral("available") : QStringLiteral("missing");
}

QString selectedDemLabel(const MapDataDiagnostics& diagnostics)
{
    if (!diagnostics.selectedDemLayerAvailable)
    {
        return QStringLiteral("none");
    }
    if (diagnostics.selectedElevationSource.isEmpty())
    {
        return QStringLiteral("available");
    }
    return diagnostics.selectedElevationSource;
}

QString selectedOsmLabel(const MapDataDiagnostics& diagnostics)
{
    if (!diagnostics.selectedOsmLayersAvailable)
    {
        return QStringLiteral("not selected (%1/4 files)").arg(diagnostics.osmLayerCount);
    }
    return QStringLiteral("%1 layers").arg(diagnostics.selectedOsmLayerCount);
}

QString fileAvailabilityLabel(bool available, const QString& path)
{
    return QStringLiteral("%1 - %2")
        .arg(available ? QStringLiteral("available") : QStringLiteral("missing"), path);
}

QString imageryOptionLabel(const LocalImageryOption& option)
{
    return QStringLiteral("%1 - %2")
        .arg(option.label,
             option.available ? QStringLiteral("available") : QStringLiteral("missing VRT/template"));
}

int sanitizeMaxVisibleSamples(int value)
{
    return std::clamp(value, 1000, 1000000);
}

int firstVisibleIndex(const std::vector<VaporView::Geo::NavSample>& samples, int maxVisibleSamples)
{
    return std::max(0, static_cast<int>(samples.size()) - sanitizeMaxVisibleSamples(maxVisibleSamples));
}

bool isVisibleJumpSample(const std::vector<VaporView::Geo::NavSample>& samples, int index)
{
    if (index <= 0)
    {
        return false;
    }
    return VaporView::Geo::isLikelyJump(samples[static_cast<std::size_t>(index - 1)],
                                        samples[static_cast<std::size_t>(index)]);
}

TrajectoryQualityStats qualityStatsForSamples(const std::vector<VaporView::Geo::NavSample>& samples,
                                              int maxVisibleSamples)
{
    TrajectoryQualityStats stats;
    for (int index = firstVisibleIndex(samples, maxVisibleSamples); index < static_cast<int>(samples.size()); ++index)
    {
        const VaporView::Geo::NavSample& sample = samples[static_cast<std::size_t>(index)];
        const bool usable = VaporView::Geo::isUsableForDisplay(sample);
        const bool jump = usable && isVisibleJumpSample(samples, index);
        if (!usable)
        {
            ++stats.invalidSamples;
            ++stats.markerSamples;
            continue;
        }
        if (jump)
        {
            ++stats.jumpSamples;
            ++stats.markerSamples;
            continue;
        }

        ++stats.lineSamples;
        switch (sample.fixQuality)
        {
        case VaporView::Geo::FixQuality::Fixed:
            ++stats.fixedSamples;
            break;
        case VaporView::Geo::FixQuality::Float:
            ++stats.floatSamples;
            break;
        case VaporView::Geo::FixQuality::Dgps:
            ++stats.dgpsSamples;
            break;
        case VaporView::Geo::FixQuality::Single:
            ++stats.singleSamples;
            break;
        case VaporView::Geo::FixQuality::Invalid:
            ++stats.invalidSamples;
            ++stats.markerSamples;
            --stats.lineSamples;
            break;
        case VaporView::Geo::FixQuality::Unknown:
            ++stats.unknownSamples;
            break;
        }
    }
    return stats;
}

QString qualityStatsSummary(const TrajectoryQualityStats& stats)
{
    return QStringLiteral("Fixed %1 Float %2 DGPS %3 Single %4 Unknown %5 Invalid %6 Jump %7")
        .arg(stats.fixedSamples)
        .arg(stats.floatSamples)
        .arg(stats.dgpsSamples)
        .arg(stats.singleSamples)
        .arg(stats.unknownSamples)
        .arg(stats.invalidSamples)
        .arg(stats.jumpSamples);
}

} // namespace

Map3DWindow::Map3DWindow(QWidget* parent)
    : QMainWindow(parent)
    , status_label_(new QLabel(this))
    , replay_timer_(new QTimer(this))
    , latest_track_source_(QStringLiteral("none"))
{
    setObjectName(QStringLiteral("map3DWindow"));
    setWindowTitle(QStringLiteral("VaporView 3D Map"));
    setAttribute(Qt::WA_QuitOnClose, false);
    resize(1100, 760);
    if (isMap3DHeadlessTest())
    {
        headless_view_ = new QWidget(this);
        headless_view_->setMinimumSize(640, 420);
        headless_view_->setObjectName(QStringLiteral("map3DView"));
        setCentralWidget(headless_view_);
    }
    else
    {
        view_ = new OsgEarthViewWidget(this);
        view_->setObjectName(QStringLiteral("map3DView"));
        setCentralWidget(view_);
    }
    status_label_->setObjectName(QStringLiteral("map3DStatusLabel"));

    QToolBar* toolbar = addToolBar(QStringLiteral("3D Map"));
    QAction* openSessionAction = toolbar->addAction(QStringLiteral("打开 Session"));
    connect(openSessionAction, &QAction::triggered, this, &Map3DWindow::openSessionDirectory);

    QAction* clearAction = toolbar->addAction(QStringLiteral("清空轨迹"));
    connect(clearAction, &QAction::triggered, this, &Map3DWindow::clearTrack);

    replay_action_ = toolbar->addAction(QStringLiteral("播放"));
    replay_action_->setObjectName(QStringLiteral("map3DReplayAction"));
    replay_action_->setCheckable(true);
    replay_action_->setEnabled(false);
    connect(replay_action_, &QAction::triggered, this, &Map3DWindow::toggleReplay);

    replay_stop_action_ = toolbar->addAction(QStringLiteral("停止回放"));
    replay_stop_action_->setObjectName(QStringLiteral("map3DReplayStopAction"));
    replay_stop_action_->setEnabled(false);
    connect(replay_stop_action_, &QAction::triggered, this, &Map3DWindow::stopReplay);

    replay_speed_combo_ = new QComboBox(toolbar);
    replay_speed_combo_->setObjectName(QStringLiteral("map3DReplaySpeedCombo"));
    replay_speed_combo_->addItems({QStringLiteral("0.5x"),
                                   QStringLiteral("1x"),
                                   QStringLiteral("2x"),
                                   QStringLiteral("5x"),
                                   QStringLiteral("10x")});
    toolbar->addWidget(replay_speed_combo_);

    replay_slider_ = new QSlider(Qt::Horizontal, toolbar);
    replay_slider_->setObjectName(QStringLiteral("map3DReplaySlider"));
    replay_slider_->setMinimumWidth(180);
    replay_slider_->setEnabled(false);
    replay_slider_->setTracking(true);
    toolbar->addWidget(replay_slider_);
    connect(replay_slider_, &QSlider::sliderMoved, this, &Map3DWindow::onReplaySliderMoved);
    connect(replay_slider_, &QSlider::valueChanged, this, [this](int value) {
        if (!replay_.isPlaying() && replay_slider_ && replay_slider_->isSliderDown())
        {
            rebuildReplayAtElapsedUs(replaySliderValueToElapsedUs(value));
        }
    });

    follow_action_ = toolbar->addAction(QStringLiteral("跟随飞机"));
    follow_action_->setCheckable(true);
    QSettings settings(QStringLiteral("VaporView"), QStringLiteral("Map3D"));
    max_visible_samples_ = sanitizeMaxVisibleSamples(settings.value(QStringLiteral("maxVisibleSamples"), 200000).toInt());

    max_visible_samples_spin_ = new QSpinBox(toolbar);
    max_visible_samples_spin_->setObjectName(QStringLiteral("map3DMaxVisibleSamplesSpin"));
    max_visible_samples_spin_->setRange(1000, 1000000);
    max_visible_samples_spin_->setSingleStep(1000);
    max_visible_samples_spin_->setValue(max_visible_samples_);
    max_visible_samples_spin_->setPrefix(QStringLiteral("可见点 "));
    max_visible_samples_spin_->setSuffix(QStringLiteral(" 点"));
    max_visible_samples_spin_->setToolTip(QStringLiteral("最大可见轨迹点数"));
    max_visible_samples_spin_->setStatusTip(max_visible_samples_spin_->toolTip());
    toolbar->addWidget(max_visible_samples_spin_);
    connect(max_visible_samples_spin_, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
        max_visible_samples_ = sanitizeMaxVisibleSamples(value);
        if (view_)
        {
            view_->setMaxVisibleSamples(max_visible_samples_);
        }
        QSettings settings(QStringLiteral("VaporView"), QStringLiteral("Map3D"));
        settings.setValue(QStringLiteral("maxVisibleSamples"), max_visible_samples_);
        updateStatus(nullptr);
    });

    replay_.setSpeed(settings.value(QStringLiteral("replaySpeed"), 1.0).toDouble());
    const QString replaySpeedText = QStringLiteral("%1x").arg(replay_.speed(), 0, 'g', 3);
    const int replaySpeedIndex = replay_speed_combo_->findText(replaySpeedText);
    replay_speed_combo_->setCurrentIndex(replaySpeedIndex >= 0 ? replaySpeedIndex : 1);
    connect(replay_speed_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &Map3DWindow::onReplaySpeedChanged);

    replay_timer_->setTimerType(Qt::PreciseTimer);
    replay_timer_->setInterval(replay_.intervalMs());
    connect(replay_timer_, &QTimer::timeout, this, &Map3DWindow::onReplayTick);

    follow_action_->setChecked(settings.value(QStringLiteral("followAircraft"), false).toBool());
    if (view_)
    {
        view_->setFollowAircraft(follow_action_->isChecked());
        view_->setMaxVisibleSamples(max_visible_samples_);
    }
    connect(follow_action_, &QAction::toggled, this, [this](bool enabled) {
        if (view_)
        {
            view_->setFollowAircraft(enabled);
        }
        QSettings settings(QStringLiteral("VaporView"), QStringLiteral("Map3D"));
        settings.setValue(QStringLiteral("followAircraft"), enabled);
    });

    QAction* loadEarthAction = toolbar->addAction(QStringLiteral("加载 Earth 文件"));
    connect(loadEarthAction, &QAction::triggered, this, &Map3DWindow::openEarthFile);

    local_imagery_menu_ = new QMenu(QStringLiteral("本地影像"), this);
    local_imagery_action_ = toolbar->addAction(QStringLiteral("本地影像"));
    local_imagery_action_->setObjectName(QStringLiteral("map3DLocalImageryAction"));
    local_imagery_action_->setMenu(local_imagery_menu_);

    local_3d_tiles_action_ = toolbar->addAction(QStringLiteral("本地 3D Tiles"));
    local_3d_tiles_action_->setObjectName(QStringLiteral("map3DLocal3DTilesAction"));
    local_3d_tiles_action_->setEnabled(false);
    local_3d_tiles_action_->setToolTip(QStringLiteral("加载 data/maps/tiles3d/local/tileset.json 作为本地 3D Tiles 预览叠加层"));
    local_3d_tiles_action_->setStatusTip(local_3d_tiles_action_->toolTip());
    connect(local_3d_tiles_action_, &QAction::triggered, this, &Map3DWindow::loadLocal3DTilesPreview);

    clear_local_3d_tiles_action_ = toolbar->addAction(QStringLiteral("清除 3D Tiles"));
    clear_local_3d_tiles_action_->setObjectName(QStringLiteral("map3DClearLocal3DTilesAction"));
    clear_local_3d_tiles_action_->setEnabled(false);
    clear_local_3d_tiles_action_->setToolTip(QStringLiteral("清除当前本地 3D Tiles 预览叠加层"));
    clear_local_3d_tiles_action_->setStatusTip(clear_local_3d_tiles_action_->toolTip());
    connect(clear_local_3d_tiles_action_, &QAction::triggered, this, &Map3DWindow::clearLocal3DTilesPreview);

    QAction* reloadBestMapAction = toolbar->addAction(QStringLiteral("重载最佳本地地图"));
    reloadBestMapAction->setObjectName(QStringLiteral("map3DReloadBestMapAction"));
    connect(reloadBestMapAction, &QAction::triggered, this, &Map3DWindow::reloadBestLocalMap);

    QAction* flyToAircraftAction = toolbar->addAction(QStringLiteral("飞到飞机"));
    flyToAircraftAction->setObjectName(QStringLiteral("map3DFlyToAircraftAction"));
    connect(flyToAircraftAction, &QAction::triggered, this, &Map3DWindow::flyToAircraft);

    QAction* flyToTrackAction = toolbar->addAction(QStringLiteral("飞到轨迹"));
    flyToTrackAction->setObjectName(QStringLiteral("map3DFlyToTrackAction"));
    connect(flyToTrackAction, &QAction::triggered, this, &Map3DWindow::flyToTrack);

    QAction* resetViewAction = toolbar->addAction(QStringLiteral("重置视角"));
    resetViewAction->setObjectName(QStringLiteral("map3DResetViewAction"));
    connect(resetViewAction, &QAction::triggered, this, &Map3DWindow::resetView);

    diagnostics_action_ = toolbar->addAction(QStringLiteral("地图诊断"));
    diagnostics_action_->setObjectName(QStringLiteral("map3DDiagnosticsAction"));
    connect(diagnostics_action_, &QAction::triggered, this, &Map3DWindow::showMapDiagnostics);

    statusBar()->addPermanentWidget(status_label_, 1);
    updateReplayUi();
    updateStatus(nullptr);
    if (isMap3DHeadlessTest())
    {
        setMapSelection(map_data_manager_.selectBestAvailableMap());
        latest_earth_load_.requestedPath = map_selection_.earthFile;
        latest_earth_load_.failureReason = QStringLiteral("Headless test mode; earth loading was not attempted.");
    }
    else
    {
        loadInitialEarthFile();
    }
}

Map3DWindow::~Map3DWindow()
{
    if (view_)
    {
        view_->shutdown();
    }
}

void Map3DWindow::appendSample(const VaporView::Geo::NavSample& sample)
{
    if (replay_.isPlaying())
    {
        stopReplay();
    }
    replay_.clear();
    if (view_)
    {
        view_->appendSample(sample);
    }
    if (headless_view_)
    {
        ++headless_sample_count_;
        headless_samples_.push_back(sample);
    }
    recordTrackSource(QStringLiteral("Live"), &sample);
    updateReplayUi();
    updateStatus(&sample, false);
}

void Map3DWindow::appendSamples(const std::vector<VaporView::Geo::NavSample>& samples)
{
    if (replay_.isPlaying())
    {
        stopReplay();
    }
    replay_.clear();
    if (view_)
    {
        view_->appendSamples(samples);
    }
    if (headless_view_)
    {
        headless_sample_count_ += static_cast<int>(samples.size());
        headless_samples_.insert(headless_samples_.end(), samples.cbegin(), samples.cend());
    }
    recordTrackSource(QStringLiteral("Live"), samples.empty() ? nullptr : &samples.back(),
                      samples.empty() ? QStringLiteral("empty live batch") : QString());
    updateReplayUi();
    updateStatus(samples.empty() ? nullptr : &samples.back(), false);
}

void Map3DWindow::clearTrack()
{
    replay_timer_->stop();
    replay_.clear();
    if (view_)
    {
        view_->clearTrack();
    }
    if (headless_view_)
    {
        headless_sample_count_ = 0;
        headless_samples_.clear();
    }
    recordTrackSource(QStringLiteral("none"), nullptr, QStringLiteral("track cleared"));
    updateReplayUi();
    updateStatus(nullptr);
}

void Map3DWindow::loadSessionDirectory(const QString& sessionDir)
{
    const VaporView::Geo::SessionTrackReadResult result = VaporView::Geo::readSessionTrack(sessionDir);
    if (!result.ok)
    {
        QMessageBox::warning(this,
                             QStringLiteral("Session Track"),
                             QStringLiteral("无法读取轨迹: %1").arg(result.error));
        return;
    }

    replay_timer_->stop();
    if (view_)
    {
        view_->clearTrack();
        view_->appendSamples(result.samples);
    }
    if (headless_view_)
    {
        headless_sample_count_ = static_cast<int>(result.samples.size());
        headless_samples_ = result.samples;
    }
    replay_.setSamples(result.samples);
    latest_drop_source_.clear();
    latest_drop_reason_.clear();
    latest_drop_record_timestamp_us_ = 0;
    recordTrackSource(QStringLiteral("Session"),
                      replay_.currentSample(),
                      result.sourceCsvPath);
    const bool focusedTrack = autoFocusTrack(QStringLiteral("Track auto"));
    updateReplayUi();
    updateStatus(replay_.currentSample());
    statusBar()->showMessage(QStringLiteral("Loaded %1 samples from %2%3")
                                 .arg(result.samples.size())
                                 .arg(result.sourceCsvPath,
                                      focusedTrack ? QStringLiteral(" (auto-focused track)") : QString()),
                             5000);
}

void Map3DWindow::noteLiveSampleDrop(const QString& source, const QString& reason, qint64 recordTimestampUs)
{
    latest_drop_source_ = source.isEmpty() ? QStringLiteral("Live") : source;
    latest_drop_reason_ = reason.isEmpty() ? QStringLiteral("unknown") : reason;
    latest_drop_record_timestamp_us_ = recordTimestampUs;
    if (diagnostics_text_)
    {
        diagnostics_text_->setPlainText(diagnosticsText());
    }
    updateStatus(nullptr);
}

void Map3DWindow::loadInitialEarthFile()
{
    QSettings settings(QStringLiteral("VaporView"), QStringLiteral("Map3D"));
    const QString lastEarthFile = settings.value(QStringLiteral("lastEarthFile")).toString();
    const MapDataSelection autoSelection = map_data_manager_.selectBestAvailableMap();
    const QString initialEarthFile =
        QFileInfo(lastEarthFile).isFile() && !map_data_manager_.isBuiltInEarthFile(lastEarthFile)
            ? lastEarthFile
            : autoSelection.earthFile;

    if (!QFileInfo(initialEarthFile).isFile())
    {
        latest_earth_load_ = {};
        latest_earth_load_.requestedPath = initialEarthFile;
        latest_earth_load_.failureReason = QStringLiteral("Selected earth file does not exist.");
        setMapSelection(autoSelection);
        statusBar()->showMessage(QStringLiteral("未找到默认 Earth 文件，当前显示本地 NED 网格。"), 8000);
        return;
    }

    const bool loaded = view_ ? view_->loadEarthFile(initialEarthFile) : false;
    latest_earth_load_ = view_ ? view_->earthLoadDiagnostics() : EarthLoadDiagnostics{};
    if (!loaded)
    {
        setMapSelection(autoSelection);
        statusBar()->showMessage(QStringLiteral("自动加载 Earth 文件失败，当前显示本地 NED 网格: %1").arg(initialEarthFile), 8000);
        return;
    }

    MapDataSelection activeSelection = autoSelection;
    if (initialEarthFile != autoSelection.earthFile)
    {
        activeSelection.mode = MapDataMode::NaturalEarth;
        activeSelection.earthFile = initialEarthFile;
        activeSelection.earthFilePath = initialEarthFile;
        activeSelection.diagnostics.earthFilePath = initialEarthFile;
        activeSelection.diagnostics.messages.push_back(QStringLiteral("Using user-selected custom earth file."));
    }
    setMapSelection(activeSelection);
    settings.setValue(QStringLiteral("lastEarthFile"), initialEarthFile);
    updateStatus(nullptr);
    statusBar()->showMessage(QStringLiteral("已自动加载 Earth 文件: %1").arg(initialEarthFile), 5000);
}

void Map3DWindow::openSessionDirectory()
{
    QSettings settings(QStringLiteral("VaporView"), QStringLiteral("Map3D"));
    const QString initial = settings.value(QStringLiteral("lastSessionDir")).toString();
    const QString dir = QFileDialog::getExistingDirectory(this,
                                                          QStringLiteral("选择 Session 目录"),
                                                          initial);
    if (dir.isEmpty())
    {
        return;
    }
    settings.setValue(QStringLiteral("lastSessionDir"), dir);
    loadSessionDirectory(dir);
}

void Map3DWindow::openEarthFile()
{
    QSettings settings(QStringLiteral("VaporView"), QStringLiteral("Map3D"));
    const QString initial = settings.value(QStringLiteral("lastEarthFile")).toString();
    const QString file = QFileDialog::getOpenFileName(this,
                                                      QStringLiteral("加载 Earth 文件"),
                                                      initial,
                                                      QStringLiteral("osgEarth (*.earth);;All Files (*)"));
    if (file.isEmpty())
    {
        return;
    }
    const bool loaded = view_ ? view_->loadEarthFile(file) : false;
    latest_earth_load_ = view_ ? view_->earthLoadDiagnostics() : EarthLoadDiagnostics{};
    if (!loaded)
    {
        QMessageBox::warning(this,
                             QStringLiteral("osgEarth"),
                             QStringLiteral("无法加载 Earth 文件: %1").arg(file));
        return;
    }
    settings.setValue(QStringLiteral("lastEarthFile"), file);
    MapDataSelection selection = map_data_manager_.selectBestAvailableMap();
    selection.earthFile = file;
    selection.earthFilePath = file;
    selection.diagnostics.earthFilePath = file;
    selection.diagnostics.messages.push_back(QStringLiteral("Loaded user-selected earth file."));
    setMapSelection(selection);
    const bool focusedTrack = autoFocusTrack(QStringLiteral("Track auto"));
    updateStatus(nullptr);
    statusBar()->showMessage(QStringLiteral("Loaded earth file: %1%2")
                                 .arg(file,
                                      focusedTrack ? QStringLiteral(" (auto-focused track)") : QString()),
                             5000);
}

void Map3DWindow::loadLocalImageryTemplate(const LocalImageryOption& option)
{
    if (!option.available)
    {
        statusBar()->showMessage(QStringLiteral("本地影像不可用: %1").arg(option.label), 5000);
        return;
    }

    const bool loaded = view_ ? view_->loadEarthFile(option.earthFilePath) : false;
    latest_earth_load_ = view_ ? view_->earthLoadDiagnostics() : EarthLoadDiagnostics{};
    if (!loaded && view_)
    {
        statusBar()->showMessage(QStringLiteral("加载本地影像失败: %1").arg(option.earthFilePath), 8000);
        return;
    }

    MapDataSelection selection = map_data_manager_.selectBestAvailableMap();
    selection.earthFile = option.earthFilePath;
    selection.earthFilePath = option.earthFilePath;
    selection.description = QStringLiteral("Natural Earth background with %1 overlay.").arg(option.label);
    selection.diagnostics.earthFilePath = option.earthFilePath;
    selection.diagnostics.messages.push_back(QStringLiteral("Loaded optional local imagery template: %1").arg(option.label));
    setMapSelection(selection);

    QSettings settings(QStringLiteral("VaporView"), QStringLiteral("Map3D"));
    settings.setValue(QStringLiteral("lastEarthFile"), option.earthFilePath);
    const bool focusedTrack = autoFocusTrack(QStringLiteral("Track auto"));
    updateStatus(nullptr);
    statusBar()->showMessage(QStringLiteral("已加载本地影像: %1%2")
                                 .arg(option.label,
                                      focusedTrack ? QStringLiteral(" (已自动定位轨迹)") : QString()),
                             5000);
}

void Map3DWindow::loadLocal3DTilesPreview()
{
    const MapDataDiagnostics& diagnostics = map_selection_.diagnostics;
    if (!diagnostics.local3DTilesTilesetValid)
    {
        latest_local_3d_tiles_load_ = {};
        latest_local_3d_tiles_load_.requestedPath = diagnostics.local3DTilesTilesetPath;
        latest_local_3d_tiles_load_.failureReason =
            QStringLiteral("Local 3D Tiles contract is not valid; open Map Data Diagnostics for details.");
        if (diagnostics_text_)
        {
            diagnostics_text_->setPlainText(diagnosticsText());
        }
        statusBar()->showMessage(QStringLiteral("本地 3D Tiles 契约无效，请先查看地图诊断。"), 8000);
        return;
    }

    const bool loaded = view_ ? view_->loadLocal3DTilesPreview(diagnostics.local3DTilesTilesetPath) : false;
    latest_local_3d_tiles_load_ = view_ ? view_->local3DTilesLoadDiagnostics() : Local3DTilesLoadDiagnostics{};
    if (diagnostics_text_)
    {
        diagnostics_text_->setPlainText(diagnosticsText());
    }
    updateStatus(nullptr);

    if (!loaded)
    {
        if (clear_local_3d_tiles_action_)
        {
            clear_local_3d_tiles_action_->setEnabled(false);
        }
        statusBar()->showMessage(QStringLiteral("本地 3D Tiles 预览加载失败: %1")
                                     .arg(latest_local_3d_tiles_load_.failureReason.isEmpty()
                                              ? diagnostics.local3DTilesTilesetPath
                                              : latest_local_3d_tiles_load_.failureReason),
                                 9000);
        return;
    }

    if (clear_local_3d_tiles_action_)
    {
        clear_local_3d_tiles_action_->setEnabled(true);
    }
    statusBar()->showMessage(QStringLiteral("已加载本地 3D Tiles 预览叠加层: %1")
                                 .arg(diagnostics.local3DTilesTilesetPath),
                             6000);
}

void Map3DWindow::clearLocal3DTilesPreview()
{
    if (view_)
    {
        view_->clearLocal3DTilesPreview();
    }
    latest_local_3d_tiles_load_ = {};
    latest_local_3d_tiles_load_.failureReason = QStringLiteral("Local 3D Tiles preview overlay cleared.");
    if (clear_local_3d_tiles_action_)
    {
        clear_local_3d_tiles_action_->setEnabled(false);
    }
    if (diagnostics_text_)
    {
        diagnostics_text_->setPlainText(diagnosticsText());
    }
    updateStatus(nullptr);
    statusBar()->showMessage(QStringLiteral("已清除本地 3D Tiles 预览叠加层。"), 5000);
}

void Map3DWindow::reloadBestLocalMap()
{
    const MapDataSelection selection = map_data_manager_.selectBestAvailableMap();
    if (!QFileInfo(selection.earthFile).isFile())
    {
        latest_earth_load_ = {};
        latest_earth_load_.requestedPath = selection.earthFile;
        latest_earth_load_.failureReason = QStringLiteral("Selected earth file does not exist.");
        setMapSelection(selection);
        updateStatus(nullptr);
        statusBar()->showMessage(QStringLiteral("未找到完整本地地图数据，保持本地网格显示。"), 8000);
        return;
    }

    const bool loaded = view_ ? view_->loadEarthFile(selection.earthFile) : false;
    latest_earth_load_ = view_ ? view_->earthLoadDiagnostics() : EarthLoadDiagnostics{};
    if (!loaded && view_)
    {
        setMapSelection(selection);
        updateStatus(nullptr);
        statusBar()->showMessage(QStringLiteral("重载最佳本地地图失败: %1").arg(selection.earthFile), 8000);
        return;
    }

    setMapSelection(selection);
    QSettings settings(QStringLiteral("VaporView"), QStringLiteral("Map3D"));
    settings.setValue(QStringLiteral("lastEarthFile"), selection.earthFile);
    const bool focusedTrack = autoFocusTrack(QStringLiteral("Track auto"));
    updateStatus(nullptr);
    statusBar()->showMessage(QStringLiteral("已重载最佳本地地图: %1%2")
                                 .arg(selection.earthFile,
                                      focusedTrack ? QStringLiteral(" (已自动定位轨迹)") : QString()),
                             5000);
}

void Map3DWindow::flyToAircraft()
{
    const bool ok = view_ ? view_->flyToAircraft() : headless_sample_count_ > 0;
    setCameraNote(ok ? QStringLiteral("Aircraft") : QStringLiteral("Aircraft unavailable"));
    updateStatus(nullptr);
    statusBar()->showMessage(ok ? QStringLiteral("已定位到飞机。")
                                : QStringLiteral("暂无飞机位置可定位。"),
                             3000);
}

void Map3DWindow::flyToTrack()
{
    const bool ok = view_ ? view_->flyToTrack() : headless_sample_count_ > 0;
    setCameraNote(ok ? QStringLiteral("Track") : QStringLiteral("Track unavailable"));
    updateStatus(nullptr);
    statusBar()->showMessage(ok ? QStringLiteral("已定位到完整轨迹。")
                                : QStringLiteral("暂无轨迹可定位。"),
                             3000);
}

void Map3DWindow::resetView()
{
    if (view_)
    {
        view_->resetView();
    }
    setCameraNote(QStringLiteral("Reset"));
    updateStatus(nullptr);
    statusBar()->showMessage(QStringLiteral("视角已重置。"), 3000);
}

void Map3DWindow::showMapDiagnostics()
{
    if (!diagnostics_dialog_)
    {
        diagnostics_dialog_ = new QDialog(this);
        diagnostics_dialog_->setWindowTitle(QStringLiteral("3D Map 数据诊断"));
        diagnostics_dialog_->resize(820, 520);

        QVBoxLayout* layout = new QVBoxLayout(diagnostics_dialog_);
        diagnostics_text_ = new QPlainTextEdit(diagnostics_dialog_);
        diagnostics_text_->setReadOnly(true);
        layout->addWidget(diagnostics_text_);

        QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Close, diagnostics_dialog_);
        connect(buttons, &QDialogButtonBox::rejected, diagnostics_dialog_, &QDialog::hide);
        layout->addWidget(buttons);
    }

    if (diagnostics_text_)
    {
        diagnostics_text_->setPlainText(diagnosticsText());
    }
    diagnostics_dialog_->show();
    diagnostics_dialog_->raise();
    diagnostics_dialog_->activateWindow();
}

void Map3DWindow::toggleReplay()
{
    if (!replay_.hasSamples())
    {
        updateReplayUi();
        return;
    }

    if (replay_.isPlaying())
    {
        replay_.pause();
        replay_timer_->stop();
        replay_tick_clock_.invalidate();
    }
    else
    {
        replay_.play();
        rebuildReplayAt(replay_.currentIndex());
        replay_tick_clock_.restart();
        replay_timer_->start(replay_.intervalMs());
    }
    updateReplayUi();
}

void Map3DWindow::stopReplay()
{
    replay_timer_->stop();
    replay_tick_clock_.invalidate();
    replay_.stop();
    rebuildReplayAt(replay_.currentIndex(), false);
    updateReplayUi();
}

void Map3DWindow::onReplayTick()
{
    if (!replay_.isPlaying() || !replay_.hasSamples())
    {
        replay_timer_->stop();
        replay_.pause();
        replay_tick_clock_.invalidate();
        updateReplayUi();
        return;
    }

    const qint64 elapsedMs = replay_tick_clock_.isValid()
        ? replay_tick_clock_.restart()
        : static_cast<qint64>(replay_.intervalMs());
    const qint64 deltaUs = static_cast<qint64>(
        std::llround(static_cast<double>(elapsedMs) * 1000.0 * replay_.speed()));
    replay_.stepByElapsedUs(deltaUs);
    rebuildReplayAt(replay_.currentIndex());
    if (!replay_.isPlaying())
    {
        replay_timer_->stop();
        replay_tick_clock_.invalidate();
    }
    updateReplayUi();
}

void Map3DWindow::onReplaySliderMoved(int value)
{
    if (!replay_.hasSamples())
    {
        return;
    }
    rebuildReplayAtElapsedUs(replaySliderValueToElapsedUs(value));
    if (replay_.isPlaying())
    {
        replay_tick_clock_.restart();
    }
    updateReplayUi();
}

void Map3DWindow::onReplaySpeedChanged(int index)
{
    if (!replay_speed_combo_ || index < 0)
    {
        return;
    }
    replay_.setSpeed(VaporView::Geo::TrajectoryReplay::speedFromText(replay_speed_combo_->itemText(index)));
    replay_timer_->setInterval(replay_.intervalMs());
    if (replay_.isPlaying())
    {
        replay_tick_clock_.restart();
    }
    QSettings settings(QStringLiteral("VaporView"), QStringLiteral("Map3D"));
    settings.setValue(QStringLiteral("replaySpeed"), replay_.speed());
}

void Map3DWindow::rebuildReplayAt(int index, bool forceStatus)
{
    if (!replay_.hasSamples())
    {
        return;
    }
    replay_.seek(index);
    const std::vector<VaporView::Geo::NavSample> visibleSamples = replay_.visibleSamples();

    if (view_)
    {
        view_->clearTrack();
        view_->appendSamples(visibleSamples);
    }
    if (headless_view_)
    {
        headless_sample_count_ = static_cast<int>(visibleSamples.size());
        headless_samples_ = visibleSamples;
    }
    recordTrackSource(QStringLiteral("Replay"),
                      visibleSamples.empty() ? nullptr : &visibleSamples.back());
    updateStatus(visibleSamples.empty() ? nullptr : &visibleSamples.back(), forceStatus);
}

void Map3DWindow::rebuildReplayAtElapsedUs(qint64 elapsedUs)
{
    if (!replay_.hasSamples())
    {
        return;
    }
    replay_.seekElapsedUs(elapsedUs);
    rebuildReplayAt(replay_.currentIndex());
}

void Map3DWindow::setReplayEnabled(bool enabled)
{
    if (replay_action_)
    {
        replay_action_->setEnabled(enabled);
    }
    if (replay_stop_action_)
    {
        replay_stop_action_->setEnabled(enabled);
    }
    if (replay_slider_)
    {
        replay_slider_->setEnabled(enabled);
    }
    if (replay_speed_combo_)
    {
        replay_speed_combo_->setEnabled(enabled);
    }
}

void Map3DWindow::updateReplayUi()
{
    const bool hasReplay = replay_.hasSamples();
    setReplayEnabled(hasReplay);
    if (replay_action_)
    {
        const QSignalBlocker blocker(replay_action_);
        replay_action_->setChecked(replay_.isPlaying());
        replay_action_->setText(replay_.isPlaying() ? QStringLiteral("暂停") : QStringLiteral("播放"));
    }
    if (replay_stop_action_)
    {
        replay_stop_action_->setText(QStringLiteral("停止回放"));
    }
    if (replay_slider_)
    {
        const QSignalBlocker blocker(replay_slider_);
        replay_slider_->setRange(0, hasReplay ? replaySliderMaximum() : 0);
        replay_slider_->setValue(hasReplay ? replaySliderValue() : 0);
    }
}

int Map3DWindow::replaySliderMaximum() const
{
    const qint64 durationMs = (replay_.durationUs() + 999) / 1000;
    return static_cast<int>(std::clamp<qint64>(durationMs, 0, std::numeric_limits<int>::max()));
}

int Map3DWindow::replaySliderValue() const
{
    const qint64 elapsedMs = (replay_.elapsedUs() + 999) / 1000;
    return static_cast<int>(std::clamp<qint64>(elapsedMs, 0, replaySliderMaximum()));
}

qint64 Map3DWindow::replaySliderValueToElapsedUs(int value) const
{
    return static_cast<qint64>(std::clamp(value, 0, replaySliderMaximum())) * 1000;
}

QString Map3DWindow::replayTimeLabel() const
{
    const double elapsedS = static_cast<double>(replay_.elapsedUs()) / 1000000.0;
    const double durationS = static_cast<double>(replay_.durationUs()) / 1000000.0;
    return QStringLiteral("t %1/%2 s").arg(elapsedS, 0, 'f', 3).arg(durationS, 0, 'f', 3);
}

void Map3DWindow::setMapSelection(const MapDataSelection& selection)
{
    map_selection_ = selection;
    if (local_imagery_menu_)
    {
        local_imagery_menu_->clear();
        for (const LocalImageryOption& option : map_selection_.diagnostics.localImageryOptions)
        {
            QAction* action = local_imagery_menu_->addAction(imageryOptionLabel(option));
            action->setObjectName(QStringLiteral("map3DLocalImagery_%1").arg(option.key));
            action->setEnabled(option.available);
            action->setToolTip(QStringLiteral("%1\nVRT: %2\nEarth: %3")
                                   .arg(option.available ? QStringLiteral("可加载") : QStringLiteral("缺少本地 VRT 或 earth 模板"),
                                        option.vrtPath,
                                        option.earthFilePath));
            connect(action, &QAction::triggered, this, [this, option]() {
                loadLocalImageryTemplate(option);
            });
        }
        local_imagery_action_->setEnabled(map_selection_.diagnostics.localImageryAvailable);
    }
    if (local_3d_tiles_action_)
    {
        const bool enabled = map_selection_.diagnostics.local3DTilesTilesetValid;
        local_3d_tiles_action_->setEnabled(enabled);
        local_3d_tiles_action_->setToolTip(
            enabled
                ? QStringLiteral("加载本地 3D Tiles 预览叠加层: %1")
                      .arg(map_selection_.diagnostics.local3DTilesTilesetPath)
                : QStringLiteral("本地 3D Tiles 不可用或契约无效；请查看地图诊断"));
        local_3d_tiles_action_->setStatusTip(local_3d_tiles_action_->toolTip());
    }
    if (diagnostics_text_)
    {
        diagnostics_text_->setPlainText(diagnosticsText());
    }
}

int Map3DWindow::currentTrackSampleCount() const
{
    if (view_)
    {
        return view_->sampleCount();
    }
    return headless_sample_count_;
}

bool Map3DWindow::autoFocusTrack(const QString& note)
{
    if (currentTrackSampleCount() <= 0)
    {
        return false;
    }

    const bool ok = view_ ? view_->flyToTrack() : true;
    if (ok)
    {
        setCameraNote(note);
    }
    return ok;
}

void Map3DWindow::setCameraNote(const QString& note)
{
    latest_camera_note_ = note;
    if (diagnostics_text_)
    {
        diagnostics_text_->setPlainText(diagnosticsText());
    }
}

QString Map3DWindow::diagnosticsText() const
{
    const MapDataDiagnostics& diagnostics = map_selection_.diagnostics;
    const Map3DPerformanceStats stats = view_ ? view_->performanceStats() : Map3DPerformanceStats{};
    const int totalSamples = view_ ? stats.totalSamples : headless_sample_count_;
    const int visibleSamples = view_ ? stats.visibleSamples : std::min(headless_sample_count_, max_visible_samples_);
    const int maxVisibleSamples = view_ ? stats.maxVisibleSamples : max_visible_samples_;
    const int hiddenSamples = std::max(0, totalSamples - visibleSamples);
    const TrajectoryQualityStats qualityStats =
        view_ ? stats.qualityStats : qualityStatsForSamples(headless_samples_, max_visible_samples_);
    QStringList lines;
    lines << QStringLiteral("Mode: %1 (%2)")
                 .arg(MapDataManager::modeLabel(map_selection_.mode),
                      MapDataManager::modeKey(map_selection_.mode));
    if (!map_selection_.description.isEmpty())
    {
        lines << QStringLiteral("Description: %1").arg(map_selection_.description);
    }
    const QString earthFile = map_selection_.earthFile.isEmpty() ? map_selection_.earthFilePath : map_selection_.earthFile;
    lines << QStringLiteral("Earth file: %1").arg(earthFile.isEmpty() ? QStringLiteral("<none>") : earthFile);
    lines << QStringLiteral("Earth load:");
    lines << QStringLiteral("  Requested path: %1")
                 .arg(latest_earth_load_.requestedPath.isEmpty() ? QStringLiteral("<none>") : latest_earth_load_.requestedPath);
    lines << QStringLiteral("  Attempted: %1").arg(latest_earth_load_.attempted ? QStringLiteral("yes") : QStringLiteral("no"));
    lines << QStringLiteral("  Loaded: %1").arg(latest_earth_load_.loaded ? QStringLiteral("yes") : QStringLiteral("no"));
    lines << QStringLiteral("  Textured fallback: %1").arg(latest_earth_load_.usedTexturedFallback ? QStringLiteral("yes") : QStringLiteral("no"));
    lines << QStringLiteral("  MapNode: %1").arg(latest_earth_load_.foundMapNode ? QStringLiteral("yes") : QStringLiteral("no"));
    lines << QStringLiteral("  Layers: %1/%2 open").arg(latest_earth_load_.openLayerCount).arg(latest_earth_load_.layerCount);
    if (!latest_earth_load_.failureReason.isEmpty())
    {
        lines << QStringLiteral("  Failure/note: %1").arg(latest_earth_load_.failureReason);
    }
    if (!latest_earth_load_.layerSummaries.isEmpty())
    {
        lines << QStringLiteral("  Layer details:");
        for (const QString& layerSummary : latest_earth_load_.layerSummaries)
        {
            lines << QStringLiteral("    - %1").arg(layerSummary);
        }
    }
    lines << QStringLiteral("Local 3D Tiles preview load:");
    lines << QStringLiteral("  Requested path: %1")
                 .arg(latest_local_3d_tiles_load_.requestedPath.isEmpty()
                          ? QStringLiteral("<none>")
                          : latest_local_3d_tiles_load_.requestedPath);
    lines << QStringLiteral("  Attempted: %1").arg(latest_local_3d_tiles_load_.attempted ? QStringLiteral("yes") : QStringLiteral("no"));
    lines << QStringLiteral("  Loaded: %1").arg(latest_local_3d_tiles_load_.loaded ? QStringLiteral("yes") : QStringLiteral("no"));
    if (!latest_local_3d_tiles_load_.nodeDescription.isEmpty())
    {
        lines << QStringLiteral("  Node: %1").arg(latest_local_3d_tiles_load_.nodeDescription);
    }
    if (!latest_local_3d_tiles_load_.failureReason.isEmpty())
    {
        lines << QStringLiteral("  Failure/note: %1").arg(latest_local_3d_tiles_load_.failureReason);
    }
    lines << QStringLiteral("Render performance:");
    lines << QStringLiteral("  Samples: %1 visible / %2 total / %3 hidden")
                 .arg(visibleSamples)
                 .arg(totalSamples)
                 .arg(hiddenSamples);
    lines << QStringLiteral("  Max visible samples: %1").arg(maxVisibleSamples);
    lines << QStringLiteral("  Trajectory segments: %1 x %2 samples")
                 .arg(stats.segmentCount)
                 .arg(stats.segmentSize);
    lines << QStringLiteral("  FPS: %1").arg(stats.framesPerSecond, 0, 'f', 1);
    lines << QStringLiteral("  Frame ms: %1").arg(stats.frameMs, 0, 'f', 1);
    lines << QStringLiteral("  Track update ms: %1").arg(stats.trackUpdateMs, 0, 'f', 1);
    lines << QStringLiteral("Trajectory quality:");
    lines << QStringLiteral("  Visible line samples: %1").arg(qualityStats.lineSamples);
    lines << QStringLiteral("  Visible marker samples: %1").arg(qualityStats.markerSamples);
    lines << QStringLiteral("  Fixed: %1").arg(qualityStats.fixedSamples);
    lines << QStringLiteral("  Float: %1").arg(qualityStats.floatSamples);
    lines << QStringLiteral("  DGPS: %1").arg(qualityStats.dgpsSamples);
    lines << QStringLiteral("  Single: %1").arg(qualityStats.singleSamples);
    lines << QStringLiteral("  Unknown: %1").arg(qualityStats.unknownSamples);
    lines << QStringLiteral("  Invalid/unusable: %1").arg(qualityStats.invalidSamples);
    lines << QStringLiteral("  Jump markers: %1").arg(qualityStats.jumpSamples);
    lines << QStringLiteral("Track data:");
    lines << QStringLiteral("  Source: %1").arg(latest_track_source_.isEmpty() ? QStringLiteral("none") : latest_track_source_);
    lines << QStringLiteral("  Latest record timestamp us: %1")
                 .arg(latest_track_record_timestamp_us_ > 0 ? QString::number(latest_track_record_timestamp_us_) : QStringLiteral("<none>"));
    lines << QStringLiteral("  Latest device timestamp us: %1")
                 .arg(latest_track_device_timestamp_us_ > 0 ? QString::number(latest_track_device_timestamp_us_) : QStringLiteral("<none>"));
    lines << QStringLiteral("  Attitude source: %1")
                 .arg(attitudeSourceLabel(has_latest_status_sample_ ? &latest_status_sample_ : nullptr));
    if (has_latest_status_sample_ && latest_status_sample_.hasLlh())
    {
        lines << QStringLiteral("  Height reference: %1")
                     .arg(heightReferenceLabel(latest_status_sample_.heightReference));
        lines << QStringLiteral("  Height safety note: %1").arg(heightReferenceUncheckedNote());
    }
    if (!latest_track_note_.isEmpty())
    {
        lines << QStringLiteral("  Note: %1").arg(latest_track_note_);
    }
    lines << QStringLiteral("  Last drop source: %1").arg(latest_drop_source_.isEmpty() ? QStringLiteral("<none>") : latest_drop_source_);
    lines << QStringLiteral("  Last drop reason: %1").arg(latest_drop_reason_.isEmpty() ? QStringLiteral("<none>") : latest_drop_reason_);
    lines << QStringLiteral("  Last drop record timestamp us: %1")
                 .arg(latest_drop_record_timestamp_us_ > 0 ? QString::number(latest_drop_record_timestamp_us_) : QStringLiteral("<none>"));
    lines << QStringLiteral("Camera: %1").arg(latest_camera_note_.isEmpty() ? QStringLiteral("<none>") : latest_camera_note_);
    lines << QStringLiteral("Layer summary:");
    lines << QStringLiteral("  Natural Earth: %1").arg(availabilityLabel(diagnostics.naturalEarthAvailable));
    lines << QStringLiteral("  Selected DEM: %1").arg(selectedDemLabel(diagnostics));
    lines << QStringLiteral("  Copernicus DEM VRT: %1").arg(availabilityLabel(diagnostics.copernicusDemAvailable));
    lines << QStringLiteral("  SRTM VRT: %1").arg(availabilityLabel(diagnostics.srtmDemAvailable));
    lines << QStringLiteral("  OSM vectors: %1 (%2/4 files found)")
                 .arg(diagnostics.osmVectorAvailable ? QStringLiteral("available") : QStringLiteral("missing"))
                 .arg(diagnostics.osmLayerCount);
    lines << QStringLiteral("  Selected OSM: %1").arg(selectedOsmLabel(diagnostics));
    lines << QStringLiteral("  Selected full-local earth: %1")
                 .arg(diagnostics.selectedFullLocalEarthPath.isEmpty() ? QStringLiteral("<not selected>") : diagnostics.selectedFullLocalEarthPath);
    lines << QStringLiteral("  Optional local imagery: %1 (%2/3 VRTs found)")
                 .arg(diagnostics.localImageryAvailable ? QStringLiteral("available") : QStringLiteral("not configured"))
                 .arg(diagnostics.localImageryLayerCount);
    if (!diagnostics.localImageryOptions.empty())
    {
        lines << QStringLiteral("  Local imagery menu:");
        for (const LocalImageryOption& option : diagnostics.localImageryOptions)
        {
            lines << QStringLiteral("    - %1: %2")
                         .arg(option.label,
                              option.available ? QStringLiteral("available") : QStringLiteral("missing"));
        }
    }
    lines << QStringLiteral("  Optional local 3D Tiles: %1")
                 .arg(diagnostics.local3DTilesAvailable ? QStringLiteral("available") : QStringLiteral("not configured"));
    lines << QStringLiteral("  Local 3D Tiles contract: %1")
                 .arg(diagnostics.local3DTilesAvailable
                          ? (diagnostics.local3DTilesTilesetValid ? QStringLiteral("valid") : QStringLiteral("needs attention"))
                          : QStringLiteral("not checked"));
    lines << QStringLiteral("Current working directory: %1").arg(diagnostics.currentWorkingDirectory.isEmpty() ? QStringLiteral("<unknown>") : diagnostics.currentWorkingDirectory);
    lines << QStringLiteral("Project root: %1").arg(diagnostics.projectRoot.isEmpty() ? QStringLiteral("<unknown>") : diagnostics.projectRoot);
    lines << QStringLiteral("Maps root: %1").arg(diagnostics.mapsRoot.isEmpty() ? QStringLiteral("<unknown>") : diagnostics.mapsRoot);
    lines << QStringLiteral("Full local Copernicus earth: %1").arg(diagnostics.fullLocalEarthPath);
    lines << QStringLiteral("Full local SRTM earth: %1").arg(diagnostics.fullLocalSrtmEarthPath);
    lines << QStringLiteral("Natural Earth texture: %1").arg(diagnostics.naturalEarthTexturePath);
    lines << QStringLiteral("Natural Earth VRT: %1").arg(diagnostics.naturalEarthVrtPath);
    lines << QStringLiteral("Natural Earth raster: %1").arg(diagnostics.naturalEarthRasterPath);
    lines << QStringLiteral("Copernicus DEM VRT: %1").arg(diagnostics.copernicusDemVrtPath);
    lines << QStringLiteral("SRTM VRT: %1").arg(diagnostics.srtmDemVrtPath);
    lines << QStringLiteral("OSM roads: %1").arg(fileAvailabilityLabel(diagnostics.osmRoadsAvailable, diagnostics.osmRoadsPath));
    lines << QStringLiteral("OSM water: %1").arg(fileAvailabilityLabel(diagnostics.osmWaterAvailable, diagnostics.osmWaterPath));
    lines << QStringLiteral("OSM buildings: %1").arg(fileAvailabilityLabel(diagnostics.osmBuildingsAvailable, diagnostics.osmBuildingsPath));
    lines << QStringLiteral("OSM places: %1").arg(fileAvailabilityLabel(diagnostics.osmPlacesAvailable, diagnostics.osmPlacesPath));
    if (!diagnostics.osmLayerContracts.isEmpty())
    {
        lines << QStringLiteral("OSM layer contract:");
        for (const QString& contract : diagnostics.osmLayerContracts)
        {
            lines << QStringLiteral("  - %1").arg(contract);
        }
    }
    lines << QStringLiteral("Sentinel-2 imagery VRT: %1").arg(diagnostics.sentinel2ImageryVrtPath);
    lines << QStringLiteral("Landsat imagery VRT: %1").arg(diagnostics.landsatImageryVrtPath);
    lines << QStringLiteral("OpenAerialMap imagery VRT: %1").arg(diagnostics.openAerialMapImageryVrtPath);
    lines << QStringLiteral("Local 3D Tiles tileset: %1").arg(diagnostics.local3DTilesTilesetPath);
    lines << QStringLiteral("Local 3D Tiles valid: %1").arg(diagnostics.local3DTilesTilesetValid ? QStringLiteral("yes") : QStringLiteral("no"));
    lines << QStringLiteral("Local 3D Tiles referenced resources: %1").arg(diagnostics.local3DTilesResourceCount);
    if (!diagnostics.local3DTilesResourceUris.isEmpty())
    {
        lines << QStringLiteral("Local 3D Tiles resource URIs:");
        for (const QString& uri : diagnostics.local3DTilesResourceUris)
        {
            lines << QStringLiteral("  - %1").arg(uri);
        }
    }
    if (!diagnostics.local3DTilesExternalUris.isEmpty())
    {
        lines << QStringLiteral("Local 3D Tiles non-local/unsupported URIs:");
        for (const QString& uri : diagnostics.local3DTilesExternalUris)
        {
            lines << QStringLiteral("  - %1").arg(uri);
        }
    }
    if (!diagnostics.local3DTilesMissingResources.isEmpty())
    {
        lines << QStringLiteral("Local 3D Tiles missing resources:");
        for (const QString& path : diagnostics.local3DTilesMissingResources)
        {
            lines << QStringLiteral("  - %1").arg(path);
        }
    }
    if (!diagnostics.local3DTilesDiagnostics.isEmpty())
    {
        lines << QStringLiteral("Local 3D Tiles diagnostics:");
        for (const QString& message : diagnostics.local3DTilesDiagnostics)
        {
            lines << QStringLiteral("  - %1").arg(message);
        }
    }
    lines << QStringLiteral("OSG plugin path: %1").arg(diagnostics.osgPluginPath.isEmpty() ? QStringLiteral("<not found>") : diagnostics.osgPluginPath);
    lines << QStringLiteral("OSG_LIBRARY_PATH: %1").arg(diagnostics.osgLibraryPath.isEmpty() ? QStringLiteral("<not set>") : diagnostics.osgLibraryPath);
    lines << QStringLiteral("OSGEARTH_NOTIFY_LEVEL: %1").arg(diagnostics.osgEarthNotifyLevel.isEmpty() ? QStringLiteral("<not set>") : diagnostics.osgEarthNotifyLevel);
    lines << QStringLiteral("GDAL_DATA: %1").arg(diagnostics.gdalDataPath.isEmpty() ? QStringLiteral("<not found>") : diagnostics.gdalDataPath);
    lines << QStringLiteral("PROJ_DATA: %1").arg(diagnostics.projDataPath.isEmpty() ? QStringLiteral("<not found>") : diagnostics.projDataPath);
    lines << QStringLiteral("PROJ_LIB: %1").arg(diagnostics.projLibPath.isEmpty() ? QStringLiteral("<not found>") : diagnostics.projLibPath);

    if (!diagnostics.foundFiles.isEmpty())
    {
        lines << QString();
        lines << QStringLiteral("Found files:");
        for (const QString& path : diagnostics.foundFiles)
        {
            lines << QStringLiteral("  - %1").arg(path);
        }
    }

    if (!diagnostics.missingFiles.isEmpty())
    {
        lines << QString();
        lines << QStringLiteral("Missing files:");
        for (const QString& path : diagnostics.missingFiles)
        {
            lines << QStringLiteral("  - %1").arg(path);
        }
    }

    if (!diagnostics.fullLocalBlockers.isEmpty())
    {
        lines << QString();
        lines << QStringLiteral("Full local map blockers:");
        for (const QString& blocker : diagnostics.fullLocalBlockers)
        {
            lines << QStringLiteral("  - %1").arg(blocker);
        }
    }

    if (!diagnostics.warnings.isEmpty())
    {
        lines << QString();
        lines << QStringLiteral("Warnings:");
        for (const QString& warning : diagnostics.warnings)
        {
            lines << QStringLiteral("  - %1").arg(warning);
        }
    }

    if (!diagnostics.messages.isEmpty())
    {
        lines << QString();
        lines << QStringLiteral("Diagnostics:");
        for (const QString& message : diagnostics.messages)
        {
            lines << QStringLiteral("  - %1").arg(message);
        }
    }
    return lines.join(QLatin1Char('\n'));
}

void Map3DWindow::recordTrackSource(const QString& source,
                                    const VaporView::Geo::NavSample* latest,
                                    const QString& note)
{
    latest_track_source_ = source.isEmpty() ? QStringLiteral("none") : source;
    latest_track_note_ = note;
    latest_track_record_timestamp_us_ = latest ? latest->recordTimestampUs : 0;
    latest_track_device_timestamp_us_ = latest ? latest->deviceTimestampUs : 0;
    if (!latest)
    {
        has_latest_status_sample_ = false;
    }
    latest_drop_source_.clear();
    latest_drop_reason_.clear();
    latest_drop_record_timestamp_us_ = 0;
    if (diagnostics_text_)
    {
        diagnostics_text_->setPlainText(diagnosticsText());
    }
}

void Map3DWindow::updateStatus(const VaporView::Geo::NavSample* latest, bool force)
{
    const VaporView::Geo::NavSample* displayLatest = latest;
    if (latest)
    {
        latest_status_sample_ = *latest;
        has_latest_status_sample_ = true;
    }
    else if (has_latest_status_sample_)
    {
        displayLatest = &latest_status_sample_;
    }
    if (!force)
    {
        if (status_update_clock_.isValid() && status_update_clock_.elapsed() < kStatusUpdateIntervalMs)
        {
            return;
        }
        status_update_clock_.restart();
    }
    else
    {
        status_update_clock_.invalidate();
    }

    const Map3DPerformanceStats stats = view_ ? view_->performanceStats() : Map3DPerformanceStats{};
    const int totalSamples = view_ ? stats.totalSamples : headless_sample_count_;
    const int visibleSamples = view_ ? stats.visibleSamples : std::min(headless_sample_count_, max_visible_samples_);
    const TrajectoryQualityStats qualityStats =
        view_ ? stats.qualityStats : qualityStatsForSamples(headless_samples_, max_visible_samples_);
    QString text = QStringLiteral("Points: %1/%2").arg(visibleSamples).arg(totalSamples);
    if (visibleSamples > 0)
    {
        text += QStringLiteral(" | Q %1").arg(qualityStatsSummary(qualityStats));
    }
    text += QStringLiteral(" | Source %1").arg(latest_track_source_.isEmpty() ? QStringLiteral("none") : latest_track_source_);
    if (!latest_camera_note_.isEmpty())
    {
        text += QStringLiteral(" | Camera %1").arg(latest_camera_note_);
    }
    if (latest_track_record_timestamp_us_ > 0)
    {
        text += QStringLiteral(" rec %1").arg(latest_track_record_timestamp_us_);
    }
    if (latest_track_device_timestamp_us_ > 0)
    {
        text += QStringLiteral(" dev %1").arg(latest_track_device_timestamp_us_);
    }
    if (!latest_drop_reason_.isEmpty())
    {
        text += QStringLiteral(" | Last drop %1: %2")
                    .arg(latest_drop_source_.isEmpty() ? QStringLiteral("Live") : latest_drop_source_,
                         latest_drop_reason_);
        if (latest_drop_record_timestamp_us_ > 0)
        {
            text += QStringLiteral(" rec %1").arg(latest_drop_record_timestamp_us_);
        }
    }
    if (view_ || headless_view_)
    {
        const QSize framebufferSize = view_ ? view_->framebufferSize() : headless_view_->size();
        text += QStringLiteral(" | Map %1 | View %2x%3")
                    .arg(MapDataManager::modeLabel(map_selection_.mode))
                    .arg(framebufferSize.width())
                    .arg(framebufferSize.height());
        text += QStringLiteral(" | DEM %1 | OSM %2")
                    .arg(selectedDemLabel(map_selection_.diagnostics),
                         selectedOsmLabel(map_selection_.diagnostics));
    }
    if (view_)
    {
        text += QStringLiteral(" | Seg %1x%2 | FPS %3 | Frame %4 ms | Track %5 ms")
                    .arg(stats.segmentCount)
                    .arg(stats.segmentSize)
                    .arg(stats.framesPerSecond, 0, 'f', 1)
                    .arg(stats.frameMs, 0, 'f', 1)
                    .arg(stats.trackUpdateMs, 0, 'f', 1);
    }
    if (displayLatest && displayLatest->hasLlh())
    {
        const QString satellitesText = displayLatest->satellites > 0
            ? QString::number(displayLatest->satellites)
            : QStringLiteral("--");
        const QString hdopText = std::isfinite(displayLatest->hdop)
            ? QString::number(displayLatest->hdop, 'f', 2)
            : QStringLiteral("--");
        text += QStringLiteral(" | Lat %1 Lon %2 H %3 m %4 | Fix %5 | Sats %6 | HDOP %7")
                    .arg(displayLatest->latDeg, 0, 'f', 7)
                    .arg(displayLatest->lonDeg, 0, 'f', 7)
                    .arg(displayLatest->heightM, 0, 'f', 2)
                    .arg(heightReferenceLabel(displayLatest->heightReference))
                    .arg(static_cast<int>(displayLatest->fixQuality))
                    .arg(satellitesText, hdopText);
        text += QStringLiteral(" | Height ref unchecked");
        text += QStringLiteral(" | Att %1").arg(attitudeSourceLabel(displayLatest));
    }
    if (replay_.hasSamples())
    {
        text += QStringLiteral(" | Replay %1/%2 %3x %4%5")
                    .arg(qMax(0, replay_.currentIndex() + 1))
                    .arg(replay_.sampleCount())
                    .arg(replay_.speed(), 0, 'g', 3)
                    .arg(replayTimeLabel())
                    .arg(replay_.isPlaying() ? QStringLiteral(" playing") : QStringLiteral(""));
    }
    status_label_->setText(text);
}

} // namespace VaporView::Map3D
