#pragma once

#include <QtCore/QString>
#include <QtCore/QStringList>

namespace VaporView::Map3D {

enum class MapDataMode {
    LocalGridOnly = 0,
    NaturalEarth,
    SrtmDem,
    CopernicusDem
};

struct MapDataDiagnostics {
    QString mapsRoot;
    QString earthFilePath;
    QString naturalEarthTexturePath;
    QString copernicusDemVrtPath;
    QString srtmDemVrtPath;
    QString osgPluginPath;
    QString gdalDataPath;
    QString projDataPath;
    QStringList missingFiles;
    QStringList messages;
};

struct MapDataSelection {
    MapDataMode mode = MapDataMode::LocalGridOnly;
    QString earthFilePath;
    MapDataDiagnostics diagnostics;

    bool hasEarthFile() const;
};

class MapDataManager {
public:
    MapDataManager();

    MapDataSelection selectBestAvailableMap() const;
    bool isBuiltInEarthFile(const QString& earthPath) const;

    static QString modeLabel(MapDataMode mode);
    static QString modeKey(MapDataMode mode);

private:
    QStringList candidateRoots() const;
    MapDataSelection evaluateRoot(const QString& root) const;
};

} // namespace VaporView::Map3D
