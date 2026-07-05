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
    QString naturalEarthTexturePath;
    QString copernicusDemVrtPath;
    QString srtmDemVrtPath;
    QString osmRoadsPath;
    QString osmWaterPath;
    QString osmBuildingsPath;
    QString osmPlacesPath;
    QString osgPluginPath;
    QString gdalDataPath;
    QString projDataPath;
    QString osgLibraryPath;
    QString osgEarthNotifyLevel;
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
