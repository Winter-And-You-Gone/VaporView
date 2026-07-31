#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="$repo_root/build/Release"
work_dir="$build_dir/ifw-work"
stage_dir="$work_dir/stage"
packages_dir="$work_dir/packages"
output_dir="${VAPORVIEW_IFW_OUTPUT_DIR:-$build_dir/ifw}"
ifw_bin="${VAPORVIEW_IFW_BIN:-}"
repository_url=""
skip_build=0

usage() {
    printf 'Usage: %s [--ifw-bin DIR] [--repository-url URL] [--output-dir DIR] [--skip-build]\n' "$0"
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --ifw-bin) ifw_bin="$2"; shift 2 ;;
        --repository-url) repository_url="$2"; shift 2 ;;
        --output-dir) output_dir="$2"; shift 2 ;;
        --skip-build) skip_build=1; shift ;;
        -h|--help) usage; exit 0 ;;
        *) printf 'Unknown argument: %s\n' "$1" >&2; usage >&2; exit 2 ;;
    esac
done

if [[ "$skip_build" -eq 0 ]]; then
    cmake -S "$repo_root" --preset linux-x64-gcc-release -DVAPORVIEW_ENABLE_OSGEARTH=ON
    cmake --build --preset linux-x64-gcc-release
fi

binarycreator="${ifw_bin:+$ifw_bin/binarycreator}"
repogen="${ifw_bin:+$ifw_bin/repogen}"
binarycreator="${binarycreator:-$(command -v binarycreator || true)}"
repogen="${repogen:-$(command -v repogen || true)}"
[[ -x "$binarycreator" ]] || { printf 'binarycreator was not found.\n' >&2; exit 1; }
[[ -x "$repogen" ]] || { printf 'repogen was not found.\n' >&2; exit 1; }

version="$(sed -nE 's/^project\(VaporView VERSION ([0-9]+\.[0-9]+\.[0-9]+).*/\1/p' "$repo_root/CMakeLists.txt")"
[[ -n "$version" ]] || { printf 'Could not determine project version.\n' >&2; exit 1; }
release_date="$(date +%F)"
case "$work_dir" in
    "$build_dir"/*) ;;
    *) printf 'Refusing to remove unexpected IFW work directory: %s\n' "$work_dir" >&2; exit 1 ;;
esac
if [[ -z "$work_dir" || "$work_dir" == "/" || "$work_dir" == "$repo_root" || "$work_dir" == "$build_dir" ]]; then
    printf 'Refusing to remove a broad IFW work directory: %s\n' "$work_dir" >&2
    exit 1
fi
if [[ -z "$output_dir" || "$output_dir" == "/" || "$output_dir" == "$repo_root" || "$output_dir" == "$build_dir" ]]; then
    printf 'Refusing to use a broad IFW output directory: %s\n' "$output_dir" >&2
    exit 1
fi
rm -rf -- "$work_dir"
mkdir -p "$stage_dir" "$packages_dir" "$output_dir"

for executable in VaporView VaporViewSky VaporViewSkyCore VaporViewSkyTui; do
    source_path="$(find "$build_dir" -type f -name "$executable" -not -path '*/ifw-work/*' -print -quit)"
    [[ -n "$source_path" ]] || { printf 'Missing executable: %s\n' "$executable" >&2; exit 1; }
    cp "$source_path" "$stage_dir/$executable"
done

for library in "$build_dir"/*.so*; do
    [[ -e "$library" ]] && cp "$library" "$stage_dir/"
done

resource_source="$(find "$build_dir" -type f -path '*/resources/modern_style.qss' -not -path '*/ifw-work/*' -print -quit)"
[[ -n "$resource_source" ]] || { printf 'Runtime resources were not staged.\n' >&2; exit 1; }
resource_root="$(dirname "$resource_source")"
mkdir -p "$stage_dir/resources"
for resource in modern_style.qss combo_arrow_down.xpm combo_arrow_up.xpm sky_startup_logo_ansi.txt; do
    [[ -f "$resource_root/$resource" ]] && cp "$resource_root/$resource" "$stage_dir/resources/"
done
for directory in lucide VaproViewLOGO; do
    [[ -d "$resource_root/$directory" ]] && cp -a "$resource_root/$directory" "$stage_dir/resources/"
done

for shared in gdal proj proj4; do
    [[ -d "$build_dir/share/$shared" ]] && mkdir -p "$stage_dir/share" && cp -a "$build_dir/share/$shared" "$stage_dir/share/"
done
mkdir -p "$stage_dir/drivers" "$stage_dir/licenses"
cp -a "$repo_root/packaging/ifw/drivers/." "$stage_dir/drivers/"
cp "$repo_root/LICENSE" "$stage_dir/licenses/"
[[ -f "$repo_root/third_party/rtklib/LICENSE.txt" ]] && cp "$repo_root/third_party/rtklib/LICENSE.txt" "$stage_dir/licenses/"

package_root="$packages_dir/com.vaporview.core"
mkdir -p "$package_root/meta" "$package_root/data"
cp "$repo_root/packaging/ifw/packages/com.vaporview.core/meta/installscript.qs" "$package_root/meta/"
cp "$repo_root/LICENSE" "$package_root/meta/license.txt"
sed -e "s/@VAPORVIEW_VERSION@/$version/g" -e "s/@VAPORVIEW_RELEASE_DATE@/$release_date/g" \
    "$repo_root/packaging/ifw/packages/com.vaporview.core/meta/package.xml.in" > "$package_root/meta/package.xml"
cp -a "$stage_dir/." "$package_root/data/"

remote_repositories=""
if [[ -n "$repository_url" ]]; then
    remote_repositories="<RemoteRepositories><Repository><Url>$repository_url</Url><Enabled>1</Enabled></Repository></RemoteRepositories>"
fi
sed -e "s/@VAPORVIEW_VERSION@/$version/g" -e "s#@VAPORVIEW_TARGET_DIR@#/opt/VaporView#g" -e "s#@VAPORVIEW_REMOTE_REPOSITORIES@#$remote_repositories#g" \
    "$repo_root/packaging/ifw/config.xml.in" > "$work_dir/config.xml"
cp "$repo_root/packaging/ifw/control.qs" "$work_dir/"
cp "$repo_root/packaging/ifw/installer.qss" "$work_dir/"

installer_path="$output_dir/VaporView-$version-linux-x64-setup.run"
rm -f "$installer_path"
"$binarycreator" --offline-only -c "$work_dir/config.xml" -p "$packages_dir" "$installer_path"

repository_path="$output_dir/repository"
case "$repository_path" in
    "$output_dir"/*) ;;
    *) printf 'Refusing to remove unexpected repository directory: %s\n' "$repository_path" >&2; exit 1 ;;
esac
rm -rf -- "$repository_path"
mkdir -p "$repository_path"
"$repogen" -p "$packages_dir" "$repository_path"

printf 'Created: %s\n' "$installer_path"
printf 'Staging: %s\n' "$stage_dir"
