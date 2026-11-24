#!/usr/bin/env python3
"""Extract the four local inn groups used by the relink fixture."""
import argparse
from pathlib import Path
import ctypes as c
import runpy
import struct
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--data", type=Path, required=True)
parser.add_argument("--storm", type=Path, required=True)
parser.add_argument("--out", type=Path, required=True)
args = parser.parse_args()
args.out.mkdir(parents=True, exist_ok=True)
script = Path(__file__).resolve().parents[2] / "tools/build-lamp-catalog.py"
chunks=runpy.run_path(str(script))['chunks']
lib=c.CDLL(str(args.storm.resolve()))
handle=c.c_void_p
lib.SFileOpenArchive.argtypes=[c.c_char_p,c.c_uint,c.c_uint,c.POINTER(handle)]
lib.SFileOpenFileEx.argtypes=[handle,c.c_char_p,c.c_uint,c.POINTER(handle)]
lib.SFileGetFileSize.argtypes=[handle,c.c_void_p]
lib.SFileGetFileSize.restype=c.c_uint
lib.SFileReadFile.argtypes=[handle,c.c_void_p,c.c_uint,c.POINTER(c.c_uint),c.c_void_p]
lib.SFileCloseFile.argtypes=[handle]
lib.SFileCloseArchive.argtypes=[handle]
d=args.data
order={'common':0,'common-2':1,'expansion':2,'lichking':3}
archives=sorted((p for p in d.iterdir() if p.suffix.lower()=='.mpq'),key=lambda p:(order.get(p.stem.lower(),4),p.stem.lower()),reverse=True)
targets={rf'world\wmo\azeroth\buildings\goldshireinn\goldshireinn_{i:03}.wmo'.encode(): i for i in (0,2,3,4)}
for path in archives:
    if not targets: break
    archive=handle()
    if not lib.SFileOpenArchive(bytes(path),0,0x100,c.byref(archive)): continue
    for asset,i in list(targets.items()):
        f=handle()
        if not lib.SFileOpenFileEx(archive,asset,0,c.byref(f)): continue
        length=lib.SFileGetFileSize(f,None)
        buf=c.create_string_buffer(length)
        n=c.c_uint()
        assert lib.SFileReadFile(f,buf,length,c.byref(n),None) and n.value==length
        lib.SFileCloseFile(f)
        inner=chunks(chunks(buf.raw)[b'MOGP'][68:])
        keys=(b'MOVT',b'MOVI',b'MOPY',b'MOBN',b'MOBR')
        data=[inner[k] for k in keys]
        node_counts=[v[3] for v in struct.iter_unpack('<4HIf',data[3]) if v[0]&4]
        print(path.name,i,[len(x) for x in data],f'largest leaves {sorted(node_counts,reverse=True)[:5]}')
        (args.out/f'{i}.bin').write_bytes(struct.pack('<5I',*(len(x) for x in data))+b''.join(data))
        targets.pop(asset)
    lib.SFileCloseArchive(archive)
assert not targets,targets
