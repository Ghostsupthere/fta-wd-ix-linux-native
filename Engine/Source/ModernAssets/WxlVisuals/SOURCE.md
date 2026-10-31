# Native visual-environment source

Adapted from the supplied WarcraftXL `scripts/wxl-visual-environment/vendor/coa-vfog`
(CoAVolFog: volumetric fog, god rays, local lights, modern water). GPL-3.0-or-later;
the supplied LICENSE is included here unchanged. The Windows loader, D3D9 device
wrapper, hooks, overlay, settings panel and grading/forever-look modules are **not**
imported: the native client already owns a D3D9Ex device (via dxvk-native), so the
imported renderers run directly against it.

## What is imported

- **Headers**: `config.h`, `engine_actors.h`, `engine_lights.h`, `fog_*.h`,
  `fullscreen_triangle.h`, `gpu_timing.h`, `log.h`, `msaa_depth.h`, `noise_volume.h`,
  `renderer.h`, `selection_policy.h`, `transparent_fog.h`, `water_*.h` are
  byte-identical to the supplied source. `engine.h` is adapted.
- **Sources**: most `.cpp` files differ from the supplied source only by 4–5 lines
  (the Windows include replaced with `NativePlatform.h`/`d3d9.h`). Larger rewrites:
  `log.cpp` (mutex-guarded file logging without Windows APIs), `msaa_depth.cpp`
  (NVAPI registration/StretchRect path removed; RESZ-only, see below) and
  `selection_policy.cpp` (the Windows client-config reader is removed; the
  selection helpers keep the supplied behavior and the native profile feeds them).
- **HLSL**: all `shaders/*` are byte-identical to the supplied `shaders/`.
- **Native-only files**: `native_config.*` (bounded, non-throwing INI reader),
  `native_lights.cpp` (point-light selection extracted from `engine_lights.cpp`),
  `native_support.cpp`, `NativePlatform.h` (QPC and exception shims).

## Depth copy

The upstream renderer reads scene depth through a RESZ resolve into an INTZ texture.
dxvk-native only advertises/handles RESZ for AMD adapters. `patches/dxvk-native-depth-copy.patch`
(applied by `fetch-dxvk.sh`, non-Windows builds only) makes the existing Vulkan
resolve available on every vendor. The native adapter additionally refuses to
activate any effect until a GPU readback test on private surfaces, using the actual
framebuffer's format and multisample type, has passed. Failure leaves stock
rendering untouched.

## Shaders

`compiled/*.h` is checked-in D3D9 shader-model-3 bytecode produced by a
checksum-pinned `d3dcompiler_47.dll` under an isolated offline Wine prefix.
`shader_manifest.json` records the compiler hash, every HLSL source hash and every
bytecode hash. `tools/build-native-visual-shaders.py --verify` checks them without
Wine and runs as the `VerifyNativeVisualShaders` build step; without `--verify`
it regenerates the headers. The native client never loads Wine or the compiler.

## Integration

`WoW/Source/WorldClient/Linux/WxlModernVisuals.cpp` replaces the Windows hooks and
fixed addresses with typed native state: camera and DayNight values, the native
M2 light grid (point lights), viewer map-object groups (interior state), FFX glow
cvars, liquid settings (water classification) and visible-unit enumeration (water
contacts). Hook points are `CGWorldFrame::OnWorldRender`, `Liquid::CRenderer::Flush`
and `World::UnloadMap`; graphics resource release is driven by
`NativeVisualHooks::deviceLost` from `CGxDeviceD3d9Ex::IReleaseD3dResources`.

## Splashes and ripples

The FTA profile always keeps the client's own splash and wake sprites (the supplied
selection policy forces `WaterClientSplashes` on), so no suppression hook is needed
or wanted; the modern ripple simulation is added to them. `native_contacts.cpp`
imports the supplied contact selection (plausibility, 48-yard range, nearest-first
ordering) and the native client feeds it typed visible units whose bodies cross a
liquid surface, using the world position for transported units.

## Not enabled

Per-transparent-draw fog fitting needs a separate native hook; `transparentFog` is
forced off.
