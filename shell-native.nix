# Build wowdecomp as a NATIVE Linux ELF, reaching Vulkan through dxvk-native's
# D3D11 frontend (libdxvk_d3d11.so / libdxvk_dxgi.so).
#
# This is the sibling of shell.nix, which cross-compiles a Windows x64 PE with
# clang-cl + xwin. Both stay supported: the PE is what currently ships, the ELF
# is the port in progress.
#
#   nix-shell shell-native.nix
#   ./build-native-cmake.sh
#
# dxvk-native itself is built out-of-tree and is NOT built by this shell:
#
#   nix-shell -p meson ninja pkg-config gcc sdl3 vulkan-headers vulkan-loader glslang python3 \
#     --run 'meson setup build-native-sdl3 --buildtype release -Dnative_sdl3=enabled \
#            -Dnative_sdl2=disabled -Dnative_glfw=disabled && ninja -C build-native-sdl3'
#
# Point DXVK_NATIVE_DIR at that build directory (default
# ~/Projects/dxvk/build-native-sdl3).
{ pkgs ? import <nixpkgs> { } }:

pkgs.mkShell {
  name = "wowdecomp-linux-native";

  nativeBuildInputs = [
    pkgs.gcc
    pkgs.cmake
    pkgs.ninja
    pkgs.pkg-config
    pkgs.python3        # upstream's re/tools scripts
  ];

  buildInputs = [
    # Windowing and input, and dxvk-native's WSI talks to the same library --
    # one window system in the process, not two.
    #
    # SDL3, not SDL2. nixpkgs' "SDL2" attribute is sdl2-compat, a shim
    # forwarding to SDL3, so the SDL2 build was already running on SDL3 through
    # a translation layer. SDL3 replaced display indices with opaque
    # SDL_DisplayIDs, and the shim's index<->ID mapping is the leading suspect
    # for the client opening on the wrong monitor at the wrong resolution.
    pkgs.sdl3

    # SDL dlopens its video backend's libraries at runtime rather than linking
    # them, so a missing Wayland client library is not a link error -- SDL
    # just falls back to X11/XWayland without saying so. Under a tiling
    # Wayland compositor that is the difference between a real fullscreen
    # surface and an XWayland window that gets tiled, which is exactly the
    # symptom: SDL_SetWindowFullscreen returning success with an empty error
    # while SDL_GetWindowFlags still reported FULLSCREEN=0.
    pkgs.wayland
    pkgs.libxkbcommon
    pkgs.libdecor

    # Vulkan. Headers for compiling, loader because dxvk-native dlopens
    # libvulkan.so.1 at runtime; without it on the library path DXVK reports
    # "vkGetInstanceProcAddr not found" and then segfaults.
    pkgs.vulkan-headers
    pkgs.vulkan-loader

    # The engine vendors freetype, expat and pcre under ThirdParty/ and those
    # vendored copies are what both builds compile. The system ones are here so
    # a vendored copy that will not build natively can be swapped out with
    # -DWOWDECOMP_SYSTEM_FREETYPE=ON etc. rather than being patched.
    pkgs.freetype
    pkgs.expat

    # Ogg/Vorbis for BlizzardCore/OggDecompress.cpp. On Windows the codec comes
    # from the headers vendored in the FMOD Ex tree; natively the system
    # libraries are used instead, and the two disagree about
    # ov_callbacks::tell_func's return type.
    pkgs.libogg
    pkgs.libvorbis

    # The vendored FMOD Ex has a native ALSA output backend, so audio is a real
    # port rather than a stub. find_package(ALSA REQUIRED) in
    # Engine/Source/SoundEngine fails at configure time without this.
    # (Debian/Ubuntu: libasound2-dev.)
    pkgs.alsa-lib

    # PulseAudio for the native pulse output (fmod_output_pulse.cpp) -- FMOD's
    # raw-ALSA "default" PCM is silent under PipeWire, so the real output talks
    # libpulse directly (reaching pipewire-pulse). pkg-config libpulse-simple.
    # (Debian/Ubuntu: libpulse-dev.)
    pkgs.libpulseaudio

    # For BlizzardCore's URL::DownloadURL. Without it that function returns
    # false (Win64's own early-failure return), which costs HTTP patch
    # downloads and streaming telemetry but nothing on the critical path.
    pkgs.curl

    # Tools/XvidDecoder links this on Linux; ThirdParty/xvidcore ships only
    # headers and Windows import libraries.
    pkgs.xvidcore
  ];

  shellHook = ''
    export DXVK_NATIVE_DIR="''${DXVK_NATIVE_DIR:-$HOME/Projects/dxvk/build-native-sdl3}"
    # dxvk-native compiles in several WSI backends and refuses to guess
    # off-Windows (wsi_platform.cpp throws), so this has to be set for anything
    # that actually runs.
    export DXVK_WSI_DRIVER=SDL3
    export LD_LIBRARY_PATH="${pkgs.vulkan-loader}/lib:$DXVK_NATIVE_DIR/src/d3d11:$DXVK_NATIVE_DIR/src/dxgi:''${LD_LIBRARY_PATH:-}"
    echo "wowdecomp native Linux shell"
    echo "  cc:          $(${pkgs.gcc}/bin/gcc --version | head -1)"
    echo "  dxvk-native: $DXVK_NATIVE_DIR"
    if [ ! -f "$DXVK_NATIVE_DIR/src/d3d11/libdxvk_d3d11.so" ]; then
      echo "               MISSING -- build dxvk-native first (see the header of this file)"
    else
      echo "               libdxvk_d3d11.so present"
    fi
  '';
}
