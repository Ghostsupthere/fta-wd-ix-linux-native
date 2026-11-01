#!/usr/bin/env bash
# Install the native Linux client overlay into a From The Ashes base client folder.
#   ./install.sh /path/to/From_The_Ashes_Client [--force]
# Adds Wow64, the FTA-Linux folder and the offline shader packs. Nothing the Windows client uses is changed;
# shader packs and WarcraftXL.cfg that are already there are kept unless --force is given.
set -euo pipefail

here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
fail() { printf 'install: %s\n' "$*" >&2; exit 1; }

target=${1:-}
force=0
[[ ${2:-} == --force ]] && force=1
[[ -n $target && $target != --help && $target != -h ]] || { printf 'Usage: %s /path/to/base-client [--force]\n' "$0"; exit 0; }
[[ -d $target ]] || fail "$target is not a folder"
target=$(cd -- "$target" && pwd -P)
for name in Data Interface; do
    [[ -d $target/$name ]] || fail "$target has no $name folder; point this at the From The Ashes client (the one with Wow.exe)"
done
[[ -f $target/Wow.exe || -f $target/WarcraftXL.dll ]] || printf 'warning: no Wow.exe or WarcraftXL.dll in %s; continuing\n' "$target" >&2
[[ -d $here/payload ]] || fail 'payload folder is missing next to install.sh'

# Overwrite our own files (Wow64, FTA-Linux); keep anything else that exists unless --force.
rm -rf -- "$target/FTA-Linux"
cp -r -- "$here/payload/FTA-Linux" "$target/FTA-Linux"
cp -f -- "$here/payload/Wow64" "$target/Wow64"
mkdir -p -- "$target/Utils/WarcraftXL/visuals"
for pack in "$here"/payload/Utils/WarcraftXL/visuals/*.pack; do
    [[ -f $pack ]] || continue
    if (( force )) || [[ ! -e $target/Utils/WarcraftXL/visuals/$(basename "$pack") ]]; then
        cp -f -- "$pack" "$target/Utils/WarcraftXL/visuals/"
    fi
done
chmod +x "$target/Wow64" "$target/FTA-Linux/play.sh" "$target/FTA-Linux/install-deps.sh"

printf 'Installed into %s\nSystem libraries:   %s/FTA-Linux/install-deps.sh [--install]\nCheck the install:  %s/FTA-Linux/play.sh --check\nStart the game:     %s/FTA-Linux/play.sh\n' \
    "$target" "$target" "$target" "$target"

"$target/FTA-Linux/install-deps.sh" || true
