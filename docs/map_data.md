# VaporView Offline Map Data

VaporView keeps map data local and offline. Do not add online commercial map services, Cesium ion, or API-key based imagery providers to the default map setup.

See also:

- `docs/osgearth_setup.md` for osgEarth, OSG plugin, GDAL, and PROJ setup.
- `docs/map3d_usage.md` for 3D Map window usage and diagnostics.

## Natural Earth Background

The default offline 3D map background uses Natural Earth II 1:50m shaded relief with water.

- Source: `https://naturalearth.s3.amazonaws.com/50m_raster/NE2_50M_SR_W.zip`
- License/status: Natural Earth data is public domain.
- Local earth file: `resources/maps/vaporview_default.earth`
- Raster file after download: `resources/maps/natural_earth/NE2_50M_SR_W/NE2_50M_SR_W.tif`
- VRT used by osgEarth: `resources/maps/natural_earth/NE2_50M_SR_W/NE2_50M_SR_W.vrt`
- Preview texture used by the manual fallback globe: `resources/maps/natural_earth/NE2_50M_SR_W/NE2_50M_SR_W_2048.png`

Natural Earth is an offline visual background layer only. It is not DEM data, does not contain real terrain elevation, and must not be used as a height source for flight clearance or altitude analysis.

Download or refresh the local copy:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/download-natural-earth-map.ps1
```

## Local Terrain DEM

Local terrain elevation is optional and lives under `resources/maps/terrain/`. The preferred source is Copernicus DEM GLO-30 GeoTIFF tiles. SRTM GeoTIFF tiles are the backup source when Copernicus data is unavailable.

`MapDataManager` selects the best local base map stack in this order: Copernicus DEM, then SRTM, then Natural Earth, then the built-in local grid. When all OSM GeoPackages are also present, the selected window mode becomes `Full local map`, but diagnostics still show the underlying selected base mode so DEM priority remains visible and auditable.

Recommended directory layout:

```text
resources/maps/terrain/
  copernicus_dem_glo30/
    *.tif
    copernicus_dem_glo30.vrt
  srtm/
    *.tif
    srtm.vrt
```

Build a VRT from local Copernicus GeoTIFF tiles:

```powershell
gdalbuildvrt resources/maps/terrain/copernicus_dem_glo30/copernicus_dem_glo30.vrt resources/maps/terrain/copernicus_dem_glo30/*.tif
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

If the GeoTIFF tiles are staged outside the canonical project directory, pass that input directory explicitly. The helper still writes the generated VRT to `resources/maps/terrain/copernicus_dem_glo30/copernicus_dem_glo30.vrt` or `resources/maps/terrain/srtm/srtm.vrt`, because those are the paths that `MapDataManager` and the built-in `.earth` templates auto-load:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/prepare-demo-dem.ps1 -DemDir X:\DEM\copernicus_tiles
python scripts/prepare-demo-dem.py --dem-dir X:\DEM\copernicus_tiles
```

Build a VRT from local SRTM GeoTIFF tiles:

```powershell
gdalbuildvrt resources/maps/terrain/srtm/srtm.vrt resources/maps/terrain/srtm/*.tif
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

The Copernicus DEM-enabled template is `resources/maps/vaporview_with_dem.earth`. The SRTM fallback template is `resources/maps/vaporview_with_srtm.earth`. Both keep the Natural Earth visual background and add a `GDALElevation` layer for local terrain.

Height reference note: EPSILON geodetic height is treated as WGS84 ellipsoid height, matching the device manual. Session samples with an explicit non-ellipsoid reference use their recorded ECEF coordinates when available; otherwise diagnostics report that a WGS84 display fallback is active. Copernicus DEM and SRTM retain their own vertical-datum assumptions, so the 3D view does not make AGL or terrain-clearance safety decisions.

VaporView automatic map selection first chooses the best available terrain/background base, then upgrades that same base to a full-local OSM template when all required OSM GeoPackages are present:

1. Copernicus DEM base when Natural Earth and `resources/maps/terrain/copernicus_dem_glo30/copernicus_dem_glo30.vrt` exist:
   - `vaporview_full_local.earth` when all required OSM GeoPackages also exist.
   - `vaporview_with_dem.earth` when OSM is incomplete or absent.
2. SRTM fallback base when Natural Earth and `resources/maps/terrain/srtm/srtm.vrt` exist and Copernicus DEM is unavailable:
   - `vaporview_full_local_srtm.earth` when all required OSM GeoPackages also exist.
   - `vaporview_with_srtm.earth` when OSM is incomplete or absent.
3. `vaporview_default.earth` when Natural Earth exists but no DEM VRT exists.
4. Local grid only when no complete local Natural Earth dataset exists.

Across multiple candidate `resources/maps` roots, Copernicus DEM still outranks a complete SRTM full-local root. Diagnostics show `Selected base mode` separately from the final `Full local map` window mode so that this DEM priority remains auditable.

If no DEM VRT exists, the 3D map still uses Natural Earth plus the local grid fallback.

The 3D Map diagnostics panel also includes a readiness summary. Read it as the current local data capability tier, not as a legal or quality certification:

- `Local grid fallback only` means Natural Earth is incomplete; prepare `resources/maps/natural_earth/NE2_50M_SR_W/NE2_50M_SR_W.vrt` and `.tif` first.
- `Ready for offline visual background only` means Natural Earth is loaded but no Copernicus DEM or SRTM VRT is selected.
- `Ready for terrain-backed offline map` means Natural Earth plus a DEM VRT is selected; generate the four OSM GeoPackages to unlock `Full local map`.
- `Ready for full offline local map` means Natural Earth, one DEM source, and all required OSM GeoPackages are selected. Optional imagery overlays and local 3D Tiles may still be absent.

## Local OSM Vector Data

Local OSM vector data is optional and lives under `resources/maps/osm/`. This path is for open, offline files only. Do not add online tile services, API keys, Cesium ion, or commercial map providers.

Expected generated files:

```text
resources/maps/osm/
  roads.gpkg
  water.gpkg
  buildings.gpkg
  places.gpkg
```

Prepare those files from a local `.osm.pbf` or `.osm` extract with GDAL/OGR:

```powershell
python scripts/prepare-osm-local-data.py resources/maps/osm/local_extract.osm.pbf --overwrite
```

The helper does not download data. It runs `ogr2ogr` locally and writes four GeoPackage files. It searches for GDAL/OGR tools in `PATH`, `GDAL_BIN`, the project-local `.local_deps/vcpkg_installed/x64-windows/tools/gdal` and `bin` folders, and common Windows OSGeo4W/QGIS install folders. If your tools are elsewhere, pass `--gdal-bin C:\path\to\gdal\bin`. The generated GeoPackage layer names are `roads`, `water`, `buildings`, and `places`. The `buildings` layer also gets a normalized `extrusion_height_m` field derived from OSM `height`, `building:height`, `building:levels`, or `levels`; missing values fall back to 10 m. Once Natural Earth, one DEM VRT, and all four OSM GeoPackages exist, `MapDataManager` selects `FullLocalMap`. It loads `resources/maps/vaporview_full_local.earth` for Copernicus DEM, or `resources/maps/vaporview_full_local_srtm.earth` when SRTM is the only available DEM.

Validate the generated files and layer names without reconverting:

```powershell
python scripts/prepare-osm-local-data.py resources/maps/osm/local_extract.osm.pbf --check
python scripts/prepare-osm-local-data.py resources/maps/osm/local_extract.osm.pbf --gdal-bin C:\OSGeo4W\bin --check
```

If `ogrinfo` is unavailable, `--check` still reports missing files, but layer-name validation requires GDAL/OGR.

The current full-local earth template renders local OSM context offline in a safe default mode:

- water polygons as a blue draped `FeatureImage`
- roads as a yellow draped `FeatureImage`
- building GeoPackages as prepared local data only, not rendered by default
- place GeoPackages as prepared local data only, not rendered by default

The 3D Map diagnostics also prints the expected OSM layer contract for each file: `roads.gpkg -> layer roads -> OGRFeatures osm-roads -> FeatureImage OSM roads`, `water.gpkg -> layer water -> OGRFeatures osm-water -> FeatureImage OSM water fill`, `buildings.gpkg -> layer buildings -> generated data only; not rendered by the safe default full-local earth template`, and `places.gpkg -> layer places -> generated data only; not rendered by the safe default full-local earth template`. If a GeoPackage exists but a safe rendered layer does not appear, compare this contract with `ogrinfo -ro -so resources/maps/osm/<file>.gpkg <layer>` and the matching `.earth` feature name. `scripts/prepare-osm-local-data.py --check` performs that layer-name check when `ogrinfo` is available, and it additionally verifies that `buildings.gpkg` exposes the `extrusion_height_m` field for later opt-in building rendering.

The helper still writes `extrusion_height_m` into the generated `buildings.gpkg` layer from local OSM height or level tags when present, and otherwise writes the 10 m fallback. The automatic full-local `.earth` templates do not render building extrusion or place labels by default because full-country OSM extracts can create missing-font boxes for CJK labels and can stall osgEarth while zooming.

## Optional Local High-Resolution Imagery

Optional local imagery is reserved under `resources/maps/imagery/`. VaporView does not download imagery and does not use commercial online imagery services by default.

Expected VRT entry points:

```text
resources/maps/imagery/sentinel2/sentinel2.vrt
resources/maps/imagery/landsat/landsat.vrt
resources/maps/imagery/openaerialmap/openaerialmap.vrt
```

Manual local imagery earth templates:

```text
resources/maps/vaporview_with_sentinel2_imagery.earth
resources/maps/vaporview_with_landsat_imagery.earth
resources/maps/vaporview_with_openaerialmap_imagery.earth
```

Build each VRT from local GeoTIFF files with the repository helper:

```powershell
python scripts/prepare-local-imagery.py sentinel2
python scripts/prepare-local-imagery.py landsat
python scripts/prepare-local-imagery.py openaerialmap
```

If the GeoTIFF files are staged outside the canonical project directory, pass that input directory explicitly. The generated VRT is still written to the canonical `resources/maps/imagery/<slot>/<slot>.vrt` path so `MapDataManager` and the 3D Map `本地影像` toolbar menu can auto-detect it:

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

Local 3D Tiles live under `resources/maps/tiles3d/` for offline 3D content experiments. This path is local-only and does not use Cesium ion.

Expected entry point:

```text
resources/maps/tiles3d/local/tileset.json
```

Put the complete VaporView native OSG building dataset under `resources/maps/tiles3d/local/`. `MapDataManager` scans the `tileset.json` path and requires `extras.format` to equal `vaporview-osg-native-building-tiles`. The 3D Map toolbar exposes a `本地 OSG 建筑` action when that local-only contract is valid. It loads the native OSG payloads as an independent overlay, without replacing the Natural Earth/DEM/OSM base map or affecting track and aircraft layers.

This is not a general Cesium 3D Tiles renderer: tileset transforms, runtime LOD selection, remote URLs, and b3dm/i3dm/pnts decoding are not supported. Rendering depends on the installed OSG/osgEarth runtime plugin support for the native payloads. If any payload cannot be loaded, VaporView preserves the previous building overlay and reports the atomic-load failure in diagnostics.

The diagnostics currently check:

- `tileset.json` is parseable JSON.
- `asset.version`, `root`, `root.boundingVolume`, and `geometricError` are present.
- at least one local `content.uri`, `content.url`, or `contents[].uri` payload reference exists.
- `content.uri`, `content.url`, and `contents[].uri` references stay relative to `resources/maps/tiles3d/local/`.
- referenced local payload files exist.
- `http://`, `https://`, protocol-relative, absolute, or other non-portable URI references are flagged.

## Real DEM Verification Flow

1. Place Copernicus DEM GLO-30 GeoTIFF tiles in `resources/maps/terrain/copernicus_dem_glo30/`.
2. Run `powershell -ExecutionPolicy Bypass -File scripts/prepare-demo-dem.ps1` or `python scripts/prepare-demo-dem.py`.
3. Confirm `resources/maps/terrain/copernicus_dem_glo30/copernicus_dem_glo30.vrt` exists.
4. Build and start VaporView with `-DVAPORVIEW_ENABLE_OSGEARTH=ON`.
5. Open the 3D Map window and click `地图诊断`.
6. Confirm the mode is `Natural Earth + Copernicus DEM` and the loaded earth file is `vaporview_with_dem.earth`.

For SRTM fallback validation, place SRTM GeoTIFF tiles in `resources/maps/terrain/srtm/`, run the helper with `-Srtm`, and confirm the diagnostics mode is `Natural Earth + SRTM DEM`.

## Full Local Map Verification Flow

1. Prepare Natural Earth with `powershell -ExecutionPolicy Bypass -File scripts/download-natural-earth-map.ps1`.
2. Place Copernicus DEM or SRTM GeoTIFF tiles under `resources/maps/terrain/`.
3. Build the DEM VRT with `scripts/prepare-demo-dem.ps1` or `scripts/prepare-demo-dem.py`.
4. Place a local open OSM extract under `resources/maps/osm/`.
5. Run `python scripts/prepare-osm-local-data.py resources/maps/osm/local_extract.osm.pbf --overwrite`.
6. Run `python scripts/prepare-osm-local-data.py resources/maps/osm/local_extract.osm.pbf --check`.
7. Build and start VaporView with `-DVAPORVIEW_ENABLE_OSGEARTH=ON`.
8. Open the 3D Map window and click `地图诊断`.
9. Confirm the mode is `Full local map` and the loaded earth file is `vaporview_full_local.earth` for Copernicus DEM or `vaporview_full_local_srtm.earth` for SRTM-only fallback.
10. Zoom into the OSM extract area and confirm local water and roads are visible. Building and place GeoPackages should appear in diagnostics as prepared data, but they are not rendered by the safe default template.

## Optional Imagery And 3D Tiles Diagnostics Flow

1. Place local GeoTIFF imagery under one of `resources/maps/imagery/sentinel2/`, `resources/maps/imagery/landsat/`, or `resources/maps/imagery/openaerialmap/`.
2. Build the matching VRT with `python scripts/prepare-local-imagery.py sentinel2`, `landsat`, or `openaerialmap`.
3. Optionally place a local 3D Tiles dataset under `resources/maps/tiles3d/local/` with `tileset.json` at the root.
4. Build and start VaporView with `-DVAPORVIEW_ENABLE_OSGEARTH=ON`.
5. Open the 3D Map window and click `地图诊断`.
6. Confirm optional local imagery and optional local 3D Tiles availability are reported. These optional files should not change the selected base map mode.
7. To view local imagery, click `本地影像` and select the enabled Sentinel-2, Landsat, or OpenAerialMap entry.
8. For local 3D Tiles, confirm the diagnostics show `Local 3D Tiles contract: valid`, then click `本地 3D Tiles` to attempt a local preview overlay.
9. If the preview fails, check `Local 3D Tiles preview load` in diagnostics. A failure such as `osgDB::readNodeFile returned null` usually means the current OSG/osgEarth runtime lacks the needed 3D Tiles/plugin support for that dataset.

## Git Tracking

The `data/` directory ignores large downloaded rasters, DEM tiles, imagery, OSM extracts, GeoPackages, 3D Tiles payloads, and archives. Git tracks only small map templates, VRT metadata, README files, and scripts needed to recreate the local map setup.
