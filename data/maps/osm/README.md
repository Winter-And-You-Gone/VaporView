# Local OSM Vector Data

Place open local OSM extracts here when you want the 3D map to use offline vector context.

Expected generated files:

- `roads.gpkg`
- `water.gpkg`
- `buildings.gpkg`
- `places.gpkg`

Use `scripts/prepare-osm-local-data.py` to convert a local `.osm.pbf` or `.osm` extract into these GeoPackage files with GDAL/OGR. Large OSM inputs and generated GeoPackages are intentionally ignored by git.

Example:

```powershell
python scripts/prepare-osm-local-data.py data/maps/osm/local_extract.osm.pbf --overwrite
```

Recommended open sources for local extracts include OpenStreetMap regional extracts such as Geofabrik. The script does not download anything; it only converts files you already placed locally.

Do not place online tile credentials or commercial map service configuration in this directory.
