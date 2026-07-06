# VaporView osgEarth Setup

VaporView's 3D map is a native Qt Widgets + C++ + OpenSceneGraph/osgEarth feature. It is optional and stays behind `VAPORVIEW_ENABLE_OSGEARTH=ON`. The default build remains `OFF` and must not require osgEarth.

This project does not use CesiumJS, QWebEngineView, Cesium ion, Google, GaoDe, Baidu, Tencent, TianDiTu, ArcGIS Online, Mapbox, or any key-based commercial online map provider.

## Windows Setup

Recommended local layout:

```text
.local_deps/
  vcpkg_installed/
    x64-windows/
      bin/
      include/
      lib/
      plugins/osgPlugins-*/
      share/gdal/
      share/proj/
```

The current repository CMake checks `.local_deps/vcpkg_installed/x64-windows` before system package paths. A project-local vcpkg install keeps osgEarth, OSG, GDAL, and PROJ beside the source tree instead of changing the whole machine.

Configure and build with Visual Studio Build Tools:

```powershell
cmd /s /c '"F:\VisualStudio\2022\BuildTools\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && cmake -S . -B build/Release -DVAPORVIEW_ENABLE_OSGEARTH=ON'
cmd /s /c '"F:\VisualStudio\2022\BuildTools\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && cmake --build build/Release --config Release --target VaporView'
```

Default non-3D build:

```powershell
cmd /s /c '"F:\VisualStudio\2022\BuildTools\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && cmake -S . -B build/Release -DVAPORVIEW_ENABLE_OSGEARTH=OFF'
cmd /s /c '"F:\VisualStudio\2022\BuildTools\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && cmake --build build/Release --config Release'
```

## Linux Setup

Install Qt 6 Widgets/OpenGLWidgets, OpenSceneGraph, osgEarth, GDAL, and PROJ from your distribution or a local package manager. Keep the same CMake switch:

```bash
cmake -S . -B build/Release -DVAPORVIEW_ENABLE_OSGEARTH=ON
cmake --build build/Release --target VaporView
```

If osgEarth is installed in a non-standard prefix, pass it through CMake:

```bash
cmake -S . -B build/Release \
  -DVAPORVIEW_ENABLE_OSGEARTH=ON \
  -DCMAKE_PREFIX_PATH=/path/to/osgearth/prefix
```

## Runtime Paths

On Windows, the build stages common project-local runtime files into `build/Release`:

- osgEarth/OSG/GDAL/PROJ DLLs from `.local_deps/vcpkg_installed/x64-windows/bin`
- OSG plugins from `.local_deps/vcpkg_installed/x64-windows/plugins/osgPlugins-*`
- GDAL data from `.local_deps/vcpkg_installed/x64-windows/share/gdal`
- PROJ data from `.local_deps/vcpkg_installed/x64-windows/share/proj`
- local Natural Earth map files from `data/maps`

At runtime `OsgEarthViewWidget` also tries to fill missing environment variables:

```text
OSG_LIBRARY_PATH
GDAL_DATA
PROJ_DATA
PROJ_LIB
```

The 3D Map diagnostics dialog shows the resolved plugin and data paths.

## GDAL Command-Line Tools

The optional data-preparation helpers need GDAL command-line tools:

- `gdalbuildvrt` and optionally `gdalinfo` for DEM VRT generation.
- `ogr2ogr` and optionally `ogrinfo` for local OSM GeoPackage generation.

The scripts first check `PATH`, then `GDAL_BIN`, then the project-local
`.local_deps/vcpkg_installed/x64-windows/tools/gdal` and `bin` folders, and on
Windows common OSGeo4W/QGIS folders such as `C:\OSGeo4W\bin`. If the tools are
installed elsewhere, pass the directory explicitly:

```powershell
python scripts/prepare-demo-dem.py --gdal-bin C:\OSGeo4W\bin
python scripts/prepare-osm-local-data.py data/maps/osm/local_extract.osm.pbf --gdal-bin C:\OSGeo4W\bin --check
powershell -ExecutionPolicy Bypass -File scripts/prepare-demo-dem.ps1 -GdalBin C:\OSGeo4W\bin
```

## Common Problems

### `osgEarthConfig.cmake` Not Found

Configure was run with `VAPORVIEW_ENABLE_OSGEARTH=ON`, but CMake cannot find osgEarth. Check that osgEarth is installed and that either `.local_deps/vcpkg_installed/x64-windows` exists or `CMAKE_PREFIX_PATH` points at the install prefix.

### OSG Plugin Not Found

Symptoms include blank maps, unknown file extension messages, or image/earth files failing to load. Check `OSG_LIBRARY_PATH` in the 3D Map diagnostics panel. It should point at a directory like `osgPlugins-3.6.5` or another `osgPlugins-*` directory from the local OSG install.

### Missing `GDAL_DATA` or `PROJ_DATA`

GDAL and PROJ may fail to interpret GeoTIFF/VRT coordinate systems when their data directories are missing. Open the diagnostics panel and confirm `GDAL_DATA`, `PROJ_DATA`, and `PROJ_LIB` resolve to real directories.

### `gdalbuildvrt` or `ogr2ogr` Not Found

The osgEarth runtime can load GDAL through libraries even when GDAL command-line tools are not installed. The DEM and OSM preparation scripts need the tools separately. Install OSGeo4W, QGIS, or another GDAL tools package, then add its `bin` directory to `PATH`, set `GDAL_BIN`, or pass `--gdal-bin` / `-GdalBin`.

### Black Screen

Check these in order:

1. Build with `-DVAPORVIEW_ENABLE_OSGEARTH=ON`.
2. Open `地图诊断` and check `Earth load`. If `Loaded` is `no`, the `.earth` file did not load at runtime even if it exists on disk.
3. If `MapNode` is `no`, VaporView may be using the manual Natural Earth textured-globe fallback or OSG loaded a node that is not an osgEarth map.
4. Review `Layer details`. Closed layers or non-OK status strings usually point to missing raster/VRT/GeoPackage data, missing GDAL/PROJ data, or missing OSG plugins.
5. Confirm OSG plugins, `GDAL_DATA`, `PROJ_DATA`, and `PROJ_LIB` in diagnostics.
6. Confirm `data/maps/vaporview_default.earth` and Natural Earth files exist.
7. Use `重载最佳本地地图`.
8. Use `重置视角`.
9. If no map data exists, confirm the local grid fallback appears.

### `.earth` Load Failed

The built-in `.earth` templates use paths relative to `data/maps`. Keep the `.earth` file next to `natural_earth/`, `terrain/`, and `osm/`, or load a custom template whose paths match its location.

### QOpenGLWidget / OSG Integration Issues

The current implementation embeds OSG through `QOpenGLWidget` and `GraphicsWindowEmbedded`. Keep viewer rendering single-threaded in the Qt GUI thread. Do not move osgEarth rendering into SkyCore or SkyTui.

## Verification

With osgEarth enabled:

```powershell
cmd /s /c '"F:\VisualStudio\2022\BuildTools\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && cmake -S . -B build/Release -DVAPORVIEW_ENABLE_OSGEARTH=ON'
cmd /s /c '"F:\VisualStudio\2022\BuildTools\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && cmake --build build/Release --config Release'
cmd /s /c '"F:\VisualStudio\2022\BuildTools\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && ctest --test-dir build/Release --output-on-failure'
```

With osgEarth disabled:

```powershell
cmd /s /c '"F:\VisualStudio\2022\BuildTools\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && cmake -S . -B build/Release -DVAPORVIEW_ENABLE_OSGEARTH=OFF'
cmd /s /c '"F:\VisualStudio\2022\BuildTools\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && cmake --build build/Release --config Release'
cmd /s /c '"F:\VisualStudio\2022\BuildTools\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && ctest --test-dir build/Release --output-on-failure'
```
