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

VaporView diagnostics will detect this VRT. To view it, use the 3D Map toolbar `加载 Earth 文件` action and select `data/maps/vaporview_with_landsat_imagery.earth`.
