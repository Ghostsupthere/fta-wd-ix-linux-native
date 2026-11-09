#!/usr/bin/env bash
# Builds and runs tests/wxl_particle_jobs_test.cpp: particle emitter updates queued and run on several threads must
# leave every emitter bit-identical to the original serial Update() calls.
#
#   tools/run-particle-jobs-test.sh            # plain build with assertions
#   tools/run-particle-jobs-test.sh tsan       # ThreadSanitizer: also reports data races in the emitter code
#   tools/run-particle-jobs-test.sh asan       # AddressSanitizer
#
# Needs a configured build tree (build/linux) for libTempest.a and libStorm.a; it links no renderer (see
# tests/wxl_particle_jobs_stubs.cpp). Output goes to a temporary directory that is removed afterwards.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)
mode=${1:-plain}
lib="$root/build/linux/lib"
[[ -f $lib/libTempest.a && -f $lib/libStorm.a ]] || { echo "Missing $lib/libTempest.a or libStorm.a: build the client first." >&2; exit 1; }

case $mode in
    plain) san=() ;;
    tsan) san=(-fsanitize=thread) ;;
    asan) san=(-fsanitize=address -fno-sanitize-recover=all) ;;
    *) echo "usage: $0 [plain|tsan|asan]" >&2; exit 2 ;;
esac

work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT

defs=(-DDISABLE_SIGNATURE_CHECKS=1 -DWOWDECOMP_ARCH_64=1 -DWOWDECOMP_LINUX=1 -DWOWDECOMP_UNIX=1 -D__cdecl= -D__fastcall= -D__stdcall= -D__thiscall=
      "-D__int64=long long" -D_alloca=__builtin_alloca)
flags=("-I$root" "-I$root/build/linux/generated" "-I$root/ThirdParty/freetype/include" -O1 -g -std=gnu++17 -fpermissive -w -fno-strict-aliasing)

pids=()
for source in Engine/Source/Services/ParticleSystem2.cpp Engine/Source/Model2/M2ParticleJobs.cpp Engine/Source/Model2/M2ParticleBuild.cpp tests/wxl_particle_jobs_test.cpp tests/wxl_particle_jobs_stubs.cpp; do
    g++ "${defs[@]}" "${flags[@]}" "${san[@]}" -c "$root/$source" -o "$work/$(basename "$source").o" &
    pids+=($!)
done
for pid in "${pids[@]}"; do wait "$pid"; done
g++ "${san[@]}" "$work"/*.o -Wl,--start-group "$lib/libTempest.a" "$lib/libStorm.a" -Wl,--end-group -lpthread -lbz2 -lz -o "$work/test"
export TSAN_OPTIONS="halt_on_error=1 ${TSAN_OPTIONS:-}"
# The test replaces Storm's allocator (malloc-backed) next to operator new/delete; ASan would flag that pairing.
export ASAN_OPTIONS="alloc_dealloc_mismatch=0 detect_leaks=1 ${ASAN_OPTIONS:-}"
"$work/test" "${@:2}"
