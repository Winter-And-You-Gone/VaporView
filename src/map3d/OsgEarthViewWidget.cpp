#include "map3d/OsgEarthViewWidget.h"

#include "map3d/Aircraft3DLayer.h"
#include "map3d/AircraftHeading.h"
#include "map3d/TrackSampling.h"
#include "map3d/Trajectory3DLayer.h"

#include <osg/Camera>
#include <osg/BoundingSphere>
#include <osg/Geometry>
#include <osg/Geode>
#include <osg/Group>
#include <osg/Texture2D>
#include <osg/Viewport>
#include <osgDB/ReadFile>
#include <osgDB/Registry>
#include <osgUtil/Optimizer>
#include <osgGA/EventQueue>
#include <osgGA/TrackballManipulator>
#include <osgViewer/GraphicsWindow>
#include <osgViewer/Viewer>
#include <osgEarth/EarthManipulator>
#include <osgEarth/GeoData>
#include <osgEarth/GLUtils>
#include <osgEarth/MapNode>
#include <osgEarth/Map>
#include <osgEarth/Registry>
#include <osgEarth/SpatialReference>
#include <osgEarth/Viewpoint>

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHideEvent>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QOpenGLContext>
#include <QShowEvent>
#include <QThread>
#include <QWheelEvent>
#include <QElapsedTimer>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <mutex>

namespace VaporView::Map3D {
namespace {

constexpr double kEarthRadiusM = 6378137.0;

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

QStringList runtimeRootCandidates()
{
    const QString appDir = QCoreApplication::applicationDirPath();
    QStringList roots{
        appDir,
        QDir(appDir).absoluteFilePath(QStringLiteral("../.."))
    };
    if (qEnvironmentVariableIsSet("VAPORVIEW_MAP3D_DEV_SEARCH_PATHS"))
    {
        roots.push_back(QDir::currentPath());
    }
    roots.removeDuplicates();
    return roots;
}

QString firstExistingDirectory(const QStringList& roots, const QStringList& relatives)
{
    for (const QString& root : roots)
    {
        for (const QString& relative : relatives)
        {
            const QString candidate = QDir::cleanPath(QDir(root).absoluteFilePath(relative));
            if (QFileInfo(candidate).isDir())
            {
                return QFileInfo(candidate).absoluteFilePath();
            }
        }
    }
    return {};
}

QString firstExistingDirectoryMatching(const QStringList& roots,
                                       const QStringList& relatives,
                                       const QString& namePattern)
{
    for (const QString& root : roots)
    {
        for (const QString& relative : relatives)
        {
            QDir directory(QDir::cleanPath(QDir(root).absoluteFilePath(relative)));
            if (!directory.exists())
            {
                continue;
            }

            const QFileInfoList matches =
                directory.entryInfoList({namePattern}, QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
            for (const QFileInfo& match : matches)
            {
                if (match.isDir())
                {
                    return match.absoluteFilePath();
                }
            }
        }
    }
    return {};
}

QString findOsgPluginDirectory(const QStringList& roots)
{
    const QString exact = firstExistingDirectory(roots, {
        QStringLiteral("osgPlugins-3.6.5"),
        QStringLiteral("plugins/osgPlugins-3.6.5"),
        QStringLiteral(".local_deps/vcpkg_installed/x64-windows/plugins/osgPlugins-3.6.5")
    });
    if (!exact.isEmpty())
    {
        return exact;
    }

    return firstExistingDirectoryMatching(roots,
                                          {QStringLiteral("."),
                                           QStringLiteral("plugins"),
                                           QStringLiteral(".local_deps/vcpkg_installed/x64-windows/plugins")},
                                          QStringLiteral("osgPlugins-*"));
}

QString firstExistingFile(const QStringList& roots, const QStringList& relatives)
{
    for (const QString& root : roots)
    {
        for (const QString& relative : relatives)
        {
            const QString candidate = QDir::cleanPath(QDir(root).absoluteFilePath(relative));
            if (QFileInfo(candidate).isFile())
            {
                return QFileInfo(candidate).absoluteFilePath();
            }
        }
    }
    return {};
}

void prependEnvironmentPath(const char* name, const QString& path)
{
    if (path.isEmpty())
    {
        return;
    }

    const QByteArray pathBytes = QDir::toNativeSeparators(path).toLocal8Bit();
    const QByteArray current = qgetenv(name);
    if (current.isEmpty())
    {
        qputenv(name, pathBytes);
        return;
    }

    const QList<QByteArray> parts = current.split(';');
    if (!parts.contains(pathBytes))
    {
        qputenv(name, pathBytes + ';' + current);
    }
}

void setEnvironmentIfMissing(const char* name, const QString& path)
{
    if (!path.isEmpty() && qgetenv(name).isEmpty())
    {
        qputenv(name, QDir::toNativeSeparators(path).toLocal8Bit());
    }
}

void initializeOsgEarthRuntime()
{
    static std::once_flag once;
    std::call_once(once, [] {
        const QStringList roots = runtimeRootCandidates();
        const QString pluginDir = findOsgPluginDirectory(roots);
        const QString gdalDataDir = firstExistingDirectory(roots, {
            QStringLiteral("share/gdal"),
            QStringLiteral(".local_deps/vcpkg_installed/x64-windows/share/gdal")
        });
        const QString projDataDir = firstExistingDirectory(roots, {
            QStringLiteral("share/proj"),
            QStringLiteral("share/proj4"),
            QStringLiteral(".local_deps/vcpkg_installed/x64-windows/share/proj"),
            QStringLiteral(".local_deps/vcpkg_installed/x64-windows/share/proj4")
        });

        prependEnvironmentPath("OSG_LIBRARY_PATH", pluginDir);
        setEnvironmentIfMissing("GDAL_DATA", gdalDataDir);
        setEnvironmentIfMissing("PROJ_LIB", projDataDir);
        setEnvironmentIfMissing("PROJ_DATA", projDataDir);

        if (!pluginDir.isEmpty())
        {
            osgDB::Registry::instance()->getLibraryFilePathList().push_front(pluginDir.toStdString());
        }

        osgEarth::initialize();
        qInfo().noquote() << "osgEarth runtime initialized"
                          << "plugins=" << (pluginDir.isEmpty() ? QStringLiteral("<not found>") : pluginDir)
                          << "GDAL_DATA=" << QString::fromLocal8Bit(qgetenv("GDAL_DATA"))
                          << "PROJ_LIB=" << QString::fromLocal8Bit(qgetenv("PROJ_LIB"));
    });
}

QString naturalEarthTexturePathForEarthFile(const QString& earthPath)
{
    const QFileInfo earthInfo(earthPath);
    if (earthInfo.fileName() != QStringLiteral("vaporview_default.earth"))
    {
        return {};
    }

    const QString textureRelative = QStringLiteral("natural_earth/NE2_50M_SR_W/NE2_50M_SR_W_2048.png");
    const QString candidate = QDir::cleanPath(earthInfo.dir().absoluteFilePath(textureRelative));
    if (QFileInfo(candidate).isFile())
    {
        return QFileInfo(candidate).absoluteFilePath();
    }

    for (const QString& root : runtimeRootCandidates())
    {
        const QString fallback = QDir::cleanPath(QDir(root).absoluteFilePath(
            QStringLiteral("resources/maps/%1").arg(textureRelative)));
        if (QFileInfo(fallback).isFile())
        {
            return QFileInfo(fallback).absoluteFilePath();
        }
    }

    return {};
}

osg::Node* createTexturedEarthNode(const QString& texturePath)
{
    osg::ref_ptr<osg::Image> image = osgDB::readImageFile(texturePath.toStdString());
    if (!image)
    {
        return nullptr;
    }

    constexpr int kLatSegments = 64;
    constexpr int kLonSegments = 128;
    osg::ref_ptr<osg::Geometry> geometry = new osg::Geometry;
    osg::ref_ptr<osg::Vec3Array> vertices = new osg::Vec3Array;
    osg::ref_ptr<osg::Vec3Array> normals = new osg::Vec3Array;
    osg::ref_ptr<osg::Vec2Array> texCoords = new osg::Vec2Array;
    vertices->reserve((kLatSegments + 1) * (kLonSegments + 1));
    normals->reserve(vertices->capacity());
    texCoords->reserve(vertices->capacity());

    for (int latIndex = 0; latIndex <= kLatSegments; ++latIndex)
    {
        const double v = static_cast<double>(latIndex) / static_cast<double>(kLatSegments);
        const double latRad = osg::PI_2 - v * osg::PI;
        const double cosLat = std::cos(latRad);
        const double sinLat = std::sin(latRad);
        for (int lonIndex = 0; lonIndex <= kLonSegments; ++lonIndex)
        {
            const double u = static_cast<double>(lonIndex) / static_cast<double>(kLonSegments);
            const double lonRad = -osg::PI + u * 2.0 * osg::PI;
            const osg::Vec3d normal(cosLat * std::cos(lonRad),
                                    cosLat * std::sin(lonRad),
                                    sinLat);
            vertices->push_back(normal * kEarthRadiusM);
            normals->push_back(normal);
            texCoords->push_back(osg::Vec2(static_cast<float>(1.0 - u), static_cast<float>(v)));
        }
    }

    osg::ref_ptr<osg::DrawElementsUInt> indices = new osg::DrawElementsUInt(GL_TRIANGLES);
    indices->reserve(kLatSegments * kLonSegments * 6);
    for (int latIndex = 0; latIndex < kLatSegments; ++latIndex)
    {
        for (int lonIndex = 0; lonIndex < kLonSegments; ++lonIndex)
        {
            const unsigned int first = static_cast<unsigned int>(latIndex * (kLonSegments + 1) + lonIndex);
            const unsigned int second = first + static_cast<unsigned int>(kLonSegments + 1);
            indices->push_back(first);
            indices->push_back(second);
            indices->push_back(first + 1);
            indices->push_back(second);
            indices->push_back(second + 1);
            indices->push_back(first + 1);
        }
    }

    geometry->setVertexArray(vertices.get());
    geometry->setNormalArray(normals.get(), osg::Array::BIND_PER_VERTEX);
    geometry->setTexCoordArray(0, texCoords.get());
    geometry->addPrimitiveSet(indices.get());

    osg::ref_ptr<osg::Texture2D> texture = new osg::Texture2D(image.get());
    texture->setResizeNonPowerOfTwoHint(false);
    texture->setFilter(osg::Texture::MIN_FILTER, osg::Texture::LINEAR);
    texture->setFilter(osg::Texture::MAG_FILTER, osg::Texture::LINEAR);
    texture->setWrap(osg::Texture::WRAP_S, osg::Texture::REPEAT);
    texture->setWrap(osg::Texture::WRAP_T, osg::Texture::CLAMP_TO_EDGE);

    osg::ref_ptr<osg::Geode> geode = new osg::Geode;
    geode->addDrawable(geometry.get());
    geode->getOrCreateStateSet()->setTextureAttributeAndModes(0, texture.get(), osg::StateAttribute::ON);
    geode->getOrCreateStateSet()->setMode(GL_LIGHTING, osg::StateAttribute::OFF);
    return geode.release();
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
    initializeOsgEarthRuntime();
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

void OsgEarthViewWidget::appendSample(const VaporView::Geo::NavSample& sample)
{
    assertGuiThread(this, Q_FUNC_INFO);
    if (shutdown_ || !trajectory_layer_ || !aircraft_layer_)
    {
        return;
    }
    QElapsedTimer timer;
    timer.start();
    if (preserve_full_track_extent_)
    {
        preserve_full_track_extent_ = false;
        rebuildDisplayTrack();
    }
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
    if (preserve_full_track_extent_ && !samples.empty())
    {
        preserve_full_track_extent_ = false;
        rebuildDisplayTrack();
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
    assertGuiThread(this, Q_FUNC_INFO);
    if (shutdown_ || !trajectory_layer_ || !aircraft_layer_)
    {
        return;
    }
    QElapsedTimer timer;
    timer.start();
    raw_samples_.assign(samples.cbegin(), samples.cend());
    preserve_full_track_extent_ = true;
    local_frame_ = VaporView::Geo::LocalTangentPlane();
    resetWorldOverlayOrigin();
    rebuildDisplayTrack();
    last_track_update_ms_ = static_cast<double>(timer.nsecsElapsed()) / 1000000.0;
    update();
}

void OsgEarthViewWidget::clearTrack()
{
    assertGuiThread(this, Q_FUNC_INFO);
    raw_samples_.clear();
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
    earth_load_diagnostics_ = {};
    earth_load_diagnostics_.attempted = true;
    earth_load_diagnostics_.requestedPath = earthPath;
    if (shutdown_)
    {
        earth_load_diagnostics_.failureReason = QStringLiteral("3D view is shutting down.");
        return false;
    }
    const bool candidateUsesXihuInitialView =
        QFileInfo(earthPath).fileName().compare(QStringLiteral("vaporview_real3d_local.earth"),
                                                Qt::CaseInsensitive) == 0;

    initializeSceneIfNeeded();
    osg::ref_ptr<osg::Node> earthNode = osgDB::readNodeFile(earthPath.toStdString());
    if (!earthNode)
    {
        const QString readFailure = QStringLiteral("osgDB::readNodeFile returned null.");
        const QString texturePath = naturalEarthTexturePathForEarthFile(earthPath);
        osg::ref_ptr<osg::Node> texturedEarthNode = createTexturedEarthNode(texturePath);
        if (texturedEarthNode && root_)
        {
            const bool replacedPreviousNode = earth_node_.valid();
            if (earth_node_)
            {
                root_->removeChild(earth_node_.get());
            }
            earth_node_ = texturedEarthNode;
            map_node_ = nullptr;
            use_xihu_initial_view_ = candidateUsesXihuInitialView;
            trajectory_layer_->setUseWorldCoordinates(true);
            aircraft_layer_->setUseWorldCoordinates(true);
            resetWorldOverlayOrigin();
            root_->insertChild(0, earth_node_.get());
            setInitialEarthView();
            rebuildDisplayTrack();
            update();
            earth_load_diagnostics_.loaded = true;
            earth_load_diagnostics_.usedTexturedFallback = true;
            earth_load_diagnostics_.foundMapNode = false;
            earth_load_diagnostics_.failureReason =
                QStringLiteral("%1 Using manual Natural Earth textured globe fallback.").arg(readFailure);
            earth_load_diagnostics_.layerSummaries.push_back(
                QStringLiteral("Manual Natural Earth textured globe fallback (no osgEarth MapNode)."));
            if (replacedPreviousNode)
            {
                earth_load_diagnostics_.layerSummaries.push_back(QStringLiteral("Replaced previous Earth scene."));
            }
            return true;
        }

        earth_load_diagnostics_.failureReason = readFailure;
        return false;
    }
    if (!root_)
    {
        earth_load_diagnostics_.failureReason = QStringLiteral("Scene root is not initialized.");
        return false;
    }
    osgEarth::MapNode* candidateMapNode = osgEarth::MapNode::findMapNode(earthNode.get());
    earth_load_diagnostics_.foundMapNode = candidateMapNode != nullptr;
    if (!candidateMapNode)
    {
        earth_load_diagnostics_.failureReason = QStringLiteral("Loaded OSG node, but no osgEarth MapNode was found.");
        earth_load_diagnostics_.layerSummaries.push_back(earth_load_diagnostics_.failureReason);
        return false;
    }

    candidateMapNode->openMapLayers();
    if (candidateMapNode->getMap())
    {
        osgEarth::LayerVector layers;
        candidateMapNode->getMap()->getLayers(layers);
        earth_load_diagnostics_.layerCount = static_cast<int>(layers.size());
        for (const osg::ref_ptr<osgEarth::Layer>& layer : layers)
        {
            if (!layer)
            {
                continue;
            }
            const osgEarth::Status& status = layer->getStatus();
            if (layer->isOpen())
            {
                ++earth_load_diagnostics_.openLayerCount;
            }
            const QString summary = QStringLiteral("%1 | open=%2 | status=%3")
                .arg(QString::fromStdString(layer->getName()),
                     layer->isOpen() ? QStringLiteral("yes") : QStringLiteral("no"),
                     QString::fromStdString(status.toString()));
            earth_load_diagnostics_.layerSummaries.push_back(summary);
            qInfo().noquote()
                << "osgEarth layer"
                << QString::fromStdString(layer->getName())
                << "open=" << layer->isOpen()
                << "status=" << QString::fromStdString(status.toString());
        }
    }

    if (earth_node_)
    {
        root_->removeChild(earth_node_.get());
    }
    earth_node_ = earthNode;
    map_node_ = candidateMapNode;
    use_xihu_initial_view_ = candidateUsesXihuInitialView;
    earth_load_diagnostics_.loaded = true;
    trajectory_layer_->setUseWorldCoordinates(true);
    aircraft_layer_->setUseWorldCoordinates(true);
    resetWorldOverlayOrigin();
    root_->insertChild(0, earth_node_.get());
    setInitialEarthView();
    rebuildDisplayTrack();
    update();
    return true;
}

bool OsgEarthViewWidget::loadLocal3DTilesPreview(const QString& tilesetPath)
{
    assertGuiThread(this, Q_FUNC_INFO);
    local_3d_tiles_load_diagnostics_ = {};
    local_3d_tiles_load_diagnostics_.attempted = true;
    local_3d_tiles_load_diagnostics_.requestedPath = tilesetPath;
    if (shutdown_)
    {
        local_3d_tiles_load_diagnostics_.failureReason = QStringLiteral("3D view is shutting down.");
        return false;
    }
    const QFileInfo tilesetInfo(tilesetPath);
    if (!tilesetInfo.isFile())
    {
        local_3d_tiles_load_diagnostics_.failureReason =
            QStringLiteral("Local 3D Tiles tileset file does not exist.");
        return false;
    }

    constexpr qint64 kMaximumTilesetJsonBytes = 64LL * 1024LL * 1024LL;
    if (tilesetInfo.size() > kMaximumTilesetJsonBytes)
    {
        local_3d_tiles_load_diagnostics_.failureReason =
            QStringLiteral("Local 3D tile index exceeds the 64 MiB safety limit.");
        return false;
    }

    QFile tilesetFile(tilesetInfo.absoluteFilePath());
    if (!tilesetFile.open(QIODevice::ReadOnly))
    {
        local_3d_tiles_load_diagnostics_.failureReason =
            QStringLiteral("Local 3D Tiles tileset could not be opened: %1").arg(tilesetFile.errorString());
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(tilesetFile.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        local_3d_tiles_load_diagnostics_.failureReason =
            QStringLiteral("Local 3D Tiles tileset is not valid JSON: %1").arg(parseError.errorString());
        return false;
    }
    if (!document.object().value(QStringLiteral("root")).isObject())
    {
        local_3d_tiles_load_diagnostics_.failureReason =
            QStringLiteral("Local 3D tile index does not contain a root tile object.");
        return false;
    }
    const QString payloadFormat = document.object().value(QStringLiteral("extras")).toObject()
                                      .value(QStringLiteral("format")).toString();
    if (payloadFormat != QStringLiteral("vaporview-osg-native-building-tiles"))
    {
        local_3d_tiles_load_diagnostics_.failureReason =
            QStringLiteral("Unsupported building payload contract. This loader accepts only vaporview-osg-native-building-tiles, not generic Cesium 3D Tiles.");
        return false;
    }

    initializeSceneIfNeeded();
    if (!root_)
    {
        local_3d_tiles_load_diagnostics_.failureReason =
            QStringLiteral("Scene root is not initialized.");
        return false;
    }

    osg::ref_ptr<osg::Group> tilesRoot = new osg::Group;
    tilesRoot->setName("VaporView local building tiles");
    const QString tilesetDirectory = tilesetInfo.absolutePath();

    constexpr int kMaximumTileCount = 100000;
    constexpr int kMaximumTileDepth = 128;
    bool traversalLimitExceeded = false;
    std::function<void(const QJsonObject&, int)> loadTile;
    const auto loadContent = [&](const QJsonObject& content) {
        QString uri = content.value(QStringLiteral("uri")).toString();
        if (uri.isEmpty())
        {
            uri = content.value(QStringLiteral("url")).toString();
        }
        if (uri.isEmpty())
        {
            return;
        }
        ++local_3d_tiles_load_diagnostics_.payloadCount;
        const int queryIndex = uri.indexOf(QLatin1Char('?'));
        const int fragmentIndex = uri.indexOf(QLatin1Char('#'));
        int cutIndex = -1;
        if (queryIndex >= 0)
        {
            cutIndex = queryIndex;
        }
        if (fragmentIndex >= 0 && (cutIndex < 0 || fragmentIndex < cutIndex))
        {
            cutIndex = fragmentIndex;
        }
        if (cutIndex >= 0)
        {
            uri = uri.left(cutIndex);
        }
        const QString normalizedUri = QDir::cleanPath(QDir::fromNativeSeparators(uri));
        if (QDir::isAbsolutePath(normalizedUri)
            || normalizedUri == QStringLiteral("..")
            || normalizedUri.startsWith(QStringLiteral("../"))
            || uri.contains(QStringLiteral("://")))
        {
            ++local_3d_tiles_load_diagnostics_.failedPayloadCount;
            local_3d_tiles_load_diagnostics_.warnings.push_back(
                QStringLiteral("Rejected non-local tile payload URI: %1").arg(uri));
            return;
        }
        const QString payloadPath = QFileInfo(QDir(tilesetDirectory).absoluteFilePath(normalizedUri)).absoluteFilePath();
        osg::ref_ptr<osg::Node> payload =
            osgDB::readNodeFile(QDir::fromNativeSeparators(payloadPath).toStdString());
        if (!payload)
        {
            ++local_3d_tiles_load_diagnostics_.failedPayloadCount;
            local_3d_tiles_load_diagnostics_.warnings.push_back(
                QStringLiteral("Failed to load local tile payload: %1").arg(payloadPath));
            return;
        }
        tilesRoot->addChild(payload.get());
        ++local_3d_tiles_load_diagnostics_.loadedPayloadCount;
    };
    loadTile = [&](const QJsonObject& tile, int depth) {
        if (depth > kMaximumTileDepth || local_3d_tiles_load_diagnostics_.tileCount >= kMaximumTileCount)
        {
            traversalLimitExceeded = true;
            return;
        }
        ++local_3d_tiles_load_diagnostics_.tileCount;
        if (tile.value(QStringLiteral("content")).isObject())
        {
            loadContent(tile.value(QStringLiteral("content")).toObject());
        }
        const QJsonArray contents = tile.value(QStringLiteral("contents")).toArray();
        for (const QJsonValue& content : contents)
        {
            if (content.isObject())
            {
                loadContent(content.toObject());
            }
        }
        const QJsonArray children = tile.value(QStringLiteral("children")).toArray();
        for (const QJsonValue& child : children)
        {
            if (child.isObject())
            {
                loadTile(child.toObject(), depth + 1);
            }
        }
    };
    loadTile(document.object().value(QStringLiteral("root")).toObject(), 0);

    if (traversalLimitExceeded)
    {
        local_3d_tiles_load_diagnostics_.failureReason =
            QStringLiteral("Local 3D tile index exceeds the traversal safety limit; previous preview was preserved.");
        return false;
    }

    if (local_3d_tiles_load_diagnostics_.loadedPayloadCount == 0
        || local_3d_tiles_load_diagnostics_.failedPayloadCount > 0)
    {
        local_3d_tiles_load_diagnostics_.failureReason =
            local_3d_tiles_load_diagnostics_.warnings.isEmpty()
                ? QStringLiteral("Local tileset did not contain a loadable payload.")
                : QStringLiteral("Local tileset load was incomplete (%1/%2 payloads); previous preview was preserved. %3")
                      .arg(local_3d_tiles_load_diagnostics_.loadedPayloadCount)
                      .arg(local_3d_tiles_load_diagnostics_.payloadCount)
                      .arg(local_3d_tiles_load_diagnostics_.warnings.constFirst());
        return false;
    }

    osgEarth::Registry::shaderGenerator().run(tilesRoot.get());
    local_3d_tiles_load_diagnostics_.clearedPreviousPreview = local_3d_tiles_node_.valid();
    if (local_3d_tiles_node_)
    {
        root_->removeChild(local_3d_tiles_node_.get());
    }
    local_3d_tiles_node_ = tilesRoot;
    root_->addChild(local_3d_tiles_node_.get());
    local_3d_tiles_load_diagnostics_.loaded = true;
    local_3d_tiles_load_diagnostics_.nodeDescription =
        QStringLiteral("%1 loaded payloads across %2 tiles")
            .arg(local_3d_tiles_load_diagnostics_.loadedPayloadCount)
            .arg(local_3d_tiles_load_diagnostics_.tileCount);
    update();
    return true;
}

void OsgEarthViewWidget::clearLocal3DTilesPreview()
{
    assertGuiThread(this, Q_FUNC_INFO);
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
    initializeSceneIfNeeded();
    return loadAircraftModelFile(modelPath, QStringLiteral("User-selected aircraft model"));
}

void OsgEarthViewWidget::resetAircraftModelToBuiltIn()
{
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

void OsgEarthViewWidget::setFollowAircraft(bool enabled)
{
    follow_aircraft_ = enabled;
    if (shutdown_ || !viewer_)
    {
        return;
    }

    if (earth_node_ && map_node_)
    {
        if (!dynamic_cast<osgEarth::EarthManipulator*>(viewer_->getCameraManipulator()))
        {
            viewer_->setCameraManipulator(new osgEarth::EarthManipulator);
        }
    }
    else if (!dynamic_cast<osgGA::TrackballManipulator*>(viewer_->getCameraManipulator()))
    {
        viewer_->setCameraManipulator(new osgGA::TrackballManipulator);
    }
    if (enabled && !raw_samples_.empty())
    {
        updateFollowCamera(toDisplaySample(raw_samples_.back()));
    }
}

bool OsgEarthViewWidget::hasLocal3DTilesPreview() const
{
    return local_3d_tiles_node_.valid();
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
    if (shutdown_ || raw_samples_.empty())
    {
        return false;
    }
    initializeSceneIfNeeded();
    setLookAt(samplePosition(toDisplaySample(raw_samples_.back())), 600.0);
    update();
    return true;
}

bool OsgEarthViewWidget::flyToTrack()
{
    if (shutdown_ || raw_samples_.empty())
    {
        return false;
    }
    initializeSceneIfNeeded();

    osg::BoundingSphere bounds;
    for (const VaporView::Geo::NavSample& sample : raw_samples_)
    {
        bounds.expandBy(samplePosition(toDisplaySample(sample)));
    }
    if (!bounds.valid())
    {
        return false;
    }

    setLookAt(bounds.center(), (std::max)(300.0, static_cast<double>(bounds.radius()) * 3.0));
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
    return static_cast<int>(raw_samples_.size());
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
    const unsigned int button = toOsgMouseButton(event->button());
    if (graphics_window_ && button != 0)
    {
        const qreal dpr = std::max<qreal>(1.0, devicePixelRatioF());
        graphics_window_->getEventQueue()->mouseButtonRelease(
            static_cast<float>(event->position().x() * dpr),
            static_cast<float>(event->position().y() * dpr),
            button);
        update();
        event->accept();
        return;
    }
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
    const QString modelPath = firstExistingFile(runtimeRootCandidates(),
                                                {QStringLiteral("resources/maps/models/aircraft/vaporview_aircraft.osgb"),
                                                 QStringLiteral("resources/maps/models/aircraft/vaporview_aircraft.osg"),
                                                 QStringLiteral("resources/maps/models/aircraft/vaporview_aircraft.glb"),
                                                 QStringLiteral("resources/maps/models/aircraft/vaporview_aircraft.gltf")});
    loadAircraftModelFile(modelPath, QStringLiteral("Default aircraft model"));
}

bool OsgEarthViewWidget::loadAircraftModelFile(const QString& modelPath, const QString& fallbackReasonPrefix)
{
    aircraft_model_diagnostics_ = {};
    aircraft_model_diagnostics_.usingBuiltInMarker = true;
    aircraft_model_diagnostics_.requestedPath = modelPath;

    if (!aircraft_layer_)
    {
        aircraft_model_diagnostics_.failureReason =
            QStringLiteral("%1 could not be applied before the aircraft layer was initialized; using built-in marker.")
                .arg(fallbackReasonPrefix);
        return false;
    }

    const QString trimmedPath = modelPath.trimmed();
    if (trimmedPath.isEmpty())
    {
        aircraft_model_diagnostics_.failureReason =
            QStringLiteral("%1 not configured; using built-in marker.").arg(fallbackReasonPrefix);
        aircraft_layer_->clearCustomModel();
        update();
        return false;
    }

    const QFileInfo modelInfo(trimmedPath);
    if (!modelInfo.isFile())
    {
        aircraft_model_diagnostics_.failureReason =
            QStringLiteral("%1 file does not exist; using built-in marker.").arg(fallbackReasonPrefix);
        aircraft_layer_->clearCustomModel();
        update();
        return false;
    }

    aircraft_model_diagnostics_.attempted = true;
    aircraft_model_diagnostics_.requestedPath = modelInfo.absoluteFilePath();

    osg::ref_ptr<osg::Node> modelNode = osgDB::readNodeFile(modelInfo.absoluteFilePath().toStdString());
    if (!modelNode)
    {
        aircraft_model_diagnostics_.failureReason =
            QStringLiteral("%1 could not be read by osgDB::readNodeFile; using built-in marker.")
                .arg(fallbackReasonPrefix);
        aircraft_layer_->clearCustomModel();
        update();
        return false;
    }

    osgUtil::Optimizer optimizer;
    optimizer.optimize(modelNode.get(), osgUtil::Optimizer::DEFAULT_OPTIMIZATIONS);
    aircraft_layer_->setCustomModel(modelNode.get());
    aircraft_model_diagnostics_.loaded = true;
    aircraft_model_diagnostics_.usingBuiltInMarker = false;
    aircraft_model_diagnostics_.nodeDescription =
        QStringLiteral("%1 children, bound radius %2")
            .arg(modelNode->asGroup() ? modelNode->asGroup()->getNumChildren() : 0)
            .arg(modelNode->getBound().radius(), 0, 'f', 2);
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
    const double yawRad = aircraftHeadingRad(sample);
    constexpr double kFollowDistanceM = 160.0;
    constexpr double kFollowHeightM = 90.0;

    if (earth_node_
        && std::isfinite(sample.ecefXM)
        && std::isfinite(sample.ecefYM)
        && std::isfinite(sample.ecefZM)
        && sample.hasLlh())
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
    if (earth_node_ && map_node_ && center.length2() > 1.0)
    {
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

void OsgEarthViewWidget::rebuildDisplayTrack()
{
    trajectory_layer_->clear();
    aircraft_layer_->clear();
    const std::vector<VaporView::Geo::NavSample> rawSamples(raw_samples_.cbegin(), raw_samples_.cend());
    const std::vector<VaporView::Geo::NavSample> selectedSamples =
        preserve_full_track_extent_
        ? uniformlySampleTrack(rawSamples, maxVisibleSamples())
        : rawSamples;
    std::vector<VaporView::Geo::NavSample> displaySamples;
    displaySamples.reserve(selectedSamples.size());
    for (const VaporView::Geo::NavSample& sample : selectedSamples)
    {
        displaySamples.push_back(toDisplaySample(sample));
    }
    trajectory_layer_->appendSamples(displaySamples);
    if (!raw_samples_.empty())
    {
        const VaporView::Geo::NavSample latestDisplaySample = toDisplaySample(raw_samples_.back());
        aircraft_layer_->updateSample(latestDisplaySample);
        updateFollowCamera(latestDisplaySample);
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
    if (!earth_node_
        || !std::isfinite(sample.ecefXM)
        || !std::isfinite(sample.ecefYM)
        || !std::isfinite(sample.ecefZM))
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
    const bool hasRecordedEcef = std::isfinite(sample.ecefXM)
        && std::isfinite(sample.ecefYM)
        && std::isfinite(sample.ecefZM);
    if (hasRecordedEcef)
    {
        height_reference_status_ = QStringLiteral("Using recorded ECEF position; no height datum conversion required.");
        updateWorldOverlayOriginFromSample(sample);
        return sample;
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
