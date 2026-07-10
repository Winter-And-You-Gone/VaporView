# Local 3D Tiles

This directory reserves a local 3D Tiles entry point for offline 3D content experiments. It is for files you already have locally; VaporView does not use Cesium ion and does not download commercial or API-key protected 3D content.

Expected entry point:

```text
resources/maps/tiles3d/local/tileset.json
```

Put the full 3D Tiles dataset under `resources/maps/tiles3d/local/` next to `tileset.json`. Large `tileset.json`, `.b3dm`, `.i3dm`, `.pnts`, `.cmpt`, `.glb`, `.gltf`, and `.osgb` files are intentionally ignored by git.

For the project-local Hangzhou Xihu workflow, build the generator and run:

```powershell
cmake --build build/Release --config Release --target vaporview_building_tileset
python scripts/prepare-real3d-local-data.py --overwrite
```

The generated `tileset.json` uses the 3D Tiles hierarchy and bounding-volume contract, with OSG-native `.osgb` payloads because the bundled OpenSceneGraph runtime does not include a glTF or Cesium 3D Tiles reader plugin. These payloads remain fully local and are loaded by VaporView's own tileset overlay path.

`MapDataManager` scans this path and reports it in the 3D Map diagnostics. When the local-only contract is valid, the 3D Map `本地 3D Tiles` toolbar action attempts to load the tileset as an independent OSG overlay. Use `清除 3D Tiles` to remove that overlay without reloading the base map or clearing the flight track. Loading success depends on the installed OSG/osgEarth runtime plugin support; failures are reported under `Local 3D Tiles preview load` in the diagnostics panel.

The diagnostics validate that `tileset.json` is parseable, has the expected `asset.version`, `root`, `boundingVolume`, and `geometricError` structure, and that referenced `content.uri` or `contents[].uri` payloads are local relative files that exist under `resources/maps/tiles3d/local/`. Remote URLs, absolute paths, missing payloads, malformed JSON, and preview load failures are reported without contacting online services.
