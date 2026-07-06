# VaporView Local Map Data

This directory is the offline map data root used by the optional native 3D Map module.
It is intentionally local-first: VaporView does not use external cloud map services,
commercial online basemaps, API keys, or automatic downloads for the 3D map.

Tracked files in this directory are small templates, README files, and lightweight VRT
metadata needed to recreate the local map setup. Large raster tiles, DEM tiles, OSM
extracts, generated GeoPackages, imagery, 3D Tiles payloads, and archives are ignored
by Git and must be supplied locally by the user.

Automatic map selection order:

1. `vaporview_full_local.earth` when Natural Earth, Copernicus DEM, and all OSM
   GeoPackages exist.
2. `vaporview_full_local_srtm.earth` when Natural Earth, SRTM, and all OSM
   GeoPackages exist but Copernicus DEM is unavailable.
3. `vaporview_with_dem.earth` when Natural Earth and Copernicus DEM exist.
4. `vaporview_with_srtm.earth` when Natural Earth and SRTM exist.
5. `vaporview_default.earth` when only Natural Earth exists.
6. Local grid fallback when no complete local map files are available.

Directory roles:

- `natural_earth/`: offline global visual background. Natural Earth is not DEM data.
- `terrain/copernicus_dem_glo30/`: preferred local DEM GeoTIFF tiles plus
  `copernicus_dem_glo30.vrt`.
- `terrain/srtm/`: fallback DEM GeoTIFF tiles plus `srtm.vrt`.
- `osm/`: local OpenStreetMap/Geofabrik extracts and generated `roads.gpkg`,
  `water.gpkg`, `buildings.gpkg`, and `places.gpkg`.
- `imagery/`: optional local Sentinel-2, Landsat, or OpenAerialMap raster overlays.
- `tiles3d/`: optional local 3D Tiles layout and preview overlay entry point.
- `models/aircraft/`: optional automatic default aircraft model slot. The 3D Map
  toolbar can also select any local `.osgb`, `.osg`, `.glb`, or `.gltf` model and
  persist it as `Map3D/aircraftModelPath`. If no readable model is configured, the
  3D Map keeps using its built-in marker.

Use `docs/map_data.md` for the full data preparation and verification workflow, and
`docs/map3d_usage.md` for the 3D Map diagnostics panel.
