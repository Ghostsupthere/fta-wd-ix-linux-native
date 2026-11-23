#!/usr/bin/env python3
"""Build a conservative static lamp catalog from qualified M2 archive entries."""
import argparse
import ctypes as c
import math
from pathlib import Path
import re
import struct

LAMP = re.compile(r"lamp|lantern|torch|brazier|campfire|bonfire|hearth|fireplace|candle|chandelier|streetlight|lightpost|firepit|forge|sconce")
EXCLUDED = re.compile(r"broken|unlit|extinguished|weapon|spell|projectile|burning|invisible")
ADDITIVE = {3, 4, 7}
TORCH = re.compile(rb"torch|sconce")
FIRE = re.compile(rb"brazier|campfire|bonfire|hearth|fireplace|forge|firepit")
# This fixture illuminates its glass with a mesh material and has no emitter.
# The offset is at the centre of the lantern head in its legacy model geometry.
MESH_LAMPS = {
    b"world\\generic\\human\\passive doodads\\lamps\\stormwindstreetlamp01.m2":
        (0, 0, 4.25, 1, 0.73, 0.38, 15, 1.0),
}


# The Razor Hill replacement has the converter's extended 0x1ec emitter
# records. Other wrapped models still need asset-specific qualification.
MODERN_LAMPS = {
    b"world\\generic\\orc\\passive doodads\\lampposts\\orcbrazier_lightpostbarrens.m2",
}


def rest_key(data, track, fmt, embedded):
    _, loop, _, _, count, offset = struct.unpack_from("<HH4I", data, track)
    if not count or offset < 0x130 or offset + 8 * count > len(data):
        return None
    keys, values = struct.unpack_from("<II", data, offset)
    if not keys:
        return None
    if (loop == 0xFFFF and not embedded) or values < 0x130 or values + struct.calcsize(fmt) > len(data):
        raise ValueError("external or invalid bind keys")
    return struct.unpack_from(fmt, data, values)


def rotate_vector(point, key):
    if key is None:
        return point
    q = tuple(value * 0.000030518044 - 1.0 for value in key)
    norm = math.sqrt(sum(axis * axis for axis in q))
    if not 0.8 <= norm <= 1.2:
        raise ValueError("invalid bind rotation")
    x, y, z, w = (axis / norm for axis in q)
    px, py, pz = point
    tx, ty, tz = 2 * (y*pz-z*py), 2 * (z*px-x*pz), 2 * (x*py-y*px)
    return px + w*tx + y*tz-z*ty, py + w*ty + z*tx-x*tz, pz + w*tz + x*ty-y*tx


def emitter_rest_position(data, bone, raw):
    """Use the first embedded/global key along the emitter's bone chain."""
    bone_count, base = struct.unpack_from("<II", data, 0x2c)
    seq_count, seq_base = struct.unpack_from("<II", data, 0x1c)
    embedded = bool(seq_count and seq_base >= 0x130 and seq_base + 64 <= len(data) and
                    struct.unpack_from("<I", data, seq_base + 12)[0] & 0x20)
    point, visited, tracks = raw, set(), set()
    while bone != 0xFFFF:
        start = base + 88 * bone
        if bone in visited or len(visited) >= 64 or bone >= bone_count or base < 0x130 or start + 88 > len(data):
            raise ValueError("invalid emitter bone chain")
        visited.add(bone)
        flags, parent = struct.unpack_from("<IH", data, start + 4)
        if flags & 0x78:
            raise ValueError("camera-facing bind transform")
        if flags & 0x280:
            translate = rest_key(data, start + 16, "<3f", embedded)
            rotation = rest_key(data, start + 36, "<4H", embedded)
            scale = rest_key(data, start + 56, "<3f", embedded)
            for key, name in ((translate, "translation"), (rotation, "rotation"), (scale, "scale")):
                if key is not None:
                    tracks.add(name)
            translate = translate or (0, 0, 0)
            scale = scale or (1, 1, 1)
            pivot = struct.unpack_from("<3f", data, start + 76)
            if not all(math.isfinite(x) and abs(x) < 10000 for x in (*translate, *scale, *pivot)):
                raise ValueError("nonfinite bind transform")
            local = tuple((point[i] - pivot[i]) * scale[i] for i in range(3))
            rotated = rotate_vector(local, rotation)
            point = tuple(rotated[i] + pivot[i] + translate[i] for i in range(3))
        bone = parent
    return point, "+".join(sorted(tracks)) or "static"


def entry(data, modern=False, audit=None):
    stride = 0x1dc
    version = 264
    if modern and data[:4] == b"MD21" and len(data) >= 8:
        length = struct.unpack_from("<I", data, 4)[0]
        if length > len(data) - 8:
            return None
        data = data[8:8 + length]
        stride = 0x1ec
        version = 272
    if data[:4] != b"MD20" or len(data) < 0x130 or struct.unpack_from("<I", data, 4)[0] != version:
        return None
    if struct.unpack_from("<I", data, 0x108)[0]:
        return None
    count, offset = struct.unpack_from("<II", data, 0x128)
    if not 0 < count <= 32 or offset < 0x130 or offset + count * stride > len(data):
        return None
    for index in range(count):
        start = offset + index * stride
        if data[start + 0x28] not in ADDITIVE:
            continue
        x, y, z = struct.unpack_from("<3f", data, start + 8)
        keys, tint = struct.unpack_from("<II", data, start + 0x10c)
        if not 0 < keys <= 16 or tint + keys * 12 > len(data):
            continue
        for r, g, b in struct.iter_unpack("<3f", data[tint:tint + keys * 12]):
            peak = max(r, g, b)
            if all(math.isfinite(n) and abs(n) < 100 for n in (x, y, z)) and 0 < peak <= 255 and min(r, g, b) >= 0:
                raw = (x, y, z)
                bone = struct.unpack_from("<H", data, start + 20)[0]
                try:
                    position, reason = emitter_rest_position(data, bone, raw)
                except ValueError as error:
                    position, reason = raw, str(error)
                if audit is not None:
                    audit.update(raw=raw, rest=position, bone=bone, tracks=reason)
                return *position, r / peak, g / peak, b / peak
    return None


def inside_bounds(name, data, light):
    """Keep an emitter offset that lies far outside its model's bounding box on the model.

    Some torches store the emitter at its bone pivot, which for ``torch.m2`` is
    17 yards above a 1.4 yard model; the flame is drawn at the top of the mesh.
    Offsets farther than half the model height (at least one yard) outside the
    box are replaced by the box centre in x and y and a point 80 percent of the
    way up (centre for lanterns).
    """
    if data[:4] == b"MD21":
        data = data[8:]
    if data[:4] != b"MD20" or len(data) < 0xB8:
        return light
    box = struct.unpack_from("<6f", data, 0xA0)
    low, high = box[:3], box[3:]
    if not all(math.isfinite(v) for v in box) or any(high[i] < low[i] for i in range(3)):
        return light
    x, y, z, *rest = light
    margin = max(1.0, 0.5 * (high[2] - low[2]))
    if all(low[i] - margin <= value <= high[i] + margin for i, value in enumerate((x, y, z))):
        return light
    centre = [(low[i] + high[i]) / 2 for i in range(3)]
    height = 0.5 if b"lantern" in name else 0.8
    return (centre[0], centre[1], low[2] + (high[2] - low[2]) * height, *rest)


STORMWIND = b"world\\wmo\\azeroth\\buildings\\stormwind\\stormwind"
STORMWIND_GLASS = b"dungeons\\textures\\doodads\\stormwindlampglass.blp"


def chunks(data):
    result = {}
    cursor = 0
    while cursor + 8 <= len(data):
        tag, size = struct.unpack_from("<4sI", data, cursor)
        cursor += 8
        if size > len(data) - cursor:
            return {}
        result[tag[::-1]] = data[cursor:cursor + size]
        cursor += size
    return result


def glass_centers(group, glass):
    if not group or len(group.get(b"MOGP", b"")) < 68:
        return
    data = chunks(group[b"MOGP"][68:])
    positions, elements, batches = (data.get(tag, b"") for tag in (b"MOVT", b"MOVI", b"MOBA"))
    if len(positions) % 12 or len(elements) % 2 or len(batches) % 24:
        return
    vertices = list(struct.iter_unpack("<3f", positions))
    indices = [index for (index,) in struct.iter_unpack("<H", elements)]
    parents = {}

    def find(point):
        parents.setdefault(point, point)
        while parents[point] != point:
            point = parents[point]
        return point

    for _, _, _, _, _, _, start, count, _, _, flags, material in struct.iter_unpack(
            "<6hIHHHBB", batches):
        if material not in glass or flags & 2 or start + count > len(indices) or count % 3:
            continue
        for i in range(start, start + count, 3):
            triangle = indices[i:i + 3]
            if any(index >= len(vertices) for index in triangle):
                continue
            if any(not math.isfinite(axis) for index in triangle for axis in vertices[index]):
                continue
            points = [tuple(round(axis, 3) for axis in vertices[index]) for index in triangle]
            for point in points[1:]:
                parents[find(point)] = find(points[0])
    components = {}
    for point in parents:
        components.setdefault(find(point), []).append(point)
    for points in components.values():
        # The stock lantern has two four-corner rings. Reject shared scenery
        # material where it does not describe a separate lantern-sized fixture.
        if len(points) != 8:
            continue
        bounds = [(min(axis), max(axis)) for axis in zip(*points)]
        if all(0.25 < hi - lo < 2.0 for lo, hi in bounds):
            yield tuple((lo + hi) * 0.5 for lo, hi in bounds)


def stormwind_mesh_lamps(read_effective):
    root = chunks(read_effective(STORMWIND + b".wmo") or b"")
    textures = root.get(b"MOTX", b"")
    materials = root.get(b"MOMT", b"")
    glass = set()
    for index in range(len(materials) // 64):
        offset = struct.unpack_from("<I", materials, index * 64 + 12)[0]
        if textures[offset:].split(b"\0", 1)[0].lower() == STORMWIND_GLASS:
            glass.add(index)
    if not glass:
        return
    bounds = root.get(b"MOGI", b"")
    for index in range(min(len(bounds) // 32, 512)):
        path = STORMWIND + f"_{index:03d}.wmo".encode("ascii")
        group = chunks(read_effective(path) or b"")
        box = struct.unpack_from("<6f", bounds, index * 32 + 4)
        for center in glass_centers(group, glass):
            if not all(math.isfinite(axis) and low - 1 <= axis <= high + 1
                       for axis, low, high in zip(center, box[:3], box[3:])):
                continue
            key = (STORMWIND + f".wmo#{index}".encode("ascii")).decode("ascii")
            yield key + "," + ",".join(f"{v:.6g}" for v in (*center, 1, 0.73, 0.38, 15, 1.0))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--data", type=Path, required=True)
    parser.add_argument("--storm", required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--audit", type=Path, help="write per-model bind and bounds decisions as JSON")
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
    decisions = {}
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
    def read_effective(name, limit=32 << 20):
        return next((data for archive in opened if (data := read(archive, name, limit))), None)

    try:
        for name in sorted(names | MESH_LAMPS.keys() | MODERN_LAMPS):
            if not name.isascii() or b"," in name:
                continue
            data = read_effective(name, 4 << 20)
            decision = {}
            light = entry(data, modern=name in MODERN_LAMPS, audit=decision) if data else None
            if light is not None:
                base = name.rsplit(b"\\", 1)[-1]
                x, y, z, red, green, blue = light
                radius, strength = 13, 0.85
                if b"candle" in base:
                    radius, strength = 2.5, 0.5
                elif TORCH.search(base) or FIRE.search(base):
                    radius = 7 if TORCH.search(base) else 8
                    green, blue = green * 0.78, blue * 0.52
                light = (x, y, z, red, green, blue, radius, strength)
            elif data and data[:8] == b"MD20\x08\x01\0\0" and len(data) >= 0x130:
                if not struct.unpack_from("<I", data, 0x108)[0]:
                    light = MESH_LAMPS.get(name)
            if light is not None and data:
                light = inside_bounds(name, data, light)
            if light is not None:
                decision["catalog"] = light[:3]
                if "track" not in decision and "rest" not in decision:
                    decision["tracks"] = "mesh-derived"
                decisions[name.decode("ascii")] = decision
                catalog.append(name.decode("ascii") + "," + ",".join(f"{v:.5g}" for v in light))
        mesh = list(stormwind_mesh_lamps(read_effective))
    finally:
        for archive in opened:
            lib.SFileCloseArchive(archive)
    if not catalog:
        raise SystemExit("no supported lamps found")
    args.out.write_text("# model or WMO#group,x,y,z,red,green,blue,radius,intensity\n" +
                        "\n".join(catalog + mesh) + "\n")
    if args.audit:
        import json
        args.audit.write_text(json.dumps({"models": decisions, "embedded": mesh}, indent=2) + "\n")
    print(f"{len(opened)} archives, {len(names)} candidate names, {len(catalog)} models, {len(mesh)} embedded WMO lamps")


if __name__ == "__main__":
    main()
