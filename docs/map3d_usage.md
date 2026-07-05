# VaporView 3D Map Usage

The 3D Map window is an optional native desktop view for local offline flight visualization. It is available only when VaporView is built with `VAPORVIEW_ENABLE_OSGEARTH=ON`.

## Open The Window

In the VaporView ground GUI, use:

```text
View / 视图 -> 3D Map / 三维地图
```

Use `View / 视图 -> Map Data Diagnostics / 地图数据诊断` to open the 3D Map window and show the map data diagnostics panel directly.

When osgEarth is disabled, the main window does not create the native 3D map module. Rebuild with `-DVAPORVIEW_ENABLE_OSGEARTH=ON` to enable it.

## Map Modes

The window asks `MapDataManager` for the best local map data at startup.

Selection order:

1. `Full local map`: Natural Earth + DEM + local OSM water, roads, building footprints, and place labels.
2. `Natural Earth + Copernicus DEM`.
3. `Natural Earth + SRTM DEM`.
4. `Natural Earth`.
5. `Local grid only`.

No map mode should require network access. Missing files should fall back to the next available local mode.

## Toolbar

```text
打开 Session
清空轨迹
播放 / 暂停
停止回放
Replay speed
Replay slider
跟随飞机
最大可见点数
加载 Earth 文件
重载最佳本地地图
飞到飞机
飞到轨迹
重置视角
地图诊断
```

`打开 Session` loads a recorded session directory and reads `sensors/devices.csv`.

`加载 Earth 文件` opens a custom `.earth` file. Keep relative paths in that file aligned with its location.

`重载最佳本地地图` rescans `data/maps` and loads the best local dataset currently available.

`飞到飞机` moves the camera to the latest aircraft sample.

`飞到轨迹` frames the full loaded/displayed trajectory.

`重置视角` returns to the default globe or local-grid view.

`跟随飞机` keeps the camera near the latest aircraft sample as new samples arrive.

`最大可见点数` limits how many trajectory samples are rendered at once. The full session sample count is still kept for replay/status, while older rendered segments are hidden when the limit is exceeded. The value is saved as `Map3D/maxVisibleSamples`.

## Session Replay

After loading a session:

- `播放` starts replay.
- `暂停` pauses replay.
- `停止回放` stops and rewinds to the first sample.
- Speed choices are `0.5x`, `1x`, `2x`, `5x`, and `10x`.
- The slider seeks through the loaded samples.

Replay is local to the 3D Map window and does not change the recorded session files.

## Realtime Track

When the main VaporView window receives navigation samples and the 3D Map window exists, the map receives `NavSample` data behind the `VAPORVIEW_HAS_OSGEARTH` guard. If the 3D Map window is closed, the normal acquisition, recording, telemetry, SkyCore, and SkyTui paths continue without requiring the map.

## Status Bar

The status bar shows:

- visible and total track samples
- visible count after the maximum visible sample limit is applied
- map mode
- viewport size
- FPS
- frame time
- track update time
- latest latitude, longitude, and height when available
- height reference label
- fix quality
- replay position and speed

Height reference labels are display hints only. VaporView currently does not compute AGL or terrain-clearance safety decisions because RTK/GNSS and DEM height references may not be unified.

## Map Data Diagnostics

Click `地图诊断`, or use `View / 视图 -> Map Data Diagnostics / 地图数据诊断`, to inspect:

- current map mode
- active `.earth` file
- current working directory
- project root
- `data/maps` root
- Natural Earth texture, VRT, and raster paths
- Copernicus DEM VRT
- SRTM VRT
- OSM roads/water/buildings/places GeoPackages
- OSG plugin path
- `OSG_LIBRARY_PATH`
- `OSGEARTH_NOTIFY_LEVEL`
- `GDAL_DATA`
- `PROJ_DATA`
- found files
- missing files
- warnings and diagnostics messages

This panel is the first place to check when the map is black, an earth file fails to load, or DEM/OSM layers do not appear.

## Local Grid Fallback

If no complete local map data exists, the 3D Map still opens with a local grid. You can load a session, view the aircraft marker, replay the trajectory, clear the track, and use the camera tools. The grid mode is intentionally offline and does not download a basemap.

## Preparing Local Data

Natural Earth:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/download-natural-earth-map.ps1
```

Copernicus DEM:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/prepare-demo-dem.ps1
python scripts/prepare-demo-dem.py
```

SRTM:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/prepare-demo-dem.ps1 -Srtm
python scripts/prepare-demo-dem.py --srtm
```

OSM local vectors:

```powershell
python scripts/prepare-osm-local-data.py data/maps/osm/local_extract.osm.pbf --overwrite
```

The user is responsible for placing Copernicus DEM, SRTM, OSM, Sentinel-2, Landsat, or OpenAerialMap files under `data/maps`. VaporView should not download commercial or restricted data sources.
