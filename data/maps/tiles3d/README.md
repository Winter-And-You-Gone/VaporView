# Local 3D Tiles

This directory reserves a local 3D Tiles entry point for future offline 3D content experiments. It is for files you already have locally; VaporView does not use Cesium ion and does not download commercial or API-key protected 3D content.

Expected entry point:

```text
data/maps/tiles3d/local/tileset.json
```

Put the full 3D Tiles dataset under `data/maps/tiles3d/local/` next to `tileset.json`. Large `tileset.json`, `.b3dm`, `.i3dm`, `.pnts`, `.cmpt`, `.glb`, and `.gltf` files are intentionally ignored by git.

`MapDataManager` scans this path and reports it in the 3D Map diagnostics. The current renderer does not load 3D Tiles yet; this directory establishes the local, non-Cesium-ion data contract for that future step.

The diagnostics validate that `tileset.json` is parseable, has the expected `asset.version`, `root`, `boundingVolume`, and `geometricError` structure, and that referenced `content.uri` or `contents[].uri` payloads are local relative files that exist under `data/maps/tiles3d/local/`. Remote URLs, absolute paths, missing payloads, or malformed JSON are reported before renderer integration work begins.
