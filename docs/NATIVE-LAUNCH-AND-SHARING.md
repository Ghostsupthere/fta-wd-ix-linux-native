# Native Linux launcher and sharing

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

## Sharing the client

The client is shared through its own repository, **fta-linux-client**: its installer puts the client into a
recipient's own From The Ashes folder, and `tools/make-linux-portable.sh` here builds its binary payload (the
executable with its own glibc loader, C++ runtime, xvidcore and dxvk-native, using relative paths, no symlinks and no
game data). A plain copy of this development checkout is not shareable: `bin/Linux` links to the original client's
asset folders and the executable is linked against specific Nix store paths.

## Patched shader packs (WMO layered materials, grass motion and instanced grass)

Modern WMO layered materials use `Utils/WarcraftXL/visuals/wmo_shaders.pack`, grass motion uses
`Utils/WarcraftXL/visuals/grass_shaders.pack` and instanced grass uses
`Utils/WarcraftXL/visuals/grass_instanced.pack`, all generated
per install by `tools/build-wmo-shader-pack.py` (needs Wine, mingw and the pinned
`d3dcompiler_47.dll`; see `Engine/Source/ModernAssets/WxlVisuals/SOURCE.md`). It is derived
from the client's own shader files, so it is not committed or redistributed. A shared
folder without it still runs; those WMOs draw stock, and grass draws the stock way without the instanced pack (`WXL_GRASS_INSTANCING=0` also turns it off). Regenerate it if the client's shader
files change.
