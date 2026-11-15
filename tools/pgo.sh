#!/usr/bin/env bash
# Pack / unpack the PGO training profile (see WOWDECOMP_PGO in cmake/LinuxNative.cmake).
#
#   tools/pgo.sh unpack   pgo/wow64-profile.tar.xz -> pgo-data/   (what PGO=1 ./build-native-cmake.sh does)
#   tools/pgo.sh pack     pgo-data/ -> pgo/wow64-profile.tar.xz   (after a new training run)
#
# Retraining: build with WOWDECOMP_PGO=GENERATE, play (world, city, dungeon, UI), quit normally, then `pack`
# and commit the archive. A big source change (the Lua port, say) leaves functions without a matching
# profile; they just build unguided, so retrain after large changes.
set -euo pipefail
cd "$(dirname "$0")/.."
ARCHIVE=pgo/wow64-profile.tar.xz
case "${1:-}" in
    unpack)
        [ -f "$ARCHIVE" ] || { echo "no $ARCHIVE" >&2; exit 1; }
        rm -rf pgo-data && mkdir pgo-data
        tar -xJf "$ARCHIVE" -C pgo-data ;;
    pack)
        [ -d pgo-data ] || { echo "no pgo-data/ (run a GENERATE build first)" >&2; exit 1; }
        mkdir -p pgo
        tar -cf - -C pgo-data . | xz -9e > "$ARCHIVE" ;;
    *) echo "usage: $0 pack|unpack" >&2; exit 2 ;;
esac
