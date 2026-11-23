#!/usr/bin/env python3
"""Stage the maintained Windows TOC/XML closure with pinned upstream media."""
import argparse
from collections import defaultdict
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess

UPSTREAM = "ac56f1765e1daf92627e7ad88f69fb0551e97b84"
TOC = "DrakeishUI.toc"
XML_FILE = re.compile(r'<(?:Script|Include)\b[^>]*\bfile\s*=\s*["\']([^"\']+)["\']', re.I)
MEDIA_ALIASES = {
    "assets/redbutton2x.blp": "assets/ui/redbutton2x.blp",
    "assets/Dark_UI/bagslots2x_dark.blp": "assets/bagslots2x.blp",
    "assets/Dark_UI/uiactionbar2x__dark.blp": "assets/uiactionbar2x_.blp",
    "assets/Dark_UI/uiactionbar2x_flying_dark.blp": "assets/uiactionbar2x_flying.blp",
    "assets/Dark_UI/uiactionbar2x_new_dark.blp": "assets/uiactionbar2x_new.blp",
    "assets/Dark_UI/uimicromenu2x_dark.blp": "assets/uimicromenu2x.blp",
}
MEDIA = {".blp", ".tga", ".dds", ".ttf", ".otf", ".wav", ".mp3", ".ogg"}
ASSET_PATH = re.compile(r'assets[/\\]+([^"\'\r\n<>]+)', re.I)


def index(root):
    return {path.relative_to(root).as_posix().casefold(): path
            for path in sorted(root.rglob("*")) if path.is_file() and ".git" not in path.parts}


def key(path):
    return re.sub(r"[/\\]+", "/", path).strip("/").casefold()


def closure(files):
    selected = {}
    visit = [TOC.casefold()]
    while visit:
        name = visit.pop()
        if name in selected:
            continue
        if name not in files:
            raise ValueError(f"missing Windows load-graph file {name}")
        source = files[name]
        selected[name] = source
        if source.suffix.lower() not in {".xml", ".toc"}:
            continue
        text = source.read_text(encoding="utf-8-sig")
        if source.suffix.lower() == ".xml":
            references = XML_FILE.findall(re.sub(r"<!--.*?-->", "", text, flags=re.S))
            parent = name.rpartition("/")[0]
        else:
            references = [line.strip() for line in text.splitlines() if line.strip() and not line.lstrip().startswith("#")]
            parent = ""
        for ref in reversed(references):
            relative = key(f"{parent}/{ref}")
            visit.append(relative if relative in files else key(ref))
    return selected


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--windows", type=Path, required=True)
    parser.add_argument("--upstream", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True, help="new DrakeishUI directory")
    args = parser.parse_args()
    version = subprocess.check_output(["git", "-C", str(args.upstream), "rev-parse", "HEAD"], text=True).strip()
    if version != UPSTREAM:
        raise SystemExit(f"expected upstream media revision {UPSTREAM}, got {version}")
    windows, upstream = index(args.windows), index(args.upstream)
    selected = closure(windows)
    prefixes, missing = set(), set()
    for name, source in tuple(selected.items()):
        if source.suffix.lower() not in {".lua", ".xml"}:
            continue
        text = source.read_text(encoding="utf-8-sig", errors="replace")
        for value in ASSET_PATH.findall(text):
            path = "assets/" + key(value)
            top, sep, _ = path.removeprefix("assets/").partition("/")
            prefix = "assets/" + top + "/"
            if any(p.startswith(prefix) for p in upstream):
                prefixes.add(prefix)
            if Path(path).suffix in MEDIA and path not in windows and path not in upstream:
                if path not in {alias.casefold() for alias in MEDIA_ALIASES}:
                    missing.add(f"{name}: {path}")
    # These atlas families are indexed through variables by retained modules.
    prefixes.update("assets/" + group + "/" for group in ("actionbars", "bags", "characterpanel",
        "classicons", "collections", "end_caps", "raidframe", "social", "spellbook", "talents", "ui", "worldmap"))
    for name, source in {**upstream, **windows}.items():
        root_asset = name.startswith("assets/") and name.count("/") == 1
        if source.suffix.lower() in MEDIA and (root_asset or any(name.startswith(prefix) for prefix in prefixes)):
            selected[name] = source
    for prefix in prefixes:
        if not any(name.startswith(prefix) for name in selected):
            missing.add(f"media folder missing: {prefix}")
    args.out.mkdir(parents=True, exist_ok=False)
    manifest = []
    totals = defaultdict(int)
    for name, source in sorted(selected.items()):
        origin = "windows" if source == windows.get(name) else "upstream-media"
        relative = source.relative_to(args.windows if origin == "windows" else args.upstream)
        target = args.out / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, target)
        data = target.read_bytes()
        if source.suffix.lower() in MEDIA:
            module = relative.parts[1] if relative.parts[0].casefold() == "assets" and len(relative.parts) > 2 else relative.parts[0]
            totals[module] += len(data)
        manifest.append(dict(path=relative.as_posix(), size=len(data), sha256=hashlib.sha256(data).hexdigest(), source=origin))
    for relative, original in MEDIA_ALIASES.items():
        if relative.casefold() in selected:
            continue
        if original not in upstream:
            missing.add(f"missing media fallback: {relative}")
            continue
        target = args.out / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        data = upstream[original].read_bytes()
        target.write_bytes(data)
        totals["fallbacks"] += len(data)
        manifest.append(dict(path=relative, size=len(data), sha256=hashlib.sha256(data).hexdigest(),
                             source=f"upstream-fallback:{original}"))
    result = dict(upstream_media_revision=UPSTREAM, files=manifest, media_bytes=dict(sorted(totals.items())),
                  missing_media=sorted(missing))
    (args.out / "SelectivePackageManifest.json").write_text(json.dumps(result, indent=2) + "\n")
    print(f"{len(manifest)} files, {sum(item['size'] for item in manifest):,} bytes")
    for name, size in sorted(totals.items()):
        print(f"{name} {size:,} bytes")
    for path in sorted(missing):
        print(f"unresolved reference: {path}")


if __name__ == "__main__":
    main()
