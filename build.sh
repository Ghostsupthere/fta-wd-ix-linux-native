#!/usr/bin/env bash
# Cross-compile the x64 client from NixOS. Run inside nix-shell.
set -uo pipefail
cd "$(dirname "$0")"

if [ ! -d .xwin/crt/include ]; then
    echo "error: ./.xwin is not populated -- run ./fetch-sdk.sh first." >&2
    exit 1
fi

BUILD="${BUILD_DIR:-build/x64}"
TARGET="${1:-WowClient}"

cmake -S . -B "$BUILD" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE="$PWD/cmake/clang-cl-x64.cmake" \
    -DXWIN_DIR="$PWD/.xwin" \
    -DWOWDECOMP_DISABLE_SIGNATURE_CHECKS=ON \
    "${CMAKE_EXTRA_ARGS:-}" || exit 1

cmake --build "$BUILD" --target "$TARGET" --parallel "$(nproc)"
