# Local Imagery

This directory is reserved for optional local raster imagery layers. VaporView does not download imagery and does not use API-key or commercial online basemap providers.

Expected local VRT entry points:

```text
data/maps/imagery/
  sentinel2/sentinel2.vrt
  landsat/landsat.vrt
  openaerialmap/openaerialmap.vrt
```

Current built-in templates use Natural Earth as the offline visual background and optional local DEM/OSM layers. These VRT paths are scanned by `MapDataManager` and shown in diagnostics so future `.earth` templates can add higher-resolution local imagery without changing the directory contract.

Large GeoTIFF/VRT imagery and archives are intentionally ignored by git.
