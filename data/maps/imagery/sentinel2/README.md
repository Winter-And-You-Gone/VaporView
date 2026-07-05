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

VaporView diagnostics will detect this VRT. To view it, use the 3D Map toolbar `加载 Earth 文件` action and select `data/maps/vaporview_with_sentinel2_imagery.earth`.
