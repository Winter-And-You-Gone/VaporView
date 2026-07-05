#include "map3d/MapDataManager.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QTemporaryDir>
#include <iostream>

namespace {

void fail(const char* message)
{
    std::cerr << message << '\n';
    std::exit(1);
}

void require(bool condition, const char* message)
{
    if (!condition)
    {
        fail(message);
    }
}

void touch(const QDir& root, const QString& relative)
{
    const QFileInfo info(root.absoluteFilePath(relative));
    QDir().mkpath(info.absolutePath());
    QFile file(info.absoluteFilePath());
    require(file.open(QIODevice::WriteOnly), "failed to create test file");
    file.write("test");
}

VaporView::Map3D::MapDataSelection select(const QDir& root)
{
    VaporView::Map3D::MapDataManager manager({root.absolutePath()});
    return manager.selectBestAvailableMap();
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    QTemporaryDir tempDir;
    require(tempDir.isValid(), "failed to create temporary directory");
    QDir root(tempDir.path());

    auto selection = select(root);
    require(selection.mode == VaporView::Map3D::MapDataMode::LocalGridOnly, "empty root should use local grid");
    require(!selection.hasEarthFile(), "empty root should not have an earth file");

    touch(root, QStringLiteral("data/maps/vaporview_default.earth"));
    touch(root, QStringLiteral("data/maps/natural_earth/NE2_50M_SR_W/NE2_50M_SR_W_2048.png"));
    selection = select(root);
    require(selection.mode == VaporView::Map3D::MapDataMode::LocalGridOnly, "preview texture alone should not select NaturalEarth");
    require(!selection.diagnostics.naturalEarthAvailable, "preview texture alone should not mark Natural Earth available");

    touch(root, QStringLiteral("data/maps/natural_earth/NE2_50M_SR_W/NE2_50M_SR_W.vrt"));
    touch(root, QStringLiteral("data/maps/natural_earth/NE2_50M_SR_W/NE2_50M_SR_W.tif"));
    selection = select(root);
    require(selection.mode == VaporView::Map3D::MapDataMode::NaturalEarth, "Natural Earth files should select NaturalEarth");
    require(selection.hasEarthFile(), "NaturalEarth selection should expose an earth file");
    require(selection.diagnostics.naturalEarthAvailable, "NaturalEarth selection should mark Natural Earth available");
    require(!selection.diagnostics.selectedDemLayerAvailable, "NaturalEarth selection should not select a DEM layer");
    require(!selection.diagnostics.osmVectorAvailable, "NaturalEarth selection should not mark OSM complete");

    touch(root, QStringLiteral("data/maps/vaporview_with_srtm.earth"));
    touch(root, QStringLiteral("data/maps/terrain/srtm/srtm.vrt"));
    selection = select(root);
    require(selection.mode == VaporView::Map3D::MapDataMode::NaturalEarthWithSrtm, "SRTM files should select SRTM mode");
    require(selection.diagnostics.srtmDemAvailable, "SRTM VRT should be marked available");
    require(selection.diagnostics.selectedDemLayerAvailable, "SRTM selection should select a DEM layer");
    require(selection.diagnostics.selectedElevationSource == QStringLiteral("SRTM"), "SRTM selection should report SRTM elevation source");

    touch(root, QStringLiteral("data/maps/vaporview_with_dem.earth"));
    touch(root, QStringLiteral("data/maps/terrain/copernicus_dem_glo30/copernicus_dem_glo30.vrt"));
    selection = select(root);
    require(selection.mode == VaporView::Map3D::MapDataMode::NaturalEarthWithCopernicusDem, "Copernicus files should outrank SRTM");
    require(selection.diagnostics.copernicusDemAvailable, "Copernicus VRT should be marked available");
    require(selection.diagnostics.selectedElevationSource == QStringLiteral("Copernicus DEM GLO-30"), "Copernicus selection should report Copernicus elevation source");

    touch(root, QStringLiteral("data/maps/vaporview_full_local.earth"));
    selection = select(root);
    require(selection.mode == VaporView::Map3D::MapDataMode::NaturalEarthWithCopernicusDem, "incomplete OSM set should not select full local map");
    require(!selection.warnings.isEmpty(), "missing OSM files should produce a warning");
    require(selection.diagnostics.osmLayerCount == 0, "incomplete OSM set should report zero OSM layers");
    require(!selection.diagnostics.selectedOsmLayersAvailable, "incomplete OSM set should not select OSM layers");

    touch(root, QStringLiteral("data/maps/osm/roads.gpkg"));
    touch(root, QStringLiteral("data/maps/osm/water.gpkg"));
    touch(root, QStringLiteral("data/maps/osm/buildings.gpkg"));
    touch(root, QStringLiteral("data/maps/osm/places.gpkg"));
    selection = select(root);
    require(selection.mode == VaporView::Map3D::MapDataMode::FullLocalMap, "complete local data should select full local map");
    require(selection.diagnostics.foundFiles.contains(selection.earthFile), "selected earth file should be listed as found");
    require(selection.diagnostics.foundFiles.contains(selection.diagnostics.naturalEarthVrtPath), "Natural Earth VRT should be listed as found");
    require(selection.diagnostics.osmVectorAvailable, "complete local data should mark OSM vectors available");
    require(selection.diagnostics.selectedOsmLayersAvailable, "complete local data should select OSM layers");
    require(selection.diagnostics.selectedOsmLayerCount == 4, "complete local data should report four selected OSM layers");

    return 0;
}
