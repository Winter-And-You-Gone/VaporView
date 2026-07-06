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
    VaporView::Map3D::MapDataManager builtInManager({root.absolutePath()});
    require(builtInManager.isBuiltInEarthFile(QStringLiteral("data/maps/vaporview_full_local_srtm.earth")),
            "SRTM full-local earth template should be treated as built in");
    selection = select(root);
    require(selection.mode == VaporView::Map3D::MapDataMode::LocalGridOnly, "preview texture alone should not select NaturalEarth");
    require(!selection.diagnostics.naturalEarthAvailable, "preview texture alone should not mark Natural Earth available");

    touch(root, QStringLiteral("data/maps/natural_earth/NE2_50M_SR_W/NE2_50M_SR_W.vrt"));
    touch(root, QStringLiteral("data/maps/natural_earth/NE2_50M_SR_W/NE2_50M_SR_W.tif"));
    selection = select(root);
    require(selection.mode == VaporView::Map3D::MapDataMode::NaturalEarth, "Natural Earth files should select NaturalEarth");
    require(selection.hasEarthFile(), "NaturalEarth selection should expose an earth file");
    require(selection.diagnostics.naturalEarthAvailable, "NaturalEarth selection should mark Natural Earth available");
    require(selection.diagnostics.osmLayerContracts.size() == 4, "diagnostics should describe the four expected OSM layer contracts");
    require(selection.diagnostics.osmLayerContracts.join(QLatin1Char('\n')).contains(QStringLiteral("roads.gpkg -> layer roads")),
            "OSM diagnostics should describe the roads GeoPackage layer name");
    require(selection.diagnostics.osmLayerContracts.join(QLatin1Char('\n')).contains(QStringLiteral("FeatureImage OSM roads")),
            "OSM diagnostics should describe the roads earth render layer");
    require(selection.diagnostics.osmLayerContracts.join(QLatin1Char('\n')).contains(QStringLiteral("TiledFeatureModel OSM place labels")),
            "OSM diagnostics should describe the place label render layer");
    require(!selection.diagnostics.selectedDemLayerAvailable, "NaturalEarth selection should not select a DEM layer");
    require(!selection.diagnostics.osmVectorAvailable, "NaturalEarth selection should not mark OSM complete");
    require(!selection.diagnostics.localImageryAvailable, "optional imagery should be absent by default");
    require(!selection.diagnostics.local3DTilesAvailable, "optional 3D Tiles should be absent by default");
    require(!selection.diagnostics.missingFiles.contains(selection.diagnostics.sentinel2ImageryVrtPath),
            "missing optional Sentinel-2 imagery should not be treated as required");
    require(!selection.diagnostics.missingFiles.contains(selection.diagnostics.local3DTilesTilesetPath),
            "missing optional 3D Tiles should not be treated as required");

    touch(root, QStringLiteral("data/maps/imagery/sentinel2/sentinel2.vrt"));
    touch(root, QStringLiteral("data/maps/imagery/landsat/landsat.vrt"));
    touch(root, QStringLiteral("data/maps/imagery/openaerialmap/openaerialmap.vrt"));
    touch(root, QStringLiteral("data/maps/tiles3d/local/tileset.json"));
    selection = select(root);
    require(selection.mode == VaporView::Map3D::MapDataMode::NaturalEarth,
            "optional imagery and 3D Tiles should not change base map selection");
    require(selection.diagnostics.localImageryAvailable, "optional imagery VRTs should be detected");
    require(selection.diagnostics.localImageryLayerCount == 3, "all three optional imagery VRTs should be counted");
    require(selection.diagnostics.local3DTilesAvailable, "optional local 3D Tiles tileset should be detected");
    require(selection.diagnostics.foundFiles.contains(selection.diagnostics.sentinel2ImageryVrtPath),
            "optional Sentinel-2 VRT should be listed as found");
    require(selection.diagnostics.foundFiles.contains(selection.diagnostics.local3DTilesTilesetPath),
            "optional 3D Tiles tileset should be listed as found");
    require(selection.diagnostics.messages.join(QLatin1Char('\n')).contains(QStringLiteral("imagery earth template")),
            "optional imagery diagnostics should mention manual imagery earth templates");

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
    touch(root, QStringLiteral("data/maps/vaporview_full_local_srtm.earth"));
    selection = select(root);
    require(selection.mode == VaporView::Map3D::MapDataMode::NaturalEarthWithCopernicusDem, "incomplete OSM set should not select full local map");
    require(!selection.warnings.isEmpty(), "missing OSM files should produce a warning");
    require(selection.diagnostics.osmLayerCount == 0, "incomplete OSM set should report zero OSM layers");
    require(selection.diagnostics.missingOsmFiles.size() == 4, "incomplete OSM set should report four missing OSM files");
    require(!selection.diagnostics.fullLocalBlockers.isEmpty(), "incomplete OSM set should explain full-local blockers");
    require(selection.diagnostics.fullLocalBlockers.join(QLatin1Char('\n')).contains(QStringLiteral("Missing OSM GeoPackage")),
            "full-local blockers should identify missing OSM GeoPackages");
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
    require(selection.diagnostics.osmLayerContracts.join(QLatin1Char('\n')).contains(QStringLiteral("buildings.gpkg -> layer buildings")),
            "complete OSM diagnostics should retain the building layer contract");
    require(selection.diagnostics.missingOsmFiles.isEmpty(), "complete local data should not report missing OSM files");
    require(selection.diagnostics.fullLocalBlockers.isEmpty(), "complete local data should not report full-local blockers");
    require(selection.diagnostics.selectedFullLocalEarthPath.endsWith(QStringLiteral("vaporview_full_local.earth")),
            "complete Copernicus full local map should report selected full-local earth template");

    QTemporaryDir srtmFullLocalDir;
    require(srtmFullLocalDir.isValid(), "failed to create SRTM full-local temporary directory");
    QDir srtmRoot(srtmFullLocalDir.path());
    touch(srtmRoot, QStringLiteral("data/maps/vaporview_default.earth"));
    touch(srtmRoot, QStringLiteral("data/maps/vaporview_with_srtm.earth"));
    touch(srtmRoot, QStringLiteral("data/maps/vaporview_full_local_srtm.earth"));
    touch(srtmRoot, QStringLiteral("data/maps/natural_earth/NE2_50M_SR_W/NE2_50M_SR_W_2048.png"));
    touch(srtmRoot, QStringLiteral("data/maps/natural_earth/NE2_50M_SR_W/NE2_50M_SR_W.vrt"));
    touch(srtmRoot, QStringLiteral("data/maps/natural_earth/NE2_50M_SR_W/NE2_50M_SR_W.tif"));
    touch(srtmRoot, QStringLiteral("data/maps/terrain/srtm/srtm.vrt"));
    touch(srtmRoot, QStringLiteral("data/maps/osm/roads.gpkg"));
    touch(srtmRoot, QStringLiteral("data/maps/osm/water.gpkg"));
    touch(srtmRoot, QStringLiteral("data/maps/osm/buildings.gpkg"));
    touch(srtmRoot, QStringLiteral("data/maps/osm/places.gpkg"));
    selection = select(srtmRoot);
    require(selection.mode == VaporView::Map3D::MapDataMode::FullLocalMap,
            "SRTM plus complete OSM should select full local map");
    require(selection.earthFile.endsWith(QStringLiteral("vaporview_full_local_srtm.earth")),
            "SRTM-only full local map should load the SRTM full-local earth template");
    require(selection.diagnostics.selectedElevationSource == QStringLiteral("SRTM"),
            "SRTM-only full local map should report SRTM elevation source");
    require(selection.diagnostics.selectedFullLocalEarthPath.endsWith(QStringLiteral("vaporview_full_local_srtm.earth")),
            "SRTM-only full local map should report selected SRTM full-local earth template");

    QTemporaryDir naturalOnlyDir;
    QTemporaryDir demPreferredDir;
    require(naturalOnlyDir.isValid(), "failed to create natural-only temporary directory");
    require(demPreferredDir.isValid(), "failed to create DEM-preferred temporary directory");
    QDir naturalRoot(naturalOnlyDir.path());
    QDir demRoot(demPreferredDir.path());
    touch(naturalRoot, QStringLiteral("data/maps/vaporview_default.earth"));
    touch(naturalRoot, QStringLiteral("data/maps/natural_earth/NE2_50M_SR_W/NE2_50M_SR_W_2048.png"));
    touch(naturalRoot, QStringLiteral("data/maps/natural_earth/NE2_50M_SR_W/NE2_50M_SR_W.vrt"));
    touch(naturalRoot, QStringLiteral("data/maps/natural_earth/NE2_50M_SR_W/NE2_50M_SR_W.tif"));
    touch(demRoot, QStringLiteral("data/maps/vaporview_default.earth"));
    touch(demRoot, QStringLiteral("data/maps/vaporview_with_dem.earth"));
    touch(demRoot, QStringLiteral("data/maps/natural_earth/NE2_50M_SR_W/NE2_50M_SR_W_2048.png"));
    touch(demRoot, QStringLiteral("data/maps/natural_earth/NE2_50M_SR_W/NE2_50M_SR_W.vrt"));
    touch(demRoot, QStringLiteral("data/maps/natural_earth/NE2_50M_SR_W/NE2_50M_SR_W.tif"));
    touch(demRoot, QStringLiteral("data/maps/terrain/copernicus_dem_glo30/copernicus_dem_glo30.vrt"));
    VaporView::Map3D::MapDataManager multiRootManager({naturalRoot.absolutePath(), demRoot.absolutePath()});
    selection = multiRootManager.selectBestAvailableMap();
    require(selection.mode == VaporView::Map3D::MapDataMode::NaturalEarthWithCopernicusDem,
            "manager should choose the best map mode across candidate roots, not the first usable root");
    require(selection.earthFile.startsWith(demRoot.absolutePath()),
            "best map selection should come from the DEM-capable root");

    return 0;
}
