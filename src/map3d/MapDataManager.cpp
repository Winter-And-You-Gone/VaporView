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
constexpr auto kNaturalEarthTextureRelative = "data/maps/natural_earth/NE2_50M_SR_W/NE2_50M_SR_W_2048.png";
constexpr auto kNaturalEarthVrtRelative = "data/maps/natural_earth/NE2_50M_SR_W/NE2_50M_SR_W.vrt";
constexpr auto kNaturalEarthRasterRelative = "data/maps/natural_earth/NE2_50M_SR_W/NE2_50M_SR_W.tif";
constexpr auto kCopernicusDemVrtRelative = "data/maps/terrain/copernicus_dem_glo30/copernicus_dem_glo30.vrt";
constexpr auto kSrtmDemVrtRelative = "data/maps/terrain/srtm/srtm.vrt";
constexpr auto kOsmRoadsRelative = "data/maps/osm/roads.gpkg";
constexpr auto kOsmWaterRelative = "data/maps/osm/water.gpkg";
constexpr auto kOsmBuildingsRelative = "data/maps/osm/buildings.gpkg";
constexpr auto kOsmPlacesRelative = "data/maps/osm/places.gpkg";

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
    MapDataSelection fallback;
    fallback.diagnostics.messages.push_back(QStringLiteral("No usable map root found; using local grid only."));

    for (const QString& root : candidateRoots())
    {
        const MapDataSelection selection = evaluateRoot(root);
        if (selection.mode != MapDataMode::LocalGridOnly)
        {
            return selection;
        }
        fallback = selection;
    }
    return fallback;
}

bool MapDataManager::isBuiltInEarthFile(const QString& earthPath) const
{
    const QString fileName = QFileInfo(earthPath).fileName();
    return fileName == QStringLiteral("vaporview_default.earth")
        || fileName == QStringLiteral("vaporview_with_dem.earth")
        || fileName == QStringLiteral("vaporview_with_srtm.earth")
        || fileName == QStringLiteral("vaporview_full_local.earth");
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
    diagnostics.fullLocalEarthPath = fullLocalEarthPath;
    diagnostics.naturalEarthTexturePath = absolutePath(root, kNaturalEarthTextureRelative);
    diagnostics.naturalEarthVrtPath = absolutePath(root, kNaturalEarthVrtRelative);
    diagnostics.naturalEarthRasterPath = absolutePath(root, kNaturalEarthRasterRelative);
    diagnostics.copernicusDemVrtPath = absolutePath(root, kCopernicusDemVrtRelative);
    diagnostics.srtmDemVrtPath = absolutePath(root, kSrtmDemVrtRelative);
    diagnostics.osmRoadsPath = absolutePath(root, kOsmRoadsRelative);
    diagnostics.osmWaterPath = absolutePath(root, kOsmWaterRelative);
    diagnostics.osmBuildingsPath = absolutePath(root, kOsmBuildingsRelative);
    diagnostics.osmPlacesPath = absolutePath(root, kOsmPlacesRelative);

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
    if (diagnostics.projDataPath.isEmpty())
    {
        diagnostics.projDataPath = environment.value(QStringLiteral("PROJ_LIB"));
    }
    if (diagnostics.projDataPath.isEmpty())
    {
        diagnostics.projDataPath = firstExistingDirectory(roots, {
            QStringLiteral("share/proj"),
            QStringLiteral("share/proj4"),
            QStringLiteral(".local_deps/vcpkg_installed/x64-windows/share/proj"),
            QStringLiteral(".local_deps/vcpkg_installed/x64-windows/share/proj4")
        });
    }

    recordFile(diagnostics, defaultEarthPath);
    recordFile(diagnostics, fullLocalEarthPath);
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

    if (isFile(copernicusEarthPath)
        && hasNaturalEarth(diagnostics)
        && isFile(diagnostics.copernicusDemVrtPath))
    {
        selection.mode = MapDataMode::NaturalEarthWithCopernicusDem;
        selection.description = QStringLiteral("Natural Earth background with local Copernicus DEM elevation.");
        setEarthFile(selection, copernicusEarthPath);
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
        && hasNaturalEarth(diagnostics)
        && isFile(diagnostics.srtmDemVrtPath))
    {
        selection.mode = MapDataMode::NaturalEarthWithSrtm;
        selection.description = QStringLiteral("Natural Earth background with local SRTM elevation.");
        setEarthFile(selection, srtmEarthPath);
        diagnostics.messages.push_back(QStringLiteral("Selected SRTM local elevation fallback."));
        finalizeSelection(selection);
        return selection;
    }

    if (isFile(defaultEarthPath) && hasNaturalEarth(diagnostics))
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
