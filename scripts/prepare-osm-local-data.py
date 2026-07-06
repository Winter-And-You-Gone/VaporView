#!/usr/bin/env python3
"""Prepare local OSM vector GeoPackages for VaporView's offline 3D map."""

from __future__ import annotations

import argparse
import os
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
        "natural = 'water'",
        "water.gpkg",
        (
            "SELECT * "
            "FROM multipolygons "
            "WHERE natural = 'water' "
            "OR hstore_get_value(other_tags, 'water') IS NOT NULL "
            "OR hstore_get_value(other_tags, 'waterway') IS NOT NULL"
        ),
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


def tool_executable_name(name: str) -> str:
    return f"{name}.exe" if sys.platform.startswith("win") else name


def candidate_gdal_dirs(project_root: Path, explicit_gdal_bin: Path | None) -> list[Path]:
    candidates: list[Path] = []
    if explicit_gdal_bin is not None:
        candidates.append(explicit_gdal_bin)

    env_gdal_bin = os.environ.get("GDAL_BIN")
    if env_gdal_bin:
        candidates.append(Path(env_gdal_bin))

    if os.environ.get("VAPORVIEW_GDAL_TOOL_SEARCH") == "PATH_ONLY":
        return candidates

    candidates.extend(
        [
            project_root / ".local_deps" / "vcpkg" / "packages" / "gdal_x64-windows" / "bin",
            project_root / ".local_deps" / "vcpkg" / "buildtrees" / "gdal" / "x64-windows-rel" / "apps",
            project_root / ".local_deps" / "vcpkg_installed" / "x64-windows" / "tools" / "gdal",
            project_root / ".local_deps" / "vcpkg_installed" / "x64-windows" / "bin",
        ]
    )

    if sys.platform.startswith("win"):
        candidates.extend(
            [
                Path("C:/OSGeo4W/bin"),
                Path("C:/OSGeo4W64/bin"),
                Path("C:/Program Files/QGIS 3.40/bin"),
                Path("C:/Program Files/QGIS 3.38/bin"),
                Path("C:/Program Files/QGIS 3.36/bin"),
                Path("C:/Program Files/QGIS 3.34/bin"),
                Path("C:/Program Files/QGIS 3.32/bin"),
            ]
        )

    unique: list[Path] = []
    seen: set[str] = set()
    for candidate in candidates:
        key = str(candidate.expanduser())
        if key not in seen:
            seen.add(key)
            unique.append(candidate.expanduser())
    return unique


def find_tool(name: str, project_root: Path, explicit_gdal_bin: Path | None = None) -> str | None:
    found = shutil.which(name)
    if found:
        return found

    executable_name = tool_executable_name(name)
    for gdal_dir in candidate_gdal_dirs(project_root, explicit_gdal_bin):
        candidate = gdal_dir / executable_name
        if candidate.is_file():
            return str(candidate)

    return None


def gdal_tool_hint(project_root: Path, explicit_gdal_bin: Path | None) -> str:
    searched = "\n  ".join(str(path) for path in candidate_gdal_dirs(project_root, explicit_gdal_bin))
    return (
        "Install GDAL/OGR command-line tools, add them to PATH, set GDAL_BIN, or pass --gdal-bin.\n"
        "Searched:\n  "
        f"{searched}"
    )


def first_existing_dir(paths: list[Path]) -> Path | None:
    for path in paths:
        if path.is_dir():
            return path
    return None


def gdal_runtime_env(project_root: Path, tool_path: str | None) -> dict[str, str]:
    env = os.environ.copy()
    path_entries: list[str] = []
    if tool_path:
        tool_dir = Path(tool_path).resolve().parent
        path_entries.append(str(tool_dir))
        if tool_dir.name.lower() == "apps":
            path_entries.append(str(tool_dir.parent))

    for path in [
        project_root / ".local_deps" / "vcpkg" / "buildtrees" / "gdal" / "x64-windows-rel",
        project_root / ".local_deps" / "vcpkg_installed" / "x64-windows" / "bin",
        project_root / ".local_deps" / "vcpkg" / "packages" / "gdal_x64-windows" / "bin",
    ]:
        if path.is_dir():
            path_entries.append(str(path))

    if path_entries:
        env["PATH"] = os.pathsep.join(path_entries + [env.get("PATH", "")])

    if not env.get("GDAL_DATA"):
        gdal_data = first_existing_dir(
            [
                project_root / ".local_deps" / "vcpkg_installed" / "x64-windows" / "share" / "gdal",
                project_root / ".local_deps" / "vcpkg" / "packages" / "gdal_x64-windows" / "share" / "gdal",
            ]
        )
        if gdal_data is not None:
            env["GDAL_DATA"] = str(gdal_data)

    proj_data = first_existing_dir(
        [
            project_root / ".local_deps" / "vcpkg_installed" / "x64-windows" / "share" / "proj",
            project_root / ".local_deps" / "vcpkg" / "packages" / "proj_x64-windows" / "share" / "proj",
        ]
    )
    if proj_data is not None:
        env.setdefault("PROJ_DATA", str(proj_data))
        env.setdefault("PROJ_LIB", str(proj_data))

    return env


def run(command: list[str], env: dict[str, str] | None = None) -> None:
    print("+", " ".join(command))
    subprocess.run(command, check=True, env=env)


def validate_outputs(output_dir: Path, ogrinfo: str | None, env: dict[str, str] | None = None) -> bool:
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
            env=env,
        )
        if result.returncode != 0:
            print(
                f"ERROR: {target.name} does not expose expected layer '{name}'.\n{result.stderr}",
                file=sys.stderr,
            )
            ok = False
        else:
            print(f"CHECK {name}: layer exists")
            if name == "buildings":
                if "extrusion_height_m" not in result.stdout:
                    print(
                        "ERROR: buildings.gpkg layer 'buildings' does not expose "
                        "the required extrusion_height_m field for local building extrusion.",
                        file=sys.stderr,
                    )
                    ok = False
                else:
                    print("CHECK buildings: extrusion_height_m field exists")
    return ok


def main() -> int:
    parser = argparse.ArgumentParser(
        description=(
            "Convert a local open OSM extract into roads/water/buildings/places "
            "GeoPackages for resources/maps/vaporview_full_local.earth. "
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
        help="Output directory. Defaults to resources/maps/osm under the project root.",
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
    parser.add_argument(
        "--gdal-bin",
        type=Path,
        default=None,
        help=(
            "Directory containing GDAL/OGR command-line tools such as ogr2ogr and "
            "ogrinfo. Useful for OSGeo4W, QGIS, or a manual project-local GDAL install."
        ),
    )
    args = parser.parse_args()

    project_root = args.project_root.resolve()
    source = args.source.resolve()
    output_dir = (args.output_dir or (project_root / "resources" / "maps" / "osm")).resolve()
    gdal_bin = args.gdal_bin.resolve() if args.gdal_bin is not None else None

    ogr2ogr = find_tool("ogr2ogr", project_root, gdal_bin)
    ogrinfo = find_tool("ogrinfo", project_root, gdal_bin)
    tool_env = gdal_runtime_env(project_root, ogr2ogr or ogrinfo)

    if args.check:
        if ogrinfo is None:
            print(
                "WARNING: ogrinfo was not found; checking file presence only. "
                + gdal_tool_hint(project_root, gdal_bin),
                file=sys.stderr,
            )
        return 0 if validate_outputs(output_dir, ogrinfo, env=tool_env) else 2

    if ogr2ogr is None:
        print(
            "ERROR: ogr2ogr was not found. " + gdal_tool_hint(project_root, gdal_bin),
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
        run(command, env=tool_env)

    print("OSM local vector data is ready:")
    for config in LAYER_CONFIGS.values():
        file_name = config[2]
        print(f"  {output_dir / file_name}")
    if not validate_outputs(output_dir, ogrinfo, env=tool_env):
        return 1
    print("MapDataManager will select FullLocalMap when Natural Earth, a DEM VRT, and all four GeoPackages exist.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
