#include "map3d/Map3DWindow.h"

#include "geo/SessionTrackReader.h"
#include "map3d/OsgEarthViewWidget.h"

#include <QAction>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QSettings>
#include <QSignalBlocker>
#include <QSlider>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWidget>

namespace VaporView::Map3D {
namespace {

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
        return QStringLiteral("off");
    }
    return QStringLiteral("%1 layers").arg(diagnostics.selectedOsmLayerCount);
}

} // namespace

Map3DWindow::Map3DWindow(QWidget* parent)
    : QMainWindow(parent)
    , status_label_(new QLabel(this))
    , replay_timer_(new QTimer(this))
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
            rebuildReplayAt(value);
        }
    });

    follow_action_ = toolbar->addAction(QStringLiteral("跟随飞机"));
    follow_action_->setCheckable(true);
    QSettings settings(QStringLiteral("VaporView"), QStringLiteral("Map3D"));
    max_visible_samples_ = settings.value(QStringLiteral("maxVisibleSamples"), 200000).toInt();
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
    connect(diagnostics_action_, &QAction::triggered, this, &Map3DWindow::showMapDiagnostics);

    statusBar()->addPermanentWidget(status_label_, 1);
    updateReplayUi();
    updateStatus(nullptr);
    if (isMap3DHeadlessTest())
    {
        setMapSelection(map_data_manager_.selectBestAvailableMap());
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
    }
    updateReplayUi();
    updateStatus(&sample);
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
    }
    updateReplayUi();
    updateStatus(samples.empty() ? nullptr : &samples.back());
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
    }
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
    }
    replay_.setSamples(result.samples);
    updateReplayUi();
    updateStatus(replay_.currentSample());
    statusBar()->showMessage(QStringLiteral("Loaded %1 samples from %2")
                                 .arg(result.samples.size())
                                 .arg(result.sourceCsvPath),
                             5000);
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
        setMapSelection(autoSelection);
        statusBar()->showMessage(QStringLiteral("未找到默认 Earth 文件，当前显示本地 NED 网格。"), 8000);
        return;
    }

    const bool loaded = view_ ? view_->loadEarthFile(initialEarthFile) : false;
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
    updateStatus(nullptr);
    statusBar()->showMessage(QStringLiteral("Loaded earth file: %1").arg(file), 5000);
}

void Map3DWindow::reloadBestLocalMap()
{
    const MapDataSelection selection = map_data_manager_.selectBestAvailableMap();
    if (!QFileInfo(selection.earthFile).isFile())
    {
        setMapSelection(selection);
        updateStatus(nullptr);
        statusBar()->showMessage(QStringLiteral("未找到完整本地地图数据，保持本地网格显示。"), 8000);
        return;
    }

    const bool loaded = view_ ? view_->loadEarthFile(selection.earthFile) : false;
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
    updateStatus(nullptr);
    statusBar()->showMessage(QStringLiteral("已重载最佳本地地图: %1").arg(selection.earthFile), 5000);
}

void Map3DWindow::flyToAircraft()
{
    const bool ok = view_ ? view_->flyToAircraft() : headless_sample_count_ > 0;
    statusBar()->showMessage(ok ? QStringLiteral("已定位到飞机。")
                                : QStringLiteral("暂无飞机位置可定位。"),
                             3000);
}

void Map3DWindow::flyToTrack()
{
    const bool ok = view_ ? view_->flyToTrack() : headless_sample_count_ > 0;
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
    }
    else
    {
        replay_.play();
        rebuildReplayAt(replay_.currentIndex());
        replay_timer_->start(replay_.intervalMs());
    }
    updateReplayUi();
}

void Map3DWindow::stopReplay()
{
    replay_timer_->stop();
    replay_.stop();
    rebuildReplayAt(replay_.currentIndex());
    updateReplayUi();
}

void Map3DWindow::onReplayTick()
{
    if (!replay_.isPlaying() || !replay_.hasSamples())
    {
        replay_timer_->stop();
        replay_.pause();
        updateReplayUi();
        return;
    }

    replay_.stepForward();
    rebuildReplayAt(replay_.currentIndex());
    if (!replay_.isPlaying())
    {
        replay_timer_->stop();
    }
    updateReplayUi();
}

void Map3DWindow::onReplaySliderMoved(int value)
{
    if (!replay_.hasSamples())
    {
        return;
    }
    rebuildReplayAt(value);
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
    QSettings settings(QStringLiteral("VaporView"), QStringLiteral("Map3D"));
    settings.setValue(QStringLiteral("replaySpeed"), replay_.speed());
}

void Map3DWindow::rebuildReplayAt(int index)
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
    }
    updateStatus(visibleSamples.empty() ? nullptr : &visibleSamples.back());
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
        replay_slider_->setRange(0, hasReplay ? replay_.sampleCount() - 1 : 0);
        replay_slider_->setValue(qMax(0, replay_.currentIndex()));
    }
}

void Map3DWindow::setMapSelection(const MapDataSelection& selection)
{
    map_selection_ = selection;
    if (diagnostics_text_)
    {
        diagnostics_text_->setPlainText(diagnosticsText());
    }
}

QString Map3DWindow::diagnosticsText() const
{
    const MapDataDiagnostics& diagnostics = map_selection_.diagnostics;
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
    lines << QStringLiteral("Layer summary:");
    lines << QStringLiteral("  Natural Earth: %1").arg(availabilityLabel(diagnostics.naturalEarthAvailable));
    lines << QStringLiteral("  Selected DEM: %1").arg(selectedDemLabel(diagnostics));
    lines << QStringLiteral("  Copernicus DEM VRT: %1").arg(availabilityLabel(diagnostics.copernicusDemAvailable));
    lines << QStringLiteral("  SRTM VRT: %1").arg(availabilityLabel(diagnostics.srtmDemAvailable));
    lines << QStringLiteral("  OSM vectors: %1 (%2/4 files found)")
                 .arg(diagnostics.osmVectorAvailable ? QStringLiteral("available") : QStringLiteral("missing"))
                 .arg(diagnostics.osmLayerCount);
    lines << QStringLiteral("  Selected OSM: %1").arg(selectedOsmLabel(diagnostics));
    lines << QStringLiteral("Current working directory: %1").arg(diagnostics.currentWorkingDirectory.isEmpty() ? QStringLiteral("<unknown>") : diagnostics.currentWorkingDirectory);
    lines << QStringLiteral("Project root: %1").arg(diagnostics.projectRoot.isEmpty() ? QStringLiteral("<unknown>") : diagnostics.projectRoot);
    lines << QStringLiteral("Maps root: %1").arg(diagnostics.mapsRoot.isEmpty() ? QStringLiteral("<unknown>") : diagnostics.mapsRoot);
    lines << QStringLiteral("Full local earth: %1").arg(diagnostics.fullLocalEarthPath);
    lines << QStringLiteral("Natural Earth texture: %1").arg(diagnostics.naturalEarthTexturePath);
    lines << QStringLiteral("Natural Earth VRT: %1").arg(diagnostics.naturalEarthVrtPath);
    lines << QStringLiteral("Natural Earth raster: %1").arg(diagnostics.naturalEarthRasterPath);
    lines << QStringLiteral("Copernicus DEM VRT: %1").arg(diagnostics.copernicusDemVrtPath);
    lines << QStringLiteral("SRTM VRT: %1").arg(diagnostics.srtmDemVrtPath);
    lines << QStringLiteral("OSM roads: %1").arg(diagnostics.osmRoadsPath);
    lines << QStringLiteral("OSM water: %1").arg(diagnostics.osmWaterPath);
    lines << QStringLiteral("OSM buildings: %1").arg(diagnostics.osmBuildingsPath);
    lines << QStringLiteral("OSM places: %1").arg(diagnostics.osmPlacesPath);
    lines << QStringLiteral("OSG plugin path: %1").arg(diagnostics.osgPluginPath.isEmpty() ? QStringLiteral("<not found>") : diagnostics.osgPluginPath);
    lines << QStringLiteral("OSG_LIBRARY_PATH: %1").arg(diagnostics.osgLibraryPath.isEmpty() ? QStringLiteral("<not set>") : diagnostics.osgLibraryPath);
    lines << QStringLiteral("OSGEARTH_NOTIFY_LEVEL: %1").arg(diagnostics.osgEarthNotifyLevel.isEmpty() ? QStringLiteral("<not set>") : diagnostics.osgEarthNotifyLevel);
    lines << QStringLiteral("GDAL_DATA: %1").arg(diagnostics.gdalDataPath.isEmpty() ? QStringLiteral("<not found>") : diagnostics.gdalDataPath);
    lines << QStringLiteral("PROJ_DATA: %1").arg(diagnostics.projDataPath.isEmpty() ? QStringLiteral("<not found>") : diagnostics.projDataPath);

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

void Map3DWindow::updateStatus(const VaporView::Geo::NavSample* latest)
{
    const Map3DPerformanceStats stats = view_ ? view_->performanceStats() : Map3DPerformanceStats{};
    const int totalSamples = view_ ? stats.totalSamples : headless_sample_count_;
    const int visibleSamples = view_ ? stats.visibleSamples : headless_sample_count_;
    QString text = QStringLiteral("Points: %1/%2").arg(visibleSamples).arg(totalSamples);
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
        text += QStringLiteral(" | FPS %1 | Frame %2 ms | Track %3 ms")
                    .arg(stats.framesPerSecond, 0, 'f', 1)
                    .arg(stats.frameMs, 0, 'f', 1)
                    .arg(stats.trackUpdateMs, 0, 'f', 1);
    }
    if (latest && latest->hasLlh())
    {
        text += QStringLiteral(" | Lat %1 Lon %2 H %3 m %4 | Fix %5")
                    .arg(latest->latDeg, 0, 'f', 7)
                    .arg(latest->lonDeg, 0, 'f', 7)
                    .arg(latest->heightM, 0, 'f', 2)
                    .arg(heightReferenceLabel(latest->heightReference))
                    .arg(static_cast<int>(latest->fixQuality));
    }
    if (replay_.hasSamples())
    {
        text += QStringLiteral(" | Replay %1/%2 %3x%4")
                    .arg(qMax(0, replay_.currentIndex() + 1))
                    .arg(replay_.sampleCount())
                    .arg(replay_.speed(), 0, 'g', 3)
                    .arg(replay_.isPlaying() ? QStringLiteral(" playing") : QStringLiteral(""));
    }
    status_label_->setText(text);
}

} // namespace VaporView::Map3D
