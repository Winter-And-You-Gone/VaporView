# Local Aircraft Model

Place one local aircraft model here to replace the built-in 3D Map marker:

- `vaporview_aircraft.osgb`
- `vaporview_aircraft.osg`
- `vaporview_aircraft.glb`
- `vaporview_aircraft.gltf`

The 3D Map loads the first existing file in that order. If the file is missing or the
installed OSG runtime cannot read it, VaporView keeps the built-in aircraft marker and
reports the fallback in the diagnostics panel.

Use only local assets with licensing terms you can satisfy. Do not place online service
URLs, Cesium ion references, or commercial map keys here.
