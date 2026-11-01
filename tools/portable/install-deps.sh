#!/usr/bin/env bash
# Check, and optionally install, the system libraries the native Linux client needs.
#   FTA-Linux/install-deps.sh            report what is missing and the command that would install it
#   FTA-Linux/install-deps.sh --install  install the packages (uses sudo when not root)
#   FTA-Linux/install-deps.sh --yes      with --install: do not ask before installing
set -euo pipefail

here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
client=$(dirname -- "$here")

# Running the copy inside the unpacked archive (fta-linux-install/payload/FTA-Linux) instead of the installed one.
if [[ -f $client/../install.sh && $(basename -- "$client") == payload ]]; then
    printf 'FTA Linux: this is the unpacked installer, not the installed client.\nRun %s first (from your From The Ashes client folder), then start the game with ./FTA-Linux/play.sh from that folder.\n' "$(cd -- "$client/.." && pwd -P)/install.sh" >&2
    if command -v notify-send >/dev/null; then notify-send 'FTA Linux Client' 'This is the unpacked installer. Run install.sh from your client folder first, then use ./FTA-Linux/play.sh there.' 2>/dev/null || true; fi
    exit 1
fi
install=0 assume=0
for arg in "$@"; do
    case $arg in
        --install) install=1 ;;
        --yes|-y) assume=1 ;;
        --help|-h) sed -n '2,6p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) printf 'Unknown option: %s\n' "$arg" >&2; exit 2 ;;
    esac
done

[[ -r /etc/os-release ]] && . /etc/os-release
ids=" ${ID:-} ${ID_LIKE:-} "

# Packages per family: SDL3, libcurl, ALSA, PulseAudio client libraries, Vulkan loader and a Vulkan driver.
manager="" packages=() note=""
if [[ $ids == *' arch '* || $ids == *' cachyos '* || $ids == *' manjaro '* ]]; then
    manager=pacman
    packages=(sdl3 curl alsa-lib libpulse vulkan-icd-loader vulkan-radeon vulkan-intel)
    note='NVIDIA users also need nvidia-utils (the driver package for your card).'
elif [[ $ids == *' debian '* || $ids == *' ubuntu '* ]]; then
    manager=apt
    # The t64 names are the ones Debian 13 and Ubuntu 24.04+ use; older releases keep the plain names.
    cand() { apt-cache policy "$1" 2>/dev/null | awk '/Candidate:/ {print $2}'; }
    pick() { for name in "$@"; do c=$(cand "$name"); [[ -n $c && $c != '(none)' ]] && { echo "$name"; return; }; done; echo "$1"; }
    packages=("$(pick libcurl4t64 libcurl4)" "$(pick libasound2t64 libasound2)" libpulse0 libvulkan1 mesa-vulkan-drivers)
    note='NVIDIA users also need the proprietary driver (nvidia-driver).'
    sdl=$(cand libsdl3-0)
    if [[ -n $sdl && $sdl != '(none)' ]]; then packages=(libsdl3-0 "${packages[@]}")
    else note+=$'\nThis release has no SDL3 package (it is in Debian 13 and Ubuntu 25.04 or newer). Upgrade, or build SDL 3.2+\nfrom source into /usr/local; the steps are in README.md under "Ubuntu".'; fi
elif [[ $ids == *' fedora '* || $ids == *' rhel '* || $ids == *' centos '* ]]; then
    manager=dnf
    packages=(SDL3 libcurl alsa-lib pulseaudio-libs vulkan-loader mesa-vulkan-drivers)
    note='NVIDIA users also need the driver from RPM Fusion (akmod-nvidia). SDL3 needs Fedora 42 or newer.'
elif [[ $ids == *' suse '* || $ids == *' opensuse'* ]]; then
    manager=zypper
    packages=(libSDL3-0 libcurl4 alsa-lib libpulse0 libvulkan1 Mesa-vulkan-drivers)
    note='NVIDIA users also need the driver from NVIDIA'"'"'s repository.'
fi

# What is actually missing, as the bundled loader sees it.
libpath="$here/lib:$here/dxvk"
for dir in /usr/lib /usr/lib64 /usr/lib/x86_64-linux-gnu /lib/x86_64-linux-gnu /lib64 /lib /usr/local/lib \
           /usr/lib/pulseaudio /usr/lib/x86_64-linux-gnu/pulseaudio; do
    [[ -d $dir ]] && libpath+=":$dir"
done
missing=""
if [[ -x $client/Wow64 ]]; then
    listing=$("$here/lib/ld-linux-x86-64.so.2" --library-path "$libpath" --list "$client/Wow64" 2>&1 || true)
    missing=$(grep -oE '[A-Za-z0-9_.+-]+\.so[.0-9]*' <<<"$(grep -E 'not found|cannot open shared object' <<<"$listing")" | sort -u | grep -v '^Wow64' || true)
fi
# Libraries SDL3 and the Vulkan loader open at run time do not show up in the list above.
runtime=""
# (command substitution, not `| grep -q`: with pipefail the early exit would SIGPIPE find and report a false miss)
have_lib() { [[ -n $(find /usr/lib /usr/lib64 /lib /lib64 /usr/lib/x86_64-linux-gnu /usr/local/lib -maxdepth 1 -name "$1" 2>/dev/null | head -1) ]]; }
have_lib 'libvulkan.so.1*' || runtime+=" libvulkan.so.1"
have_lib 'libSDL3.so.0*' || runtime+=" libSDL3.so.0"
icd=$(find /usr/share/vulkan/icd.d /etc/vulkan/icd.d /usr/local/share/vulkan/icd.d -name '*.json' 2>/dev/null | head -1 || true)

if [[ -z $missing && -z $runtime && -n $icd ]]; then
    printf 'All required system libraries and a Vulkan driver are present.\n'
    exit 0
fi
[[ -n $missing$runtime ]] && printf 'Missing libraries:%s\n' "$(printf ' %s' $missing $runtime | tr ' ' '\n' | sort -u | tr '\n' ' ')"
[[ -z $icd ]] && printf 'No Vulkan driver (ICD) was found under /usr/share/vulkan/icd.d.\n'

if [[ -z $manager ]]; then
    printf 'This distribution (%s) is not recognised. Install SDL3, libcurl, ALSA, the PulseAudio client\nlibraries, the Vulkan loader and a Vulkan driver with your package manager.\n' "${PRETTY_NAME:-unknown}"
    exit 1
fi

case $manager in
    pacman) cmd=(pacman -S --needed "${packages[@]}") ;;
    apt) cmd=(apt-get install "${packages[@]}") ;;
    dnf) cmd=(dnf install "${packages[@]}") ;;
    zypper) cmd=(zypper install "${packages[@]}") ;;
esac
[[ $EUID -ne 0 ]] && cmd=(sudo "${cmd[@]}")
printf 'Packages for %s:\n  %s\n%s\n' "${PRETTY_NAME:-$ID}" "${cmd[*]}" "$note"

if (( ! install )); then
    printf 'Run again with --install to do this.\n'
    exit 1
fi
if (( ! assume )); then
    read -r -p 'Install now? [y/N] ' answer
    [[ $answer == [yY]* ]] || { printf 'Nothing installed.\n'; exit 1; }
fi
[[ $manager == apt && $EUID -eq 0 ]] && apt-get update -qq || true
[[ $manager == apt && $EUID -ne 0 ]] && sudo apt-get update -qq || true
(( assume )) && case $manager in pacman) cmd+=(--noconfirm) ;; apt) cmd+=(-y) ;; dnf|zypper) cmd+=(-y) ;; esac
"${cmd[@]}"
printf 'Done. Check with: %s/FTA-Linux/play.sh --check\n' "$client"
