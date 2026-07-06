# Local Aircraft Model

Place one local aircraft model here to provide an automatic default replacement for
the built-in 3D Map marker:

- `vaporview_aircraft.osgb`
- `vaporview_aircraft.osg`
- `vaporview_aircraft.glb`
- `vaporview_aircraft.gltf`

The 3D Map loads the first existing file in that order when no user-selected model
is stored in `QSettings("VaporView", "Map3D")` as `Map3D/aircraftModelPath`.
From the 3D Map toolbar, `加载飞机模型` can select any local `.osgb`, `.osg`,
`.glb`, or `.gltf` model path, and `内置飞机标记` clears that setting. If the
selected/default file is missing or the installed OSG runtime cannot read it,
VaporView keeps the built-in aircraft marker and reports the fallback in the
diagnostics panel.

Use only local assets with licensing terms you can satisfy. Do not place online service
URLs, Cesium ion references, or commercial map keys here.
