# From The Ashes: native Linux client (portable overlay)

A native Linux build of the From The Ashes client (no Wine). It installs **into your existing From The
Ashes base client folder** and uses that folder's `Data`, `Interface`, `WTF` and `WarcraftXL.cfg`. It
contains no game data, and the Windows client keeps working.

## Install

```
./install.sh /path/to/From_The_Ashes_Client
/path/to/From_The_Ashes_Client/FTA-Linux/install-deps.sh --install   # only if something is missing
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

`FTA-Linux/install-deps.sh` reports what is missing and the exact install command for your distribution
(Arch/CachyOS, Debian/Ubuntu, Fedora, openSUSE); `FTA-Linux/install-deps.sh --install` runs it (asks first, uses
sudo; add `--yes` to skip the question). `install.sh` runs the same check at the end. `play.sh --check` verifies
the whole install.

## Distribution notes

SDL3 is the one dependency older releases lack. `install-deps.sh` handles the rest.

- **Arch, CachyOS, Manjaro:** everything is in the repositories. NVIDIA users add `nvidia-utils`.
- **Debian 13 and Ubuntu 25.04 or newer:** everything is in the repositories (`libsdl3-0`). Tested on
  Debian 13. NVIDIA users add the proprietary driver (`nvidia-driver`, or Ubuntu's driver tool).
- **Ubuntu 22.04 and 24.04 (and Debian 12):** no SDL3 package. Either upgrade, or build SDL 3.2 or newer
  into `/usr/local`, which the launcher searches. Tested on Ubuntu 24.04 (this gave SDL 3.2.31):

  ```
  sudo apt-get install build-essential cmake ninja-build git pkg-config libx11-dev libxext-dev libxcursor-dev \
      libxi-dev libxfixes-dev libxrandr-dev libxss-dev libxkbcommon-dev libwayland-dev wayland-protocols \
      libdrm-dev libgbm-dev libegl-dev libasound2-dev libpulse-dev libudev-dev libdecor-0-dev libvulkan-dev
  git clone --depth 1 --branch release-3.2.x https://github.com/libsdl-org/SDL.git
  cmake -S SDL -B SDL/build -G Ninja -DCMAKE_BUILD_TYPE=Release -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF
  cmake --build SDL/build && sudo cmake --install SDL/build
  FTA-Linux/install-deps.sh --install      # the rest
  ```

- **Fedora:** SDL3 is packaged from Fedora 42 (`SDL3`); on older releases build it as above. NVIDIA users
  add the driver from RPM Fusion (`akmod-nvidia`).
- **openSUSE:** Tumbleweed has `libSDL3-0`; Leap releases without it need the source build above.
- **Verification:** only Arch/CachyOS, Debian 13 and Ubuntu 24.04 (with the source-built SDL) were tested,
  and only up to `play.sh --check` on the last two (the Docker images have no GPU). The Fedora and openSUSE
  package names come from those distributions' package lists and have not been run. If a name is wrong,
  install the library `play.sh --check` reports by your usual route. The maintainer's own SDL3 is 3.4.x;
  3.2.x builds and loads but has not been played.

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
