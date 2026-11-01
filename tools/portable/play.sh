#!/usr/bin/env bash
# Start the native Linux client installed in this From The Ashes client folder.
#   FTA-Linux/play.sh            start the game (extra arguments go to the client)
#   FTA-Linux/play.sh --check    verify the install and the system libraries; start nothing
set -euo pipefail

here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
client=$(dirname -- "$here")
cd -- "$client"

fail() {
    printf 'FTA Linux: %s\n' "$*" >&2
    if command -v notify-send >/dev/null; then notify-send 'FTA Linux Client' "$*" 2>/dev/null || true; fi
    exit 1
}

mode=play
case ${1:-} in
    --help|-h) printf 'Usage: %s [--check | game arguments...]\n' "$0"; exit 0 ;;
    --check) mode=check; shift ;;
esac

[[ -x ./Wow64 ]] || fail "Wow64 is missing from $client (run install.sh again)"
for name in Data Interface; do
    [[ -d $name ]] || fail "$client has no $name folder: install into the From The Ashes base client"
done
mkdir -p -- Logs Cache WTF Errors

# Order: this overlay's libraries first, then the system's. The overlay carries glibc, the C++ runtime,
# xvidcore and dxvk-native; SDL3, curl, ALSA, PulseAudio and the Vulkan driver come from the system.
libpath="$here/lib:$here/dxvk"
for dir in /usr/lib /usr/lib64 /usr/lib/x86_64-linux-gnu /lib/x86_64-linux-gnu /lib64 /lib /usr/local/lib \
           /usr/lib/pulseaudio /usr/lib/x86_64-linux-gnu/pulseaudio; do
    [[ -d $dir ]] && libpath+=":$dir"
done
# Applied to the game process only: exported, it would make the system's own tools (grep, tee, ...) load
# this overlay's glibc and crash.
libpath+=${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}
unset LD_LIBRARY_PATH
export DXVK_WSI_DRIVER=${DXVK_WSI_DRIVER:-SDL3}

loader=$here/lib/ld-linux-x86-64.so.2
listing=$("$loader" --library-path "$libpath" --list ./Wow64 2>&1) || fail "the bundled loader cannot start Wow64: $listing (run FTA-Linux/install-deps.sh to check and install them)"
missing=$(grep 'not found' <<<"$listing" || true)
if [[ -n $missing ]]; then
    printf '%s\n' "$missing" >&2
    fail 'system libraries are missing (see the list above). Run FTA-Linux/install-deps.sh to check and install them'
fi

if [[ $mode == check ]]; then
    printf 'Client folder: %s\nOverlay: %s\n' "$client" "$(tr '\n' ' ' <"$here/VERSION.txt")"
    printf 'All shared libraries resolve.\n'
    for pack in light_shaders wmo_shaders grass_shaders grass_instanced; do
        [[ -f Utils/WarcraftXL/visuals/$pack.pack ]] || printf 'note: Utils/WarcraftXL/visuals/%s.pack is missing (that feature stays off)\n' "$pack"
    done
    [[ -f WarcraftXL.cfg ]] || printf 'note: no WarcraftXL.cfg; native features use their defaults\n'
    printf 'Check passed; no game was started. GPU presentation and server reachability were not tested.\n'
    exit 0
fi

log=Logs/native-launch-$(date +%Y%m%d-%H%M%S)-$$.log
printf 'FTA Linux launch log: %s\n' "$client/$log"
status=0
env LD_LIBRARY_PATH="$libpath" ./Wow64 "$@" 2>&1 | tee -- "$log" >/dev/null || status=${PIPESTATUS[0]}
if (( status )); then fail "client exited with status $status. See $client/$log"; fi
