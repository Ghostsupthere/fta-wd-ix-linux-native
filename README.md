# From The Ashes: native Linux client (source code)

This is the **source code** of a native Linux client (no Wine) for **From The Ashes**
It is Linux x86-64 only and is not meant to be built or run on Windows.

| I want to... | Go to |
|---|---|
| **Play it on Linux** | the installer: **[Releases](https://github.com/Ghostsupthere/fta-linux-client/releases/latest)** (one-click installer, install guide and distribution notes in the [fta-linux-client](https://github.com/Ghostsupthere/fta-linux-client) README) |
| **Read or build the code** | this repository; start at "Building from source" below |
| **Check what works** | [docs/WXL-NATIVE-PORT.md](docs/WXL-NATIVE-PORT.md), the feature-by-feature status ledger |

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

## Building from source

Linux x86-64 only. You need [Nix](https://nixos.org/download/) (it provides the compiler, CMake, SDL3 and the Vulkan
headers through `shell-native.nix`), Git, and a From The Ashes client folder to run against.
