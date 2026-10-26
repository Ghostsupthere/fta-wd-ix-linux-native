#!/usr/bin/env bash
# One-off: download the MSVC CRT + Windows SDK (x86_64) into ./.xwin
#
# This project targets x64, so the CRT and SDK import libraries must be the
# 64-bit ones; an x86 splat cannot be reused.
#
# Runs on the host rather than inside a container: fetching from Microsoft's
# CDN inside one hits truncated responses that exhaust xwin's retries.
#
# --accept-license accepts the Microsoft Visual Studio Build Tools licence on
# your behalf. That is the only mode xwin operates in.
set -euo pipefail
cd "$(dirname "$0")"

if [ -d .xwin/crt/include ]; then
    echo ".xwin already populated -- delete it to re-fetch."
    exit 0
fi

# xwin uses native-tls, which finds no CA bundle under a bare nix-shell.
export SSL_CERT_FILE="${SSL_CERT_FILE:-/etc/ssl/certs/ca-certificates.crt}"

run_xwin() {
    # --include-atl: the original build pulls in sphelper.h (Speech API) via
    # VoiceChat.cpp, which needs ATL headers.
    xwin --accept-license \
         --arch x86_64 \
         --include-atl \
         --http-retry 10 \
         --timeout 120s \
         --cache-dir .xwin-cache \
         splat --output .xwin
}

if command -v xwin >/dev/null 2>&1; then
    run_xwin
else
    echo "xwin not on PATH; using nix-shell."
    nix-shell -p xwin cacert --run "$(declare -f run_xwin); run_xwin"
fi
