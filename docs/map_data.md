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

Build a VRT from local SRTM GeoTIFF tiles:

```powershell
gdalbuildvrt data/maps/terrain/srtm/srtm.vrt data/maps/terrain/srtm/*.tif
```

The Copernicus DEM-enabled template is `data/maps/vaporview_with_dem.earth`. The SRTM fallback template is `data/maps/vaporview_with_srtm.earth`. Both keep the Natural Earth visual background and add a `GDALElevation` layer for local terrain.

VaporView auto-load order:

1. `vaporview_with_dem.earth` when `data/maps/terrain/copernicus_dem_glo30/copernicus_dem_glo30.vrt` exists.
2. `vaporview_with_srtm.earth` when `data/maps/terrain/srtm/srtm.vrt` exists.
3. `vaporview_default.earth` when no DEM VRT exists.

If no DEM VRT exists, the 3D map still uses Natural Earth plus the local grid fallback.

## Git Tracking

The `data/` directory ignores large downloaded rasters, DEM tiles, and archives. Git tracks only small map templates, VRT metadata, README files, and scripts needed to recreate the local map setup.
