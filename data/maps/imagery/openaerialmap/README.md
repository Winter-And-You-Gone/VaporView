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

VaporView diagnostics will detect this VRT. To view it, open the 3D Map toolbar `本地影像` menu and select the enabled OpenAerialMap entry. If you need to browse manually, use `加载 Earth 文件` and select `data/maps/vaporview_with_openaerialmap_imagery.earth`.
