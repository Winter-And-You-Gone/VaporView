# Landsat Local Imagery

Place local open Landsat-derived GeoTIFF imagery here when testing higher-resolution offline backgrounds.

Expected VRT:

```text
data/maps/imagery/landsat/landsat.vrt
```

Build the VRT from local files only, for example:

```powershell
gdalbuildvrt data/maps/imagery/landsat/landsat.vrt data/maps/imagery/landsat/*.tif
```

VaporView diagnostics will detect this VRT, but the current default map templates do not automatically load it yet.
