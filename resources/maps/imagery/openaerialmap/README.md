# OpenAerialMap Local Imagery

Place local open OpenAerialMap GeoTIFF imagery here when testing higher-resolution offline backgrounds.

Expected VRT:

```text
resources/maps/imagery/openaerialmap/openaerialmap.vrt
```

Build the VRT from local files only, for example:

```powershell
gdalbuildvrt resources/maps/imagery/openaerialmap/openaerialmap.vrt resources/maps/imagery/openaerialmap/*.tif
```

VaporView diagnostics will detect this VRT. To view it, open the 3D Map toolbar `本地影像` menu and select the enabled OpenAerialMap entry. If you need to browse manually, use `加载 Earth 文件` and select `resources/maps/vaporview_with_openaerialmap_imagery.earth`.
