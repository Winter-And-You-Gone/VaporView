# Sentinel-2 Local Imagery

Place local open Sentinel-2-derived GeoTIFF imagery here when testing higher-resolution offline backgrounds.

Expected VRT:

```text
data/maps/imagery/sentinel2/sentinel2.vrt
```

Build the VRT from local files only, for example:

```powershell
gdalbuildvrt data/maps/imagery/sentinel2/sentinel2.vrt data/maps/imagery/sentinel2/*.tif
```

VaporView diagnostics will detect this VRT, but the current default map templates do not automatically load it yet.
