#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
directory="$(realpath "${1:-tools/drakeishui}")"
for test in cooldowns settings batch round2 portraits portraits_legacy interface party_events tooltip unit_bars; do
    lua5.1 - "$directory" "$test" <<'LUA'
local mock = dofile("tests/drakeishui/mock.lua")
mock.directory = arg[1]
assert(loadfile("tests/drakeishui/" .. arg[2] .. ".lua"))(mock)
LUA
done
