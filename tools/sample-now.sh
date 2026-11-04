#!/usr/bin/env bash
# Asks the running native client (started with WXL_SAMPLE_PROFILE=<hz>) to write Logs/samples.txt, then prints
# the per-function report. Optional argument: a profiler zone name to keep, for example "UI render".
set -u
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)
logs="$root/bin/Linux/Logs"
log=$(ls -t "$logs"/native-launch-*.log | head -1)
pid=$(grep -o 'kill -USR1 [0-9]*' "$log" | awk '{print $3}' | tail -1)
[[ -n $pid ]] && kill -0 "$pid" 2>/dev/null || { echo "No sampling client running (start it with WXL_SAMPLE_PROFILE=500)." >&2; exit 1; }
rm -f "$logs/samples.txt"
kill -USR1 "$pid"
for _ in $(seq 1 50); do [[ -s $logs/samples.txt ]] && break; sleep 0.2; done
sleep 1
python3 -I "$root/tools/symbolize-samples.py" "$logs/samples.txt" ${1:+--zone "$1"} --top 25
