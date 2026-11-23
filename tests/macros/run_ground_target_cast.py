#!/usr/bin/env python3
"""Exercise the production macro placement and early item-use dispatch with fake world state."""

import os
from pathlib import Path
import shlex
import subprocess
import tempfile

here = Path(__file__).resolve().parent
root = here.parents[1]
spell = (root / "WoW/Source/Magic/MagicClient/Spell_C.cpp").read_text()
item = (root / "WoW/Source/Object/ObjectClient/Item_C.cpp").read_text()
apply_start = spell.index("static bool ApplyMacroGroundTarget(")
apply_end = spell.index("bool Spell_C_CanPlaceMacroGroundTarget(", apply_start)
item_use = item.index("int32_t CGItem_C::UseItem(")
spell_start = item.index("    const bool cursorMacro", item_use)
spell_end = item.index("    int16_t overrideId", spell_start)
targeting_start = item.index("    if (Spell_C_IsTargeting() && Spell_C_CanTargetItems())", spell_end)
targeting_end = item.index("    if (stats->m_flags[0] & ITEM_FLAG_IS_WRAPPER)", targeting_start)

with tempfile.TemporaryDirectory(prefix="macro-ground-target-cast-") as directory:
    work = Path(directory)
    (work / "apply_target.inc").write_text(spell[apply_start:apply_end])
    (work / "item_use.inc").write_text(item[spell_start:spell_end] + item[targeting_start:targeting_end])
    binary = work / "test"
    subprocess.run([
        *shlex.split(os.environ.get("CXX", "g++")), "-std=c++20", "-UNDEBUG",
        "-Wall", "-Wextra", "-Werror", "-I", str(root), "-I", str(work),
        str(here / "ground_target_cast_test.cpp"), "-o", str(binary),
    ], check=True)
    subprocess.run([str(binary)], check=True)
