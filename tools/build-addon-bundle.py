#!/usr/bin/env python3
"""Build Data/addon.MPQ, the client-owned addon bundle the engine mounts above every patch archive.

  build-addon-bundle.py --addons DIR --out addon.mpq [--only NAME ...] [--exclude NAME ...] [--stormlib libstorm.so]

DIR holds one folder per addon (the Interface/AddOns layout, for example the merged tree). Every addon folder
that has <name>/<name>.toc is added as Interface\\AddOns\\<name>\\..., and Interface\\AddOns\\NativeAddons.manifest lists the
names (the engine finds bundled TOCs through it; the bundle needs no listfile). Addons whose TOC says
`## X-WarcraftXL-Required: 1` are always enabled and hidden from the addon lists. Refuses an existing output file.
"""
import argparse, ctypes, os, sys

MPQ_FILE_COMPRESS = 0x00000200
MPQ_FILE_REPLACEEXISTING = 0x80000000
MPQ_COMPRESSION_ZLIB = 0x02
MPQ_CREATE_LISTFILE = 0x00100000
MPQ_CREATE_ATTRIBUTES = 0x00200000
MPQ_CREATE_ARCHIVE_V2 = 0x01000000


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--addons", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--only", nargs="*")
    ap.add_argument("--exclude", nargs="*", default=[])
    ap.add_argument("--stormlib", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "..", "Tools", "stormlib", "libstorm.so"))
    args = ap.parse_args()
    if os.path.exists(args.out):
        sys.exit(f"{args.out} exists; remove it first")

    names = []
    for entry in sorted(os.listdir(args.addons), key=str.lower):
        folder = os.path.join(args.addons, entry)
        if not os.path.isfile(os.path.join(folder, entry + ".toc")):
            continue
        if args.only and entry not in args.only:
            continue
        if entry in args.exclude:
            continue
        names.append(entry)
    if not names:
        sys.exit("no addons selected")
    files = []
    for name in names:
        base = os.path.join(args.addons, name)
        for root, _, items in os.walk(base):
            for item in sorted(items):
                src = os.path.join(root, item)
                rel = os.path.relpath(src, args.addons).replace("/", "\\")
                files.append((src, "Interface\\AddOns\\" + rel))
    manifest = ("# Addons of the client-owned bundle, one folder name per line.\n" + "\n".join(names) + "\n").encode()
    total = len(files) + 3
    size = 1
    while size < total * 2:
        size *= 2
    size = max(size, 4096)

    lib = ctypes.CDLL(os.path.abspath(args.stormlib))
    lib.SFileCreateArchive.argtypes = [ctypes.c_char_p, ctypes.c_uint32, ctypes.c_uint32, ctypes.POINTER(ctypes.c_void_p)]
    lib.SFileAddFileEx.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_uint32, ctypes.c_uint32, ctypes.c_uint32]
    lib.SFileCloseArchive.argtypes = [ctypes.c_void_p]
    handle = ctypes.c_void_p()
    flags = MPQ_CREATE_LISTFILE | MPQ_CREATE_ATTRIBUTES | MPQ_CREATE_ARCHIVE_V2
    if not lib.SFileCreateArchive(args.out.encode(), flags, size, ctypes.byref(handle)):
        sys.exit("cannot create the archive")
    failed = 0
    tmp = args.out + ".manifest.tmp"
    open(tmp, "wb").write(manifest)
    try:
        items = files + [(tmp, "Interface\\AddOns\\NativeAddons.manifest")]
        for src, dst in items:
            if os.path.getsize(src) == 0:
                continue  # StormLib cannot store empty files; nothing in an addon needs one
            ok = lib.SFileAddFileEx(handle, src.encode(), dst.encode(), MPQ_FILE_COMPRESS | MPQ_FILE_REPLACEEXISTING, MPQ_COMPRESSION_ZLIB, MPQ_COMPRESSION_ZLIB)
            if not ok:
                failed += 1
                print("failed:", dst, file=sys.stderr)
    finally:
        lib.SFileCloseArchive(handle)
        os.remove(tmp)
    if failed:
        os.remove(args.out)
        sys.exit(f"{failed} files could not be added")
    print(f"{args.out}: {len(names)} addons, {len(files)} files, {os.path.getsize(args.out)} bytes")


main()
