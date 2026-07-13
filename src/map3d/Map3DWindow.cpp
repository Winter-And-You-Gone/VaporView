#include "map3d/Map3DWindow.h"

#include "Map3DDiagnosticsFormatter.h"

#include "AppTheme.h"
#include "SingleLevelPopupMenu.h"
#include "geo/SessionTrackReader.h"
#include "geo/TrajectoryQuality.h"
#include "Map3DRuntime.h"
#include "map3d/OsgEarthViewWidget.h"

#include <QAction>
#include <QCloseEvent>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHideEvent>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QSettings>
#include <QSignalBlocker>
#include <QSlider>
#include <QShowEvent>
#include <QSpinBox>
#include <QStatusBar>
#include <QStringList>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWidget>
#include <QWidgetAction>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>

namespace VaporView::Map3D {
namespace {

constexpr qint64 kStatusUpdateIntervalMs = 200;
constexpr double kAutomaticSentinel2ImageryRangeM = 20000.0;

QString heightSafetyNote()
{
    return QStringLiteral("vertical reference applied for display; no AGL/terrain-clearance decision");
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

QString fixQualityLabel(VaporView::Geo::FixQuality quality)
{
    switch (quality)
    {
    case VaporView::Geo::FixQuality::Fixed:
        return QStringLiteral("Fixed");
    case VaporView::Geo::FixQuality::Float:
        return QStringLiteral("Float");
    case VaporView::Geo::FixQuality::Dgps:
        return QStringLiteral("DGPS");
    case VaporView::Geo::FixQuality::Single:
        return QStringLiteral("Single");
    case VaporView::Geo::FixQuality::Invalid:
        return QStringLiteral("Invalid");
    case VaporView::Geo::FixQuality::Unknown:
        return QStringLiteral("Unknown");
    }
    return QStringLiteral("Unknown");
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
    return QStringLiteral("%1 safe layers (water/roads, %2/4 files)")
        .arg(diagnostics.selectedOsmLayerCount)
        .arg(diagnostics.osmLayerCount);
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

QString tiandituKeySettingKey()
{
    return QStringLiteral("map/tianditu_key");
}

QString configuredTiandituKey()
{
    QSettings settings(QStringLiteral("VaporView"), QStringLiteral("TrajectoryViewer"));
    return settings.value(tiandituKeySettingKey()).toString().trimmed();
}

QString defaultSessionDataDirectory()
{
    return map3DProjectDataDirectory();
}

QString trajectorySampleDetailText(int sampleIndex, const VaporView::Geo::NavSample& sample)
{
    QStringList parts;
    parts << QStringLiteral("Selected #%1").arg(sampleIndex + 1);
    if (sample.hasLlh())
    {
        parts << QStringLiteral("Lat %1").arg(sample.latDeg, 0, 'f', 7)
              << QStringLiteral("Lon %1").arg(sample.lonDeg, 0, 'f', 7)
              << QStringLiteral("H %1 m %2")
                     .arg(sample.heightM, 0, 'f', 2)
                     .arg(heightReferenceLabel(sample.heightReference));
    }
    if (sample.hasEcef())
    {
        parts << QStringLiteral("ECEF %1, %2, %3 m")
                     .arg(sample.ecefXM, 0, 'f', 2)
                     .arg(sample.ecefYM, 0, 'f', 2)
                     .arg(sample.ecefZM, 0, 'f', 2);
    }
    parts << QStringLiteral("Fix %1").arg(fixQualityLabel(sample.fixQuality));
    if (sample.satellites > 0)
    {
        parts << QStringLiteral("Sats %1").arg(sample.satellites);
    }
    if (std::isfinite(sample.hdop))
    {
        parts << QStringLiteral("HDOP %1").arg(sample.hdop, 0, 'f', 2);
    }
    if (sample.recordTimestampUs > 0)
    {
        parts << QStringLiteral("rec %1").arg(sample.recordTimestampUs);
    }
    if (sample.deviceTimestampUs > 0)
    {
        parts << QStringLiteral("dev %1").arg(sample.deviceTimestampUs);
    }
    parts << QStringLiteral("Att %1").arg(attitudeSourceLabel(&sample));
    return parts.join(QStringLiteral(" | "));
}

MapDataSelection selectionForCustomEarth(const MapDataSelection& discovered,
                                         const QString& earthFile,
                                         const QString& description,
                                         const QString& diagnosticMessage)
{
    MapDataSelection selection = discovered;
    selection.mode = MapDataMode::NaturalEarth;
    selection.earthFile = earthFile;
    selection.earthFilePath = earthFile;
    selection.description = description;

    MapDataDiagnostics& diagnostics = selection.diagnostics;
    diagnostics.earthFilePath = earthFile;
    diagnostics.selectedBaseMode = MapDataMode::NaturalEarth;
    diagnostics.selectedBaseModeLabel = MapDataManager::modeLabel(MapDataMode::NaturalEarth);
    diagnostics.selectedBaseModeKey = MapDataManager::modeKey(MapDataMode::NaturalEarth);
    diagnostics.selectedBaseEarthFilePath = earthFile;
    diagnostics.real3DLocalReady = false;
    diagnostics.real3DLocalEarthPath.clear();
    diagnostics.local3DTilesAvailable = false;
    diagnostics.local3DTilesTilesetValid = false;
    diagnostics.local3DTilesHasExternalUris = false;
    diagnostics.local3DTilesTilesetPath.clear();
    diagnostics.local3DTilesResourceCount = 0;
    diagnostics.local3DTilesResourceUris.clear();
    diagnostics.local3DTilesMissingResources.clear();
    diagnostics.local3DTilesExternalUris.clear();
    diagnostics.local3DTilesDiagnostics.clear();
    diagnostics.selectedDemLayerAvailable = false;
    diagnostics.selectedOsmLayersAvailable = false;
    diagnostics.selectedElevationSource.clear();
    diagnostics.selectedFullLocalEarthPath.clear();
    diagnostics.messages.push_back(diagnosticMessage);
    return selection;
}

void configureMenuRow(VaporView::SingleLevelPopupMenuRow* row, int minimumWidth = 250)
{
    if (!row)
    {
        return;
    }
    row->setTextAlignment(VaporView::SingleLevelPopupTextAlignment::Left);
    row->setHorizontalPadding(18, 14);
    row->setRowSpacing(6);
    row->setCheckSlotWidth(0);
    row->setRowHeight(34);
    row->setMinimumRowWidth(minimumWidth);
}

int sanitizeMaxVisibleSamples(int value)
{
    return std::clamp(value, 1000, 1000000);
}

int firstVisibleIndex(const std::vector<VaporView::Geo::NavSample>& samples, int maxVisibleSamples)
{
    return (std::max)(0, static_cast<int>(samples.size()) - sanitizeMaxVisibleSamples(maxVisibleSamples));
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

QString replayStateLabel(const VaporView::Geo::TrajectoryReplay& replay)
{
    if (!replay.hasSamples())
    {
        return QStringLiteral("unloaded");
    }
    if (replay.isPlaying())
    {
        return QStringLiteral("playing");
    }
    if (replay.currentIndex() <= 0)
    {
        return QStringLiteral("stopped");
    }
    return QStringLiteral("paused");
}

} // namespace

Map3DWindow::Map3DWindow(QWidget* parent)
    : QMainWindow(parent)
    , status_label_(new QLabel(this))
    , replay_timer_(new QTimer(this))
    , sentinel2_auto_load_timer_(new QTimer(this))
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
        connect(view_,
                &OsgEarthViewWidget::trajectorySampleSelected,
                this,
                &Map3DWindow::showSelectedTrajectorySample);
        connect(view_, &OsgEarthViewWidget::trajectorySampleSelectionCleared, this, [this]() {
            clearSelectedTrajectorySample();
            updateStatus(nullptr);
        });
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
    VaporView::configureComboBoxPopup(replay_speed_combo_, VaporView::isDarkThemeEnabled());
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
            rebuildReplayAtElapsed(replaySliderValueToElapsed(value));
        }
    });

    follow_action_ = toolbar->addAction(QStringLiteral("跟随飞机"));
    follow_action_->setObjectName(QStringLiteral("map3DFollowAction"));
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
    replay_timer_->setInterval(static_cast<int>(replay_.interval().count()));
    connect(replay_timer_, &QTimer::timeout, this, &Map3DWindow::onReplayTick);

    sentinel2_auto_load_timer_->setTimerType(Qt::CoarseTimer);
    sentinel2_auto_load_timer_->setInterval(300);
    connect(sentinel2_auto_load_timer_, &QTimer::timeout, this, [this]() {
        if (view_)
        {
            maybeLoadSentinel2ImageryForRange(view_->earthCameraRangeM());
        }
    });

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
        setCameraNote(enabled ? QStringLiteral("Follow aircraft") : QStringLiteral("Manual/free camera"));
        updateStatus(nullptr);
    });

    QAction* loadEarthAction = toolbar->addAction(QStringLiteral("加载 Earth 文件"));
    connect(loadEarthAction, &QAction::triggered, this, &Map3DWindow::openEarthFile);

    local_imagery_menu_ = new VaporView::SingleLevelPopupMenu(this);
    local_imagery_menu_->setTitle(QStringLiteral("本地影像"));
    local_imagery_menu_->setObjectName(QStringLiteral("map3DLocalImageryMenu"));
    local_imagery_menu_->setPanelPadding(12);
    local_imagery_menu_->setCornerRadius(10);
    local_imagery_menu_->refreshTheme();
    local_imagery_action_ = toolbar->addAction(QStringLiteral("本地影像"));
    local_imagery_action_->setObjectName(QStringLiteral("map3DLocalImageryAction"));
    local_imagery_action_->setMenu(local_imagery_menu_);

    local_3d_tiles_action_ = toolbar->addAction(QStringLiteral("本地 OSG 建筑"));
    local_3d_tiles_action_->setObjectName(QStringLiteral("map3DLocal3DTilesAction"));
    local_3d_tiles_action_->setEnabled(false);
    local_3d_tiles_action_->setToolTip(QStringLiteral("加载 VaporView 原生 OSG 建筑瓦片；不支持通用 Cesium 3D Tiles"));
    local_3d_tiles_action_->setStatusTip(local_3d_tiles_action_->toolTip());
    connect(local_3d_tiles_action_, &QAction::triggered, this, &Map3DWindow::loadLocal3DTilesPreview);

    clear_local_3d_tiles_action_ = toolbar->addAction(QStringLiteral("清除 OSG 建筑"));
    clear_local_3d_tiles_action_->setObjectName(QStringLiteral("map3DClearLocal3DTilesAction"));
    clear_local_3d_tiles_action_->setEnabled(false);
    clear_local_3d_tiles_action_->setToolTip(QStringLiteral("清除当前本地 OSG 建筑叠加层"));
    clear_local_3d_tiles_action_->setStatusTip(clear_local_3d_tiles_action_->toolTip());
    connect(clear_local_3d_tiles_action_, &QAction::triggered, this, &Map3DWindow::clearLocal3DTilesPreview);

    load_aircraft_model_action_ = toolbar->addAction(QStringLiteral("加载飞机模型"));
    load_aircraft_model_action_->setObjectName(QStringLiteral("map3DLoadAircraftModelAction"));
    load_aircraft_model_action_->setToolTip(QStringLiteral("选择本地 .osgb/.osg/.glb/.gltf 飞机模型"));
    load_aircraft_model_action_->setStatusTip(load_aircraft_model_action_->toolTip());
    connect(load_aircraft_model_action_, &QAction::triggered, this, &Map3DWindow::openAircraftModel);

    reset_aircraft_model_action_ = toolbar->addAction(QStringLiteral("内置飞机标记"));
    reset_aircraft_model_action_->setObjectName(QStringLiteral("map3DResetAircraftModelAction"));
    reset_aircraft_model_action_->setToolTip(QStringLiteral("清除自定义飞机模型设置并恢复内置标记"));
    reset_aircraft_model_action_->setStatusTip(reset_aircraft_model_action_->toolTip());
    connect(reset_aircraft_model_action_, &QAction::triggered, this, &Map3DWindow::resetAircraftModel);

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
        QTimer::singleShot(0, this, [this]() {
            loadInitialEarthFile();
            const QString aircraftModelPath =
                QSettings(QStringLiteral("VaporView"), QStringLiteral("Map3D"))
                    .value(QStringLiteral("aircraftModelPath"))
                    .toString();
            if (!aircraftModelPath.isEmpty() && view_)
            {
                statusBar()->showMessage(QStringLiteral("正在后台加载飞机模型: %1").arg(aircraftModelPath));
                view_->loadAircraftModelAsync(aircraftModelPath, [this, aircraftModelPath](bool loaded) {
                    statusBar()->showMessage(
                        loaded
                            ? QStringLiteral("已加载飞机模型: %1").arg(aircraftModelPath)
                            : QStringLiteral("飞机模型加载失败，已保留当前标记: %1").arg(aircraftModelPath),
                        7000);
                    refreshDiagnosticsText();
                });
            }
        });
    }
}

Map3DWindow::~Map3DWindow()
{
    if (sentinel2_auto_load_timer_)
    {
        sentinel2_auto_load_timer_->stop();
    }
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
    rendered_replay_index_ = -1;
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
    rendered_replay_index_ = -1;
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
    rendered_replay_index_ = -1;
    clearSelectedTrajectorySample();
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
    VaporView::Geo::SessionTrackReadResult result = VaporView::Geo::readSessionTrack(sessionDir);
    if (!result.ok)
    {
        QMessageBox::warning(this,
                             QStringLiteral("Session Track"),
                             QStringLiteral("无法读取轨迹: %1").arg(result.error));
        return;
    }

    QSettings settings(QStringLiteral("VaporView"), QStringLiteral("Map3D"));
    settings.setValue(QStringLiteral("lastSessionDir"), QFileInfo(sessionDir).absoluteFilePath());

    const QString sourceCsvPath = result.sourceCsvPath;
    const auto samples = std::make_shared<const std::vector<VaporView::Geo::NavSample>>(
        std::move(result.samples));
    replay_timer_->stop();
    clearSelectedTrajectorySample();
    if (view_)
    {
        view_->setSamples(samples);
    }
    if (headless_view_)
    {
        headless_sample_count_ = static_cast<int>(samples->size());
        headless_samples_ = *samples;
    }
    replay_.setSamples(samples);
    rendered_replay_index_ = -1;
    latest_drop_source_.clear();
    latest_drop_reason_.clear();
    latest_drop_record_timestamp_us_ = 0;
    recordTrackSource(QStringLiteral("Session"),
                      replay_.currentSample(),
                      sourceCsvPath);
    resetAutomaticSentinel2Imagery();
    const bool focusedTrack = autoFocusTrack(QStringLiteral("Track auto"));
    applyConfiguredTiandituSatelliteImagery(false);
    updateReplayUi();
    updateStatus(replay_.currentSample());
    if (sentinel2_auto_load_timer_
        && !tianditu_satellite_imagery_loaded_
        && !isSentinel2ImageryActive())
    {
        sentinel2_auto_load_timer_->start();
        QTimer::singleShot(0, this, [this]() {
            if (view_)
            {
                maybeLoadSentinel2ImageryForRange(view_->earthCameraRangeM());
            }
        });
    }
    statusBar()->showMessage(QStringLiteral("Loaded %1 samples from %2%3")
                                 .arg(samples->size())
                                 .arg(sourceCsvPath,
                                      focusedTrack ? QStringLiteral(" (auto-focused track)") : QString()),
                             5000);
}

void Map3DWindow::noteLiveSampleDrop(const QString& source, const QString& reason, qint64 recordTimestampUs)
{
    latest_drop_source_ = source.isEmpty() ? QStringLiteral("Live") : source;
    latest_drop_reason_ = reason.isEmpty() ? QStringLiteral("unknown") : reason;
    latest_drop_record_timestamp_us_ = recordTimestampUs;
    refreshDiagnosticsText();
    updateStatus(nullptr);
}
void Map3DWindow::loadInitialEarthFile()
{
    const MapDataSelection autoSelection = map_data_manager_.selectBestAvailableMap();
    QSettings settings(QStringLiteral("VaporView"), QStringLiteral("Map3D"));
    const QString persistedPath = settings.value(QStringLiteral("lastEarthFile")).toString();
    const bool hasPersistedCustomEarth =
        !persistedPath.isEmpty() && !map_data_manager_.isBuiltInEarthFile(persistedPath);
    const QString initialPath = hasPersistedCustomEarth ? persistedPath : autoSelection.earthFile;
    if (!view_ || !QFileInfo(initialPath).isFile())
    {
        latest_earth_load_ = {};
        latest_earth_load_.requestedPath = initialPath;
        latest_earth_load_.failureReason = QStringLiteral("Selected earth file does not exist.");
        setMapSelection(autoSelection);
        statusBar()->showMessage(QStringLiteral("未找到默认 Earth 文件，当前显示本地 NED 网格。"), 8000);
        return;
    }

    statusBar()->showMessage(QStringLiteral("正在后台加载 Earth 文件: %1").arg(initialPath));
    view_->loadEarthFileAsync(initialPath, [this, autoSelection, initialPath, hasPersistedCustomEarth](bool loaded) {
        latest_earth_load_ = view_ ? view_->earthLoadDiagnostics() : EarthLoadDiagnostics{};
        if (!loaded && hasPersistedCustomEarth)
        {
            QSettings(QStringLiteral("VaporView"), QStringLiteral("Map3D"))
                .remove(QStringLiteral("lastEarthFile"));
            const QString fallbackPath = autoSelection.earthFile;
            if (view_ && QFileInfo(fallbackPath).isFile())
            {
                statusBar()->showMessage(QStringLiteral("自定义 Earth 失败，正在加载自动地图: %1").arg(fallbackPath));
                view_->loadEarthFileAsync(fallbackPath, [this, autoSelection, fallbackPath](bool fallbackLoaded) {
                    latest_earth_load_ = view_ ? view_->earthLoadDiagnostics() : EarthLoadDiagnostics{};
                    if (!fallbackLoaded)
                    {
                        setMapSelection(autoSelection);
                        statusBar()->showMessage(QStringLiteral("自动加载 Earth 文件失败，已保留当前场景。"), 8000);
                        return;
                    }
                    setMapSelection(autoSelection);
                    QSettings(QStringLiteral("VaporView"), QStringLiteral("Map3D"))
                        .setValue(QStringLiteral("lastEarthFile"), fallbackPath);
                    if (autoSelection.diagnostics.real3DLocalReady)
                    {
                        loadConfiguredLocal3DTiles(false);
                    }
                    applyConfiguredTiandituSatelliteImagery(false);
                    if (sentinel2_auto_load_timer_
                        && !tianditu_satellite_imagery_loaded_
                        && !isSentinel2ImageryActive())
                    {
                        sentinel2_auto_load_timer_->start();
                    }
                    updateStatus(nullptr);
                    statusBar()->showMessage(QStringLiteral("已自动加载 Earth 文件: %1").arg(fallbackPath), 5000);
                });
                return;
            }
        }
        if (!loaded)
        {
            setMapSelection(autoSelection);
            statusBar()->showMessage(QStringLiteral("自动加载 Earth 文件失败，已保留当前场景: %1").arg(initialPath), 8000);
            return;
        }
        const MapDataSelection activeSelection = initialPath == autoSelection.earthFile
            ? autoSelection
            : selectionForCustomEarth(autoSelection,
                                      initialPath,
                                      QStringLiteral("User-selected custom Earth scene."),
                                      QStringLiteral("Using user-selected custom earth file."));
        setMapSelection(activeSelection);
        resetAutomaticSentinel2Imagery();
        QSettings(QStringLiteral("VaporView"), QStringLiteral("Map3D"))
            .setValue(QStringLiteral("lastEarthFile"), initialPath);
        if (activeSelection.diagnostics.real3DLocalReady)
        {
            loadConfiguredLocal3DTiles(false);
        }
        applyConfiguredTiandituSatelliteImagery(false);
        if (sentinel2_auto_load_timer_
            && !tianditu_satellite_imagery_loaded_
            && !isSentinel2ImageryActive())
        {
            sentinel2_auto_load_timer_->start();
        }
        updateStatus(nullptr);
        statusBar()->showMessage(QStringLiteral("已自动加载 Earth 文件: %1").arg(initialPath), 5000);
    });
}
void Map3DWindow::openSessionDirectory()
{
    QSettings settings(QStringLiteral("VaporView"), QStringLiteral("Map3D"));
    QString initial = settings.value(QStringLiteral("lastSessionDir")).toString();
    if (initial.isEmpty() || !QFileInfo(initial).isDir())
    {
        initial = defaultSessionDataDirectory();
    }
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
    const QString file = QFileDialog::getOpenFileName(
        this, QStringLiteral("加载 Earth 文件"),
        settings.value(QStringLiteral("lastEarthFile")).toString(),
        QStringLiteral("osgEarth (*.earth);;All Files (*)"));
    if (file.isEmpty() || !view_) return;
    const MapDataSelection selection =
        selectionForCustomEarth(map_data_manager_.selectBestAvailableMap(), file,
                                QStringLiteral("User-selected custom Earth scene."),
                                QStringLiteral("Loaded user-selected earth file."));
    statusBar()->showMessage(QStringLiteral("正在后台加载 Earth 文件: %1").arg(file));
    view_->loadEarthFileAsync(file, [this, file, selection](bool loaded) {
        latest_earth_load_ = view_ ? view_->earthLoadDiagnostics() : EarthLoadDiagnostics{};
        if (!loaded)
        {
            QMessageBox::warning(this, QStringLiteral("osgEarth"),
                                 QStringLiteral("无法加载 Earth 文件: %1").arg(file));
            return;
        }
        QSettings(QStringLiteral("VaporView"), QStringLiteral("Map3D"))
            .setValue(QStringLiteral("lastEarthFile"), file);
        setMapSelection(selection);
        resetAutomaticSentinel2Imagery();
        applyConfiguredTiandituSatelliteImagery(false);
        if (sentinel2_auto_load_timer_
            && !tianditu_satellite_imagery_loaded_
            && !isSentinel2ImageryActive())
        {
            sentinel2_auto_load_timer_->start();
        }
        const bool focusedTrack = autoFocusTrack(QStringLiteral("Track auto"));
        updateStatus(nullptr);
        statusBar()->showMessage(QStringLiteral("Loaded earth file: %1%2")
                                     .arg(file, focusedTrack ? QStringLiteral(" (auto-focused track)") : QString()),
                                 5000);
    });
}
void Map3DWindow::loadLocalImageryTemplate(const LocalImageryOption& option)
{
    if (!option.available || !view_)
    {
        statusBar()->showMessage(QStringLiteral("本地影像不可用: %1").arg(option.label), 5000);
        return;
    }

    const MapDataSelection selection =
        selectionForCustomEarth(map_data_manager_.selectBestAvailableMap(),
                                option.earthFilePath,
                                QStringLiteral("Natural Earth background with %1 overlay.").arg(option.label),
                                QStringLiteral("Loaded optional local imagery template: %1").arg(option.label));
    statusBar()->showMessage(QStringLiteral("正在后台加载本地影像: %1").arg(option.label));
    view_->loadEarthFileAsync(option.earthFilePath, [this, option, selection](bool loaded) {
        latest_earth_load_ = view_ ? view_->earthLoadDiagnostics() : EarthLoadDiagnostics{};
        if (!loaded)
        {
            statusBar()->showMessage(QStringLiteral("加载本地影像失败: %1").arg(option.earthFilePath), 8000);
            return;
        }
        setMapSelection(selection);
        resetAutomaticSentinel2Imagery();
        QSettings(QStringLiteral("VaporView"), QStringLiteral("Map3D"))
            .setValue(QStringLiteral("lastEarthFile"), option.earthFilePath);
        applyConfiguredTiandituSatelliteImagery(false);
        if (sentinel2_auto_load_timer_
            && !tianditu_satellite_imagery_loaded_
            && !isSentinel2ImageryActive())
        {
            sentinel2_auto_load_timer_->start();
        }
        const bool focusedTrack = autoFocusTrack(QStringLiteral("Track auto"));
        updateStatus(nullptr);
        statusBar()->showMessage(QStringLiteral("已加载本地影像: %1%2")
                                     .arg(option.label,
                                          focusedTrack ? QStringLiteral(" (已自动定位轨迹)") : QString()),
                                 5000);
    });
}

void Map3DWindow::maybeLoadSentinel2ImageryForRange(double rangeM)
{
    if (!view_
        || automatic_sentinel2_imagery_loaded_
        || automatic_sentinel2_imagery_loading_
        || tianditu_satellite_imagery_loaded_
        || !std::isfinite(rangeM)
        || rangeM > kAutomaticSentinel2ImageryRangeM
        || isSentinel2ImageryActive())
    {
        return;
    }

    const MapDataSelection bestSelection = map_data_manager_.selectBestAvailableMap();
    if (bestSelection.diagnostics.localImageryOptions.empty())
    {
        return;
    }

    const auto optionIt = std::find_if(bestSelection.diagnostics.localImageryOptions.cbegin(),
                                       bestSelection.diagnostics.localImageryOptions.cend(),
                                       [](const LocalImageryOption& option) {
        return option.key == QStringLiteral("sentinel2") && option.available;
    });
    if (optionIt == bestSelection.diagnostics.localImageryOptions.cend())
    {
        return;
    }
    const LocalImageryOption option = *optionIt;

    automatic_sentinel2_imagery_loading_ = true;
    const MapDataSelection selection =
        selectionForCustomEarth(bestSelection,
                                option.earthFilePath,
                                QStringLiteral("Natural Earth background with %1 overlay.").arg(option.label),
                                QStringLiteral("Automatically loaded local imagery template after zoom: %1")
                                    .arg(option.label));
    statusBar()->showMessage(QStringLiteral("放大到近景，正在自动加载 Sentinel-2 本地影像..."));
    view_->loadEarthFilePreservingViewAsync(option.earthFilePath, [this, option, selection](bool loaded) {
        automatic_sentinel2_imagery_loading_ = false;
        latest_earth_load_ = view_ ? view_->earthLoadDiagnostics() : EarthLoadDiagnostics{};
        if (!loaded)
        {
            if (sentinel2_auto_load_timer_)
            {
                sentinel2_auto_load_timer_->stop();
            }
            statusBar()->showMessage(QStringLiteral("自动加载 Sentinel-2 本地影像失败: %1").arg(option.earthFilePath), 8000);
            return;
        }
        automatic_sentinel2_imagery_loaded_ = true;
        setMapSelection(selection);
        if (sentinel2_auto_load_timer_)
        {
            sentinel2_auto_load_timer_->stop();
        }
        updateStatus(nullptr, true);
        statusBar()->showMessage(QStringLiteral("已自动加载 Sentinel-2 本地影像，已保留当前视角。"), 5000);
    });
}

void Map3DWindow::resetAutomaticSentinel2Imagery()
{
    automatic_sentinel2_imagery_loaded_ = false;
    automatic_sentinel2_imagery_loading_ = false;
}

bool Map3DWindow::isSentinel2ImageryActive() const
{
    const QString earthPath = !map_selection_.earthFilePath.isEmpty()
        ? map_selection_.earthFilePath
        : map_selection_.earthFile;
    const QString fileName = QFileInfo(earthPath).fileName();
    return fileName.compare(QStringLiteral("vaporview_with_sentinel2_imagery.earth"),
                            Qt::CaseInsensitive) == 0
        || fileName.compare(QStringLiteral("vaporview_real3d_local.earth"),
                            Qt::CaseInsensitive) == 0;
}

void Map3DWindow::loadLocal3DTilesPreview()
{
    loadConfiguredLocal3DTiles(true);
}

bool Map3DWindow::applyConfiguredTiandituSatelliteImagery(bool showStatusMessage)
{
    const bool hadTiandituLayer = tianditu_satellite_imagery_loaded_;
    tianditu_satellite_imagery_loaded_ = false;
    if (!view_ || !view_->hasEarthMap())
    {
        return false;
    }

    const QString key = configuredTiandituKey();
    if (key.isEmpty())
    {
        if (hadTiandituLayer)
        {
            view_->applyTiandituSatelliteImagery(QString());
            latest_earth_load_ = view_->earthLoadDiagnostics();
            updateStatus(nullptr, true);
        }
        if (showStatusMessage)
        {
            statusBar()->showMessage(QStringLiteral("未配置天地图 Key，3D 地图保留本地离线影像。"), 6000);
        }
        return false;
    }

    tianditu_satellite_imagery_loaded_ = view_->applyTiandituSatelliteImagery(key);
    latest_earth_load_ = view_->earthLoadDiagnostics();
    if (tianditu_satellite_imagery_loaded_ && sentinel2_auto_load_timer_)
    {
        sentinel2_auto_load_timer_->stop();
    }
    if (showStatusMessage)
    {
        statusBar()->showMessage(
            tianditu_satellite_imagery_loaded_
                ? QStringLiteral("已加载天地图卫星影像。")
                : QStringLiteral("天地图卫星影像加载失败，已保留当前本地影像。"),
            6000);
    }
    updateStatus(nullptr, true);
    return tianditu_satellite_imagery_loaded_;
}

bool Map3DWindow::loadConfiguredLocal3DTiles(bool showStatusMessage)
{
    const MapDataDiagnostics diagnostics = map_selection_.diagnostics;
    if (!diagnostics.local3DTilesTilesetValid || !view_)
    {
        latest_local_3d_tiles_load_ = {};
        latest_local_3d_tiles_load_.requestedPath = diagnostics.local3DTilesTilesetPath;
        latest_local_3d_tiles_load_.failureReason =
            QStringLiteral("Native OSG building tiles contract is not valid; open Map Data Diagnostics for details.");
        refreshDiagnosticsText();
        if (showStatusMessage)
        {
            statusBar()->showMessage(QStringLiteral("本地 OSG 建筑瓦片契约无效，请先查看地图诊断。"), 8000);
        }
        return false;
    }

    if (showStatusMessage)
    {
        statusBar()->showMessage(QStringLiteral("正在后台加载本地 3D 建筑..."));
    }
    view_->loadLocal3DTilesPreviewAsync(
        diagnostics.local3DTilesTilesetPath,
        [this, diagnostics, showStatusMessage](bool loaded) {
            latest_local_3d_tiles_load_ =
                view_ ? view_->local3DTilesLoadDiagnostics() : Local3DTilesLoadDiagnostics{};
            refreshDiagnosticsText();
            updateStatus(nullptr);
            if (clear_local_3d_tiles_action_)
            {
                clear_local_3d_tiles_action_->setEnabled(view_ && view_->hasLocal3DTilesPreview());
            }
            if (!showStatusMessage)
            {
                return;
            }
            statusBar()->showMessage(
                loaded
                    ? QStringLiteral("已加载本地 OSG 建筑叠加层: %1 个 payload")
                          .arg(latest_local_3d_tiles_load_.loadedPayloadCount)
                    : QStringLiteral("本地 3D 建筑加载失败: %1")
                          .arg(latest_local_3d_tiles_load_.failureReason.isEmpty()
                                   ? diagnostics.local3DTilesTilesetPath
                                   : latest_local_3d_tiles_load_.failureReason),
                loaded ? 6000 : 9000);
        });
    return true;
}

void Map3DWindow::clearLocal3DTilesPreview()
{
    if (view_)
    {
        view_->clearLocal3DTilesPreview();
    }
    latest_local_3d_tiles_load_ = {};
    latest_local_3d_tiles_load_.failureReason = QStringLiteral("Local OSG building overlay cleared.");
    if (clear_local_3d_tiles_action_)
    {
        clear_local_3d_tiles_action_->setEnabled(false);
    }
    refreshDiagnosticsText();
    updateStatus(nullptr);
    statusBar()->showMessage(QStringLiteral("已清除本地 OSG 建筑叠加层。"), 5000);
}

void Map3DWindow::openAircraftModel()
{
    QSettings settings(QStringLiteral("VaporView"), QStringLiteral("Map3D"));
    const QString initial = settings.value(QStringLiteral("aircraftModelPath")).toString();
    const QString file = QFileDialog::getOpenFileName(this,
                                                      QStringLiteral("加载飞机模型"),
                                                      initial,
                                                      QStringLiteral("3D Models (*.osgb *.osg *.glb *.gltf);;All Files (*)"));
    if (file.isEmpty() || !view_)
    {
        return;
    }

    statusBar()->showMessage(QStringLiteral("正在后台加载飞机模型: %1").arg(file));
    view_->loadAircraftModelAsync(file, [this, file](bool loaded) {
        refreshDiagnosticsText();
        updateStatus(nullptr);
        if (!loaded)
        {
            QMessageBox::warning(this,
                                 QStringLiteral("飞机模型"),
                                 QStringLiteral("无法加载飞机模型，已保留当前标记: %1").arg(file));
            return;
        }
        QSettings(QStringLiteral("VaporView"), QStringLiteral("Map3D"))
            .setValue(QStringLiteral("aircraftModelPath"), file);
        statusBar()->showMessage(QStringLiteral("已加载飞机模型: %1").arg(file), 6000);
    });
}

void Map3DWindow::resetAircraftModel()
{
    QSettings settings(QStringLiteral("VaporView"), QStringLiteral("Map3D"));
    settings.remove(QStringLiteral("aircraftModelPath"));
    if (view_)
    {
        view_->resetAircraftModelToBuiltIn();
    }
    refreshDiagnosticsText();
    updateStatus(nullptr);
    statusBar()->showMessage(QStringLiteral("已恢复内置飞机标记。"), 5000);
}

void Map3DWindow::reloadBestLocalMap()
{
    const MapDataSelection selection = map_data_manager_.selectBestAvailableMap();
    if (!view_ || !QFileInfo(selection.earthFile).isFile())
    {
        latest_earth_load_ = {};
        latest_earth_load_.requestedPath = selection.earthFile;
        latest_earth_load_.failureReason = QStringLiteral("Selected earth file does not exist.");
        updateStatus(nullptr);
        statusBar()->showMessage(QStringLiteral("未找到可用的本地地图数据，已保留当前地图。"), 8000);
        return;
    }

    statusBar()->showMessage(QStringLiteral("正在后台重载最佳本地地图: %1").arg(selection.earthFile));
    view_->loadEarthFileAsync(selection.earthFile, [this, selection](bool loaded) {
        latest_earth_load_ = view_ ? view_->earthLoadDiagnostics() : EarthLoadDiagnostics{};
        if (!loaded)
        {
            updateStatus(nullptr);
            statusBar()->showMessage(QStringLiteral("重载最佳本地地图失败，已保留当前地图: %1")
                                         .arg(selection.earthFile),
                                     8000);
            return;
        }
        setMapSelection(selection);
        resetAutomaticSentinel2Imagery();
        if (selection.diagnostics.real3DLocalReady)
        {
            loadConfiguredLocal3DTiles(false);
        }
        applyConfiguredTiandituSatelliteImagery(false);
        if (sentinel2_auto_load_timer_
            && !tianditu_satellite_imagery_loaded_
            && !isSentinel2ImageryActive())
        {
            sentinel2_auto_load_timer_->start();
        }
        QSettings(QStringLiteral("VaporView"), QStringLiteral("Map3D"))
            .setValue(QStringLiteral("lastEarthFile"), selection.earthFile);
        const bool focusedTrack = autoFocusTrack(QStringLiteral("Track auto"));
        updateStatus(nullptr);
        statusBar()->showMessage(QStringLiteral("已重载最佳本地地图: %1%2")
                                     .arg(selection.earthFile,
                                          focusedTrack ? QStringLiteral(" (已自动定位轨迹)") : QString()),
                                 6000);
    });
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

    refreshDiagnosticsText(true);
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
        renderReplayAtCurrentPosition();
        replay_tick_clock_.restart();
        replay_timer_->start(static_cast<int>(replay_.interval().count()));
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
        : static_cast<qint64>(replay_.interval().count());
    const auto delta = VaporView::Geo::TrajectoryReplay::Duration(
        static_cast<qint64>(std::llround(static_cast<double>(elapsedMs) * 1000.0 * replay_.speed())));
    replay_.stepBy(delta);
    renderReplayAtCurrentPosition();
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
    rebuildReplayAtElapsed(replaySliderValueToElapsed(value));
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
    replay_timer_->setInterval(static_cast<int>(replay_.interval().count()));
    if (replay_.isPlaying())
    {
        replay_tick_clock_.restart();
    }
    QSettings settings(QStringLiteral("VaporView"), QStringLiteral("Map3D"));
    settings.setValue(QStringLiteral("replaySpeed"), replay_.speed());
    refreshDiagnosticsText();
    updateStatus(nullptr);
}

void Map3DWindow::rebuildReplayAt(int index, bool forceStatus)
{
    if (!replay_.hasSamples())
    {
        return;
    }
    replay_.seek(index);
    renderReplayAtCurrentPosition(forceStatus);
}

void Map3DWindow::closeEvent(QCloseEvent* event)
{
    if (sentinel2_auto_load_timer_)
    {
        sentinel2_auto_load_timer_->stop();
    }
    QMainWindow::closeEvent(event);
}

void Map3DWindow::showEvent(QShowEvent* event)
{
    QMainWindow::showEvent(event);
    if (sentinel2_auto_load_timer_
        && view_
        && view_->hasEarthMap()
        && !tianditu_satellite_imagery_loaded_
        && !isSentinel2ImageryActive()
        && !sentinel2_auto_load_timer_->isActive())
    {
        sentinel2_auto_load_timer_->start();
    }
    if (replay_.isPlaying() && replay_timer_ && !replay_timer_->isActive())
    {
        replay_tick_clock_.restart();
        replay_timer_->start(static_cast<int>(replay_.interval().count()));
    }
}

void Map3DWindow::hideEvent(QHideEvent* event)
{
    if (sentinel2_auto_load_timer_)
    {
        sentinel2_auto_load_timer_->stop();
    }
    if (replay_timer_)
    {
        replay_timer_->stop();
    }
    replay_tick_clock_.invalidate();
    QMainWindow::hideEvent(event);
}

void Map3DWindow::renderReplayAtCurrentPosition(bool forceStatus)
{
    if (!replay_.hasSamples())
    {
        return;
    }

    const int targetIndex = replay_.currentIndex();
    const std::vector<VaporView::Geo::NavSample>& replaySamples = replay_.samples();
    const auto replayStorage = replay_.sampleStorage();
    if (targetIndex < 0 || targetIndex >= static_cast<int>(replaySamples.size()))
    {
        return;
    }

    if (rendered_replay_index_ < 0 || targetIndex < rendered_replay_index_)
    {
        if (view_)
        {
            view_->setSamples(replayStorage, targetIndex + 1);
        }
        if (headless_view_)
        {
            const auto end = replaySamples.cbegin() + targetIndex + 1;
            const std::vector<VaporView::Geo::NavSample> visibleSamples(replaySamples.cbegin(), end);
            headless_samples_ = visibleSamples;
            headless_sample_count_ = static_cast<int>(visibleSamples.size());
        }
    }
    else if (targetIndex > rendered_replay_index_)
    {
        for (int index = rendered_replay_index_ + 1; index <= targetIndex; ++index)
        {
            const VaporView::Geo::NavSample& sample = replaySamples[static_cast<std::size_t>(index)];
            if (view_)
            {
                view_->appendSampleFromStorage(replayStorage, index);
            }
            if (headless_view_)
            {
                headless_samples_.push_back(sample);
                headless_sample_count_ = static_cast<int>(headless_samples_.size());
            }
        }
    }

    rendered_replay_index_ = targetIndex;
    const VaporView::Geo::NavSample& currentSample = replaySamples[static_cast<std::size_t>(targetIndex)];
    recordTrackSource(QStringLiteral("Replay"),
                      &currentSample);
    updateStatus(&currentSample, forceStatus);
}

void Map3DWindow::rebuildReplayAtElapsed(VaporView::Geo::TrajectoryReplay::Duration elapsed)
{
    if (!replay_.hasSamples())
    {
        return;
    }
    replay_.seekElapsed(elapsed);
    renderReplayAtCurrentPosition();
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
    const qint64 durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        replay_.duration() + std::chrono::microseconds(999)).count();
    return static_cast<int>(std::clamp<qint64>(durationMs, 0, (std::numeric_limits<int>::max)()));
}

int Map3DWindow::replaySliderValue() const
{
    const qint64 elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        replay_.elapsed() + std::chrono::microseconds(999)).count();
    return static_cast<int>(std::clamp<qint64>(elapsedMs, 0, replaySliderMaximum()));
}

VaporView::Geo::TrajectoryReplay::Duration Map3DWindow::replaySliderValueToElapsed(int value) const
{
    return std::chrono::duration_cast<VaporView::Geo::TrajectoryReplay::Duration>(
        std::chrono::milliseconds(std::clamp(value, 0, replaySliderMaximum())));
}

QString Map3DWindow::replayTimeLabel() const
{
    const double elapsedS = std::chrono::duration<double>(replay_.elapsed()).count();
    const double durationS = std::chrono::duration<double>(replay_.duration()).count();
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
            auto* row = new VaporView::SingleLevelPopupMenuRow(local_imagery_menu_);
            row->setText(imageryOptionLabel(option));
            configureMenuRow(row);
            row->setEnabled(option.available);
            row->setToolTip(QStringLiteral("%1\nVRT: %2\nEarth: %3")
                                .arg(option.available ? QStringLiteral("可加载") : QStringLiteral("缺少本地 VRT 或 earth 模板"),
                                     option.vrtPath,
                                     option.earthFilePath));
            QWidgetAction* action = local_imagery_menu_->addRow(row);
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
        local_imagery_action_->setEnabled(map_selection_.diagnostics.localImageryMenuAvailable);
    }
    if (local_3d_tiles_action_)
    {
        const bool enabled = map_selection_.diagnostics.local3DTilesTilesetValid;
        local_3d_tiles_action_->setEnabled(enabled);
        local_3d_tiles_action_->setToolTip(
            enabled
                ? QStringLiteral("加载本地 OSG 建筑瓦片: %1")
                      .arg(map_selection_.diagnostics.local3DTilesTilesetPath)
                : QStringLiteral("本地 OSG 建筑瓦片不可用或契约无效；请查看地图诊断"));
        local_3d_tiles_action_->setStatusTip(local_3d_tiles_action_->toolTip());
    }
    refreshDiagnosticsText();
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
    refreshDiagnosticsText();
}

void Map3DWindow::refreshDiagnosticsText(bool force)
{
    if (!diagnostics_text_ || !diagnostics_dialog_)
    {
        return;
    }
    if (force || diagnostics_dialog_->isVisible())
    {
        diagnostics_text_->setPlainText(diagnosticsText());
    }
}
QString Map3DWindow::diagnosticsText() const
{
    const Map3DPerformanceStats stats = view_ ? view_->performanceStats() : Map3DPerformanceStats{};
    Map3DDiagnosticsContext context;
    context.mapSelection = map_selection_;
    context.earthLoad = latest_earth_load_;
    context.tilesLoad = latest_local_3d_tiles_load_;
    context.aircraftModel = view_ ? view_->aircraftModelDiagnostics() : AircraftModelDiagnostics{};
    context.performance = stats;
    context.totalSamples = view_ ? stats.totalSamples : headless_sample_count_;
    context.visibleSamples = view_
        ? stats.visibleSamples
        : (std::min)(headless_sample_count_, max_visible_samples_);
    context.maxVisibleSamples = view_ ? stats.maxVisibleSamples : max_visible_samples_;
    context.qualityStats = view_
        ? stats.qualityStats
        : qualityStatsForSamples(headless_samples_, max_visible_samples_);
    context.trackSource = latest_track_source_;
    context.replayState = replayStateLabel(replay_);
    context.hasReplay = replay_.hasSamples();
    context.replayIndex = replay_.currentIndex();
    context.replaySampleCount = replay_.sampleCount();
    context.replaySpeed = replay_.speed();
    context.replayTime = replayTimeLabel();
    context.latestRecordTimestampUs = latest_track_record_timestamp_us_;
    context.latestDeviceTimestampUs = latest_track_device_timestamp_us_;
    context.attitudeSource =
        attitudeSourceLabel(has_latest_status_sample_ ? &latest_status_sample_ : nullptr);
    context.followAircraft = follow_action_ && follow_action_->isChecked();
    context.hasLatestLlh = has_latest_status_sample_ && latest_status_sample_.hasLlh();
    if (context.hasLatestLlh)
    {
        context.heightReference = heightReferenceLabel(latest_status_sample_.heightReference);
        context.fixQuality = fixQualityLabel(latest_status_sample_.fixQuality);
        context.heightSafetyNote = heightSafetyNote();
    }
    context.trackNote = latest_track_note_;
    context.dropSource = latest_drop_source_;
    context.dropReason = latest_drop_reason_;
    context.dropRecordTimestampUs = latest_drop_record_timestamp_us_;
    context.cameraNote = latest_camera_note_;
    return formatMap3DDiagnostics(context);
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
    refreshDiagnosticsText();
}

void Map3DWindow::showSelectedTrajectorySample(int sampleIndex, const VaporView::Geo::NavSample& sample)
{
    selected_track_sample_index_ = sampleIndex;
    selected_track_sample_ = sample;
    has_selected_track_sample_ = true;
    const QString detail = trajectorySampleDetailText(sampleIndex, sample);
    statusBar()->showMessage(detail, 12000);
    updateStatus(nullptr);
}

void Map3DWindow::clearSelectedTrajectorySample()
{
    selected_track_sample_index_ = -1;
    has_selected_track_sample_ = false;
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
    const int visibleSamples = view_ ? stats.visibleSamples : (std::min)(headless_sample_count_, max_visible_samples_);
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
    text += QStringLiteral(" | Follow %1")
                .arg(follow_action_ && follow_action_->isChecked() ? QStringLiteral("On") : QStringLiteral("Off"));
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
        if (tianditu_satellite_imagery_loaded_)
        {
            text += QStringLiteral(" | Imagery 天地图卫星");
        }
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
                    .arg(fixQualityLabel(displayLatest->fixQuality))
                    .arg(satellitesText, hdopText);
        if (displayLatest->heightReference == VaporView::Geo::HeightReference::Wgs84Ellipsoid)
        {
            text += QStringLiteral(" | Height ref applied");
        }
        else if (displayLatest->heightReference == VaporView::Geo::HeightReference::Unknown)
        {
            text += QStringLiteral(" | Height ref assumed WGS84");
        }
        else
        {
            text += QStringLiteral(" | Height ref uses recorded ECEF when available");
        }
        text += QStringLiteral(" | Att %1").arg(attitudeSourceLabel(displayLatest));
    }
    if (replay_.hasSamples())
    {
        text += QStringLiteral(" | Replay %1 %2/%3 %4x %5")
                    .arg(replayStateLabel(replay_))
                    .arg((std::max)(0, replay_.currentIndex() + 1))
                    .arg(replay_.sampleCount())
                    .arg(replay_.speed(), 0, 'g', 3)
                    .arg(replayTimeLabel());
    }
    if (has_selected_track_sample_)
    {
        text += QStringLiteral(" | Selected #%1").arg(selected_track_sample_index_ + 1);
        if (selected_track_sample_.hasLlh())
        {
            text += QStringLiteral(" Lat %1 Lon %2 H %3 m | Fix %4")
                        .arg(selected_track_sample_.latDeg, 0, 'f', 7)
                        .arg(selected_track_sample_.lonDeg, 0, 'f', 7)
                        .arg(selected_track_sample_.heightM, 0, 'f', 2)
                        .arg(fixQualityLabel(selected_track_sample_.fixQuality));
        }
    }
    status_label_->setText(text);
}

} // namespace VaporView::Map3D
