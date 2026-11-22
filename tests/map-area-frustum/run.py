#!/usr/bin/env python3
"""Check the production terrain-block walk with both launch settings."""

import os
from pathlib import Path
import shlex
import subprocess
import tempfile

here = Path(__file__).resolve().parent
root = here.parents[1]
source = (root / "WoW/Source/WorldClient/MapArea.cpp").read_text()
start = source.index("template <bool TestFrustumOnce>")
end = source.index("// VERIFY: 1e0fb652", start)

with tempfile.TemporaryDirectory(prefix="map-area-frustum-") as work:
    work = Path(work)
    (work / "terrain_update.inc").write_text(source[start:end])
    test = work / "test"
    subprocess.run([
        *shlex.split(os.environ.get("CXX", "g++")), "-std=c++17", "-O3", "-UNDEBUG",
        "-Wall", "-Wextra", "-Werror", "-I", str(work), str(here / "test.cpp"), "-o", str(test),
    ], check=True)
    for mode in ("0", "1"):
        subprocess.run([str(test), mode], env={**os.environ, "WXL_TERRAIN_BLOCK_FRUSTUM": mode}, check=True)
