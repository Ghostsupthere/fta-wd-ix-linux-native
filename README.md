# From The Ashes: native Linux client (source code)

This is the **source code** of a native Linux client (no Wine) for **From The Ashes**, the World of Warcraft 3.3.5a
(build 12340) server. It is Linux x86-64 only and is not meant to be built or run on Windows.

| I want to... | Go to |
|---|---|
| **Play it on Linux** | the installer: **[Releases](https://github.com/Ghostsupthere/fta-linux-client/releases/latest)** (one-click installer, install guide and distribution notes in the [fta-linux-client](https://github.com/Ghostsupthere/fta-linux-client) README) |
| **Read or build the code** | this repository; start at "Building from source" below |
| **Check what works** | [docs/WXL-NATIVE-PORT.md](docs/WXL-NATIVE-PORT.md), the feature-by-feature status ledger |

It is the **From The Ashes** client extensions (WarcraftXL) ported in-process: modern models and textures, collection
armor, the improved world renderer, and the extra UI tools the server uses. It is built on
wowdecomp, a C++ reconstruction of the original client in which every
function keeps the address it came from (`// OFFSET:` tags) so it can be compared against the original. This
repository adds the Linux platform layer and the WarcraftXL ports. The game renders through
[dxvk-native](https://github.com/doitsujin/dxvk) (Direct3D 9 on Vulkan) with SDL3 for the window, input and audio.

Join the Discord for other clients and to show off what you build: https://discord.gg/DsZMGUD8aT

> **Status.** The client builds, links, logs in and runs in the game world, and its maintainer plays on it. It is not at
> proven parity with the Windows client: each feature below is marked as confirmed in game, implemented but not yet
> checked in game, or off by default. Only Arch/CachyOS has been used to play; other distributions were checked in
> containers without a GPU.

## Playing it

You do not build anything to play. Download `FTA-Linux-Installer.run` from the
**[Releases page](https://github.com/Ghostsupthere/fta-linux-client/releases/latest)** and double-click it. It finds your
From The Ashes client folder (the one with `Wow.exe`), installs the Linux client into it, installs any missing system
libraries and adds a menu shortcut. The Windows client is not touched and no game data is included. You need a From The
Ashes client folder and a Linux x86-64 machine with a Vulkan GPU. Manual steps, per-distribution notes (Arch, Debian,
Ubuntu, Fedora, openSUSE) and troubleshooting are in the
[fta-linux-client README](https://github.com/Ghostsupthere/fta-linux-client#readme).

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
| `WXL_PARTICLE_THREADS` | Run particle emitter updates on the model worker threads (needs the `M2UseThreads` option); `0` updates them one by one on the main thread, as the original does | on |
| `WXL_PARTICLE_BUILD_THREADS` | Build particle vertices ahead of drawing on the model worker threads; `0` builds them during each draw call, as the original does | on |

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
| Nameplate API (`C_NamePlate`, `nameplateN` unit tokens, `NAME_PLATE_*` events) for DBM, Questie, Immersion, ElvUI | Implemented; not yet exercised in a world. Beyond the Windows client |
| Frame profiler (`GetProfilerZones()`, `WXL_PROFILE_LOG`): zone times, draw calls, triangles | Implemented; works on the real client |

### Deliberately not ported
Tree wind, colour grading and Forever Glow stay out by decision of the developer; `wxl-db2` has no consumer
on the native client; split-animation flattening is not implemented because no specification or sample exists.

## Building from source

Linux x86-64 only. You need [Nix](https://nixos.org/download/) (it provides the compiler, CMake, SDL3 and the Vulkan
headers through `shell-native.nix`), Git, and a From The Ashes client folder to run against.

```
nix-shell shell-native.nix            # compiler, CMake, SDL3, Vulkan headers
./fetch-dxvk.sh                       # builds the pinned dxvk-native into .deps/dxvk
DXVK_NATIVE_DIR=$PWD/.deps/dxvk/build-native-sdl3 ./build-native-cmake.sh Wow   # -> bin/Linux/Wow64
./launch-native.sh --check            # needs a game folder: see docs/NATIVE-LAUNCH-AND-SHARING.md
tools/make-linux-portable.sh          # gather the payload and build the release files (needs the fta-linux-client checkout)
```

`tests/` holds sanitizer-built fixtures for the ported modules and `tools/` the offline shader builders and GPU probes.
How the launcher works, and its development-checkout assumptions, are in
[docs/NATIVE-LAUNCH-AND-SHARING.md](docs/NATIVE-LAUNCH-AND-SHARING.md). Release files are published from the
[fta-linux-client](https://github.com/Ghostsupthere/fta-linux-client) repository.

## Layout

| Path | Contents |
|---|---|
| `Engine/Source/` | Engine modules: Storm, Gx (Direct3D 9 / dxvk-native), Model2, Frame/FrameXML UI, SoundEngine, Lua, ... |
| `Engine/Source/ModernAssets/` | WarcraftXL asset and renderer ports (M3, DDS, physics, visuals, quest tracker, loot beam, ...) |
| `WoW/Source/`, `WoW/Common/` | Game modules: objects, world rendering, UI, networking, database, glue screens, and the `Linux/` WarcraftXL adapters |
| `ThirdParty/` | Libraries linked from their upstream sources |
| `tools/`, `tests/` | Shader pack builders, GPU probes, the payload builder for the portable client, sanitizer fixtures |

## Decompilation progress (wowdecomp, measured 2026-10-15)

About 90% of the original binary's functions (24,515 of 27,157) and 93% of its code bytes have a counterpart
in the source tree. This measures coverage of the original, not how much works; the untagged remainder is
mostly compiler-generated code and third-party libraries linked from upstream (FreeType, zlib, PCRE, parts of
FMOD Ex).

## Credits

- **[From The Ashes](https://discord.gg/DsZMGUD8aT)** is the server this client is made for.
- **wowdecomp**, the C++ reconstruction of the 3.3.5a client this is built on, and the forks of it.
- [binana](https://github.com/thunderbrewhq/binana), [thunderbrew](https://github.com/thunderbrewhq/thunderbrew) and
  [whoa](https://github.com/whoahq/whoa), which wowdecomp credits as well
- [dxvk](https://github.com/doitsujin/dxvk) and SDL3, which the Linux client renders and runs through
- Everyone who reported bugs
- WarcraftXL, whose modules are ported here (imported files keep their GPL-3.0-or-later headers)
- **The World of Warcraft community**: the modders, server developers, tool authors and players whose years of work
  and documentation make projects like this possible. Thank you.

## License

Different parts of this tree have different licenses, and none has been changed: the WarcraftXL ports are
GPL-3.0-or-later, the bundled libraries keep their own licenses, and the wowdecomp reconstruction is credited as
published by its authors, who declare no license. See [LICENSING.md](LICENSING.md). The installer repository
(`fta-linux-client`) is GPL-3.0-or-later.

This project is free to use, share and build on. **No Blizzard game assets or game data are included** in this
repository or in the release files: no MPQ archives, models, textures, sounds, databases or interface files. You need
your own From The Ashes client folder, and the game data in it remains Blizzard's. The one kind of derived file the
releases carry is the optional offline shader packs (`Utils/WarcraftXL/visuals/*.pack`): they are generated from shader
code in a client's own files with WarcraftXL's patches, and without them the features that use them (surface lights,
layered WMO materials, grass motion) stay off.
