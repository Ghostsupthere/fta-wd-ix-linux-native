#!/usr/bin/env bash
# Build the client as a native Linux ELF. Run inside `nix-shell shell-native.nix`.
#
# The sibling of ./build.sh, which cross-compiles the Windows x64 PE. Same
# ergonomics: optional target name as $1, BUILD_DIR and CMAKE_EXTRA_ARGS
# honoured.
#
#   nix-shell shell-native.nix --run ./build-native-cmake.sh
#   nix-shell shell-native.nix --run './build-native-cmake.sh Gx'   # one library
#
# D3D11 comes from dxvk-native, built out of tree by meson. Point
# DXVK_NATIVE_DIR at its build directory if it is not ~/Projects/dxvk/build-native-sdl3.
#
# Both client targets link natively: Wow -> bin/Linux/Wow64 and
# WowClient -> bin/Linux/WowClient64.
set -uo pipefail
cd "$(dirname "$0")"

BUILD="${BUILD_DIR:-build/linux}"
TARGET="${1:-Wow}"
DXVK_NATIVE_DIR="${DXVK_NATIVE_DIR:-$HOME/Projects/dxvk/build-native-sdl3}"

if [ ! -f "$DXVK_NATIVE_DIR/src/d3d11/libdxvk_d3d11.so" ]; then
    echo "error: no libdxvk_d3d11.so under $DXVK_NATIVE_DIR." >&2
    echo "       Build dxvk-native first; see the header of cmake/DxvkNative.cmake." >&2
    exit 1
fi

cmake -S . -B "$BUILD" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DDXVK_NATIVE_DIR="$DXVK_NATIVE_DIR" \
    -DWOWDECOMP_DISABLE_SIGNATURE_CHECKS=ON \
    ${CMAKE_EXTRA_ARGS:-} || exit 1

cmake --build "$BUILD" --target "$TARGET" --parallel "$(nproc)"
