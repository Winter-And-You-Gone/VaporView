#!/usr/bin/env python3
"""Prepare local imagery VRT files for VaporView's offline 3D map."""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path


IMAGERY_CONFIGS = {
    "sentinel2": {
        "folder": "sentinel2",
        "vrt": "sentinel2.vrt",
        "earth": "vaporview_with_sentinel2_imagery.earth",
        "label": "Sentinel-2",
    },
    "landsat": {
        "folder": "landsat",
        "vrt": "landsat.vrt",
        "earth": "vaporview_with_landsat_imagery.earth",
        "label": "Landsat",
    },
    "openaerialmap": {
        "folder": "openaerialmap",
        "vrt": "openaerialmap.vrt",
        "earth": "vaporview_with_openaerialmap_imagery.earth",
        "label": "OpenAerialMap",
    },
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
        "Install GDAL command-line tools, add them to PATH, set GDAL_BIN, or pass --gdal-bin.\n"
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


def imagery_paths(project_root: Path, key: str) -> tuple[Path, Path, Path, str, str]:
    config = IMAGERY_CONFIGS[key]
    imagery_dir = project_root / "resources" / "maps" / "imagery" / config["folder"]
    vrt_path = imagery_dir / config["vrt"]
    earth_path = project_root / "resources" / "maps" / config["earth"]
    expected_relative = f"imagery/{config['folder']}/{config['vrt']}"
    return imagery_dir, vrt_path, earth_path, expected_relative, config["label"]


def collect_tiles(imagery_dir: Path) -> list[Path]:
    if not imagery_dir.is_dir():
        return []
    return sorted(
        path
        for path in imagery_dir.iterdir()
        if path.is_file() and path.suffix.lower() in {".tif", ".tiff"}
    )


def validate_template(earth_path: Path, expected_relative: str) -> None:
    if not earth_path.is_file():
        raise RuntimeError(f"Expected imagery earth template not found: {earth_path}")
    text = earth_path.read_text(encoding="utf-8", errors="replace")
    if expected_relative not in text:
        raise RuntimeError(f"{earth_path.name} does not reference {expected_relative}")


def run(command: list[str], env: dict[str, str] | None = None) -> None:
    print("+", " ".join(command))
    subprocess.run(command, check=True, env=env)


def main() -> int:
    parser = argparse.ArgumentParser(
        description=(
            "Build or check a local imagery VRT for VaporView. The script only reads "
            "local GeoTIFF imagery and does not download data, call online map services, "
            "or use Cesium ion."
        ),
        epilog="Generated VRTs enable the 3D Map toolbar menu overlay entries.",
    )
    parser.add_argument(
        "imagery",
        choices=sorted(IMAGERY_CONFIGS.keys()),
        nargs="?",
        default="sentinel2",
        help="Imagery slot to prepare. Defaults to sentinel2.",
    )
    parser.add_argument(
        "--project-root",
        type=Path,
        default=Path(__file__).resolve().parents[1],
        help="VaporView project root. Defaults to the parent of scripts/.",
    )
    parser.add_argument(
        "--imagery-dir",
        type=Path,
        default=None,
        help=(
            "Input imagery tile directory. Defaults to resources/maps/imagery/<slot>. "
            "The generated VRT is still written to the canonical resources/maps/imagery "
            "path so MapDataManager and the local imagery toolbar menu can auto-detect it."
        ),
    )
    parser.add_argument(
        "--gdal-bin",
        type=Path,
        default=None,
        help=(
            "Directory containing GDAL command-line tools such as gdalbuildvrt and "
            "gdalinfo. Useful for OSGeo4W, QGIS, or a manual project-local GDAL install."
        ),
    )
    parser.add_argument(
        "--check",
        action="store_true",
        help="Only check local inputs, tool availability, generated VRT, and earth template.",
    )
    args = parser.parse_args()

    project_root = args.project_root.resolve()
    imagery_dir, vrt_path, earth_path, expected_relative, label = imagery_paths(project_root, args.imagery)
    tile_dir = args.imagery_dir.resolve() if args.imagery_dir is not None else imagery_dir

    imagery_dir.mkdir(parents=True, exist_ok=True)
    tiles = collect_tiles(tile_dir)
    if not tiles:
        print(
            f"ERROR: no GeoTIFF imagery tiles found in {tile_dir}. "
            "Place local .tif/.tiff imagery files there first; this script does not download data.",
            file=sys.stderr,
        )
        return 2

    gdal_bin = args.gdal_bin.resolve() if args.gdal_bin is not None else None
    gdalbuildvrt = find_tool("gdalbuildvrt", project_root, gdal_bin)
    if gdalbuildvrt is None:
        print(
            "ERROR: gdalbuildvrt was not found. " + gdal_tool_hint(project_root, gdal_bin),
            file=sys.stderr,
        )
        return 2

    gdalinfo = find_tool("gdalinfo", project_root, gdal_bin)
    tool_env = gdal_runtime_env(project_root, gdalbuildvrt or gdalinfo)
    try:
        validate_template(earth_path, expected_relative)
    except RuntimeError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 2

    if args.check:
        if not vrt_path.is_file():
            print(f"ERROR: expected VRT does not exist yet: {vrt_path}", file=sys.stderr)
            return 2
        if gdalinfo is not None:
            run([gdalinfo, str(vrt_path)], env=tool_env)
        print(f"Imagery check passed for {label}: {vrt_path}")
        print(f"Earth template references expected VRT: {earth_path}")
        return 0

    if vrt_path.exists():
        vrt_path.unlink()

    if tile_dir != imagery_dir:
        print(f"Using imagery tiles from: {tile_dir}")
        print(f"Writing auto-detect VRT to: {vrt_path}")

    run([gdalbuildvrt, str(vrt_path), *[str(tile) for tile in tiles]], env=tool_env)
    if not vrt_path.is_file():
        print(f"ERROR: expected VRT was not created: {vrt_path}", file=sys.stderr)
        return 1

    if gdalinfo is not None:
        run([gdalinfo, str(vrt_path)], env=tool_env)

    print(f"Prepared {label} imagery VRT: {vrt_path}")
    print(f"Validated earth template: {earth_path}")
    print("Start VaporView with -DVAPORVIEW_ENABLE_OSGEARTH=ON; the local imagery toolbar menu should enable this overlay.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
