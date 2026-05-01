#!/usr/bin/env bash
set -euo pipefail

action="${1:-build}"
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
preset_name="linux-arm64-gcc-release"
build_dir="${repo_root}/build/Release"

case "$(uname -m)" in
  aarch64|arm64)
    ;;
  *)
    echo "This build entrypoint is for native Linux ARM64 hosts." >&2
    exit 1
    ;;
esac

for tool in gcc g++ cmake ninja; do
  if ! command -v "${tool}" >/dev/null 2>&1; then
    echo "${tool} was not found in PATH." >&2
    exit 1
  fi
done

cmake_args=(--preset "${preset_name}")
if [[ -n "${VAPORVIEW_QT6_PREFIX:-}" ]]; then
  cmake_args+=(-DCMAKE_PREFIX_PATH="${VAPORVIEW_QT6_PREFIX}")
fi

cd "${repo_root}"

case "${action}" in
  clean)
    rm -rf "${build_dir}"
    ;;
  configure)
    cmake "${cmake_args[@]}"
    ;;
  build)
    cmake "${cmake_args[@]}"
    cmake --build --preset "${preset_name}"
    ;;
  rebuild)
    rm -rf "${build_dir}"
    cmake "${cmake_args[@]}"
    cmake --build --preset "${preset_name}"
    ;;
  test)
    cmake "${cmake_args[@]}"
    cmake --build --preset "${preset_name}"
    ctest --preset "${preset_name}"
    ;;
  *)
    echo "Usage: $0 [configure|build|rebuild|test|clean]" >&2
    exit 2
    ;;
esac
