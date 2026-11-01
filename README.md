# From The Ashes: native Linux client

A native Linux build (no Wine) of the World of Warcraft 3.3.5a (build 12340) client, with the
**From The Ashes** client extensions (WarcraftXL) ported in-process: modern models and textures, collection
armor, the improved world renderer, and the extra UI tools the From The Ashes server uses.

It is built on wowdecomp, a C++ reconstruction of the original
client in which every function keeps the address it came from (`// OFFSET:` tags) so it can be compared
against the original. This repository adds the Linux platform layer and the WarcraftXL ports. The game
renders through [dxvk-native](https://github.com/doitsujin/dxvk) (Direct3D 9 on Vulkan) with SDL3 for the
window, input and audio.

Join the Discord for other clients and to show off what you build: https://discord.gg/DsZMGUD8aT

> **Status.** The client builds, links, logs in and runs in the game world, and its maintainer plays on it. It is not at proven parity with the Windows client: each feature below is marked as confirmed in
> game, implemented but not yet checked in game, or off by default. The full, honest ledger is
> [WXL-NATIVE-PORT.md](WXL-NATIVE-PORT.md).

## Install (players)

You need a From The Ashes base client folder (the one with `Wow.exe`, `Data`, `Interface`, `WTF`) and a
Linux x86-64 machine with a Vulkan GPU. The native client installs **into that folder**, uses its game data
and leaves the Windows files untouched. There is no game data in this repository or in the overlay.

1. Get the overlay `wd-ix-linux-native-portable.tar.gz` (from the maintainer or the repository's Releases
   page), or build it from a built source tree with `tools/make-linux-portable.sh`.
2. Unpack it and install:

   ```
   mkdir fta-linux && tar -xzf wd-ix-linux-native-portable.tar.gz -C fta-linux && cd fta-linux
   ./install.sh /path/to/From_The_Ashes_Client
   ```

3. Check and install the system libraries it needs (SDL3, libcurl, ALSA, PulseAudio client libraries, a
   Vulkan driver):

   ```
   /path/to/From_The_Ashes_Client/FTA-Linux/install-deps.sh            # report what is missing
   /path/to/From_The_Ashes_Client/FTA-Linux/install-deps.sh --install  # install it (asks first, uses sudo)
   ```

4. Verify and play:

   ```
   /path/to/From_The_Ashes_Client/FTA-Linux/play.sh --check
   /path/to/From_The_Ashes_Client/FTA-Linux/play.sh
   ```

The overlay carries its own glibc, C++ runtime, xvidcore and dxvk-native, so it does not depend on your
distribution's glibc. Logs go to `Logs/native-launch-*.log` in the client folder. To remove it, delete
`Wow64` and `FTA-Linux/` from the client folder.

| Distribution | Notes |
|---|---|
| Arch, CachyOS, Manjaro | Everything is in the repositories. Tested. NVIDIA: add `nvidia-utils`. |
| Debian 13, Ubuntu 25.04+ | Everything is in the repositories (`libsdl3-0`). Tested on Debian 13. |
| Ubuntu 22.04 / 24.04, Debian 12 | No SDL3 package: upgrade, or build SDL 3.2+ into `/usr/local` (steps in [tools/portable/README.md](tools/portable/README.md)). Tested on 24.04 up to `--check`. |
| Fedora | SDL3 from Fedora 42 (`SDL3`); NVIDIA via RPM Fusion. Package names not yet run. |
| openSUSE | Tumbleweed has `libSDL3-0`. Package names not yet run. |

Only Arch/CachyOS has been used to actually play. The other rows were checked in containers without a GPU, up
to the point where the libraries resolve and the engine starts.

### Settings

Native features are switched in the client's `WarcraftXL.cfg` (the same file the Windows client reads) or by
environment variable of the same name, which wins. Missing keys use the built-in default; the last column shows what the From The Ashes profile ships.

| Key | Effect | FTA profile |
|---|---|---|
| `WXL_MODERN_ASSETS` | Convert modern M2/BLP/WMO assets (and M3/DDS drop-ins) at load | on |
| `WXL_COLLECTION_ARMOR` | Collection armor models | on |
| `WXL_VISUAL_ENVIRONMENT`, `WXL_MODERN_WATER`, `WXL_GOD_RAYS`, `WXL_LOCAL_LIGHTS`, ... | Water, atmosphere and lighting renderer | on |
| `WXL_GRASS_MOTION` | Grass sway and parting around the player | on |
| `WXL_SURFACE_LIGHTS` (+ `_WMO`) | Torches and lamps light terrain, models and buildings | unset (off); set to `1` to enable |
| `WXL_LOOT_BEAM` | Loot beams over corpses | on |
| `WXL_GAMEOBJECT_EDITOR` | GM game-object editor overlay | on |
| `WXL_QUEST_RENDERING` | Quest markers and objective rings | off |
| `WXL_UNIT_OVERLAYS` | Target outline, facing chevrons, danger guides | off |
| `WXL_TREE_WIND` | Not ported (stays off) | off |

## What the client includes

Status: **Confirmed** = seen working in game by the maintainer. **Implemented** = built and covered by tests
or GPU probes, not yet accepted in game. **Off** = ported, disabled until its switch is set.

### Assets
| Module | Status |
|---|---|
| Modern M2 / BLP / WMO loading and conversion (skeleton merge, material and batch rebuild, 75-bone subdivision) | Implemented |
| M3 / M3A / M3CFG and DDS drop-in providers | Implemented; synthetic-tested only (no sample content exists yet) |

### Characters and equipment
| Module | Status |
|---|---|
| Collection armor and waist models | Implemented; partly confirmed (some armor still reported missing) |
| Item physics (belts, shields) | Implemented |
| Weapon sidecars: models, textures, offhand mirror | Confirmed (sidecars); mirror implemented |
| Back-sheath flip, reverse grip | Implemented |
| Additional-race creation, underwear, character customizer, Character Animation Studio | Confirmed (race creation); others implemented |
| Transmogrification preview support (sizing, preview camera, titles) | Implemented |

### World rendering
| Module | Status |
|---|---|
| Visual environment: FFT water, MSAA depth, atmosphere, god rays, local lights, interior transitions | Implemented |
| Distance fog and transparent fog | Confirmed |
| Grass motion and hardware-instanced grass | Confirmed |
| WMO layered materials (two-layer overlays, four-layer height blends) | Implemented |
| Surface lights (Living Azeroth): terrain, M2 and WMO lights | Implemented; off unless enabled |
| Loot beams | Confirmed |
| Extended view distance, object-distance scale | Implemented |

### Interface and gameplay helpers
| Module | Status |
|---|---|
| Game-object editor (GM) | Implemented |
| Selection circle and combat-readability settings bridge | Implemented |
| Quest markers, corpse and screen markers, objective rings | Off (implemented) |
| Unit outline, facing chevrons, danger cone | Off (implemented) |
| Required add-ons, NPCBot targeting, resurrection and enchant, Lone Wolf, Goblin Glider | Implemented |
| Skybox, visual-time and weather control bridges; WarcraftXL control panel API | Implemented |
| Cast-failure sound mute | Implemented |

### Deliberately not ported
Tree wind, colour grading and Forever Glow stay out by decision of the developer; `wxl-db2` has no consumer
on the native client; split-animation flattening is not implemented because no specification or sample exists.

## Building from source

Linux (native client):

```
nix-shell shell-native.nix            # compiler, CMake, SDL3, Vulkan headers
./fetch-dxvk.sh                       # builds the pinned dxvk-native into .deps/dxvk
DXVK_NATIVE_DIR=$PWD/.deps/dxvk/build-native-sdl3 ./build-native-cmake.sh Wow   # -> bin/Linux/Wow64
./launch-native.sh --check            # needs a game folder: see NATIVE-LAUNCH-AND-SHARING.md
tools/make-linux-portable.sh          # package the portable overlay
```

`tests/` holds sanitizer-built fixtures for the ported modules and `tools/` the offline shader builders and
GPU probes. How the launcher works, and its development-checkout assumptions, are in
[NATIVE-LAUNCH-AND-SHARING.md](NATIVE-LAUNCH-AND-SHARING.md).

Windows (the original wowdecomp target; Visual Studio 2022 with the C++ desktop workload, CMake 3.10+):

```powershell
cmake -S . -B build/x64 -G "Visual Studio 17 2022" -A x64
cmake --build build/x64 --config Release --target WowClient --parallel
```

The output is `bin/Win/WowClient64.exe` (with a crash logger) or `Wow64.exe`. Copy your 3.3.5a client's
`Data` folder into `bin/Win/` and point `Data/<locale>/realmlist.wtf` at your server. The Win32 build needs
NASM; the options are `WOWDECOMP_DISABLE_SIGNATURE_CHECKS`, `WOWCLIENT_CRASH_LOG` and
`WOWCLIENT_OUTPUT_OVERRIDE`. Cross-building is described in [CROSS-BUILD.md](CROSS-BUILD.md).

## Layout

| Path | Contents |
|---|---|
| `Engine/Source/` | Engine modules: Storm, Gx (Direct3D 9 / dxvk-native), Model2, Frame/FrameXML UI, SoundEngine, Lua, ... |
| `Engine/Source/ModernAssets/` | WarcraftXL asset and renderer ports (M3, DDS, physics, visuals, quest tracker, loot beam, ...) |
| `WoW/Source/`, `WoW/Common/` | Game modules: objects, world rendering, UI, networking, database, glue screens, and the `Linux/` WarcraftXL adapters |
| `ThirdParty/` | Libraries linked from their upstream sources |
| `tools/`, `tests/` | Shader pack builders, GPU probes, the portable overlay builder, sanitizer fixtures |

## Decompilation progress (wowdecomp, measured 2026-10-15)

About 90% of the original binary's functions (24,515 of 27,157) and 93% of its code bytes have a counterpart
in the source tree. This measures coverage of the original, not how much works; the untagged remainder is
mostly compiler-generated code and third-party libraries linked from upstream (FreeType, zlib, PCRE, parts of
FMOD Ex).

## Credits

- wowdecomp and its contributors, for the reconstruction this is built on
- WarcraftXL, whose modules are ported here (imported files keep their GPL-3.0-or-later headers)
- [binana](https://github.com/thunderbrewhq/binana), [thunderbrew](https://github.com/thunderbrewhq/thunderbrew), [whoa](https://github.com/whoahq/whoa)
- [dxvk](https://github.com/doitsujin/dxvk) and SDL3
- Everyone who reported bugs
- The WoW modding community
