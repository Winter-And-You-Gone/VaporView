#!/usr/bin/env python3
"""Prepare local OSM vector GeoPackages for VaporView's offline 3D map."""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
from pathlib import Path


BUILDING_HEIGHT_SQL = (
    "COALESCE("
    "CAST(REPLACE(REPLACE(hstore_get_value(other_tags, 'height'), 'm', ''), ' ', '') AS REAL), "
    "CAST(REPLACE(REPLACE(hstore_get_value(other_tags, 'building:height'), 'm', ''), ' ', '') AS REAL), "
    "CAST(hstore_get_value(other_tags, 'building:levels') AS REAL) * 3.0, "
    "CAST(hstore_get_value(other_tags, 'levels') AS REAL) * 3.0, "
    "10.0)"
)


LAYER_CONFIGS = {
    "roads": (
        "lines",
        "highway IS NOT NULL",
        "roads.gpkg",
    ),
    "water": (
        "multipolygons",
        "natural = 'water' OR water IS NOT NULL OR waterway IS NOT NULL",
        "water.gpkg",
    ),
    "buildings": (
        "multipolygons",
        "building IS NOT NULL",
        "buildings.gpkg",
        (
            "SELECT *, "
            f"{BUILDING_HEIGHT_SQL} AS extrusion_height_m "
            "FROM multipolygons "
            "WHERE building IS NOT NULL"
        ),
    ),
    "places": (
        "points",
        "name IS NOT NULL",
        "places.gpkg",
    ),
}


def find_tool(name: str, project_root: Path) -> str | None:
    found = shutil.which(name)
    if found:
        return found

    if sys.platform.startswith("win"):
        candidate = project_root / ".local_deps" / "vcpkg_installed" / "x64-windows" / "tools" / "gdal" / f"{name}.exe"
        if candidate.is_file():
            return str(candidate)

    return None


def run(command: list[str]) -> None:
    print("+", " ".join(command))
    subprocess.run(command, check=True)


def validate_outputs(output_dir: Path, ogrinfo: str | None) -> bool:
    ok = True
    for name, config in LAYER_CONFIGS.items():
        file_name = config[2]
        target = output_dir / file_name
        if not target.is_file():
            print(f"ERROR: missing generated GeoPackage for {name}: {target}", file=sys.stderr)
            ok = False
            continue

        print(f"FOUND {name}: {target}")
        if ogrinfo is None:
            continue

        result = subprocess.run(
            [ogrinfo, "-ro", "-so", str(target), name],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        if result.returncode != 0:
            print(
                f"ERROR: {target.name} does not expose expected layer '{name}'.\n{result.stderr}",
                file=sys.stderr,
            )
            ok = False
        else:
            print(f"CHECK {name}: layer exists")
    return ok


def main() -> int:
    parser = argparse.ArgumentParser(
        description=(
            "Convert a local open OSM extract into roads/water/buildings/places "
            "GeoPackages for data/maps/vaporview_full_local.earth. "
            "The generated layer names are roads, water, buildings, and places. "
            "The buildings layer includes extrusion_height_m derived from local "
            "OSM height or building level tags, with a 10 m fallback."
        )
    )
    parser.add_argument(
        "source",
        type=Path,
        help="Local .osm.pbf or .osm extract. The script does not download data.",
    )
    parser.add_argument(
        "--project-root",
        type=Path,
        default=Path(__file__).resolve().parents[1],
        help="VaporView project root. Defaults to the parent of scripts/.",
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=None,
        help="Output directory. Defaults to data/maps/osm under the project root.",
    )
    parser.add_argument(
        "--overwrite",
        action="store_true",
        help="Replace existing generated GeoPackage files.",
    )
    parser.add_argument(
        "--check",
        action="store_true",
        help="Only validate the expected generated GeoPackages and layer names; do not convert.",
    )
    args = parser.parse_args()

    project_root = args.project_root.resolve()
    source = args.source.resolve()
    output_dir = (args.output_dir or (project_root / "data" / "maps" / "osm")).resolve()

    ogr2ogr = find_tool("ogr2ogr", project_root)
    ogrinfo = find_tool("ogrinfo", project_root)

    if args.check:
        if ogrinfo is None:
            print(
                "WARNING: ogrinfo was not found on PATH or in "
                ".local_deps/vcpkg_installed/x64-windows/tools/gdal; checking file presence only.",
                file=sys.stderr,
            )
        return 0 if validate_outputs(output_dir, ogrinfo) else 2

    if ogr2ogr is None:
        print(
            "ERROR: ogr2ogr was not found on PATH or in "
            ".local_deps/vcpkg_installed/x64-windows/tools/gdal. Install GDAL/OGR first.",
            file=sys.stderr,
        )
        return 2

    if not source.is_file():
        print(f"ERROR: source OSM extract does not exist: {source}", file=sys.stderr)
        return 2

    output_dir.mkdir(parents=True, exist_ok=True)

    for name, config in LAYER_CONFIGS.items():
        source_layer, where_clause, file_name = config[:3]
        target = output_dir / file_name
        if target.exists():
            if not args.overwrite:
                print(f"SKIP {name}: {target} already exists; pass --overwrite to replace it.")
                continue
            target.unlink()

        command = [
            ogr2ogr,
            "-f",
            "GPKG",
            str(target),
            str(source),
            "-nln",
            name,
            "-t_srs",
            "EPSG:4326",
            "-lco",
            "SPATIAL_INDEX=YES",
        ]
        if len(config) >= 4:
            command.extend(["-dialect", "SQLITE", "-sql", config[3]])
        else:
            command.extend([source_layer, "-where", where_clause])
        run(command)

    print("OSM local vector data is ready:")
    for config in LAYER_CONFIGS.values():
        file_name = config[2]
        print(f"  {output_dir / file_name}")
    if not validate_outputs(output_dir, ogrinfo):
        return 1
    print("MapDataManager will select FullLocalMap when Natural Earth, a DEM VRT, and all four GeoPackages exist.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
