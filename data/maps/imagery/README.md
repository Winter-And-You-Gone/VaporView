# Local Imagery

This directory is reserved for optional local raster imagery layers. VaporView does not download imagery and does not use API-key or commercial online basemap providers.

Expected local VRT entry points:

```text
data/maps/imagery/
  sentinel2/sentinel2.vrt
  landsat/landsat.vrt
  openaerialmap/openaerialmap.vrt
```

Manual `.earth` templates are available at the `data/maps` root:

```text
data/maps/vaporview_with_sentinel2_imagery.earth
data/maps/vaporview_with_landsat_imagery.earth
data/maps/vaporview_with_openaerialmap_imagery.earth
```

Each template keeps Natural Earth as the offline global background and adds exactly one local imagery VRT overlay. `MapDataManager` scans these VRT paths and shows them in diagnostics, but optional imagery does not change the automatic base map selection order.

Large GeoTIFF/VRT imagery and archives are intentionally ignored by git.
