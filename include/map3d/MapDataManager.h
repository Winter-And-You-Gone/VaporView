#pragma once

#include <QtCore/QString>
#include <QtCore/QStringList>

namespace VaporView::Map3D {

enum class MapDataMode {
    LocalGridOnly = 0,
    NaturalEarth,
    NaturalEarthWithSrtm,
    NaturalEarthWithCopernicusDem,
    FullLocalMap
};

struct MapDataDiagnostics {
    QString currentWorkingDirectory;
    QString projectRoot;
    QString mapsRoot;
    QString earthFilePath;
    QString fullLocalEarthPath;
    QString fullLocalSrtmEarthPath;
    QString naturalEarthTexturePath;
    QString naturalEarthVrtPath;
    QString naturalEarthRasterPath;
    QString copernicusDemVrtPath;
    QString srtmDemVrtPath;
    QString osmRoadsPath;
    QString osmWaterPath;
    QString osmBuildingsPath;
    QString osmPlacesPath;
    QString sentinel2ImageryVrtPath;
    QString landsatImageryVrtPath;
    QString openAerialMapImageryVrtPath;
    QString local3DTilesTilesetPath;
    QString osgPluginPath;
    QString gdalDataPath;
    QString projDataPath;
    QString projLibPath;
    QString osgLibraryPath;
    QString osgEarthNotifyLevel;
    bool naturalEarthAvailable = false;
    bool copernicusDemAvailable = false;
    bool srtmDemAvailable = false;
    bool osmVectorAvailable = false;
    bool localImageryAvailable = false;
    bool local3DTilesAvailable = false;
    bool selectedDemLayerAvailable = false;
    bool selectedOsmLayersAvailable = false;
    QString selectedElevationSource;
    int osmLayerCount = 0;
    int selectedOsmLayerCount = 0;
    int localImageryLayerCount = 0;
    QStringList missingFiles;
    QStringList foundFiles;
    QStringList warnings;
    QStringList messages;
};

struct MapDataSelection {
    MapDataMode mode = MapDataMode::LocalGridOnly;
    QString earthFile;
    QString earthFilePath;
    QString description;
    QStringList foundFiles;
    QStringList missingFiles;
    QStringList warnings;
    MapDataDiagnostics diagnostics;

    bool hasEarthFile() const;
};

class MapDataManager {
public:
    MapDataManager();
    explicit MapDataManager(QStringList candidateRoots);

    MapDataSelection selectBestAvailableMap() const;
    bool isBuiltInEarthFile(const QString& earthPath) const;

    static QString modeLabel(MapDataMode mode);
    static QString modeKey(MapDataMode mode);

private:
    QStringList candidateRoots() const;
    MapDataSelection evaluateRoot(const QString& root) const;

    QStringList candidate_roots_;
};

} // namespace VaporView::Map3D
