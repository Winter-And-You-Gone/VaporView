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

The built-in 3D maps sample local Copernicus DEM GLO-30 first, then local SRTM. At regional and closer scales (osgEarth level 8 and above), gaps are filled from the public [Copernicus DEM GLO-30 collection on AWS](https://registry.opendata.aws/copernicus-dem/). GDAL reads the visible area from cloud-optimized GeoTIFFs over HTTPS without an account or key. At wider scales the map skips these one-degree files to avoid opening thousands of them for a single globe tile.

The osgEarth disk cache stores successfully assembled GLO-30 tiles for offline reuse at the same zoom. A cached tile stays in use until the cache is cleared. Regional DEM files can also be installed in the directories above for offline coverage at every zoom.

Cesium World Terrain fills any remaining gaps when a token is configured. Set `VAPORVIEW_CESIUM_ION_TOKEN` to an ion token with read access to asset 1 before starting VaporView, or set the current user's `VaporView/Map3D` setting `cesium/ion_token` on Windows. The first tile that needs Cesium triggers ion authentication. Its shared map HTTP cache keeps up to 256 MiB and follows the server's expiration dates and validators.

GLO-30 attribution: terrain adapted from the Copernicus DEM. © DLR e.V. 2010–2014 and © Airbus Defence and Space GmbH 2014–2018, provided under COPERNICUS by the European Union and ESA. See the [Copernicus DEM product and licence](https://dataspace.copernicus.eu/explore-data/data-collections/copernicus-contributing-missions/collections-description/COP-DEM).

Build a VRT after placing GeoTIFF tiles:

```powershell
gdalbuildvrt resources/maps/terrain/copernicus_dem_glo30/copernicus_dem_glo30.vrt resources/maps/terrain/copernicus_dem_glo30/*.tif
```

The default Copernicus DEM earth template is `resources/maps/vaporview_with_dem.earth`. The SRTM fallback template is `resources/maps/vaporview_with_srtm.earth`.

Application auto-load order:

1. `vaporview_with_dem.earth` when `terrain/copernicus_dem_glo30/copernicus_dem_glo30.vrt` exists.
2. `vaporview_with_srtm.earth` when `terrain/srtm/srtm.vrt` exists.
3. `vaporview_default.earth` when no DEM VRT exists, keeping Natural Earth plus the local grid fallback.
