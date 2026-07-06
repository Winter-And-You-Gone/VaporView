# Local Imagery

This directory is reserved for optional local raster imagery layers. VaporView does not download imagery and does not use API-key or commercial online basemap providers.

Expected local VRT entry points:

```text
resources/maps/imagery/
  sentinel2/sentinel2.vrt
  landsat/landsat.vrt
  openaerialmap/openaerialmap.vrt
```

Manual `.earth` templates are available at the `resources/maps` root:

```text
resources/maps/vaporview_with_sentinel2_imagery.earth
resources/maps/vaporview_with_landsat_imagery.earth
resources/maps/vaporview_with_openaerialmap_imagery.earth
```

Each template keeps Natural Earth as the offline global background and adds exactly one local imagery VRT overlay. `MapDataManager` scans these VRT paths and shows them in diagnostics, but optional imagery does not change the automatic base map selection order.

Prepare VRT files from local GeoTIFFs with:

```powershell
python scripts/prepare-local-imagery.py sentinel2
python scripts/prepare-local-imagery.py landsat
python scripts/prepare-local-imagery.py openaerialmap
```

Use `--imagery-dir` when the source GeoTIFFs live outside `resources/maps/imagery/<slot>/`. The helper still writes the VRT to the canonical project path so the 3D Map `本地影像` menu can auto-detect it.

Large GeoTIFF/VRT imagery and archives are intentionally ignored by git.
