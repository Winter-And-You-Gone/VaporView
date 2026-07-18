
#include "map3d/OsgEarthViewWidget.h"

#include "map3d/Aircraft3DLayer.h"
#include "map3d/AircraftHeading.h"
#include "Map3DAssetLoader.h"
#include "Map3DRuntime.h"
#include "map3d/TrackSampling.h"
#include "map3d/Trajectory3DLayer.h"

#include <osg/Camera>
#include <osg/BoundingSphere>
#include <osg/Geometry>
#include <osg/Geode>
#include <osg/Group>
#include <osg/Texture2D>
#include <osg/Viewport>
#include <osgGA/EventQueue>
#include <osgGA/TrackballManipulator>
#include <osgViewer/GraphicsWindow>
#include <osgViewer/Viewer>
#include <osgEarth/EarthManipulator>
#include <osgEarth/ElevationLayer>
#include <osgEarth/GeoData>
#include <osgEarth/GLUtils>
#include <osgEarth/ImageLayer>
#include <osgEarth/Layer>
#include <osgEarth/MapNode>
#include <osgEarth/Map>
#include <osgEarth/Profile>
#include <osgEarth/Registry>
#include <osgEarth/SpatialReference>
#include <osgEarth/TerrainEngineNode>
#include <osgEarth/TerrainOptions>
#include <osgEarth/VisibleLayer>
#include <osgEarth/Viewpoint>
#include <osgEarth/XYZ>

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QHideEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QOpenGLContext>
#include <QShowEvent>
#include <QThread>
#include <QWheelEvent>
#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QUrl>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <mutex>
#include <optional>

namespace VaporView::Map3D {
namespace {

constexpr double kEarthRadiusM = 6378137.0;
constexpr unsigned kTiandituMaxZoom = 18;
constexpr const char* kTiandituSatelliteLayerName = "Tianditu Satellite imagery";
constexpr const char* kTiandituBrowserUserAgent =
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
    "(KHTML, like Gecko) Chrome/135.0.0.0 Safari/537.36";
constexpr float kTerrainTilePixelSize = 128.0f;
constexpr double kEarthMaxInteractivePitchDeg = -4.0;
constexpr double kEarthProjectionNearPlaneM = 1.0;
constexpr double kEarthProjectionDefaultFarPlaneM = 10000000.0;
constexpr double kEarthProjectionLocalRangeLimitM = 1000000.0;
constexpr double kEarthProjectionLocalFarMultiplier = 18.0;
constexpr double kEarthProjectionLowAngleFarMultiplier = 6.0;
constexpr double kEarthProjectionLocalMinFarPlaneM = 20000.0;
constexpr double kEarthProjectionLowAngleMinFarPlaneM = 8000.0;
constexpr double kEarthProjectionLocalMaxFarPlaneM = 850000.0;
constexpr double kEarthProjectionLowAngleMaxFarPlaneM = 120000.0;
constexpr float kEarthSmallFeatureCullPixels = 3.0f;
constexpr float kEarthLowAngleSmallFeatureCullPixels = 9.0f;
constexpr float kEarthCullLODScale = 1.0f;
constexpr float kEarthLowAngleCullLODScale = 2.5f;
constexpr double kEarthLowAngleStartPitchDeg = -35.0;

struct EarthProjectionProfile {
    double farPlaneM = kEarthProjectionDefaultFarPlaneM;
    float smallFeatureCullPixels = kEarthSmallFeatureCullPixels;
    float lodScale = kEarthCullLODScale;
};

double blend(double lowAngle0, double lowAngle1, double factor)
{
    return lowAngle0 + (lowAngle1 - lowAngle0) * factor;
}

double lowAngleFactor(double pitchDeg)
{
    if (!std::isfinite(pitchDeg))
    {
        return 0.0;
    }
    return std::clamp((pitchDeg - kEarthLowAngleStartPitchDeg)
                          / (kEarthMaxInteractivePitchDeg - kEarthLowAngleStartPitchDeg),
                      0.0,
                      1.0);
}

EarthProjectionProfile earthProjectionProfile(double cameraRangeM, double pitchDeg)
{
    if (!std::isfinite(cameraRangeM)
        || cameraRangeM <= 0.0
        || cameraRangeM > kEarthProjectionLocalRangeLimitM)
    {
        return {};
    }

    const double lowAngle = lowAngleFactor(pitchDeg);
    const double farMultiplier =
        blend(kEarthProjectionLocalFarMultiplier,
              kEarthProjectionLowAngleFarMultiplier,
              lowAngle);
    const double minFarPlaneM =
        blend(kEarthProjectionLocalMinFarPlaneM,
              kEarthProjectionLowAngleMinFarPlaneM,
              lowAngle);
    const double maxFarPlaneM =
        blend(kEarthProjectionLocalMaxFarPlaneM,
              kEarthProjectionLowAngleMaxFarPlaneM,
              lowAngle);

    EarthProjectionProfile profile;
    profile.farPlaneM = std::clamp(cameraRangeM * farMultiplier, minFarPlaneM, maxFarPlaneM);
    profile.smallFeatureCullPixels =
        static_cast<float>(blend(static_cast<double>(kEarthSmallFeatureCullPixels),
                                 static_cast<double>(kEarthLowAngleSmallFeatureCullPixels),
                                 lowAngle));
    profile.lodScale =
        static_cast<float>(blend(static_cast<double>(kEarthCullLODScale),
                                 static_cast<double>(kEarthLowAngleCullLODScale),
                                 lowAngle));
    return profile;
}

void configureEarthManipulator(osgEarth::EarthManipulator* manipulator)
{
    if (!manipulator)
    {
        return;
    }
    osgEarth::EarthManipulator::Settings* settings = manipulator->getSettings();
    if (!settings)
    {
        return;
    }

    settings->setMinMaxPitch(-90.0, kEarthMaxInteractivePitchDeg);
    settings->setMinMaxDistance(50.0, 20000000.0);
    settings->setTerrainAvoidanceEnabled(true);
    settings->setTerrainAvoidanceMinimumDistance(25.0);
}

double currentEarthManipulatorDistance(const osgViewer::Viewer* viewer)
{
    if (!viewer)
    {
        return (std::numeric_limits<double>::quiet_NaN)();
    }
    const auto* manipulator =
        dynamic_cast<const osgEarth::EarthManipulator*>(viewer->getCameraManipulator());
    if (!manipulator)
    {
        return (std::numeric_limits<double>::quiet_NaN)();
    }
    const double distanceM = manipulator->getDistance();
    return std::isfinite(distanceM) ? distanceM : (std::numeric_limits<double>::quiet_NaN)();
}

double currentEarthManipulatorPitchDeg(const osgViewer::Viewer* viewer)
{
    if (!viewer)
    {
        return (std::numeric_limits<double>::quiet_NaN)();
    }
    const auto* manipulator =
        dynamic_cast<const osgEarth::EarthManipulator*>(viewer->getCameraManipulator());
    if (!manipulator)
    {
        return (std::numeric_limits<double>::quiet_NaN)();
    }
    const osgEarth::Viewpoint viewpoint = manipulator->getViewpoint();
    if (!viewpoint.isValid() || !viewpoint.pitch().isSet())
    {
        return (std::numeric_limits<double>::quiet_NaN)();
    }
    const double pitchDeg = viewpoint.pitch().get().as(osgEarth::Units::DEGREES);
    return std::isfinite(pitchDeg) ? pitchDeg : (std::numeric_limits<double>::quiet_NaN)();
}

void configureHighResolutionTerrain(osgEarth::MapNode* mapNode)
{
    if (!mapNode) return;

    osgEarth::TerrainOptionsAPI terrainOptions = mapNode->getTerrainOptions();
    terrainOptions.setLODMethod(osgEarth::LODMethod::SCREEN_SPACE);
    terrainOptions.setTilePixelSize(kTerrainTilePixelSize);
    terrainOptions.setScreenSpaceError(0.0f);
    terrainOptions.setProgressive(false);
    terrainOptions.setMorphImagery(false);
    mapNode->setScreenSpaceError(0.0f);

    if (osgEarth::TerrainEngine* terrainEngine = mapNode->getTerrainEngine())
    {
        terrainEngine->dirtyTerrainOptions();
    }
}

void assertGuiThread(const QObject* object, const char* function)
{
    Q_ASSERT_X(object && QThread::currentThread() == object->thread(),
               "OsgEarthViewWidget",
               function);
}

unsigned int toOsgMouseButton(Qt::MouseButton button)
{
    switch (button)
    {
    case Qt::LeftButton:
        return 1;
    case Qt::MiddleButton:
        return 2;
    case Qt::RightButton:
        return 3;
    default:
        return 0;
    }
}

int toOsgKey(const QKeyEvent* event)
{
    if (!event)
    {
        return 0;
    }

    switch (event->key())
    {
    case Qt::Key_Left:
        return osgGA::GUIEventAdapter::KEY_Left;
    case Qt::Key_Right:
        return osgGA::GUIEventAdapter::KEY_Right;
    case Qt::Key_Up:
        return osgGA::GUIEventAdapter::KEY_Up;
    case Qt::Key_Down:
        return osgGA::GUIEventAdapter::KEY_Down;
    case Qt::Key_PageUp:
        return osgGA::GUIEventAdapter::KEY_Page_Up;
    case Qt::Key_PageDown:
        return osgGA::GUIEventAdapter::KEY_Page_Down;
    case Qt::Key_Home:
        return osgGA::GUIEventAdapter::KEY_Home;
    case Qt::Key_End:
        return osgGA::GUIEventAdapter::KEY_End;
    case Qt::Key_Escape:
        return osgGA::GUIEventAdapter::KEY_Escape;
    case Qt::Key_Space:
        return ' ';
    default:
        break;
    }

    const QString text = event->text();
    if (text.size() == 1)
    {
        const char16_t character = text.at(0).toLower().unicode();
        if (QChar(character).isPrint())
        {
            return character;
        }
    }
    return 0;
}
osg::Node* createLocalGridNode()
{
    osg::ref_ptr<osg::Geode> geode = new osg::Geode;
    osg::ref_ptr<osg::Geometry> geometry = new osg::Geometry;
    osg::ref_ptr<osg::Vec3Array> vertices = new osg::Vec3Array;
    osg::ref_ptr<osg::Vec4Array> colors = new osg::Vec4Array;

    constexpr int kExtentM = 1000;
    constexpr int kSpacingM = 50;
    for (int value = -kExtentM; value <= kExtentM; value += kSpacingM)
    {
        vertices->push_back(osg::Vec3(value, -kExtentM, 0.0f));
        vertices->push_back(osg::Vec3(value, kExtentM, 0.0f));
        vertices->push_back(osg::Vec3(-kExtentM, value, 0.0f));
        vertices->push_back(osg::Vec3(kExtentM, value, 0.0f));
        const osg::Vec4 color = value == 0
            ? osg::Vec4(0.62f, 0.72f, 0.82f, 1.0f)
            : osg::Vec4(0.28f, 0.34f, 0.40f, 1.0f);
        colors->push_back(color);
        colors->push_back(color);
        colors->push_back(color);
        colors->push_back(color);
    }

    geometry->setVertexArray(vertices.get());
    geometry->setColorArray(colors.get(), osg::Array::BIND_PER_VERTEX);
    geometry->addPrimitiveSet(new osg::DrawArrays(osg::PrimitiveSet::LINES, 0, static_cast<GLsizei>(vertices->size())));
    geode->addDrawable(geometry.get());
    return geode.release();
}

osg::Vec3d samplePosition(const VaporView::Geo::NavSample& sample)
{
    if (sample.hasEcef())
    {
        return osg::Vec3d(sample.ecefXM, sample.ecefYM, sample.ecefZM);
    }
    if (sample.hasNed())
    {
        return osg::Vec3d(sample.nedEM, sample.nedNM, -sample.nedDM);
    }
    return osg::Vec3d(sample.lonDeg * 100000.0, sample.latDeg * 100000.0, sample.heightM);
}

bool sampleWorldFocusPosition(const VaporView::Geo::NavSample& sample, osg::Vec3d& world)
{
    if (sample.hasEcef())
    {
        world.set(sample.ecefXM, sample.ecefYM, sample.ecefZM);
        return true;
    }
    if (!sample.hasLlh())
    {
        return false;
    }

    const osgEarth::SpatialReference* wgs84 = osgEarth::SpatialReference::get("wgs84");
    if (!wgs84)
    {
        return false;
    }
    osgEarth::GeoPoint geo(wgs84, sample.lonDeg, sample.latDeg, sample.heightM, osgEarth::ALTMODE_ABSOLUTE);
    return geo.toWorld(world)
        && std::isfinite(world.x())
        && std::isfinite(world.y())
        && std::isfinite(world.z());
}

std::optional<osgEarth::Viewpoint> currentEarthViewpoint(osgViewer::Viewer* viewer)
{
    if (!viewer)
    {
        return std::nullopt;
    }

    const auto* manipulator =
        dynamic_cast<const osgEarth::EarthManipulator*>(viewer->getCameraManipulator());
    if (!manipulator)
    {
        return std::nullopt;
    }

    osgEarth::Viewpoint viewpoint = manipulator->getViewpoint();
    if (!viewpoint.isValid() || viewpoint.nodeIsSet())
    {
        return std::nullopt;
    }
    return viewpoint;
}

bool restoreEarthViewpoint(osgViewer::Viewer* viewer, const osgEarth::Viewpoint& viewpoint)
{
    if (!viewer || !viewer->getCamera() || !viewpoint.isValid())
    {
        return false;
    }

    auto* manipulator =
        dynamic_cast<osgEarth::EarthManipulator*>(viewer->getCameraManipulator());
    if (!manipulator)
    {
        return false;
    }
    manipulator->setViewpoint(viewpoint, 0.0);
    manipulator->updateCamera(*viewer->getCamera());
    return true;
}

QString tiandituSatelliteUrlTemplate(const QString& key)
{
    const QString encodedKey = QString::fromLatin1(QUrl::toPercentEncoding(key.trimmed()));
    return QStringLiteral(
        "https://t[01234567].tianditu.gov.cn/img_w/wmts"
        "?SERVICE=WMTS&REQUEST=GetTile&VERSION=1.0.0"
        "&LAYER=img&STYLE=default&TILEMATRIXSET=w&FORMAT=tiles"
        "&TILEMATRIX={z}&TILEROW={y}&TILECOL={x}&tk=%1")
        .arg(encodedKey);
}

void removeLayerByName(osgEarth::Map* map, const char* layerName)
{
    if (!map || !layerName)
    {
        return;
    }

    while (true)
    {
        osg::ref_ptr<osgEarth::Layer> existing = map->getLayerByName(layerName);
        if (!existing)
        {
            return;
        }
        map->removeLayer(existing.get());
    }
}

unsigned tiandituSatelliteInsertIndex(osgEarth::Map* map)
{
    if (!map)
    {
        return 0;
    }

    osgEarth::LayerVector layers;
    map->getLayers(layers);
    for (unsigned index = 0; index < layers.size(); ++index)
    {
        if (dynamic_cast<osgEarth::ElevationLayer*>(layers[index].get()))
        {
            return index;
        }
    }
    return static_cast<unsigned>(layers.size());
}

std::size_t layerIndex(Map3DLayer layer)
{
    return static_cast<std::size_t>(layer);
}

Map3DLayer mapLayerCategory(const osgEarth::Layer* layer)
{
    if (!layer)
    {
        return Map3DLayer::Count;
    }

    const QString name = QString::fromStdString(layer->getName()).toLower();
    if (dynamic_cast<const osgEarth::ElevationLayer*>(layer))
    {
        return Map3DLayer::DigitalElevation;
    }
    if (name.contains(QStringLiteral("water"))
        || name.contains(QStringLiteral("hydro")))
    {
        return Map3DLayer::Hydrography;
    }
    if (name.contains(QStringLiteral("road"))
        || name.contains(QStringLiteral("transport")))
    {
        return Map3DLayer::RoadNetwork;
    }
    if (dynamic_cast<const osgEarth::ImageLayer*>(layer))
    {
        if (name.contains(QStringLiteral("satellite"))
            || name.contains(QStringLiteral("sentinel"))
            || name.contains(QStringLiteral("landsat"))
            || name.contains(QStringLiteral("aerial")))
        {
            return Map3DLayer::SatelliteImagery;
        }
        return Map3DLayer::BaseMap;
    }
    return Map3DLayer::Count;
}

} // namespace

OsgEarthViewWidget::OsgEarthViewWidget(QWidget* parent)
    : QOpenGLWidget(parent)
    , trajectory_layer_(std::make_unique<Trajectory3DLayer>())
    , aircraft_layer_(std::make_unique<Aircraft3DLayer>())
{
    layer_visibility_.fill(true);
    initializeMap3DRuntime();
    setMinimumSize(640, 420);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    frameTimer_.setInterval(33);
    connect(&frameTimer_, &QTimer::timeout, this, [this]() {
        if (!shutdown_)
        {
            update();
        }
    });
}

OsgEarthViewWidget::~OsgEarthViewWidget()
{
    shutdown();
}

void OsgEarthViewWidget::shutdown()
{
    assertGuiThread(this, Q_FUNC_INFO);
    frameTimer_.stop();
    if (shutdown_)
    {
        return;
    }
    shutdown_ = true;
    ++earth_load_generation_;
    ++local_3d_tiles_load_generation_;
    ++aircraft_load_generation_;
    cancelAsyncWatchers();
    QObject::disconnect(gl_context_destruction_connection_);
    setUpdatesEnabled(false);
    clearFocus();

    bool madeCurrent = false;
    QOpenGLContext* openGlContext = context();
    if (initialized_ && openGlContext && openGlContext->isValid())
    {
        makeCurrent();
        madeCurrent = QOpenGLContext::currentContext() == openGlContext;
    }
    if (viewer_)
    {
        viewer_->setDone(true);
        viewer_->stopThreading();
        viewer_->setCameraManipulator(nullptr);
        viewer_->setDatabasePager(nullptr);
        viewer_->setImagePager(nullptr);
        if (viewer_->getCamera())
        {
            viewer_->getCamera()->setGraphicsContext(nullptr);
        }
        viewer_->setSceneData(nullptr);
    }
    if (root_)
    {
        if (madeCurrent)
        {
            root_->releaseGLObjects(nullptr);
        }
        root_->removeChildren(0, root_->getNumChildren());
    }
    earth_node_ = nullptr;
    local_3d_tiles_node_ = nullptr;
    overlay_transform_ = nullptr;

    map_node_ = nullptr;
    trajectory_layer_.reset();
    aircraft_layer_.reset();
    root_ = nullptr;
    graphics_window_ = nullptr;
    viewer_.reset();
    initialized_ = false;
    if (madeCurrent)
    {
        doneCurrent();
    }
}

void OsgEarthViewWidget::registerAsyncWatcher(QFutureWatcherBase* watcher)
{
    if (watcher)
    {
        async_watchers_.insert(watcher);
    }
}

void OsgEarthViewWidget::unregisterAsyncWatcher(QFutureWatcherBase* watcher)
{
    async_watchers_.remove(watcher);
}

void OsgEarthViewWidget::cancelAsyncWatchers()
{
    const QSet<QFutureWatcherBase*> watchers = async_watchers_;
    async_watchers_.clear();
    for (QFutureWatcherBase* watcher : watchers)
    {
        if (!watcher)
        {
            continue;
        }
        QObject::disconnect(watcher, nullptr, this, nullptr);
        watcher->cancel();
        watcher->waitForFinished();
        delete watcher;
    }
}

void OsgEarthViewWidget::appendSample(const VaporView::Geo::NavSample& sample)
{
    assertGuiThread(this, Q_FUNC_INFO);
    if (shutdown_ || !trajectory_layer_ || !aircraft_layer_)
    {
        return;
    }
    QElapsedTimer timer;
    timer.start();
    detachSharedSamplesForLiveAppend();
    raw_samples_.push_back(sample);
    while (!preserve_full_track_extent_
           && static_cast<int>(raw_samples_.size()) > maxVisibleSamples())
    {
        raw_samples_.pop_front();
    }
    const VaporView::Geo::NavSample displaySample = toDisplaySample(sample);
    trajectory_layer_->appendSample(displaySample);
    aircraft_layer_->updateSample(displaySample);
    updateFollowCamera(displaySample);
    last_track_update_ms_ = static_cast<double>(timer.nsecsElapsed()) / 1000000.0;
    update();
}

void OsgEarthViewWidget::appendSamples(const std::vector<VaporView::Geo::NavSample>& samples)
{
    assertGuiThread(this, Q_FUNC_INFO);
    if (shutdown_ || !trajectory_layer_ || !aircraft_layer_)
    {
        return;
    }
    QElapsedTimer timer;
    timer.start();
    if (!samples.empty())
    {
        detachSharedSamplesForLiveAppend();
    }
    raw_samples_.insert(raw_samples_.end(), samples.cbegin(), samples.cend());
    while (!preserve_full_track_extent_
           && static_cast<int>(raw_samples_.size()) > maxVisibleSamples())
    {
        raw_samples_.pop_front();
    }
    std::vector<VaporView::Geo::NavSample> displaySamples;
    displaySamples.reserve(samples.size());
    for (const VaporView::Geo::NavSample& sample : samples)
    {
        displaySamples.push_back(toDisplaySample(sample));
    }
    trajectory_layer_->appendSamples(displaySamples);
    if (!displaySamples.empty())
    {
        aircraft_layer_->updateSample(displaySamples.back());
        updateFollowCamera(displaySamples.back());
    }
    last_track_update_ms_ = static_cast<double>(timer.nsecsElapsed()) / 1000000.0;
    update();
}

void OsgEarthViewWidget::setSamples(const std::vector<VaporView::Geo::NavSample>& samples)
{
    setSamples(std::make_shared<const std::vector<VaporView::Geo::NavSample>>(samples));
}

void OsgEarthViewWidget::setSamples(
    std::shared_ptr<const std::vector<VaporView::Geo::NavSample>> samples,
    int sampleCount)
{
    assertGuiThread(this, Q_FUNC_INFO);
    if (shutdown_ || !trajectory_layer_ || !aircraft_layer_)
    {
        return;
    }
    QElapsedTimer timer;
    timer.start();
    raw_samples_.clear();
    shared_samples_ = std::move(samples);
    const int available = shared_samples_ ? static_cast<int>(shared_samples_->size()) : 0;
    shared_sample_count_ = sampleCount < 0 ? available : std::clamp(sampleCount, 0, available);
    preserve_full_track_extent_ = true;
    local_frame_ = VaporView::Geo::LocalTangentPlane();
    resetWorldOverlayOrigin();
    rebuildDisplayTrack();
    last_track_update_ms_ = static_cast<double>(timer.nsecsElapsed()) / 1000000.0;
    update();
}

void OsgEarthViewWidget::appendSampleFromStorage(
    const std::shared_ptr<const std::vector<VaporView::Geo::NavSample>>& samples,
    int sampleIndex)
{
    assertGuiThread(this, Q_FUNC_INFO);
    if (shutdown_ || !samples || sampleIndex < 0
        || sampleIndex >= static_cast<int>(samples->size()))
    {
        return;
    }
    if (shared_samples_ != samples || sampleIndex != shared_sample_count_)
    {
        setSamples(samples, sampleIndex + 1);
        return;
    }

    QElapsedTimer timer;
    timer.start();
    ++shared_sample_count_;
    const VaporView::Geo::NavSample displaySample = toDisplaySample((*samples)[static_cast<std::size_t>(sampleIndex)]);
    trajectory_layer_->appendSample(displaySample);
    aircraft_layer_->updateSample(displaySample);
    updateFollowCamera(displaySample);
    last_track_update_ms_ = static_cast<double>(timer.nsecsElapsed()) / 1000000.0;
    update();
}

void OsgEarthViewWidget::clearTrack()
{
    assertGuiThread(this, Q_FUNC_INFO);
    raw_samples_.clear();
    shared_samples_.reset();
    shared_sample_count_ = 0;
    preserve_full_track_extent_ = false;
    height_reference_status_.clear();
    if (shutdown_ || !trajectory_layer_ || !aircraft_layer_)
    {
        return;
    }
    trajectory_layer_->clear();
    aircraft_layer_->clear();
    local_frame_ = VaporView::Geo::LocalTangentPlane();
    resetWorldOverlayOrigin();
    update();
}

bool OsgEarthViewWidget::loadEarthFile(const QString& earthPath)
{
    assertGuiThread(this, Q_FUNC_INFO);
    ++earth_load_generation_;
    const Detail::EarthAssetLoadResult result = Detail::loadEarthAsset(earthPath);
    return applyEarthLoad(result.diagnostics, result.node, result.mapNode, result.useXihuInitialView, false);
}

void OsgEarthViewWidget::loadEarthFileAsync(const QString& earthPath,
                                            std::function<void(bool)> finished)
{
    loadEarthFileAsync(earthPath, false, std::move(finished));
}

void OsgEarthViewWidget::loadEarthFilePreservingViewAsync(const QString& earthPath,
                                                          std::function<void(bool)> finished)
{
    loadEarthFileAsync(earthPath, true, std::move(finished));
}

bool OsgEarthViewWidget::applyTiandituSatelliteImagery(const QString& key)
{
    assertGuiThread(this, Q_FUNC_INFO);
    if (shutdown_ || !map_node_ || !map_node_->getMap())
    {
        return false;
    }

    osgEarth::Map* map = map_node_->getMap();
    removeLayerByName(map, kTiandituSatelliteLayerName);

    const QString trimmedKey = key.trimmed();
    if (trimmedKey.isEmpty())
    {
        earth_load_diagnostics_.layerSummaries.push_back(
            QStringLiteral("Tianditu Satellite imagery not applied: no key configured."));
        update();
        return false;
    }

    osg::ref_ptr<osgEarth::XYZImageLayer> layer = new osgEarth::XYZImageLayer;
    layer->setName(kTiandituSatelliteLayerName);
    osgEarth::URIContext tiandituContext;
    tiandituContext.addHeader("User-Agent", kTiandituBrowserUserAgent);
    tiandituContext.addHeader("Referer", "https://map.tianditu.gov.cn/");
    layer->setURL(osgEarth::URI(tiandituSatelliteUrlTemplate(trimmedKey).toStdString(),
                                tiandituContext));
    layer->setProfile(osgEarth::Profile::create(osgEarth::Profile::SPHERICAL_MERCATOR));
    layer->setFormat("jpg");
    layer->options().minLevel() = 0u;
    layer->options().maxLevel() = kTiandituMaxZoom;

    const unsigned insertIndex = tiandituSatelliteInsertIndex(map);
    map->insertLayer(layer.get(), insertIndex);
    earth_load_diagnostics_.layerSummaries.push_back(
        QStringLiteral("Tianditu Satellite imagery added at layer index %1%2.")
            .arg(insertIndex)
            .arg(layer->isOpen()
                     ? QString()
                     : QStringLiteral("; pending open status=%1")
                           .arg(QString::fromStdString(layer->getStatus().toString()))));
    ++earth_load_diagnostics_.layerCount;
    if (layer->isOpen())
    {
        ++earth_load_diagnostics_.openLayerCount;
    }
    applyLayerVisibility(Map3DLayer::SatelliteImagery);
    update();
    return true;
}

void OsgEarthViewWidget::loadEarthFileAsync(const QString& earthPath,
                                            bool preserveCurrentEarthView,
                                            std::function<void(bool)> finished)
{
    assertGuiThread(this, Q_FUNC_INFO);
    if (shutdown_)
    {
        if (finished) finished(false);
        return;
    }
    const quint64 generation = ++earth_load_generation_;
    earth_load_diagnostics_ = {};
    earth_load_diagnostics_.attempted = true;
    earth_load_diagnostics_.requestedPath = earthPath;
    auto* watcher = new QFutureWatcher<Detail::EarthAssetLoadResult>(this);
    registerAsyncWatcher(watcher);
    connect(watcher, &QFutureWatcher<Detail::EarthAssetLoadResult>::finished,
            this, [this, watcher, generation, preserveCurrentEarthView, finished = std::move(finished)]() mutable {
        const Detail::EarthAssetLoadResult result = watcher->result();
        unregisterAsyncWatcher(watcher);
        watcher->deleteLater();
        if (shutdown_ || generation != earth_load_generation_) return;
        const bool loaded = applyEarthLoad(result.diagnostics,
                                           result.node,
                                           result.mapNode,
                                           result.useXihuInitialView,
                                           preserveCurrentEarthView);
        if (finished) finished(loaded);
    });
    watcher->setFuture(QtConcurrent::run([earthPath]() {
        return Detail::loadEarthAsset(earthPath);
    }));
}

bool OsgEarthViewWidget::applyEarthLoad(EarthLoadDiagnostics diagnostics,
                                        osg::ref_ptr<osg::Node> node,
                                        osgEarth::MapNode* mapNode,
                                        bool useXihuInitialView,
                                        bool preserveCurrentEarthView)
{
    assertGuiThread(this, Q_FUNC_INFO);
    earth_load_diagnostics_ = std::move(diagnostics);
    if (shutdown_ || !earth_load_diagnostics_.loaded || !node) return false;
    initializeSceneIfNeeded();
    if (!root_)
    {
        earth_load_diagnostics_.loaded = false;
        earth_load_diagnostics_.failureReason = QStringLiteral("Scene root is not initialized.");
        return false;
    }
    const std::optional<osgEarth::Viewpoint> previousViewpoint =
        preserveCurrentEarthView ? currentEarthViewpoint(viewer_.get()) : std::nullopt;
    const bool replacedPreviousNode = earth_node_.valid();
    if (earth_node_) root_->removeChild(earth_node_.get());
    earth_node_ = std::move(node);
    map_node_ = mapNode;
    configureHighResolutionTerrain(map_node_);
    earth_load_diagnostics_.layerSummaries.push_back(
        QStringLiteral("Terrain detail: visible-first screen-space LOD, 128 px tile threshold, low-angle adaptive clipping and LOD."));
    use_xihu_initial_view_ = useXihuInitialView;
    if (replacedPreviousNode)
    {
        earth_load_diagnostics_.layerSummaries.push_back(QStringLiteral("Replaced previous Earth scene."));
    }
    trajectory_layer_->setUseWorldCoordinates(true);
    aircraft_layer_->setUseWorldCoordinates(true);
    resetWorldOverlayOrigin();
    root_->insertChild(0, earth_node_.get());
    applyAllLayerVisibility();
    setInitialEarthView();
    if (previousViewpoint && restoreEarthViewpoint(viewer_.get(), *previousViewpoint))
    {
        earth_load_diagnostics_.layerSummaries.push_back(
            QStringLiteral("Preserved previous Earth camera viewpoint."));
    }
    rebuildDisplayTrack();
    update();
    return true;
}

bool OsgEarthViewWidget::loadLocal3DTilesPreview(const QString& tilesetPath)
{
    assertGuiThread(this, Q_FUNC_INFO);
    ++local_3d_tiles_load_generation_;
    const Detail::Local3DTilesAssetLoadResult result = Detail::loadLocal3DTilesAsset(tilesetPath);
    return applyLocal3DTilesLoad(result.diagnostics, result.node);
}

void OsgEarthViewWidget::loadLocal3DTilesPreviewAsync(
    const QString& tilesetPath,
    std::function<void(bool)> finished)
{
    assertGuiThread(this, Q_FUNC_INFO);
    if (shutdown_)
    {
        if (finished) finished(false);
        return;
    }
    const quint64 generation = ++local_3d_tiles_load_generation_;
    local_3d_tiles_load_diagnostics_ = {};
    local_3d_tiles_load_diagnostics_.attempted = true;
    local_3d_tiles_load_diagnostics_.requestedPath = tilesetPath;
    auto* watcher = new QFutureWatcher<Detail::Local3DTilesAssetLoadResult>(this);
    registerAsyncWatcher(watcher);
    connect(watcher, &QFutureWatcher<Detail::Local3DTilesAssetLoadResult>::finished,
            this, [this, watcher, generation, finished = std::move(finished)]() mutable {
        const Detail::Local3DTilesAssetLoadResult result = watcher->result();
        unregisterAsyncWatcher(watcher);
        watcher->deleteLater();
        if (shutdown_ || generation != local_3d_tiles_load_generation_) return;
        const bool loaded = applyLocal3DTilesLoad(result.diagnostics, result.node);
        if (finished) finished(loaded);
    });
    watcher->setFuture(QtConcurrent::run([tilesetPath]() {
        return Detail::loadLocal3DTilesAsset(tilesetPath);
    }));
}

bool OsgEarthViewWidget::applyLocal3DTilesLoad(Local3DTilesLoadDiagnostics diagnostics,
                                               osg::ref_ptr<osg::Group> node)
{
    assertGuiThread(this, Q_FUNC_INFO);
    local_3d_tiles_load_diagnostics_ = std::move(diagnostics);
    if (shutdown_ || !local_3d_tiles_load_diagnostics_.loaded || !node) return false;
    initializeSceneIfNeeded();
    if (!root_)
    {
        local_3d_tiles_load_diagnostics_.loaded = false;
        local_3d_tiles_load_diagnostics_.failureReason = QStringLiteral("Scene root is not initialized.");
        return false;
    }
    local_3d_tiles_load_diagnostics_.clearedPreviousPreview = local_3d_tiles_node_.valid();
    if (local_3d_tiles_node_) root_->removeChild(local_3d_tiles_node_.get());
    local_3d_tiles_node_ = std::move(node);
    root_->addChild(local_3d_tiles_node_.get());
    applyLayerVisibility(Map3DLayer::Buildings3D);
    update();
    return true;
}

void OsgEarthViewWidget::clearLocal3DTilesPreview()
{
    assertGuiThread(this, Q_FUNC_INFO);
    ++local_3d_tiles_load_generation_;
    if (shutdown_)
    {
        local_3d_tiles_node_ = nullptr;
        return;
    }
    if (root_ && local_3d_tiles_node_)
    {
        root_->removeChild(local_3d_tiles_node_.get());
    }
    local_3d_tiles_node_ = nullptr;
}

EarthLoadDiagnostics OsgEarthViewWidget::earthLoadDiagnostics() const
{
    return earth_load_diagnostics_;
}

Local3DTilesLoadDiagnostics OsgEarthViewWidget::local3DTilesLoadDiagnostics() const
{
    return local_3d_tiles_load_diagnostics_;
}

AircraftModelDiagnostics OsgEarthViewWidget::aircraftModelDiagnostics() const
{
    return aircraft_model_diagnostics_;
}

bool OsgEarthViewWidget::loadAircraftModel(const QString& modelPath)
{
    assertGuiThread(this, Q_FUNC_INFO);
    if (shutdown_)
    {
        aircraft_model_diagnostics_.failureReason = QStringLiteral("3D view is shutting down.");
        return false;
    }
    ++aircraft_load_generation_;
    initializeSceneIfNeeded();
    return loadAircraftModelFile(modelPath, QStringLiteral("User-selected aircraft model"));
}

void OsgEarthViewWidget::loadAircraftModelAsync(const QString& modelPath,
                                                std::function<void(bool)> finished)
{
    loadAircraftModelFileAsync(modelPath,
                               QStringLiteral("User-selected aircraft model"),
                               std::move(finished));
}

void OsgEarthViewWidget::resetAircraftModelToBuiltIn()
{
    assertGuiThread(this, Q_FUNC_INFO);
    ++aircraft_load_generation_;
    if (shutdown_)
    {

        return;
    }
    initializeSceneIfNeeded();
    aircraft_model_diagnostics_ = {};
    aircraft_model_diagnostics_.usingBuiltInMarker = true;
    aircraft_model_diagnostics_.failureReason =
        QStringLiteral("User reset aircraft model; using built-in marker.");
    if (aircraft_layer_)
    {
        aircraft_layer_->clearCustomModel();
    }
    update();
}

void OsgEarthViewWidget::setLayerVisible(Map3DLayer layer, bool visible)
{
    const std::size_t index = layerIndex(layer);
    if (index >= layer_visibility_.size())
    {
        return;
    }
    layer_visibility_[index] = visible;
    applyLayerVisibility(layer);
}

bool OsgEarthViewWidget::layerVisible(Map3DLayer layer) const
{
    const std::size_t index = layerIndex(layer);
    return index < layer_visibility_.size() && layer_visibility_[index];
}

bool OsgEarthViewWidget::layerAvailable(Map3DLayer layer) const
{
    if (layer == Map3DLayer::Buildings3D)
    {
        return local_3d_tiles_node_.valid();
    }
    if (layer == Map3DLayer::FlightElements)
    {
        return trajectory_layer_ && aircraft_layer_;
    }
    if (!map_node_ || !map_node_->getMap())
    {
        return false;
    }

    osgEarth::LayerVector layers;
    map_node_->getMap()->getLayers(layers);
    return std::any_of(layers.cbegin(), layers.cend(), [layer](const osg::ref_ptr<osgEarth::Layer>& candidate) {
        return dynamic_cast<osgEarth::VisibleLayer*>(candidate.get())
            && mapLayerCategory(candidate.get()) == layer;
    });
}

void OsgEarthViewWidget::applyLayerVisibility(Map3DLayer layer)
{
    const bool visible = layerVisible(layer);
    if (layer == Map3DLayer::Buildings3D)
    {
        if (local_3d_tiles_node_)
        {
            local_3d_tiles_node_->setNodeMask(visible ? ~0u : 0u);
        }
    }
    else if (layer == Map3DLayer::FlightElements)
    {
        const osg::Node::NodeMask mask = visible ? ~0u : 0u;
        if (trajectory_layer_ && trajectory_layer_->node())
        {
            trajectory_layer_->node()->setNodeMask(mask);
        }
        if (aircraft_layer_ && aircraft_layer_->node())
        {
            aircraft_layer_->node()->setNodeMask(mask);
        }
    }
    else if (map_node_ && map_node_->getMap())
    {
        osgEarth::LayerVector layers;
        map_node_->getMap()->getLayers(layers);
        for (const osg::ref_ptr<osgEarth::Layer>& candidate : layers)
        {
            auto* visibleLayer = dynamic_cast<osgEarth::VisibleLayer*>(candidate.get());
            if (visibleLayer && mapLayerCategory(candidate.get()) == layer)
            {
                visibleLayer->setVisible(visible);
            }
        }
    }
    update();
}

void OsgEarthViewWidget::applyAllLayerVisibility()
{
    for (std::size_t index = 0; index < layer_visibility_.size(); ++index)
    {
        applyLayerVisibility(static_cast<Map3DLayer>(index));
    }
}

void OsgEarthViewWidget::setFollowAircraft(bool enabled)
{
    follow_aircraft_ = enabled;
    if (shutdown_ || !viewer_)
    {
        return;
    }

    if (earth_node_ && map_node_)
    {
        auto* manipulator =
            dynamic_cast<osgEarth::EarthManipulator*>(viewer_->getCameraManipulator());
        if (!manipulator)
        {
            osg::ref_ptr<osgEarth::EarthManipulator> replacement = new osgEarth::EarthManipulator;
            viewer_->setCameraManipulator(replacement.get());
            manipulator = replacement.get();
        }
        configureEarthManipulator(manipulator);
    }
    else if (!dynamic_cast<osgGA::TrackballManipulator*>(viewer_->getCameraManipulator()))
    {
        viewer_->setCameraManipulator(new osgGA::TrackballManipulator);
    }
    if (enabled)
    {
        if (const VaporView::Geo::NavSample* latest = latestRawSample())
        {
            updateFollowCamera(toDisplaySample(*latest));
        }
    }
}

bool OsgEarthViewWidget::hasLocal3DTilesPreview() const
{
    return local_3d_tiles_node_.valid();
}

double OsgEarthViewWidget::earthCameraRangeM() const
{
    if (!viewer_ || !earth_node_ || !map_node_)
    {
        return (std::numeric_limits<double>::quiet_NaN)();
    }

    const auto* manipulator =
        dynamic_cast<const osgEarth::EarthManipulator*>(viewer_->getCameraManipulator());
    if (!manipulator)
    {
        return (std::numeric_limits<double>::quiet_NaN)();
    }

    const double rangeM = manipulator->getDistance();
    return std::isfinite(rangeM) ? rangeM : (std::numeric_limits<double>::quiet_NaN)();
}

namespace {

osgEarth::EarthManipulator* ensureEarthManipulator(osgViewer::Viewer* viewer)
{
    if (!viewer)
    {
        return nullptr;
    }
    auto* manipulator = dynamic_cast<osgEarth::EarthManipulator*>(viewer->getCameraManipulator());
    if (!manipulator)
    {
        osg::ref_ptr<osgEarth::EarthManipulator> replacement = new osgEarth::EarthManipulator;
        viewer->setCameraManipulator(replacement.get());
        manipulator = replacement.get();
    }
    configureEarthManipulator(manipulator);
    return manipulator;
}

osgGA::TrackballManipulator* ensureTrackballManipulator(osgViewer::Viewer* viewer)
{
    if (!viewer)
    {
        return nullptr;
    }
    auto* manipulator = dynamic_cast<osgGA::TrackballManipulator*>(viewer->getCameraManipulator());
    if (!manipulator)
    {
        osg::ref_ptr<osgGA::TrackballManipulator> replacement = new osgGA::TrackballManipulator;
        viewer->setCameraManipulator(replacement.get());
        manipulator = replacement.get();
    }
    return manipulator;
}

} // namespace

void OsgEarthViewWidget::setMaxVisibleSamples(int maxVisibleSamples)
{
    if (shutdown_ || !trajectory_layer_)
    {
        return;
    }
    trajectory_layer_->setMaxVisibleSamples(maxVisibleSamples);
    if (!preserve_full_track_extent_)
    {
        while (static_cast<int>(raw_samples_.size()) > trajectory_layer_->maxVisibleSamples())
        {
            raw_samples_.pop_front();
        }
    }
    if (preserve_full_track_extent_)
    {
        rebuildDisplayTrack();
    }
    update();
}

bool OsgEarthViewWidget::flyToAircraft()
{
    const VaporView::Geo::NavSample* latest = latestRawSample();
    if (shutdown_ || !latest)
    {
        return false;
    }
    initializeSceneIfNeeded();
    osg::Vec3d center;
    if (earth_node_ && map_node_)
    {
        if (!sampleWorldFocusPosition(*latest, center))
        {
            return false;
        }
        setLookAt(center, 1200.0);
    }
    else
    {
        setLookAt(samplePosition(toDisplaySample(*latest)), 600.0);
    }
    update();
    return true;
}

bool OsgEarthViewWidget::flyToTrack()
{
    if (shutdown_ || sampleCount() == 0)
    {
        return false;
    }
    initializeSceneIfNeeded();

    osg::BoundingSphere bounds;
    std::vector<VaporView::Geo::NavSample> focusSamples;
    if (shared_samples_)
    {
        focusSamples = uniformlySampleTrack(*shared_samples_,
                                            static_cast<std::size_t>(shared_sample_count_),
                                            maxVisibleSamples());
    }
    else
    {
        focusSamples.assign(raw_samples_.cbegin(), raw_samples_.cend());
    }
    if (earth_node_ && map_node_)
    {
        for (const VaporView::Geo::NavSample& sample : focusSamples)
        {
            osg::Vec3d world;
            if (sampleWorldFocusPosition(sample, world))
            {
                bounds.expandBy(world);
            }
        }
    }
    else
    {
        for (const VaporView::Geo::NavSample& sample : focusSamples)
        {
            bounds.expandBy(samplePosition(toDisplaySample(sample)));
        }
    }
    if (!bounds.valid())
    {
        return false;
    }

    setLookAt(bounds.center(),
              trackFocusRangeM(static_cast<double>(bounds.radius()), earth_node_ && map_node_));
    update();
    return true;
}

void OsgEarthViewWidget::resetView()
{
    if (shutdown_)
    {
        return;
    }
    initializeSceneIfNeeded();
    setInitialEarthView();
    update();
}

int OsgEarthViewWidget::sampleCount() const
{
    return shared_samples_ ? shared_sample_count_ : static_cast<int>(raw_samples_.size());
}

int OsgEarthViewWidget::visibleSampleCount() const
{
    return trajectory_layer_ ? trajectory_layer_->visibleSampleCount() : 0;
}

int OsgEarthViewWidget::maxVisibleSamples() const
{
    return trajectory_layer_ ? trajectory_layer_->maxVisibleSamples() : 0;
}

Map3DPerformanceStats OsgEarthViewWidget::performanceStats() const
{
    Map3DPerformanceStats stats;
    stats.totalSamples = sampleCount();
    stats.visibleSamples = visibleSampleCount();
    stats.maxVisibleSamples = maxVisibleSamples();
    stats.segmentCount = trajectory_layer_ ? trajectory_layer_->segmentCount() : 0;
    stats.segmentSize = trajectory_layer_ ? trajectory_layer_->segmentSize() : 0;
    stats.qualityStats = trajectory_layer_ ? trajectory_layer_->qualityStats() : TrajectoryQualityStats{};
    stats.frameMs = smoothed_frame_ms_ > 0.0 ? smoothed_frame_ms_ : last_frame_ms_;
    stats.framesPerSecond = frames_per_second_;
    stats.trackUpdateMs = last_track_update_ms_;
    stats.heightReferenceStatus = height_reference_status_;
    return stats;
}

QSize OsgEarthViewWidget::framebufferSize() const
{
    return framebuffer_size_;
}

bool OsgEarthViewWidget::hasEarthMap() const
{
    return earth_node_.valid();
}

void OsgEarthViewWidget::initializeGL()
{
    if (shutdown_)
    {
        return;
    }
    initializeSceneIfNeeded();
    QObject::disconnect(gl_context_destruction_connection_);
    if (QOpenGLContext* openGlContext = context())
    {
        gl_context_destruction_connection_ =
            connect(openGlContext,
                    &QOpenGLContext::aboutToBeDestroyed,
                    this,
                    &OsgEarthViewWidget::releaseGlObjectsForContextDestruction,
                    Qt::DirectConnection);
    }
}

void OsgEarthViewWidget::resizeGL(int w, int h)
{
    if (shutdown_)
    {
        return;
    }
    initializeSceneIfNeeded();
    updateCameraViewport(w, h);
}

void OsgEarthViewWidget::paintGL()
{
    if (shutdown_)
    {
        return;
    }
    initializeSceneIfNeeded();
    if (viewer_)
    {
        QElapsedTimer timer;
        timer.start();
        updateCameraProjectionForCurrentView();
        viewer_->frame();
        last_frame_ms_ = static_cast<double>(timer.nsecsElapsed()) / 1000000.0;
        smoothed_frame_ms_ = smoothed_frame_ms_ <= 0.0
            ? last_frame_ms_
            : (smoothed_frame_ms_ * 0.9 + last_frame_ms_ * 0.1);
        frames_per_second_ = smoothed_frame_ms_ > 0.0 ? 1000.0 / smoothed_frame_ms_ : 0.0;
    }
}

void OsgEarthViewWidget::mousePressEvent(QMouseEvent* event)
{
    if (shutdown_)
    {
        QOpenGLWidget::mousePressEvent(event);
        return;
    }
    initializeSceneIfNeeded();
    setFocus(Qt::MouseFocusReason);
    mouse_press_position_ = event->position();
    mouse_press_tracks_selection_ = event->button() == Qt::LeftButton;
    mouse_dragged_since_press_ = false;
    const unsigned int button = toOsgMouseButton(event->button());
    if (graphics_window_ && button != 0)
    {
        const qreal dpr = std::max<qreal>(1.0, devicePixelRatioF());
        graphics_window_->getEventQueue()->mouseButtonPress(
            static_cast<float>(event->position().x() * dpr),
            static_cast<float>(event->position().y() * dpr),
            button);
        update();
        event->accept();
        return;
    }
    QOpenGLWidget::mousePressEvent(event);
}

void OsgEarthViewWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (shutdown_)
    {
        QOpenGLWidget::mouseReleaseEvent(event);
        return;
    }
    initializeSceneIfNeeded();
    const bool canSelectTrajectory =
        mouse_press_tracks_selection_
        && event->button() == Qt::LeftButton
        && !mouse_dragged_since_press_;
    const unsigned int button = toOsgMouseButton(event->button());
    if (graphics_window_ && button != 0)
    {
        const qreal dpr = std::max<qreal>(1.0, devicePixelRatioF());
        graphics_window_->getEventQueue()->mouseButtonRelease(
            static_cast<float>(event->position().x() * dpr),
            static_cast<float>(event->position().y() * dpr),
            button);
        if (canSelectTrajectory)
        {
            selectTrajectorySampleAt(event->position());
        }
        mouse_press_tracks_selection_ = false;
        mouse_dragged_since_press_ = false;
        update();
        event->accept();
        return;
    }
    mouse_press_tracks_selection_ = false;
    mouse_dragged_since_press_ = false;
    QOpenGLWidget::mouseReleaseEvent(event);
}

void OsgEarthViewWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (shutdown_)
    {
        QOpenGLWidget::mouseMoveEvent(event);
        return;
    }
    initializeSceneIfNeeded();
    if (mouse_press_tracks_selection_)
    {
        const QPointF delta = event->position() - mouse_press_position_;
        const double distanceSq = delta.x() * delta.x() + delta.y() * delta.y();
        if (distanceSq > 25.0)
        {
            mouse_dragged_since_press_ = true;
        }
    }
    if (graphics_window_)
    {
        const qreal dpr = std::max<qreal>(1.0, devicePixelRatioF());
        graphics_window_->getEventQueue()->mouseMotion(
            static_cast<float>(event->position().x() * dpr),
            static_cast<float>(event->position().y() * dpr));
        update();
        event->accept();
        return;
    }
    QOpenGLWidget::mouseMoveEvent(event);
}

void OsgEarthViewWidget::wheelEvent(QWheelEvent* event)
{
    if (shutdown_)
    {
        QOpenGLWidget::wheelEvent(event);
        return;
    }
    initializeSceneIfNeeded();
    if (graphics_window_ && event->angleDelta().y() != 0)
    {
        const osgGA::GUIEventAdapter::ScrollingMotion motion =
            event->angleDelta().y() > 0
                ? osgGA::GUIEventAdapter::SCROLL_UP
                : osgGA::GUIEventAdapter::SCROLL_DOWN;
        graphics_window_->getEventQueue()->mouseScroll(motion);
        update();
        event->accept();
        return;
    }
    QOpenGLWidget::wheelEvent(event);
}

void OsgEarthViewWidget::keyPressEvent(QKeyEvent* event)
{
    if (shutdown_)
    {
        QOpenGLWidget::keyPressEvent(event);
        return;
    }
    initializeSceneIfNeeded();
    const int key = toOsgKey(event);
    if (graphics_window_ && key != 0)
    {
        graphics_window_->getEventQueue()->keyPress(key);
        update();
        event->accept();
        return;
    }
    QOpenGLWidget::keyPressEvent(event);
}

void OsgEarthViewWidget::keyReleaseEvent(QKeyEvent* event)
{
    if (shutdown_)
    {
        QOpenGLWidget::keyReleaseEvent(event);
        return;
    }
    initializeSceneIfNeeded();
    const int key = toOsgKey(event);
    if (graphics_window_ && key != 0)
    {
        graphics_window_->getEventQueue()->keyRelease(key);
        update();
        event->accept();
        return;
    }
    QOpenGLWidget::keyReleaseEvent(event);
}

void OsgEarthViewWidget::initializeSceneIfNeeded()
{
    if (shutdown_)
    {
        return;
    }
    if (initialized_)
    {
        return;
    }

    root_ = new osg::Group;
    overlay_transform_ = new osg::MatrixTransform;
    overlay_transform_->addChild(trajectory_layer_->node());
    overlay_transform_->addChild(aircraft_layer_->node());
    root_->addChild(createLocalGridNode());
    root_->addChild(overlay_transform_.get());
    loadDefaultAircraftModelIfAvailable();

    viewer_ = std::make_unique<osgViewer::Viewer>();
    viewer_->setThreadingModel(osgViewer::Viewer::SingleThreaded);
    viewer_->setRealizeOperation(new osgEarth::GL3RealizeOperation);
    viewer_->setSceneData(root_.get());
    if (!follow_aircraft_)
    {
        viewer_->setCameraManipulator(new osgGA::TrackballManipulator);
    }

    const qreal dpr = std::max<qreal>(1.0, devicePixelRatioF());
    const int framebufferWidth = (std::max)(1, static_cast<int>(std::lround(width() * dpr)));
    const int framebufferHeight = (std::max)(1, static_cast<int>(std::lround(height() * dpr)));
    graphics_window_ = new osgViewer::GraphicsWindowEmbedded(0, 0, framebufferWidth, framebufferHeight);
    if (viewer_->getCamera())
    {
        viewer_->getCamera()->setGraphicsContext(graphics_window_.get());
        viewer_->getCamera()->setClearColor(osg::Vec4(0.055f, 0.065f, 0.075f, 1.0f));
        viewer_->getCamera()->setViewMatrixAsLookAt(osg::Vec3d(220.0, -320.0, 240.0),
                                                    osg::Vec3d(0.0, 0.0, 0.0),
                                                    osg::Vec3d(0.0, 0.0, 1.0));
    }
    updateCameraViewport(width(), height());
    osgEarth::GL3RealizeOperation gl3RealizeOperation;
    gl3RealizeOperation(graphics_window_.get());
    initialized_ = true;
}

void OsgEarthViewWidget::loadDefaultAircraftModelIfAvailable()
{
    const QString modelPath = firstExistingMap3DFile(map3DRuntimeRootCandidates(),
                                                {QStringLiteral("resources/maps/models/aircraft/vaporview_aircraft.osgb"),
                                                 QStringLiteral("resources/maps/models/aircraft/vaporview_aircraft.osg"),
                                                 QStringLiteral("resources/maps/models/aircraft/vaporview_aircraft.glb"),
                                                 QStringLiteral("resources/maps/models/aircraft/vaporview_aircraft.gltf")});
    loadAircraftModelFileAsync(modelPath, QStringLiteral("Default aircraft model"));
}

bool OsgEarthViewWidget::loadAircraftModelFile(const QString& modelPath,
                                               const QString& fallbackReasonPrefix)
{
    const Detail::AircraftAssetLoadResult result =
        Detail::loadAircraftAsset(modelPath, fallbackReasonPrefix);
    return applyAircraftModelLoad(result.diagnostics, result.node);
}

void OsgEarthViewWidget::loadAircraftModelFileAsync(
    const QString& modelPath,
    const QString& fallbackReasonPrefix,
    std::function<void(bool)> finished)
{
    assertGuiThread(this, Q_FUNC_INFO);
    if (shutdown_)
    {
        if (finished) finished(false);
        return;
    }
    const quint64 generation = ++aircraft_load_generation_;
    aircraft_model_diagnostics_ = {};
    aircraft_model_diagnostics_.attempted = !modelPath.trimmed().isEmpty();
    aircraft_model_diagnostics_.requestedPath = modelPath;
    auto* watcher = new QFutureWatcher<Detail::AircraftAssetLoadResult>(this);
    registerAsyncWatcher(watcher);
    connect(watcher, &QFutureWatcher<Detail::AircraftAssetLoadResult>::finished,
            this, [this, watcher, generation, finished = std::move(finished)]() mutable {
        const Detail::AircraftAssetLoadResult result = watcher->result();
        unregisterAsyncWatcher(watcher);
        watcher->deleteLater();
        if (shutdown_ || generation != aircraft_load_generation_) return;
        const bool loaded = applyAircraftModelLoad(result.diagnostics, result.node);
        if (finished) finished(loaded);
    });
    watcher->setFuture(QtConcurrent::run([modelPath, fallbackReasonPrefix]() {
        return Detail::loadAircraftAsset(modelPath, fallbackReasonPrefix);
    }));
}

bool OsgEarthViewWidget::applyAircraftModelLoad(AircraftModelDiagnostics diagnostics,
                                                osg::ref_ptr<osg::Node> node)
{
    assertGuiThread(this, Q_FUNC_INFO);
    if (shutdown_ || !aircraft_layer_) return false;
    if (!diagnostics.loaded || !node)
    {
        diagnostics.usingBuiltInMarker = !aircraft_layer_->hasCustomModel();
        aircraft_model_diagnostics_ = std::move(diagnostics);
        return false;
    }
    aircraft_layer_->setCustomModel(node.get());
    aircraft_model_diagnostics_ = std::move(diagnostics);
    update();
    return true;
}

void OsgEarthViewWidget::updateCameraViewport(int w, int h)
{
    const qreal dpr = std::max<qreal>(1.0, devicePixelRatioF());

    const int safeWidth = (std::max)(1, static_cast<int>(std::lround(w * dpr)));
    const int safeHeight = (std::max)(1, static_cast<int>(std::lround(h * dpr)));
    framebuffer_size_ = QSize(safeWidth, safeHeight);

    if (graphics_window_)
    {
        graphics_window_->resized(0, 0, safeWidth, safeHeight);
        graphics_window_->getEventQueue()->windowResize(0, 0, safeWidth, safeHeight);
    }

    if (viewer_ && viewer_->getCamera())
    {
        viewer_->getCamera()->setViewport(new osg::Viewport(0, 0, safeWidth, safeHeight));
        updateCameraProjectionForCurrentView();
    }
}

void OsgEarthViewWidget::updateCameraProjectionForCurrentView()
{
    if (!viewer_ || !viewer_->getCamera())
    {
        return;
    }

    const int safeWidth = (std::max)(1, framebuffer_size_.width());
    const int safeHeight = (std::max)(1, framebuffer_size_.height());
    const double rangeM = earth_node_ && map_node_
        ? currentEarthManipulatorDistance(viewer_.get())
        : (std::numeric_limits<double>::quiet_NaN)();
    const double pitchDeg = earth_node_ && map_node_
        ? currentEarthManipulatorPitchDeg(viewer_.get())
        : (std::numeric_limits<double>::quiet_NaN)();
    const EarthProjectionProfile projectionProfile =
        earthProjectionProfile(rangeM, pitchDeg);

    osg::Camera* camera = viewer_->getCamera();
    camera->setProjectionMatrixAsPerspective(
        30.0,
        static_cast<double>(safeWidth) / static_cast<double>(safeHeight),
        kEarthProjectionNearPlaneM,
        projectionProfile.farPlaneM);
    camera->setComputeNearFarMode(osg::CullSettings::DO_NOT_COMPUTE_NEAR_FAR);
    camera->setCullingMode(osg::CullSettings::ENABLE_ALL_CULLING);
    camera->setSmallFeatureCullingPixelSize(projectionProfile.smallFeatureCullPixels);
    camera->setLODScale(projectionProfile.lodScale);
}

void OsgEarthViewWidget::updateFollowCamera(const VaporView::Geo::NavSample& sample)
{
    if (!follow_aircraft_ || !viewer_ || !viewer_->getCamera())
    {
        return;
    }

    const osg::Vec3d center = samplePosition(sample);
    const double yawRad = aircraftHeadingRad(sample);
    constexpr double kFollowDistanceM = 160.0;
    constexpr double kFollowHeightM = 90.0;

    if (earth_node_ && sample.hasEcef() && sample.hasLlh())
    {
        osgEarth::GeoPoint focalPoint;
        const osgEarth::SpatialReference* wgs84 = osgEarth::SpatialReference::get("wgs84");
        if (wgs84 && focalPoint.fromWorld(wgs84, center))
        {
            osgEarth::EarthManipulator* manipulator = ensureEarthManipulator(viewer_.get());
            if (manipulator)
            {
                const double rangeM = std::hypot(kFollowDistanceM, kFollowHeightM);
                const double pitchDeg = -std::atan2(kFollowHeightM, kFollowDistanceM)
                    * 180.0 / 3.14159265358979323846;
                manipulator->setViewpoint(
                    osgEarth::Viewpoint("Follow aircraft",
                                        focalPoint.x(),
                                        focalPoint.y(),
                                        focalPoint.z(),
                                        aircraftHeadingDeg(sample),
                                        pitchDeg,
                                        rangeM),
                    0.0);
                manipulator->updateCamera(*viewer_->getCamera());
                return;
            }
        }
        return;
    }

    if (!sample.hasNed())
    {
        return;
    }

    const osg::Vec3d eye(center.x() - std::sin(yawRad) * kFollowDistanceM,
                         center.y() - std::cos(yawRad) * kFollowDistanceM,
                         center.z() + kFollowHeightM);
    osgGA::TrackballManipulator* manipulator = ensureTrackballManipulator(viewer_.get());
    if (manipulator)
    {
        manipulator->setHomePosition(eye, center, osg::Vec3d(0.0, 0.0, 1.0), false);
        manipulator->home(0.0);
        manipulator->updateCamera(*viewer_->getCamera());
    }
}

void OsgEarthViewWidget::showEvent(QShowEvent* event)
{
    QOpenGLWidget::showEvent(event);
    if (!shutdown_)
    {
        frameTimer_.start();
    }
}

void OsgEarthViewWidget::hideEvent(QHideEvent* event)
{
    frameTimer_.stop();
    QOpenGLWidget::hideEvent(event);
}

void OsgEarthViewWidget::releaseGlObjectsForContextDestruction()
{
    if (shutdown_ || !root_ || QOpenGLContext::currentContext() != context())
    {
        return;
    }
    root_->releaseGLObjects(nullptr);
}

void OsgEarthViewWidget::setInitialEarthView()
{
    if (!viewer_)
    {
        return;
    }

    if (!earth_node_ || !map_node_)
    {
        osg::ref_ptr<osgGA::TrackballManipulator> manipulator = new osgGA::TrackballManipulator;
        manipulator->setHomePosition(osg::Vec3d(0.0, -kEarthRadiusM * 3.0, kEarthRadiusM * 1.2),
                                     osg::Vec3d(0.0, 0.0, 0.0),
                                     osg::Vec3d(0.0, 0.0, 1.0),
                                     false);
        viewer_->setCameraManipulator(manipulator.get());
        manipulator->home(0.0);

        if (viewer_->getCamera())
        {
            viewer_->getCamera()->setViewMatrixAsLookAt(osg::Vec3d(0.0, -kEarthRadiusM * 3.0, kEarthRadiusM * 1.2),
                                                        osg::Vec3d(0.0, 0.0, 0.0),
                                                        osg::Vec3d(0.0, 0.0, 1.0));
        }
        return;
    }

    osg::ref_ptr<osgEarth::EarthManipulator> manipulator = new osgEarth::EarthManipulator;
    configureEarthManipulator(manipulator.get());
    viewer_->setCameraManipulator(manipulator.get());
    const osgEarth::Viewpoint initialView =
        use_xihu_initial_view_
            ? osgEarth::Viewpoint("Hangzhou Xihu", 120.10, 30.25, 0.0, -15.0, -42.0, 11000.0)
            : osgEarth::Viewpoint("VaporView Earth", 0.0, 20.0, 0.0, 0.0, -90.0, 18000000.0);
    manipulator->setViewpoint(initialView, 0.0);
    if (viewer_->getCamera())
    {
        manipulator->updateCamera(*viewer_->getCamera());
    }
}

void OsgEarthViewWidget::setLookAt(const osg::Vec3d& center, double distanceM)
{
    if (!viewer_ || !viewer_->getCamera())
    {
        return;
    }

    const double safeDistance = (std::max)(50.0, distanceM);
    if (earth_node_ && map_node_)
    {
        if (!(center.length2() > 1.0))
        {
            return;
        }
        osgEarth::GeoPoint focalPoint;
        const osgEarth::SpatialReference* wgs84 = osgEarth::SpatialReference::get("wgs84");
        osgEarth::EarthManipulator* manipulator = ensureEarthManipulator(viewer_.get());
        if (wgs84 && manipulator && focalPoint.fromWorld(wgs84, center))
        {
            manipulator->setViewpoint(
                osgEarth::Viewpoint("Map target",
                                    focalPoint.x(),
                                    focalPoint.y(),
                                    focalPoint.z(),
                                    0.0,
                                    -35.0,
                                    safeDistance),
                0.0);
            manipulator->updateCamera(*viewer_->getCamera());
            return;
        }
        return;
    }

    const osg::Vec3d eye(center.x() - safeDistance,
                         center.y() - safeDistance,
                         center.z() + safeDistance * 0.65);
    osgGA::TrackballManipulator* manipulator = ensureTrackballManipulator(viewer_.get());
    if (manipulator)
    {
        manipulator->setHomePosition(eye, center, osg::Vec3d(0.0, 0.0, 1.0), false);
        manipulator->home(0.0);
        manipulator->updateCamera(*viewer_->getCamera());
    }
}

bool OsgEarthViewWidget::selectTrajectorySampleAt(const QPointF& widgetPosition)
{
    if (shutdown_
        || !layerVisible(Map3DLayer::FlightElements)
        || !trajectory_layer_
        || !viewer_
        || !viewer_->getCamera())
    {
        return false;
    }

    osg::Camera* camera = viewer_->getCamera();
    osg::Viewport* viewport = camera->getViewport();
    if (!viewport)
    {
        return false;
    }

    const qreal dpr = std::max<qreal>(1.0, devicePixelRatioF());
    const double screenX = widgetPosition.x() * dpr;
    const double screenYTop = widgetPosition.y() * dpr;
    const double screenYBottom =
        static_cast<double>((std::max)(1, framebuffer_size_.height())) - screenYTop;
    const osg::Matrixd localToWorld =
        overlay_transform_.valid() ? overlay_transform_->getMatrix() : osg::Matrixd::identity();
    const osg::Matrixd localToWindow =
        localToWorld
        * camera->getViewMatrix()
        * camera->getProjectionMatrix()
        * viewport->computeWindowMatrix();

    constexpr double kPickRadiusPx = 16.0;
    std::optional<TrajectoryPickResult> pick =
        trajectory_layer_->pickNearestSample(localToWindow,
                                             screenX,
                                             screenYBottom,
                                             kPickRadiusPx * static_cast<double>(dpr));
    if (!pick)
    {
        pick = trajectory_layer_->pickNearestSample(localToWindow,
                                                    screenX,
                                                    screenYTop,
                                                    kPickRadiusPx * static_cast<double>(dpr));
    }
    if (!pick)
    {
        trajectory_layer_->setSelectedSampleIndex(-1);
        emit trajectorySampleSelectionCleared();
        update();
        return false;
    }

    trajectory_layer_->setSelectedSampleIndex(pick->sampleIndex);
    emit trajectorySampleSelected(pick->sampleIndex, pick->sample);
    update();
    return true;
}

void OsgEarthViewWidget::rebuildDisplayTrack()
{
    trajectory_layer_->clear();
    aircraft_layer_->clear();
    std::vector<VaporView::Geo::NavSample> selectedSamples;
    if (shared_samples_)
    {
        selectedSamples = uniformlySampleTrack(*shared_samples_,
                                               static_cast<std::size_t>(shared_sample_count_),
                                               maxVisibleSamples());
    }
    else
    {
        selectedSamples.assign(raw_samples_.cbegin(), raw_samples_.cend());
    }
    std::vector<VaporView::Geo::NavSample> displaySamples;
    displaySamples.reserve(selectedSamples.size());
    for (const VaporView::Geo::NavSample& sample : selectedSamples)
    {
        displaySamples.push_back(toDisplaySample(sample));
    }
    trajectory_layer_->appendSamples(displaySamples);
    if (const VaporView::Geo::NavSample* latest = latestRawSample())
    {
        const VaporView::Geo::NavSample latestDisplaySample = toDisplaySample(*latest);
        aircraft_layer_->updateSample(latestDisplaySample);
        updateFollowCamera(latestDisplaySample);
    }
}

void OsgEarthViewWidget::detachSharedSamplesForLiveAppend()
{
    if (!shared_samples_)
    {
        preserve_full_track_extent_ = false;
        return;
    }
    const auto end = shared_samples_->cbegin() + shared_sample_count_;
    raw_samples_.assign(shared_samples_->cbegin(), end);
    shared_samples_.reset();
    shared_sample_count_ = 0;
    preserve_full_track_extent_ = false;
    while (static_cast<int>(raw_samples_.size()) > maxVisibleSamples())
    {
        raw_samples_.pop_front();
    }
    rebuildDisplayTrack();
}

const VaporView::Geo::NavSample* OsgEarthViewWidget::latestRawSample() const
{
    if (shared_samples_ && shared_sample_count_ > 0)
    {
        return &(*shared_samples_)[static_cast<std::size_t>(shared_sample_count_ - 1)];
    }
    return raw_samples_.empty() ? nullptr : &raw_samples_.back();
}

VaporView::Geo::NavSample OsgEarthViewWidget::toDisplaySample(const VaporView::Geo::NavSample& sample)
{
    if (earth_node_)
    {
        return toWorldSample(sample);
    }
    return toLocalSample(sample);
}

void OsgEarthViewWidget::resetWorldOverlayOrigin()
{
    has_world_overlay_origin_ = false;
    world_overlay_origin_.set(0.0, 0.0, 0.0);
    if (overlay_transform_)
    {
        overlay_transform_->setMatrix(osg::Matrix::identity());
    }
    if (trajectory_layer_)
    {
        trajectory_layer_->clearWorldOrigin();
    }
    if (aircraft_layer_)
    {
        aircraft_layer_->clearWorldOrigin();
    }
}

void OsgEarthViewWidget::updateWorldOverlayOriginFromSample(const VaporView::Geo::NavSample& sample)
{
    if (!earth_node_ || !sample.hasEcef())
    {
        return;
    }

    if (has_world_overlay_origin_)
    {
        return;
    }

    world_overlay_origin_.set(sample.ecefXM, sample.ecefYM, sample.ecefZM);
    has_world_overlay_origin_ = true;
    if (overlay_transform_)
    {
        overlay_transform_->setMatrix(osg::Matrix::translate(world_overlay_origin_));
    }
    if (trajectory_layer_)
    {
        trajectory_layer_->setWorldOrigin(world_overlay_origin_);
    }
    if (aircraft_layer_)
    {
        aircraft_layer_->setWorldOrigin(world_overlay_origin_);
    }
}

VaporView::Geo::NavSample OsgEarthViewWidget::toLocalSample(const VaporView::Geo::NavSample& sample)
{
    if (sample.hasNed() || !sample.hasLlh())
    {
        return sample;
    }

    if (!local_frame_.isValid())
    {
        local_frame_ = VaporView::Geo::LocalTangentPlane(sample);
    }

    VaporView::Geo::NavSample local = sample;
    const VaporView::Geo::NedPoint ned = VaporView::Geo::navSampleToNed(sample, local_frame_);
    local.nedNM = ned.northM;
    local.nedEM = ned.eastM;
    local.nedDM = ned.downM;
    return local;
}

VaporView::Geo::NavSample OsgEarthViewWidget::toWorldSample(const VaporView::Geo::NavSample& sample)
{
    const bool hasRecordedEcef = sample.hasEcef();
    if (hasRecordedEcef)
    {
        height_reference_status_ = QStringLiteral("Using recorded ECEF position; no height datum conversion required.");
        VaporView::Geo::NavSample worldSample = sample;
        if (!worldSample.hasLlh())
        {
            const VaporView::Geo::LlhPoint llh = VaporView::Geo::ecefToLlh(
                {sample.ecefXM, sample.ecefYM, sample.ecefZM});
            worldSample.latDeg = llh.latDeg;
            worldSample.lonDeg = llh.lonDeg;
            worldSample.heightM = llh.heightM;
            worldSample.heightReference = VaporView::Geo::HeightReference::Wgs84Ellipsoid;
        }
        updateWorldOverlayOriginFromSample(worldSample);
        return worldSample;
    }

    if (!sample.hasLlh())
    {
        return sample;
    }

    if (sample.heightReference != VaporView::Geo::HeightReference::Wgs84Ellipsoid)
    {
        height_reference_status_ = QStringLiteral(
            "Height datum cannot be converted without recorded ECEF; sample omitted from the Earth overlay.");
        VaporView::Geo::NavSample invalidWorldSample = sample;
        const double invalid = (std::numeric_limits<double>::quiet_NaN)();
        invalidWorldSample.latDeg = invalid;
        invalidWorldSample.lonDeg = invalid;
        invalidWorldSample.heightM = invalid;
        invalidWorldSample.ecefXM = invalid;
        invalidWorldSample.ecefYM = invalid;
        invalidWorldSample.ecefZM = invalid;
        invalidWorldSample.nedNM = invalid;
        invalidWorldSample.nedEM = invalid;
        invalidWorldSample.nedDM = invalid;
        return invalidWorldSample;
    }

    height_reference_status_ = QStringLiteral("WGS84 ellipsoid height applied.");

    const osgEarth::SpatialReference* wgs84 = osgEarth::SpatialReference::get("wgs84");
    if (!wgs84)
    {
        return sample;
    }

    osgEarth::GeoPoint geo(wgs84, sample.lonDeg, sample.latDeg, sample.heightM, osgEarth::ALTMODE_ABSOLUTE);
    osg::Vec3d world;
    if (!geo.toWorld(world))
    {
        return sample;
    }

    VaporView::Geo::NavSample worldSample = sample;
    worldSample.ecefXM = world.x();
    worldSample.ecefYM = world.y();
    worldSample.ecefZM = world.z();
    updateWorldOverlayOriginFromSample(worldSample);
    return worldSample;
}

} // namespace VaporView::Map3D
