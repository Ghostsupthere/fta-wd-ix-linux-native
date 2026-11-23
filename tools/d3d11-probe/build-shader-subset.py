#!/usr/bin/env python3
"""Translate an installed billboard shader pair through the donor SM5 pipeline."""

import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
from types import SimpleNamespace


def import_script(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--donor", type=Path, required=True, help="NativeVulkan/Source")
    parser.add_argument("--data", type=Path, required=True, help="installed client Data directory")
    parser.add_argument("--stormlib", required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--glslang", default="glslangValidator")
    parser.add_argument("--spirv-cross", default="spirv-cross")
    parser.add_argument("--fxc", required=True, help="FXC-compatible offline compiler command")
    args = parser.parse_args()
    donor = import_script("translate_shaders", args.donor / "Tools/D3D11Shaders/translate_shaders.py")
    loader = import_script("wmo_pack", Path(__file__).resolve().parents[1] / "build-wmo-shader-pack.py")
    # One known stock pair from patch.MPQ. Keep exact byte hashes so it cannot
    # impersonate any patched stage or become a client-wide shader bank.
    mpq = loader.Mpq(args.stormlib, args.data, ["patch.MPQ"])
    args.out.mkdir(parents=True, exist_ok=False)
    args.reuse_from = None
    stages = []
    for kind, profile, name in (("vertex", "vs_3_0", "Color_T1"), ("pixel", "ps_3_0", "Combiners_Mod")):
        member = f"Shaders\\{kind.capitalize()}\\{profile}\\{name}.bls"
        payload = mpq.read(member)
        if not payload:
            raise SystemExit(f"patch.MPQ is missing {member}")
        _, _, _, permutations, _ = donor.archive.split(payload)
        code = permutations[0]["code"]
        record = SimpleNamespace(kind=kind, profile=profile, names=[f"{name}:0"], code=code,
                                 md5=hashlib.md5(code).hexdigest(), sh=donor.archive.ir.decode(code))
        stage = donor.compile_one(record, args)
        stage["source"] = f"patch.MPQ:{member}"
        stages.append(stage)
    (args.out / "manifest.json").write_text(json.dumps({
        "format": "Wxl.D3D11ProbeSubset", "version": 1, "shaders": stages,
    }, indent=2) + "\n", encoding="utf-8")
    for stage in stages:
        print(stage["stage"], stage["status"], stage.get("path", stage.get("reason")))
    return any(stage["status"] != "compiled" for stage in stages)


if __name__ == "__main__":
    raise SystemExit(main())
