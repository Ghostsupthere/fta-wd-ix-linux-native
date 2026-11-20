#!/usr/bin/env python3
"""Run the isolated model-load regression fixtures with Python 3 and g++.

From the engine root: python3 tests/model-loading/run.py
"""

import os
from pathlib import Path
import shlex
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]


def extract(path, start, stop, *, include_stop=False):
    source = (ROOT / path).read_text()
    try:
        begin = source.index(start)
        end = source.index(stop, begin + len(start))
    except ValueError as error:
        raise SystemExit(f"{path}: cannot extract {start!r} through {stop!r}") from error
    if include_stop:
        end += len(stop)
    first_line = source.count("\n", 0, begin) + 1
    return f'#line {first_line} "{path}"\n{source[begin:end]}\n'


def main():
    model = "Engine/Source/Model2/M2Model.cpp"
    shared = "Engine/Source/Model2/M2Shared.cpp"
    effect = "WoW/Source/Object/ObjectClient/Effect_C.cpp"
    async_file = "Engine/Source/Services/AsyncFile.cpp"

    # Copy the production code into the test translation units. The fixtures
    # supply only stand-ins for dependencies and the code past the tested gates.
    sections = {
        "effect_callbacks": (
            extract(shared, "void CM2Shared::LoadFailedCallback(", "\n}", include_stop=True)
            + extract(shared, "void CM2Shared::SkinProfileLoadedCallback(", "\n}", include_stop=True)
        ),
        "effect_attachment": extract(
            effect, "static CGObject_C* EffectAttachmentOwner(", "    CreatureModelDataRec* modelData =",
        ),
        "effect_initialize": extract(effect, "void CEffect::PostInitialize() {", "    int32_t hasSound"),
        "async_wait": extract(async_file, "void AsyncFileReadWait(", "\n}", include_stop=True),
        "model_wait": extract(
            model, "namespace {\nclass ModelLoadReference", "void CM2Model::SetAnimating(",
        ),
        "model_has_failed": extract(
            model, "bool CM2Model::HasLoadFailed()", "\n}", include_stop=True,
        ),
        "model_is_loaded": extract(model, "uint32_t CM2Model::IsLoaded(", "\n}", include_stop=True),
        "model_initialize": extract(
            model, "int32_t CM2Model::InitializeLoaded()", "    M2Data* data = shared->m_data;",
        ),
        "model_allocations": extract(
            model,
            "    uint8_t* memory = static_cast<uint8_t*>(SMemAlloc(emitterBytes + emitterOffset,",
            "    uint8_t* cursor = memory;",
        ),
    }

    compiler = shlex.split(os.environ.get("CXX", "g++"))
    with tempfile.TemporaryDirectory(prefix="model-loading-tests-") as work:
        build = Path(work)
        for name, text in sections.items():
            (build / (name + ".inc")).write_text(text)

        for test in ("deferred_effect_test", "model_readiness_test", "model_allocation_test"):
            executable = build / test
            subprocess.run(
                [*compiler, "-std=c++20", "-UNDEBUG", "-Wall", "-Wextra", "-Werror",
                 "-Wno-unused-parameter", "-I", work, str(HERE / (test + ".cpp")),
                 "-o", str(executable)],
                check=True,
            )
            subprocess.run([str(executable)], check=True, timeout=10)
            print(f"{test} passed", flush=True)


if __name__ == "__main__":
    main()
