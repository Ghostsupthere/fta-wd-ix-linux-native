# DrakeishUI changes from the Linux client

`Addons/DrakeishUI` is DrakeishUI as the Linux client currently runs it.

## What is in the folder

- Every Lua, XML, TOC and text file, including the new modules.
- Only the textures and fonts that the addon bundle's copy does not have (43 files).

The other 1,119 textures and fonts are byte-identical to the bundle's copy, so they are not stored here. **This folder is not a complete addon on its own.** Copy it over a DrakeishUI folder from the bundle.

The bundle also has `modules/bottombarstoggle.lua` and `modules/sidebars.lua`. No XML or TOC file loads either one, so they are not stored here. Copying this folder over yours leaves them alone.

## Seeing what changed

The folder was added in two commits:

1. **`DrakeishUI: base copy from the required-addon bundle (text files)`**: the bundle's versions, unchanged.
2. **`DrakeishUI: Linux client changes`**: our changes. `git show` on this commit gives the full diff.

The base is the bundle copy we received on 2026-11-25. If your files have changed since then, merge the second commit instead of copying over them. One way, from the root of this repo:

```sh
git format-patch -1 b4cc5fc --stdout -- Addons/DrakeishUI > drakeishui-linux.patch
```

Then, inside your own DrakeishUI git checkout:

```sh
git apply -p3 --3way drakeishui-linux.patch
```

If `--3way` cannot find the base files in your checkout, use `git apply -p3 --reject` and resolve the `.rej` files.

## Main additions

- **Settings window** with profiles, export and import, and a command for each option (`/drakeish`).
- **Action bars and layout:** cooldown numbers, end cap fit, keybind mode, element sizes and scale sliders.
- **Unit frames:** health and power text, bar sizes, portrait choices.
- **Bags, chat, footer, fonts and tooltips** options.
- **Nameplates** (`modules/Kui_Nameplates`, options in `nameplate_options.lua`, opened with `/knp`): a "Drakeish" bar texture matching the cast bar, separate name and health fonts, full-length names, a height offset and a debuff row. The debuff row uses the Linux client's `C_NamePlate` API and `nameplate1` to `nameplate64` unit tokens. It does nothing if `C_NamePlate` is missing.
- **Fixes:** the target aura `SetPoint` loop error, and the out-of-range overlay on resized action buttons.

Some features depend on the Linux client engine in this repo and may behave differently on other clients.

## Tests

The tests need `lua5.1` and a complete addon folder (the bundle's copy with this folder copied over it). From the repo root:

```sh
bash tests/drakeishui/run.sh /path/to/DrakeishUI
```

They use a mock of the game API, so they do not replace an in-game check.

## Notes

- `SelectivePackageManifest.json` lists the size and SHA-256 of the complete addon's files. Git line-ending conversion is turned off for this folder so those values stay correct.
- Some files in `tools/drakeishui` are also in this folder. They are currently identical. This folder shows how they fit into the whole addon.
