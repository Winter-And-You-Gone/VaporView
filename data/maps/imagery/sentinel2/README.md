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

VaporView diagnostics will detect this VRT. To view it, open the 3D Map toolbar `本地影像` menu and select the enabled Sentinel-2 entry. If you need to browse manually, use `加载 Earth 文件` and select `data/maps/vaporview_with_sentinel2_imagery.earth`.
