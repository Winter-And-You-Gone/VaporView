# VaporView Offline Map Data

VaporView keeps map data local and offline. Do not add online commercial map services, Cesium ion, or API-key based imagery providers to the default map setup.

See also:

- `docs/osgearth_setup.md` for osgEarth, OSG plugin, GDAL, and PROJ setup.
- `docs/map3d_usage.md` for 3D Map window usage and diagnostics.

## Natural Earth Background

The default offline 3D map background uses Natural Earth II 1:50m shaded relief with water.

- Source: `https://naturalearth.s3.amazonaws.com/50m_raster/NE2_50M_SR_W.zip`
- License/status: Natural Earth data is public domain.
- Local earth file: `data/maps/vaporview_default.earth`
- Raster file after download: `data/maps/natural_earth/NE2_50M_SR_W/NE2_50M_SR_W.tif`
- VRT used by osgEarth: `data/maps/natural_earth/NE2_50M_SR_W/NE2_50M_SR_W.vrt`
- Preview texture used by the manual fallback globe: `data/maps/natural_earth/NE2_50M_SR_W/NE2_50M_SR_W_2048.png`

Natural Earth is an offline visual background layer only. It is not DEM data, does not contain real terrain elevation, and must not be used as a height source for flight clearance or altitude analysis.

Download or refresh the local copy:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/download-natural-earth-map.ps1
```

## Local Terrain DEM

Local terrain elevation is optional and lives under `data/maps/terrain/`. The preferred source is Copernicus DEM GLO-30 GeoTIFF tiles. SRTM GeoTIFF tiles are the backup source when Copernicus data is unavailable.

Recommended directory layout:

```text
data/maps/terrain/
  copernicus_dem_glo30/
    *.tif
    copernicus_dem_glo30.vrt
  srtm/
    *.tif
    srtm.vrt
```

Build a VRT from local Copernicus GeoTIFF tiles:

```powershell
gdalbuildvrt data/maps/terrain/copernicus_dem_glo30/copernicus_dem_glo30.vrt data/maps/terrain/copernicus_dem_glo30/*.tif
```

Or use the repository helper, which only reads local files and does not download DEM data:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/prepare-demo-dem.ps1
python scripts/prepare-demo-dem.py
```

The helper searches for GDAL command-line tools in `PATH`, `GDAL_BIN`, the
project-local `.local_deps/vcpkg_installed/x64-windows/tools/gdal` and `bin`
folders, and common Windows OSGeo4W/QGIS install folders. If your GDAL tools
are somewhere else, pass the directory explicitly:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/prepare-demo-dem.ps1 -GdalBin C:\OSGeo4W\bin
python scripts/prepare-demo-dem.py --gdal-bin C:\OSGeo4W\bin
```

If the GeoTIFF tiles are staged outside the canonical project directory, pass that input directory explicitly. The helper still writes the generated VRT to `data/maps/terrain/copernicus_dem_glo30/copernicus_dem_glo30.vrt` or `data/maps/terrain/srtm/srtm.vrt`, because those are the paths that `MapDataManager` and the built-in `.earth` templates auto-load:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/prepare-demo-dem.ps1 -DemDir X:\DEM\copernicus_tiles
python scripts/prepare-demo-dem.py --dem-dir X:\DEM\copernicus_tiles
```

Build a VRT from local SRTM GeoTIFF tiles:

```powershell
gdalbuildvrt data/maps/terrain/srtm/srtm.vrt data/maps/terrain/srtm/*.tif
```

The helper can prepare the SRTM fallback VRT too:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/prepare-demo-dem.ps1 -Srtm
python scripts/prepare-demo-dem.py --srtm
```

Use `--check` to validate local DEM inputs and the generated VRT without rebuilding it:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/prepare-demo-dem.ps1 -Check
python scripts/prepare-demo-dem.py --check
powershell -ExecutionPolicy Bypass -File scripts/prepare-demo-dem.ps1 -Srtm -Check
python scripts/prepare-demo-dem.py --srtm --check
```

The Copernicus DEM-enabled template is `data/maps/vaporview_with_dem.earth`. The SRTM fallback template is `data/maps/vaporview_with_srtm.earth`. Both keep the Natural Earth visual background and add a `GDALElevation` layer for local terrain.

Height reference note: RTK/GNSS samples may report WGS84 ellipsoid height, mean sea level height, EGM2008-related orthometric height, or a local NED height. Copernicus DEM and SRTM elevations are terrain datasets with their own vertical datum assumptions. VaporView currently labels the `NavSample::heightReference` value for display and diagnostics only; it does not perform terrain clearance or AGL safety decisions while the RTK and DEM height references are unchecked.

VaporView automatic map selection order:

1. Full local map when Natural Earth, one DEM VRT, and all required local OSM GeoPackages exist:
   - `vaporview_full_local.earth` when Copernicus DEM is available.
   - `vaporview_full_local_srtm.earth` when only the SRTM fallback DEM is available.
2. `vaporview_with_dem.earth` when Natural Earth and `data/maps/terrain/copernicus_dem_glo30/copernicus_dem_glo30.vrt` exist.
3. `vaporview_with_srtm.earth` when Natural Earth and `data/maps/terrain/srtm/srtm.vrt` exist.
4. `vaporview_default.earth` when Natural Earth exists but no DEM VRT exists.
5. Local grid only when no complete local Natural Earth dataset exists.

If no DEM VRT exists, the 3D map still uses Natural Earth plus the local grid fallback.

The 3D Map diagnostics panel also includes a readiness summary. Read it as the current local data capability tier, not as a legal or quality certification:

- `Local grid fallback only` means Natural Earth is incomplete; prepare `data/maps/natural_earth/NE2_50M_SR_W/NE2_50M_SR_W.vrt` and `.tif` first.
- `Ready for offline visual background only` means Natural Earth is loaded but no Copernicus DEM or SRTM VRT is selected.
- `Ready for terrain-backed offline map` means Natural Earth plus a DEM VRT is selected; generate the four OSM GeoPackages to unlock `Full local map`.
- `Ready for full offline local map` means Natural Earth, one DEM source, and all required OSM GeoPackages are selected. Optional imagery overlays and local 3D Tiles may still be absent.

## Local OSM Vector Data

Local OSM vector data is optional and lives under `data/maps/osm/`. This path is for open, offline files only. Do not add online tile services, API keys, Cesium ion, or commercial map providers.

Expected generated files:

```text
data/maps/osm/
  roads.gpkg
  water.gpkg
  buildings.gpkg
  places.gpkg
```

Prepare those files from a local `.osm.pbf` or `.osm` extract with GDAL/OGR:

```powershell
python scripts/prepare-osm-local-data.py data/maps/osm/local_extract.osm.pbf --overwrite
```

The helper does not download data. It runs `ogr2ogr` locally and writes four GeoPackage files. It searches for GDAL/OGR tools in `PATH`, `GDAL_BIN`, the project-local `.local_deps/vcpkg_installed/x64-windows/tools/gdal` and `bin` folders, and common Windows OSGeo4W/QGIS install folders. If your tools are elsewhere, pass `--gdal-bin C:\path\to\gdal\bin`. The generated GeoPackage layer names are `roads`, `water`, `buildings`, and `places`. The `buildings` layer also gets a normalized `extrusion_height_m` field derived from OSM `height`, `building:height`, `building:levels`, or `levels`; missing values fall back to 10 m. Once Natural Earth, one DEM VRT, and all four OSM GeoPackages exist, `MapDataManager` selects `FullLocalMap`. It loads `data/maps/vaporview_full_local.earth` for Copernicus DEM, or `data/maps/vaporview_full_local_srtm.earth` when SRTM is the only available DEM.

Validate the generated files and layer names without reconverting:

```powershell
python scripts/prepare-osm-local-data.py data/maps/osm/local_extract.osm.pbf --check
python scripts/prepare-osm-local-data.py data/maps/osm/local_extract.osm.pbf --gdal-bin C:\OSGeo4W\bin --check
```

If `ogrinfo` is unavailable, `--check` still reports missing files, but layer-name validation requires GDAL/OGR.

The current full-local earth template renders local OSM context offline:

- water polygons as a blue draped `FeatureImage`
- roads as a yellow draped `FeatureImage`
- building footprints as a tan draped `FeatureImage`
- building polygons as a coarse fixed-height `TiledFeatureModel` extrusion
- place names as a `TiledFeatureModel` label layer

The 3D Map diagnostics also prints the expected OSM layer contract for each file: `roads.gpkg -> layer roads -> OGRFeatures osm-roads -> FeatureImage OSM roads`, `water.gpkg -> layer water -> OGRFeatures osm-water -> FeatureImage OSM water fill`, `buildings.gpkg -> layer buildings -> OGRFeatures osm-buildings -> FeatureImage OSM building footprints + TiledFeatureModel OSM building extrusion`, and `places.gpkg -> layer places -> OGRFeatures osm-places -> TiledFeatureModel OSM place labels`. If a GeoPackage exists but the vector layer does not render, compare this contract with `ogrinfo -ro -so data/maps/osm/<file>.gpkg <layer>` and the matching `.earth` feature name. `scripts/prepare-osm-local-data.py --check` performs that layer-name check when `ogrinfo` is available, and it additionally verifies that `buildings.gpkg` exposes the `extrusion_height_m` field required by the local building-extrusion style.

The building extrusion reads `extrusion_height_m` from the generated `buildings.gpkg` layer and clamps the result to at least 10 m. The helper derives that field from local OSM height or level tags when present, and otherwise writes the 10 m fallback. This gives local 3D context without relying on online tiles or proprietary building-height sources; it is still not a surveyed building-height model.

## Optional Local High-Resolution Imagery

Optional local imagery is reserved under `data/maps/imagery/`. VaporView does not download imagery and does not use commercial online imagery services by default.

Expected VRT entry points:

```text
data/maps/imagery/sentinel2/sentinel2.vrt
data/maps/imagery/landsat/landsat.vrt
data/maps/imagery/openaerialmap/openaerialmap.vrt
```

Manual local imagery earth templates:

```text
data/maps/vaporview_with_sentinel2_imagery.earth
data/maps/vaporview_with_landsat_imagery.earth
data/maps/vaporview_with_openaerialmap_imagery.earth
```

Build each VRT from local GeoTIFF files with the repository helper:

```powershell
python scripts/prepare-local-imagery.py sentinel2
python scripts/prepare-local-imagery.py landsat
python scripts/prepare-local-imagery.py openaerialmap
```

If the GeoTIFF files are staged outside the canonical project directory, pass that input directory explicitly. The generated VRT is still written to the canonical `data/maps/imagery/<slot>/<slot>.vrt` path so `MapDataManager` and the 3D Map `本地影像` toolbar menu can auto-detect it:

```powershell
python scripts/prepare-local-imagery.py sentinel2 --imagery-dir X:\Imagery\sentinel2_tiles
python scripts/prepare-local-imagery.py landsat --imagery-dir X:\Imagery\landsat_tiles
python scripts/prepare-local-imagery.py openaerialmap --imagery-dir X:\Imagery\openaerialmap_tiles
```

Use `--check` to validate local imagery inputs, GDAL availability, the generated VRT, and the matching `.earth` template without rebuilding:

```powershell
python scripts/prepare-local-imagery.py sentinel2 --check
python scripts/prepare-local-imagery.py landsat --check
python scripts/prepare-local-imagery.py openaerialmap --check
```

The helper only reads local GeoTIFF files. It does not download imagery and does not contact online map services. It searches for GDAL command-line tools in the same locations as the DEM and OSM helpers, including `PATH`, `GDAL_BIN`, the project-local `.local_deps/vcpkg_installed/x64-windows/tools/gdal` and `bin` folders, and common Windows OSGeo4W/QGIS install folders. If your GDAL tools are elsewhere, pass `--gdal-bin C:\path\to\gdal\bin`.

`MapDataManager` scans these VRTs and shows them in the 3D Map diagnostics. Optional imagery does not change the automatic base map mode. The diagnostics distinguish VRTs found from menu-ready overlays: a toolbar entry is enabled only when both the VRT and the matching `.earth` template exist. After preparing a VRT, use the 3D Map toolbar `本地影像` menu to load the matching imagery `.earth` template. The templates keep Natural Earth as the offline global background and add one local imagery overlay.

## Optional Local 3D Tiles

Local 3D Tiles live under `data/maps/tiles3d/` for offline 3D content experiments. This path is local-only and does not use Cesium ion.

Expected entry point:

```text
data/maps/tiles3d/local/tileset.json
```

Put the complete local 3D Tiles dataset under `data/maps/tiles3d/local/`. `MapDataManager` scans the `tileset.json` path and reports it in diagnostics. The 3D Map toolbar exposes a `本地 3D Tiles` preview action when the local-only contract is valid. That action attempts to load the tileset as an independent OSG overlay, without replacing the Natural Earth/DEM/OSM base map and without affecting track or aircraft layers.

Rendering success depends on the installed OSG/osgEarth runtime plugin support for the tileset payloads. If `osgDB::readNodeFile` cannot load the dataset, VaporView keeps the base map running and reports the failure in the diagnostics panel. Treat this as a diagnostic-backed preview entry point, not as a Cesium ion or web-based 3D Tiles pipeline.

The diagnostics currently check:

- `tileset.json` is parseable JSON.
- `asset.version`, `root`, `root.boundingVolume`, and `geometricError` are present.
- at least one local `content.uri`, `content.url`, or `contents[].uri` payload reference exists.
- `content.uri`, `content.url`, and `contents[].uri` references stay relative to `data/maps/tiles3d/local/`.
- referenced local payload files exist.
- `http://`, `https://`, protocol-relative, absolute, or other non-portable URI references are flagged.

## Real DEM Verification Flow

1. Place Copernicus DEM GLO-30 GeoTIFF tiles in `data/maps/terrain/copernicus_dem_glo30/`.
2. Run `powershell -ExecutionPolicy Bypass -File scripts/prepare-demo-dem.ps1` or `python scripts/prepare-demo-dem.py`.
3. Confirm `data/maps/terrain/copernicus_dem_glo30/copernicus_dem_glo30.vrt` exists.
4. Build and start VaporView with `-DVAPORVIEW_ENABLE_OSGEARTH=ON`.
5. Open the 3D Map window and click `地图诊断`.
6. Confirm the mode is `Natural Earth + Copernicus DEM` and the loaded earth file is `vaporview_with_dem.earth`.

For SRTM fallback validation, place SRTM GeoTIFF tiles in `data/maps/terrain/srtm/`, run the helper with `-Srtm`, and confirm the diagnostics mode is `Natural Earth + SRTM DEM`.

## Full Local Map Verification Flow

1. Prepare Natural Earth with `powershell -ExecutionPolicy Bypass -File scripts/download-natural-earth-map.ps1`.
2. Place Copernicus DEM or SRTM GeoTIFF tiles under `data/maps/terrain/`.
3. Build the DEM VRT with `scripts/prepare-demo-dem.ps1` or `scripts/prepare-demo-dem.py`.
4. Place a local open OSM extract under `data/maps/osm/`.
5. Run `python scripts/prepare-osm-local-data.py data/maps/osm/local_extract.osm.pbf --overwrite`.
6. Run `python scripts/prepare-osm-local-data.py data/maps/osm/local_extract.osm.pbf --check`.
7. Build and start VaporView with `-DVAPORVIEW_ENABLE_OSGEARTH=ON`.
8. Open the 3D Map window and click `地图诊断`.
9. Confirm the mode is `Full local map` and the loaded earth file is `vaporview_full_local.earth` for Copernicus DEM or `vaporview_full_local_srtm.earth` for SRTM-only fallback.
10. Zoom into the OSM extract area and confirm local water, roads, building footprints, coarse building extrusion, and place labels are visible.

## Optional Imagery And 3D Tiles Diagnostics Flow

1. Place local GeoTIFF imagery under one of `data/maps/imagery/sentinel2/`, `data/maps/imagery/landsat/`, or `data/maps/imagery/openaerialmap/`.
2. Build the matching VRT with `python scripts/prepare-local-imagery.py sentinel2`, `landsat`, or `openaerialmap`.
3. Optionally place a local 3D Tiles dataset under `data/maps/tiles3d/local/` with `tileset.json` at the root.
4. Build and start VaporView with `-DVAPORVIEW_ENABLE_OSGEARTH=ON`.
5. Open the 3D Map window and click `地图诊断`.
6. Confirm optional local imagery and optional local 3D Tiles availability are reported. These optional files should not change the selected base map mode.
7. To view local imagery, click `本地影像` and select the enabled Sentinel-2, Landsat, or OpenAerialMap entry.
8. For local 3D Tiles, confirm the diagnostics show `Local 3D Tiles contract: valid`, then click `本地 3D Tiles` to attempt a local preview overlay.
9. If the preview fails, check `Local 3D Tiles preview load` in diagnostics. A failure such as `osgDB::readNodeFile returned null` usually means the current OSG/osgEarth runtime lacks the needed 3D Tiles/plugin support for that dataset.

## Git Tracking

The `data/` directory ignores large downloaded rasters, DEM tiles, imagery, OSM extracts, GeoPackages, 3D Tiles payloads, and archives. Git tracks only small map templates, VRT metadata, README files, and scripts needed to recreate the local map setup.
