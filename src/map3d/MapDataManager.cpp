#include "map3d/MapDataManager.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QFileInfo>
#include <QtCore/QProcessEnvironment>

namespace VaporView::Map3D {
namespace {

constexpr auto kDefaultEarthRelative = "data/maps/vaporview_default.earth";
constexpr auto kCopernicusEarthRelative = "data/maps/vaporview_with_dem.earth";
constexpr auto kSrtmEarthRelative = "data/maps/vaporview_with_srtm.earth";
constexpr auto kNaturalEarthTextureRelative = "data/maps/natural_earth/NE2_50M_SR_W/NE2_50M_SR_W_2048.png";
constexpr auto kCopernicusDemVrtRelative = "data/maps/terrain/copernicus_dem_glo30/copernicus_dem_glo30.vrt";
constexpr auto kSrtmDemVrtRelative = "data/maps/terrain/srtm/srtm.vrt";

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

void requireFile(MapDataDiagnostics& diagnostics, const QString& path)
{
    if (!isFile(path))
    {
        diagnostics.missingFiles.push_back(path);
    }
}

} // namespace

MapDataManager::MapDataManager() = default;

bool MapDataSelection::hasEarthFile() const
{
    return !earthFilePath.isEmpty();
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
        || fileName == QStringLiteral("vaporview_with_srtm.earth");
}

QString MapDataManager::modeLabel(MapDataMode mode)
{
    switch (mode)
    {
    case MapDataMode::CopernicusDem:
        return QStringLiteral("Copernicus DEM");
    case MapDataMode::SrtmDem:
        return QStringLiteral("SRTM DEM");
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
    case MapDataMode::CopernicusDem:
        return QStringLiteral("CopernicusDem");
    case MapDataMode::SrtmDem:
        return QStringLiteral("SrtmDem");
    case MapDataMode::NaturalEarth:
        return QStringLiteral("NaturalEarth");
    case MapDataMode::LocalGridOnly:
        return QStringLiteral("LocalGridOnly");
    }
    return QStringLiteral("Unknown");
}

QStringList MapDataManager::candidateRoots() const
{
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
    diagnostics.mapsRoot = QDir::cleanPath(QDir(root).absoluteFilePath(QStringLiteral("data/maps")));

    const QString defaultEarthPath = absolutePath(root, kDefaultEarthRelative);
    const QString copernicusEarthPath = absolutePath(root, kCopernicusEarthRelative);
    const QString srtmEarthPath = absolutePath(root, kSrtmEarthRelative);
    diagnostics.naturalEarthTexturePath = absolutePath(root, kNaturalEarthTextureRelative);
    diagnostics.copernicusDemVrtPath = absolutePath(root, kCopernicusDemVrtRelative);
    diagnostics.srtmDemVrtPath = absolutePath(root, kSrtmDemVrtRelative);

    const QStringList roots = candidateRoots();
    const QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    diagnostics.osgPluginPath = environment.value(QStringLiteral("OSG_LIBRARY_PATH"));
    if (diagnostics.osgPluginPath.isEmpty())
    {
        diagnostics.osgPluginPath = firstExistingDirectory(roots, {
            QStringLiteral("osgPlugins-3.6.5"),
            QStringLiteral("plugins/osgPlugins-3.6.5"),
            QStringLiteral(".local_deps/vcpkg_installed/x64-windows/plugins/osgPlugins-3.6.5")
        });
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

    if (isFile(copernicusEarthPath)
        && isFile(diagnostics.naturalEarthTexturePath)
        && isFile(diagnostics.copernicusDemVrtPath))
    {
        selection.mode = MapDataMode::CopernicusDem;
        selection.earthFilePath = QFileInfo(copernicusEarthPath).absoluteFilePath();
        diagnostics.earthFilePath = selection.earthFilePath;
        diagnostics.messages.push_back(QStringLiteral("Selected Copernicus DEM GLO-30 local elevation."));
        return selection;
    }

    if (isFile(srtmEarthPath)
        && isFile(diagnostics.naturalEarthTexturePath)
        && isFile(diagnostics.srtmDemVrtPath))
    {
        selection.mode = MapDataMode::SrtmDem;
        selection.earthFilePath = QFileInfo(srtmEarthPath).absoluteFilePath();
        diagnostics.earthFilePath = selection.earthFilePath;
        diagnostics.messages.push_back(QStringLiteral("Selected SRTM local elevation fallback."));
        return selection;
    }

    if (isFile(defaultEarthPath) && isFile(diagnostics.naturalEarthTexturePath))
    {
        selection.mode = MapDataMode::NaturalEarth;
        selection.earthFilePath = QFileInfo(defaultEarthPath).absoluteFilePath();
        diagnostics.earthFilePath = selection.earthFilePath;
        diagnostics.messages.push_back(QStringLiteral("Selected Natural Earth offline background."));
        requireFile(diagnostics, copernicusEarthPath);
        requireFile(diagnostics, diagnostics.copernicusDemVrtPath);
        requireFile(diagnostics, srtmEarthPath);
        requireFile(diagnostics, diagnostics.srtmDemVrtPath);
        return selection;
    }

    selection.mode = MapDataMode::LocalGridOnly;
    diagnostics.messages.push_back(QStringLiteral("No complete offline map set found for this root."));
    requireFile(diagnostics, defaultEarthPath);
    requireFile(diagnostics, diagnostics.naturalEarthTexturePath);
    requireFile(diagnostics, copernicusEarthPath);
    requireFile(diagnostics, diagnostics.copernicusDemVrtPath);
    requireFile(diagnostics, srtmEarthPath);
    requireFile(diagnostics, diagnostics.srtmDemVrtPath);
    return selection;
}

} // namespace VaporView::Map3D
