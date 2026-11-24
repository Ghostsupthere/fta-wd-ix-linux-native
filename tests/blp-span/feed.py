#!/usr/bin/env python3
"""Stream every .blp in the client's MPQs (newest patch wins) to stdout for scan.cpp.
usage: feed.py DATA_DIR STORMLIB_SO | scan"""
import ctypes as c, struct, sys
from pathlib import Path
data = Path(sys.argv[1]); lib = c.CDLL(sys.argv[2]); h = c.c_void_p
lib.SFileOpenArchive.argtypes = [c.c_char_p, c.c_uint, c.c_uint, c.POINTER(h)]
lib.SFileOpenFileEx.argtypes = [h, c.c_char_p, c.c_uint, c.POINTER(h)]
lib.SFileGetFileSize.argtypes = [h, c.c_void_p]; lib.SFileGetFileSize.restype = c.c_uint
lib.SFileReadFile.argtypes = [h, c.c_void_p, c.c_uint, c.POINTER(c.c_uint), c.c_void_p]
lib.SFileCloseFile.argtypes = [h]
order = {"common": 0, "common-2": 1, "expansion": 2, "lichking": 3}
arch = sorted((p for p in data.iterdir() if p.suffix.lower() == '.mpq'), key=lambda p: (order.get(p.stem.lower(), 4), p.stem.lower()))
opened = []
for p in reversed(arch):  # newest first
    a = h()
    if lib.SFileOpenArchive(bytes(p), 0, 0x100, c.byref(a)): opened.append(a)
names = set()
for a in opened:
    f = h()
    if lib.SFileOpenFileEx(a, b'(listfile)', 0, c.byref(f)):
        n = lib.SFileGetFileSize(f, None); b = c.create_string_buffer(n); r = c.c_uint()
        lib.SFileReadFile(f, b, n, c.byref(r), None); lib.SFileCloseFile(f)
        for line in b.raw.splitlines():
            s = line.decode('latin1')
            if s.lower().endswith('.blp'): names.add(s)
out = sys.stdout.buffer
for s in sorted(names):
    for a in opened:
        f = h()
        if lib.SFileOpenFileEx(a, s.encode('latin1'), 0, c.byref(f)):
            n = lib.SFileGetFileSize(f, None); b = c.create_string_buffer(n); r = c.c_uint()
            lib.SFileReadFile(f, b, n, c.byref(r), None); lib.SFileCloseFile(f)
            nm = s.encode('latin1')
            out.write(struct.pack('<I', len(nm)) + nm + struct.pack('<I', r.value) + b.raw[:r.value])
            break
out.flush()
