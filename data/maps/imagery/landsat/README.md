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

VaporView diagnostics will detect this VRT. To view it, open the 3D Map toolbar `本地影像` menu and select the enabled Landsat entry. If you need to browse manually, use `加载 Earth 文件` and select `data/maps/vaporview_with_landsat_imagery.earth`.
