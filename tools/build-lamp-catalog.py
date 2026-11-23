#!/usr/bin/env python3
"""Build a conservative static lamp catalog from readable legacy M2 archive entries."""
import argparse
import ctypes as c
import math
from pathlib import Path
import re
import struct

LAMP = re.compile(r"lamp|lantern|torch|brazier|campfire|candle|chandelier|streetlight|lightpost|firepit|forge|sconce")
EXCLUDED = re.compile(r"broken|unlit|extinguished|weapon|spell|projectile|burning|invisible")
ADDITIVE = {3, 4, 7}


def entry(data):
    if data[:4] != b"MD20" or len(data) < 0x130 or struct.unpack_from("<I", data, 4)[0] != 264:
        return None
    if struct.unpack_from("<I", data, 0x108)[0]:
        return None
    count, offset = struct.unpack_from("<II", data, 0x128)
    if not 0 < count <= 32 or offset < 0x130 or offset + count * 476 > len(data):
        return None
    for index in range(count):
        start = offset + index * 476
        if data[start + 0x28] not in ADDITIVE:
            continue
        x, y, z = struct.unpack_from("<3f", data, start + 8)
        keys, tint = struct.unpack_from("<II", data, start + 0x10c)
        if not 0 < keys <= 16 or tint + keys * 12 > len(data):
            continue
        for r, g, b in struct.iter_unpack("<3f", data[tint:tint + keys * 12]):
            peak = max(r, g, b)
            if all(math.isfinite(n) and abs(n) < 100 for n in (x, y, z)) and 0 < peak <= 255 and min(r, g, b) >= 0:
                return x, y, z, r / peak, g / peak, b / peak
    return None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--data", type=Path, required=True)
    parser.add_argument("--storm", required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    lib = c.CDLL(args.storm)
    handle = c.c_void_p
    lib.SFileOpenArchive.argtypes = [c.c_char_p, c.c_uint, c.c_uint, c.POINTER(handle)]
    lib.SFileOpenFileEx.argtypes = [handle, c.c_char_p, c.c_uint, c.POINTER(handle)]
    lib.SFileGetFileSize.argtypes = [handle, c.c_void_p]
    lib.SFileGetFileSize.restype = c.c_uint
    lib.SFileReadFile.argtypes = [handle, c.c_void_p, c.c_uint, c.POINTER(c.c_uint), c.c_void_p]
    lib.SFileCloseFile.argtypes = [handle]
    lib.SFileCloseArchive.argtypes = [handle]

    def read(archive, name, limit=32 << 20):
        file = handle()
        if not lib.SFileOpenFileEx(archive, name, 0, c.byref(file)):
            return None
        try:
            length = lib.SFileGetFileSize(file, None)
            if not 0 < length <= limit:
                return None
            buffer = c.create_string_buffer(length)
            result = c.c_uint()
            return buffer.raw if lib.SFileReadFile(file, buffer, length, c.byref(result), None) and result.value == length else None
        finally:
            lib.SFileCloseFile(file)

    # Patch suffixes are ordered after the original archives; later entries
    # supersede earlier ones, including versions with authored lights.
    order = {"common": 0, "common-2": 1, "expansion": 2, "lichking": 3}
    archives = sorted((p for p in args.data.iterdir() if p.suffix.lower() == ".mpq"),
                      key=lambda p: (order.get(p.stem.lower(), 4), p.stem.lower()))
    names, catalog, opened = set(), [], []
    for path in reversed(archives):
        archive = handle()
        if not lib.SFileOpenArchive(bytes(path), 0, 0x100, c.byref(archive)):
            continue
        opened.append(archive)
        files = read(archive, b"(listfile)") or b""
        for name in files.splitlines():
            name = name.lower().replace(b"/", b"\\")
            if name.endswith(b".m2") and name.startswith(b"world\\"):
                base = name.rsplit(b"\\", 1)[-1].decode("ascii", errors="ignore")
                if LAMP.search(base) and not EXCLUDED.search(base):
                    names.add(name)
    try:
        for name in sorted(names):
            if not name.isascii() or b"," in name:
                continue
            data = next((data for archive in opened if (data := read(archive, name, 4 << 20))), None)
            light = entry(data) if data else None
            if light is None:
                continue
            base = name.rsplit(b"\\", 1)[-1]
            radius, strength = (5, 0.5) if b"candle" in base else (13, 0.85)
            catalog.append(name.decode("ascii") + "," + ",".join(f"{v:.5g}" for v in (*light, radius, strength)))
    finally:
        for archive in opened:
            lib.SFileCloseArchive(archive)
    if not catalog:
        raise SystemExit("no supported lamps found")
    args.out.write_text("# model,x,y,z,red,green,blue,radius,intensity\n" + "\n".join(catalog) + "\n")
    print(f"{len(opened)} archives, {len(names)} candidate names, {len(catalog)} catalog entries")


if __name__ == "__main__":
    main()
