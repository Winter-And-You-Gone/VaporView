# VaporView Offline Map Data

The first offline 3D map dataset uses Natural Earth II 1:50m shaded relief with water.

- Source: `https://naturalearth.s3.amazonaws.com/50m_raster/NE2_50M_SR_W.zip`
- License/status: Natural Earth data is public domain.
- Local earth file: `data/maps/vaporview_default.earth`
- Raster file after download: `data/maps/natural_earth/NE2_50M_SR_W/NE2_50M_SR_W.tif`

Download or refresh the local copy:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/download-natural-earth-map.ps1
```

The `data/` directory still ignores the large downloaded raster and zip files. Git tracks only the small `.earth`, `.vrt`, README, and download script needed to recreate the local map.
