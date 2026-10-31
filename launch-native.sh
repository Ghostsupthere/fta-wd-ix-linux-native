#!/usr/bin/env bash
# Run the existing native development client; never rebuild or change game settings.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
runtime=0
if [[ ${1:-} == --_runtime ]]; then runtime=1; shift; fi
mode=play
case ${1:-} in
    --help|-h)
        printf 'Usage: %s [--check | game arguments...]\n--check validates this setup without opening the game.\n' "$0"
        exit 0 ;;
    --check) mode=check; shift ;;
esac
fail() {
    printf 'FTA native: %s\n' "$*" >&2
    if command -v notify-send >/dev/null; then notify-send 'FTA Native Client' "$*" 2>/dev/null || true; fi
    exit 1
}
export FTA_NATIVE_GAME_DIR=${FTA_NATIVE_GAME_DIR:-$root/bin/Linux}
export DXVK_NATIVE_DIR=${DXVK_NATIVE_DIR:-$root/.deps/dxvk/build-native-sdl3}
export DXVK_WSI_DRIVER=SDL3
[[ -x "$FTA_NATIVE_GAME_DIR/Wow64" ]] || fail "Missing executable: $FTA_NATIVE_GAME_DIR/Wow64"
for name in Data Interface; do
    [[ -d "$FTA_NATIVE_GAME_DIR/$name" ]] || fail "Missing/broken $name directory in $FTA_NATIVE_GAME_DIR"
done
for name in d3d9 d3d11 dxgi; do
    [[ -f "$DXVK_NATIVE_DIR/src/$name/libdxvk_$name.so" ]] || fail "Missing DXVK $name library under $DXVK_NATIVE_DIR"
done
if (( runtime )); then
    export LD_LIBRARY_PATH="$DXVK_NATIVE_DIR/src/d3d9:${LD_LIBRARY_PATH:-}:/usr/lib"
    cd -- "$FTA_NATIVE_GAME_DIR"
    if [[ $mode == check ]]; then
        printf 'Executable: %s/Wow64\nDXVK: %s\n' "$PWD" "$DXVK_NATIVE_DIR"
        dependencies=$(ldd ./Wow64 2>&1) || fail "$dependencies"
        [[ $dependencies != *'not found'* ]] || fail "$dependencies"
        printf 'Linked libraries resolved. SDL3/Vulkan environment configured.\n'
        for name in Data Interface DBFilesClient Utils WTF fonts; do
            if [[ -L $name ]]; then printf '%s -> %s\n' "$name" "$(readlink -- "$name")"; fi
        done
        printf 'Check passed; no game was started. GPU presentation and server reachability were not tested.\n'
        exit 0
    fi
    exec ./Wow64 "$@"
fi
nix=$(command -v nix-shell || true)
if [[ -z $nix ]]; then
    for candidate in "$HOME/.nix-profile/bin/nix-shell" /nix/var/nix/profiles/default/bin/nix-shell; do
        if [[ -x $candidate ]]; then nix=$candidate; break; fi
    done
fi
[[ -n $nix ]] || fail 'nix-shell is missing. This development build requires its Nix runtime.'
# --run is a shell command: Bash %q preserves spaces and literal game arguments.
args=("$root/launch-native.sh" --_runtime)
if [[ $mode == check ]]; then args+=(--check); fi
args+=("$@")
printf -v command '%q ' "${args[@]}"
if [[ $mode == check ]]; then exec "$nix" "$root/shell-native.nix" --run "$command"; fi
mkdir -p -- "$FTA_NATIVE_GAME_DIR/Logs"
log="$FTA_NATIVE_GAME_DIR/Logs/native-launch-$(date +%Y%m%d-%H%M%S)-$$.log"
printf 'FTA native launch log: %s\n' "$log"
status=0
"$nix" "$root/shell-native.nix" --run "$command" >"$log" 2>&1 || status=$?
if (( status )); then
    printf 'FTA native exited with status %s. See %s\n' "$status" "$log" >&2
    if command -v notify-send >/dev/null; then notify-send 'FTA Native Client' "Client exited ($status). Log: $log" 2>/dev/null || true; fi
fi
exit "$status"
