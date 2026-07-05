# VaporView Offline Map Data

VaporView keeps map data local and offline. Do not add online commercial map services, Cesium ion, or API-key based imagery providers to the default map setup.

## Natural Earth Background

The default offline 3D map background uses Natural Earth II 1:50m shaded relief with water.

- Source: `https://naturalearth.s3.amazonaws.com/50m_raster/NE2_50M_SR_W.zip`
- License/status: Natural Earth data is public domain.
- Local earth file: `data/maps/vaporview_default.earth`
- Raster file after download: `data/maps/natural_earth/NE2_50M_SR_W/NE2_50M_SR_W.tif`

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
python scripts/prepare-demo-dem.py --check
python scripts/prepare-demo-dem.py --srtm --check
```

The Copernicus DEM-enabled template is `data/maps/vaporview_with_dem.earth`. The SRTM fallback template is `data/maps/vaporview_with_srtm.earth`. Both keep the Natural Earth visual background and add a `GDALElevation` layer for local terrain.

VaporView auto-load order:

1. `vaporview_full_local.earth` when Natural Earth, a DEM VRT, and all required local OSM GeoPackages exist.
2. `vaporview_with_dem.earth` when `data/maps/terrain/copernicus_dem_glo30/copernicus_dem_glo30.vrt` exists.
3. `vaporview_with_srtm.earth` when `data/maps/terrain/srtm/srtm.vrt` exists.
4. `vaporview_default.earth` when no DEM VRT exists.

If no DEM VRT exists, the 3D map still uses Natural Earth plus the local grid fallback.

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

The helper does not download data. It runs `ogr2ogr` locally and writes four GeoPackage files. Once Natural Earth, one DEM VRT, and all four OSM GeoPackages exist, `MapDataManager` selects `FullLocalMap` and loads `data/maps/vaporview_full_local.earth`.

The current full-local earth template declares the local OSM GeoPackage sources. Detailed OSM vector styling is intentionally a later step after the local data pipeline is stable.

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
6. Build and start VaporView with `-DVAPORVIEW_ENABLE_OSGEARTH=ON`.
7. Open the 3D Map window and click `地图诊断`.
8. Confirm the mode is `Full local map` and the loaded earth file is `vaporview_full_local.earth`.

## Git Tracking

The `data/` directory ignores large downloaded rasters, DEM tiles, and archives. Git tracks only small map templates, VRT metadata, README files, and scripts needed to recreate the local map setup.
