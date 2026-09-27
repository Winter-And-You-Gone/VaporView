#include "map3d/MapDataManager.h"
#include "Map3DRuntime.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QFileInfo>
#include <QtCore/QFile>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonParseError>
#include <QtCore/QJsonValue>
#include <QtCore/QProcessEnvironment>

#include <utility>

namespace VaporView::Map3D {
namespace {

constexpr auto kDefaultEarthRelative = "resources/maps/vaporview_default.earth";
constexpr auto kCopernicusEarthRelative = "resources/maps/vaporview_with_dem.earth";
constexpr auto kSrtmEarthRelative = "resources/maps/vaporview_with_srtm.earth";
constexpr auto kFullLocalEarthRelative = "resources/maps/vaporview_full_local.earth";
constexpr auto kFullLocalSrtmEarthRelative = "resources/maps/vaporview_full_local_srtm.earth";
constexpr auto kReal3DLocalEarthRelative = "resources/maps/vaporview_real3d_local.earth";
constexpr auto kSentinel2ImageryEarthRelative = "resources/maps/vaporview_with_sentinel2_imagery.earth";
constexpr auto kLandsatImageryEarthRelative = "resources/maps/vaporview_with_landsat_imagery.earth";
constexpr auto kOpenAerialMapImageryEarthRelative = "resources/maps/vaporview_with_openaerialmap_imagery.earth";
constexpr auto kNaturalEarthTextureRelative = "resources/maps/natural_earth/NE2_50M_SR_W/NE2_50M_SR_W_2048.png";
constexpr auto kNaturalEarthVrtRelative = "resources/maps/natural_earth/NE2_50M_SR_W/NE2_50M_SR_W.vrt";
constexpr auto kNaturalEarthRasterRelative = "resources/maps/natural_earth/NE2_50M_SR_W/NE2_50M_SR_W.tif";
constexpr auto kCopernicusDemVrtRelative = "resources/maps/terrain/copernicus_dem_glo30/copernicus_dem_glo30.vrt";
constexpr auto kSrtmDemVrtRelative = "resources/maps/terrain/srtm/srtm.vrt";
constexpr auto kOsmRoadsRelative = "resources/maps/osm/roads.gpkg";
constexpr auto kOsmWaterRelative = "resources/maps/osm/water.gpkg";
constexpr auto kOsmBuildingsRelative = "resources/maps/osm/buildings.gpkg";
constexpr auto kOsmPlacesRelative = "resources/maps/osm/places.gpkg";
constexpr auto kSentinel2ImageryVrtRelative = "resources/maps/imagery/sentinel2/sentinel2.vrt";
constexpr auto kLandsatImageryVrtRelative = "resources/maps/imagery/landsat/landsat.vrt";
constexpr auto kOpenAerialMapImageryVrtRelative = "resources/maps/imagery/openaerialmap/openaerialmap.vrt";
constexpr auto kLocal3DTilesTilesetRelative = "resources/maps/tiles3d/local/tileset.json";
constexpr qint64 kMaximumTilesetJsonBytes = 64LL * 1024LL * 1024LL;
constexpr int kMaximumTileCount = 100000;
constexpr int kMaximumTileDepth = 128;

QString absolutePath(const QString& root, const char* relative)
{
    return QDir::cleanPath(QDir(root).absoluteFilePath(QString::fromLatin1(relative)));
}

bool isFile(const QString& path)
{
    return QFileInfo(path).isFile();
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

QStringList osgEarthEnvironmentVariables(const QProcessEnvironment& environment)
{
    QStringList entries;
    const QStringList keys = environment.keys();
    for (const QString& key : keys)
    {
        if (key.startsWith(QStringLiteral("OSGEARTH_")))
        {
            entries.push_back(QStringLiteral("%1=%2").arg(key, environment.value(key)));
        }
    }
    entries.sort(Qt::CaseInsensitive);
    return entries;
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

void recordFile(MapDataDiagnostics& diagnostics, const QString& path)
{
    if (isFile(path))
    {
        diagnostics.foundFiles.push_back(path);
    }
    else
    {
        diagnostics.missingFiles.push_back(path);
    }
}

void recordOptionalFile(MapDataDiagnostics& diagnostics, const QString& path)
{
    if (isFile(path))
    {
        diagnostics.foundFiles.push_back(path);
    }
}

bool hasCompleteOsmSet(const MapDataDiagnostics& diagnostics)
{
    return isFile(diagnostics.osmRoadsPath)
        && isFile(diagnostics.osmWaterPath)
        && isFile(diagnostics.osmBuildingsPath)
        && isFile(diagnostics.osmPlacesPath);
}

bool hasNaturalEarth(const MapDataDiagnostics& diagnostics)
{
    return isFile(diagnostics.naturalEarthVrtPath) && isFile(diagnostics.naturalEarthRasterPath);
}

bool hasAnyDem(const MapDataDiagnostics& diagnostics)
{
    return isFile(diagnostics.copernicusDemVrtPath) || isFile(diagnostics.srtmDemVrtPath);
}

QString fullLocalEarthForAvailableDem(const QString& copernicusEarthPath,
                                      const QString& srtmEarthPath,
                                      const MapDataDiagnostics& diagnostics)
{
    if (diagnostics.copernicusDemAvailable && isFile(copernicusEarthPath))
    {
        return copernicusEarthPath;
    }
    if (diagnostics.srtmDemAvailable && isFile(srtmEarthPath))
    {
        return srtmEarthPath;
    }
    return {};
}

int osmLayerCount(const MapDataDiagnostics& diagnostics)
{
    int count = 0;
    if (diagnostics.osmRoadsAvailable)
    {
        ++count;
    }
    if (diagnostics.osmWaterAvailable)
    {
        ++count;
    }
    if (diagnostics.osmBuildingsAvailable)
    {
        ++count;
    }
    if (diagnostics.osmPlacesAvailable)
    {
        ++count;
    }
    return count;
}

int localImageryLayerCount(const MapDataDiagnostics& diagnostics)
{
    int count = 0;
    if (isFile(diagnostics.sentinel2ImageryVrtPath))
    {
        ++count;
    }
    if (isFile(diagnostics.landsatImageryVrtPath))
    {
        ++count;
    }
    if (isFile(diagnostics.openAerialMapImageryVrtPath))
    {
        ++count;
    }
    return count;
}

int localImageryMenuEntryCount(const std::vector<LocalImageryOption>& options)
{
    int count = 0;
    for (const LocalImageryOption& option : options)
    {
        if (option.available)
        {
            ++count;
        }
    }
    return count;
}

std::vector<LocalImageryOption> localImageryOptions(const MapDataDiagnostics& diagnostics)
{
    return {
        {QStringLiteral("sentinel2"),
         QStringLiteral("Sentinel-2 本地影像"),
         diagnostics.sentinel2ImageryEarthPath,
         diagnostics.sentinel2ImageryVrtPath,
         isFile(diagnostics.sentinel2ImageryEarthPath) && isFile(diagnostics.sentinel2ImageryVrtPath)},
        {QStringLiteral("landsat"),
         QStringLiteral("Landsat 本地影像"),
         diagnostics.landsatImageryEarthPath,
         diagnostics.landsatImageryVrtPath,
         isFile(diagnostics.landsatImageryEarthPath) && isFile(diagnostics.landsatImageryVrtPath)},
        {QStringLiteral("openaerialmap"),
         QStringLiteral("OpenAerialMap 本地影像"),
         diagnostics.openAerialMapImageryEarthPath,
         diagnostics.openAerialMapImageryVrtPath,
         isFile(diagnostics.openAerialMapImageryEarthPath) && isFile(diagnostics.openAerialMapImageryVrtPath)}
    };
}

QString stripUriQueryAndFragment(QString uri)
{
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
    return cutIndex >= 0 ? uri.left(cutIndex) : uri;
}

bool hasUriSchemeOrNetworkPath(const QString& uri)
{
    const QString trimmed = uri.trimmed();
    const QString lower = trimmed.toLower();
    if (lower.startsWith(QStringLiteral("//")))
    {
        return true;
    }

    const int colonIndex = lower.indexOf(QLatin1Char(':'));
    if (colonIndex <= 0)
    {
        return false;
    }

    const int slashIndex = lower.indexOf(QLatin1Char('/'));
    const int backslashIndex = lower.indexOf(QLatin1Char('\\'));
    int firstSeparator = -1;
    if (slashIndex >= 0)
    {
        firstSeparator = slashIndex;
    }
    if (backslashIndex >= 0 && (firstSeparator < 0 || backslashIndex < firstSeparator))
    {
        firstSeparator = backslashIndex;
    }
    return firstSeparator < 0 || colonIndex < firstSeparator;
}

QString resolvedPathForBoundaryCheck(const QString& path)
{
    QString current = QFileInfo(path).absoluteFilePath();
    QStringList missingSuffix;
    while (!current.isEmpty())
    {
        const QFileInfo info(current);
        const QString canonical = info.canonicalFilePath();
        if (!canonical.isEmpty())
        {
            QString resolved = canonical;
            for (auto it = missingSuffix.crbegin(); it != missingSuffix.crend(); ++it)
            {
                resolved = QDir(resolved).absoluteFilePath(*it);
            }
            return QDir::cleanPath(resolved);
        }

        const QString name = info.fileName();
        if (!name.isEmpty())
        {
            missingSuffix.push_back(name);
        }
        const QString parent = info.absolutePath();
        if (parent == current)
        {
            break;
        }
        current = parent;
    }
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

bool pathStartsWithDirectory(const QString& path, const QString& directory)
{
    QString cleanPath = resolvedPathForBoundaryCheck(path).replace(QLatin1Char('\\'), QLatin1Char('/'));
    QString cleanDirectory = resolvedPathForBoundaryCheck(directory).replace(QLatin1Char('\\'), QLatin1Char('/'));
#ifdef Q_OS_WIN
    cleanPath = cleanPath.toLower();
    cleanDirectory = cleanDirectory.toLower();
#endif
    if (!cleanDirectory.endsWith(QLatin1Char('/')))
    {
        cleanDirectory.append(QLatin1Char('/'));
    }
    return cleanPath == cleanDirectory.left(cleanDirectory.size() - 1)
        || cleanPath.startsWith(cleanDirectory);
}

void appendContentUri(const QJsonObject& object, QStringList& uris)
{
    const QJsonValue uriValue = object.value(QStringLiteral("uri"));
    if (uriValue.isString())
    {
        uris.push_back(uriValue.toString());
    }
    const QJsonValue urlValue = object.value(QStringLiteral("url"));
    if (urlValue.isString())
    {
        uris.push_back(urlValue.toString());
    }
}

void collectTileContentUris(const QJsonObject& tile,
                            int depth,
                            int& tileCount,
                            bool& traversalLimitExceeded,
                            QStringList& uris)
{
    if (depth > kMaximumTileDepth || tileCount >= kMaximumTileCount)
    {
        traversalLimitExceeded = true;
        return;
    }
    ++tileCount;

    const QJsonValue contentValue = tile.value(QStringLiteral("content"));
    if (contentValue.isObject())
    {
        appendContentUri(contentValue.toObject(), uris);
    }

    const QJsonValue contentsValue = tile.value(QStringLiteral("contents"));
    if (contentsValue.isArray())
    {
        const QJsonArray contents = contentsValue.toArray();
        for (const QJsonValue& value : contents)
        {
            if (value.isObject())
            {
                appendContentUri(value.toObject(), uris);
            }
        }
    }

    const QJsonValue childrenValue = tile.value(QStringLiteral("children"));
    if (childrenValue.isArray())
    {
        const QJsonArray children = childrenValue.toArray();
        for (const QJsonValue& child : children)
        {
            if (child.isObject())
            {
                collectTileContentUris(child.toObject(),
                                       depth + 1,
                                       tileCount,
                                       traversalLimitExceeded,
                                       uris);
            }
        }
    }
}

void collectLocal3DTilesDiagnostics(MapDataDiagnostics& diagnostics)
{
    if (!isFile(diagnostics.local3DTilesTilesetPath))
    {
        return;
    }

    QFile file(diagnostics.local3DTilesTilesetPath);
    if (QFileInfo(file).size() > kMaximumTilesetJsonBytes)
    {
        const QString message = QStringLiteral("原生 OSG 建筑瓦片集超过 64 MiB 安全上限。");
        diagnostics.local3DTilesDiagnostics.push_back(message);
        diagnostics.warnings.push_back(message);
        return;
    }
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        const QString message = QStringLiteral("无法打开原生 OSG 建筑瓦片集：%1").arg(file.errorString());
        diagnostics.local3DTilesDiagnostics.push_back(message);
        diagnostics.warnings.push_back(message);
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        const QString message = QStringLiteral("原生 OSG 建筑瓦片集不是有效 JSON：%1").arg(parseError.errorString());
        diagnostics.local3DTilesDiagnostics.push_back(message);
        diagnostics.warnings.push_back(message);
        return;
    }

    const QJsonObject tileset = document.object();
    const QString payloadFormat = tileset.value(QStringLiteral("extras")).toObject()
                                      .value(QStringLiteral("format")).toString();
    const bool hasNativePayloadFormat =
        payloadFormat == QStringLiteral("vaporview-osg-native-building-tiles");
    const QJsonValue assetValue = tileset.value(QStringLiteral("asset"));
    const bool hasAsset = assetValue.isObject();
    const bool hasAssetVersion = hasAsset
        && !assetValue.toObject().value(QStringLiteral("version")).toString().trimmed().isEmpty();
    const QJsonValue rootValue = tileset.value(QStringLiteral("root"));
    const bool hasRoot = rootValue.isObject();
    const QJsonObject rootObject = hasRoot ? rootValue.toObject() : QJsonObject{};
    const bool hasBoundingVolume = hasRoot && rootObject.value(QStringLiteral("boundingVolume")).isObject();
    const bool hasRootGeometricError = hasRoot && rootObject.value(QStringLiteral("geometricError")).isDouble();
    const bool hasTilesetGeometricError = tileset.value(QStringLiteral("geometricError")).isDouble();
    const bool hasGeometricError = hasRootGeometricError || hasTilesetGeometricError;

    auto addIssue = [&diagnostics](const QString& message) {
        diagnostics.local3DTilesDiagnostics.push_back(message);
        diagnostics.warnings.push_back(message);
    };

    if (!hasAsset)
    {
        addIssue(QStringLiteral("原生 OSG 建筑瓦片集缺少 asset 对象。"));
    }
    else if (!hasAssetVersion)
    {
        addIssue(QStringLiteral("原生 OSG 建筑瓦片集缺少 asset.version。"));
    }
    if (!hasNativePayloadFormat)
    {
        addIssue(QStringLiteral("本地建筑瓦片集 extras.format 必须为 vaporview-osg-native-building-tiles；此加载器不支持通用 Cesium 3D Tiles 数据块。"));
    }
    if (!hasRoot)
    {
        addIssue(QStringLiteral("原生 OSG 建筑瓦片集缺少根瓦片。"));
    }
    if (hasRoot && !hasBoundingVolume)
    {
        addIssue(QStringLiteral("原生 OSG 建筑根瓦片缺少 boundingVolume。"));
    }
    if (!hasGeometricError)
    {
        addIssue(QStringLiteral("原生 OSG 建筑瓦片集或根瓦片缺少 geometricError。"));
    }

    QStringList uris;
    int tileCount = 0;
    bool traversalLimitExceeded = false;
    if (hasRoot)
    {
        collectTileContentUris(rootObject, 0, tileCount, traversalLimitExceeded, uris);
    }
    if (traversalLimitExceeded)
    {
        addIssue(QStringLiteral("原生 OSG 建筑瓦片集超过遍历安全上限。"));
    }
    uris.removeDuplicates();
    diagnostics.local3DTilesResourceUris = uris;
    diagnostics.local3DTilesResourceCount = uris.size();
    if (uris.isEmpty())
    {
        addIssue(QStringLiteral("原生 OSG 建筑瓦片集没有 content.uri 条目。"));
    }

    const QFileInfo tilesetInfo(diagnostics.local3DTilesTilesetPath);
    const QString datasetRoot = QDir::cleanPath(tilesetInfo.absolutePath());
    for (const QString& rawUri : uris)
    {
        const QString uri = rawUri.trimmed();
        const QString resourcePath = stripUriQueryAndFragment(uri);
        if (resourcePath.isEmpty())
        {
            addIssue(QStringLiteral("原生 OSG 建筑瓦片集包含空资源 URI。"));
            continue;
        }
        if (hasUriSchemeOrNetworkPath(resourcePath) || QDir::isAbsolutePath(resourcePath))
        {
            diagnostics.local3DTilesExternalUris.push_back(rawUri);
            continue;
        }

        const QString absoluteResource = QDir::cleanPath(QDir(datasetRoot).absoluteFilePath(resourcePath));
        if (!pathStartsWithDirectory(absoluteResource, datasetRoot))
        {
            diagnostics.local3DTilesExternalUris.push_back(rawUri);
            continue;
        }
        if (isFile(absoluteResource))
        {
            diagnostics.foundFiles.push_back(absoluteResource);
        }
        else
        {
            diagnostics.local3DTilesMissingResources.push_back(absoluteResource);
        }
    }

    diagnostics.local3DTilesExternalUris.removeDuplicates();
    diagnostics.local3DTilesMissingResources.removeDuplicates();
    diagnostics.local3DTilesHasExternalUris = !diagnostics.local3DTilesExternalUris.isEmpty();

    for (const QString& uri : diagnostics.local3DTilesExternalUris)
    {
        addIssue(QStringLiteral("原生 OSG 建筑瓦片资源 URI 不是可移植本地路径：%1").arg(uri));
    }
    for (const QString& path : diagnostics.local3DTilesMissingResources)
    {
        addIssue(QStringLiteral("原生 OSG 建筑瓦片引用的资源缺失：%1").arg(path));
    }

    diagnostics.local3DTilesTilesetValid = hasAsset
        && hasAssetVersion
        && hasNativePayloadFormat
        && hasRoot
        && hasBoundingVolume
        && hasGeometricError
        && !traversalLimitExceeded
        && !uris.isEmpty()
        && diagnostics.local3DTilesExternalUris.isEmpty()
        && diagnostics.local3DTilesMissingResources.isEmpty();
    diagnostics.local3DTilesDiagnostics.push_back(
        diagnostics.local3DTilesTilesetValid
            ? QStringLiteral("本地 OSG 建筑瓦片集通过原生仅本地资源格式检查。")
            : QStringLiteral("本地 OSG 建筑瓦片集在接入渲染前需要检查。"));
}

QString bestAvailableDemSource(const MapDataDiagnostics& diagnostics)
{
    if (diagnostics.copernicusDemAvailable)
    {
        return QStringLiteral("Copernicus DEM GLO-30");
    }
    if (diagnostics.srtmDemAvailable)
    {
        return QStringLiteral("SRTM");
    }
    return {};
}

void collectOsmDiagnostics(MapDataDiagnostics& diagnostics)
{
    diagnostics.osmRoadsAvailable = isFile(diagnostics.osmRoadsPath);
    diagnostics.osmWaterAvailable = isFile(diagnostics.osmWaterPath);
    diagnostics.osmBuildingsAvailable = isFile(diagnostics.osmBuildingsPath);
    diagnostics.osmPlacesAvailable = isFile(diagnostics.osmPlacesPath);

    if (!diagnostics.osmRoadsAvailable)
    {
        diagnostics.missingOsmFiles.push_back(diagnostics.osmRoadsPath);
    }
    if (!diagnostics.osmWaterAvailable)
    {
        diagnostics.missingOsmFiles.push_back(diagnostics.osmWaterPath);
    }
    if (!diagnostics.osmBuildingsAvailable)
    {
        diagnostics.missingOsmFiles.push_back(diagnostics.osmBuildingsPath);
    }
    if (!diagnostics.osmPlacesAvailable)
    {
        diagnostics.missingOsmFiles.push_back(diagnostics.osmPlacesPath);
    }
}

void collectOsmLayerContracts(MapDataDiagnostics& diagnostics)
{
    diagnostics.osmLayerContracts = {
        QStringLiteral("%1 -> layer roads -> OGRFeatures osm-roads -> FeatureImage OSM roads")
            .arg(diagnostics.osmRoadsPath),
        QStringLiteral("%1 -> layer water -> OGRFeatures osm-water -> FeatureImage OSM water fill")
            .arg(diagnostics.osmWaterPath),
        QStringLiteral("%1 -> layer buildings -> 仅生成数据；默认安全本地地图模板不渲染此图层")
            .arg(diagnostics.osmBuildingsPath),
        QStringLiteral("%1 -> layer places -> 仅生成数据；默认安全本地地图模板不渲染此图层")
            .arg(diagnostics.osmPlacesPath)
    };
}

void collectFullLocalBlockers(MapDataDiagnostics& diagnostics,
                              const QString& fullLocalEarthPath,
                              const QString& fullLocalSrtmEarthPath)
{
    if (!diagnostics.naturalEarthAvailable)
    {
        diagnostics.fullLocalBlockers.push_back(QStringLiteral("Natural Earth 的 VRT 或栅格数据不完整。"));
    }
    if (!diagnostics.copernicusDemAvailable && !diagnostics.srtmDemAvailable)
    {
        diagnostics.fullLocalBlockers.push_back(QStringLiteral("没有可用的 Copernicus DEM 或 SRTM VRT。"));
    }
    if (diagnostics.copernicusDemAvailable && !isFile(fullLocalEarthPath))
    {
        diagnostics.fullLocalBlockers.push_back(QStringLiteral("缺少 Copernicus 完整本地地图模板。"));
    }
    if (!diagnostics.copernicusDemAvailable && diagnostics.srtmDemAvailable && !isFile(fullLocalSrtmEarthPath))
    {
        diagnostics.fullLocalBlockers.push_back(QStringLiteral("缺少 SRTM 完整本地地图模板。"));
    }
    for (const QString& path : diagnostics.missingOsmFiles)
    {
        diagnostics.fullLocalBlockers.push_back(QStringLiteral("缺少 OSM GeoPackage：%1").arg(path));
    }
}

void setEarthFile(MapDataSelection& selection, const QString& path)
{
    const QString absolute = QFileInfo(path).absoluteFilePath();
    selection.earthFile = absolute;
    selection.earthFilePath = absolute;
    selection.diagnostics.earthFilePath = absolute;
}

void finalizeSelection(MapDataSelection& selection)
{
    MapDataDiagnostics& diagnostics = selection.diagnostics;
    diagnostics.baseMapPriority =
        QStringLiteral("本地实景三维 > Copernicus DEM > SRTM > Natural Earth > 本地网格");

    if (selection.mode == MapDataMode::FullLocalMap)
    {
        diagnostics.selectedBaseMode =
            diagnostics.selectedElevationSource == QStringLiteral("SRTM")
                ? MapDataMode::NaturalEarthWithSrtm
                : MapDataMode::NaturalEarthWithCopernicusDem;
    }
    else
    {
        diagnostics.selectedBaseMode = selection.mode;
    }
    diagnostics.localGridFallbackAvailable = true;
    diagnostics.localGridFallbackActive = selection.mode == MapDataMode::LocalGridOnly;
    diagnostics.selectedBaseModeLabel = MapDataManager::modeLabel(diagnostics.selectedBaseMode);
    diagnostics.selectedBaseModeKey = MapDataManager::modeKey(diagnostics.selectedBaseMode);
    diagnostics.selectedBaseEarthFilePath =
        selection.earthFilePath.isEmpty() ? selection.earthFile : selection.earthFilePath;

    const auto ready = [](const bool value) {
        return value ? QStringLiteral("就绪") : QStringLiteral("缺失");
    };

    diagnostics.readinessChecks = {
        QStringLiteral("Natural Earth 底图：%1").arg(ready(diagnostics.naturalEarthAvailable)),
        QStringLiteral("DEM 地形：%1")
            .arg(diagnostics.selectedDemLayerAvailable
                     ? diagnostics.selectedElevationSource
                     : QStringLiteral("缺失")),
        QStringLiteral("OSM 矢量文件：%1（%2/4）；安全渲染图层：%3")
            .arg(diagnostics.selectedOsmLayersAvailable ? QStringLiteral("就绪") : QStringLiteral("缺失"))
            .arg(diagnostics.osmLayerCount)
            .arg(diagnostics.selectedOsmLayersAvailable ? QStringLiteral("水系、道路") : QStringLiteral("无")),
        QStringLiteral("可选影像叠加层：%1（%2/3）")
            .arg(diagnostics.localImageryMenuAvailable ? QStringLiteral("可从菜单加载") : QStringLiteral("菜单未就绪"))
            .arg(diagnostics.localImageryMenuEntryCount),
        QStringLiteral("可选原生 OSG 建筑瓦片：%1")
            .arg(diagnostics.local3DTilesAvailable
                     ? (diagnostics.local3DTilesTilesetValid ? QStringLiteral("格式有效") : QStringLiteral("需要检查"))
                     : QStringLiteral("未配置")),
        QStringLiteral("本地实景三维地图：%1")
            .arg(diagnostics.real3DLocalReady ? QStringLiteral("就绪") : QStringLiteral("未就绪"))
    };

    diagnostics.readinessNextSteps.clear();
    switch (selection.mode)
    {
    case MapDataMode::FullLocalMap:
        if (diagnostics.real3DLocalReady)
        {
            diagnostics.readinessSummary =
                QStringLiteral("杭州西湖实景三维已就绪：已选择 Sentinel-2 影像、%1 高程、OSM 地理要素和本地建筑瓦片。")
                    .arg(diagnostics.selectedElevationSource);
        }
        else
        {
            diagnostics.readinessSummary =
                QStringLiteral("完整离线地图已就绪：已选择 Natural Earth、%1 高程及安全的 OSM 水系和道路图层。")
                    .arg(diagnostics.selectedElevationSource);
            diagnostics.readinessNextSteps.push_back(
                QStringLiteral("OSM 建筑和地名已准备好供诊断使用，但不会自动渲染，以免全国范围的标签和建筑导致地图卡顿。"));
        }
        if (!diagnostics.localImageryAvailable)
        {
            diagnostics.readinessNextSteps.push_back(
                QStringLiteral("可选：准备 Sentinel-2、Landsat 或 OpenAerialMap GeoTIFF VRT，用于高分辨率影像叠加。"));
        }
        if (!diagnostics.local3DTilesAvailable)
        {
            diagnostics.readinessNextSteps.push_back(
                QStringLiteral("可选：将 VaporView 原生 OSG 建筑瓦片集放入 resources/maps/tiles3d/local/，用于预览诊断。"));
        }
        break;
    case MapDataMode::NaturalEarthWithCopernicusDem:
    case MapDataMode::NaturalEarthWithSrtm:
        diagnostics.readinessSummary =
            QStringLiteral("带地形的离线地图已就绪：已选择 Natural Earth 和 %1 高程；OSM 矢量数据不完整。")
                .arg(diagnostics.selectedElevationSource);
        diagnostics.readinessNextSteps.push_back(
            QStringLiteral("使用 scripts/prepare-osm-local-data.py 生成全部四个本地 OSM GeoPackage，以启用完整本地地图。"));
        diagnostics.readinessNextSteps.push_back(
            QStringLiteral("命令：python scripts/prepare-osm-local-data.py resources/maps/osm/local_extract.osm.pbf --overwrite"));
        diagnostics.readinessNextSteps.push_back(
            QStringLiteral("验证：python scripts/prepare-osm-local-data.py resources/maps/osm/local_extract.osm.pbf --check"));
        if (!diagnostics.missingOsmFiles.isEmpty())
        {
            diagnostics.readinessNextSteps.push_back(
                QStringLiteral("缺少 OSM 文件：%1").arg(diagnostics.missingOsmFiles.join(QStringLiteral("; "))));
        }
        break;
    case MapDataMode::NaturalEarth:
        diagnostics.readinessSummary =
            QStringLiteral("仅离线视觉底图就绪：已选择 Natural Earth，但没有真实 DEM 地形。");
        diagnostics.readinessNextSteps.push_back(
            QStringLiteral("将 Copernicus DEM GLO-30 GeoTIFF 瓦片放入 resources/maps/terrain/copernicus_dem_glo30/，然后运行 scripts/prepare-demo-dem.py。"));
        diagnostics.readinessNextSteps.push_back(
            QStringLiteral("命令：python scripts/prepare-demo-dem.py"));
        diagnostics.readinessNextSteps.push_back(
            QStringLiteral("Copernicus DEM 不可用时，可使用 resources/maps/terrain/srtm/ 中的 SRTM 作为备用。"));
        diagnostics.readinessNextSteps.push_back(
            QStringLiteral("SRTM 备用命令：python scripts/prepare-demo-dem.py --srtm"));
        break;
    case MapDataMode::LocalGridOnly:
        diagnostics.readinessSummary =
            QStringLiteral("仅能回退到本地网格：没有完整的离线 Natural Earth 数据集。");
        diagnostics.readinessNextSteps.push_back(
            QStringLiteral("打开“地图资源”读取 HTTP 清单并下载 Natural Earth 底图。"));
        diagnostics.readinessNextSteps.push_back(
            QStringLiteral("开发或离线准备可使用 scripts/download-natural-earth-map.ps1。"));
        diagnostics.readinessNextSteps.push_back(
            QStringLiteral("命令：powershell -ExecutionPolicy Bypass -File scripts/download-natural-earth-map.ps1"));
        diagnostics.readinessNextSteps.push_back(
            QStringLiteral("然后添加 Copernicus DEM 或 SRTM VRT，以提供真实地形高程。"));
        diagnostics.readinessNextSteps.push_back(
            QStringLiteral("DEM 命令：python scripts/prepare-demo-dem.py"));
        break;
    }

    if (diagnostics.readinessNextSteps.isEmpty())
    {
        diagnostics.readinessNextSteps.push_back(
            QStringLiteral("所选模式所需地图数据已就绪，没有阻塞项。"));
    }

    selection.foundFiles = selection.diagnostics.foundFiles;
    selection.missingFiles = selection.diagnostics.missingFiles;
    selection.warnings = selection.diagnostics.warnings;
}

int baseModePriority(MapDataMode mode)
{
    switch (mode)
    {
    case MapDataMode::FullLocalMap:
        return 0;
    case MapDataMode::NaturalEarthWithCopernicusDem:
        return 30;
    case MapDataMode::NaturalEarthWithSrtm:
        return 20;
    case MapDataMode::NaturalEarth:
        return 10;
    case MapDataMode::LocalGridOnly:
        return 0;
    }
    return 0;
}

int selectionPriority(const MapDataSelection& selection)
{
    int priority = baseModePriority(selection.diagnostics.selectedBaseMode);
    if (selection.mode == MapDataMode::FullLocalMap)
    {
        ++priority;
    }
    if (selection.diagnostics.real3DLocalReady)
    {
        priority += 100;
    }
    return priority;
}

} // namespace

MapDataManager::MapDataManager() = default;

MapDataManager::MapDataManager(QStringList candidateRoots)
    : candidate_roots_(std::move(candidateRoots))
{
}

bool MapDataSelection::hasEarthFile() const
{
    return !earthFilePath.isEmpty() || !earthFile.isEmpty();
}

MapDataSelection MapDataManager::selectBestAvailableMap(MapDataScanMode scanMode) const
{
    MapDataSelection best;
    bool haveSelection = false;

    for (const QString& root : candidateRoots())
    {
        const MapDataSelection selection = evaluateRoot(root, scanMode);
        if (!haveSelection || selectionPriority(selection) > selectionPriority(best))
        {
            best = selection;
            haveSelection = true;
            if (best.diagnostics.real3DLocalReady)
            {
                break;
            }
        }
    }

    if (!haveSelection)
    {
        best.diagnostics.messages.push_back(QStringLiteral("未找到可用地图根目录；仅使用本地网格。"));
    }
    return best;
}

bool MapDataManager::isBuiltInEarthFile(const QString& earthPath) const
{
    const QString fileName = QFileInfo(earthPath).fileName();
    return fileName == QStringLiteral("vaporview_default.earth")
        || fileName == QStringLiteral("vaporview_with_dem.earth")
        || fileName == QStringLiteral("vaporview_with_srtm.earth")
        || fileName == QStringLiteral("vaporview_full_local.earth")
        || fileName == QStringLiteral("vaporview_full_local_srtm.earth")
        || fileName == QStringLiteral("vaporview_real3d_local.earth");
}

QString MapDataManager::modeLabel(MapDataMode mode)
{
    switch (mode)
    {
    case MapDataMode::FullLocalMap:
        return QStringLiteral("完整本地地图");
    case MapDataMode::NaturalEarthWithCopernicusDem:
        return QStringLiteral("Natural Earth + Copernicus DEM");
    case MapDataMode::NaturalEarthWithSrtm:
        return QStringLiteral("Natural Earth + SRTM DEM");
    case MapDataMode::NaturalEarth:
        return QStringLiteral("Natural Earth");
    case MapDataMode::LocalGridOnly:
        return QStringLiteral("仅本地网格");
    }
    return QStringLiteral("Unknown");
}

QString MapDataManager::modeKey(MapDataMode mode)
{
    switch (mode)
    {
    case MapDataMode::FullLocalMap:
        return QStringLiteral("FullLocalMap");
    case MapDataMode::NaturalEarthWithCopernicusDem:
        return QStringLiteral("NaturalEarthWithCopernicusDem");
    case MapDataMode::NaturalEarthWithSrtm:
        return QStringLiteral("NaturalEarthWithSrtm");
    case MapDataMode::NaturalEarth:
        return QStringLiteral("NaturalEarth");
    case MapDataMode::LocalGridOnly:
        return QStringLiteral("LocalGridOnly");
    }
    return QStringLiteral("Unknown");
}

QStringList MapDataManager::candidateRoots() const
{
    if (!candidate_roots_.isEmpty())
    {
        return candidate_roots_;
    }

    const QString appDir = QCoreApplication::applicationDirPath();
    QStringList roots{
        appDir,
        QDir(appDir).absoluteFilePath(QStringLiteral("../.."))
    };
    const QString downloadedMapRoot = map3DDownloadedMapRoot();
    if (!downloadedMapRoot.isEmpty())
    {
        roots.push_back(downloadedMapRoot);
    }
    if (qEnvironmentVariableIsSet("VAPORVIEW_MAP3D_DEV_SEARCH_PATHS"))
    {
        roots.push_back(QDir::currentPath());
    }
    roots.removeDuplicates();
    return roots;
}

MapDataSelection MapDataManager::evaluateRoot(const QString& root, MapDataScanMode scanMode) const
{
    MapDataSelection selection;
    MapDataDiagnostics& diagnostics = selection.diagnostics;
    diagnostics.currentWorkingDirectory = QDir::currentPath();
    diagnostics.projectRoot = QDir::cleanPath(QDir(root).absolutePath());
    diagnostics.mapsRoot = QDir::cleanPath(QDir(root).absoluteFilePath(QStringLiteral("resources/maps")));

    const QString defaultEarthPath = absolutePath(root, kDefaultEarthRelative);
    const QString copernicusEarthPath = absolutePath(root, kCopernicusEarthRelative);
    const QString srtmEarthPath = absolutePath(root, kSrtmEarthRelative);
    const QString fullLocalEarthPath = absolutePath(root, kFullLocalEarthRelative);
    const QString fullLocalSrtmEarthPath = absolutePath(root, kFullLocalSrtmEarthRelative);
    const QString real3DLocalEarthPath = absolutePath(root, kReal3DLocalEarthRelative);
    diagnostics.real3DLocalEarthPath = real3DLocalEarthPath;
    diagnostics.fullLocalEarthPath = fullLocalEarthPath;
    diagnostics.fullLocalSrtmEarthPath = fullLocalSrtmEarthPath;
    diagnostics.sentinel2ImageryEarthPath = absolutePath(root, kSentinel2ImageryEarthRelative);
    diagnostics.landsatImageryEarthPath = absolutePath(root, kLandsatImageryEarthRelative);
    diagnostics.openAerialMapImageryEarthPath = absolutePath(root, kOpenAerialMapImageryEarthRelative);
    diagnostics.naturalEarthTexturePath = absolutePath(root, kNaturalEarthTextureRelative);
    diagnostics.naturalEarthVrtPath = absolutePath(root, kNaturalEarthVrtRelative);
    diagnostics.naturalEarthRasterPath = absolutePath(root, kNaturalEarthRasterRelative);
    diagnostics.copernicusDemVrtPath = absolutePath(root, kCopernicusDemVrtRelative);
    diagnostics.srtmDemVrtPath = absolutePath(root, kSrtmDemVrtRelative);
    diagnostics.osmRoadsPath = absolutePath(root, kOsmRoadsRelative);
    diagnostics.osmWaterPath = absolutePath(root, kOsmWaterRelative);
    diagnostics.osmBuildingsPath = absolutePath(root, kOsmBuildingsRelative);
    diagnostics.osmPlacesPath = absolutePath(root, kOsmPlacesRelative);
    diagnostics.sentinel2ImageryVrtPath = absolutePath(root, kSentinel2ImageryVrtRelative);
    diagnostics.landsatImageryVrtPath = absolutePath(root, kLandsatImageryVrtRelative);
    diagnostics.openAerialMapImageryVrtPath = absolutePath(root, kOpenAerialMapImageryVrtRelative);
    diagnostics.local3DTilesTilesetPath = absolutePath(root, kLocal3DTilesTilesetRelative);

    const QStringList roots = candidateRoots();
    const QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    diagnostics.osgPluginPath = environment.value(QStringLiteral("OSG_LIBRARY_PATH"));
    diagnostics.osgLibraryPath = diagnostics.osgPluginPath;
    diagnostics.osgEarthNotifyLevel = environment.value(QStringLiteral("OSGEARTH_NOTIFY_LEVEL"));
    diagnostics.osgEarthEnvironment = osgEarthEnvironmentVariables(environment);
    if (diagnostics.osgPluginPath.isEmpty())
    {
        diagnostics.osgPluginPath = findOsgPluginDirectory(roots);
        diagnostics.osgLibraryPath = diagnostics.osgPluginPath;
    }
    diagnostics.gdalDataPath = environment.value(QStringLiteral("GDAL_DATA"));
    if (diagnostics.gdalDataPath.isEmpty())
    {
        diagnostics.gdalDataPath = firstExistingDirectory(roots, {
            QStringLiteral("share/gdal"),
            QStringLiteral(".local_deps/vcpkg_installed/x64-windows/share/gdal")
        });
    }
    diagnostics.projDataPath = environment.value(QStringLiteral("PROJ_DATA"));
    diagnostics.projLibPath = environment.value(QStringLiteral("PROJ_LIB"));
    const QString inferredProjPath = firstExistingDirectory(roots, {
        QStringLiteral("share/proj"),
        QStringLiteral("share/proj4"),
        QStringLiteral(".local_deps/vcpkg_installed/x64-windows/share/proj"),
        QStringLiteral(".local_deps/vcpkg_installed/x64-windows/share/proj4")
    });
    if (diagnostics.projDataPath.isEmpty())
    {
        diagnostics.projDataPath = inferredProjPath.isEmpty() ? diagnostics.projLibPath : inferredProjPath;
    }
    if (diagnostics.projLibPath.isEmpty())
    {
        diagnostics.projLibPath = inferredProjPath.isEmpty() ? diagnostics.projDataPath : inferredProjPath;
    }

    recordFile(diagnostics, defaultEarthPath);
    recordFile(diagnostics, fullLocalEarthPath);
    recordFile(diagnostics, fullLocalSrtmEarthPath);
    recordOptionalFile(diagnostics, real3DLocalEarthPath);
    recordOptionalFile(diagnostics, diagnostics.sentinel2ImageryEarthPath);
    recordOptionalFile(diagnostics, diagnostics.landsatImageryEarthPath);
    recordOptionalFile(diagnostics, diagnostics.openAerialMapImageryEarthPath);
    recordFile(diagnostics, diagnostics.naturalEarthTexturePath);
    recordFile(diagnostics, diagnostics.naturalEarthVrtPath);
    recordFile(diagnostics, diagnostics.naturalEarthRasterPath);
    recordFile(diagnostics, copernicusEarthPath);
    recordFile(diagnostics, diagnostics.copernicusDemVrtPath);
    recordFile(diagnostics, srtmEarthPath);
    recordFile(diagnostics, diagnostics.srtmDemVrtPath);
    recordFile(diagnostics, diagnostics.osmRoadsPath);
    recordFile(diagnostics, diagnostics.osmWaterPath);
    recordFile(diagnostics, diagnostics.osmBuildingsPath);
    recordFile(diagnostics, diagnostics.osmPlacesPath);
    recordOptionalFile(diagnostics, diagnostics.sentinel2ImageryVrtPath);
    recordOptionalFile(diagnostics, diagnostics.landsatImageryVrtPath);
    recordOptionalFile(diagnostics, diagnostics.openAerialMapImageryVrtPath);
    recordOptionalFile(diagnostics, diagnostics.local3DTilesTilesetPath);

    diagnostics.naturalEarthAvailable = hasNaturalEarth(diagnostics);
    diagnostics.copernicusDemAvailable = isFile(diagnostics.copernicusDemVrtPath);
    diagnostics.srtmDemAvailable = isFile(diagnostics.srtmDemVrtPath);
    collectOsmDiagnostics(diagnostics);
    collectOsmLayerContracts(diagnostics);
    diagnostics.osmLayerCount = osmLayerCount(diagnostics);
    diagnostics.osmVectorAvailable = diagnostics.osmLayerCount == 4;
    diagnostics.localImageryLayerCount = localImageryLayerCount(diagnostics);
    diagnostics.localImageryAvailable = diagnostics.localImageryLayerCount > 0;
    diagnostics.localImageryOptions = localImageryOptions(diagnostics);
    diagnostics.localImageryMenuEntryCount = localImageryMenuEntryCount(diagnostics.localImageryOptions);
    diagnostics.localImageryMenuAvailable = diagnostics.localImageryMenuEntryCount > 0;
    diagnostics.local3DTilesAvailable = isFile(diagnostics.local3DTilesTilesetPath);
    if (scanMode == MapDataScanMode::Full)
    {
        collectLocal3DTilesDiagnostics(diagnostics);
    }
    else if (diagnostics.local3DTilesAvailable)
    {
        diagnostics.local3DTilesDiagnostics.push_back(
            QStringLiteral("为加快三维地图启动，原生 OSG 建筑瓦片格式校验已延后。"));
        diagnostics.messages.push_back(
            QStringLiteral("已检测到可选原生 OSG 建筑瓦片集；主动加载渲染或资源时将执行格式校验。"));
    }
    diagnostics.real3DLocalReady = isFile(real3DLocalEarthPath)
        && diagnostics.naturalEarthAvailable
        && diagnostics.copernicusDemAvailable
        && hasCompleteOsmSet(diagnostics)
        && isFile(diagnostics.sentinel2ImageryVrtPath)
        && diagnostics.local3DTilesTilesetValid;
    collectFullLocalBlockers(diagnostics, fullLocalEarthPath, fullLocalSrtmEarthPath);

    if (diagnostics.localImageryAvailable)
    {
        diagnostics.messages.push_back(
            diagnostics.localImageryMenuAvailable
                ? QStringLiteral("已检测到可选本地高分辨率影像 VRT；可使用本地影像菜单加载已就绪的叠加层。")
                : QStringLiteral("已检测到本地高分辨率影像 VRT，但缺少菜单所需的匹配地图场景模板。"));
    }
    if (diagnostics.local3DTilesAvailable)
    {
        diagnostics.messages.push_back(
            diagnostics.local3DTilesTilesetValid
                ? QStringLiteral("已检测到原生 OSG 建筑瓦片集，并通过仅本地资源格式检查。")
                : QStringLiteral("已检测到原生 OSG 建筑瓦片集，但接入渲染前仍需检查。"));
    }

    const QString selectedFullLocalEarthPath = fullLocalEarthForAvailableDem(
        fullLocalEarthPath,
        fullLocalSrtmEarthPath,
        diagnostics);

    if (diagnostics.real3DLocalReady)
    {
        selection.mode = MapDataMode::FullLocalMap;
        selection.description =
            QStringLiteral("杭州西湖 Sentinel-2 影像、Copernicus DEM、OSM 地理要素及本地三维建筑瓦片。");
        setEarthFile(selection, real3DLocalEarthPath);
        diagnostics.selectedDemLayerAvailable = true;
        diagnostics.selectedOsmLayersAvailable = true;
        diagnostics.selectedElevationSource = QStringLiteral("Copernicus DEM GLO-30");
        diagnostics.selectedFullLocalEarthPath = real3DLocalEarthPath;
        diagnostics.selectedOsmLayerCount = 2;
        diagnostics.messages.push_back(
            QStringLiteral("已选择杭州西湖实景三维本地地图，包含 Sentinel-2 影像和本地建筑瓦片。"));
        finalizeSelection(selection);
        return selection;
    }

    if (!selectedFullLocalEarthPath.isEmpty()
        && diagnostics.naturalEarthAvailable
        && hasAnyDem(diagnostics)
        && diagnostics.osmVectorAvailable)
    {
        selection.mode = MapDataMode::FullLocalMap;
        selection.description = QStringLiteral("Natural Earth 底图、本地 DEM 及本地 OSM 矢量 GeoPackage。");
        setEarthFile(selection, selectedFullLocalEarthPath);
        diagnostics.selectedDemLayerAvailable = true;
        diagnostics.selectedOsmLayersAvailable = true;
        diagnostics.selectedElevationSource = bestAvailableDemSource(diagnostics);
        diagnostics.selectedFullLocalEarthPath = selectedFullLocalEarthPath;
        diagnostics.selectedOsmLayerCount = 2;
        diagnostics.messages.push_back(
            QStringLiteral("已选择完整本地地图，包含安全的离线 OSM 水系和道路图层及 %1 高程。")
                .arg(diagnostics.selectedElevationSource));
        diagnostics.messages.push_back(
            QStringLiteral("OSM 建筑和地名标签已生成为 GeoPackage；默认不渲染，以避免中文字形缺失和缩放卡顿。"));
        finalizeSelection(selection);
        return selection;
    }

    if (isFile(copernicusEarthPath)
        && diagnostics.naturalEarthAvailable
        && diagnostics.copernicusDemAvailable)
    {
        selection.mode = MapDataMode::NaturalEarthWithCopernicusDem;
        selection.description = QStringLiteral("Natural Earth 底图及本地 Copernicus DEM 高程。");
        setEarthFile(selection, copernicusEarthPath);
        diagnostics.selectedDemLayerAvailable = true;
        diagnostics.selectedElevationSource = QStringLiteral("Copernicus DEM GLO-30");
        diagnostics.messages.push_back(QStringLiteral("已选择 Copernicus DEM GLO-30 本地高程。"));
        if (isFile(fullLocalEarthPath) && hasCompleteOsmSet(diagnostics))
        {
            diagnostics.messages.push_back(QStringLiteral("完整本地 OSM 数据可用，但自动选择优先使用 DEM 模板。"));
        }
        else if (isFile(fullLocalEarthPath) && !hasCompleteOsmSet(diagnostics))
        {
            diagnostics.warnings.push_back(QStringLiteral("完整本地地图模板存在，但缺少一个或多个 OSM GeoPackage。"));
        }
        finalizeSelection(selection);
        return selection;
    }

    if (isFile(srtmEarthPath)
        && diagnostics.naturalEarthAvailable
        && diagnostics.srtmDemAvailable)
    {
        selection.mode = MapDataMode::NaturalEarthWithSrtm;
        selection.description = QStringLiteral("Natural Earth 底图及本地 SRTM 高程。");
        setEarthFile(selection, srtmEarthPath);
        diagnostics.selectedDemLayerAvailable = true;
        diagnostics.selectedElevationSource = QStringLiteral("SRTM");
        diagnostics.messages.push_back(QStringLiteral("已选择 SRTM 本地高程作为备用。"));
        finalizeSelection(selection);
        return selection;
    }

    if (isFile(defaultEarthPath) && diagnostics.naturalEarthAvailable)
    {
        selection.mode = MapDataMode::NaturalEarth;
        selection.description = QStringLiteral("不含地形高程的 Natural Earth 离线视觉底图。");
        setEarthFile(selection, defaultEarthPath);
        diagnostics.messages.push_back(QStringLiteral("已选择 Natural Earth 离线底图。"));
        diagnostics.warnings.push_back(QStringLiteral("Natural Earth 仅提供影像；没有可用的真实 DEM 地形。"));
        finalizeSelection(selection);
        return selection;
    }

    selection.mode = MapDataMode::LocalGridOnly;
    selection.description = QStringLiteral("未找到完整本地地图数据；使用内置本地网格回退。");
    diagnostics.messages.push_back(QStringLiteral("此根目录下未找到完整离线地图数据。"));
    finalizeSelection(selection);
    return selection;
}

} // namespace VaporView::Map3D
