#!/usr/bin/env python3
"""Compare legacy and filtered lamp/model scans with the real scan bodies."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

here = Path(__file__).resolve().parent
root = here.parents[1]
path = "WoW/Source/WorldClient/Linux/WxlSurfaceLights.cpp"
legacy = subprocess.check_output(["git", "show", f"7372abe:{path}"], cwd=root, text=True)
current = (root / path).read_text()

def extract(source, begin, end):
    start = source.index(begin)
    return source[start:source.index(end, start)]

with tempfile.TemporaryDirectory(prefix="surface-light-scan-") as work:
    work = Path(work)
    results = []
    table_fixture = (root / "tests/wxl_surface_lights_test.cpp").read_text()
    builder = extract(table_fixture, "struct Builder {", "static Candidate Point")
    for kind, source, marker in (("legacy", legacy, "struct SceneModel {"),
                                 ("filtered", current, "enum ModelLightPath")):
        code = (extract(source, "constexpr unsigned kLampDebugLimit", "sl::GridBuilder gridBuilder")
                + extract(source, "struct Stats {", "// ---- engine data")
                + extract(source, marker, "// ---- indoor and ray tests"))
        (work / "scan.inc").write_text(builder + code)
        executable = work / kind
        subprocess.run([*shlex.split(os.environ.get("CXX", "g++")), "-std=c++20", "-O2",
                        "-UNDEBUG", "-Wall", "-Wextra", "-Werror", "-I", str(root), "-I", str(work),
                        str(here / "fixture.cpp"), "-o", str(executable)], check=True)
        with (work / f"{kind}.log").open("wb") as output:
            subprocess.run([str(executable)], stdout=output, check=True)
        results.append((work / f"{kind}.log").read_bytes())
    assert results[0] == results[1], "selected source sequence, data or lifetime behavior changed"
    print("Legacy and filtered light sources, placement order and destruction checks match")
