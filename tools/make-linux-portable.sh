#!/usr/bin/env bash
# Build a portable overlay for the native Linux client: the Wow64 executable, its own glibc/loader and
# dxvk-native, a launcher and an installer. The overlay is installed INTO an existing From The Ashes base
# client folder (install.sh), so it holds no game data and no symlinks.
#
#   tools/make-linux-portable.sh [OUTPUT_DIR]
#
# Needs the built bin/Linux/Wow64 and the dxvk-native build under .deps (see shell-native.nix), and patchelf.
# The executable is built against a Nix glibc, so the overlay carries that glibc and starts through its
# loader (PT_INTERP is rewritten to a path relative to the client folder; the launcher runs from there).
set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)
out=${1:-$root/../../wd-ix-linux-native-portable}
dxvk=${DXVK_NATIVE_DIR:-$root/.deps/dxvk/build-native-sdl3}
exe=$root/bin/Linux/Wow64
visuals=$root/../../From_The_Ashes_Client/Utils/WarcraftXL/visuals

fail() { printf 'make-linux-portable: %s\n' "$*" >&2; exit 1; }
command -v patchelf >/dev/null || fail 'patchelf is required'
[[ -x $exe ]] || fail "missing $exe (build it first)"
for name in d3d9 d3d11 dxgi; do
    [[ -f $dxvk/src/$name/libdxvk_$name.so.0.30101 ]] || fail "missing dxvk $name build under $dxvk"
done

# Store directories the executable was linked against.
runpath=$(readelf -d "$exe" | sed -n 's/.*Library runpath: \[\(.*\)\]/\1/p')
store_dir() { tr ':' '\n' <<<"$runpath" | grep -m1 -- "$1" || fail "no $1 in the executable's runpath"; }
glibc=$(store_dir '-glibc-')
gcclib=$(store_dir '-gcc-.*-lib/')
xvid=$(store_dir '-xvidcore-')

rm -rf -- "$out"
payload=$out/payload
lib=$payload/FTA-Linux/lib
mkdir -p -- "$lib" "$payload/FTA-Linux/dxvk" "$payload/Utils/WarcraftXL/visuals"

# Real files only: no symlinks anywhere in the overlay.
for name in ld-linux-x86-64.so.2 libc.so.6 libm.so.6 libdl.so.2 libpthread.so.0 libresolv.so.2 librt.so.1 \
            libmvec.so.1 libutil.so.1 libanl.so.1 libnss_files.so.2 libnss_dns.so.2; do
    cp -L -- "$glibc/$name" "$lib/$name"
done
cp -L -- "$gcclib/libstdc++.so.6" "$gcclib/libgcc_s.so.1" "$lib/"
cp -L -- "$xvid/libxvidcore.so.4" "$lib/"
chmod u+w -- "$lib"/*
for name in libstdc++.so.6 libgcc_s.so.1 libxvidcore.so.4; do patchelf --set-rpath '$ORIGIN' "$lib/$name"; done

for name in d3d9 d3d11 dxgi; do
    cp -L -- "$dxvk/src/$name/libdxvk_$name.so.0.30101" "$payload/FTA-Linux/dxvk/libdxvk_$name.so.0"
    chmod u+w -- "$payload/FTA-Linux/dxvk/libdxvk_$name.so.0"
    patchelf --set-rpath '$ORIGIN' "$payload/FTA-Linux/dxvk/libdxvk_$name.so.0"
done
# The engine opens the dxvk libraries by their unversioned names.
for name in d3d9 d3d11 dxgi; do
    cp -- "$payload/FTA-Linux/dxvk/libdxvk_$name.so.0" "$payload/FTA-Linux/dxvk/libdxvk_$name.so"
done

cp -- "$exe" "$payload/Wow64"
chmod u+w -- "$payload/Wow64"
patchelf --set-interpreter 'FTA-Linux/lib/ld-linux-x86-64.so.2' --set-rpath '$ORIGIN/FTA-Linux/lib' "$payload/Wow64"
strip --strip-unneeded "$payload/Wow64" 2>/dev/null || true

# Offline-built shader packs (derived from the client's own shaders; building them needs Wine).
for pack in grass_instanced grass_shaders light_shaders wmo_shaders; do
    if [[ -f $visuals/$pack.pack ]]; then cp -- "$visuals/$pack.pack" "$payload/Utils/WarcraftXL/visuals/"
    else printf 'note: %s.pack not found; the overlay will not carry it\n' "$pack" >&2; fi
done

cp -- "$root/tools/portable/play.sh" "$root/tools/portable/install-deps.sh" "$payload/FTA-Linux/"
cp -- "$root/tools/portable/install.sh" "$out/install.sh"
cp -- "$root/tools/portable/README.md" "$out/README.md"
chmod +x "$payload/FTA-Linux/play.sh" "$payload/FTA-Linux/install-deps.sh" "$out/install.sh" "$payload/Wow64"
printf 'commit: %s\nbuilt: %s\nglibc: %s\n' "$(git -C "$root" rev-parse --short HEAD 2>/dev/null || echo unknown)" \
    "$(date -u +%Y-%m-%dT%H:%MZ)" "$(basename "$(dirname "$glibc")")" >"$payload/FTA-Linux/VERSION.txt"

# One-click installer: the launcher script followed by the overlay archive (self-extracting).
installer=$(dirname -- "$out")/FTA-Linux-Installer.run
tarball=$out.tar.gz
tar -C "$out" -czf "$tarball" .
sum=$(sha256sum "$tarball" | cut -d' ' -f1)
# Written aside and moved into place, so nobody copies a half-written file.
{ sed "s/@PAYLOAD_SHA256@/$sum/" "$root/tools/portable/installer-header.sh"; cat "$tarball"; } >"$installer.tmp"
chmod +x "$installer.tmp"
mv -f -- "$installer.tmp" "$installer"
( cd -- "$(dirname -- "$installer")" && sha256sum "$(basename -- "$installer")" "$(basename -- "$tarball")" >FTA-Linux-SHA256SUMS.txt )

if find "$out" -type l | grep -q .; then fail 'the overlay contains symlinks'; fi
printf 'Portable overlay written to %s (%s)\nArchive: %s\nOne-click installer: %s (%s)\n' "$out" "$(du -sh "$out" | cut -f1)" "$tarball" "$installer" "$(du -sh "$installer" | cut -f1)"
