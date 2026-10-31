# Native Linux launcher and sharing audit

## Launching this checkout

Run `./launch-native.sh`, or open **FTA Native Client** from the applications menu
or desktop after running `./install-native-launcher.py`. A desktop may ask to
allow launching its shortcut the first time. Re-run the installer after moving
the checkout: desktop entries contain absolute paths.

The launcher finds this checkout from its own location, uses its
`.deps/dxvk/build-native-sdl3`, enters `shell-native.nix`, selects SDL3, adds the
native D3D9 library and host `/usr/lib` to the runtime search path, and starts
`bin/Linux/Wow64` with the game directory as its working directory. It forwards
game arguments without evaluating them as commands. It does not rebuild, edit
settings, or stop an existing game. Optional `FTA_NATIVE_GAME_DIR` and
`DXVK_NATIVE_DIR` environment overrides are supported.

Each launch writes `bin/Linux/Logs/native-launch-<date>-<pid>.log`. Failed launches
also report their log path via a desktop notification when `notify-send` exists.
The game retains its own crash reports under `bin/Linux/Errors/`.

`./launch-native.sh --check` checks directories, DXVK and linked libraries through
the Nix environment without launching the game. It does not test GPU presentation,
log in, or confirm server reachability. `./install-native-launcher.py --output-dir
/tmp/fta-launcher-review` generates a shortcut for review without installing it.

## Can friends copy the folder today?

**Not reliably. Same CachyOS does not reproduce this development environment.**
The following were verified locally on 2026-10-30:

| Dependency | Current state | Needed for a shareable release |
| --- | --- | --- |
| Asset directories | `bin/Linux/{Data,Interface,DBFilesClient,Utils,WTF,fonts,Cache}` use absolute links into Shawn's original client | Build a standalone staging directory containing the actual required files; use internal relative links only. Include the matching patches, addons, loose DBCs, fonts and runtime asset maps. |
| Executable loader | Wow64's ELF interpreter is a specific `/nix/store/...-glibc-2.44-57/lib/ld-linux-x86-64.so.2` | Preserve its exact runtime closure with a Nix package, or rebuild/repackage for a portable Linux runtime. Changing LD_LIBRARY_PATH cannot repair a missing ELF interpreter. |
| Library paths | Wow64 RUNPATH includes exact Nix SDL3/audio/curl/xvid/GCC libraries and Shawn's absolute DXVK build path | Package/version all runtime dependencies and remove developer-directory assumptions. Installing today's Nix channel alone need not recreate the old exact paths. |
| DXVK | Libraries are outside `bin/Linux`, in `.deps/dxvk/build-native-sdl3` | Ship the native DXVK runtime libraries together with the client and set a relative search path. A Windows d3d9.dll does not supply this native dependency. |
| Runtime-loaded libraries | Vulkan and SDL backends load libraries beyond direct ELF NEEDED entries | Test runtime-loaded dependencies too; `ldd` alone is insufficient. Keep host GPU driver/ICD integration. |
| Graphics/session | Current setup needs Nix's Vulkan loader plus host `/usr/lib`; tested on this machine | Verify recipients' Vulkan drivers and SDL3 on their Wayland/X11 session. Same OS does not imply the same GPU or display setup. Avoid shipping Shawn's NVIDIA driver as a universal dependency. |
| Server addresses | Config uses private LAN `192.168.50.215`; locale realmlist also exists under Data/enUS | Supply the reachable auth address and ensure the server advertises a reachable realm/world address. Outside the LAN, use a reachable server/VPN arrangement. This audit changes no server or network settings. |
| Personal/writable data | WTF/Account, Cache, Logs, Errors and hardware/display preferences belong to the current installation | Produce fresh per-player writable state and a clean config template; omit personal account/addon data and crash logs. Let each player choose graphics/display preferences. |
| Version consistency | Client patches, addons, native binary and server extensions work as a set | Publish a version/checksum manifest and keep the matching assets and protocol versions together. Source-only Git checkout does not contain the game assets or built runtime. |

## Future packaging work

The least disruptive first route is a **pinned Nix runtime package** containing
Wow64 and native DXVK, with the complete transitive store closure made available
to recipients who install Nix. Keep a package/profile reference so garbage
collection cannot remove the runtime. Export/import must include the full closure,
not just the top-level store object. See the official
[Nix closure-copy documentation](https://nix.dev/manual/nix/2.18/package-management/copy-closure)
and [export operation](https://nix.dev/manual/nix/2.26/command-ref/nix-store/export).
The current unpinned development shell is not that release package.

For a folder that launches without Nix installation, build a separate portable
release: choose a supported glibc baseline, package appropriate user-space
libraries, replace machine-specific paths with `$ORIGIN`-relative paths, and handle
the loader and host Vulkan drivers deliberately. Merely rewriting RUNPATH or
putting today's Nix executable into an AppImage does not remove its interpreter
and transitive runtime dependencies.

Then stage clean assets/settings, generate the installed shortcut on the recipient
machine, and test the release as a different user on a clean CachyOS machine
without Shawn's checkout or Nix store paths. Verify login, world entry, character
creation, armor/physics, Transmogrification, PocketPals, audio, fullscreen, restart
and saving preferences. Test both the intended server connection and a missing
server. No large asset copy, distributable bundle, or network change has been
performed as part of this audit.

The installer follows the
[Desktop Entry specification](https://specifications.freedesktop.org/desktop-entry/latest-single/)
for Exec quoting and creates user-local shortcuts only.

## Patched shader packs (WMO layered materials, grass motion and instanced grass)

Modern WMO layered materials use `Utils/WarcraftXL/visuals/wmo_shaders.pack`, grass motion uses
`Utils/WarcraftXL/visuals/grass_shaders.pack` and instanced grass uses
`Utils/WarcraftXL/visuals/grass_instanced.pack`, all generated
per install by `tools/build-wmo-shader-pack.py` (needs Wine, mingw and the pinned
`d3dcompiler_47.dll`; see `Engine/Source/ModernAssets/WxlVisuals/SOURCE.md`). It is derived
from the client's own shader files, so it is not committed or redistributed. A shared
folder without it still runs; those WMOs draw stock, and grass draws the stock way without the instanced pack (`WXL_GRASS_INSTANCING=0` also turns it off). Regenerate it if the client's shader
files change.
