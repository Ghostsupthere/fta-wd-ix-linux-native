#!/usr/bin/env python3
"""Reject a missing XML texture before package staging and verify the pinned fallback copies."""
import contextlib
import io
import json
from pathlib import Path
import runpy
import sys
import tempfile
from unittest.mock import patch

root = Path(__file__).resolve().parents[2]
module = runpy.run_path(str(root / "tools/package-drakeishui.py"))

with tempfile.TemporaryDirectory(prefix="package-media-") as tmp:
    work = Path(tmp)
    windows, upstream = work / "windows", work / "upstream"
    windows.mkdir()
    upstream.mkdir()
    (windows / "DrakeishUI.toc").write_text("## Interface: 30300\nDrakeishUI.xml\n")
    (windows / "DrakeishUI.xml").write_text(
        r'<Ui xmlns="http://www.blizzard.com/wow/ui/"><Texture file="Interface\AddOns\DrakeishUI\Extra\Test"/></Ui>'
    )

    def package(out):
        args = ["package-drakeishui.py", "--windows", str(windows), "--upstream", str(upstream), "--out", str(out)]
        with patch.object(sys, "argv", args), patch("subprocess.check_output", return_value=module["UPSTREAM"]):
            with contextlib.redirect_stdout(io.StringIO()):
                module["main"]()

    failed = work / "failed"
    try:
        package(failed)
    except SystemExit as error:
        assert "missing XML texture" in str(error)
        assert not failed.exists()
    else:
        raise AssertionError("missing XML media was accepted")
    (upstream / "Extra").mkdir()
    (upstream / "Extra/Test.blp").write_bytes(b"XML media")
    for family in ("actionbars", "bags", "characterpanel", "classicons", "collections", "end_caps",
                   "raidframe", "social", "spellbook", "talents", "ui", "worldmap"):
        path = upstream / "assets" / family / "fixture.blp"
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(b"atlas family")
    for original in module["MEDIA_ALIASES"].values():
        path = upstream / original
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(original.encode())
    out = work / "DrakeishUI"
    package(out)
    assert (out / "Extra/Test.blp").read_bytes() == b"XML media"
    manifest = json.loads((out / "SelectivePackageManifest.json").read_text())
    assert not manifest["missing_media"]
    for alias, original in module["MEDIA_ALIASES"].items():
        assert (out / alias).read_bytes() == (upstream / original).read_bytes()
print("XML asset closure, missing-reference failure and all six package aliases passed")
