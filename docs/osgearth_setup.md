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
      plugins/osgPlugins-3.6.5/
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

## Common Problems

### `osgEarthConfig.cmake` Not Found

Configure was run with `VAPORVIEW_ENABLE_OSGEARTH=ON`, but CMake cannot find osgEarth. Check that osgEarth is installed and that either `.local_deps/vcpkg_installed/x64-windows` exists or `CMAKE_PREFIX_PATH` points at the install prefix.

### OSG Plugin Not Found

Symptoms include blank maps, unknown file extension messages, or image/earth files failing to load. Check `OSG_LIBRARY_PATH` in the 3D Map diagnostics panel. It should point at a directory like `osgPlugins-3.6.5`.

### Missing `GDAL_DATA` or `PROJ_DATA`

GDAL and PROJ may fail to interpret GeoTIFF/VRT coordinate systems when their data directories are missing. Open the diagnostics panel and confirm `GDAL_DATA` and `PROJ_DATA` resolve to real directories.

### Black Screen

Check these in order:

1. Build with `-DVAPORVIEW_ENABLE_OSGEARTH=ON`.
2. Confirm OSG plugins, `GDAL_DATA`, and `PROJ_DATA` in diagnostics.
3. Confirm `data/maps/vaporview_default.earth` and Natural Earth files exist.
4. Use `重载最佳本地地图`.
5. Use `重置视角`.
6. If no map data exists, confirm the local grid fallback appears.

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
