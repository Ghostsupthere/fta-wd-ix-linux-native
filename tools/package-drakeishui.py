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
from xml.etree import ElementTree

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
TEXTURES = (".blp", ".tga", ".dds")
MEDIA = {".blp", ".tga", ".dds", ".ttf", ".otf", ".wav", ".mp3", ".ogg"}
# Texture trees outside assets/ that the Windows load graph references by path or builds names from.
UPSTREAM_TREES = ("modules/dragonflightui/textures/", "libs/classicapi/texture/")
ADDON_PATH = re.compile(r"""['"](Interface[\\/]+Add[Oo]ns[\\/]+DrakeishUI[\\/]+[^'"]+)['"]""", re.I)
ASSET_PATH = re.compile(r'assets[/\\]+([^"\'\r\n<>]+)', re.I)


COMPAT = "linux_compat_bags.lua"
ACCOUNT_SAVED_VARIABLES = (
    "## SavedVariables: DragonflightUIDB, collapse_state, buffs_state, bags_state,quest_state, "
    "chat_state, party_state, caps_state, stance_state, bottombars_state, sidebars_state, "
    "hide_toggle_buttons, unitframes_state, gryphon_style_state, diablo_orbs_state, "
    "hide_pet_frame_state, raid_frames_state, raid_frame_style_state, D32MiniMapButtonPosition, "
    "darken_ui_state, DrakeishUI_BagDB, DrakeishUI_SocialDB"
)
# One-line source patches applied to Windows files while staging (path -> [(old, new)]); each `old` must
# occur exactly once, so an upstream change that moves the line fails the build instead of going missing.
PATCHES = {
    "modules/mainbars.lua": [(
        "\tMainMenuBarLeftEndCap:SetClearPoint('BOTTOMLEFT', endCapLeftX, y)\n"
        "\tMainMenuBarRightEndCap:SetClearPoint('BOTTOMRIGHT', endCapRightX, y)\n",
        "\tlocal left, right = endCapLeftX, endCapRightX\n"
        "\tif addon.GetEndCapOffsets then left, right = addon.GetEndCapOffsets(left, right) end\n"
        "\tMainMenuBarLeftEndCap:SetClearPoint('BOTTOMLEFT', left, y)\n"
        "\tMainMenuBarRightEndCap:SetClearPoint('BOTTOMRIGHT', right, y)\n",
    )],
    # The 3.3.5 client ships no DressUpBackground art for High Elf, Goblin, Worgen or Murloc, so the
    # character panel's model backdrop was plain black for those races. Borrow existing art.
    "modules/characterpanel/model.lua": [(
        'local RACE_FALLBACK = { GNOME = "Dwarf", TROLL = "Orc" }\n',
        'local RACE_FALLBACK = { GNOME = "Dwarf", TROLL = "Orc", HIGHELF = "BloodElf", GOBLIN = "Orc",\n'
        '    WORGEN = "Human", MURLOC = "Orc" }\n',
    )],
    "modules/chat.lua": [(
        '    chat:SetPoint("BOTTOMLEFT", chatAnchor, "BOTTOMLEFT", 0, 0)\n'
        '    chat:SetScale(chatAnchor:GetScale())\n'
        '    chat:SetSize(chatAnchor:GetWidth(), chatAnchor:GetHeight())\n',
        '    local inset = addon.ChatOptions and addon.ChatOptions:GetInset() or 0\n'
        '    chat:SetPoint("BOTTOMLEFT", chatAnchor, "BOTTOMLEFT", inset, inset)\n'
        '    chat:SetScale(chatAnchor:GetScale())\n'
        '    chat:SetSize(math.max(1, chatAnchor:GetWidth() - 2 * inset),\n'
        '        math.max(1, chatAnchor:GetHeight() - 2 * inset))\n',
    )],
    "modules/modules.xml": [(
        '\t<Script file="cooldowns.lua"/>\n',
        "",
    )],
    "modules/native_layout.lua": [(
        '    {"MinimalChatAnchor","Chat"}}',
        '    {"MinimalChatAnchor","Chat"}, {"ImprovedUICastBar","Player cast bar"}, {"WatchFrame","Objective tracker"},\n'
        '    {"DrakeishUICombinedBags","Combined bags"}, {"DrakeishUI_RaidFrames","Raid frames"},\n'
        '    {"pUiMicroMenu","Micro menu",96,24}}',
    )],
    "modules/buffs.lua": [(
        'end\n\n-- Reuse one driver; aura/anchor bursts',
        '    if addon.Dimensions then addon.Dimensions:ApplyAuras(OFFSET_X) end\n'
        'end\n\n-- Reuse one driver; aura/anchor bursts',
    )],
    "modules/dragonflightui/modules/castbar.lua": [(
        "(value * sizeX) / statusMax",
        "(value * f:GetWidth()) / statusMax",
    ), (
        "tick:SetSize(2.5, sizeY - 2)",
        "tick:SetSize(2.5, f:GetHeight() - 2)",
    )],
    "modules/raidframes.lua": [(
        'local UNIT_W = 72\n',
        'local _, addon = ...\n'
        'local function RaidDimension(key,default)\n'
        '    return addon.Dimensions and addon.Dimensions:Value("raid",key,default) or default\n'
        'end\n'
        'local function UnitWidth() return RaidDimension("width",72) end\n',
    ), (
        '    if RaidFrameStyle() == 2 then return UNIT_H_THIN end\n    return UNIT_H_FULL',
        '    if RaidFrameStyle() == 2 then return RaidDimension("height", UNIT_H_THIN) end\n    return RaidDimension("height", UNIT_H_FULL)',
    ), (
        'local GROUP_GAP = 6',
        'local function GroupGap() return RaidDimension("gap",6) end',
    ), (
        'f:SetWidth(UNIT_W)',
        'f:SetWidth(UnitWidth())',
    ), (
        '(UNIT_W + GROUP_GAP) * GROUPS_PER_ROW',
        '(UnitWidth() + GroupGap()) * GROUPS_PER_ROW',
    ), (
        '            if raidUnits[i] then raidUnits[i]:SetHeight(h) end\n',
        '            if raidUnits[i] then raidUnits[i]:SetSize(UnitWidth(),h) end\n',
    ), (
        '        if raidPet then raidPet:SetHeight(h) end',
        '        if raidPet then raidPet:SetSize(UnitWidth(),h) end',
    ), (
        'col * (UNIT_W + GROUP_GAP)',
        'col * (UnitWidth() + GroupGap())',
    ), (
        'cols * (UNIT_W + GROUP_GAP) - GROUP_GAP + PAD',
        'cols * (UnitWidth() + GroupGap()) - GroupGap() + PAD',
    )],
    "modules/itemlevel.lua": [
        (
            '    fontString:SetFont(path, size, outline)\n',
            '    if addon.Appearance then addon.Appearance:ApplyFont(fontString, "bags", path, size, outline)\n'
            '    else fontString:SetFont(path, size, outline) end\n',
        ),

        (
            '    if not IsModuleEnabled() then return false end\n    local config = GetModuleConfig()\n',
            '    if not IsModuleEnabled() then return false end\n'
            '    local override = addon.BagQuality and addon.BagQuality:LevelContext(context)\n'
            '    if override ~= nil then return override end\n'
            '    local config = GetModuleConfig()\n',
        ),
        (
        '    fontString:SetTextColor(r or 1, g or 1, b or 1)\n    fontString:Show()\n',
        '    fontString:SetTextColor(r or 1, g or 1, b or 1)\n    fontString:Show()\n'
        '    if addon.BagQuality then addon.BagQuality:PaintLevel(button, fontString, anchorTo or button) end\n',
    )],
    "modules/native_controls.lua": [
        (
            '    message=string.lower(message or "")\n',
            '    if addon.Settings and addon.Settings.HandleCommand and addon.Settings:HandleCommand(message or "") then return end\n'
            '    message=string.lower(message or "")\n',
        ),
        (
        '    elseif InterfaceOptionsFrame_OpenToCategory then InterfaceOptionsFrame_OpenToCategory(showPanel()) end\n',
        '    elseif message:match("^%s*$") and addon.Settings then addon.Settings:Open()\n'
        '    elseif InterfaceOptionsFrame_OpenToCategory then InterfaceOptionsFrame_OpenToCategory(showPanel()) end\n',
    )],
    "modules/characterpanel/settingscog.lua": [
        (
            '    CP.ApplyBodyBackground()\n',
            '    if not addon.Settings or not addon.Settings:IsReady() then CP.ApplyBodyBackground() end\n',
        ),
        (
            '    if CP.ApplyModelBackdrop then CP.ApplyModelBackdrop() end\n',
            '    if (not addon.Settings or not addon.Settings:IsReady()) and CP.ApplyModelBackdrop then\n'
            '        CP.ApplyModelBackdrop()\n'
            '    end\n',
        ),
        (
            '    if CP.ApplyGearSummaryVisibility then CP.ApplyGearSummaryVisibility() end\n',
            '    if (not addon.Settings or not addon.Settings:IsReady()) and CP.ApplyGearSummaryVisibility then\n'
            '        CP.ApplyGearSummaryVisibility()\n'
            '    end\n',
        ),
        (
            '        menuEntries(entries)\n',
            '        if addon.Settings then\n'
            '            entries[#entries + 1] = { text = addon.L["DrakeishUI Settings"], func = function()\n'
            '                addon.Settings:Open("Character Panel")\n'
            '            end }\n'
            '        end\n'
            '        menuEntries(entries)\n',
        ),
    ],
    # The sidebar's "Tertiary" stats block is filled by FTA_Tertiary, which no addon we ship defines, so
    # it showed up as an empty PLAYERSTAT_TERTIARY header (ticket #10). Show it only when a provider exists.
    "modules/characterpanel/sidebar.lua": [(
        '    if key == "gearscore" then return cfg.show_gear_score and true or false end\n    return true\n',
        '    if key == "gearscore" then return cfg.show_gear_score and true or false end\n'
        '    if key == "PLAYERSTAT_TERTIARY" then return FTA_Tertiary ~= nil and FTA_Tertiary.UpdateRows ~= nil end\n'
        '    return true\n',
    )],
}
PATCHES["modules/dragonflightui/modules/partyframes.lua"] = (
    (
        '            frame.portrait:SetPoint("LEFT", frame, "LEFT", 4, 0)',
        '            frame.portrait:SetPoint("LEFT", frame, "LEFT", 4, 0)\n'
        '            if addon.RegisterPartyPortrait then addon.RegisterPartyPortrait(unit, frame.portrait) end',
    ),
)
SETTINGS_FILES = ("settings_profiles.lua", "settings_commands.lua", "settings_character.lua", "settings_window.lua", "action_cooldowns.lua", "unit_values.lua", "bag_quality.lua", "chat_options.lua", "chat_footer.lua", "appearance.lua", "layout_dimensions.lua", "profile_exchange.lua", "action_endcaps.lua", "keybind_mode.lua", "unit_portraits.lua")
TWEAKS = "fta_ui_tweaks.lua"  # FTA changes that need the whole addon loaded first: listed last in the TOC


def add_linux_compat(toc):
    """Register the Linux saved variable and keep the shims first and last in the TOC."""
    text = toc.decode("utf-8-sig")
    newline = "\r\n" if "\r\n" in text else "\n"
    lines = text.split(newline)
    if lines.count(ACCOUNT_SAVED_VARIABLES) != 1:
        raise SystemExit(f"account SavedVariables patch for {TOC} does not apply exactly once")
    saved_variables = lines.index(ACCOUNT_SAVED_VARIABLES)
    lines[saved_variables] += ", DrakeishUI_Config"
    first = next(i for i, line in enumerate(lines) if line.strip() and not line.lstrip().startswith("#"))
    lines.insert(first, COMPAT)
    while lines and not lines[-1].strip():
        lines.pop()
    lines.extend(SETTINGS_FILES)
    lines.append(TWEAKS)
    lines.append("")
    return newline.join(lines).encode("utf-8")


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


def xml_texture_references(file):
    for element in ElementTree.parse(file).iter():
        tag = element.tag.rsplit("}", 1)[-1].casefold()
        if tag in {"script", "include"}:
            continue
        value = key(element.get("file", ""))
        prefix = "interface/addons/drakeishui/"
        if value.startswith(prefix):
            yield value.removeprefix(prefix)


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
    for name, source in upstream.items():
        if name.startswith(UPSTREAM_TREES) and source.suffix.lower() in MEDIA:
            selected.setdefault(name, source)
    for name, source in {**upstream, **windows}.items():
        root_asset = name.startswith("assets/") and name.count("/") == 1
        if source.suffix.lower() in MEDIA and (root_asset or any(name.startswith(prefix) for prefix in prefixes)):
            selected[name] = source
    for prefix in prefixes:
        if not any(name.startswith(prefix) for name in selected):
            missing.add(f"media folder missing: {prefix}")
    available = {**upstream, **windows}
    fallbacks = {key(alias): upstream[original] for alias, original in MEDIA_ALIASES.items()
                 if original in upstream}
    missing_xml = set()
    for name, source in tuple(selected.items()):
        if source.suffix.lower() != ".xml":
            continue
        for reference in xml_texture_references(source):
            paths = [reference] if Path(reference).suffix else [reference + ext for ext in TEXTURES]
            media = next((path for path in paths if path in available or path in fallbacks), None)
            if media is None:
                missing_xml.add(f"{name}: XML texture {reference}")
            elif media in available:
                selected[media] = available[media]
    if missing_xml:
        for path in sorted(missing_xml):
            print(f"unresolved reference: {path}")
        raise SystemExit(f"{len(missing_xml)} missing XML texture reference(s)")

    present = set(selected) | set(fallbacks)
    folders = {"/".join(name.split("/")[:i]) for name in present for i in range(1, name.count("/") + 1)}
    for name, source in tuple(selected.items()):
        if source.suffix.lower() not in {".lua", ".xml"}:
            continue
        for value in ADDON_PATH.findall(source.read_text(encoding="utf-8-sig", errors="replace")):
            reference = key(value).split("/", 3)[-1]
            if not reference or reference.endswith("/"):
                continue
            if reference not in folders and not any(reference + ext in present for ext in ("", *sorted(MEDIA))):
                missing.add(f"{name}: addon path {reference}")
    args.out.mkdir(parents=True, exist_ok=False)
    manifest = []
    totals = defaultdict(int)
    for name, source in sorted(selected.items()):
        origin = "windows" if source == windows.get(name) else "upstream-media"
        relative = source.relative_to(args.windows if origin == "windows" else args.upstream)
        target = args.out / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, target)
        if origin == "windows" and relative.as_posix().casefold() == TOC.casefold():
            target.write_bytes(add_linux_compat(target.read_bytes()))
        for old_text, new_text in PATCHES.get(relative.as_posix().casefold(), ()) if origin == "windows" else ():
            text = target.read_bytes().decode("utf-8")
            if text.count(old_text) != 1:
                raise SystemExit(f"patch for {relative} does not apply exactly once")
            target.write_bytes(text.replace(old_text, new_text).encode("utf-8"))
        data = target.read_bytes()
        if source.suffix.lower() in MEDIA:
            module = relative.parts[1] if relative.parts[0].casefold() == "assets" and len(relative.parts) > 2 else relative.parts[0]
            totals[module] += len(data)
        manifest.append(dict(path=relative.as_posix(), size=len(data), sha256=hashlib.sha256(data).hexdigest(), source=origin))
    compat = Path(__file__).with_name("drakeishui") / COMPAT
    data = compat.read_bytes()
    (args.out / COMPAT).write_bytes(data)
    manifest.append(dict(path=COMPAT, size=len(data), sha256=hashlib.sha256(data).hexdigest(), source="linux-compat"))
    for name in (*SETTINGS_FILES, TWEAKS):
        data = (Path(__file__).with_name("drakeishui") / name).read_bytes()
        (args.out / name).write_bytes(data)
        manifest.append(dict(path=name, size=len(data), sha256=hashlib.sha256(data).hexdigest(), source="fta-tweaks"))
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
