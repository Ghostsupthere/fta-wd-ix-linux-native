#!/usr/bin/env python3
"""Verify or regenerate the native companion's checked-in shader bytecode.

Regeneration uses an isolated offline Wine prefix and checksum-pinned compiler.
The native client never loads Wine or the compiler DLL.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parent.parent
BASE = ROOT / 'Engine/Source/ModernAssets/WxlVisuals'
MANIFEST = BASE / 'shader_manifest.json'

def digest(data):
    return hashlib.sha256(data).hexdigest()

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--verify', action='store_true', help='verify source and bytecode without Wine')
    parser.add_argument('--compiler-dll', type=Path, default=ROOT / '.deps/visual-shader-tools/d3dcompiler_47.dll')
    args = parser.parse_args()
    manifest = json.loads(MANIFEST.read_text())
    sources = {str(p.relative_to(BASE/'shaders')): digest(p.read_bytes())
               for p in sorted((BASE/'shaders').rglob('*')) if p.is_file()}
    if args.verify:
        if sources != manifest['sources']:
            raise SystemExit('Visual HLSL changed: regenerate native shaders and review the manifest.')
        for name, record in manifest['shaders'].items():
            header = (BASE/'compiled'/f'{name}.h').read_text()
            blob = bytes(int(v,16) for v in re.findall(r'0x([0-9a-f]{2})', header))
            if len(blob) != record['bytecode_bytes'] or digest(blob) != record['bytecode_sha256']:
                raise SystemExit(f'{name}: shader bytecode does not match its recorded checksum')
        print(f"Verified {len(manifest['shaders'])} native visual shaders and {len(sources)} source files")
        return
    if not args.compiler_dll.is_file() or digest(args.compiler_dll.read_bytes()) != manifest['compiler_sha256']:
        raise SystemExit('Missing or mismatched offline compiler DLL; see Engine/Source/ModernAssets/WxlVisuals/SOURCE.md')
    work = ROOT/'.deps/visual-shader-tools'
    work.mkdir(parents=True, exist_ok=True)
    compiler = work/'d3dcompiler_47.dll'
    if compiler.resolve() != args.compiler_dll.resolve():
        shutil.copy2(args.compiler_dll, compiler)
    exe = work/'compile.exe'
    subprocess.run(['x86_64-w64-mingw32-g++','-std=c++17','-static',str(ROOT/'tools/wxl_compile_shader.cpp'),'-ld3dcompiler','-o',str(exe)],check=True)
    env = dict(os.environ, WINEPREFIX=str(work/'wine'), WINEDEBUG='-all',
               WINEDLLOVERRIDES='d3dcompiler_47=n;mscoree,mshtml=')
    for name, record in sorted(manifest['shaders'].items()):
        binary = work/f'{name}.bin'
        result = subprocess.run(['wine', str(exe), 'Z:'+str(BASE/'shaders'/record['source']),
                                 record['profile'], 'Z:'+str(binary), *record['defines']],
                                env=env,cwd=BASE/'shaders',capture_output=True,text=True)
        if result.returncode:
            raise SystemExit(name+': '+result.stdout+result.stderr)
        blob = binary.read_bytes()
        values = ','.join(f'0x{byte:02x}' for byte in blob)
        header = ('// Generated from vendored HLSL; see ../SOURCE.md and shader_manifest.json.\n'
                  '#pragma once\nalignas(4) inline constexpr unsigned char g_'+name+'[] = {'+values+'};\n')
        destination = BASE/'compiled'/f'{name}.h'
        temporary = destination.with_suffix('.h.tmp')
        temporary.write_text(header)
        temporary.replace(destination)
        record['bytecode_sha256'] = digest(blob)
        record['bytecode_bytes'] = len(blob)
        print(name,len(blob),flush=True)
    manifest['sources'] = sources
    MANIFEST.write_text(json.dumps(manifest,indent=2)+'\n')

if __name__ == '__main__':
    main()
