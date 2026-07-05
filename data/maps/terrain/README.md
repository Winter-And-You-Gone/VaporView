# VaporView Local Terrain Data

Place offline DEM files under this directory. The files are intentionally not tracked by Git.

Preferred layout:

```text
data/maps/terrain/
  copernicus_dem_glo30/
    *.tif
    copernicus_dem_glo30.vrt
  srtm/
    *.tif
    srtm.vrt
```

VaporView prefers Copernicus DEM GLO-30 GeoTIFF tiles. SRTM GeoTIFF tiles are the fallback when Copernicus data is unavailable.

Build a VRT after placing GeoTIFF tiles:

```powershell
gdalbuildvrt data/maps/terrain/copernicus_dem_glo30/copernicus_dem_glo30.vrt data/maps/terrain/copernicus_dem_glo30/*.tif
```

The default Copernicus DEM earth template is `data/maps/vaporview_with_dem.earth`. The SRTM fallback template is `data/maps/vaporview_with_srtm.earth`.

Application auto-load order:

1. `vaporview_with_dem.earth` when `terrain/copernicus_dem_glo30/copernicus_dem_glo30.vrt` exists.
2. `vaporview_with_srtm.earth` when `terrain/srtm/srtm.vrt` exists.
3. `vaporview_default.earth` when no DEM VRT exists, keeping Natural Earth plus the local grid fallback.
