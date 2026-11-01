#!/usr/bin/env bash
# From The Ashes native Linux client: one-click installer (self-extracting).
#
#   Double-click this file, or run:  bash FTA-Linux-Installer.run
#
# It finds your From The Ashes client folder (the one with Wow.exe), installs the Linux client into it,
# offers to install the system libraries it needs, adds a menu shortcut and offers to start the game.
# Nothing the Windows client uses is changed.
#
#   --client DIR     use this client folder instead of looking for one
#   --yes            answer the questions with their defaults (installs missing libraries with sudo);
#                    only installs into --client DIR or the folder you run it from
#   --no-shortcut    do not add the menu shortcut
set -euo pipefail

PAYLOAD_SHA256=@PAYLOAD_SHA256@

self=$(readlink -f -- "$0")
client="" assume=0 shortcut=1 interm=0 pause=0
while (( $# )); do
    case $1 in
        --client) client=${2:?--client needs a folder}; shift ;;
        --yes|-y) assume=1 ;;
        --no-shortcut) shortcut=0 ;;
        --in-terminal) interm=1 ;;
        --pause) pause=1 ;;
        --help|-h) sed -n '2,14p' "$self" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) printf 'Unknown option: %s\n' "$1" >&2; exit 2 ;;
    esac
    shift
done

# Opened from a file manager there is no terminal to show progress or ask for a password: open one.
if (( ! interm )) && [[ ! -t 0 || ! -t 1 ]]; then
    relaunch=("$self" --in-terminal --pause)
    (( assume )) && relaunch+=(--yes)
    (( ! shortcut )) && relaunch+=(--no-shortcut)
    [[ -n $client ]] && relaunch+=(--client "$client")
    for term in x-terminal-emulator gnome-terminal ptyxis konsole xfce4-terminal mate-terminal lxterminal tilix kitty alacritty foot xterm; do
        command -v "$term" >/dev/null 2>&1 || continue
        case $term in
            gnome-terminal|ptyxis) exec "$term" -- bash "${relaunch[@]}" ;;
            xfce4-terminal|mate-terminal) exec "$term" -x bash "${relaunch[@]}" ;;
            kitty|foot) exec "$term" bash "${relaunch[@]}" ;;
            *) exec "$term" -e bash "${relaunch[@]}" ;;
        esac
    done
    # No terminal emulator found: carry on with dialogs only.
fi

pause_and_exit() {
    local code=$1
    if (( pause )); then read -r -p 'Press Enter to close this window. ' _ || true; fi
    exit "$code"
}
fail() {
    printf '\nInstall failed: %s\n' "$*" >&2
    if [[ ! -t 1 ]] && command -v zenity >/dev/null 2>&1; then zenity --error --text="$*" 2>/dev/null || true; fi
    pause_and_exit 1
}
ask() { # ask "question" default(Y|N): returns 0 for yes
    local def=$2 reply
    if (( assume )); then [[ $def == Y ]]; return; fi
    if [[ ! -t 0 ]]; then [[ $def == Y ]]; return; fi
    read -r -p "$1 $([[ $def == Y ]] && echo '[Y/n]' || echo '[y/N]') " reply || reply=""
    case ${reply:-$def} in [yY]*) return 0 ;; *) return 1 ;; esac
}

printf '\n=== From The Ashes: native Linux client installer ===\n\n'

# ---- find the client folder --------------------------------------------------------------------
valid() { [[ -d $1/Data && -d $1/Interface ]]; }
candidates=()
add() { local d; d=$(cd -- "$1" 2>/dev/null && pwd -P) || return 0; valid "$d" || return 0
        local c; for c in "${candidates[@]:-}"; do [[ $c == "$d" ]] && return 0; done; candidates+=("$d"); }
if [[ -n $client ]]; then
    valid "$client" || fail "$client has no Data and Interface folders (give the folder that contains Wow.exe)"
    add "$client"
else
    add "$PWD"; add "$(dirname -- "$self")"; add "$(dirname -- "$self")/.."
    # A disk search is only ever offered to someone who can see and confirm the result: never with --yes.
    if (( ${#candidates[@]} == 0 && ! assume )); then
        printf 'Looking for your From The Ashes client folder (this can take a few seconds)...\n'
        while IFS= read -r exe; do add "$(dirname -- "$exe")"; done < <(
            timeout 25 find "$HOME" /mnt /media "/run/media/${USER:-$(id -un)}" -maxdepth 6 -name Wow.exe -type f 2>/dev/null || true)
    fi
fi
if (( ${#candidates[@]} == 0 )); then
    picked=""
    if [[ ! -t 1 ]] && command -v zenity >/dev/null 2>&1; then
        picked=$(zenity --file-selection --directory --title='Select your From The Ashes client folder (with Wow.exe)' 2>/dev/null || true)
    elif [[ ! -t 1 ]] && command -v kdialog >/dev/null 2>&1; then
        picked=$(kdialog --getexistingdirectory "$HOME" --title 'Select your From The Ashes client folder' 2>/dev/null || true)
    elif (( ! assume )) && [[ -t 0 ]]; then
        read -r -e -p 'Could not find it. Type or paste the path of your client folder (the one with Wow.exe): ' picked || true
        picked=${picked/#\~/$HOME}
    fi
    [[ -n $picked ]] || fail 'no client folder chosen. Run it from inside your client folder, or run again with --client /path/to/client'
    valid "$picked" || fail "$picked has no Data and Interface folders (give the folder that contains Wow.exe)"
    add "$picked"
fi
if (( ${#candidates[@]} == 1 )); then
    client=${candidates[0]}
    printf 'Client folder: %s\n' "$client"
    ask 'Install the Linux client into it?' Y || { printf 'Nothing installed.\n'; pause_and_exit 1; }
else
    (( assume )) && fail 'several client folders found; choose one with --client'
    printf 'Found several client folders:\n'
    for i in "${!candidates[@]}"; do printf '  %d) %s\n' $((i + 1)) "${candidates[$i]}"; done
    read -r -p 'Which one? [1] ' pick || pick=1
    pick=${pick:-1}
    [[ $pick =~ ^[0-9]+$ ]] && (( pick >= 1 && pick <= ${#candidates[@]} )) || fail 'not a valid choice'
    client=${candidates[$((pick - 1))]}
fi

# ---- unpack and install -------------------------------------------------------------------------
tmp=$(mktemp -d)
trap 'rm -rf -- "$tmp"' EXIT
offset=$(LC_ALL=C awk '/^__PAYLOAD__$/ { print NR + 1; exit }' "$self")
[[ -n $offset ]] || fail 'this installer file is damaged (no payload). Download it again.'
if command -v sha256sum >/dev/null 2>&1 && [[ $PAYLOAD_SHA256 != @* ]]; then
    actual=$(tail -n +"$offset" "$self" | sha256sum | cut -d' ' -f1)
    if [[ $actual != "$PAYLOAD_SHA256" ]]; then
        size=$(stat -c %s -- "$self" 2>/dev/null || echo '?')
        fail "the installer file is damaged (checksum mismatch; file size $size bytes, expected about 30 MB). Copy or download it again, and compare its sha256sum with FTA-Linux-SHA256SUMS.txt."
    fi
fi
printf 'Unpacking...\n'
tail -n +"$offset" "$self" | tar -xz -C "$tmp"
"$tmp/install.sh" "$client" --no-deps || fail 'the installer could not copy the files (see above)'

# ---- system libraries ----------------------------------------------------------------------------
printf '\nChecking system libraries...\n'
if ! "$client/FTA-Linux/install-deps.sh"; then
    if ask 'Install the missing system libraries now (asks for your password)?' Y; then
        "$client/FTA-Linux/install-deps.sh" --install --yes || fail 'the libraries could not be installed. Run FTA-Linux/install-deps.sh to see the exact command.'
    else
        printf 'Skipped. Run FTA-Linux/install-deps.sh --install in the client folder when you are ready.\n'
    fi
fi

# ---- menu shortcut -------------------------------------------------------------------------------
if (( shortcut )); then
    apps=${XDG_DATA_HOME:-$HOME/.local/share}/applications
    mkdir -p -- "$apps"
    {
        printf '[Desktop Entry]\nType=Application\nName=From The Ashes (Linux)\n'
        printf 'Comment=Native Linux client for From The Ashes\n'
        printf 'Exec="%s/FTA-Linux/play.sh"\nPath=%s\nTerminal=false\nCategories=Game;\n' "$client" "$client"
    } >"$apps/fta-linux-client.desktop"
    printf 'Added "From The Ashes (Linux)" to your applications menu.\n'
fi

printf '\nDone. Start it any time with:  %s/FTA-Linux/play.sh\n' "$client"
if ask 'Start the game now?' N; then
    pause=0
    "$client/FTA-Linux/play.sh" || true
fi
pause_and_exit 0
exit 0
__PAYLOAD__
