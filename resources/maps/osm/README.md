# Local OSM Vector Data

Place open local OSM extracts here when you want the 3D map to use offline vector context.

Expected generated files:

- `roads.gpkg`
- `water.gpkg`
- `buildings.gpkg`
- `places.gpkg`

Use `scripts/prepare-osm-local-data.py` to convert a local `.osm.pbf` or `.osm` extract into these GeoPackage files with GDAL/OGR. Large OSM inputs and generated GeoPackages are intentionally ignored by git.

The full-local earth template renders these local files as water fill, road lines, building footprints, local building extrusion, and place labels. It does not use online OSM tiles. The conversion script writes `extrusion_height_m` for buildings from local OSM `height`, `building:height`, `building:levels`, or `levels` tags, with a 10 m fallback. The extrusion is a visual context layer, not surveyed building-height data.

Example:

```powershell
python scripts/prepare-osm-local-data.py resources/maps/osm/local_extract.osm.pbf --overwrite
python scripts/prepare-osm-local-data.py resources/maps/osm/local_extract.osm.pbf --check
```

If `ogr2ogr` or `ogrinfo` is outside `PATH`, set `GDAL_BIN` or pass the tool
directory explicitly:

```powershell
python scripts/prepare-osm-local-data.py resources/maps/osm/local_extract.osm.pbf --gdal-bin C:\OSGeo4W\bin --overwrite
```

`--check` validates the expected `roads`, `water`, `buildings`, and `places` GeoPackage outputs. If GDAL's `ogrinfo` is available, it also checks that each file exposes the layer name used by the full-local earth templates.

Recommended open sources for local extracts include OpenStreetMap regional extracts such as Geofabrik. The script does not download anything; it only converts files you already placed locally.

Do not place online tile credentials or commercial map service configuration in this directory.
