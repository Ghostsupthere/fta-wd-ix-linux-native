#!/usr/bin/env python3
"""Compare the two production sort paths and time representative offline passes."""

import os
from pathlib import Path
import shlex
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]


def section(file, begin, end):
    source = (ROOT / "Engine/Source/Model2" / file).read_text()
    start = source.index(begin)
    return source[start:source.index(end, start)]


with tempfile.TemporaryDirectory(prefix="m2-transparent-sort-") as scratch:
    scratch = Path(scratch)
    (scratch / "legacy.inc").write_text(
        section("M2Scene.cpp", "int32_t CM2Scene::SortTransparent(", "// VERIFY: 706189e1")
        + section("M2Shared.cpp", "void M2HeapSort(", "#if defined(WOWDECOMP_LINUX)")
    )
    program = scratch / "test"
    subprocess.run([
        *shlex.split(os.environ.get("CXX", "g++")), "-std=c++17", "-O3", "-flto",
        "-UNDEBUG", "-Wall", "-Wextra", "-Werror", "-I", str(ROOT), "-I", str(scratch),
        str(HERE / "test.cpp"), "-o", str(program),
    ], check=True)
    subprocess.run([str(program)], check=True)
