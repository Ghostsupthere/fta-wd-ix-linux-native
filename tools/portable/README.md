# From The Ashes: native Linux client (portable overlay)

A native Linux build of the From The Ashes client (no Wine). It installs **into your existing From The
Ashes base client folder** and uses that folder's `Data`, `Interface`, `WTF` and `WarcraftXL.cfg`. It
contains no game data, and the Windows client keeps working.

## Install

```
./install.sh /path/to/From_The_Ashes_Client
/path/to/From_The_Ashes_Client/FTA-Linux/play.sh --check
/path/to/From_The_Ashes_Client/FTA-Linux/play.sh
```

`install.sh` adds `Wow64`, a `FTA-Linux/` folder and the offline shader packs
(`Utils/WarcraftXL/visuals/*.pack`, kept if you already have them; `--force` replaces them). Remove the
overlay by deleting `Wow64` and `FTA-Linux/` from the client folder.

## What the system must provide

The overlay carries glibc, the C++ runtime, xvidcore and dxvk-native, so it does not depend on your
distribution's glibc. It needs these from the system:

- **SDL3** (`libSDL3.so.0`, SDL 3.2 or newer): Arch `sdl3`, Fedora `SDL3`, Debian 13 `libsdl3-0`
- **Vulkan driver and loader** (Mesa or the NVIDIA driver, `libvulkan.so.1`)
- **libcurl** (`libcurl.so.4`), **ALSA** (`libasound.so.2`), **PulseAudio client libraries**
  (`libpulse.so.0`, `libpulse-simple.so.0`; PipeWire's pulse package provides them)

`play.sh --check` lists anything that is missing.

## Notes

- Run it through `FTA-Linux/play.sh`. The executable finds its loader relative to the client folder, so it
  must be started from there (the launcher does this).
- Logs are written to `Logs/native-launch-*.log` in the client folder.
- Native-only features are switched in `WarcraftXL.cfg` (for example `WXL_SURFACE_LIGHTS`,
  `WXL_QUEST_RENDERING`, `WXL_UNIT_OVERLAYS`). Missing keys use the built-in defaults.
- The shader packs are built from the client's own shaders with `tools/build-light-shader-pack.py` and
  `tools/build-wmo-shader-pack.py` in the source tree; a base client without them runs with those features off.
- Status and known gaps: `WXL-NATIVE-PORT.md` in the source tree.
- Source: https://github.com/Ghostsupthere/fta-wd-ix-linux-native
- Rebuild this overlay from a built source tree with `tools/make-linux-portable.sh`.
