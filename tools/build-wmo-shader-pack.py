#!/usr/bin/env python3
"""Generate the native client's patched shader packs for this install: the WMO layered-material
pack (wmo_shaders.pack) and the grass-motion pack (grass_shaders.pack).

Reads the MapObj shader permutations from the client's own MPQs (StormLib via
ctypes), patches them with the supplied WarcraftXL shader-text transforms through
the checksum-pinned Microsoft d3dcompiler under an isolated offline Wine prefix, and
writes Utils/WarcraftXL/visuals/wmo_shaders.pack next to the native executable. The
pack is derived from game assets, so it is generated locally and never committed.
The native client never loads Wine or the compiler; without the pack it draws stock.
"""
import argparse, ctypes, hashlib, json, os, shutil, struct, subprocess, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MANIFEST = ROOT / 'Engine/Source/ModernAssets/WxlVisuals/shader_manifest.json'
VERTEX = ['MapObjDiffuse_T1', 'MapObjSpecular_T1', 'MapObjDiffuse_T1_Refl',
          'MapObjUDiffuse_T1', 'MapObjUSpecular_T1', 'MapObjUDiffuse_T1_Refl']
GRASS = 'DetailDoodad'
GRASS_LIVE_PERMUTATIONS = 3   # group 0 (live path); group 1 is the dormant point-light path
PIXEL = ['MapObjDiffuse', 'MapObjOpaque', 'MapObjSpecular', 'MapObjMetal', 'MapObjEnv', 'MapObjEnvMetal']
ARCHIVES = ['patch-ZC.mpq', 'patch-ZB.mpq', 'Patch-ZA.mpq', 'patch-Z.mpq', 'Patch-YE.mpq', 'Patch-YD.mpq',
            'Patch-YB.mpq', 'Patch-S-002.mpq', 'patch-X.MPQ', 'Patch-R.mpq', 'Patch-M.mpq', 'Patch-I.mpq',
            'Patch-5.mpq', 'patch-3.MPQ', 'patch-2.MPQ', 'patch.MPQ', 'lichking.MPQ', 'expansion.MPQ',
            'common-2.MPQ', 'common.MPQ']

class Mpq:
    def __init__(self, storm, data_dir):
        self.lib = ctypes.CDLL(storm)
        lib = self.lib
        lib.SFileOpenArchive.argtypes = [ctypes.c_char_p, ctypes.c_uint, ctypes.c_uint, ctypes.POINTER(ctypes.c_void_p)]
        lib.SFileOpenFileEx.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_uint, ctypes.POINTER(ctypes.c_void_p)]
        lib.SFileGetFileSize.argtypes = [ctypes.c_void_p, ctypes.c_void_p]; lib.SFileGetFileSize.restype = ctypes.c_uint
        lib.SFileReadFile.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_uint, ctypes.POINTER(ctypes.c_uint), ctypes.c_void_p]
        self.archives = []
        for name in ARCHIVES:
            path = Path(data_dir) / name
            handle = ctypes.c_void_p()
            if path.exists() and lib.SFileOpenArchive(str(path).encode(), 0, 0x100, ctypes.byref(handle)):
                self.archives.append(handle)
    def read(self, name):
        for handle in self.archives:        # highest-priority archive first
            file = ctypes.c_void_p()
            if self.lib.SFileOpenFileEx(handle, name.encode(), 0, ctypes.byref(file)):
                size = self.lib.SFileGetFileSize(file, None)
                buffer = ctypes.create_string_buffer(size); read = ctypes.c_uint()
                self.lib.SFileReadFile(file, buffer, size, ctypes.byref(read), None)
                return buffer.raw[:read.value]
        return None

def permutations(blob):
    magic, version, _ = struct.unpack('<III', blob[:12])
    if magic != 0x47585348 or version != 0x10003:
        raise SystemExit('unexpected BLS header')
    position, out = 12, []
    while position + 16 <= len(blob):
        _, _, _, _, size = struct.unpack('<IIHHI', blob[position:position + 16]); position += 16
        out.append(blob[position:position + size]); position += (size + 3) & ~3
    return out

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--client', type=Path, default=ROOT.parent.parent / 'From_The_Ashes_Client', help='client directory with Data/*.mpq')
    parser.add_argument('--storm', default=os.environ.get('STORMLIB', '/home/shawn/Documents/AzerothCore/Build/tools/stormlib/libstorm.so'))
    parser.add_argument('--out', type=Path, default=ROOT / 'bin/Linux/Utils/WarcraftXL/visuals/wmo_shaders.pack')
    parser.add_argument('--compiler-dll', type=Path, default=ROOT / '.deps/visual-shader-tools/d3dcompiler_47.dll')
    args = parser.parse_args()
    expected = json.loads(MANIFEST.read_text())['compiler_sha256']
    if not args.compiler_dll.is_file() or hashlib.sha256(args.compiler_dll.read_bytes()).hexdigest() != expected:
        raise SystemExit('Missing or mismatched offline compiler DLL (see Engine/Source/ModernAssets/WxlVisuals/SOURCE.md)')
    mpq = Mpq(args.storm, args.client / 'Data')
    if not mpq.archives: raise SystemExit('no client archives opened')
    work = ROOT / '.deps/wmo-shader-pack'
    shutil.rmtree(work, ignore_errors=True); (work / 'in').mkdir(parents=True)
    count = {'vs': 0, 'ps': 0}
    for kind, names, profile, ext in (('vs', VERTEX, 'Vertex\\vs_3_0', 'vs'), ('ps', PIXEL, 'Pixel\\ps_3_0', 'ps')):
        for name in names:
            blob = mpq.read(f'Shaders\\{profile}\\{name}.bls')
            if blob is None: raise SystemExit(f'missing {name}.bls')
            for index, code in enumerate(permutations(blob)):
                (work / 'in' / f'{name}_{index:03d}.{ext}').write_bytes(code); count[kind] += 1
    print(f"extracted {count['vs']} vertex and {count['ps']} pixel permutations")
    grass_blob = mpq.read(f'Shaders\\Vertex\\vs_3_0\\{GRASS}.bls')
    if grass_blob is None: raise SystemExit(f'missing {GRASS}.bls')
    (work / 'grass').mkdir()
    for index, code in enumerate(permutations(grass_blob)[:GRASS_LIVE_PERMUTATIONS]):
        (work / 'grass' / f'{GRASS}_{index}.vs').write_bytes(code)
    compiler = work / 'd3dcompiler_47.dll'; shutil.copy2(args.compiler_dll, compiler)
    exe = work / 'wmo_pack.exe'
    subprocess.run(['x86_64-w64-mingw32-g++', '-std=c++20', '-O2', '-static', '-I', str(ROOT),
                    str(ROOT / 'tools/wxl_wmo_shader_pack.cpp'), str(ROOT / 'Engine/Source/ModernAssets/WxlWmo/WmoShaderPack.cpp'),
                    '-o', str(exe)], check=True)
    env = dict(os.environ, WINEPREFIX=str(work / 'wine'), WINEDEBUG='-all', WINEDLLOVERRIDES='d3dcompiler_47=n;mscoree,mshtml=')
    args.out.parent.mkdir(parents=True, exist_ok=True)
    temporary = args.out.with_suffix('.pack.tmp')
    subprocess.run(['wine', str(exe), 'Z:' + str(work / 'in'), 'Z:' + str(temporary)], env=env, cwd=work, check=True)
    temporary.replace(args.out)
    print('wrote', args.out)
    grass_out = args.out.parent / 'grass_shaders.pack'
    grass_temporary = grass_out.with_suffix('.pack.tmp')
    subprocess.run(['wine', str(exe), '--grass', 'Z:' + str(work / 'grass'), 'Z:' + str(grass_temporary)], env=env, cwd=work, check=True)
    grass_temporary.replace(grass_out)
    print('wrote', grass_out)

if __name__ == '__main__':
    main()
