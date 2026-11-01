#!/usr/bin/env bash
# Install the native Linux client overlay into a From The Ashes base client folder.
#   ./install.sh                      installs into the folder you run it from (open a terminal in your client folder)
#   ./install.sh /path/to/client      installs into that folder instead
#   add --force to replace shader packs that are already there
# Adds Wow64, the FTA-Linux folder and the offline shader packs. Nothing the Windows client uses is changed;
# shader packs and WarcraftXL.cfg that are already there are kept unless --force is given.
set -euo pipefail

here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
fail() { printf 'install: %s\n' "$*" >&2; exit 1; }

target="" force=0
for arg in "$@"; do
    case $arg in
        --force) force=1 ;;
        --help|-h) sed -n '2,6p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
        -*) fail "unknown option $arg" ;;
        *) [[ -z $target ]] || fail 'give at most one folder'; target=$arg ;;
    esac
done
target=${target:-$PWD}
[[ -d $target ]] || fail "$target is not a folder"
target=$(cd -- "$target" && pwd -P)
for name in Data Interface; do
    [[ -d $target/$name ]] || fail "$target has no $name folder. Run this from your From The Ashes client folder (the one with Wow.exe), or give that folder as an argument"
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

if [[ $target == "$PWD" ]]; then prefix=.; else prefix=$target; fi
printf 'Installed into %s\nNext, from that folder:\n  %s/FTA-Linux/install-deps.sh --install   (system libraries, if the check below lists any)\n  %s/FTA-Linux/play.sh --check\n  %s/FTA-Linux/play.sh\n\n' \
    "$target" "$prefix" "$prefix" "$prefix"

"$target/FTA-Linux/install-deps.sh" || true
