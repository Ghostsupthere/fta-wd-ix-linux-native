#!/usr/bin/env python3
"""Generate the surface-light shader pack for this install (Utils/WarcraftXL/visuals/light_shaders.pack).

Reads the client's own terrain, M2 and WMO lit vertex/pixel shader permutations from its MPQs,
applies the supplied wxl-living-azeroth text patches (ModelLightShaderText.hpp,
TerrainLightShaderText.hpp) and assembles the result through the checksum-pinned Microsoft
d3dcompiler under an isolated offline Wine prefix. The pack is derived from game assets, so it is
generated locally and never committed. The native client never loads Wine or the compiler; without
the pack, or with WXL_SURFACE_LIGHTS unset, nothing about its shaders changes.

    tools/build-light-shader-pack.py [--client DIR] [--out FILE]
"""
import argparse, hashlib, importlib.util, json, os, shutil, subprocess, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location('wmopack', ROOT / 'tools/build-wmo-shader-pack.py')
wmopack = importlib.util.module_from_spec(spec); spec.loader.exec_module(wmopack)

# Terrain (Map.cpp CMap::Initialize / InitializePCFShaders) and the M2 vertex families (M2Shared.cpp).
TERRAIN_VERTEX = ['Terrain']
TERRAIN_PIXEL = ['Terrain0', 'Terrain0_env', 'Terrain1', 'Terrain1w', 'Terrain2', 'Terrain2_pcf', 'Terrain3', 'Terrain3_pcf']
M2_VERTEX = ['Diffuse_T1', 'Diffuse_T2', 'Diffuse_Env', 'Diffuse_T1_Env', 'Diffuse_T1_T2', 'Diffuse_Env_Env', 'Diffuse_Env_T2']
WMO_VERTEX = wmopack.VERTEX

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--client', type=Path, default=ROOT.parent.parent / 'From_The_Ashes_Client')
    parser.add_argument('--storm', default=os.environ.get('STORMLIB'), help='path to the StormLib shared library (or set STORMLIB)')
    parser.add_argument('--out', type=Path, default=ROOT / 'bin/Linux/Utils/WarcraftXL/visuals/light_shaders.pack')
    parser.add_argument('--compiler-dll', type=Path, default=ROOT / '.deps/visual-shader-tools/d3dcompiler_47.dll')
    args = parser.parse_args()
    if not args.storm: raise SystemExit('Give --storm /path/to/libstorm.so or set STORMLIB (the StormLib shared library)')
    expected = json.loads(wmopack.MANIFEST.read_text())['compiler_sha256']
    if not args.compiler_dll.is_file() or hashlib.sha256(args.compiler_dll.read_bytes()).hexdigest() != expected:
        raise SystemExit('Missing or mismatched offline compiler DLL (see Engine/Source/ModernAssets/WxlVisuals/SOURCE.md)')
    mpq = wmopack.Mpq(args.storm, args.client / 'Data')
    if not mpq.archives: raise SystemExit('no client archives opened')
    work = ROOT / '.deps/light-shader-pack'
    shutil.rmtree(work / 'in', ignore_errors=True); (work / 'in').mkdir(parents=True)
    counts = {}
    jobs = [('tv', 'Vertex\\vs_3_0', 'vs', TERRAIN_VERTEX), ('tp', 'Pixel\\ps_3_0', 'ps', TERRAIN_PIXEL),
            ('m2', 'Vertex\\vs_3_0', 'vs', M2_VERTEX), ('wmo', 'Vertex\\vs_3_0', 'vs', WMO_VERTEX)]
    for group, profile, ext, names in jobs:
        for name in names:
            blob = mpq.read(f'Shaders\\{profile}\\{name}.bls')
            if blob is None:
                print(f'note: {name}.bls not in the client archives'); continue
            for index, code in enumerate(wmopack.permutations(blob)):
                (work / 'in' / f'{group}__{name}__{index:03d}.{ext}').write_bytes(code)
                counts[group] = counts.get(group, 0) + 1
    print('extracted', counts)
    compiler = work / 'd3dcompiler_47.dll'; shutil.copy2(args.compiler_dll, compiler)
    exe = work / 'light_pack.exe'
    subprocess.run(['x86_64-w64-mingw32-g++', '-std=c++20', '-O2', '-static', '-I', str(ROOT),
                    str(ROOT / 'tools/wxl_wmo_shader_pack.cpp'), str(ROOT / 'Engine/Source/ModernAssets/WxlWmo/WmoShaderPack.cpp'),
                    '-o', str(exe)], check=True)
    env = dict(os.environ, WINEPREFIX=str(work / 'wine'), WINEDEBUG='-all', WINEDLLOVERRIDES='d3dcompiler_47=n;mscoree,mshtml=')
    args.out.parent.mkdir(parents=True, exist_ok=True)
    temporary = args.out.with_suffix('.pack.tmp')
    subprocess.run(['wine', str(exe), '--lights', 'Z:' + str(work / 'in'), 'Z:' + str(temporary)], env=env, cwd=work, check=True)
    temporary.replace(args.out)
    print('wrote', args.out)

if __name__ == '__main__':
    main()
