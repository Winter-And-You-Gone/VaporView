#pragma once

#include <QString>
#include <QStringList>

namespace VaporView::Map3D {

inline constexpr unsigned kTerrainMinLevel = 8;
inline constexpr auto kGlobalBaseDemRelativePath =
    "resources/maps/terrain/global/etopo2022_global_coarse.tif";
inline constexpr auto kGlobalBaseDemLayerName = "Global Base DEM (ETOPO 2022)";

QStringList map3DRuntimeRootCandidates();
QString map3DDownloadedMapRoot();
QString map3DProjectDataDirectory();
QString firstExistingMap3DFile(const QStringList& roots, const QStringList& relatives);
void initializeMap3DRuntime();

} // namespace VaporView::Map3D
