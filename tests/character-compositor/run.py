#!/usr/bin/env python3
"""Exercise compositor paste functions on extracted character BLPs.

The manifest is a tab-separated list of section kind (base, lower, upper)
and BLP file path, relative to the manifest or absolute. No game assets
are stored in the test. The reference revision is used only at level 9.
"""
import argparse
import os
from pathlib import Path
import re
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
COMPONENT = "WoW/Common/ComponentCore/CharacterComponent.cpp"


def function(source, signature, path):
    start = source.index(signature)
    end = source.index("\n}", start) + 2
    line = source.count("\n", 0, start) + 1
    return f'#line {line} "{path}"\n{source[start:end]}\n'


def build(output, source, work):
    names = ("PasteCrappyGreen", "PasteOpaque", "PasteTransparent1Bit", "PasteTransparent4Bit",
             "PasteTransparent8Bit", "PasteOpaqueScale", "PasteTransparent1BitScale",
             "PasteTransparent4BitScale", "PasteTransparent8BitScale", "Paste", "PasteScale",
             "PasteFromSkin", "PasteToSection")
    generated = [function(source, "CompSectionInfo CCharacterComponent::s_sectionInfoRaw", COMPONENT) + ";"]
    for signature in re.findall(r"^static (?:bool|void) \w*Compositor\w*\(", source, re.M):
        generated.append(function(source, signature, COMPONENT))
    generated.extend(function(source, f"void CCharacterComponent::{name}(", COMPONENT) for name in names)
    texture = "Engine/Source/Services/Texture.cpp"
    generated.append(function((ROOT / texture).read_text(), "int32_t TextureCalcMipCount(", texture))
    code = work / "paste.inc"
    code.write_text("\n".join(generated))
    assets = ROOT / "Engine/Source/ModernAssets"
    subprocess.run([
        os.getenv("CXX", "g++"), "-std=c++20", "-O2", "-g", "-DWOWDECOMP_LINUX", "-UNDEBUG",
        "-DWOWDECOMP_ARCH_64=1", "-DWOWDECOMP_UNIX=1", "-D__stdcall=", "-D__cdecl=",
        "-D__fastcall=", "-D__int64=long long", "-D__thiscall=", "-fno-strict-aliasing",
        "-fpermissive", "-I", str(ROOT), "-I", str(work), str(HERE / "fixture.cpp"),
        str(assets / "ModernBlp.cpp"), str(assets / "WxlBlp/textures/blp/BlpTranscode.cpp"),
        str(assets / "WxlBlp/textures/dds/Dds.cpp"), "-o", str(output)
    ], cwd=ROOT, check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manifest", type=Path)
    parser.add_argument("--reference", default="b85db83")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="character-compositor-") as directory:
        work = Path(directory)
        build(work / "candidate", (ROOT / COMPONENT).read_text(), work)
        reference = subprocess.check_output(["git", "show", f"{args.reference}:{COMPONENT}"], cwd=ROOT, text=True)
        build(work / "reference", reference, work)
        manifest = str(args.manifest.resolve())
        old = subprocess.check_output([work / "reference", "9", manifest])
        new = subprocess.check_output([work / "candidate", "9", manifest])
        if old != new:
            raise SystemExit(f"Level 9 atlas output differs: {old!r} versus {new!r}")
        print(new.decode().strip() + " (pixel-identical to reference)", flush=True)
        subprocess.run([work / "candidate", "10", manifest], check=True)


if __name__ == "__main__":
    main()
