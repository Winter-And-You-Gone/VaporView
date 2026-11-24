# VaporView Local Terrain Data

Place offline DEM files under this directory. The files are intentionally not tracked by Git.

Preferred layout:

```text
resources/maps/terrain/
  copernicus_dem_glo30/
    *.tif
    copernicus_dem_glo30.vrt
  srtm/
    *.tif
    srtm.vrt
```

The built-in 3D maps sample local Copernicus DEM GLO-30 first, then local SRTM. To fill the remaining gaps with Cesium World Terrain, set the environment variable `VAPORVIEW_CESIUM_ION_TOKEN` to an ion token with read access to asset 1 before starting VaporView. Windows installations also accept the current user's `VaporView/Map3D` setting `cesium/ion_token`.

The first tile outside local coverage triggers ion authentication. The shared map HTTP disk cache keeps up to 256 MiB of responses and follows the server's expiration dates and validators. Opening the same area again can reuse fresh tiles after authentication. Offline deployments can install their own DEMs in the directories above.

Build a VRT after placing GeoTIFF tiles:

```powershell
gdalbuildvrt resources/maps/terrain/copernicus_dem_glo30/copernicus_dem_glo30.vrt resources/maps/terrain/copernicus_dem_glo30/*.tif
```

The default Copernicus DEM earth template is `resources/maps/vaporview_with_dem.earth`. The SRTM fallback template is `resources/maps/vaporview_with_srtm.earth`.

Application auto-load order:

1. `vaporview_with_dem.earth` when `terrain/copernicus_dem_glo30/copernicus_dem_glo30.vrt` exists.
2. `vaporview_with_srtm.earth` when `terrain/srtm/srtm.vrt` exists.
3. `vaporview_default.earth` when no DEM VRT exists, keeping Natural Earth plus the local grid fallback.
