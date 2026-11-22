#!/usr/bin/env python3
"""Exercise the production area replay cache with stand-in scene lists."""

import os
from pathlib import Path
import shlex
import subprocess
import tempfile

here = Path(__file__).resolve().parent
root = here.parents[1]
source = (root / "WoW/Source/WorldClient/Map.cpp").read_text()
start = source.index("namespace TerrainAreaReplay {")
end = source.index("// VERIFY: 9eee5402", start)

with tempfile.TemporaryDirectory(prefix="terrain-area-replay-") as work:
    work = Path(work)
    (work / "terrain_replay.inc").write_text(source[start:end])
    binary = work / "test"
    subprocess.run([
        *shlex.split(os.environ.get("CXX", "g++")),
        "-std=c++17", "-O2", "-UNDEBUG", "-Wall", "-Wextra", "-Werror",
        "-I", str(root), "-I", str(work), str(here / "test.cpp"), "-o", str(binary),
    ], check=True)
    for setting in ("0", "1"):
        subprocess.run([str(binary), setting], check=True,
                       env={**os.environ, "WXL_TERRAIN_AREA_REPLAY": setting})
