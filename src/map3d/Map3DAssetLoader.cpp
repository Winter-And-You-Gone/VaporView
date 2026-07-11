#include "Map3DAssetLoader.h"
#include "Map3DRuntime.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

#include <osg/Geometry>
#include <osg/Geode>
#include <osg/Texture2D>
#include <osgDB/ReadFile>
#include <osgEarth/Layer>
#include <osgEarth/Map>
#include <osgEarth/MapNode>
#include <osgEarth/Registry>
#include <osgUtil/Optimizer>

#include <cmath>
#include <functional>

namespace VaporView::Map3D::Detail {
namespace {

constexpr double kEarthRadiusM = 6378137.0;
constexpr qint64 kMaximumTilesetJsonBytes = 64LL * 1024LL * 1024LL;
constexpr int kMaximumTileCount = 100000;
constexpr int kMaximumTileDepth = 128;

QString naturalEarthTexturePath(const QString& earthPath)
{
    const QFileInfo earthInfo(earthPath);
    if (earthInfo.fileName().compare(QStringLiteral("vaporview_default.earth"),
                                     Qt::CaseInsensitive) != 0)
    {
        return {};
    }
    const QString relative = QStringLiteral("natural_earth/NE2_50M_SR_W/NE2_50M_SR_W_2048.png");
    const QString adjacent = QDir::cleanPath(earthInfo.dir().absoluteFilePath(relative));
    if (QFileInfo(adjacent).isFile())
    {
        return QFileInfo(adjacent).absoluteFilePath();
    }
    for (const QString& root : map3DRuntimeRootCandidates())
    {
        const QString candidate = QDir::cleanPath(
            QDir(root).absoluteFilePath(QStringLiteral("resources/maps/%1").arg(relative)));
        if (QFileInfo(candidate).isFile())
        {
            return QFileInfo(candidate).absoluteFilePath();
        }
    }
    return {};
}

osg::ref_ptr<osg::Node> createTexturedEarthNode(const QString& texturePath)
{
    osg::ref_ptr<osg::Image> image = osgDB::readImageFile(texturePath.toStdString());
    if (!image)
    {
        return {};
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
        const double v = static_cast<double>(latIndex) / kLatSegments;
        const double latRad = osg::PI_2 - v * osg::PI;
        const double cosLat = std::cos(latRad);
        const double sinLat = std::sin(latRad);
        for (int lonIndex = 0; lonIndex <= kLonSegments; ++lonIndex)
        {
            const double u = static_cast<double>(lonIndex) / kLonSegments;
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
    return geode;
}

QString resolvedPath(const QString& path)
{
    const QFileInfo info(path);
    const QString canonical = info.canonicalFilePath();
    if (!canonical.isEmpty())
    {
        return QDir::cleanPath(canonical);
    }
    const QString parentCanonical = QDir(info.absolutePath()).canonicalPath();
    return parentCanonical.isEmpty()
        ? QDir::cleanPath(info.absoluteFilePath())
        : QDir(parentCanonical).absoluteFilePath(info.fileName());
}

bool pathIsWithin(const QString& path, const QString& root)
{
    QString candidate = QDir::fromNativeSeparators(resolvedPath(path));
    QString boundary = QDir::fromNativeSeparators(resolvedPath(root));
#ifdef Q_OS_WIN
    candidate = candidate.toLower();
    boundary = boundary.toLower();
#endif
    if (!boundary.endsWith(QLatin1Char('/')))
    {
        boundary.append(QLatin1Char('/'));
    }
    return candidate == boundary.chopped(1) || candidate.startsWith(boundary);
}

QString localPayloadPath(const QString& tilesetDirectory, QString uri)
{
    const int query = uri.indexOf(QLatin1Char('?'));
    const int fragment = uri.indexOf(QLatin1Char('#'));
    int cut = query;
    if (cut < 0 || (fragment >= 0 && fragment < cut))
    {
        cut = fragment;
    }
    if (cut >= 0)
    {
        uri.truncate(cut);
    }
    const QString normalized = QDir::cleanPath(QDir::fromNativeSeparators(uri));
    if (normalized.isEmpty() || QDir::isAbsolutePath(normalized)
        || normalized == QStringLiteral("..") || normalized.startsWith(QStringLiteral("../"))
        || uri.contains(QStringLiteral("://")))
    {
        return {};
    }
    const QString absolute = QFileInfo(QDir(tilesetDirectory).absoluteFilePath(normalized)).absoluteFilePath();
    return pathIsWithin(absolute, tilesetDirectory) ? absolute : QString{};
}

} // namespace

EarthAssetLoadResult loadEarthAsset(const QString& earthPath)
{
    EarthAssetLoadResult result;
    result.diagnostics.attempted = true;
    result.diagnostics.requestedPath = earthPath;
    result.useXihuInitialView = QFileInfo(earthPath).fileName().compare(
        QStringLiteral("vaporview_real3d_local.earth"), Qt::CaseInsensitive) == 0;
    result.node = osgDB::readNodeFile(QDir::fromNativeSeparators(earthPath).toStdString());
    if (!result.node)
    {
        result.node = createTexturedEarthNode(naturalEarthTexturePath(earthPath));
        if (!result.node)
        {
            result.diagnostics.failureReason = QStringLiteral("osgDB::readNodeFile returned null.");
            return result;
        }
        result.diagnostics.loaded = true;
        result.diagnostics.usedTexturedFallback = true;
        result.diagnostics.failureReason = QStringLiteral(
            "osgDB::readNodeFile returned null. Using manual Natural Earth textured globe fallback.");
        result.diagnostics.layerSummaries.push_back(
            QStringLiteral("Manual Natural Earth textured globe fallback (no osgEarth MapNode)."));
        return result;
    }

    result.mapNode = osgEarth::MapNode::findMapNode(result.node.get());
    result.diagnostics.foundMapNode = result.mapNode != nullptr;
    if (!result.mapNode)
    {
        result.diagnostics.failureReason = QStringLiteral("Loaded OSG node, but no osgEarth MapNode was found.");
        result.diagnostics.layerSummaries.push_back(result.diagnostics.failureReason);
        result.node = nullptr;
        return result;
    }
    result.mapNode->openMapLayers();
    if (result.mapNode->getMap())
    {
        osgEarth::LayerVector layers;
        result.mapNode->getMap()->getLayers(layers);
        result.diagnostics.layerCount = static_cast<int>(layers.size());
        for (const osg::ref_ptr<osgEarth::Layer>& layer : layers)
        {
            if (!layer)
            {
                continue;
            }
            if (layer->isOpen())
            {
                ++result.diagnostics.openLayerCount;
            }
            result.diagnostics.layerSummaries.push_back(
                QStringLiteral("%1 | open=%2 | status=%3")
                    .arg(QString::fromStdString(layer->getName()),
                         layer->isOpen() ? QStringLiteral("yes") : QStringLiteral("no"),
                         QString::fromStdString(layer->getStatus().toString())));
        }
    }
    result.diagnostics.loaded = true;
    return result;
}

Local3DTilesAssetLoadResult loadLocal3DTilesAsset(const QString& tilesetPath)
{
    Local3DTilesAssetLoadResult result;
    auto& diagnostics = result.diagnostics;
    diagnostics.attempted = true;
    diagnostics.requestedPath = tilesetPath;
    const QFileInfo info(tilesetPath);
    if (!info.isFile())
    {
        diagnostics.failureReason = QStringLiteral("Local 3D Tiles tileset file does not exist.");
        return result;
    }
    if (info.size() > kMaximumTilesetJsonBytes)
    {
        diagnostics.failureReason = QStringLiteral("Local 3D tile index exceeds the 64 MiB safety limit.");
        return result;
    }
    QFile file(info.absoluteFilePath());
    if (!file.open(QIODevice::ReadOnly))
    {
        diagnostics.failureReason = QStringLiteral("Local 3D Tiles tileset could not be opened: %1").arg(file.errorString());
        return result;
    }
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
    {
        diagnostics.failureReason = QStringLiteral("Local 3D Tiles tileset is not valid JSON: %1").arg(error.errorString());
        return result;
    }
    const QJsonObject tileset = document.object();
    const QJsonObject rootTile = tileset.value(QStringLiteral("root")).toObject();
    if (rootTile.isEmpty())
    {
        diagnostics.failureReason = QStringLiteral("Local 3D tile index does not contain a root tile object.");
        return result;
    }
    if (tileset.value(QStringLiteral("extras")).toObject().value(QStringLiteral("format")).toString()
        != QStringLiteral("vaporview-osg-native-building-tiles"))
    {
        diagnostics.failureReason = QStringLiteral("Unsupported building payload contract. This loader accepts only vaporview-osg-native-building-tiles, not generic Cesium 3D Tiles.");
        return result;
    }

    osg::ref_ptr<osg::Group> tilesRoot = new osg::Group;
    tilesRoot->setName("VaporView local building tiles");
    bool traversalExceeded = false;
    const QString tilesetDirectory = info.absolutePath();
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
        ++diagnostics.payloadCount;
        const QString payloadPath = localPayloadPath(tilesetDirectory, uri);
        if (payloadPath.isEmpty())
        {
            ++diagnostics.failedPayloadCount;
            diagnostics.warnings.push_back(QStringLiteral("Rejected non-local tile payload URI: %1").arg(uri));
            return;
        }
        osg::ref_ptr<osg::Node> payload = osgDB::readNodeFile(
            QDir::fromNativeSeparators(payloadPath).toStdString());
        if (!payload)
        {
            ++diagnostics.failedPayloadCount;
            diagnostics.warnings.push_back(QStringLiteral("Failed to load local tile payload: %1").arg(payloadPath));
            return;
        }
        tilesRoot->addChild(payload.get());
        ++diagnostics.loadedPayloadCount;
    };
    loadTile = [&](const QJsonObject& tile, int depth) {
        if (depth > kMaximumTileDepth || diagnostics.tileCount >= kMaximumTileCount)
        {
            traversalExceeded = true;
            return;
        }
        ++diagnostics.tileCount;
        if (tile.value(QStringLiteral("content")).isObject())
        {
            loadContent(tile.value(QStringLiteral("content")).toObject());
        }
        for (const QJsonValue& content : tile.value(QStringLiteral("contents")).toArray())
        {
            if (content.isObject())
            {
                loadContent(content.toObject());
            }
        }
        for (const QJsonValue& child : tile.value(QStringLiteral("children")).toArray())
        {
            if (child.isObject())
            {
                loadTile(child.toObject(), depth + 1);
            }
        }
    };
    loadTile(rootTile, 0);
    if (traversalExceeded)
    {
        diagnostics.failureReason = QStringLiteral("Local 3D tile index exceeds the traversal safety limit; previous preview was preserved.");
        return result;
    }
    if (diagnostics.loadedPayloadCount == 0 || diagnostics.failedPayloadCount > 0)
    {
        diagnostics.failureReason = diagnostics.warnings.isEmpty()
            ? QStringLiteral("Local tileset did not contain a loadable payload.")
            : QStringLiteral("Local tileset load was incomplete (%1/%2 payloads); previous preview was preserved. %3")
                  .arg(diagnostics.loadedPayloadCount)
                  .arg(diagnostics.payloadCount)
                  .arg(diagnostics.warnings.constFirst());
        return result;
    }
    osgEarth::Registry::shaderGenerator().run(tilesRoot.get());
    diagnostics.loaded = true;
    diagnostics.nodeDescription = QStringLiteral("%1 loaded payloads across %2 tiles")
                                      .arg(diagnostics.loadedPayloadCount)
                                      .arg(diagnostics.tileCount);
    result.node = tilesRoot;
    return result;
}

AircraftAssetLoadResult loadAircraftAsset(const QString& modelPath,
                                          const QString& reasonPrefix)
{
    AircraftAssetLoadResult result;
    auto& diagnostics = result.diagnostics;
    diagnostics.requestedPath = modelPath;
    diagnostics.usingBuiltInMarker = true;
    const QString trimmed = modelPath.trimmed();
    if (trimmed.isEmpty())
    {
        diagnostics.failureReason = QStringLiteral("%1 not configured; using built-in marker.").arg(reasonPrefix);
        return result;
    }
    const QFileInfo info(trimmed);
    if (!info.isFile())
    {
        diagnostics.failureReason = QStringLiteral("%1 file does not exist; using built-in marker.").arg(reasonPrefix);
        return result;
    }
    diagnostics.attempted = true;
    diagnostics.requestedPath = info.absoluteFilePath();
    result.node = osgDB::readNodeFile(QDir::fromNativeSeparators(info.absoluteFilePath()).toStdString());
    if (!result.node)
    {
        diagnostics.failureReason = QStringLiteral("%1 could not be read by osgDB::readNodeFile; using built-in marker.").arg(reasonPrefix);
        return result;
    }
    osgUtil::Optimizer optimizer;
    optimizer.optimize(result.node.get(), osgUtil::Optimizer::DEFAULT_OPTIMIZATIONS);
    diagnostics.loaded = true;
    diagnostics.usingBuiltInMarker = false;
    diagnostics.nodeDescription = QStringLiteral("%1 children, bound radius %2")
                                      .arg(result.node->asGroup() ? result.node->asGroup()->getNumChildren() : 0)
                                      .arg(result.node->getBound().radius(), 0, 'f', 2);
    return result;
}

} // namespace VaporView::Map3D::Detail
