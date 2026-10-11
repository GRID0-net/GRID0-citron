#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Citrosis contributors
# SPDX-License-Identifier: GPL-2.0-or-later
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
build_dir="${root}/build-macos"
jobs=1
package=0
configure_only=0
qt=0
dev_only=0
extra_cmake=()

usage() {
    cat <<'EOF'
Usage: ./build-citrosis-macos.sh [options] [-- CMake options]
  --setup             Install required Homebrew build dependencies and exit
  --build-dir PATH    Build directory (default: ./build-macos)
  --jobs N            Parallel compiler jobs (default: 1)
  --qt               Build the legacy Qt frontend instead of Swift
  --configure-only   Configure without compiling
  --package          Also create a ZIP of the finished app
  --dev-only         Only build the development bundle in bin/ (skip dist/Citrosis.app)
  --help             Show this help
EOF
}

[[ $(uname -s) == Darwin ]] || { echo 'This script requires macOS.' >&2; exit 1; }
while (($#)); do
    case "$1" in
        --setup)
            command -v brew >/dev/null || { echo 'Install Homebrew first.' >&2; exit 1; }
            HOMEBREW_NO_AUTO_UPDATE=1 HOMEBREW_NO_INSTALL_CLEANUP=1 brew install cmake ninja ffmpeg glslang autoconf automake libtool
            exit 0 ;;
        --build-dir|--jobs)
            (($# >= 2)) || { usage >&2; exit 1; }
            if [[ $1 == --build-dir ]]; then build_dir=$2; else jobs=$2; fi
            shift 2 ;;
        --qt) qt=1; shift ;;
        --configure-only) configure_only=1; shift ;;
        --package) package=1; shift ;;
        --dev-only) dev_only=1; shift ;;
        --help|-h) usage; exit 0 ;;
        --) shift; extra_cmake=("$@"); break ;;
        *) echo "Unknown option: $1" >&2; usage >&2; exit 1 ;;
    esac
done
[[ $jobs =~ ^[1-9][0-9]*$ ]] || { echo '--jobs must be a positive integer.' >&2; exit 1; }
for tool in cmake ninja brew glslangValidator python3; do
    command -v "$tool" >/dev/null || { echo "Missing $tool; run --setup." >&2; exit 1; }
done
xcrun --find clang++ >/dev/null
brew_prefix=$(brew --prefix)
ffmpeg_prefix=$(brew --prefix ffmpeg)
frontend_options=(-DENABLE_QT=OFF -DCITROSIS_NATIVE_APP=ON -DENABLE_SDL2=ON)
frontend_target=citrosis-mac
source_app=citrosis-mac.app
prefix_path="$ffmpeg_prefix;$brew_prefix"
if ((qt)); then
    qt_prefix=$(brew --prefix qtbase)
    svg_prefix=$(brew --prefix qtsvg)
    prefix_path="$qt_prefix;$svg_prefix;$prefix_path"
    frontend_options=(-DENABLE_QT=ON -DCITROSIS_NATIVE_APP=OFF -DUSE_SYSTEM_QT=ON -DQt6Svg_DIR="$svg_prefix/lib/cmake/Qt6Svg")
    frontend_target=citron
    source_app=citron.app
fi

cmake -S "$root" -B "$build_dir" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=14.0 \
    -DCMAKE_PREFIX_PATH="$prefix_path" \
    -DCITRON_USE_CPM=ON -DCPM_USE_LOCAL_PACKAGES=OFF \
    -DCITRON_USE_BUNDLED_VCPKG=OFF \
    -DCITRON_USE_BUNDLED_QT=OFF -DCITRON_USE_BUNDLED_FFMPEG=OFF \
    -DCITRON_TESTS=OFF -DENABLE_WEB_SERVICE=ON -DENABLE_OPENSSL=ON \
    -DCITRON_ENABLE_LTO=OFF -DCITRON_INSTALL_GIT_HOOKS=OFF \
    "${frontend_options[@]}" \
    ${extra_cmake[@]+"${extra_cmake[@]}"}
((configure_only)) && exit 0
cmake --build "$build_dir" --target "$frontend_target" --parallel "$jobs"
build_dir=$(cd "$build_dir" && pwd)
mkdir -p "$build_dir/bin/$source_app/Contents/Resources"
cp -f "$root/dist/citron.icns" "$build_dir/bin/$source_app/Contents/Resources/citron.icns"
codesign --force --sign - --entitlements "$root/dist/macos/Citrosis.entitlements" "$build_dir/bin/$source_app"
if [[ -f "$build_dir/bin/citron-cmd" ]]; then
    codesign --force --sign - --entitlements "$root/dist/macos/Citrosis.entitlements" "$build_dir/bin/citron-cmd"
fi
echo "Built: $build_dir/bin/$source_app"

((dev_only)) && exit 0

# Finished app: build-macos/dist/Citrosis.app (dependencies bundled, icon checked, signed).
dist_dir="$build_dir/dist"
mkdir -p "$dist_dir"
stage=$(mktemp -d "$dist_dir/.stage-XXXXXX")
trap 'rm -rf "$stage"' EXIT
app="$stage/Citrosis.app"
ditto "$build_dir/bin/$source_app" "$app"
# The bundle copy step can be skipped by Ninja when the icon is unchanged; always install it.
mkdir -p "$app/Contents/Resources"
cp -f "$root/dist/citron.icns" "$app/Contents/Resources/citron.icns"
if ((qt)); then
    "$qt_prefix/bin/macdeployqt" "$app" -always-overwrite -codesign=-
    python3 "$root/tools/macos/verify_bundle.py" "$app"
else
    python3 "$root/tools/macos/deploy_native.py" "$app" --entitlements "$root/dist/macos/Citrosis.entitlements"
fi
codesign --force --sign - --entitlements "$root/dist/macos/Citrosis.entitlements" "$app"
codesign --verify --deep --strict "$app"
# Replace only the finished app after deployment and verification pass.
rm -rf "$dist_dir/Citrosis.app"
mv "$app" "$dist_dir/Citrosis.app"
# Make Finder/Dock pick up a changed icon instead of a cached one.
touch "$dist_dir/Citrosis.app"
echo "Finished app: $dist_dir/Citrosis.app"

if ((package)); then
    ditto -c -k --keepParent "$dist_dir/Citrosis.app" "$dist_dir/Citrosis-$(uname -m).zip"
    echo "Packaged: $dist_dir/Citrosis-$(uname -m).zip"
fi
