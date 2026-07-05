# OpenAerialMap Local Imagery

Place local open OpenAerialMap GeoTIFF imagery here when testing higher-resolution offline backgrounds.

Expected VRT:

```text
data/maps/imagery/openaerialmap/openaerialmap.vrt
```

Build the VRT from local files only, for example:

```powershell
gdalbuildvrt data/maps/imagery/openaerialmap/openaerialmap.vrt data/maps/imagery/openaerialmap/*.tif
```

VaporView diagnostics will detect this VRT, but the current default map templates do not automatically load it yet.
