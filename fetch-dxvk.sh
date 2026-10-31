#!/usr/bin/env bash
# Fetch and build dxvk-native, which provides the D3D9 (and, from PR2 on, D3D11)
# implementation the native Linux build renders through. There is no d3d9.dll /
# d3d11.dll on Linux; the client dlopen's libdxvk_d3d9.so instead (see the
# "d3d9.dll" mapping in Engine/Source/Os/Linux/OsLibrary.cpp).
#
# Pinned to an EXACT upstream tag so builds are reproducible. Bump deliberately.
# One native-only patch exposes DXVK's existing Vulkan depth resolve on all
# adapter vendors. Windows builds and other render states remain upstream.
#
#   ./fetch-dxvk.sh            # clone + build into .deps/dxvk, print the build dir
#   DXVK_DIR=/path ./fetch-dxvk.sh
set -euo pipefail

DXVK_REPO="https://github.com/doitsujin/dxvk.git"
DXVK_TAG="v3.1.1"
DXVK_DIR="${DXVK_DIR:-$(cd "$(dirname "$0")" && pwd)/.deps/dxvk}"
BUILD_SUBDIR="build-native-sdl3"
BUILD_DIR="$DXVK_DIR/$BUILD_SUBDIR"

if [ ! -d "$DXVK_DIR/.git" ]; then
    echo "fetch-dxvk: cloning doitsujin/dxvk $DXVK_TAG into $DXVK_DIR"
    git clone --branch "$DXVK_TAG" --depth 1 --recurse-submodules "$DXVK_REPO" "$DXVK_DIR"
else
    cur="$(git -C "$DXVK_DIR" describe --tags --always 2>/dev/null || echo unknown)"
    echo "fetch-dxvk: $DXVK_DIR already present at $cur (expected $DXVK_TAG); not re-cloning"
fi

# Keep the vendor-independent native depth path reproducible on fresh checkouts.
PATCH_PATH="$(cd "$(dirname "$0")" && pwd)/patches/dxvk-native-depth-copy.patch"
if git -C "$DXVK_DIR" apply --reverse --check "$PATCH_PATH" 2>/dev/null; then
    : # Already applied.
elif git -C "$DXVK_DIR" apply --check "$PATCH_PATH"; then
    git -C "$DXVK_DIR" apply "$PATCH_PATH"
else
    echo "fetch-dxvk: native depth-copy patch does not match this DXVK checkout" >&2
    exit 1
fi

# Build deps come from nix so the script is self-contained; if meson/ninja are
# already on PATH this still works (nix-shell just re-provides them).
run() {
    if command -v nix-shell >/dev/null 2>&1; then
        nix-shell -p meson ninja pkg-config gcc sdl3 vulkan-headers vulkan-loader glslang python3 \
            --run "$1"
    else
        bash -c "$1"
    fi
}

if [ ! -f "$BUILD_DIR/src/d3d9/libdxvk_d3d9.so" ]; then
    echo "fetch-dxvk: configuring + building dxvk-native (SDL3) -- this takes a few minutes"
    run "cd '$DXVK_DIR' && meson setup '$BUILD_SUBDIR' --buildtype release \
        -Dnative_sdl3=enabled -Dnative_sdl2=disabled -Dnative_glfw=disabled && \
        ninja -C '$BUILD_SUBDIR'"
fi

# Ninja is incremental; existing libraries must also pick up native patches.
run "ninja -C '$BUILD_DIR'"

if [ ! -f "$BUILD_DIR/src/d3d9/libdxvk_d3d9.so" ]; then
    echo "fetch-dxvk: ERROR -- build did not produce libdxvk_d3d9.so under $BUILD_DIR" >&2
    exit 1
fi

echo "fetch-dxvk: ready. Point the client build at it with:"
echo "    export DXVK_NATIVE_DIR='$BUILD_DIR'"
