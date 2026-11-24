#!/usr/bin/env python3
"""Compare candidate masks and relink hits against the stock ray/face code."""
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile

here = Path(__file__).resolve().parent
root = here.parents[1]

def function(path, marker, end):
    code = (root / path).read_text()
    start = code.index(marker)
    return code[start:code.index(end, start)]

intersect = function("Engine/Source/Tempest/NTempest.cpp",
                     "bool NTempest::Intersect(const C3Ray& ray, const C3Vector* verts, const uint16_t* indices",
                     "// VERIFY: 94d97d2a")
visit = function("WoW/Source/WorldClient/BspQuery_SegmentLink.cpp",
                 "void BspQuery_SegmentLink::operator()(uint16_t face)",
                 "bool BspQuery_SegmentLink::VisitFilteredLeaf")
with tempfile.TemporaryDirectory(prefix="wmo-relink-candidates-") as folder:
    work = Path(folder)
    (work / "query.inc").write_text(intersect + visit)
    executable = work / "wmo-relink-candidates"
    subprocess.run([*shlex.split(os.environ.get("CXX", "g++")), "-std=c++17", "-O3", "-UNDEBUG",
                    "-Wall", "-Wextra", "-Werror", "-I", str(root), "-I", str(work), str(here / "test.cpp"),
                    str(root / "WoW/Source/WorldClient/RelinkCandidates.cpp"), "-o", str(executable)], check=True)
    subprocess.run([str(executable), *sys.argv[1:]], check=True)
