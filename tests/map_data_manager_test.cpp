#include "map3d/MapDataManager.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QTemporaryDir>
#include <iostream>

namespace {

class EnvVarGuard {
public:
    explicit EnvVarGuard(const char* name)
        : name_(name),
          original_(qgetenv(name)),
          had_original_(!original_.isEmpty())
    {
    }

    ~EnvVarGuard()
    {
        if (had_original_)
        {
            qputenv(name_, original_);
        }
        else
        {
            qunsetenv(name_);
        }
    }

    void unset()
    {
        qunsetenv(name_);
    }

private:
    const char* name_;
    QByteArray original_;
    bool had_original_ = false;
};

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

void writeFile(const QDir& root, const QString& relative, const QByteArray& contents)
{
    const QFileInfo info(root.absoluteFilePath(relative));
    QDir().mkpath(info.absolutePath());
    QFile file(info.absoluteFilePath());
    require(file.open(QIODevice::WriteOnly), "failed to write test file");
    file.write(contents);
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
    require(selection.diagnostics.readinessSummary.contains(QStringLiteral("Local grid fallback")),
            "local grid selection should summarize fallback readiness");
    require(selection.diagnostics.baseMapPriority.contains(QStringLiteral("Copernicus DEM > SRTM > Natural Earth > Local grid")),
            "diagnostics should state the base map selection priority");
    require(selection.diagnostics.selectedBaseMode == VaporView::Map3D::MapDataMode::LocalGridOnly,
            "local grid selection should report LocalGridOnly as the selected base mode");
    require(selection.diagnostics.readinessNextSteps.join(QLatin1Char('\n')).contains(QStringLiteral("Natural Earth")),
            "local grid readiness should suggest preparing Natural Earth");
    require(selection.diagnostics.readinessNextSteps.join(QLatin1Char('\n')).contains(QStringLiteral("download-natural-earth-map.ps1")),
            "local grid readiness should include the Natural Earth preparation command");
    require(selection.diagnostics.readinessNextSteps.join(QLatin1Char('\n')).contains(QStringLiteral("python scripts/prepare-demo-dem.py")),
            "local grid readiness should include the DEM preparation command");

    {
        EnvVarGuard osgLibraryPathGuard("OSG_LIBRARY_PATH");
        osgLibraryPathGuard.unset();
        touch(root, QStringLiteral(".local_deps/vcpkg_installed/x64-windows/plugins/osgPlugins-3.6.6/osgdb_earth.dll"));
        selection = select(root);
        require(selection.diagnostics.osgPluginPath.endsWith(QStringLiteral("osgPlugins-3.6.6")),
                "diagnostics should discover project-local osgPlugins-* directories without a hard-coded OSG patch version");
        require(selection.diagnostics.osgLibraryPath == selection.diagnostics.osgPluginPath,
                "OSG library path diagnostics should mirror the inferred plugin directory");
    }

    touch(root, QStringLiteral("data/maps/natural_earth/NE2_50M_SR_W/NE2_50M_SR_W.vrt"));
    touch(root, QStringLiteral("data/maps/natural_earth/NE2_50M_SR_W/NE2_50M_SR_W.tif"));
    selection = select(root);
    require(selection.mode == VaporView::Map3D::MapDataMode::NaturalEarth, "Natural Earth files should select NaturalEarth");
    require(selection.hasEarthFile(), "NaturalEarth selection should expose an earth file");
    require(selection.diagnostics.naturalEarthAvailable, "NaturalEarth selection should mark Natural Earth available");
    require(selection.diagnostics.selectedBaseMode == VaporView::Map3D::MapDataMode::NaturalEarth,
            "NaturalEarth selection should report NaturalEarth as the selected base mode");
    require(selection.diagnostics.selectedBaseEarthFilePath.endsWith(QStringLiteral("vaporview_default.earth")),
            "NaturalEarth selection should report the default earth file as the selected base earth");
    require(selection.diagnostics.readinessSummary.contains(QStringLiteral("visual background only")),
            "Natural Earth selection should explain visual-only readiness");
    require(selection.diagnostics.readinessNextSteps.join(QLatin1Char('\n')).contains(QStringLiteral("Copernicus DEM")),
            "Natural Earth readiness should suggest preparing Copernicus DEM");
    require(selection.diagnostics.readinessNextSteps.join(QLatin1Char('\n')).contains(QStringLiteral("python scripts/prepare-demo-dem.py")),
            "Natural Earth readiness should include the Copernicus DEM preparation command");
    require(selection.diagnostics.readinessNextSteps.join(QLatin1Char('\n')).contains(QStringLiteral("python scripts/prepare-demo-dem.py --srtm")),
            "Natural Earth readiness should include the SRTM fallback preparation command");
    require(selection.diagnostics.readinessChecks.join(QLatin1Char('\n')).contains(QStringLiteral("Terrain DEM: missing")),
            "Natural Earth readiness checks should report missing terrain DEM");
    require(selection.diagnostics.osmLayerContracts.size() == 4, "diagnostics should describe the four expected OSM layer contracts");
    require(selection.diagnostics.osmLayerContracts.join(QLatin1Char('\n')).contains(QStringLiteral("roads.gpkg -> layer roads")),
            "OSM diagnostics should describe the roads GeoPackage layer name");
    require(selection.diagnostics.osmLayerContracts.join(QLatin1Char('\n')).contains(QStringLiteral("FeatureImage OSM roads")),
            "OSM diagnostics should describe the roads earth render layer");
    require(selection.diagnostics.osmLayerContracts.join(QLatin1Char('\n')).contains(QStringLiteral("FeatureImage OSM building footprints")),
            "OSM diagnostics should describe the building footprint render layer");
    require(selection.diagnostics.osmLayerContracts.join(QLatin1Char('\n')).contains(QStringLiteral("TiledFeatureModel OSM building extrusion")),
            "OSM diagnostics should describe the building extrusion render layer");
    require(selection.diagnostics.osmLayerContracts.join(QLatin1Char('\n')).contains(QStringLiteral("TiledFeatureModel OSM place labels")),
            "OSM diagnostics should describe the place label render layer");
    require(!selection.diagnostics.selectedDemLayerAvailable, "NaturalEarth selection should not select a DEM layer");
    require(!selection.diagnostics.osmVectorAvailable, "NaturalEarth selection should not mark OSM complete");
    require(!selection.diagnostics.localImageryAvailable, "optional imagery should be absent by default");
    require(!selection.diagnostics.localImageryMenuAvailable, "optional imagery menu should be unavailable by default");
    require(selection.diagnostics.localImageryMenuEntryCount == 0, "missing optional imagery should report zero menu-ready entries");
    require(selection.diagnostics.localImageryOptions.size() == 3,
            "diagnostics should expose all optional local imagery slots");
    require(!selection.diagnostics.localImageryOptions[0].available,
            "missing optional imagery slot should not be available");
    require(!selection.diagnostics.local3DTilesAvailable, "optional 3D Tiles should be absent by default");
    require(!selection.diagnostics.missingFiles.contains(selection.diagnostics.sentinel2ImageryVrtPath),
            "missing optional Sentinel-2 imagery should not be treated as required");
    require(!selection.diagnostics.missingFiles.contains(selection.diagnostics.sentinel2ImageryEarthPath),
            "missing optional Sentinel-2 imagery earth template should not be treated as required");
    require(!selection.diagnostics.missingFiles.contains(selection.diagnostics.landsatImageryEarthPath),
            "missing optional Landsat imagery earth template should not be treated as required");
    require(!selection.diagnostics.missingFiles.contains(selection.diagnostics.openAerialMapImageryEarthPath),
            "missing optional OpenAerialMap imagery earth template should not be treated as required");
    require(!selection.diagnostics.missingFiles.contains(selection.diagnostics.local3DTilesTilesetPath),
            "missing optional 3D Tiles should not be treated as required");

    touch(root, QStringLiteral("data/maps/imagery/sentinel2/sentinel2.vrt"));
    selection = select(root);
    require(selection.diagnostics.localImageryAvailable,
            "optional imagery VRT should be detected even when the matching earth template is missing");
    require(selection.diagnostics.localImageryLayerCount == 1,
            "diagnostics should count detected optional imagery VRTs separately");
    require(!selection.diagnostics.localImageryMenuAvailable,
            "imagery menu should stay disabled when only a VRT is present");
    require(selection.diagnostics.localImageryMenuEntryCount == 0,
            "VRT-only imagery should not count as a menu-ready entry");
    require(!selection.diagnostics.localImageryOptions[0].available,
            "VRT-only imagery option should not be directly loadable from the menu");
    require(selection.diagnostics.messages.join(QLatin1Char('\n')).contains(QStringLiteral("no matching imagery earth templates")),
            "VRT-only imagery diagnostics should explain why the menu is unavailable");

    touch(root, QStringLiteral("data/maps/vaporview_with_sentinel2_imagery.earth"));
    touch(root, QStringLiteral("data/maps/imagery/landsat/landsat.vrt"));
    touch(root, QStringLiteral("data/maps/vaporview_with_landsat_imagery.earth"));
    touch(root, QStringLiteral("data/maps/imagery/openaerialmap/openaerialmap.vrt"));
    touch(root, QStringLiteral("data/maps/vaporview_with_openaerialmap_imagery.earth"));
    touch(root, QStringLiteral("data/maps/tiles3d/local/tileset.json"));
    selection = select(root);
    require(selection.mode == VaporView::Map3D::MapDataMode::NaturalEarth,
            "optional imagery and 3D Tiles should not change base map selection");
    require(selection.diagnostics.localImageryAvailable, "optional imagery VRTs should be detected");
    require(selection.diagnostics.localImageryMenuAvailable, "optional imagery menu should be available when VRTs and earth templates exist");
    require(selection.diagnostics.localImageryLayerCount == 3, "all three optional imagery VRTs should be counted");
    require(selection.diagnostics.localImageryMenuEntryCount == 3, "all three optional imagery entries should be menu-ready");
    require(selection.diagnostics.localImageryOptions.size() == 3,
            "all three optional imagery menu entries should be reported");
    require(selection.diagnostics.localImageryOptions[0].available,
            "Sentinel-2 imagery option should be available when VRT and earth template exist");
    require(selection.diagnostics.localImageryOptions[0].earthFilePath.endsWith(QStringLiteral("vaporview_with_sentinel2_imagery.earth")),
            "Sentinel-2 imagery option should point at its earth template");
    require(selection.diagnostics.local3DTilesAvailable, "optional local 3D Tiles tileset should be detected");
    require(!selection.diagnostics.local3DTilesTilesetValid,
            "invalid placeholder 3D Tiles JSON should not pass local-only contract checks");
    require(selection.diagnostics.local3DTilesDiagnostics.join(QLatin1Char('\n')).contains(QStringLiteral("not valid JSON")),
            "invalid 3D Tiles JSON should be diagnosed");
    require(selection.diagnostics.foundFiles.contains(selection.diagnostics.sentinel2ImageryVrtPath),
            "optional Sentinel-2 VRT should be listed as found");
    require(selection.diagnostics.foundFiles.contains(selection.diagnostics.local3DTilesTilesetPath),
            "optional 3D Tiles tileset should be listed as found");
    require(selection.diagnostics.messages.join(QLatin1Char('\n')).contains(QStringLiteral("menu-ready overlays")),
            "optional imagery diagnostics should mention menu-ready toolbar overlays");

    writeFile(root, QStringLiteral("data/maps/tiles3d/local/content/building.b3dm"),
              QByteArrayLiteral("not a real b3dm"));
    writeFile(root,
              QStringLiteral("data/maps/tiles3d/local/tileset.json"),
              QByteArrayLiteral(R"JSON({
  "asset": {"version": "1.1"},
  "geometricError": 500,
  "root": {
    "boundingVolume": {"region": [0, 0, 0.1, 0.1, 0, 100]},
    "geometricError": 250,
    "content": {"uri": "content/building.b3dm"}
  }
})JSON"));
    selection = select(root);
    require(selection.diagnostics.local3DTilesAvailable, "valid local 3D Tiles tileset should be detected");
    require(selection.diagnostics.local3DTilesTilesetValid,
            "valid local 3D Tiles tileset should pass local-only contract checks");
    require(selection.diagnostics.local3DTilesResourceCount == 1,
            "valid local 3D Tiles tileset should report referenced resource count");
    require(selection.diagnostics.local3DTilesResourceUris.contains(QStringLiteral("content/building.b3dm")),
            "valid local 3D Tiles diagnostics should list content URI");
    require(selection.diagnostics.local3DTilesExternalUris.isEmpty(),
            "valid local 3D Tiles tileset should not report external URIs");
    require(selection.diagnostics.local3DTilesMissingResources.isEmpty(),
            "valid local 3D Tiles tileset should not report missing resources");

    writeFile(root, QStringLiteral("data/maps/tiles3d/local/content/parent.b3dm"),
              QByteArrayLiteral("not a real parent b3dm"));
    writeFile(root, QStringLiteral("data/maps/tiles3d/local/content/child-a.b3dm"),
              QByteArrayLiteral("not a real child a b3dm"));
    writeFile(root, QStringLiteral("data/maps/tiles3d/local/content/child-b.b3dm"),
              QByteArrayLiteral("not a real child b b3dm"));
    writeFile(root,
              QStringLiteral("data/maps/tiles3d/local/tileset.json"),
              QByteArrayLiteral(R"JSON({
  "asset": {"version": "1.1"},
  "geometricError": 500,
  "root": {
    "boundingVolume": {"region": [0, 0, 0.1, 0.1, 0, 100]},
    "geometricError": 250,
    "content": {"uri": "content/parent.b3dm"},
    "children": [
      {
        "boundingVolume": {"region": [0, 0, 0.05, 0.05, 0, 50]},
        "geometricError": 0,
        "contents": [
          {"uri": "content/child-a.b3dm"},
          {"url": "content/child-b.b3dm"}
        ]
      }
    ]
  }
})JSON"));
    selection = select(root);
    require(selection.diagnostics.local3DTilesTilesetValid,
            "nested local 3D Tiles contents[] and child content URLs should pass local-only checks");
    require(selection.diagnostics.local3DTilesResourceCount == 3,
            "nested local 3D Tiles diagnostics should count parent and child resources");
    require(selection.diagnostics.local3DTilesResourceUris.contains(QStringLiteral("content/parent.b3dm")),
            "nested local 3D Tiles diagnostics should list parent content URI");
    require(selection.diagnostics.local3DTilesResourceUris.contains(QStringLiteral("content/child-a.b3dm")),
            "nested local 3D Tiles diagnostics should list child contents URI");
    require(selection.diagnostics.local3DTilesResourceUris.contains(QStringLiteral("content/child-b.b3dm")),
            "nested local 3D Tiles diagnostics should list child contents URL alias");

    writeFile(root,
              QStringLiteral("data/maps/tiles3d/local/tileset.json"),
              QByteArrayLiteral(R"JSON({
  "asset": {"version": "1.1"},
  "geometricError": 500,
  "root": {
    "boundingVolume": {"region": [0, 0, 0.1, 0.1, 0, 100]},
    "geometricError": 250,
    "content": {"uri": "https://example.invalid/tiles/building.b3dm"}
  }
})JSON"));
    selection = select(root);
    require(selection.diagnostics.local3DTilesAvailable,
            "3D Tiles tileset with remote URI should still be detected as present");
    require(!selection.diagnostics.local3DTilesTilesetValid,
            "3D Tiles tileset with remote URI should not pass local-only checks");
    require(selection.diagnostics.local3DTilesHasExternalUris,
            "3D Tiles tileset with remote URI should report external URI flag");
    require(selection.diagnostics.local3DTilesExternalUris.contains(QStringLiteral("https://example.invalid/tiles/building.b3dm")),
            "3D Tiles diagnostics should list the remote URI");

    writeFile(root,
              QStringLiteral("data/maps/tiles3d/local/tileset.json"),
              QByteArrayLiteral(R"JSON({
  "asset": {"version": "1.1"},
  "geometricError": 500,
  "root": {
    "boundingVolume": {"region": [0, 0, 0.1, 0.1, 0, 100]},
    "geometricError": 250,
    "content": {"uri": "/outside/building.b3dm"}
  }
})JSON"));
    selection = select(root);
    require(!selection.diagnostics.local3DTilesTilesetValid,
            "3D Tiles tileset with an absolute URI should not pass local-only checks");
    require(selection.diagnostics.local3DTilesHasExternalUris,
            "3D Tiles tileset with an absolute URI should report external URI flag");
    require(selection.diagnostics.local3DTilesExternalUris.contains(QStringLiteral("/outside/building.b3dm")),
            "3D Tiles diagnostics should list the absolute URI");

    writeFile(root,
              QStringLiteral("data/maps/tiles3d/local/tileset.json"),
              QByteArrayLiteral(R"JSON({
  "asset": {"version": "1.1"},
  "geometricError": 500,
  "root": {
    "boundingVolume": {"region": [0, 0, 0.1, 0.1, 0, 100]},
    "geometricError": 250,
    "content": {"uri": "content/missing.b3dm"}
  }
})JSON"));
    selection = select(root);
    require(!selection.diagnostics.local3DTilesTilesetValid,
            "3D Tiles tileset with a missing local payload should not pass local-only checks");
    require(!selection.diagnostics.local3DTilesMissingResources.isEmpty(),
            "3D Tiles diagnostics should list missing local payload files");
    require(selection.diagnostics.local3DTilesMissingResources.join(QLatin1Char('\n')).contains(QStringLiteral("missing.b3dm")),
            "3D Tiles diagnostics should identify the missing payload filename");
    require(selection.diagnostics.local3DTilesDiagnostics.join(QLatin1Char('\n')).contains(QStringLiteral("referenced resource is missing")),
            "3D Tiles diagnostics should explain missing payload resources");

    writeFile(root,
              QStringLiteral("data/maps/tiles3d/local/tileset.json"),
              QByteArrayLiteral(R"JSON({
  "asset": {"version": "1.1"},
  "geometricError": 500,
  "root": {
    "boundingVolume": {"region": [0, 0, 0.1, 0.1, 0, 100]},
    "geometricError": 250
  }
})JSON"));
    selection = select(root);
    require(!selection.diagnostics.local3DTilesTilesetValid,
            "3D Tiles tileset without content URI should not pass local-only checks");
    require(selection.diagnostics.local3DTilesDiagnostics.join(QLatin1Char('\n')).contains(QStringLiteral("no content.uri entries")),
            "3D Tiles diagnostics should explain missing content URIs");

    touch(root, QStringLiteral("data/maps/vaporview_with_srtm.earth"));
    touch(root, QStringLiteral("data/maps/terrain/srtm/srtm.vrt"));
    selection = select(root);
    require(selection.mode == VaporView::Map3D::MapDataMode::NaturalEarthWithSrtm, "SRTM files should select SRTM mode");
    require(selection.diagnostics.srtmDemAvailable, "SRTM VRT should be marked available");
    require(selection.diagnostics.selectedDemLayerAvailable, "SRTM selection should select a DEM layer");
    require(selection.diagnostics.selectedElevationSource == QStringLiteral("SRTM"), "SRTM selection should report SRTM elevation source");
    require(selection.diagnostics.selectedBaseMode == VaporView::Map3D::MapDataMode::NaturalEarthWithSrtm,
            "SRTM selection should report SRTM as the selected base mode");
    require(selection.diagnostics.readinessSummary.contains(QStringLiteral("terrain-backed offline map")),
            "SRTM selection should summarize terrain-backed readiness");
    require(selection.diagnostics.readinessNextSteps.join(QLatin1Char('\n')).contains(QStringLiteral("OSM GeoPackages")),
            "terrain-backed readiness should suggest preparing OSM GeoPackages");
    require(selection.diagnostics.readinessNextSteps.join(QLatin1Char('\n')).contains(QStringLiteral("python scripts/prepare-osm-local-data.py data/maps/osm/local_extract.osm.pbf --overwrite")),
            "terrain-backed readiness should include the OSM conversion command");
    require(selection.diagnostics.readinessNextSteps.join(QLatin1Char('\n')).contains(QStringLiteral("python scripts/prepare-osm-local-data.py data/maps/osm/local_extract.osm.pbf --check")),
            "terrain-backed readiness should include the OSM validation command");

    touch(root, QStringLiteral("data/maps/vaporview_with_dem.earth"));
    touch(root, QStringLiteral("data/maps/terrain/copernicus_dem_glo30/copernicus_dem_glo30.vrt"));
    selection = select(root);
    require(selection.mode == VaporView::Map3D::MapDataMode::NaturalEarthWithCopernicusDem, "Copernicus files should outrank SRTM");
    require(selection.diagnostics.copernicusDemAvailable, "Copernicus VRT should be marked available");
    require(selection.diagnostics.selectedElevationSource == QStringLiteral("Copernicus DEM GLO-30"), "Copernicus selection should report Copernicus elevation source");
    require(selection.diagnostics.selectedBaseMode == VaporView::Map3D::MapDataMode::NaturalEarthWithCopernicusDem,
            "Copernicus selection should report Copernicus DEM as the selected base mode");

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
    require(selection.diagnostics.readinessSummary.contains(QStringLiteral("Ready for full offline local map")),
            "full local map should summarize complete local readiness");
    require(selection.diagnostics.readinessChecks.join(QLatin1Char('\n')).contains(QStringLiteral("OSM vector layers: ready (4/4)")),
            "full local readiness checks should report selected OSM readiness");
    require(selection.diagnostics.foundFiles.contains(selection.earthFile), "selected earth file should be listed as found");
    require(selection.diagnostics.foundFiles.contains(selection.diagnostics.naturalEarthVrtPath), "Natural Earth VRT should be listed as found");
    require(selection.diagnostics.osmVectorAvailable, "complete local data should mark OSM vectors available");
    require(selection.diagnostics.selectedOsmLayersAvailable, "complete local data should select OSM layers");
    require(selection.diagnostics.selectedOsmLayerCount == 4, "complete local data should report four selected OSM layers");
    require(selection.diagnostics.osmLayerContracts.join(QLatin1Char('\n')).contains(QStringLiteral("buildings.gpkg -> layer buildings")),
            "complete OSM diagnostics should retain the building layer contract");
    require(selection.diagnostics.osmLayerContracts.join(QLatin1Char('\n')).contains(QStringLiteral("TiledFeatureModel OSM building extrusion")),
            "complete OSM diagnostics should include the building extrusion contract");
    require(selection.diagnostics.missingOsmFiles.isEmpty(), "complete local data should not report missing OSM files");
    require(selection.diagnostics.fullLocalBlockers.isEmpty(), "complete local data should not report full-local blockers");
    require(selection.diagnostics.selectedFullLocalEarthPath.endsWith(QStringLiteral("vaporview_full_local.earth")),
            "complete Copernicus full local map should report selected full-local earth template");
    require(selection.diagnostics.selectedBaseMode == VaporView::Map3D::MapDataMode::NaturalEarthWithCopernicusDem,
            "complete Copernicus full local map should still report Copernicus DEM as the selected base mode");

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
    require(selection.diagnostics.selectedBaseMode == VaporView::Map3D::MapDataMode::NaturalEarthWithSrtm,
            "SRTM-only full local map should still report SRTM as the selected base mode");

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
