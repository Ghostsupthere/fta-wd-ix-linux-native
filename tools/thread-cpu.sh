#!/usr/bin/env bash
# Per-thread CPU use of the running native client: tells whether the main thread, the model workers or DXVK's
# command-submission thread is the busy one. Usage: tools/thread-cpu.sh [seconds=10] [pid]
# Percentages are of one core (100% = one thread fully busy). Run it while standing in a busy scene.
set -u
secs=${1:-10}
pid=${2:-$(pgrep -n -x Wow64 || true)}
[[ -n $pid && -d /proc/$pid ]] || { echo "No running Wow64 (pass a pid)." >&2; exit 1; }
tick=$(getconf CLK_TCK)
snap() {
    for t in /proc/$pid/task/*; do
        [[ -r $t/stat ]] || continue
        read -r line < "$t/stat" || continue
        rest=${line##*) }          # after the "(comm)" field
        set -- $rest
        printf '%s %s %s\n' "${t##*/}" "$(( ${12} + ${13} ))" "$(tr -d '\n' < "$t/comm")"   # utime + stime
    done
}
a=$(snap); sleep "$secs"; b=$(snap)
join -j1 <(sort <<<"$a") <(sort <<<"$b") 2>/dev/null | awk -v secs="$secs" -v tick="$tick" '
    { d = $4 - $2; name = $3; if (name == "") name = $5; printf "%6.1f%%  tid %-8s %s\n", 100.0 * d / tick / secs, $1, name }' | sort -rn | head -14
