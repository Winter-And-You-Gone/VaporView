#!/usr/bin/env python3
"""Build VaporView's compact global coarse DEM from NOAA ETOPO 2022 60s surface GeoTIFF.

Outputs a tiled, DEFLATE+PREDICTOR=2 compressed Int16 GeoTIFF with internal
overviews and oceans clamped to 0 m. Uses the project's bundled gdal.dll (or
system libgdal) via ctypes so no extra GIS CLI tools or imagecodecs are needed.
"""

from __future__ import annotations

import argparse
import ctypes
import os
import sys
import urllib.request
from pathlib import Path

import numpy as np

ETOPO_URL = (
    "https://www.ngdc.noaa.gov/mgg/global/relief/ETOPO2022/data/60s/"
    "60s_surface_elev_gtif/ETOPO_2022_v1_60s_N90W180_surface.tif"
)

WGS84_WKT = (
    'GEOGCS["WGS 84",DATUM["WGS_1984",SPHEROID["WGS 84",6378137,298.257223563,'
    'AUTHORITY["EPSG","7030"]],AUTHORITY["EPSG","6326"]],PRIMEM["Greenwich",0,'
    'AUTHORITY["EPSG","8901"]],UNIT["degree",0.0174532925199433,'
    'AUTHORITY["EPSG","9122"]],AXIS["Latitude",NORTH],AXIS["Longitude",EAST],'
    'AUTHORITY["EPSG","4326"]]'
)


class GDALRasterIOExtraArg(ctypes.Structure):
    _fields_ = [
        ("nVersion", ctypes.c_int),
        ("eResampleAlg", ctypes.c_int),
        ("pfnProgress", ctypes.c_void_p),
        ("pProgressData", ctypes.c_void_p),
        ("bFloatingPointWindowValidity", ctypes.c_int),
        ("dfXOff", ctypes.c_double),
        ("dfYOff", ctypes.c_double),
        ("dfXSize", ctypes.c_double),
        ("dfYSize", ctypes.c_double),
    ]


def find_gdal_library(repo_root: Path) -> Path:
    candidates = [
        repo_root / "build" / "Release" / "gdal.dll",
        repo_root / ".local_deps" / "vcpkg_installed" / "x64-windows" / "bin" / "gdal.dll",
    ]
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    if os.name != "nt":
        for name in ("libgdal.so", "libgdal.so.34", "libgdal.so.33", "libgdal.so.32", "libgdal.dylib"):
            try:
                ctypes.CDLL(name)
                return Path(name)
            except OSError:
                continue
    raise FileNotFoundError("Could not locate gdal.dll / libgdal.")


def configure_gdal_data_env(repo_root: Path) -> None:
    proj_candidates = [
        repo_root / "build" / "Release" / "share" / "proj",
        repo_root / ".local_deps" / "vcpkg_installed" / "x64-windows" / "share" / "proj",
    ]
    for candidate in proj_candidates:
        if (candidate / "proj.db").is_file():
            os.environ.setdefault("PROJ_DATA", str(candidate))
            os.environ.setdefault("PROJ_LIB", str(candidate))
            break
    gdal_candidates = [
        repo_root / "build" / "Release" / "share" / "gdal",
        repo_root / ".local_deps" / "vcpkg_installed" / "x64-windows" / "share" / "gdal",
    ]
    for candidate in gdal_candidates:
        if candidate.is_dir():
            os.environ.setdefault("GDAL_DATA", str(candidate))
            break


def load_gdal(gdal_lib: Path, repo_root: Path) -> ctypes.CDLL:
    configure_gdal_data_env(repo_root)
    if os.name == "nt":
        os.add_dll_directory(str(gdal_lib.parent))
    gdal = ctypes.CDLL(str(gdal_lib))

    gdal.GDALAllRegister.restype = None
    gdal.GDALOpen.argtypes = [ctypes.c_char_p, ctypes.c_int]
    gdal.GDALOpen.restype = ctypes.c_void_p
    gdal.GDALGetRasterXSize.argtypes = [ctypes.c_void_p]
    gdal.GDALGetRasterXSize.restype = ctypes.c_int
    gdal.GDALGetRasterYSize.argtypes = [ctypes.c_void_p]
    gdal.GDALGetRasterYSize.restype = ctypes.c_int
    gdal.GDALGetDriverByName.argtypes = [ctypes.c_char_p]
    gdal.GDALGetDriverByName.restype = ctypes.c_void_p
    gdal.GDALCreate.argtypes = [
        ctypes.c_void_p,
        ctypes.c_char_p,
        ctypes.c_int,
        ctypes.c_int,
        ctypes.c_int,
        ctypes.c_int,
        ctypes.POINTER(ctypes.c_char_p),
    ]
    gdal.GDALCreate.restype = ctypes.c_void_p
    gdal.GDALSetGeoTransform.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_double)]
    gdal.GDALSetGeoTransform.restype = ctypes.c_int
    gdal.GDALSetProjection.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
    gdal.GDALSetProjection.restype = ctypes.c_int
    gdal.GDALSetMetadataItem.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p]
    gdal.GDALSetMetadataItem.restype = ctypes.c_int
    gdal.GDALGetRasterBand.argtypes = [ctypes.c_void_p, ctypes.c_int]
    gdal.GDALGetRasterBand.restype = ctypes.c_void_p
    gdal.GDALRasterIOEx.argtypes = [
        ctypes.c_void_p,
        ctypes.c_int,
        ctypes.c_int,
        ctypes.c_int,
        ctypes.c_int,
        ctypes.c_int,
        ctypes.c_void_p,
        ctypes.c_int,
        ctypes.c_int,
        ctypes.c_int,
        ctypes.c_longlong,
        ctypes.c_longlong,
        ctypes.POINTER(GDALRasterIOExtraArg),
    ]
    gdal.GDALRasterIOEx.restype = ctypes.c_int
    gdal.GDALRasterIO.argtypes = [
        ctypes.c_void_p,
        ctypes.c_int,
        ctypes.c_int,
        ctypes.c_int,
        ctypes.c_int,
        ctypes.c_int,
        ctypes.c_void_p,
        ctypes.c_int,
        ctypes.c_int,
        ctypes.c_int,
        ctypes.c_int,
        ctypes.c_int,
    ]
    gdal.GDALRasterIO.restype = ctypes.c_int
    gdal.GDALBuildOverviews.argtypes = [
        ctypes.c_void_p,
        ctypes.c_char_p,
        ctypes.c_int,
        ctypes.POINTER(ctypes.c_int),
        ctypes.c_int,
        ctypes.POINTER(ctypes.c_int),
        ctypes.c_void_p,
        ctypes.c_void_p,
    ]
    gdal.GDALBuildOverviews.restype = ctypes.c_int
    gdal.CPLSetConfigOption.argtypes = [ctypes.c_char_p, ctypes.c_char_p]
    gdal.CPLSetConfigOption.restype = None
    gdal.GDALClose.argtypes = [ctypes.c_void_p]
    gdal.GDALClose.restype = None

    gdal.GDALAllRegister()
    return gdal


def read_resampled_grid(gdal: ctypes.CDLL, source_path: Path, width: int, height: int) -> np.ndarray:
    GA_ReadOnly = 0
    GF_Read = 0
    GDT_Float32 = 6
    GRIORA_Average = 5

    ds = gdal.GDALOpen(str(source_path).encode("utf-8"), GA_ReadOnly)
    if not ds:
        raise RuntimeError(f"GDALOpen failed for {source_path}")
    try:
        src_w = gdal.GDALGetRasterXSize(ds)
        src_h = gdal.GDALGetRasterYSize(ds)
        print(f"Source raster: {src_w}x{src_h}")
        band = gdal.GDALGetRasterBand(ds, 1)
        buf = np.empty((height, width), dtype=np.float32)
        extra = GDALRasterIOExtraArg()
        extra.nVersion = 1
        extra.eResampleAlg = GRIORA_Average
        err = gdal.GDALRasterIOEx(
            band,
            GF_Read,
            0,
            0,
            src_w,
            src_h,
            buf.ctypes.data,
            width,
            height,
            GDT_Float32,
            0,
            0,
            ctypes.byref(extra),
        )
        if err != 0:
            raise RuntimeError(f"GDALRasterIOEx read failed with error {err}")
    finally:
        gdal.GDALClose(ds)

    buf = np.where(np.isfinite(buf), buf, 0.0)
    # Clamp bathymetry to sea level (0 m) so oceans compress to near-zero bytes.
    np.maximum(buf, 0.0, out=buf)
    return np.clip(np.rint(buf), 0.0, 32767.0).astype(np.int16)


def write_geotiff_with_gdal(gdal: ctypes.CDLL, out_path: Path, grid: np.ndarray) -> None:
    gdal.CPLSetConfigOption(b"COMPRESS_OVERVIEW", b"DEFLATE")
    gdal.CPLSetConfigOption(b"PREDICTOR_OVERVIEW", b"2")
    gdal.CPLSetConfigOption(b"ZLEVEL_OVERVIEW", b"9")
    gdal.CPLSetConfigOption(b"INTERLEAVE_OVERVIEW", b"PIXEL")

    driver = gdal.GDALGetDriverByName(b"GTiff")
    if not driver:
        raise RuntimeError("GDAL GTiff driver is unavailable.")

    height, width = grid.shape
    out_path.parent.mkdir(parents=True, exist_ok=True)
    tmp_path = out_path.with_suffix(".tmp.tif")
    if tmp_path.exists():
        tmp_path.unlink()

    opts = [
        b"TILED=YES",
        b"BLOCKXSIZE=256",
        b"BLOCKYSIZE=256",
        b"COMPRESS=DEFLATE",
        b"ZLEVEL=9",
        b"PREDICTOR=2",
        None,
    ]
    opts_arr = (ctypes.c_char_p * len(opts))(*opts)

    GDT_Int16 = 3
    GF_Write = 1
    ds = gdal.GDALCreate(driver, str(tmp_path).encode("utf-8"), width, height, 1, GDT_Int16, opts_arr)
    if not ds:
        raise RuntimeError(f"GDALCreate failed for {tmp_path}")

    try:
        gt = (ctypes.c_double * 6)(-180.0, 360.0 / width, 0.0, 90.0, 0.0, -180.0 / height)
        if gdal.GDALSetGeoTransform(ds, gt) != 0:
            raise RuntimeError("GDALSetGeoTransform failed.")
        if gdal.GDALSetProjection(ds, WGS84_WKT.encode("ascii")) != 0:
            raise RuntimeError("GDALSetProjection failed.")
        gdal.GDALSetMetadataItem(ds, b"AREA_OR_POINT", b"Area", None)

        band = gdal.GDALGetRasterBand(ds, 1)
        contiguous = np.ascontiguousarray(grid, dtype=np.int16)
        err = gdal.GDALRasterIO(
            band,
            GF_Write,
            0,
            0,
            width,
            height,
            contiguous.ctypes.data,
            width,
            height,
            GDT_Int16,
            0,
            0,
        )
        if err != 0:
            raise RuntimeError(f"GDALRasterIO write failed with error {err}")

        overviews = (ctypes.c_int * 4)(2, 4, 8, 16)
        if gdal.GDALBuildOverviews(ds, b"AVERAGE", 4, overviews, 0, None, None, None) != 0:
            raise RuntimeError("GDALBuildOverviews failed.")
    finally:
        gdal.GDALClose(ds)

    tmp_path.replace(out_path)


def main() -> int:
    repo_root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description="Build compact global coarse DEM GeoTIFF from NOAA ETOPO 2022.")
    parser.add_argument("--source", type=Path, help="Local ETOPO_2022_v1_60s_N90W180_surface.tif path")
    parser.add_argument(
        "--output",
        type=Path,
        default=repo_root / "resources" / "maps" / "terrain" / "global" / "etopo2022_global_coarse.tif",
        help="Output GeoTIFF path",
    )
    parser.add_argument("--width", type=int, default=8192, help="Output width in pixels (default: 8192)")
    parser.add_argument("--height", type=int, default=4096, help="Output height in pixels (default: 4096)")
    args = parser.parse_args()

    source_path = args.source
    downloaded = False
    if source_path is None:
        source_path = args.output.parent / "ETOPO_2022_v1_60s_N90W180_surface.tif"
        source_path.parent.mkdir(parents=True, exist_ok=True)
        print(f"Downloading NOAA ETOPO 2022 60s surface GeoTIFF to {source_path} ...")
        urllib.request.urlretrieve(ETOPO_URL, source_path)
        downloaded = True

    if not source_path.is_file():
        print(f"Source file not found: {source_path}", file=sys.stderr)
        return 1

    gdal_lib = find_gdal_library(repo_root)
    print(f"Using GDAL library: {gdal_lib}")
    gdal = load_gdal(gdal_lib, repo_root)

    grid = read_resampled_grid(gdal, source_path, args.width, args.height)
    print(f"Resampled shape: {grid.shape}, min={int(grid.min())} m, max={int(grid.max())} m")

    print(f"Writing compressed GeoTIFF -> {args.output}")
    write_geotiff_with_gdal(gdal, args.output, grid)
    size_mb = args.output.stat().st_size / (1024 * 1024)
    print(f"Done: {args.output} ({size_mb:.2f} MiB)")

    if downloaded:
        source_path.unlink(missing_ok=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
