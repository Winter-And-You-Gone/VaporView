#include "map3d/MapDataManager.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QFileInfo>
#include <QtCore/QProcessEnvironment>

#include <utility>

namespace VaporView::Map3D {
namespace {

constexpr auto kDefaultEarthRelative = "data/maps/vaporview_default.earth";
constexpr auto kCopernicusEarthRelative = "data/maps/vaporview_with_dem.earth";
constexpr auto kSrtmEarthRelative = "data/maps/vaporview_with_srtm.earth";
constexpr auto kFullLocalEarthRelative = "data/maps/vaporview_full_local.earth";
constexpr auto kFullLocalSrtmEarthRelative = "data/maps/vaporview_full_local_srtm.earth";
constexpr auto kSentinel2ImageryEarthRelative = "data/maps/vaporview_with_sentinel2_imagery.earth";
constexpr auto kLandsatImageryEarthRelative = "data/maps/vaporview_with_landsat_imagery.earth";
constexpr auto kOpenAerialMapImageryEarthRelative = "data/maps/vaporview_with_openaerialmap_imagery.earth";
constexpr auto kNaturalEarthTextureRelative = "data/maps/natural_earth/NE2_50M_SR_W/NE2_50M_SR_W_2048.png";
constexpr auto kNaturalEarthVrtRelative = "data/maps/natural_earth/NE2_50M_SR_W/NE2_50M_SR_W.vrt";
constexpr auto kNaturalEarthRasterRelative = "data/maps/natural_earth/NE2_50M_SR_W/NE2_50M_SR_W.tif";
constexpr auto kCopernicusDemVrtRelative = "data/maps/terrain/copernicus_dem_glo30/copernicus_dem_glo30.vrt";
constexpr auto kSrtmDemVrtRelative = "data/maps/terrain/srtm/srtm.vrt";
constexpr auto kOsmRoadsRelative = "data/maps/osm/roads.gpkg";
constexpr auto kOsmWaterRelative = "data/maps/osm/water.gpkg";
constexpr auto kOsmBuildingsRelative = "data/maps/osm/buildings.gpkg";
constexpr auto kOsmPlacesRelative = "data/maps/osm/places.gpkg";
constexpr auto kSentinel2ImageryVrtRelative = "data/maps/imagery/sentinel2/sentinel2.vrt";
constexpr auto kLandsatImageryVrtRelative = "data/maps/imagery/landsat/landsat.vrt";
constexpr auto kOpenAerialMapImageryVrtRelative = "data/maps/imagery/openaerialmap/openaerialmap.vrt";
constexpr auto kLocal3DTilesTilesetRelative = "data/maps/tiles3d/local/tileset.json";

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
        QStringLiteral("%1 -> layer buildings -> OGRFeatures osm-buildings -> FeatureImage OSM building footprints + TiledFeatureModel OSM building extrusion")
            .arg(diagnostics.osmBuildingsPath),
        QStringLiteral("%1 -> layer places -> OGRFeatures osm-places -> TiledFeatureModel OSM place labels")
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
    selection.foundFiles = selection.diagnostics.foundFiles;
    selection.missingFiles = selection.diagnostics.missingFiles;
    selection.warnings = selection.diagnostics.warnings;
}

int modePriority(MapDataMode mode)
{
    switch (mode)
    {
    case MapDataMode::FullLocalMap:
        return 4;
    case MapDataMode::NaturalEarthWithCopernicusDem:
        return 3;
    case MapDataMode::NaturalEarthWithSrtm:
        return 2;
    case MapDataMode::NaturalEarth:
        return 1;
    case MapDataMode::LocalGridOnly:
        return 0;
    }
    return 0;
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
        if (!haveSelection || modePriority(selection.mode) > modePriority(best.mode))
        {
            best = selection;
            haveSelection = true;
            if (best.mode == MapDataMode::FullLocalMap)
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
        || fileName == QStringLiteral("vaporview_full_local_srtm.earth");
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
    return {
        QDir::currentPath(),
        appDir,
        QDir(appDir).absoluteFilePath(QStringLiteral("../.."))
    };
}

MapDataSelection MapDataManager::evaluateRoot(const QString& root) const
{
    MapDataSelection selection;
    MapDataDiagnostics& diagnostics = selection.diagnostics;
    diagnostics.currentWorkingDirectory = QDir::currentPath();
    diagnostics.projectRoot = QDir::cleanPath(QDir(root).absolutePath());
    diagnostics.mapsRoot = QDir::cleanPath(QDir(root).absoluteFilePath(QStringLiteral("data/maps")));

    const QString defaultEarthPath = absolutePath(root, kDefaultEarthRelative);
    const QString copernicusEarthPath = absolutePath(root, kCopernicusEarthRelative);
    const QString srtmEarthPath = absolutePath(root, kSrtmEarthRelative);
    const QString fullLocalEarthPath = absolutePath(root, kFullLocalEarthRelative);
    const QString fullLocalSrtmEarthPath = absolutePath(root, kFullLocalSrtmEarthRelative);
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
    if (diagnostics.osgPluginPath.isEmpty())
    {
        diagnostics.osgPluginPath = firstExistingDirectory(roots, {
            QStringLiteral("osgPlugins-3.6.5"),
            QStringLiteral("plugins/osgPlugins-3.6.5"),
            QStringLiteral(".local_deps/vcpkg_installed/x64-windows/plugins/osgPlugins-3.6.5")
        });
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
    diagnostics.local3DTilesAvailable = isFile(diagnostics.local3DTilesTilesetPath);
    collectFullLocalBlockers(diagnostics, fullLocalEarthPath, fullLocalSrtmEarthPath);

    if (diagnostics.localImageryAvailable)
    {
        diagnostics.messages.push_back(
            QStringLiteral("Optional local high-resolution imagery VRTs detected; use the local imagery toolbar menu or load the matching imagery earth template."));
    }
    if (diagnostics.local3DTilesAvailable)
    {
        diagnostics.messages.push_back(
            QStringLiteral("Optional local 3D Tiles tileset detected for future 3D content loading."));
    }

    const QString selectedFullLocalEarthPath = fullLocalEarthForAvailableDem(
        fullLocalEarthPath,
        fullLocalSrtmEarthPath,
        diagnostics);

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
        diagnostics.selectedOsmLayerCount = diagnostics.osmLayerCount;
        diagnostics.messages.push_back(
            QStringLiteral("Selected full local map with offline OSM vector layers and %1 elevation.")
                .arg(diagnostics.selectedElevationSource));
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
