#!/usr/bin/env python3
"""Prepare VaporView's offline real-3D dataset for Hangzhou Xihu District."""

from __future__ import annotations

import argparse
import datetime as dt
import json
import math
import os
import shutil
import subprocess
import sys
import urllib.parse
import urllib.request
from pathlib import Path


DEFAULT_PLACE = "西湖区, 杭州市, 浙江省, 中国"
NOMINATIM_URL = "https://nominatim.openstreetmap.org/search"
EARTH_SEARCH_URL = "https://earth-search.aws.element84.com/v1/search"
USER_AGENT = "VaporView-local-map/1.0 (offline dataset preparation)"


def candidate_gdal_dirs(project_root: Path) -> list[Path]:
    return [
        project_root / ".local_deps" / "vcpkg" / "buildtrees" / "gdal" / "x64-windows-rel" / "apps",
        project_root / ".local_deps" / "vcpkg_installed" / "x64-windows" / "tools" / "gdal",
        Path("C:/OSGeo4W/bin"),
        Path("C:/OSGeo4W64/bin"),
    ]


def find_tool(name: str, project_root: Path) -> str:
    executable = f"{name}.exe" if sys.platform.startswith("win") else name
    found = shutil.which(name)
    if found:
        return found
    for directory in candidate_gdal_dirs(project_root):
        candidate = directory / executable
        if candidate.is_file():
            return str(candidate)
    raise RuntimeError(f"{name} was not found; searched PATH and project-local GDAL directories")


def find_generator(project_root: Path, explicit: Path | None) -> str:
    if explicit and explicit.is_file():
        return str(explicit)
    names = ["vaporview_building_tileset.exe", "vaporview_building_tileset"]
    roots = [
        project_root / "build" / "Release",
        project_root / "build" / "Release" / "Release",
    ]
    for root in roots:
        for name in names:
            candidate = root / name
            if candidate.is_file():
                return str(candidate)
    raise RuntimeError(
        "vaporview_building_tileset was not found; build it first with "
        "cmake --build build/Release --config Release --target vaporview_building_tileset"
    )


def runtime_env(project_root: Path, proxy: str | None) -> dict[str, str]:
    env = os.environ.copy()
    path_entries = [
        project_root / ".local_deps" / "vcpkg" / "buildtrees" / "gdal" / "x64-windows-rel" / "apps",
        project_root / ".local_deps" / "vcpkg" / "buildtrees" / "gdal" / "x64-windows-rel",
        project_root / ".local_deps" / "vcpkg_installed" / "x64-windows" / "bin",
    ]
    env["PATH"] = os.pathsep.join([str(path) for path in path_entries if path.is_dir()] + [env.get("PATH", "")])
    gdal_data = project_root / ".local_deps" / "vcpkg_installed" / "x64-windows" / "share" / "gdal"
    proj_data = project_root / ".local_deps" / "vcpkg_installed" / "x64-windows" / "share" / "proj"
    if gdal_data.is_dir():
        env["GDAL_DATA"] = str(gdal_data)
    if proj_data.is_dir():
        env["PROJ_DATA"] = str(proj_data)
        env["PROJ_LIB"] = str(proj_data)
    env["CPL_VSIL_CURL_ALLOWED_EXTENSIONS"] = ".tif,.tiff"
    env["GDAL_HTTP_MULTIRANGE"] = "YES"
    env["VSI_CACHE"] = "TRUE"
    env["VSI_CACHE_SIZE"] = str(64 * 1024 * 1024)
    if proxy:
        env["HTTP_PROXY"] = proxy
        env["HTTPS_PROXY"] = proxy
    return env


def open_json(url: str, *, payload: dict | None = None, proxy: str | None = None) -> dict | list:
    handlers = []
    if proxy:
        handlers.append(urllib.request.ProxyHandler({"http": proxy, "https": proxy}))
    opener = urllib.request.build_opener(*handlers)
    data = json.dumps(payload).encode() if payload is not None else None
    headers = {"User-Agent": USER_AGENT}
    if data is not None:
        headers["Content-Type"] = "application/json"
    request = urllib.request.Request(url, data=data, headers=headers)
    with opener.open(request, timeout=60) as response:
        return json.load(response)


def resolve_xihu_boundary(place: str, output_path: Path, proxy: str | None) -> tuple[list[float], dict]:
    query = urllib.parse.urlencode(
        {
            "q": place,
            "format": "jsonv2",
            "polygon_geojson": 1,
            "limit": 5,
            "accept-language": "zh-CN",
        }
    )
    results = open_json(f"{NOMINATIM_URL}?{query}", proxy=proxy)
    if not isinstance(results, list) or not results:
        raise RuntimeError(f"Nominatim returned no result for {place}")
    selected = next(
        (item for item in results if item.get("type") in {"administrative", "boundary"}),
        results[0],
    )
    south, north, west, east = map(float, selected["boundingbox"])
    feature_collection = {
        "type": "FeatureCollection",
        "name": "hangzhou_xihu_district",
        "features": [
            {
                "type": "Feature",
                "properties": {
                    "display_name": selected.get("display_name"),
                    "osm_type": selected.get("osm_type"),
                    "osm_id": selected.get("osm_id"),
                    "licence": selected.get("licence"),
                    "source": "OpenStreetMap Nominatim",
                },
                "geometry": selected["geojson"],
            }
        ],
    }
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(json.dumps(feature_collection, ensure_ascii=False, indent=2), encoding="utf-8")
    return [west, south, east, north], selected


def sentinel_sources(bbox: list[float], proxy: str | None) -> list[dict]:
    end = dt.datetime.now(dt.timezone.utc)
    start = end - dt.timedelta(days=365)
    payload = {
        "collections": ["sentinel-2-l2a"],
        "bbox": bbox,
        "datetime": f"{start:%Y-%m-%dT00:00:00Z}/{end:%Y-%m-%dT23:59:59Z}",
        "limit": 100,
    }
    search = open_json(EARTH_SEARCH_URL, payload=payload, proxy=proxy)
    features = search.get("features", [])
    candidates: dict[str, dict] = {}
    for feature in features:
        visual = feature.get("assets", {}).get("visual", {}).get("href")
        if not visual:
            continue
        item_id = feature.get("id", "")
        parts = item_id.split("_")
        mgrs_tile = parts[1] if len(parts) > 1 else item_id
        cloud = float(feature.get("properties", {}).get("eo:cloud_cover", 999.0))
        existing = candidates.get(mgrs_tile)
        if existing is None or cloud < existing["cloud_cover"]:
            candidates[mgrs_tile] = {
                "item_id": item_id,
                "mgrs_tile": mgrs_tile,
                "datetime": feature.get("properties", {}).get("datetime"),
                "cloud_cover": cloud,
                "href": visual,
            }
    if not candidates:
        raise RuntimeError("No Sentinel-2 visual assets were found for the AOI")
    return sorted(candidates.values(), key=lambda item: item["mgrs_tile"])


def copernicus_dem_sources(bbox: list[float]) -> list[str]:
    west, south, east, north = bbox
    longitudes = range(math.floor(west), math.floor(east) + 1)
    latitudes = range(math.floor(south), math.floor(north) + 1)
    sources = []
    for latitude in latitudes:
        for longitude in longitudes:
            lat_token = f"N{latitude:02d}" if latitude >= 0 else f"S{abs(latitude):02d}"
            lon_token = f"E{longitude:03d}" if longitude >= 0 else f"W{abs(longitude):03d}"
            stem = f"Copernicus_DSM_COG_10_{lat_token}_00_{lon_token}_00_DEM"
            sources.append(f"https://copernicus-dem-30m.s3.amazonaws.com/{stem}/{stem}.tif")
    return sources


def run(command: list[str], env: dict[str, str]) -> None:
    print("+", " ".join(command), flush=True)
    subprocess.run(command, check=True, env=env)


def warp_remote_rasters(
    gdalwarp: str,
    sources: list[str],
    destination: Path,
    bbox: list[float],
    resolution: float,
    env: dict[str, str],
) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    west, south, east, north = bbox
    command = [
        gdalwarp,
        "-overwrite",
        "-multi",
        "-wo",
        "NUM_THREADS=ALL_CPUS",
        "-te_srs",
        "EPSG:4326",
        "-te",
        str(west),
        str(south),
        str(east),
        str(north),
        "-t_srs",
        "EPSG:4326",
        "-tr",
        str(resolution),
        str(resolution),
        "-r",
        "bilinear",
        "-co",
        "TILED=YES",
        "-co",
        "COMPRESS=DEFLATE",
        "-co",
        "BIGTIFF=IF_SAFER",
    ]
    command.extend(f"/vsicurl/{source}" for source in sources)
    command.append(str(destination))
    run(command, env)


def build_vrt(gdalbuildvrt: str, vrt_path: Path, source_path: Path, env: dict[str, str]) -> None:
    run([gdalbuildvrt, "-overwrite", str(vrt_path), str(source_path)], env)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Prepare offline Sentinel-2, Copernicus DEM, and OSM building tiles for Hangzhou Xihu District."
    )
    parser.add_argument("--project-root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--place", default=DEFAULT_PLACE)
    parser.add_argument("--generator", type=Path, default=None)
    parser.add_argument("--proxy", default=None, help="Optional HTTP/HTTPS proxy, for example http://127.0.0.1:7890")
    parser.add_argument("--overwrite", action="store_true")
    args = parser.parse_args()

    project_root = args.project_root.resolve()
    boundary_path = project_root / "resources" / "maps" / "aoi" / "xihu_district.geojson"
    imagery_tif = project_root / "resources" / "maps" / "imagery" / "sentinel2" / "sentinel2.tif"
    imagery_vrt = project_root / "resources" / "maps" / "imagery" / "sentinel2" / "sentinel2.vrt"
    imagery_metadata = project_root / "resources" / "maps" / "imagery" / "sentinel2" / "sentinel2.source.json"
    dem_tif = (
        project_root
        / "resources"
        / "maps"
        / "terrain"
        / "copernicus_dem_glo30"
        / "copernicus_dem_glo30_xihu.tif"
    )
    dem_vrt = (
        project_root
        / "resources"
        / "maps"
        / "terrain"
        / "copernicus_dem_glo30"
        / "copernicus_dem_glo30.vrt"
    )
    buildings = project_root / "resources" / "maps" / "osm" / "buildings.gpkg"
    tileset_dir = project_root / "resources" / "maps" / "tiles3d" / "local"

    for path in [imagery_tif, imagery_vrt, dem_tif, dem_vrt, tileset_dir / "tileset.json"]:
        if path.exists() and not args.overwrite:
            raise RuntimeError(f"{path} already exists; pass --overwrite to replace generated data")

    gdalwarp = find_tool("gdalwarp", project_root)
    gdalbuildvrt = find_tool("gdalbuildvrt", project_root)
    generator = find_generator(project_root, args.generator.resolve() if args.generator else None)
    env = runtime_env(project_root, args.proxy)

    bbox, boundary = resolve_xihu_boundary(args.place, boundary_path, args.proxy)
    sentinel = sentinel_sources(bbox, args.proxy)
    dem_sources = copernicus_dem_sources(bbox)

    print("AOI:", boundary.get("display_name"), bbox)
    print("Sentinel-2 sources:", [(item["mgrs_tile"], item["cloud_cover"]) for item in sentinel])
    print("Copernicus DEM sources:", dem_sources)

    warp_remote_rasters(gdalwarp, [item["href"] for item in sentinel], imagery_tif, bbox, 0.0001, env)
    build_vrt(gdalbuildvrt, imagery_vrt, imagery_tif, env)
    imagery_metadata.write_text(
        json.dumps(
            {
                "aoi": boundary.get("display_name"),
                "bbox": bbox,
                "sources": sentinel,
                "licence_note": "Sentinel-2 L2A public COG imagery; verify downstream attribution requirements.",
            },
            ensure_ascii=False,
            indent=2,
        ),
        encoding="utf-8",
    )

    warp_remote_rasters(gdalwarp, dem_sources, dem_tif, bbox, 0.000277777777777778, env)
    build_vrt(gdalbuildvrt, dem_vrt, dem_tif, env)

    run(
        [
            generator,
            "--input",
            str(buildings),
            "--output",
            str(tileset_dir),
            "--bbox",
            ",".join(str(value) for value in bbox),
            "--clip",
            str(boundary_path),
            "--dem",
            str(dem_tif),
            "--tile-size",
            "0.025",
            "--fallback-base-height",
            "20",
            "--overwrite",
        ],
        env,
    )
    print("Prepared VaporView real-3D local data for Hangzhou Xihu District.")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as error:
        print(f"ERROR: {error}", file=sys.stderr)
        raise SystemExit(2)
