#include "map3d/Map3DWindow.h"

#include "geo/SessionTrackReader.h"
#include "map3d/OsgEarthViewWidget.h"

#include <QAction>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QSettings>
#include <QStatusBar>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWidget>

namespace VaporView::Map3D {
namespace {

QString heightReferenceLabel(VaporView::Geo::HeightReference reference)
{
    switch (reference)
    {
    case VaporView::Geo::HeightReference::Ellipsoid:
        return QStringLiteral("ellipsoid");
    case VaporView::Geo::HeightReference::MeanSeaLevel:
        return QStringLiteral("MSL");
    case VaporView::Geo::HeightReference::Local:
        return QStringLiteral("local");
    case VaporView::Geo::HeightReference::Dem:
        return QStringLiteral("DEM");
    case VaporView::Geo::HeightReference::Unknown:
        return QStringLiteral("unknown");
    }
    return QStringLiteral("unknown");
}

bool isMap3DHeadlessTest()
{
    return qEnvironmentVariableIsSet("VAPORVIEW_MAP3D_HEADLESS_TEST");
}

} // namespace

Map3DWindow::Map3DWindow(QWidget* parent)
    : QMainWindow(parent)
    , status_label_(new QLabel(this))
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

    follow_action_ = toolbar->addAction(QStringLiteral("跟随飞机"));
    follow_action_->setCheckable(true);
    QSettings settings(QStringLiteral("VaporView"), QStringLiteral("Map3D"));
    follow_action_->setChecked(settings.value(QStringLiteral("followAircraft"), false).toBool());
    if (view_)
    {
        view_->setFollowAircraft(follow_action_->isChecked());
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

    diagnostics_action_ = toolbar->addAction(QStringLiteral("地图诊断"));
    connect(diagnostics_action_, &QAction::triggered, this, &Map3DWindow::showMapDiagnostics);

    statusBar()->addPermanentWidget(status_label_, 1);
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
    if (view_)
    {
        view_->appendSample(sample);
    }
    if (headless_view_)
    {
        ++headless_sample_count_;
    }
    updateStatus(&sample);
}

void Map3DWindow::appendSamples(const std::vector<VaporView::Geo::NavSample>& samples)
{
    if (view_)
    {
        view_->appendSamples(samples);
    }
    if (headless_view_)
    {
        headless_sample_count_ += static_cast<int>(samples.size());
    }
    updateStatus(samples.empty() ? nullptr : &samples.back());
}

void Map3DWindow::clearTrack()
{
    if (view_)
    {
        view_->clearTrack();
    }
    if (headless_view_)
    {
        headless_sample_count_ = 0;
    }
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

    clearTrack();
    appendSamples(result.samples);
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
    lines << QStringLiteral("Current working directory: %1").arg(diagnostics.currentWorkingDirectory.isEmpty() ? QStringLiteral("<unknown>") : diagnostics.currentWorkingDirectory);
    lines << QStringLiteral("Project root: %1").arg(diagnostics.projectRoot.isEmpty() ? QStringLiteral("<unknown>") : diagnostics.projectRoot);
    lines << QStringLiteral("Maps root: %1").arg(diagnostics.mapsRoot.isEmpty() ? QStringLiteral("<unknown>") : diagnostics.mapsRoot);
    lines << QStringLiteral("Full local earth: %1").arg(diagnostics.fullLocalEarthPath);
    lines << QStringLiteral("Natural Earth texture: %1").arg(diagnostics.naturalEarthTexturePath);
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
    const int sampleCount = view_ ? view_->sampleCount() : headless_sample_count_;
    QString text = QStringLiteral("Points: %1").arg(sampleCount);
    if (view_ || headless_view_)
    {
        const QSize framebufferSize = view_ ? view_->framebufferSize() : headless_view_->size();
        text += QStringLiteral(" | Map %1 | View %2x%3")
                    .arg(MapDataManager::modeLabel(map_selection_.mode))
                    .arg(framebufferSize.width())
                    .arg(framebufferSize.height());
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
    status_label_->setText(text);
}

} // namespace VaporView::Map3D
