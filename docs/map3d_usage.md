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

The `地图诊断` panel reports both static file discovery and runtime earth loading. When the map appears blank or incomplete, check `Earth load` first: it shows the requested `.earth` path, whether OSG/osgEarth loaded it, whether a real `MapNode` was found, and each layer's open/status string.

Selection order:

1. `Full local map`: Natural Earth + DEM + local OSM water and roads rendered in safe default mode. Building and place GeoPackages are detected and reported, but are not auto-rendered because full-country labels/buildings can stall the native 3D view. This uses `vaporview_full_local.earth` with Copernicus DEM, or `vaporview_full_local_srtm.earth` when SRTM is the only available DEM.
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
本地影像
本地 OSG 建筑
清除 OSG 建筑
加载飞机模型
内置飞机标记
重载最佳本地地图
飞到飞机
飞到轨迹
重置视角
地图诊断
```

`打开 Session` loads a recorded session directory and reads `sensors/devices.csv`.
The reader supports both newer `nav_*`/`epsilon_*` columns and older RTK/IMU session columns such as `rtk_lat`, `rtk_lon`, `rtk_alt`, `rtk_fix`, `rtk_sat`, `rtk_heading`, `rtk_pitch`, `rtk_vel_*`, and `imu_roll`/`imu_yaw`.
When quaternion columns (`quat_w`, `quat_x`, `quat_y`, `quat_z`, including `epsilon_quat_*` aliases) are available, the aircraft marker and follow camera use them for attitude/heading before falling back to roll/pitch/yaw.

By default the aircraft is a built-in OSG marker, so the 3D Map works without extra assets. `加载飞机模型` lets you choose any local `.osgb`, `.osg`, `.glb`, or `.gltf` model path; the selection is saved as `Map3D/aircraftModelPath`. `内置飞机标记` clears that setting and returns to the built-in marker. You can also keep an automatic default model under `resources/maps/models/aircraft/` as `vaporview_aircraft.osgb`, `vaporview_aircraft.osg`, `vaporview_aircraft.glb`, or `vaporview_aircraft.gltf`. The diagnostics panel reports whether the model loaded; if OSG cannot read it, VaporView keeps the built-in marker and continues rendering the flight path.

`加载 Earth 文件` opens a custom `.earth` file. Keep relative paths in that file aligned with its location.

`本地影像` opens a menu for optional local imagery templates. Entries become enabled when both the matching VRT and the `.earth` template exist. The templates live in `resources/maps`:

```text
vaporview_with_sentinel2_imagery.earth
vaporview_with_landsat_imagery.earth
vaporview_with_openaerialmap_imagery.earth
```

These overlays are still fully local. They do not change automatic base-map selection, but the menu avoids manually browsing for the template once `MapDataManager` detects the VRT.

`重载最佳本地地图` rescans `resources/maps` and loads the best local dataset currently available.

The native OSG view accepts mouse drag, mouse wheel, and keyboard focus. Click the 3D view once, then use the mouse or keyboard navigation keys that the active OSG/osgEarth manipulator supports.

`飞到飞机` moves the camera to the latest aircraft sample.

`飞到轨迹` frames the full loaded/displayed trajectory.

`重置视角` returns to the default globe or local-grid view.

`跟随飞机` keeps the camera near the latest aircraft sample as new samples arrive.
The status bar and diagnostics panel report whether follow mode is currently on or off.

`最大可见点数` limits how many trajectory samples are rendered at once. The full session sample count is still kept for replay/status, while older rendered segments are hidden when the limit is exceeded. The value is saved as `Map3D/maxVisibleSamples`.

The 3D Map persists its local UI state with `QSettings("VaporView", "Map3D")`: `lastEarthFile`, `lastSessionDir`, `followAircraft`, `maxVisibleSamples`, `replaySpeed`, and `aircraftModelPath`. These settings only affect the optional 3D Map window and do not alter SkyCore, SkyTui, telemetry, or recorded session files.

## Session Replay

After loading a session:

- `播放` starts replay.
- `暂停` pauses replay.
- `停止回放` stops and rewinds to the first sample.
- Speed choices are `0.5x`, `1x`, `2x`, `5x`, and `10x`.
- Playback advances by elapsed session time, so sparse or irregular GNSS sample intervals are preserved.
- The slider seeks by elapsed session time from the first recorded sample. The status bar still reports the current sample index and the elapsed/duration time.

Replay is local to the 3D Map window and does not change the recorded session files.
Loading a session automatically flies the camera to the complete track so the first view is useful without manual searching. Loading or reloading an earth file also re-centers on the current track when track samples are already present.

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
- visible RTK/GNSS quality counts for Fixed, Float, DGPS, Single, Unknown, Invalid, and jump markers
- latest latitude, longitude, and height when available
- height reference label
- readable fix quality (`Fixed`, `Float`, `DGPS`, `Single`, `Invalid`, or `Unknown`)
- aircraft attitude source (`Quaternion`, `Euler`, or `none`)
- replay position and speed

Height reference labels are display hints only. The status bar and diagnostics intentionally show `Height ref unchecked` / `height reference unchecked` whenever a displayed height is based on the raw navigation sample. VaporView currently does not compute AGL or terrain-clearance safety decisions because RTK/GNSS and DEM height references may not be unified.

## Map Data Diagnostics

Click `地图诊断`, or use `View / 视图 -> Map Data Diagnostics / 地图数据诊断`, to inspect:

- current map mode
- active `.earth` file
- runtime `Earth load` status, including requested path, loaded state, MapNode state, and layer open/status strings
- render performance, including visible/total/hidden samples, trajectory segment count/size, FPS, frame time, and track update time
- local aircraft model load status and built-in marker fallback state
- visible trajectory quality, including line samples, red marker samples, RTK Fixed/Float/DGPS/Single/Unknown counts, invalid/unusable samples, and jump markers
- latest aircraft attitude source, using the same quaternion-first fallback as the aircraft marker
- current working directory
- project root
- `resources/maps` root
- Natural Earth texture, VRT, and raster paths
- Copernicus DEM VRT
- SRTM VRT
- OSM roads/water/buildings/places GeoPackages
- per-layer OSM GeoPackage availability for roads, water, buildings, and places
- OSM layer contracts that map each GeoPackage file to its expected internal layer name, `.earth` `OGRFeatures` name, and render layer type
- Full Local Map blockers, including exact missing DEM, Natural Earth, OSM, or earth template files
- optional Sentinel-2/Landsat/OpenAerialMap imagery VRTs
- optional local imagery menu-ready overlay count, which requires both the VRT and matching `.earth` template
- optional native OSG building tile `tileset.json`
- native OSG building tile contract status, referenced content URIs, missing resources, and non-local URI warnings
- OSG plugin path
- `OSG_LIBRARY_PATH`
- `OSGEARTH_NOTIFY_LEVEL`
- `GDAL_DATA`
- `PROJ_DATA`
- `PROJ_LIB`
- found files
- missing files
- warnings and diagnostics messages

This panel is the first place to check when the map is black, an earth file fails to load, or DEM/OSM layers do not appear. If the `.earth` path exists but `Loaded` is `no`, focus on OSG plugins and GDAL/PROJ data paths. If `Loaded` is `yes` but `MapNode` is `no`, the view is probably using the manual Natural Earth textured-globe fallback or an OSG node that is not an osgEarth map.

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

For tiles staged outside `resources/maps/terrain/copernicus_dem_glo30/`, pass the external tile folder while still writing the VRT to the auto-load project path:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/prepare-demo-dem.ps1 -DemDir X:\DEM\copernicus_tiles
python scripts/prepare-demo-dem.py --dem-dir X:\DEM\copernicus_tiles
```

SRTM:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/prepare-demo-dem.ps1 -Srtm
python scripts/prepare-demo-dem.py --srtm
```

OSM local vectors:

```powershell
python scripts/prepare-osm-local-data.py resources/maps/osm/local_extract.osm.pbf --overwrite
```

Optional local imagery:

```powershell
python scripts/prepare-local-imagery.py sentinel2
python scripts/prepare-local-imagery.py landsat
python scripts/prepare-local-imagery.py openaerialmap
```

For imagery staged outside `resources/maps/imagery/<slot>/`, pass the external tile folder while still writing the VRT to the auto-detect project path:

```powershell
python scripts/prepare-local-imagery.py sentinel2 --imagery-dir X:\Imagery\sentinel2_tiles
```

After building a local imagery VRT, use the `本地影像` menu and select the enabled matching overlay.

The user is responsible for placing Copernicus DEM, SRTM, OSM, Sentinel-2, Landsat, or OpenAerialMap files under `resources/maps`. VaporView should not download commercial or restricted data sources.

Optional VaporView native OSG building tiles can be placed under `resources/maps/tiles3d/local/tileset.json`. The diagnostics panel detects that path and validates the local-only data contract. A healthy dataset should report `Native OSG building tiles contract: valid`; remote, absolute, missing, or malformed content references are listed in the diagnostics panel.

When the contract is valid, the `本地 OSG 建筑` toolbar action attempts to load the tileset as a local OSG overlay. This preview does not replace the active Natural Earth/DEM/OSM base map and does not affect trajectory or aircraft layers. Use `清除 OSG 建筑` to remove the preview overlay while keeping the base map and flight track. If loading fails, check `Native OSG building tile preview load` in diagnostics; `osgDB::readNodeFile returned null` usually means the installed OSG/osgEarth runtime lacks plugin support for that native payload dataset.

Loading a new native OSG building tile preview first clears any previous preview overlay. This keeps the diagnostic `Loaded` state aligned with the visible overlay instead of leaving a stale building layer after a failed reload.
