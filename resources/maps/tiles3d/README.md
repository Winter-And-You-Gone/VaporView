# Local 3D Tiles

This directory reserves a local 3D Tiles entry point for offline 3D content experiments. It is for files you already have locally; VaporView does not use Cesium ion and does not download commercial or API-key protected 3D content.

Expected entry point:

```text
resources/maps/tiles3d/local/tileset.json
```

Put the full 3D Tiles dataset under `resources/maps/tiles3d/local/` next to `tileset.json`. Large `tileset.json`, `.b3dm`, `.i3dm`, `.pnts`, `.cmpt`, `.glb`, and `.gltf` files are intentionally ignored by git.

`MapDataManager` scans this path and reports it in the 3D Map diagnostics. When the local-only contract is valid, the 3D Map `本地 3D Tiles` toolbar action attempts to load the tileset as an independent OSG overlay. Use `清除 3D Tiles` to remove that overlay without reloading the base map or clearing the flight track. Loading success depends on the installed OSG/osgEarth runtime plugin support; failures are reported under `Local 3D Tiles preview load` in the diagnostics panel.

The diagnostics validate that `tileset.json` is parseable, has the expected `asset.version`, `root`, `boundingVolume`, and `geometricError` structure, and that referenced `content.uri` or `contents[].uri` payloads are local relative files that exist under `resources/maps/tiles3d/local/`. Remote URLs, absolute paths, missing payloads, malformed JSON, and preview load failures are reported without contacting online services.
