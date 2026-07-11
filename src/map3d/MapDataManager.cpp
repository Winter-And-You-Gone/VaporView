#include "map3d/MapDataManager.h"

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
         QStringLiteral("Sentinel-2 local imagery"),
         diagnostics.sentinel2ImageryEarthPath,
         diagnostics.sentinel2ImageryVrtPath,
         isFile(diagnostics.sentinel2ImageryEarthPath) && isFile(diagnostics.sentinel2ImageryVrtPath)},
        {QStringLiteral("landsat"),
         QStringLiteral("Landsat local imagery"),
         diagnostics.landsatImageryEarthPath,
         diagnostics.landsatImageryVrtPath,
         isFile(diagnostics.landsatImageryEarthPath) && isFile(diagnostics.landsatImageryVrtPath)},
        {QStringLiteral("openaerialmap"),
         QStringLiteral("OpenAerialMap local imagery"),
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

bool pathStartsWithDirectory(const QString& path, const QString& directory)
{
    QString cleanPath = QDir::cleanPath(path).replace(QLatin1Char('\\'), QLatin1Char('/'));
    QString cleanDirectory = QDir::cleanPath(directory).replace(QLatin1Char('\\'), QLatin1Char('/'));
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

void collectTileContentUris(const QJsonObject& tile, QStringList& uris)
{
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
                collectTileContentUris(child.toObject(), uris);
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
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        const QString message = QStringLiteral("Local 3D Tiles tileset could not be opened: %1").arg(file.errorString());
        diagnostics.local3DTilesDiagnostics.push_back(message);
        diagnostics.warnings.push_back(message);
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        const QString message = QStringLiteral("Local 3D Tiles tileset is not valid JSON: %1").arg(parseError.errorString());
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
        addIssue(QStringLiteral("Local 3D Tiles tileset is missing asset object."));
    }
    else if (!hasAssetVersion)
    {
        addIssue(QStringLiteral("Local 3D Tiles tileset asset.version is missing."));
    }
    if (!hasNativePayloadFormat)
    {
        addIssue(QStringLiteral("Local building tileset extras.format must be vaporview-osg-native-building-tiles; generic Cesium 3D Tiles payloads are not supported by this loader."));
    }
    if (!hasRoot)
    {
        addIssue(QStringLiteral("Local 3D Tiles tileset is missing root tile."));
    }
    if (hasRoot && !hasBoundingVolume)
    {
        addIssue(QStringLiteral("Local 3D Tiles root tile is missing boundingVolume."));
    }
    if (!hasGeometricError)
    {
        addIssue(QStringLiteral("Local 3D Tiles tileset/root geometricError is missing."));
    }

    QStringList uris;
    if (hasRoot)
    {
        collectTileContentUris(rootObject, uris);
    }
    uris.removeDuplicates();
    diagnostics.local3DTilesResourceUris = uris;
    diagnostics.local3DTilesResourceCount = uris.size();
    if (uris.isEmpty())
    {
        addIssue(QStringLiteral("Local 3D Tiles tileset has no content.uri entries yet."));
    }

    const QFileInfo tilesetInfo(diagnostics.local3DTilesTilesetPath);
    const QString datasetRoot = QDir::cleanPath(tilesetInfo.absolutePath());
    for (const QString& rawUri : uris)
    {
        const QString uri = rawUri.trimmed();
        const QString resourcePath = stripUriQueryAndFragment(uri);
        if (resourcePath.isEmpty())
        {
            addIssue(QStringLiteral("Local 3D Tiles contains an empty content URI."));
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
        addIssue(QStringLiteral("Local 3D Tiles content URI is not local/portable: %1").arg(uri));
    }
    for (const QString& path : diagnostics.local3DTilesMissingResources)
    {
        addIssue(QStringLiteral("Local 3D Tiles referenced resource is missing: %1").arg(path));
    }

    diagnostics.local3DTilesTilesetValid = hasAsset
        && hasAssetVersion
        && hasNativePayloadFormat
        && hasRoot
        && hasBoundingVolume
        && hasGeometricError
        && !uris.isEmpty()
        && diagnostics.local3DTilesExternalUris.isEmpty()
        && diagnostics.local3DTilesMissingResources.isEmpty();
    diagnostics.local3DTilesDiagnostics.push_back(
        diagnostics.local3DTilesTilesetValid
            ? QStringLiteral("Local OSG building tileset passes the native local-only contract checks.")
            : QStringLiteral("Local OSG building tileset needs attention before renderer integration."));
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
        QStringLiteral("%1 -> layer buildings -> generated data only; not rendered by the safe default full-local earth template")
            .arg(diagnostics.osmBuildingsPath),
        QStringLiteral("%1 -> layer places -> generated data only; not rendered by the safe default full-local earth template")
            .arg(diagnostics.osmPlacesPath)
    };
}

void collectFullLocalBlockers(MapDataDiagnostics& diagnostics,
                              const QString& fullLocalEarthPath,
                              const QString& fullLocalSrtmEarthPath)
{
    if (!diagnostics.naturalEarthAvailable)
    {
        diagnostics.fullLocalBlockers.push_back(QStringLiteral("Natural Earth VRT/raster is incomplete."));
    }
    if (!diagnostics.copernicusDemAvailable && !diagnostics.srtmDemAvailable)
    {
        diagnostics.fullLocalBlockers.push_back(QStringLiteral("No Copernicus DEM or SRTM VRT is available."));
    }
    if (diagnostics.copernicusDemAvailable && !isFile(fullLocalEarthPath))
    {
        diagnostics.fullLocalBlockers.push_back(QStringLiteral("Copernicus full-local earth template is missing."));
    }
    if (!diagnostics.copernicusDemAvailable && diagnostics.srtmDemAvailable && !isFile(fullLocalSrtmEarthPath))
    {
        diagnostics.fullLocalBlockers.push_back(QStringLiteral("SRTM full-local earth template is missing."));
    }
    for (const QString& path : diagnostics.missingOsmFiles)
    {
        diagnostics.fullLocalBlockers.push_back(QStringLiteral("Missing OSM GeoPackage: %1").arg(path));
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
        QStringLiteral("Real 3D local > Copernicus DEM > SRTM > Natural Earth > Local grid");

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
        return value ? QStringLiteral("ready") : QStringLiteral("missing");
    };

    diagnostics.readinessChecks = {
        QStringLiteral("Natural Earth background: %1").arg(ready(diagnostics.naturalEarthAvailable)),
        QStringLiteral("Terrain DEM: %1")
            .arg(diagnostics.selectedDemLayerAvailable
                     ? diagnostics.selectedElevationSource
                     : QStringLiteral("missing")),
        QStringLiteral("OSM vector files: %1 (%2/4); safe rendered layers: %3")
            .arg(diagnostics.selectedOsmLayersAvailable ? QStringLiteral("ready") : QStringLiteral("missing"))
            .arg(diagnostics.osmLayerCount)
            .arg(diagnostics.selectedOsmLayersAvailable ? QStringLiteral("water, roads") : QStringLiteral("none")),
        QStringLiteral("Optional imagery overlays: %1 (%2/3)")
            .arg(diagnostics.localImageryMenuAvailable ? QStringLiteral("menu ready") : QStringLiteral("not menu ready"))
            .arg(diagnostics.localImageryMenuEntryCount),
        QStringLiteral("Optional local 3D Tiles: %1")
            .arg(diagnostics.local3DTilesAvailable
                     ? (diagnostics.local3DTilesTilesetValid ? QStringLiteral("contract valid") : QStringLiteral("needs attention"))
                     : QStringLiteral("not configured")),
        QStringLiteral("Real 3D local map: %1")
            .arg(diagnostics.real3DLocalReady ? QStringLiteral("ready") : QStringLiteral("not ready"))
    };

    diagnostics.readinessNextSteps.clear();
    switch (selection.mode)
    {
    case MapDataMode::FullLocalMap:
        if (diagnostics.real3DLocalReady)
        {
            diagnostics.readinessSummary =
                QStringLiteral("Ready for Hangzhou Xihu real 3D: Sentinel-2 imagery, %1 elevation, OSM context, and local building tiles are selected.")
                    .arg(diagnostics.selectedElevationSource);
        }
        else
        {
            diagnostics.readinessSummary =
                QStringLiteral("Ready for full offline local map: Natural Earth, %1 elevation, and safe OSM water/road context are selected.")
                    .arg(diagnostics.selectedElevationSource);
            diagnostics.readinessNextSteps.push_back(
                QStringLiteral("OSM buildings and places are prepared for diagnostics, but are not auto-rendered because full-country labels/buildings can stall the 3D map."));
        }
        if (!diagnostics.localImageryAvailable)
        {
            diagnostics.readinessNextSteps.push_back(
                QStringLiteral("Optional: prepare Sentinel-2, Landsat, or OpenAerialMap GeoTIFF VRTs for high-resolution imagery overlays."));
        }
        if (!diagnostics.local3DTilesAvailable)
        {
            diagnostics.readinessNextSteps.push_back(
                QStringLiteral("Optional: place a local 3D Tiles dataset under resources/maps/tiles3d/local/ for preview diagnostics."));
        }
        break;
    case MapDataMode::NaturalEarthWithCopernicusDem:
    case MapDataMode::NaturalEarthWithSrtm:
        diagnostics.readinessSummary =
            QStringLiteral("Ready for terrain-backed offline map: Natural Earth and %1 elevation are selected; OSM vectors are not complete.")
                .arg(diagnostics.selectedElevationSource);
        diagnostics.readinessNextSteps.push_back(
            QStringLiteral("Generate all four local OSM GeoPackages with scripts/prepare-osm-local-data.py to enable Full local map."));
        diagnostics.readinessNextSteps.push_back(
            QStringLiteral("Command: python scripts/prepare-osm-local-data.py resources/maps/osm/local_extract.osm.pbf --overwrite"));
        diagnostics.readinessNextSteps.push_back(
            QStringLiteral("Validate: python scripts/prepare-osm-local-data.py resources/maps/osm/local_extract.osm.pbf --check"));
        if (!diagnostics.missingOsmFiles.isEmpty())
        {
            diagnostics.readinessNextSteps.push_back(
                QStringLiteral("Missing OSM files: %1").arg(diagnostics.missingOsmFiles.join(QStringLiteral("; "))));
        }
        break;
    case MapDataMode::NaturalEarth:
        diagnostics.readinessSummary =
            QStringLiteral("Ready for offline visual background only: Natural Earth is selected, but no real DEM terrain is available.");
        diagnostics.readinessNextSteps.push_back(
            QStringLiteral("Place Copernicus DEM GLO-30 GeoTIFF tiles under resources/maps/terrain/copernicus_dem_glo30/ and run scripts/prepare-demo-dem.py."));
        diagnostics.readinessNextSteps.push_back(
            QStringLiteral("Command: python scripts/prepare-demo-dem.py"));
        diagnostics.readinessNextSteps.push_back(
            QStringLiteral("Use SRTM under resources/maps/terrain/srtm/ as a fallback when Copernicus DEM is unavailable."));
        diagnostics.readinessNextSteps.push_back(
            QStringLiteral("SRTM fallback command: python scripts/prepare-demo-dem.py --srtm"));
        break;
    case MapDataMode::LocalGridOnly:
        diagnostics.readinessSummary =
            QStringLiteral("Local grid fallback only: no complete offline Natural Earth dataset is available.");
        diagnostics.readinessNextSteps.push_back(
            QStringLiteral("Run scripts/download-natural-earth-map.ps1 to prepare the offline Natural Earth background."));
        diagnostics.readinessNextSteps.push_back(
            QStringLiteral("Command: powershell -ExecutionPolicy Bypass -File scripts/download-natural-earth-map.ps1"));
        diagnostics.readinessNextSteps.push_back(
            QStringLiteral("Then add Copernicus DEM or SRTM VRTs for real terrain elevation."));
        diagnostics.readinessNextSteps.push_back(
            QStringLiteral("DEM command: python scripts/prepare-demo-dem.py"));
        break;
    }

    if (diagnostics.readinessNextSteps.isEmpty())
    {
        diagnostics.readinessNextSteps.push_back(
            QStringLiteral("No required map-data blockers remain for the selected mode."));
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

MapDataSelection MapDataManager::selectBestAvailableMap() const
{
    MapDataSelection best;
    bool haveSelection = false;

    for (const QString& root : candidateRoots())
    {
        const MapDataSelection selection = evaluateRoot(root);
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
        best.diagnostics.messages.push_back(QStringLiteral("No usable map root found; using local grid only."));
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
        return QStringLiteral("Full local map");
    case MapDataMode::NaturalEarthWithCopernicusDem:
        return QStringLiteral("Natural Earth + Copernicus DEM");
    case MapDataMode::NaturalEarthWithSrtm:
        return QStringLiteral("Natural Earth + SRTM DEM");
    case MapDataMode::NaturalEarth:
        return QStringLiteral("Natural Earth");
    case MapDataMode::LocalGridOnly:
        return QStringLiteral("Local grid only");
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
    if (qEnvironmentVariableIsSet("VAPORVIEW_MAP3D_DEV_SEARCH_PATHS"))
    {
        roots.push_back(QDir::currentPath());
    }
    roots.removeDuplicates();
    return roots;
}

MapDataSelection MapDataManager::evaluateRoot(const QString& root) const
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
    collectLocal3DTilesDiagnostics(diagnostics);
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
                ? QStringLiteral("Optional local high-resolution imagery VRTs detected; use the local imagery toolbar menu to load menu-ready overlays.")
                : QStringLiteral("Optional local high-resolution imagery VRTs detected, but no matching imagery earth templates are available for the toolbar menu."));
    }
    if (diagnostics.local3DTilesAvailable)
    {
        diagnostics.messages.push_back(
            diagnostics.local3DTilesTilesetValid
                ? QStringLiteral("Optional local 3D Tiles tileset detected and passed local-only contract checks.")
                : QStringLiteral("Optional local 3D Tiles tileset detected but needs attention before renderer integration."));
    }

    const QString selectedFullLocalEarthPath = fullLocalEarthForAvailableDem(
        fullLocalEarthPath,
        fullLocalSrtmEarthPath,
        diagnostics);

    if (diagnostics.real3DLocalReady)
    {
        selection.mode = MapDataMode::FullLocalMap;
        selection.description =
            QStringLiteral("Hangzhou Xihu Sentinel-2 imagery, Copernicus DEM, OSM context, and local 3D building tiles.");
        setEarthFile(selection, real3DLocalEarthPath);
        diagnostics.selectedDemLayerAvailable = true;
        diagnostics.selectedOsmLayersAvailable = true;
        diagnostics.selectedElevationSource = QStringLiteral("Copernicus DEM GLO-30");
        diagnostics.selectedFullLocalEarthPath = real3DLocalEarthPath;
        diagnostics.selectedOsmLayerCount = 2;
        diagnostics.messages.push_back(
            QStringLiteral("Selected Hangzhou Xihu real-3D local map with Sentinel-2 imagery and local building tiles."));
        finalizeSelection(selection);
        return selection;
    }

    if (!selectedFullLocalEarthPath.isEmpty()
        && diagnostics.naturalEarthAvailable
        && hasAnyDem(diagnostics)
        && diagnostics.osmVectorAvailable)
    {
        selection.mode = MapDataMode::FullLocalMap;
        selection.description = QStringLiteral("Natural Earth background, local DEM, and local OSM vector GeoPackages.");
        setEarthFile(selection, selectedFullLocalEarthPath);
        diagnostics.selectedDemLayerAvailable = true;
        diagnostics.selectedOsmLayersAvailable = true;
        diagnostics.selectedElevationSource = bestAvailableDemSource(diagnostics);
        diagnostics.selectedFullLocalEarthPath = selectedFullLocalEarthPath;
        diagnostics.selectedOsmLayerCount = 2;
        diagnostics.messages.push_back(
            QStringLiteral("Selected full local map with safe offline OSM water/road layers and %1 elevation.")
                .arg(diagnostics.selectedElevationSource));
        diagnostics.messages.push_back(
            QStringLiteral("OSM buildings and place labels are available as generated GeoPackages but are not rendered by default to avoid CJK glyph boxes and zoom-time stalls."));
        finalizeSelection(selection);
        return selection;
    }

    if (isFile(copernicusEarthPath)
        && diagnostics.naturalEarthAvailable
        && diagnostics.copernicusDemAvailable)
    {
        selection.mode = MapDataMode::NaturalEarthWithCopernicusDem;
        selection.description = QStringLiteral("Natural Earth background with local Copernicus DEM elevation.");
        setEarthFile(selection, copernicusEarthPath);
        diagnostics.selectedDemLayerAvailable = true;
        diagnostics.selectedElevationSource = QStringLiteral("Copernicus DEM GLO-30");
        diagnostics.messages.push_back(QStringLiteral("Selected Copernicus DEM GLO-30 local elevation."));
        if (isFile(fullLocalEarthPath) && hasCompleteOsmSet(diagnostics))
        {
            diagnostics.messages.push_back(QStringLiteral("Full local OSM data is available but automatic selection prioritizes DEM templates."));
        }
        else if (isFile(fullLocalEarthPath) && !hasCompleteOsmSet(diagnostics))
        {
            diagnostics.warnings.push_back(QStringLiteral("Full local map template exists, but one or more OSM GeoPackages are missing."));
        }
        finalizeSelection(selection);
        return selection;
    }

    if (isFile(srtmEarthPath)
        && diagnostics.naturalEarthAvailable
        && diagnostics.srtmDemAvailable)
    {
        selection.mode = MapDataMode::NaturalEarthWithSrtm;
        selection.description = QStringLiteral("Natural Earth background with local SRTM elevation.");
        setEarthFile(selection, srtmEarthPath);
        diagnostics.selectedDemLayerAvailable = true;
        diagnostics.selectedElevationSource = QStringLiteral("SRTM");
        diagnostics.messages.push_back(QStringLiteral("Selected SRTM local elevation fallback."));
        finalizeSelection(selection);
        return selection;
    }

    if (isFile(defaultEarthPath) && diagnostics.naturalEarthAvailable)
    {
        selection.mode = MapDataMode::NaturalEarth;
        selection.description = QStringLiteral("Natural Earth offline visual background without terrain elevation.");
        setEarthFile(selection, defaultEarthPath);
        diagnostics.messages.push_back(QStringLiteral("Selected Natural Earth offline background."));
        diagnostics.warnings.push_back(QStringLiteral("Natural Earth is imagery only; no real DEM terrain is available."));
        finalizeSelection(selection);
        return selection;
    }

    selection.mode = MapDataMode::LocalGridOnly;
    selection.description = QStringLiteral("No complete local map set found; use the built-in local grid fallback.");
    diagnostics.messages.push_back(QStringLiteral("No complete offline map set found for this root."));
    finalizeSelection(selection);
    return selection;
}

} // namespace VaporView::Map3D
