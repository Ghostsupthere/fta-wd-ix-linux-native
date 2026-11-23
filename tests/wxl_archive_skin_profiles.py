#!/usr/bin/env python3
"""Inspect the skin/material contract of a model and all its archived profiles."""
import argparse
from collections import Counter
from pathlib import Path
import runpy
import struct

ROOT = Path(__file__).resolve().parents[1]


def table(data, at, stride):
    count, offset = struct.unpack_from("<II", data, at)
    if count and (offset < 4 or offset + count * stride > len(data)):
        raise ValueError(f"table at 0x{at:x} has invalid bounds")
    return data[offset:offset + count * stride]


def inspect_profile(skin, model):
    if not skin or skin[:4] != b"SKIN":
        raise ValueError("missing or invalid SKIN")
    vertices = table(skin, 4, 2)
    indices = table(skin, 12, 2)
    sections = table(skin, 28, 48)
    batches = table(skin, 36, 24)
    materials = list(struct.iter_unpack("<HH", table(model, 0x70, 4)))
    combos = [i for (i,) in struct.iter_unpack("<H", table(model, 0x80, 2))]
    weight_count = len(table(model, 0x90, 2)) // 2
    coordinate_count = len(table(model, 0x88, 2)) // 2
    transform_count = len(table(model, 0x98, 2)) // 2
    texture_count = len(table(model, 0x50, 16)) // 16
    signatures = Counter()
    issues = Counter()
    for batch in struct.iter_unpack("<Bb11H", batches):
        _, _, shader, section, geoset, color, material, layer, count, texture, coordinate, weight, transform = batch
        if section >= len(sections) // 48 or material >= len(materials):
            issues["section or material index"] += 1
            continue
        if any(start + count > capacity for start, capacity in
               ((texture, len(combos)), (coordinate, coordinate_count),
                (weight, weight_count), (transform, transform_count))):
            issues["lookup span"] += 1
        slots = tuple(combos[texture:texture + count])
        if any(slot >= texture_count for slot in slots):
            issues["texture index"] += 1
        signatures[(geoset, materials[material], shader, color, layer, slots, coordinate, weight, transform)] += 1
    return len(vertices) // 2, len(indices) // 2, len(batches) // 24, signatures, issues


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--storm", required=True)
    parser.add_argument("--data", required=True, type=Path)
    parser.add_argument("model")
    args = parser.parse_args()
    mpq = runpy.run_path(str(ROOT / "tools/build-wmo-shader-pack.py"))["Mpq"](args.storm, args.data)
    data = mpq.read(args.model)
    if not data:
        raise SystemExit("model not found")
    if data.startswith(b"MD21"):
        length = struct.unpack_from("<I", data, 4)[0]
        data = data[8:8 + length]
    if data[:4] != b"MD20":
        raise SystemExit("missing MD20 model payload")
    version = struct.unpack_from("<I", data, 4)[0]
    profiles = struct.unpack_from("<I", data, 0x44)[0]
    if not 1 <= profiles <= 4:
        raise SystemExit(f"unexpected profile count {profiles}")
    print(f"{args.model} version {version}, skin profiles {profiles}")
    baseline = None
    for profile in range(profiles):
        path = args.model[:-3] + f"{profile:02d}.skin"
        vertices, indices, batches, signatures, issues = inspect_profile(mpq.read(path), data)
        print(f"{profile:02d} {vertices} vertices, {indices} indices, {batches} batches, issues {dict(issues)}")
        if baseline is None:
            baseline = signatures
        else:
            print(f"    relative to 00: {sum((signatures - baseline).values())} changed or added bindings, "
                  f"{sum((baseline - signatures).values())} removed bindings")
        for signature, count in signatures.items():
            print(f"    {count} x {signature}")


if __name__ == "__main__":
    main()
