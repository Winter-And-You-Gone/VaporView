#include "map3d/OsgEarthViewWidget.h"

#include "map3d/Aircraft3DLayer.h"
#include "map3d/Trajectory3DLayer.h"

#include <osg/Camera>
#include <osg/Geometry>
#include <osg/Geode>
#include <osg/Group>
#include <osg/Viewport>
#include <osgDB/ReadFile>
#include <osgGA/TrackballManipulator>
#include <osgViewer/GraphicsWindow>
#include <osgViewer/Viewer>
#include <osgEarth/EarthManipulator>
#include <osgEarth/GeoData>
#include <osgEarth/MapNode>
#include <osgEarth/SpatialReference>
#include <osgEarth/Viewpoint>

#include <algorithm>
#include <cmath>
#include <limits>

namespace VaporView::Map3D {
namespace {

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
    if (std::isfinite(sample.ecefXM)
        && std::isfinite(sample.ecefYM)
        && std::isfinite(sample.ecefZM))
    {
        return osg::Vec3d(sample.ecefXM, sample.ecefYM, sample.ecefZM);
    }
    if (sample.hasNed())
    {
        return osg::Vec3d(sample.nedEM, sample.nedNM, -sample.nedDM);
    }
    return osg::Vec3d(sample.lonDeg * 100000.0, sample.latDeg * 100000.0, sample.heightM);
}

} // namespace

OsgEarthViewWidget::OsgEarthViewWidget(QWidget* parent)
    : QOpenGLWidget(parent)
    , trajectory_layer_(std::make_unique<Trajectory3DLayer>())
    , aircraft_layer_(std::make_unique<Aircraft3DLayer>())
{
    setMinimumSize(640, 420);
    setFocusPolicy(Qt::StrongFocus);
    frameTimer_.setInterval(33);
    connect(&frameTimer_, &QTimer::timeout, this, QOverload<>::of(&OsgEarthViewWidget::update));
    frameTimer_.start();
}

OsgEarthViewWidget::~OsgEarthViewWidget()
{
    shutdown();
}

void OsgEarthViewWidget::shutdown()
{
    frameTimer_.stop();
    if (shutdown_)
    {
        return;
    }
    shutdown_ = true;
    if (initialized_)
    {
        makeCurrent();
    }
    if (viewer_)
    {
        viewer_->setDone(true);
        viewer_->setCameraManipulator(nullptr);
        if (viewer_->getCamera())
        {
            viewer_->getCamera()->setGraphicsContext(nullptr);
        }
        viewer_->setSceneData(nullptr);
    }
    if (root_)
    {
        root_->removeChildren(0, root_->getNumChildren());
    }
    earth_node_ = nullptr;
    map_node_ = nullptr;
    trajectory_layer_.reset();
    aircraft_layer_.reset();
    root_ = nullptr;
    graphics_window_ = nullptr;
    viewer_.reset();
    if (initialized_)
    {
        doneCurrent();
    }
}

void OsgEarthViewWidget::appendSample(const VaporView::Geo::NavSample& sample)
{
    raw_samples_.push_back(sample);
    const VaporView::Geo::NavSample displaySample = toDisplaySample(sample);
    trajectory_layer_->appendSample(displaySample);
    aircraft_layer_->updateSample(displaySample);
    updateFollowCamera(displaySample);
    update();
}

void OsgEarthViewWidget::appendSamples(const std::vector<VaporView::Geo::NavSample>& samples)
{
    raw_samples_.insert(raw_samples_.end(), samples.cbegin(), samples.cend());
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
    update();
}

void OsgEarthViewWidget::clearTrack()
{
    raw_samples_.clear();
    trajectory_layer_->clear();
    aircraft_layer_->clear();
    has_local_origin_ = false;
    update();
}

bool OsgEarthViewWidget::loadEarthFile(const QString& earthPath)
{
    initializeSceneIfNeeded();
    osg::ref_ptr<osg::Node> earthNode = osgDB::readNodeFile(earthPath.toStdString());
    if (!earthNode || !root_)
    {
        return false;
    }
    if (earth_node_)
    {
        root_->removeChild(earth_node_.get());
    }
    earth_node_ = earthNode;
    map_node_ = osgEarth::MapNode::findMapNode(earth_node_.get());
    trajectory_layer_->setUseWorldCoordinates(true);
    aircraft_layer_->setUseWorldCoordinates(true);
    root_->insertChild(0, earth_node_.get());
    setInitialEarthView();
    rebuildDisplayTrack();
    update();
    return true;
}

void OsgEarthViewWidget::setFollowAircraft(bool enabled)
{
    follow_aircraft_ = enabled;
    if (!viewer_)
    {
        return;
    }

    if (follow_aircraft_)
    {
        viewer_->setCameraManipulator(nullptr);
    }
    else if (!viewer_->getCameraManipulator())
    {
        if (earth_node_)
        {
            viewer_->setCameraManipulator(new osgEarth::EarthManipulator);
        }
        else
        {
            viewer_->setCameraManipulator(new osgGA::TrackballManipulator);
        }
    }
}

int OsgEarthViewWidget::sampleCount() const
{
    return trajectory_layer_ ? trajectory_layer_->sampleCount() : 0;
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
    initializeSceneIfNeeded();
}

void OsgEarthViewWidget::resizeGL(int w, int h)
{
    initializeSceneIfNeeded();
    updateCameraViewport(w, h);
}

void OsgEarthViewWidget::paintGL()
{
    initializeSceneIfNeeded();
    if (viewer_)
    {
        viewer_->frame();
    }
}

void OsgEarthViewWidget::initializeSceneIfNeeded()
{
    if (initialized_)
    {
        return;
    }

    root_ = new osg::Group;
    root_->addChild(createLocalGridNode());
    root_->addChild(trajectory_layer_->node());
    root_->addChild(aircraft_layer_->node());

    viewer_ = std::make_unique<osgViewer::Viewer>();
    viewer_->setThreadingModel(osgViewer::Viewer::SingleThreaded);
    viewer_->setSceneData(root_.get());
    if (!follow_aircraft_)
    {
        viewer_->setCameraManipulator(new osgGA::TrackballManipulator);
    }

    const qreal dpr = std::max<qreal>(1.0, devicePixelRatioF());
    const int framebufferWidth = std::max(1, static_cast<int>(std::lround(width() * dpr)));
    const int framebufferHeight = std::max(1, static_cast<int>(std::lround(height() * dpr)));
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
    initialized_ = true;
}

void OsgEarthViewWidget::updateCameraViewport(int w, int h)
{
    const qreal dpr = std::max<qreal>(1.0, devicePixelRatioF());
    const int safeWidth = std::max(1, static_cast<int>(std::lround(w * dpr)));
    const int safeHeight = std::max(1, static_cast<int>(std::lround(h * dpr)));
    framebuffer_size_ = QSize(safeWidth, safeHeight);

    if (graphics_window_)
    {
        graphics_window_->resized(0, 0, safeWidth, safeHeight);
        graphics_window_->getEventQueue()->windowResize(0, 0, safeWidth, safeHeight);
    }

    if (viewer_ && viewer_->getCamera())
    {
        viewer_->getCamera()->setViewport(new osg::Viewport(0, 0, safeWidth, safeHeight));
        viewer_->getCamera()->setProjectionMatrixAsPerspective(
            30.0,
            static_cast<double>(safeWidth) / static_cast<double>(safeHeight),
            1.0,
            10000000.0);
    }
}

void OsgEarthViewWidget::updateFollowCamera(const VaporView::Geo::NavSample& sample)
{
    if (!follow_aircraft_ || !viewer_ || !viewer_->getCamera())
    {
        return;
    }

    const osg::Vec3d center = samplePosition(sample);
    const double yawRad = std::isfinite(sample.yawDeg) ? sample.yawDeg * 3.14159265358979323846 / 180.0 : 0.0;
    constexpr double kFollowDistanceM = 160.0;
    constexpr double kFollowHeightM = 90.0;

    if (earth_node_
        && std::isfinite(sample.ecefXM)
        && std::isfinite(sample.ecefYM)
        && std::isfinite(sample.ecefZM)
        && sample.hasLlh())
    {
        const double latRad = sample.latDeg * 3.14159265358979323846 / 180.0;
        const double lonRad = sample.lonDeg * 3.14159265358979323846 / 180.0;
        const osg::Vec3d east(-std::sin(lonRad), std::cos(lonRad), 0.0);
        const osg::Vec3d north(-std::sin(latRad) * std::cos(lonRad),
                               -std::sin(latRad) * std::sin(lonRad),
                               std::cos(latRad));
        const osg::Vec3d up(std::cos(latRad) * std::cos(lonRad),
                            std::cos(latRad) * std::sin(lonRad),
                            std::sin(latRad));
        const osg::Vec3d forward = east * std::sin(yawRad) + north * std::cos(yawRad);
        const osg::Vec3d eye = center - forward * kFollowDistanceM + up * kFollowHeightM;
        viewer_->getCamera()->setViewMatrixAsLookAt(eye, center, up);
        return;
    }

    if (!sample.hasNed())
    {
        return;
    }

    const osg::Vec3d eye(center.x() - std::sin(yawRad) * kFollowDistanceM,
                         center.y() - std::cos(yawRad) * kFollowDistanceM,
                         center.z() + kFollowHeightM);
    viewer_->getCamera()->setViewMatrixAsLookAt(eye, center, osg::Vec3d(0.0, 0.0, 1.0));
}

void OsgEarthViewWidget::setInitialEarthView()
{
    if (!viewer_)
    {
        return;
    }

    osg::ref_ptr<osgEarth::EarthManipulator> manipulator = new osgEarth::EarthManipulator;
    viewer_->setCameraManipulator(manipulator.get());
    manipulator->setViewpoint(osgEarth::Viewpoint("VaporView Earth", 0.0, 20.0, 0.0, 0.0, -90.0, 18000000.0), 0.0);
    if (viewer_->getCamera())
    {
        manipulator->updateCamera(*viewer_->getCamera());
    }
    if (follow_aircraft_)
    {
        viewer_->setCameraManipulator(nullptr);
    }
}

void OsgEarthViewWidget::rebuildDisplayTrack()
{
    trajectory_layer_->clear();
    aircraft_layer_->clear();
    std::vector<VaporView::Geo::NavSample> displaySamples;
    displaySamples.reserve(raw_samples_.size());
    for (const VaporView::Geo::NavSample& sample : raw_samples_)
    {
        displaySamples.push_back(toDisplaySample(sample));
    }
    trajectory_layer_->appendSamples(displaySamples);
    if (!displaySamples.empty())
    {
        aircraft_layer_->updateSample(displaySamples.back());
        updateFollowCamera(displaySamples.back());
    }
}

VaporView::Geo::NavSample OsgEarthViewWidget::toDisplaySample(const VaporView::Geo::NavSample& sample)
{
    if (earth_node_)
    {
        return toWorldSample(sample);
    }
    return toLocalSample(sample);
}

VaporView::Geo::NavSample OsgEarthViewWidget::toLocalSample(const VaporView::Geo::NavSample& sample)
{
    if (sample.hasNed() || !sample.hasLlh())
    {
        return sample;
    }

    if (!has_local_origin_)
    {
        origin_lat_deg_ = sample.latDeg;
        origin_lon_deg_ = sample.lonDeg;
        origin_height_m_ = sample.heightM;
        has_local_origin_ = true;
    }

    VaporView::Geo::NavSample local = sample;
    constexpr double kMetersPerDegreeLat = 111320.0;
    const double originLatRad = origin_lat_deg_ * 3.14159265358979323846 / 180.0;
    const double metersPerDegreeLon = kMetersPerDegreeLat * std::max(0.01, std::cos(originLatRad));
    local.nedNM = (sample.latDeg - origin_lat_deg_) * kMetersPerDegreeLat;
    local.nedEM = (sample.lonDeg - origin_lon_deg_) * metersPerDegreeLon;
    local.nedDM = origin_height_m_ - sample.heightM;
    return local;
}

VaporView::Geo::NavSample OsgEarthViewWidget::toWorldSample(const VaporView::Geo::NavSample& sample) const
{
    if (!sample.hasLlh())
    {
        return sample;
    }

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
    worldSample.nedNM = std::numeric_limits<double>::quiet_NaN();
    worldSample.nedEM = std::numeric_limits<double>::quiet_NaN();
    worldSample.nedDM = std::numeric_limits<double>::quiet_NaN();
    return worldSample;
}

} // namespace VaporView::Map3D
