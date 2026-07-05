#!/usr/bin/env python3
"""Prepare local OSM vector GeoPackages for VaporView's offline 3D map."""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
from pathlib import Path


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
    ),
    "places": (
        "points",
        "name IS NOT NULL",
        "places.gpkg",
    ),
}


def run(command: list[str]) -> None:
    print("+", " ".join(command))
    subprocess.run(command, check=True)


def main() -> int:
    parser = argparse.ArgumentParser(
        description=(
            "Convert a local open OSM extract into roads/water/buildings/places "
            "GeoPackages for data/maps/vaporview_full_local.earth. "
            "The generated layer names are roads, water, buildings, and places."
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
    args = parser.parse_args()

    if shutil.which("ogr2ogr") is None:
        print("ERROR: ogr2ogr was not found on PATH. Install GDAL/OGR first.", file=sys.stderr)
        return 2

    source = args.source.resolve()
    if not source.is_file():
        print(f"ERROR: source OSM extract does not exist: {source}", file=sys.stderr)
        return 2

    output_dir = args.output_dir or (args.project_root / "data" / "maps" / "osm")
    output_dir.mkdir(parents=True, exist_ok=True)

    for name, (source_layer, where_clause, file_name) in LAYER_CONFIGS.items():
        target = output_dir / file_name
        if target.exists():
            if not args.overwrite:
                print(f"SKIP {name}: {target} already exists; pass --overwrite to replace it.")
                continue
            target.unlink()

        run(
            [
                "ogr2ogr",
                "-f",
                "GPKG",
                str(target),
                str(source),
                source_layer,
                "-where",
                where_clause,
                "-nln",
                name,
                "-t_srs",
                "EPSG:4326",
                "-lco",
                "SPATIAL_INDEX=YES",
            ]
        )

    print("OSM local vector data is ready:")
    for _, (_, _, file_name) in LAYER_CONFIGS.items():
        print(f"  {output_dir / file_name}")
    print("MapDataManager will select FullLocalMap when Natural Earth, a DEM VRT, and all four GeoPackages exist.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
