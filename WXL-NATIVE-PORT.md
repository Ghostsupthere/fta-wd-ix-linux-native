# WarcraftXL native-port inventory

This is the implementation tracker for replacing client behaviors that the Windows
WarcraftXL runtime supplied. It is not a claim of visual parity until each line has
an in-game test against the same FTA content.

## Overall status and next steps (2026-10-31)

**The port remains incomplete.** The Linux `Wow` target compiles and links with
the current changes. Asset conversion, armor attachment/material handling and
equipment physics now have substantial native implementations and focused
automated coverage. Several enabled renderer and presentation features still
have no native implementation. No overall parity percentage is assigned: a
compiled converter, a working GPU pass and an accepted in-game scenario are
different completion gates.

| Workstream | Current implementation | Remaining work |
| --- | --- | --- |
| M2/BLP assets | Native conversion, skeleton merge, single AFM2 unwrap, eligible v264 repairs, material/batch rebuild, packed wide index offsets and 75-bone subdivision | Split-animation rebasing; remaining format limits and real rendered acceptance |
| Collection armor and waist models | Slot filtering/ownership, Icon2 and sidecar policies, texture-readiness queues, attachment-53 buckles, owner/world replay, cached and post-worker palette copying | Remaining source-policy cases, remaining missing armor and full transition/retry acceptance; reported wisps/Human Female deformation are resolved |
| Item physics | PHYS companions and PFDC/PFID associations, skeleton binding, native-pose restoration, post-worker simulation and model lifecycle cleanup | Rendered motion, live lifecycle and frame-cost acceptance |
| WMO assets and materials | Conversion, scoped visibility handling, immutable layered texture/UV/weight snapshots, and GPU layered-material draws (two-layer overlays, four-layer height blends) through offline-patched native shaders | In-game acceptance on a layered WMO, frame cost; WMO modern emissive/parallax and the unified-render lighting extras are not implemented |
| Native environment/renderer | Distance/profile controls and scoped fog-range handling; imported water (FFT, ripples, contacts), atmosphere, god-ray and local-light renderers drawing through the native D3D9Ex device with vendor-independent depth copy and 56 verified precompiled shaders | In-game acceptance and frame cost; transparent fog is implemented but unseen in game (opt-in); grading/postprocess modules are not imported |
| Other extensions | Sound filtering, skybox/time/weather/control bridges, native local/preview/remote weapon rotation/reverse grip and loot-beam world draws | Weapon orientation and loot-beam rendered/transition acceptance; grass motion, editor features and remaining modules audited below |

The latest complete native build passed. ASan/UBSan fixtures cover skin rebuilding
and rejection, collection texture queues and placement policies, production
palette copying/scene traversal, physics core/native-adapter behavior, and WMO
visual-data preservation/lifetime accounting. Sanitizer runs used disabled leak
detection in this sandbox; they are not leak-check results. Installed shield and
waist PHYS samples also passed binding and finite solver-step checks. These checks
do not establish live GPU behavior, visual parity or teleport/logout correctness.

### Native grass motion (2026-10-31)

The profile enables the original's grass motion: a directional two-wave wind sway
plus blades parting around the player, anchored at the blade base. The Windows
renderer assembled a patched copy of the engine's grass vertex shader at runtime. Here
the same injected block runs through the offline pipeline used for WMO shaders:

- `tools/build-wmo-shader-pack.py` also extracts the three live (group 0) ground-effect
  vertex shaders and writes `Utils/WarcraftXL/visuals/grass_shaders.pack`. It is derived
  from game files, so it is generated per install and not committed.
- When the engine loads a shader, `CGxShader::Load` offers the bytecode to the pack by
  content hash and substitutes the patched version. The dormant point-light group is not
  patched. With no pack, or `WXL_GRASS_MOTION=0`, grass is stock.
- `CDetailDoodad::SetupVertexShader` fills the free constant registers c14..c22 (the
  dormant point-light block) once per frame with camera, player position, wave vectors
  and phases; `FillVertices` gives rigid props (model names ending in Roc, Bon, Stn, rock,
  stone, bone, skull and similar, plus four Badlands aliases) an interior vertex alpha the
  shader reads as "do not move". The control addon's
  `WarcraftXL_Set/GetHellfireGrassSwayDisabled` functions are registered natively.

**Instanced grass (2026-10-31).** Port of the supplied `wxl-living-azeroth` grass instancing
(Morfium's Living Azeroth, GPL-3.0-or-later; clustered surface lights and the in-game editor are not
ported). The engine bakes every visible ground-effect batch into stream buffers and, once more than
about 128 batches are visible (long grass distances), re-bakes the rest on the CPU every frame.
`CDetailDoodadInstBatch::Draw` now asks `WxlGrassInstancingPlan()` first: each model's static
vertices (position, uv) and one 64-byte record per plant (three transform rows with the scale folded
in, the normal and the colour from the engine's own `BakeColor`) are built once per batch and drawn
with D3D9 hardware instancing, one draw per model. The engine's vertex shader (stock or the
motion-patched one) is derived offline (`MakeInstancedGrassText`: the baked position comes from the
rows, the rest runs unchanged) into `grass_instanced.pack`. The engine still performs its normal
`Draw` with three-vertex placeholder buffers so its state flush runs; `NativeVisualHooks::instancedDraw`
in `CGxDeviceD3d9Ex::Draw` swaps in the derived shader, declaration and streams around the instanced
draws and restores them. Any missing piece (pack, shader, model, device error) takes the stock bake.
`WXL_GRASS_INSTANCING=0` turns it off; `Logs/GrassInstancing.log` reports batches, instances and
fallbacks; stderr announces `instanced grass active`. The plant math is shared with the bake
(`BakeColor`, `BuildInstanceMatrix`); the GPU probe renders baked and instanced blades with all three
live permutations, with and without motion, repulsion and rigid alphas, and the pixels are identical
(rotated, scaled and moved plants: 0 of 200 lit pixels differ). The user reports the grass looks fine in game (2026-10-31); the log showed 186 cached batches, no device failures, one fallback. No FPS gain is
established (the original's reported gains are not measurements of this client).

**Verification.** `tests/wxl_grass_rigid_test.cpp` (ASan/UBSan: classification, including
look-alikes such as DurIbu/Bonus, and the patch anchors) and a real-GPU probe
(`tools/wxl_grass_motion_gpu_probe.cpp`) pass. On the real backend all three live
permutations, drawn with the engine's own grass pixel shader, are pixel-identical to stock
with motion off; wind moves the blade tip (about 6 px of 64) while the base stays fixed;
blades on either side of a player lean away symmetrically; rigid alphas 1 and 254 do not
sway while endpoint alpha still does. The native build passes. **Not yet seen in game:**
sway amplitude and speed in real zones, repulsion around the player, rigid props, shadow
tiers and frame cost. The first live run logs `grass motion active` in the launch log.
The original's grass instancing/performance integrations (`wxl-living-azeroth`) are not
part of this port.

### Environment page backend (2026-10-31)

The control add-on's Environment page calls `WarcraftXL_EnvironmentAvailable`, `Get/SetEnvironmentOption`,
`Save/ResetEnvironmentOptions`, `GetEnvironmentStatus`, `ResetEnvironmentTiming` and
`VerifyGrassPlacement`; none existed natively, so the page showed "need the matching WarcraftXL runtime".
`WxlEnvironmentOptions` (`WoW/Source/WorldClient/Linux/`, pure logic in
`Engine/Source/ModernAssets/WxlGrass/EnvironmentOptions.hpp`, test `tests/wxl_environment_options_test.cpp`)
ports the supplied `Options.cpp`: the same 17 keys, ranges and defaults, clamping, and the same
`WTF/WXL/Environment.ini` text (this install's file, written by the Windows client, parses to all 17 values).
Saves go through a temporary file and a rename.

- **Live:** `grass.instanced` (`WxlGrassInstancingSetEnabled`; off drops the cached buffers and draws stock),
  `grass.windStrength` / `windSpeed` / `interaction` (`WxlGrassSetMotion`, the supplied `compat::SetMotion`
  scaling: amplitudes by strength, zero strength turns wind off, speed by speed, interaction on/off).
- **Grass cache and rebuild budget (live):** `grass.memoryMB` caps the instance data the cached batches may hold
  (the least recently used batches make room; a batch bigger than the whole cache is refused and draws stock) and
  `grass.buildMs` is the soft per-frame build budget (a build may start while the frame's spent time is under the
  budget; later batches draw stock that frame and are built on a later one). Pure policy in
  `WxlGrass/GrassBudget.hpp` (tests in `tests/wxl_environment_options_test.cpp`); the page's status line shows cache
  MB, batches dropped for room, builds deferred and batches too large. Not seen in game.
- The `lights.*` options drive the surface lights below (off by default, see that section); with the profile gate off
  they are saved and change nothing, and the page's status text says so.
- **Native default differs from Windows:** `lights.enabled` and `lights.models` default to 0 here (Windows: 1), so
  "Restore environment defaults" cannot switch surface lights on.
- **Performance sample:** the "Start 30-second performance sample" button records frame times and reports
  median / 95th / 99th / worst. "Check grass placement" does not compare anything natively: the status says it
  was not run, because instanced and stock grass share the engine's placement math.
- **Note:** this install's `Environment.ini` has `lights.enabled=0`, `lights.models=0`, `lights.occlusion=0` and
  `lights.indoorIsolation=0` (written 2026-10-09 by the Windows client), so surface lights are off in this
  profile regardless of the module's defaults.

Verified: the ASan/UBSan test (table, clamping, hostile input, round trip, percentiles) passes and `Wow` builds.
**Not seen in game.**

### Surface lights, Living Azeroth (2026-10-31)

Port of the lighting half of the supplied `wxl-living-azeroth` (Morfium's Living Azeroth, GPL-3.0-or-later): the
client's own torches, braziers and lamps light the terrain, models and buildings around them. **It is off by
default and inert unless you turn it on.**

**Switches (all must be on):**
1. `WXL_SURFACE_LIGHTS=1` in `WarcraftXL.cfg` or the environment (default **0**). With it off nothing about the
   client's shaders or lighting changes at all.
2. `Utils/WarcraftXL/visuals/light_shaders.pack`, built per install by `tools/build-light-shader-pack.py`
   (Wine and the pinned compiler, offline; the client never loads either). Derived from game assets, never committed.
3. The Environment page's "Dynamic lights" (`lights.enabled`, default **0** natively). "Light characters and
   buildings" (`lights.models`) adds M2s and WMOs; without it only the terrain is lit.

With 1 and 2 on but 3 off, the patched shaders load and stay inert (the header texture is all zero, the terrain
loop ends at once and the stock terrain point lights stay on).

**How it works.** The supplied module patches the client's terrain, M2 and WMO shaders at run time with the
Microsoft assembler, which the native client cannot do, so the supplied text patches
(`WxlSurfaceLights/ModelLightShaderText.hpp`, `TerrainLightShaderText.hpp`: assembly and edit rules imported
unchanged, only the wrapper differs) run offline, like the WMO and grass packs. On this install the builder
patched **all 128 terrain vertex and 292 terrain pixel permutations, 315 lit M2 vertex shaders and 99 WMO vertex
shaders (834 in all), with 0 conflicts**; the 315 M2 shaders it skipped have no sun term. The runtime
(`WxlSurfaceLights.cpp`) swaps them in by hash of the engine's own bytecode as `CGxShader::Load` creates them,
through the new `NativeShaderReplace` chain (grass motion now registers there too). Per frame, at the top of
`CWorldScene::RenderChunks`, it scans the M2 scene, builds the light list and the cell grid, fills one
A32B32G32R32F texture (vertex sampler 0 for models, pixel sampler 13 for the terrain) and sets VS c250 / PS
c206 / c212. The light list is the supplied one: merged torch groups, flicker, range fade, up to 512 lights, a
grid of cells listing the lights that reach each, and baked occlusion rays (`World::Intersect`, terrain and
WMO faces only) under a per-frame ray budget. `DoodadLightProperties/Assignment.cdbc` (7 lights, 24 assignments
here, 0 rejected) add lights to models by path. Model pointers kept between 4 Hz scans are guarded by a new
`NativeVisualHooks::modelDestroyed` call in `~CM2Model`.

**WMO lights and the layered-material pack.** The WMO layered module finds its shaders by hashing the engine's
current bytecode, which light-patching changes. `tools/build-wmo-shader-pack.py` therefore adds, when
`light_shaders.pack` sits beside its output, a layered variant of each light-patched WMO vertex shader keyed by
the light-patched bytecode (build order: `build-light-shader-pack.py`, then `build-wmo-shader-pack.py`). On this
install all 99 got one (198 WMO vertex shaders, 238 pixel shaders, all accepted by the real backend). At run time
WMO lights are **on** when there is no layered pack or it carries those variants, otherwise off;
`WXL_SURFACE_LIGHTS_WMO=1/0` forces it either way. The layered draws then also light, because their shader is
built from the light-patched one. Rendering of lit layered WMOs is not seen.

**In use on this install (2026-10-31):** the user ran it, turned on "Dynamic lights" and "Light characters and
buildings", saw the lighting change and judged it good, and chose to keep it enabled. `WarcraftXL.cfg` now has
`WXL_SURFACE_LIGHTS=1` and `WTF/WXL/Environment.ini` has `lights.enabled=1`, `lights.models=1`. The code defaults
stay off. That test ran with WMO lights off (the old layered pack was installed), so it covered the terrain and M2
doodads such as torches and lamp posts. The combined WMO pack was installed afterwards, which turns WMO (building)
lights on by default; that part is not yet user-tested (`WXL_SURFACE_LIGHTS_WMO=0` turns it off).

**Not ported:** the supplied in-game table editor (already removed in the supplied integration), the debug views'
panel and the runtime-assembler shader patcher (replaced by the offline pack).

**Verification (no in-game test yet).**
- `tests/wxl_surface_lights_test.cpp` (ASan/UBSan): WDBC parsing and every rejection path, the real client's
  tables, flicker, merging, pool selection, the cell grid, baked occlusion against a synthetic wall, the
  shader pack format, and the texture layout (entries point at their lights, tiles, header, constants).
- Offline build on this install: 834 shaders patched, 0 failures (above).
- `tools/wxl_surface_lights_gpu_probe.cpp` on the real dxvk-native backend: **all 834 patched shaders are
  accepted** (terrain 128 vertex / 292 pixel, M2 315, WMO 99) and the A32B32G32R32F managed texture is created.
- `Wow` builds and links; `launch-native.sh --check` passes.
- **Not verified:** that the patched shaders render the same as stock when inert, and how the lights look. Both
  need a run: `WXL_SURFACE_LIGHTS=1 ./launch-native.sh` (add `WXL_NATIVE_DIAGNOSTICS=1` for the load lines), look
  at the terrain with "Dynamic lights" off (should be unchanged), then on near torches or lamps.

### Native WMO layered materials (2026-10-31)

Converted modern WMO roots carry layered-material snapshots (texture paths, up to
four UV sets, per-vertex weights, tint). Draws now use them. The Windows renderer
patched the engine's WMO shader bytecode at runtime with the Microsoft shader
assembler; the native client cannot, so the same text patches
(`WmoShaderText.hpp`, imported unchanged) run **offline**:

- `tools/build-wmo-shader-pack.py` reads the 96 pixel and 540 vertex MapObj
  permutations from the client's own archives, patches and assembles them through
  the checksum-pinned compiler under an isolated Wine prefix, and writes
  `Utils/WarcraftXL/visuals/wmo_shaders.pack` (231 patched shaders on this install).
  The pack is derived from game assets, so it is generated per install and **not
  committed**. The client never loads Wine; without the pack, or with
  `WXL_MODERN_WMO_MATERIALS=0`, WMOs draw stock and `Logs/WmoVisuals.log` says so.
- The runtime looks shaders up by a hash of the engine's own bytecode, so any
  permutation not in the pack draws stock. The 22 vertex permutations at the highest
  shadow tier exceeded the 11 vertex output registers with the supplied layout (as in the
  original). `WmoShaderCompact.hpp` (native addition; the imported header is untouched)
  packs the extra eight floats into unused components of the stock texcoord outputs, takes
  over the COLOR1 register when that is not enough and adds at most one register. Pixel
  shaders that read COLOR1 get no compact variant (that pairing draws stock). The pack now
  patches all 99 vertex permutations (2 compact layouts, keys 0x80/0x81).
- Hooks: `CMapObj::ExteriorRender`/`InteriorRender` open a leaf scope, `CullBatch`
  latches the batch's material, `CShaderEffect::SetShaders` latches the selected
  shader pair, and `CGxDeviceD3d9Ex::Draw` hands the indexed draw to the module. It
  swaps shaders, stream 1 (extra UVs and weights), declaration, the tint constant and
  sampler addressing only around that draw, then restores them and the engine texture
  slots. Missing data, streaming textures, a failed state snapshot or any mismatch
  take the stock draw. Caches are bounded and expire after 300 frames; world leave,
  device loss and resize release them.

**Verification.** `tests/wxl_wmo_shader_pack_test.cpp` (ASan/UBSan: format, lookup
keys, fallbacks, rejection of damaged files) and the existing visual-data test with
the new facade pass. `tools/wxl_wmo_materials_gpu_probe.cpp` runs the engine's own
MapObjDiffuse permutation and its patches on the real dxvk-native backend: all 231
patched shaders are accepted, and the draws give the expected colours for
two-layer overlays (transparent/opaque/half), four-layer one-hot weights, the
remainder layer, equal blending, height-based blending, and enabled/disabled tint.
The native build passes. **Not seen in game, by decision (2026-10-31):** a Darnassus walk
showed stock WMO rendering unaffected (536,000 group renders, all stock: Darnassus has no
converted modern roots). Content that exercises the path is scarce: the standalone Night Elf
inn (21 two-layer materials) and one root in the Well of Eternity instance (3 two-layer
materials). The user chose not to test those, so this path stays recorded as implemented and
probe-verified, not game-accepted. The inn root is `world\wmo\expansion09\nightelf\10ne_nightelf_inn01.wmo`
(archive listings are incomplete, so there may be others). Appearance on a real WMO,
interior/exterior transitions, shadow tiers, frame cost and resize/teleport
cleanup are open; `Logs/WmoVisuals.log` records the pack load, the first layered
draw and periodic draw/fallback counters.

### Native loot beams (2026-10-31)

**Northshire retest exposed a camera-space bug, now corrected.** Live logs
confirm valid server hints and successful draw submission, but zero in-frustum
vertices. `CSimpleCamera::SetGxProjectionAndView` builds a view with eye at zero;
the native world uses camera-relative positions. Procedural beams kept world
positions, so the adapter now composes the camera offset into that view. The
GPU regression uses Northshire-sized coordinates: the old/unrebased path draws
zero pixels; the corrected path draws 960 coloured pixels, and zero when occluded.
Core sanitizer tests cover the same clip-space regression. The user confirms
loot beams now work in game (2026-10-31); server hint/eligibility policy is unchanged.

**Black Art of War / Slam proc overlays also have a concrete correction.** Cheese
uses `0xFFFFFFFF` and bit shifts/masks for white RGB. The native bit library's
out-of-range floating-point-to-signed-int casts produced `(0,0,0)` in the exact
addon expression. Finite bit operands now normalize to 32-bit words with defined
unsigned shifts and preserved result conventions. The production Lua regression
reproduces old black RGB and passes with white `(255,255,255)` after the fix,
including word wrap, signed/arithmetic results, shift-count masks and remainder
edges under sanitizers. No addon files or textures were changed. Native build
passes; the user confirms Art of War and Fury Slam/Blood Surge graphics now
work in game (2026-10-31).

Automatic `Logs/LootBeam.log` and capped `Logs/TextureFailures.log` remain available
for the next run. These diagnostics do not establish acceptance by themselves.

Loot shafts and drifting motes now have native D3D9Ex world-pass integration.
The imported policy requires a dead non-player unit, its per-viewer lootable flag
and the server's remaining-loot quality hint (1..9). A zero hint never creates a
beam, and quality changes replace the tracked colour. Typed native object fields
replace Windows offsets. Scans visit at most 4,096 resident objects, select the
nearest 32 within 150 yards at most every 100ms, and generate at most 11,904
vertices per frame. Tracking stores GUIDs/positions only; map unload clears it.
Brief scene interruptions retain the last valid camera for at most 200ms.

The draw uses the current native D3D projection and viewport depth range before
FFX/world-depth resolve. Matching depth is mandatory. The imported renderer
captures/restores all touched state, transforms, viewport and stream 0, draws
additively with depth testing and no depth writes, and skips failed state capture
or incompatible depth surfaces. There are no persistent GPU resources to recreate
on device reset. `WXL_LOOT_BEAM` and the disable marker are respected.

ASan/UBSan fixtures pass for the production geometry/tracking and imported device
renderer: nearest selection, bounded finite geometry, stable fades across full-set
replacement, quality downgrade/zero hints, failed scans, camera expiry/clock reset,
world cleanup, successful/failed draws, setter/capture/getter failures, complete
state/stream restoration, COM references and depth mismatch rejection. LeakSanitizer
is disabled in this sandbox; these are not leak-check results. Native Release
build passes. **Rendered appearance/occlusion, live server hints, device reset and
world transition/performance acceptance remain open.** No server, asset or
network configuration was changed.

### Native modern water and visual environment (2026-10-31)

The upstream CoAVolFog renderers (see `Engine/Source/ModernAssets/WxlVisuals/SOURCE.md`)
now run in-process against the native D3D9Ex device; no Windows DLL, D3D wrapper or
fixed address is used. `WxlModernVisuals.cpp` supplies the inputs from typed native
state: camera and DayNight values, the native M2 point-light grid, viewer map-object
groups (interior blend), FFX glow cvars, liquid settings (water class) and visible
units (water contacts). Hooks: `CGWorldFrame::OnWorldRender` (fog scope, camera/light
capture after opaque M2, atmosphere and god rays after the draw lists),
`Liquid::CRenderer::Flush` (water begin/tag/end) and `World::UnloadMap`. Resource
release is driven from `IReleaseD3dResources`, so resize, device loss and shutdown
free every default-pool object. Profile switches (`WXL_MODERN_WATER`,
`WXL_GOD_RAYS`, `WXL_LOCAL_LIGHTS`, ...) select effects; `WXL_VISUAL_ENVIRONMENT=0`
turns the whole companion off. Underwater views stay stock.

**Depth backend limitation, resolved natively.** The upstream renderer reads scene
depth via a RESZ resolve into INTZ. dxvk-native only exposed that for AMD adapters.
`patches/dxvk-native-depth-copy.patch`, applied by `fetch-dxvk.sh` for non-Windows
builds only, enables the existing Vulkan resolve for all vendors. Effects stay
disabled unless a GPU readback test on private surfaces matching the real
framebuffer's format and multisample type passes; failure falls back to stock.

**Shaders.** The 56 checked-in D3D9 bytecode headers are verified at build time
(`tools/build-native-visual-shaders.py --verify`, a `VerifyNativeVisualShaders`
step) against `shader_manifest.json`; regeneration uses a pinned offline Wine
compiler and is never needed at runtime.

**Verification.** `tools/wxl_modern_visuals_gpu_probe.cpp` runs on the real
dxvk-native D3D9 backend (real GPU): the depth readback, water and atmosphere at all
three quality levels, god rays, a point-light selection, stencil handling, a
profile/underwater skip, an aborted water pass and a device resize. It passes with
no MSAA and with 4x MSAA. `tests/wxl_visual_environment_test.cpp` passes under
ASan/UBSan: bounded INI parsing, non-finite rejection, selective and rays-only
profiles, and bounded point-light selection. The native `Wow` build passes. These
are synthetic scenes, not game frames.

**Splashes and ripples.** The FTA profile keeps the client's own splash and wake
sprites (the supplied policy forces them on), so nothing is suppressed; modern ripples
are added. Visible units whose bodies cross a liquid surface become contacts through
the supplied selection (plausibility, range, nearest-first). The GPU probe wades a
unit through water: the contact is tracked, 149 simulation steps run and about 10,000
water pixels differ from the same frame without ripples. The user confirms ripples and wake
work in game (2026-10-31); the live log shows contacts=1 with 4 of about 67 scanned
units in liquid. `Logs/VisualEnvironment.log` reports contacts, units scanned and
units in liquid.

**Transparent fog (2026-10-31).** Native hook for the supplied `TransparentFog` mode, off
unless the profile sets `WXL_TRANSPARENT_FOG=1` **and** `WXL_DISTANCE_FOG=1` (the policy
needs the stock fog replaced; the FTA `WarcraftXL.cfg` currently has both at 0, so test with
`WXL_DISTANCE_FOG=1 WXL_TRANSPARENT_FOG=1 ./launch-native.sh`). Above water only,
`CGWorldFrame::OnWorldRender` calls `WxlBeginTransparentFog()` right after
`RenderLiquidPass()`: it draws the glare first (`WxlGlareDrawn()` then skips the frame's own
glare call), composites the fog with `FogPass::BeforeTransparents`, and fits a linear stock
fog (`FitStockFog`). From then on `CM2SceneRender::SetupLighting` hands each batch's fog
range, exponent and colour to `NativeVisualHooks::m2BatchFog` (set only while armed), which
applies `ApplyStockFogFit` (additive, modulate and modulate-2x batches keep black, white and
grey). `WxlFinishVisualWorld()` then draws only the late god rays. Under water, with the
stock fog not replaced, with a failed readiness check or a failed early pass, the frame
falls back to the whole-frame fog. M2 pass 2 (drawn before the liquid when above water) and
WMO fog are not re-fitted, as in the supplied hooks. The probe adds an early-composite
scenario on the real GPU (fit usable, rewrites exponent and lighting colour, composite
and fitted transmittance both fall with distance and agree within 0.2: 0.59/0.28/0.16
composite vs 0.74/0.41/0.12 fitted at 20/60/95 yd in the thick synthetic fog, which is the
limit of one straight line); the whole-frame pass leaves no fit. The user reports it appears to work in game (2026-10-31) but with a noticeable performance cost: `fog gpu` median 6.48 ms at 3432x1362, 4x MSAA with the distance fog on, against 1.87 ms in an earlier session without it. A synthetic benchmark at that size shows the early composite costs the same as the whole-frame pass and did not reproduce 6.5 ms, so the cost was not attributed. A later A/B showed the FPS gap (about 170 against 250) gone, probably first-run shader cache warm-up (unproven); the live CPU cost is about 0.1 ms per frame, logged as `native fog cpu` in `VisualEnvironment.log`. Transparent fog appears to work in game; the FTA `WarcraftXL.cfg` now enables it with the distance fog (user request, 2026-10-31), and the code default stays opt-in.

**Not done / unverified.** Nothing else here has been seen in game: appearance, shoreline/waterline transitions,
interior blending, local-light placement, god-ray occlusion, frame cost and
teleport/logout cleanup are open. `Logs/VisualEnvironment.log` records the depth
readiness result and periodic water/atmosphere state for the first live run.

### Underwear and gossip fixes (2026-10-31)

**Gossip options (bug in the native client, not the add-on).** `Script_GetGossipOptions`
(`GossipInfo_Scripts.cpp`) ended its loop at the address of `CGGossipInfo::m_gossipText`,
assuming it follows `m_options` as it did in the Windows image. In the Linux build the linker
places `m_gossipText` (0x10d0200) *before* `m_options` (0x10d0a00), so the loop ran zero times:
`GetGossipOptions()` returned nothing for every NPC, for Immersion and the stock gossip frame
alike, while quests (a separate array) still appeared. It now loops over the 32-entry array, and a
server-defined gossip type outside the 22-entry name table reads as "gossip" instead of indexing
past it. `Wow` builds. Not yet confirmed in game. The reported "new quests not very consistent"
may be related or may be separate; recheck after this fix.

**wxl-underwear.** `WoW/Source/Ui/Linux/WxlUnderwear.{h,cpp}` provides `WXL_UnderwearSet /
Reset / Refresh / Ready` (the Transmogrification add-on only enables its panel when
`WXL_UnderwearReady()` is true, which is why it did nothing). The DLL's three detours became hooks
inside `CCharacterComponent`: `GetSectionsRecord` (underwear lookups return a per-character copy of
the record with the chosen top/bottom textures; the shared DBC row is untouched),
`CreateRequestTL` (torso-lower band added before equipment layers) and `RenderPrepTL` (band pasted
only where no equipment covers the torso-lower section). Set/Refresh call `SetSkinColor` and
`RenderPrep` like the DLL, except for stock NPC/full-skin components. Entry validation and texture
paths are `Engine/Source/ModernAssets/WxlUnderwear/UnderwearPaths.hpp`
(`tests/wxl_underwear_paths_test.cpp`, ASan/UBSan). The textures (`WXL_UW_*`, `WXL_RM_*`,
`WXL_CT_*`) exist in `patch-ZB.mpq`. `WXL_UNDERWEAR=0` disables it. Not verified in game.

### Character customizer: native implementation (2026-10-31)

`WoW/Source/Glue/Linux/WxlCharacterCustomizer.{h,cpp}` provides the `WXL_CharacterCreate*` and
`WXL_EyeHalo*` Lua functions the deployed GlueXML uses: `GetState` (9 returns), `GetOptions`,
`SetPart`, `SetSkin`, `SetArmorHidden`, druid preview (`DruidPreviewReady`, `SetDruidForm`; the model
path must be `Creature\DinkleDruid\...m2`) and eye halo (`EyeHaloReady`, `SetEyeHalo`; themes
none/Fel/WhiteArcane/Sunfire/Twilight/Sanguine, races 10/23/25, attachment 11) plus the
account-scoped created-character choice (`SetAccount`, `SameName`, `StageCreatedChoice`,
`CommitCreatedChoice`, `PendingCreatedChoice`, `ConfirmCreatedChoice`, persisted atomically in
`WTF/WarcraftXL-EyeHalo.ini` beside the executable). The Windows module detoured five client
functions; here the creation code calls the hooks directly: `CCharacterCreation::SetCharCustomizeModel`
(`WxlCreationBackgroundScope` releases the previews before the background changes),
`SetCharacterFacing`, `InitCharacterComponents`, `Dress` (hidden-armor preview and the Tuskarr cape
geoset fix via `ITEMSLOT_CAPE`) and `CCharacterComponent::FreeComponent` (preview teardown).
Extended-race emotes (race >= 22) use `ExtendedTextEmoteSoundKit` in `SoundInterface2Emotes.cpp`.
Portable rules are in `Engine/Source/ModernAssets/WxlCustomizer/` with ASan/UBSan tests
`tests/wxl_customizer_{skin_face,emote_sound,eye_halo}_test.cpp`. The Windows module hard-codes the
cape slot as 9 while the reconstructed enum has `ITEMSLOT_CAPE` = 10; the named enum is used. `Wow`
builds and links. **Not seen in game** (creation UI, druid form, eye halo, emotes).

### Character Animation Studio: native implementation (2026-10-31)

`WoW/Source/Ui/Linux/WxlAnimationLab.{h,cpp}` provides the 23 `WarcraftXL_AnimationLab*` Lua functions
used by the control add-on's `/wxlanim` window (API version 6, capability mask 511, the same
contract as the DLL), the rule tables (race/gender substitutions, exact combat pairs, five spell
phases, maintained leases) and the Levitate / Wraith Walk presentation (Hover body animation plus the
`Monk2HLIdle` right-shoulder hold on key bone 3). The DLL's two core events and attack detour became:

- `CGUnit_C::PlayBaseAnimation` owns a `WxlUnitAnimationScope` (outer request id, flags, a
  per-request token); its destructor emits the completion event before the scope is popped, which
  applies the queued sheathe/draw weapon action exactly once.
- `CM2Model::SetBoneSequence` calls `NativeVisualHooks::m2SetBoneSequence` first; the callee may
  replace the sequence id, and only requests made under an outer unit animation on that unit's own
  body model are rewritten (previews, UI models and the mount model pass through).
- `CGUnit_C::VerifyAttackIconPressed` calls `WxlAnimationLabBeforeAttackCheck`, which cancels a local
  Wraith Walk channel through `Spell_C_StopCasting` before the native attack checks.
- Per-frame expiry from `CGWorldFrame` and world teardown from `World::UnloadMap`.

The DLL reads M2 headers and unit memory defensively and parses DBC files; the port uses the typed
`CM2Model` / `M2Data` / `CGUnit_C` structures and the already-loaded `g_spellDB`,
`g_spellVisualDB`, `g_spellVisualKitDB`, `g_spellDurationDB` and `g_animationDataDB` instead. The
playable-model classifier, idle-ready and protected-motion tables and the quadratic-probe sequence
lookup are in `Engine/Source/ModernAssets/WxlAnimationLab/AnimationLabRules.hpp`
(`tests/wxl_animation_lab_rules_test.cpp`, ASan/UBSan, passes). `WXL_ANIMATION_LAB=0` disables it
(the add-on then reports the API as unavailable). The module's own Wraith Walk bytes-verified
detour and Windows `tests/verify_wraith_shoulder.py` fixture are not reproduced. `Wow` builds and
links; the 23 Lua names are present in the binary. **Not seen in game.** Things to check first:
`/wxlanim` opens and Diagnostics shows the active model; Preview/Play a browser row; a mapped
spell phase or a race/gender substitution (e.g. 126 -> 824); Wraith Walk shoulder hold; attacking
out of a Wraith Walk channel.

### Gear recolor: nothing to port (2026-10-31)

`wxl-gear-recolor` (checked against `scripts/wxl-gear-recolor`): the native dye/recolor pipeline
was removed from the module upstream; what remains is the add-on's preview-camera and model
framing: `WXL_SetModelPreviewCamera` and `WXL_ResetModelPreviewCameras` already exist in the port (`WxlModelPresentation`). No new
native code was added; only in-game confirmation of the add-on window is outstanding.

### Selection circle and combat-readability bridge (2026-10-31)

**wxl-selection-circle.** The target selection circle is oriented along the unit's own forward
direction instead of toward the camera. The Windows module detours `CGObject_C::BuildSelectionRotMatrix`
(0x744150); the reconstructed function is a static with no object, so `WxlSelectionCircleApply`
(`WoW/Source/Object/ObjectClient/Linux/WxlSelectionCircle.{h,cpp}`) is called right after it in
`CGUnit_C::RenderTargetSelection` and `RenderPetTargetSelection`. The facing is the model's
world-matrix forward row (the DLL's `VisualFacing`), falling back to `GetRawFacing`; the matrix keeps
the stock texture convention (`pi - facing`). Pure maths in
`Engine/Source/ModernAssets/WxlSelectionCircle/SelectionCircleMatrix.hpp`
(`tests/wxl_selection_circle_test.cpp`, ASan/UBSan). Differences from the DLL: corpse circles stay
stock (as in the DLL), and the ground move/attack cursor (`RenderAutoTrackCursor`) also stays
stock, where the DLL's hook would have turned it to the player's facing as a side effect of
`this` being in ECX; the per-frame refresh the DLL delegates to the quest-ring module is
unnecessary because the matrix is built right before the draw. `WXL_SELECTION_CIRCLE=0` disables it.

**wxl-combat-readability.** `WoW/Source/Ui/Linux/WxlCombatReadability.{h,cpp}` provides
`WarcraftXL_ReadabilityAvailable / GetReadabilityOption / SetReadabilityOption /
ResetReadabilityOptions` over the settings store in
`Engine/Source/ModernAssets/WxlCombatReadability/ReadabilitySettings.hpp` (keys, ranges and defaults
as the DLL; `tests/wxl_readability_settings_test.cpp`). It only stores values. Everything it feeds
(outlines, the frontal guide cone, danger rings) is drawn by `wxl-unit-outline`, which
`WXL_UNIT_OVERLAYS=0` disables. That renderer is now ported (see "Quest tracker, objective rings and unit
outline" below); with the profile switch at 0 the add-on's frontal-guide option still has no visible effect,
exactly as on a Windows client with overlays off. `WXL_COMBAT_READABILITY=0` hides the
bridge. `WxlReadabilitySnapshot()` is there for a future outline renderer.

**wxl-selcircle-poc** (quest-objective rings) is ported 2026-11-01, off by default: see the next section.

`Wow` builds and links. **Not seen in game.** To check: target a creature and turn it with the
mouse/by walking around it; the front marker of the ring should follow the creature's facing, not
rotate with the camera.

### Quest tracker, objective rings and unit outline (2026-11-01)

The developer confirmed `wxl-quest-marker`, `wxl-selcircle-poc` and `wxl-unit-outline` were only config-disabled, so
they are ported. Both gates stay as the Windows modules had them and are **0 in this profile**, so nothing changes
until `WXL_QUEST_RENDERING=1` and/or `WXL_UNIT_OVERLAYS=1` is set in `WarcraftXL.cfg` (or the environment). The
WarcraftXLControl add-on's Tracker tab and Readability page drive them; before this, its tracker natives did not
exist, so the tab reported that the runtime was missing.

**Quest markers and objective rings (`WXL_QUEST_RENDERING`).**
- Lua natives, registered only when the gate is on: `WarcraftXL_TrackerSetMarker / ClearMarkers / GetMarkerInfo /
  SetMarkerAlpha / SetSettings / SetCorpseMarker / ClearCorpseMarker / IsCorpseActive / GetCorpseInfo / SetPlayerDead /
  GetPlayerPosition / GetMapId / SetDiamond` and `SetObjectiveEntries / ClearObjectiveEntries / SetRingSettings`
  (`WoW/Source/WorldClient/Linux/WxlQuestTracker.cpp`). Validation, the 25-marker and 16-entry caps, projection and
  billboard maths are the engine-independent `Engine/Source/ModernAssets/WxlQuestTracker/TrackerState.hpp`.
- **Marker pass.** The supplied quest billboard, opaque corpse stone (alpha-tested) and screen-space diamond are drawn
  from `interface\navigation\ingamenavigationui.blp` (uncompressed BGRA 64x64 in Patch-I here; the supplied loader is
  ported as `BlpTexture.hpp`, with its generated fallback icon) by `MarkerRenderer.hpp` through the native D3D9Ex
  device inside the world pass, beside the loot beams: depth-tested against the world depth with the viewport's depth
  range, reversed-depth aware, far plane extended for standard depth, full device-state capture and restore. The Lua
  label queries project against the last drawn frame's world camera (`WxlWorldCamera`: the engine view with the eye
  translation composed back in, as the loot beams do), so labels trail the icon by at most a frame.
- **Objective rings.** `CGUnit_C::Render` (which the engine runs once per visible unit per frame, the tick the Windows
  module used through `RenderAutoTrackCursor`) calls `WxlObjectiveRingDraw`: a unit whose creature entry is in the
  selected quest's set, alive, not a player, not the locked target, within 60 yards, at most 24 a frame, gets a ring from
  the engine's own selection-circle path (`SetCircleRenderStates`, `ProjectTex::Render2d`, the facing-aligned matrix)
  using `textures\questmarker\unitselecttexture-quest.blp` (Patch-5), yellow at 40% brightness with the user's ring
  alpha. The texture swap rides the engine's `RsPush/RsPop`, so the manual save/restore of the Windows module is not
  needed. The Windows hooks on `ProjectTex2d`, `GetObjectByGuid` and the visible-object enumerator are not needed
  either: the per-unit tick replaces the GUID cache.
- **Verification.** `tests/wxl_quest_tracker_test.cpp` (ASan/UBSan: markers, caps, validation, objective entries,
  rejected calls keeping old entries, colour, projection, billboard, far plane) passes.
  `tools/wxl_quest_tracker_gpu_probe.cpp` runs the shipped `MarkerRenderer.hpp` on the real dxvk-native backend with the
  real extracted atlas: marker centred and about 32 px at 50 and 90 yards, hidden at alpha 0, behind the camera, when
  occluded and for a dead player, corpse opaque at alpha 0, diamond drawn through the depth buffer at its NDC
  position, state restored (a repeat draw is pixel-identical). **Not verified:** the Lua natives against a running game
  and server, the ring draw (it needs the engine), and anything in world space with a real camera.

**Unit outline (`WXL_UNIT_OVERLAYS`).** The supplied `Outline.cpp`, `Outline.hpp`, `Hlsl.hpp` and
`ImmediateSilhouette.hpp` are copied into `WoW/Source/WorldClient/Linux/WxlUnitOutline/` with three documented edits:
the footprint helper reads the model through `m2::FootprintInputs` instead of raw offsets, the multisampled mask is
resolved before the edge pass samples it, and the file-scope self-registration is replaced by an explicit
`WxlUnitOutlineInit` (static initialisation order is not safe with the profile reader). The WarcraftXL core APIs it
uses are provided by sibling headers with the same include names (`events/`, `game/gx|world|unit|camera|m2|combat/`,
`gpu/`, `common/`, `core/`) and `NativeFacade.cpp` / `GxFacade.cpp`; `__try/__except` become `try/catch` (the native
bindings validate before dereferencing instead).
- **What it draws.** A reaction-coloured silhouette outline on the target and mouseover (cast / boss contrast and
  pulse from the readability settings), the terrain-conforming facing chevrons (ground or overlay, four shapes), and
  the frontal-cone and ring danger guide. Options come from the control bridge (`WxlGetControlSettings`, in the
  supplied 200-byte layout) and the readability bridge.
- **Event sources.** The Windows core's detours become native hooks in `NativeVisualHooks`: `m2BatchDrawn` after the
  engine's single-model M2 indexed draw (`CM2SceneRender::DrawBatch` sets `currentM2Model`; instanced batches are not
  reported, as no single model owns them), `outlineOverlay` just before the editor overlay at `ScenePresent`, and
  `outlineDeviceLost`; world leave comes from `World::UnloadMap`.
- **Shaders.** The five small pixel shaders cannot be compiled at run time here, so `tools/build-outline-shaders.py`
  extracts the HLSL from `Hlsl.hpp`, compiles it offline with the same pinned compiler and Wine prefix as the other
  visual shaders, and writes `outline_shaders.h`; the runtime finds bytecode by a hash of the exact source text
  (`--verify` checks the table against `Hlsl.hpp` without Wine).
- **Multisampling.** The swap chain is multisampled when the player selects it, and so is its depth buffer, which the
  player-silhouette stamp shares. `EnsureBackbufferTarget` therefore creates the mask as a multisampled render target
  with the back buffer's sample type, plus a plain texture it is resolved into before sampling.
- **Verification.** `tools/wxl_unit_outline_gpu_probe.cpp` links the shipped `Outline.cpp` and `GxFacade.cpp` with a
  mock unit, camera and flat ground behind the facade and checks real pixels on dxvk-native, plain and 4x
  multisampled: red outline around a hostile target (and none with the outline off, no target, or the unit alone),
  ground and overlay chevrons, danger cone and ring, and a player target through the shared depth buffer. The
  engine-side glue (`NativeFacade.cpp`: object, model and world lookups; the batch, overlay and device-loss hooks)
  builds but has no automated test. **Not seen in game:** whether the batch hook reports the right model for every
  attached item, outline quality on real models and skinned geometry, chevron placement on real terrain, and frame
  cost (the hook runs on every single-model M2 batch while the module is on, but returns at once when there is no
  target).

### Required add-ons, NPCBot targeting, NPCBot enchant, Lone Wolf, Goblin Glider (2026-10-31)

Ported from the supplied modules; all build and link, their pure logic has ASan/UBSan tests, **none is
seen in game**. Each has a `WXL_<NAME>=0` profile switch.

- **wxl-required-addons** (`WxlRequiredAddons.{h,cpp}`): the module's Lua policy script, unchanged, runs
  in the GlueXML context right after GlueXML loads (so character select enforces it before the world
  loads) and in the in-world context after FrameXML/add-ons load (ACP). Add-ons whose TOC has
  `## X-WarcraftXL-Required: 1` (ten or more in the FTA client: ACP, AIO_Client, Transmogrification,
  NPCBotPaperdoll, ...) are force-enabled and their checkboxes locked. `WXL_REQUIRED_ADDONS=0` disables.
  The stock GlueXML context has no `GetAddOnMetadata`, so the policy could not read the tag at
  character select (first in-game test: tagged add-on stayed unchecked and unlocked); the glue context
  now gets a `GetAddOnMetadata` over the same parsed catalog before the policy runs.
- **wxl-npcbot-targets** and **wxl-npcbot-resurrection** (`WxlNpcBotTargets.{h,cpp}`, rules in
  `Engine/Source/ModernAssets/WxlNpcBot/NpcBotTargetRules.hpp`, `tests/wxl_npcbot_targets_test.cpp`): a
  server-grouped creature (HighGuid::Unit, in the native party/raid roster, the other side being the
  active player) passes the party/raid membership checks (`CGUnit_C::IsInMyPartyOrRaid` 0x718CA0 and
  `IsInMyParty` 0x718D70 - the reconstructed names are the reverse of what the README says, the
  addresses and roster callees are what matter), and a dead grouped bot is a valid target of the 36
  class resurrection spell IDs (range check view, click and hover paths). `WXL_NPCBOT_TARGETS=0` /
  `WXL_NPCBOT_RESURRECTION=0` disable.
- **wxl-npcbot-enchant** (`WxlNpcBotEnchant.{h,cpp}`): `WarcraftXL_GetPendingItemEnchant` and
  `WarcraftXL_CancelPendingItemEnchant` for the NPCBot Paperdoll add-on, on the typed
  `PendingSpellCast`; the cancel calls `Spell_C_CancelSpell` directly because `SpellStopTargeting`
  rejects tainted callers. The server half (`.botenchant`) is not part of this client port.
- **wxl-lone-wolf** (`WxlLoneWolf.{h,cpp}`, `LoneWolfIntent.hpp`, `tests/wxl_lone_wolf_intent_test.cpp`):
  with aura 601666, ranged hunter spells (family 9, damage class 3) get a zero minimum range
  (`Spell_C_GetMinMaxRange`, `SpellRec::GetMinRange`), Auto Shot no longer switches to melee at
  point-blank (`CGUnit_C::IsInMeleeRange`), and an accepted ranged shot hands back to Auto Shot. The
  handoff is the module's own state machine; its seven register-ABI gateways became observers in
  `SendPendingCast`, `OnSpellGo` (normal vs early finish), `ReleasePendingSpellCast` and the two
  auto-repeat setters. The aura watcher is registered as a game script (below). Needs the matching
  server `Spell::CheckRange` change.
- **wxl-goblin-glider** (`WxlGoblinGlider.{h,cpp}`, `GlidePolicy.hpp` with the module's 61,505-check
  policy test plus new velocity/visual checks, `tests/wxl_goblin_glider_test.cpp`): `CMovement_C::ApplyMovement`
  runs a `WxlGliderStepScope` around the stock step (steering rebase + heading, terminal-speed
  override), `CMovementShared::CalcFallDistance` takes the tuned terminal through the module's exact
  curve transform, and the glider attachment (attachment 19, `Creature\DinkleGlider\DinkleGlider*.m2`)
  plays Open then Opened through `NativeVisualHooks::gliderUpdate/gliderForget` called from
  `CM2Model::Animate/AttachToParent`. The `WXLGLIDE` self-whisper handshake needs the server
  `mod-goblin-glider` (present in `AzerothCore_Windows/modules`). Native stack-copy of the movement for
  the tuned velocity became `ScaledFeatherVelocity`, the safe-fall branch of the same accessor.
- **Game scripts** (`WxlGameScripts.{h,cpp}`): the Windows core runs each module's Lua watcher once in
  every new in-world Lua state; here `WxlRegisterGameScript` stores it and `WxlRunGameScripts` runs it
  after the in-world UI has loaded.

**Not ported:** `wxl-db2` publishes the `wxl.db2` v1 C API for out-of-core extension DLLs and decodes
WDC tables through the x64 asset host; nothing in the supplied tree consumes it and the native client
has neither the host nor an extension loader, so there is nothing to attach it to. `wxl-living-azeroth`'s
grass instancing, Environment page backend and surface lights are ported (the lights off by default; see
"Surface lights").

### Game-object editor: native implementation (2026-10-31)

`WoW/Source/Ui/Linux/WxlGameObjectEditor.{h,cpp}` ports `wxl-gameobject-editor` v1.4.0: a
server-authorized translate/rotate gizmo for one existing GameObject, driven by the
FTA control add-on through the nine `WarcraftXL_GOEditor*` Lua functions (Version, Sync,
Clear, SetTool, SetSpace, SetAxis, SetSnap, Transform, Poll; same contract, coalesced
`begin`/`update`/`end` polling, exact 16-digit runtime GUID). It does not pick doodads, edit
placements, persist anything or scale; the server stays authoritative. Enabled by default
(`WXL_GAMEOBJECT_EDITOR=0` disables it).

Differences from the Windows module, all in the platform layer:
- Dear ImGui 1.92.9 WIP, its DX9 backend and ImGuizmo (with WarcraftXL's rotation hit-area
  patch) are vendored under `ThirdParty/imgui` (target `wxl_imgui`, MIT; see its SOURCE.md).
  The Win32 backend is replaced by mouse events fed from SDL.
- Overlay: `NativeVisualHooks::overlayDraw` is called from `CGxDeviceD3d9Ex::ScenePresent` after
  the world and UI are flushed and before `EndScene` (the software cursor is drawn after it);
  `overlayDeviceLost` runs with the existing device-loss hook. The hooks are installed on the
  first valid `Sync`, and with nothing selected the overlay does no work.
- Input: `NativeVisualHooks::mouseInput` is called from `Os/Linux/Input.cpp` with the engine's
  pixel coordinates for moves, button down/up and focus loss. Moves are never swallowed; a
  left click on the hovered handle, and its release, are swallowed so WoW does not also act on it.
- Camera: `CWorldScene::viewMatrix` / `projMatrix` (the same globals the DLL reads).
- Object access: the selected object is resolved through `ClntObjMgrObjectPtr` and moved
  through the reconstructed `CGGameObject_C` fields instead of fixed addresses. M2 objects get the
  matrix, the model transform, the world bounds and `CMap::ObjectUpdate`, mirroring
  `UpdateWorldObject` without recomputing the matrix from the authoritative position; types that
  own a map-object scene (`CGGameObject_C_Type_WorldObj` family) go through
  `World::ObjectUpdate`. The portable preview lifecycle (scale preservation, restore, model reload,
  GUID/pointer reuse) is `Engine/Source/ModernAssets/WxlGameObject/GameObjectPreviewState.hpp`,
  unchanged apart from a C++17 `operator==`.
- World leave: `World::UnloadMap` clears the selection and restores the local model.

Verified outside the game: `tests/wxl_gameobject_preview_test.cpp` (upstream fixture, ASan/UBSan);
`tools/wxl_gameobject_gizmo_gpu_probe.cpp` on real dxvk-native D3D9 with a D3D row-vector camera
(gizmo draws; hover and a mouse drag along the X handle move the matrix along +X only; an isolated
Z ring turns the matrix about Z only; depth and cull state are restored); `Wow` builds and links.
Build the probe with the same include flags as the other probes plus `-IThirdParty/imgui` and the
six `ThirdParty/imgui` sources, run with `DXVK_WSI_DRIVER=SDL3`.
**Not verified:** anything in the real client. The editor needs a GM account, the matching
protocol-4 server module and the control add-on; select an object, drag a handle, then Apply/Cancel
and check the server accepts the placement. Untested in particular: M2 versus WMO/scene-doodad
objects, transports, the click-swallow behavior against WoW's own mouse handling, and the overlay
under MSAA/resolution changes.

### Weapon sidecars: native implementation (2026-10-31)

`WxlWeaponSidecar.{h,cpp}` (WoW/Common/ComponentCore) parses `WXLWeaponModels.csv`
and `WXLWeaponTextures.csv` (archive root, `DBFilesClient`, loose
`Data/*.mpq/DBFilesClient`; first row per display wins) and `Resolve()` returns what
the upstream `WeaponExtension` bakes: the model, the geoset filter (geoset 0 is
always kept; sections not listed are hidden) and the column-0 texture replacements
keyed on the M2 texture type. The DLL points the hand-item record at a baked virtual
file; the native client applies the same decision directly in `AddHandItem` /
`AddLink` (`CharacterComponent.cpp`). Rules: mainhand and ranged use the row only
when it changes something (a texture row or a geoset filter); the offhand prefers
`Model1OffhandPath` (with `Geoset1Offhand`), then the mirrored mainhand model, then
the mainhand result (shields). A missing sidecar model falls back to the stock model;
a missing sidecar texture is skipped; a stock texture that is missing no longer drops
the weapon when the sidecar supplies the object skin. Column 1 (Model2) is parsed but
unused, as upstream never applies it to hand items.

Verified: `tests/wxl_weapon_sidecar_test.cpp` (upstream sample layout, ragged rows,
first-seen dedup, all four offhand cases, malformed tables) passes; `Wow` builds.
**In game:** the user reports it "appears to work" (2026-10-31; which weapons, sides and tables they used was not recorded, so mainhand/offhand/shield and geoset-filter cases are not individually confirmed). The FTA client ships no weapon sidecar tables, so nothing changes without them. `WXLExtendedEquipment.ini` `[Memory] MaxWeaponCacheMB`
and `[EagerPreload]` have no native equivalent because nothing is baked.

**Offhand Y-mirror (2026-10-31).** `NoAutoMirror` is opt-in, as upstream: a row whose column is true gets its offhand
model (`Model1OffhandPath`, else the mainhand `Model1Path`) mirrored across Y. The Windows module wrote mirrored `.m2` and
`.skin` bytes into a virtual file (`VirtualPath.cpp`: `MirrorModelBytes`, `MirrorSkinWinding`); there are no virtual files
here, so `AddLink` asks for a separate cache entry (`CM2Scene::CreateModel` with `WxlModelMirror::kCreateFlag`) and the
loader mirrors that entry's private copy in memory (`Engine/Source/Model2/WxlModelMirror.h`):

- After `M2Init`, before the model is initialised: vertex positions and normals, bone pivots, translation and rotation
  keys (rotation negates x and z; the engine's unsigned 16-bit encoding makes that `65535 - v`), attachment, event,
  light, ribbon and particle positions, the particle wind vector, model/sequence/collision bounds and collision
  positions and normals. Scale tracks stay (unsigned magnitudes), as upstream. Emitter latitude/longitude and the
  collision triangle winding are not mirrored (upstream does not either; the collision mesh is unused for held items).
- After the skin loads and is rebuilt, before it initialises: every triangle's winding reverses (a mirror flips facing)
  and the section centres mirror. A triangle list that is not whole triangles is left alone and logged.
- Key arrays of sequences streamed in later from `.anim` files are mirrored as they become resident, once per shared
  buffer (aliased sequences share one). Upstream mirrored only keys inside the `.m2`; a non-resident sequence's offsets
  point into `.anim` data, so this port skips those until they load rather than trusting a bounds check.
- The unmirrored model keeps its own cache entry (the hash is salted and the mirror flag must match), so a mainhand and
  a mirrored offhand of the same weapon do not share data. The mirror applies only when the sidecar names a model.

A missing sidecar model now really falls back to the stock model: `CreateModel` substitutes an error cube for a missing
file, so `AddLink` checks that the archive has the file first (the earlier code relied on `CreateModel` returning null).

Verified: `tests/wxl_model_mirror_test.cpp` (ASan/UBSan: vertices, normals, pivots, shared and non-resident key arrays,
global-sequence tracks, quaternion decoding, bounds, attachments, involution, late-loaded sequences and alias
deduplication, skin winding, partial triangle lists, empty models) passes, and `Wow` builds. **Not seen in game:** the
FTA client ships no weapon sidecar tables, so there is no mirrored weapon to look at until a table sets the column.
Whether the winding, normals and attachment point look right on a real mirrored sword needs that run
(`WXL_NATIVE_DIAGNOSTICS=1` logs `weapon-sidecar: offhand mirrored <path>`).

### M3 and DDS drop-ins (2026-11-01)

Ported although no sample exists in this client: the project is live and the main developer may add M3 or DDS
content. Everything is inert unless matching input is present, and this is **synthetic-tested only**.

**DDS.** `Dds.cpp` (the supplied DXT1/3/5 DDS to BLP2 transcode) is imported unchanged under
`Engine/Source/ModernAssets/WxlBlp/textures/dds/`. `SFile::OpenEx`, `FileExists` and `FileExistsEx` now fall back to a
`.dds` of the same name when a `.blp` is missing, as the Windows asset host did; `TransformModernBlp` recognises the DDS
bytes under the `.blp` name and transcodes them (same 2048 edge ceiling as BLP2, payload copied verbatim, no luminance
alpha). Only a *missing* `.blp` is affected, so no existing texture changes.

**M3.** The supplied reader and builder (`Md34`, `M3Model`, `M2Build`, `Options`, `Math.hpp`) are imported unchanged
under `Engine/Source/ModernAssets/WxlM3/`; `ModernM3.cpp` replaces the Windows `M3Host.cpp`. A loose folder named
`patch-ZB.mpq` or `patch-ZA.mpq` (any case) under the `Data` directory next to the executable (the same place the weapon
sidecar tables are read from), ZB first, may hold `<dir>/<base>.m3` with optional `<base>.m3a` (animation library) and
`<base>.m3cfg` (options). Opening `<dir>/<base>.m2` then serves a model converted on first use, `<base>00.skin` its
skin, and a `.blp` the transcoded `.dds` beside it; the model is cached for the session. As on Windows, the drop-in wins
over an archive file of the same name. `WXL_MODERN_ASSETS=0` turns it off. The bytes are served to the engine as an
in-memory file from `SFile::OpenEx`, so the existing M2 path then loads them like any native model.

- **Conversion runs in a forked worker.** The shared MD34 reader trusts the offsets and counts in the file. Mutating a
  valid model's bytes under ASan produced out-of-bounds reads in the reader and in `BuildSkin` for roughly one file in
  seven (reads only, no writes), and some mutations make it loop. The worker has a 20 s CPU and wall deadline and an
  address-space limit of the client's current size plus 2 GiB; a crash or timeout there only fails that conversion, which
  is remembered so the file is not retried. The parent never parses an untrusted `.m3`.
- **Native differences from the host:** case-insensitive lookup (the host ran on a case-insensitive file system); a
  256 MiB cap on every source read; the `m3a` option must name a file inside the drop-in folder; no absolute paths or
  `..` in served names.
- **Layout limit on this install:** the real `patch-ZB.mpq` and `Patch-ZA.mpq` here are archive *files*, so a folder of the
  same name cannot be created next to them. The drop-in folders are reachable only on an install where those names are
  directories, which is the same constraint the Windows provider had. **Decision (user, 2026-11-01): keep the Windows
  layout for parity**, since the developer may have intended it; revisit later if that changes.

**Verification.** `tests/wxl_dds_test.cpp` (ASan/UBSan): DXT1/3/5 headers and alpha fields, mip levels, short files,
zero mip count, oversize, damaged headers, luminance alpha. `tests/wxl_m3_dropin_test.cpp` (ASan/UBSan) builds an MD34
by hand (one bone, three vertices, one triangle, one material) in a `Patch-ZB.mpq` folder with mixed-case file names and
checks the served `MD20` model (264, one bone, three vertices), the `SKIN`, the cache, the DDS-to-`BLP2` transcode,
refusals for unrelated names and path escapes, a damaged file, and 120 byte-mutated variants (every call returns, the test
process survives; without sanitizers 85 serve a well-formed model and 35 are refused). `Wow` builds and
`launch-native.sh --check` passes. **Not verified:** the `SFile` integration (the in-memory file through the engine's
async model loader and the `.dds` fallback in the file layer) has no automated test, only the build; and the converter
has only ever seen the synthetic model, so whether a real HotS/SC2 model looks right is unknown. First real run:
`WXL_NATIVE_DIAGNOSTICS=1` prints `modern-m3:` lines.

### Weapon orientation: native implementation (2026-10-30)

Back-sheath rotation, main/off-hand rotation and reverse grip now have native
animation adapters and the shipped Transmogrification Lua interfaces. The source
weapon-family/attachment policy is retained: drawn two-handed weapons remain
stock, hip anchors provide hand eligibility without receiving hand transforms,
and attachment translation is preserved. Scale and orientation compose on fresh
poses; body meshes and shields do not receive weapon rotations.

Local-player and registered UI preview roots use local choices. Nearby players
use bounded 80-owner GUID state, independent back/hand snapshot staging and
immutable resolved-root snapshots. Object-manager resolution occurs before native
animation workers, never from worker callbacks. Missing/replaced or ambiguously
shared roots retain stock placement; world unload clears remote identities and
pending snapshots while preserving local choices. Lua context teardown resets
all controls and preview registrations. `WXL_BACK_SHEATH_FLIP=0` suppresses
availability/rotation without disabling the independent equipment-size adapter.

Production state, attachment transforms and Lua bridge pass ASan/UBSan and
float-cast-overflow checks: local/preview/remote isolation, all orientations,
100 fresh-pose updates, source exceptions, bounded/invalid GUIDs, independent and
partial snapshots, capacity reuse, root replacement/disappearance, ambiguity,
world teardown, profile disable and context reset. The complete native Release
build passes. The user reports no issues in the latest build, but could not confirm an eligible
weapon was tested. **Rendered weapon placement, nearby-player server synchronization,
physics interaction and transition/performance acceptance remain unverified.**

### Local launcher and future sharing (2026-10-30)

`launch-native.sh` reproduces the working Nix/DXVK/SDL3 environment, sets the game
working directory, preserves arguments and saves per-launch logs. `--check`
validates dependencies without starting the game. `install-native-launcher.py`
creates an applications-menu/desktop shortcut (installed locally). Runtime check,
Desktop Entry validation and fake-executable argument/path smoke checks pass. See
[NATIVE-LAUNCH-AND-SHARING.md](NATIVE-LAUNCH-AND-SHARING.md) for usage and the
machine-specific paths/runtime/network/settings audit. Plain folder copying is
not a portable release: standalone assets, pinned/packaged runtime dependencies
and clean per-player settings remain packaging work.

### Renderer crash during PocketPals use: targeted fix (2026-10-30)

The native 12:49:52 crash log confirms an access violation at
`CM2Shared::SetIndices` (`Wow64+0x25507a`), reading a source skin-section start
during world rendering. The 12:43 report has the same instruction. No game close
was issued during the repository commit/push.

A matching lifetime defect was found in specialized shader combo growth: the
old path copied/freed the whole M2Data allocation, leaving external relative
arrays and their ownership registry tied to its former address. Reuse of that
address could then retire a live model's skin arrays. Linux now grows only the
combo tables near their fields and retains the original model owner. Allocation
failure changes neither table and propagates to skin-load failure. The production
conversion and upload run in the regression fixture, including unrelated-owner
cleanup, repeated uploads, table reuse, partial allocation failure and cleanup.
ASan/UBSan checks pass, as do ten installed model/profile regressions and the
complete native Release build. Existing wide HD indices/material/bone contracts
are retained.

PocketPals 4.8.5 is a Lua/texture addon plus server module, with no WXL bridge calls
found. Its causal connection and the exact triggering world model remain
unproven. **Next acceptance: restart the native executable and retry PocketPals.**
If the crash recurs, `WXL_NATIVE_DIAGNOSTICS=1` names models at combo growth and
index upload. The user reports PocketPals now appears working after this fix; wider world/lifecycle acceptance remains open.

### Transmogrification and PaperDoll follow-up (2026-10-30)

The addon's equipment-size and preview-camera warnings now have native APIs
behind them. Local weapon/shield sizing applies the source policy to fresh
attachment transforms, retains attachment origins, and excludes remote world
characters and body meshes. Registered UI roots and frame-local preview zoom/pan
use lifetime generations and reset on UI context teardown. Source policy imports
have recorded GPL provenance. Production policy/Lua fixtures pass ownership,
input, projection and reset checks, and the full native Release build passes.
Rendered sizing, preview controls and physics interaction still need acceptance.
At that stage UI-frame registration covered sizing only; the later native
weapon-orientation implementation is recorded above.

The user's next retest reports Transmogrification appears working and the update
warning is gone. Individual sizing/preview/physics acceptance remains separate.

The first title patch did not fix the PaperDoll traceback: the shipped caller
uses `IsTitleKnown(i) ~= 0`, so the boolean return introduced by that patch made
unknown titles pass. Numeric 0/1 is restored with the bounds guards retained.
`GetNumTitles` now returns the highest title-mask index, 189 in the installed DBC,
instead of sparse record ID 60,104 plus one. The updated regression executes the
actual shipped PaperDoll title-building code with production bindings and strict
`strtrim`, including all installed titles and current title 39. ASan/UBSan and
float-cast-overflow checks pass; native Release build passes. The user
subsequently confirms no further errors: the title correction is accepted in game.

### Confirmed in-game fixes (2026-10-30)

The user confirms the HD index-window fix removed the wisps and corrected Human
Female geometry. The expanded appearance cache and race-capacity bridge also
restored additional-race character creation. These three reported defects are
resolved; broader rendering and transition acceptance remains separate.

### Character-creation blocker (2026-10-30)

The user reports the additional-race warning prevents character creation. The
native client lacked the `WXL_CharacterCreateRaceCapacity` bridge required by
GlueXML and still used the original 22-race appearance cache. Linux now has
256 race rows and three independent gender slots, bounded race/sex access,
complete initialization/shutdown cleanup, and the registered capacity bridge.
ASan/UBSan covers all 768 slots, invalid indices and repeated cleanup; the full
native Release build passes. The user confirms additional-race creation works.
Optional customization/eye/druid preview bridges remain separate port work.

### Latest character-visual correction and live feedback

**Historical pre-fix retest:** The user reports wisps on naked as
well as equipped characters and apparent skin-color changes. Investigation now
has a confirmed HD skin-window defect: Human Female HD profile 01 stores 73,032
indices, with geoset 202 starting at 69,192. Native code used its low word 3,656
and treated its high word as part of the geoset ID. All 3,840 indices read through
that truncated window reference vertices outside the section. High Elf Male HD
has two equivalent invalid windows of 1,956 indices each.

The Linux loader now separates the packed file record from runtime sections,
decodes full index offsets and geoset IDs, and uses 32-bit runtime draw starts
and counts. Palette repair also accepts bounded wide index arrays. Eight HD
model/profile pairs pass section-corner/bone/native-batch preservation checks;
production index-upload, packed-record rejection and regular-model checks pass
ASan/UBSan, and the final native Release build passes.
The user now confirms this fix removed the wisps and corrected Human Female geometry.


Earlier tests reported more visible armor, working belt/item physics, normal
item textures and apparently working animations, with wisps and stretched base
triangles still present. The later HD index fix now has user-confirmed resolution
for those two visual defects.
Complete equipment/scene-transition and performance acceptance is still open.

Installed native-v264 Blood Elf Female skins contain 162-bone palettes, and
Night Elf/Draenei Female contain 141, exceeding the draw path's 75-bone upload
limit. Native skins now receive geometry-only palette compaction/subdivision
without modern shader decoding. Owned aligned copies handle unaligned source
skin tables/palettes. Upload caching accounts for palette length, and instanced
draws cannot exceed the number of uploaded palettes. The complete native build
and ASan/UBSan checks pass on these three skins and Human Female, preserving
triangle order, section IDs, every bone mapping and native batch contents.
Human Female already fits the budget. These checks do not establish that either
reported visual defect is resolved; base-model draw and scene-state diagnosis
remains the immediate priority before returning to the broader renderer port.

Native geometry optimization also now refuses merges with different shader,
texture-coordinate, material-layer, priority, palette-length or influence-count
contracts. Repeated material windows that exceed uint16 optimized offsets fall
back to original draws. Production-helper sanitizer cases pass for contract
mismatches, repeated 45,000-index windows and the exact uint16 boundary. The
complete native Release build passes; rendered acceptance remains open.

### Ordered next steps

1. **WMO layered materials: done pending acceptance.** The offline-patched shader
   pack, batch latching, extra UV/weight stream, texture readiness, sampler/tint binding
   and state restoration are implemented and GPU-probe verified. The user chose not to
   test the scarce layered content in game; revisit if more modern WMOs are added or a
   rendering problem is reported. The 22 high-shadow-tier vertex permutations now have compact layouts (GPU-probe verified, not seen in game).
2. **Close remaining armor/model policies and recovery gaps.** Audit source path,
   attachment and material cases against the native implementation; cover cached
   previews, owner replacement, incomplete loads, removal and retries. The
   weapon-side sidecars are ported (below; user reports it appears to work in
   game). Offhand mirroring is now ported (below). Private physics
   source associations: see the Item physics note below.
   Continue diagnosing remaining missing armor without treating successful
   bone-ID matching as proof of full appearance parity. The reported wisps and
   Human Female deformation are resolved by the accepted HD index-window fix.
3. **Finish unsupported model-format paths.** Packed wide skin index offsets
   now decode into 32-bit runtime sections, with installed HD character fixtures.
   Keep the existing 16-bit vertex-lookup limits explicit and finish split
   external-animation rebasing. Unsupported inputs still require protection;
   rejection alone does not establish format support. M3/MD34
   and DDS drop-in support is now ported (below) and synthetic-tested only; there are
   still no supplied source assets to exercise it.
4. **Accept the native renderer passes in game.** Water, multisample/depth
   handling, contact ripples, atmosphere, local lights, interior transitions,
   scattering, god rays and glow compensation are integrated and pass synthetic
   GPU checks (see the section above). Remaining: live appearance, waterline and
   interior transitions, frame cost, and the opt-in transparent-fog hook (implemented, unseen).
5. **Port the remaining enabled equipment/presentation modules.** Grass motion
   behavior (the game-object editor is now ported, see below; it needs an in-game GM
   test) remain open. Native weapon orientation and
   loot beams are build/fixture verified; rendered/server acceptance remains open.
   Retain the full module audit for disabled/optional extensions; they are not
   counted as implemented merely because the checked profile disables them.
6. **Run deferred acceptance when dependencies are ready.** Capture Release
   diagnostics and exercise world/paper-doll/selection views, equipment changes,
   animation, camera/water transitions, teleport/logout and resource cleanup.
   Broader acceptance remains deferred; the limited user observations above are
   recorded separately. Continue independent implementation work in the meantime;
   this deferred gate does not make the overall port complete.

### Source and content gaps

- The supplied WarcraftXL tree references `WeaponExtension.cpp`,
  `WXLWeaponModels.csv` and `WXLWeaponTextures.csv` but does not include that
  provider. It was found upstream (2026-10-31) at
  github.com/Atraxian123/WXL-Equip-Creature-Extension-CSV-WXL11 (core 1.1) and
  .../WXL-Equip-Extension-CSV (older core); the source headers are
  GPL-3.0-or-later. The native sidecar module follows the 1.1 variant.
- The supplied animation provider also rejects split AFSA/AFSB streams. Complete
  split-animation support requires additional rebasing work beyond copying its
  existing behavior.
- Installed-content survey (2026-10-31, every `.anim` and `.skel` entry in the 21
  client MPQs read through StormLib): 16,317 `.anim` files, 16,261 legacy
  (no wrapper) and 56 single-chunk AFM2 wrappers whose payload length equals
  file size minus 8. **No AFSA or AFSB chunk and no `.skel` file exists in this
  client.** Split-animation rebasing therefore has no sample to specify or
  test it against, and `.skel` merging (the only source of such files) is
  opt-in. It stays rejected-before-parse; implement only when a real
  split-animation model is supplied.
- **Split-animation rebasing: deliberately not implemented (2026-11-01).** The supplied Windows provider itself rejects
  AFSA/AFSB streams (`AnimHost.cpp`: "Until that complete rebase exists, a multi-chunk file is deliberately rejected so
  the client cannot consume an incomplete AFM2-only view"), and keeps `.skel` merging opt-in and size-capped for the same
  reason, so there is no Windows behaviour to match. Flattening would mean sending bone tracks (AFSB), attachment tracks
  (AFSA) and the rest (AFM2) into one buffer and rewriting the model's per-sequence track offsets by owner, from a layout
  nothing in this tree specifies and no file here exercises. A wrong guess feeds garbage into the track relocation instead
  of the safe present outcome (the sequence is refused and the model holds its default pose), which is real harm for no
  parity gain. The one safe step is taken: the rejection log now lists the file's chunks
  (`modern-anim: rejected ... (AFM2:4096 AFSB:81920)`, from `DescribeModernAnimationChunks`, covered in
  `tests/wxl_native_m2_repair_test.cpp`) so a real file can be specified when one exists. Implement the flattening when a
  split-animation model and its expected result are supplied.
- No loose M3/M3A/M3CFG/DDS inputs or matching loose collection-sidecar tables were
  found in this client. Their absence limits content-based acceptance; it does
  not mark those provider/features complete. The M3/DDS providers are ported anyway
  (the project is live and new content may arrive); see "M3 and DDS drop-ins".

## Runtime boundary

The Linux-native executable does not inject or load WarcraftXL.dll,
WarcraftXLHost.exe, or WarcraftXLVisuals.dll. Windows DLL code cannot directly
cause a rendering defect in this executable. A native defect can still be exposed
by an asset that WarcraftXL would otherwise have transformed or attached.

The original enabled client configuration identifies these currently relevant
features:

- Modern Assets: M2, WMO, BLP/DDS, and the host-only M3 drop-in provider.
- Collection armor: race/gender collection-model resolution, attachment rebuilding,
  section filtering, texture overrides, and bone-palette copying.
- Back-sheath flips, belt physics, loot beams, tree wind, game-object editor,
  interface helpers, and optional rendering/environment changes.
- WarcraftXLVisuals.dll: Windows D3D9 wrapper for water, fog, lights, and
  postprocessing. It is a separate renderer feature, not an asset provider.

## Direct asset path

| Feature | Native status | Validation needed |
| --- | --- | --- |
| MD20 source M2 v272-v274 | Implemented | source armor/creature model |
| MD21 + TXID/FileDataID | Implemented | MD21 collection/creature model |
| sibling .skel merge | Implemented | split-skeleton model without split animation |
| AFM2 external animation wrapper | Single-wrapper unwrapping implemented; malformed and split AFSA/AFSB streams now fail before native track parsing | streamed animation model; split-animation rebasing remains unimplemented |
| collection UV-loop / texture-slot repairs | Implemented for converted and eligible native-v264 models; synthetic native dispatch test passes | animated collection material |
| modern skin material/batch rebuild, including 75-bone palette subdivision | Native-v264 geometry-only repair and modern shader rebuild implemented with checked references and propagated rebuild failures; compile/fixture-verified, not yet runtime-verified | source skin with packed shader batches and an over-budget palette; packed wide index offsets now have installed-HD and production-upload coverage, with rendered acceptance open |
| BLP2 BGRA and component DXT | Implemented | source item/component texture |
| WMO root/group conversion | Implemented | modern WMO zone/interior |
| WMO material and visibility draw interception | Partial: converted-root antiportals/exterior reopening and owned layered material/UV/weight snapshots are implemented; layered GPU draws run through an offline-patched shader pack and are probe-verified, in-game acceptance remains | source WMO with modern material/portal behavior |
| M3/MD34 provider + DDS sidecars | Ported 2026-11-01; synthetic-tested only (see "M3 and DDS drop-ins") | any real `.m3` drop-in and a model it should look like; none exists in this client tree |

The M3 provider cannot affect the supplied FTA MPQ assets unless an M3 drop-in is
actually added: WarcraftXL's own implementation is host-only and scans only loose
Data/patch-ZA.mpq/ or Data/patch-ZB.mpq/ folders. No such source files are present
here, and this install's `patch-ZB.mpq` and `Patch-ZA.mpq` are archive files, so a
loose folder of the same name cannot sit beside them (see the section below).

The Windows M2 skin rebuilder is enabled by default when a model was hot-reshaped.
It is a required parity item, not an optional polish pass. Its Windows allocation
model cannot be copied literally: the native 64-bit loader stores pointers as
32-bit self-relative offsets. The native port now allocates replacement lookup,
batch, and palette-subdivision arrays near their native owners and releases them
with the model. Subdivision rejects malformed section windows, remaps each split
vertex's influences into a <=75-bone local palette, and re-points every batch to
the new section run.

Modern skins now validate loaded vertex/index windows, section-local palette
references and batch materials before rebuilding, and validate the rebuilt
result again. Required palette subdivision and batch rebuild failures stop skin
initialization. Shader expansion also requires its follower material to exist.
Packed index high words now decode separately from geoset IDs into 32-bit runtime
section offsets/counts. Invalid decoded windows reject; valid HD windows reach
the production native index upload without truncation. The sanitizer fixture
covers source decoding, triangle/bone preservation, upload and allocation cleanup.
Skin vertex lookups remain 16-bit, and these checks do not establish rendered
parity.

## Collection armor

This is the highest-priority remaining feature for missing armor. It is not just a
file converter:

1. It sees an ItemDisplayInfo row whose model starts with collections_.
2. It chooses the equipped character's race/sex M2 under
   Item/ObjectComponents/Collections.
3. It attaches that model to the character with stable lifetime handling.
4. It filters the shared full-body model to the equipped slot's allowed skin sections.
5. It copies compatible character bone palettes so the attachment follows animation.

The Linux decompilation exposes the corresponding non-injected surfaces:
CCharacterComponent, CM2Model::AttachToParent, deferred model calls, skin sections,
and palette generation. The current integration uses these APIs. The checked
profile enables collection armor, but world, paper-doll, and character-selection
lifecycle coverage is not yet complete or verified.

### Work completed and observed (2026-10-30)

- Collection auto-filtering now selects the full hundred-range for each slot's
  geoset group, rather than treating a group number as an exact section ID. The
  user's subsequent in-game test showed more armor; other armor remained missing.
- DBC model lists support explicit `:section[,section]` IDs, and semicolon-separated
  model/texture lists are paired by position with a first-texture fallback.
- Collection attachments use separate slot IDs and map bones by name CRC, key ID,
  lookup, and parent. This is compiled, but the blue/white stretched geometry
  initially persisted; the later HD index fix is now user-confirmed resolved.
  Semantic maps are
  now cached per child and invalidated on reattachment or changed skeleton identity;
  current matrices are still copied each animation update. Frame-time impact has
  not been tested in game.
- Collection palettes also refresh after native workers join and before physics,
  using fresh parent-first scene traversal, including cached graphs whose only
  list seed is a child. The cache/copy implementation is shared with a native API
  fixture; placement and full live transitions still require acceptance. The
  reported stretched-geometry defect is now resolved by the HD index fix.
- `WXLItemDisplayModels.csv` and `WXLItemDisplayModelMaterials.csv` have native CSV
  readers. A subset of collection-body sidecar models, common model/texture suffix
  policies, and per-instance texture-type material replacements are implemented.
  Cape/tabard sidecar slot numbers are translated to the native enum. Extra sidecar
  attachments no longer suppress the ordinary base-item path or its cleanup.
- The sidecar parser/path tests passed, including quoted lists, malformed input,
  material rows, and cape/tabard slot mapping. The native client built successfully
  after these changes; subsequent native builds also passed.
  These sidecar and lifecycle changes have not yet been verified in-game.
- Loose `Data/*.mpq/DBFilesClient` model and material sidecars are now loaded,
  with case-insensitive name matching and bounded reads. A filesystem fixture and
  native build pass; this client currently has no such loose sidecars to exercise.
- Collection DBC model/texture paths now use Icon2 path flags and custom folders;
  sidecar DBC/slot policies use those flags. Parser/path tests and native build pass;
  actual model and texture resolution remains unverified in game.
- Icon2 DBC column anchors now reach native attachment admission; mixed rows
  and glove body meshes retain source root rules. Sidecars use source slot
  defaults with glove-root handling, and ordinary components infer shoulder,
  cape and tabard folders/points. Tauren feet exclude collection entries only.
  Bounded parser/policy sanitizer fixtures and native build pass. The supplied
  provider and this port both select the left Icon2 value; live placement and
  transitions remain deferred.
- Explicitly filtered DBC or sidecar models no longer need a `collections_`
  prefix to enter the collection path; sidecars naming the Collections folder
  also qualify. Build verified, runtime verification pending.
- Adding collection armor now preserves the native body texture layers, chest
  flags and cape path; collection-only helms and shoulders clear stale stock
  model attachments. Collection helms also apply stock helmet geoset visibility.
  Build verified; appearance and helm visibility need testing.
- Collection sidecar entries with explicit attachment points now use those
  points while retaining per-slot cleanup and collection palette handling.
  Model duplication preserves root attachment intent. An identifier fixture
  and native build pass; deferred-load retries and runtime positioning remain.
- A representative installed leather collection model in `patch-ZB.mpq` is
  native MD20 v264 with five skin sections. Its textures already use types 2
  and 0, and the new v264 repair path leaves it unchanged; this sample does not
  establish the cause of the stretched geometry.
- For that Human Male collection model, all 16 bone-name CRCs match bones in
  the installed 228-bone Human Male character M2. This checks identifiers for
  one pair only; it does not verify copied matrices or other race/model pairs.
- Ordinary armor-side component sidecars now attach at explicit or slot-default
  points with independent slot ownership; they do not copy collection palettes.
  The native build passes, but placement and texture behavior remain unverified.
- Render preparation now replays equipped rows on parent/skeleton/scene,
  race/sex, profile-state or map-unload generation changes. Item visuals are
  preserved, race/sex changes refresh item component textures, and component
  reinitialization cancels pending buckle requests. Owner-watch fixture and
  native build pass; live transitions and recovery still need acceptance.
- Collection DBC, sidecar and material texture replacements now wait for
  requested texture readiness, with fallback backoff, per-child/type precedence,
  bounded fair polling and owner/item cancellation. Requests hold child references
  until completion. The production queue passes sanitizer lifecycle fixtures with
  native API stubs and compiles in the client. Glue idle now polls loaded selection
  and creation components so cached previews can recover without fresh UI events.
  The complete integration builds; live material/transition acceptance is deferred.
- Weapon-side sidecars: the provider source is upstream (see Source and content
  gaps) and is now ported; see "Weapon sidecars" below.
- Still open: full path and
  attachment policy parity, complete transition/retry behavior, and
  the native renderer passes listed below. The user requested deferring the next
  client test until the renderer and collection-sidecar dependencies are ready.

## Deliberately separate workstreams

- WarcraftXLVisuals.dll is a Windows D3D9 proxy/wrapper. Its atmosphere/water
  features require native renderer work; they are not a prerequisite for loading
  FTA M2/BLP/WMO assets.
- UI helpers, quest/target overlays, loot beam, tree wind, editor controls, and item
  physics are individual gameplay/presentation features. They must each be ported
  through native game events, not copied as DLL code.
- The reported blue/purple wisps and Human Female deformation are resolved by
  decoding HD skin index windows; the user has confirmed both fixes in game.

## Active client-profile parity gate

The checked client profile enables the following behaviors. Implementation and
in-game validation are separate. The user is currently testing character visuals
and creation; broader renderer and transition acceptance remains open.

| Windows feature / source | Profile state | Native status |
| --- | --- | --- |
| Modern Assets + asset host | enabled | Partial: direct M2/BLP/WMO conversion, checked material/batch rebuilding and palette subdivision, WMO visibility handling and layered source-data preservation are compiled. WMO layered GPU draws are implemented (probe-verified, not yet seen in game); the M3/DDS drop-in providers are ported (synthetic-tested only); unsupported M2 formats remain open. |
| Collection armor | enabled | Partial: race/sex resolution, section filtering, Icon2/sidecar placement, cached and post-worker bone remapping, bounded texture readiness, owner/world replay and cached-preview polling are implemented and build/fixture-verified. Weapon sidecars, including the offhand Y-mirror, are ported (user reports the sidecars working in game; the mirror is not seen in game); remaining policies and full runtime/transition acceptance remain open. |
| Additional-race creation cache / capacity API | required by shipped GlueXML | Implemented with bounded 256-race/three-gender storage and full cleanup; sanitizer and native build pass. User confirms additional-race creation works; optional customization controls are separate work. |
| Transmogrification sizing / preview camera / title API | required by shipped addon | Native local equipment sizing, registered previews, frame projection controls and corrected numeric title contract/bounds and mask enumeration implemented. Production Lua/policy fixtures and native build pass. User reports Transmogrification working without the update warning and no further title errors; individual sizing/preview/physics acceptance remains open. |
| Game-object editor | enabled | Ported (build + real-GPU gizmo probe + preview fixture pass); not yet run in game |
| Back-sheath / hand rotation and reverse grip | enabled | Native local/registered-preview transforms, live eligibility, bounded nearby-player snapshots and lifecycle/profile controls implemented. Production Lua/adapter sanitizer checks and full build pass; rendered/server/physics/transition acceptance remains open. |
| Grass motion | enabled | Implemented: offline-patched live grass vertex shaders, per-frame constants, rigid-prop marking and the sway-control Lua bridge; probe/sanitizer verified; user confirmed working in game (2026-10-31) |
| Visual environment companion | enabled | Integrated: imported atmosphere renderer through the native D3D9Ex device with vendor-independent depth copy; synthetic real-GPU probe (no MSAA and 4x) and sanitizer config tests pass. Not yet seen in game. |
| Modern water / multisample integration | enabled | Integrated: imported FFT water at three quality levels with MSAA depth handling and resize/abort cleanup; synthetic real-GPU probe passes. In-game appearance and frame cost unverified. |
| Water ripples | enabled | Contact capture from visible units feeds the imported ripple simulation; stock splash/wake sprites are preserved (the profile keeps client sprites). Probe-verified with a wading unit; user confirms ripples and wake work in game. Other water scenarios and frame cost remain open. |
| Native fog-density scale | enabled | World-draw scope scales linear fog range and restores it, with underwater excluded; camera-liquid state is refreshed before the scope. Compile and arithmetic checks pass, runtime and waterline verification pending. |
| Forward scattering, local lights, interior transitions, god rays, glow compensation | enabled | Integrated from native state (M2 light grid, viewer map-object groups, FFX glow cvars); god rays and point-light selection exercised in the synthetic probe. Live placement, occlusion and cost unverified. |
| Extended view distance | enabled | Native world far-clip equivalent implemented, using client-local `WarcraftXL.cfg`; compile-verified, not yet runtime-verified |
| Object-distance scale 1.5 | enabled | Native `environmentDetail` equivalent implemented; compile-verified, not yet runtime-verified |
| Grass-distance scale 1.0 | enabled but native value | Native equivalent implemented; the checked value leaves stock distance unchanged |
| Loot beam | enabled | Native typed corpse scanning, bounded procedural shafts/motes and depth-tested additive D3D9Ex world draw implemented. Production geometry and imported renderer state/failure sanitizer fixtures plus native build pass. User confirms loot beams work in game; broader quality/occlusion/reset/transition/performance coverage remains open. |
| Item-physics belts | enabled | Native companion/PFDC/PFID loading, skeleton binding and post-worker simulation implemented; core/adapter sanitizer fixtures and native build pass. Rendered motion, private-source association and performance/lifecycle acceptance remain open. |
| Cast-failure mute | enabled | Native sound-kit filter implemented and build-verified; diagnostic and in-game verification pending |
| Shadow-distance scale 1.0 | enabled but native value | Native behavior already equivalent |
| Unit overlays (`WXL_UNIT_OVERLAYS`), quest rendering (`WXL_QUEST_RENDERING`) | disabled by config | Ported natively 2026-11-01 at the developer's word that they were only config-disabled; still off in this profile, turn on in `WarcraftXL.cfg` (see "Quest tracker, objective rings and unit outline") |
| Tree wind, density variation, distance haze, ground mist | disabled | Excluded from the parity build unless explicitly enabled later |
| Distance fog, transparent fog | enabled (2026-10-31, user request) | Native distance fog and the transparent-fog hook are implemented and look right in game; both stay opt-in in code (`WXL_DISTANCE_FOG`, `WXL_TRANSPARENT_FOG`) |

The Windows DXVK proxy is not copied into the Linux build. The native executable
uses its own native-DXVK path; the proxy's backend-selection/config behavior must
be represented by Linux launch configuration rather than a Windows DLL.

The item-physics adapter restores native matrices before animation, then stages
and checks simulation output after workers join and collection retargeting
finishes. Model-owned state retires on attachment/destruction or changed
palette/skeleton/scene/world generations. Companion/PFDC/PFID lookup and cached
clock settling are implemented. Sanitizer tests also bind and simulate installed
artifact-shield (17 bones, 10 bodies, 9 joints) and Fel Plate Warrior buckle
(4 bones, 4 bodies, 3 joints) samples for 60 steps. The buckle's M2 comes from ZB
and its PHYS from ZA. These are read-only asset/solver checks, not rendered
acceptance. Frame cost and live transitions
remain open.

**Virtual-source association (assessed 2026-10-31, not ported).** The supplied module resolves *virtual* equipment names
(baked copies registered by the Windows asset host, e.g. recolours) back to a source model before looking up the PHYS
companion. The native client has no virtual model files: collection and sidecar models are real archive paths, so the
companion is found by the real name. This is a reading of the supplied `wxl-item-physics` README and
`VirtualModelSources.hpp`, not something a test exercises. What remains is content: the upstream audit
(`tools/custom_companions.py`) found 25 private models with no PHYS donor, which no code port can supply.

## Full binary/module audit

This is a source-level inventory of the three Windows binary roles and every
WarcraftXL runtime extension that can supply a client function.  It deliberately
includes modules which are not enabled by the checked profile: an inactive module
is not silently counted as ported, and turning it on in a later profile requires a
new native implementation and acceptance test.

| Windows provider/module | Native replacement status |
| --- | --- |
| `d3d9.dll` proxy / bundled DXVK selection | Native-DXVK launch path exists; no Windows proxy is used. Its proxy-specific selection logic is not applicable on Linux. |
| `WarcraftXLHost.exe` asset IPC, M3/M3A/M3CFG/DDS provider | Direct M2/BLP/WMO loaders are in-process. The M3/M3A/M3CFG and DDS providers are ported in-process (2026-11-01, "M3 and DDS drop-ins"); synthetic-tested only, no supplied input files. |
| `WarcraftXL.dll`: modern-assets | Partial; see the direct-asset table. Native material/batch rebuilding, palette subdivision, WMO visibility handling and layered source-data preservation are implemented. WMO layered GPU draws are implemented (probe-verified, not yet seen in game); the M3/DDS providers are ported (synthetic-tested only); unsupported M2 formats remain open. |
| `WarcraftXLVisuals.dll`: renderer wrapper | Partial: water, multisample depth, atmosphere, god rays and local lights run in-process through dxvk-native D3D9 (precompiled, hash-verified shaders; vendor-independent depth copy patch). Synthetic real-GPU and sanitizer tests pass. Transparent-draw fog fitting is ported (opt-in, see "Transparent fog"). Colour grading and Forever Glow/Look are **not ported on purpose**: the supplied `selection_policy.cpp` forces `ColorGrading=0`, `ForeverGlow=0`, `DataMode=0` and the Classic noise/phase off, and the supplied runtime logs "Classic placements/noise, grading, glow replacement and sprite changes disabled", so the shipped Windows product never runs them. They also need the Classic fog data and engine byte-placement patches. Porting them would add behaviour the Windows build does not have; nothing is accepted in game yet. |
| `WarcraftXL.dll`: cast-failure sound filter | Native `SI2::PlaySoundKit` filters SoundEntries 1424–1428 when `WXL_MUTE_CAST_FAILURE_SOUNDS` is enabled, returns success and clears the optional sound handle like the Windows hook. The existing profile/environment reader supplies the option; a one-time stderr diagnostic records actual suppression. Build verified; gameplay and diagnostic verification pending. |
| `wxl-collection-armor` | Partial: direct `CCharacterComponent` integration provides race/sex resolution, section filtering, independent slot ownership, Icon2/sidecar placement, bounded texture readiness, owner/world replay, cached-preview polling and cached/post-worker semantic bone copying. Model/material CSV and loose overlays are supported. Weapon sidecars, including the offhand Y-mirror, are ported (user reports the sidecars working in game; the mirror is not seen in game) and remaining policy cases are open. Production cache/copy and lifecycle fixtures pass; rendered appearance, full transition recovery and performance remain unverified. |
| `wxl-waist-models` | Native attachment-53 buckle path implemented with nonblocking texture readiness, bounded retries, slot cleanup and owner identity checks. Path/name/retry fixture and native build pass; appearance and lifecycle acceptance remain unverified. |
| `wxl-item-physics` | Partial: portable PHYS/Domino core and native scene/model adapter are implemented and compile/fixture-verified. Virtual-source association is a Windows-host concept with no native counterpart (see the note above); rendered/lifecycle/performance acceptance remains open. |
| `wxl-back-sheath-flip` | Native animation/Lua adapter implemented for back/hand rotations, reverse grip, previews and nearby-player snapshots. Source eligibility and profile gate preserved; sanitizer/native build verified. Live rendered/server/transition acceptance remains open. |
| `wxl-loot-beam` | Native world-pass adapter implemented; authoritative per-viewer hint policy, bounded tracks/geometry, state-safe imported D3D9 draw and map/profile lifecycle. Sanitizer fixtures/native build pass; user confirms live loot beams work; broader quality/occlusion/reset/transition coverage remains open. |
| `wxl-render-modern` | Grass motion implemented (offline-patched shaders; see the grass section); confirmed working in game by the user (2026-10-31). |
| `wxl-treewind` | Not ported, by decision: the developer confirmed (2026-11-01) it stays disabled and unported. |
| `wxl-gameobject-editor` | Ported natively (2026-10-31); see "Game-object editor" below. In-game GM acceptance pending. |
| `wxl-character-animation-lab` | Ported natively (2026-10-31); see "Character Animation Studio" above. In-game acceptance pending. |
| `wxl-character-customizer` | Ported natively (2026-10-31); see "Character customizer" above. In-game acceptance pending. |
| `wxl-gear-recolor` | Nothing to port: dye was removed upstream and the preview natives already exist. See "Gear recolor" above. |
| `wxl-selection-circle` | Ported natively (2026-10-31); see "Selection circle and combat-readability bridge" above. In-game acceptance pending. |
| `wxl-combat-readability` | Lua settings bridge ported (2026-10-31). Stores values only; its renderer is `wxl-unit-outline`, now ported natively (off unless `WXL_UNIT_OVERLAYS` is on). |
| `wxl-selcircle-poc`, `wxl-quest-marker`, `wxl-unit-outline` | Ported natively 2026-11-01 (developer confirmed they were only config-disabled): objective rings, quest/corpse/diamond markers and the target outline/chevron/danger-guide renderer. Gated by `WXL_QUEST_RENDERING` (rings, markers) and `WXL_UNIT_OVERLAYS` (outline), both 0 in this profile. Real-GPU probes pass; not seen in game. See "Quest tracker, objective rings and unit outline". |
| `wxl-underwear` | Ported natively (2026-10-31); see "Underwear and gossip" below. In-game acceptance pending. |
| `wxl-required-addons`, `wxl-npcbot-targets` (+ resurrection), `wxl-npcbot-enchant`, `wxl-lone-wolf`, `wxl-goblin-glider` | Ported natively (2026-10-31); see "Required add-ons, NPCBot targeting, ..." above. In-game acceptance pending. |
| `wxl-living-azeroth` | Ported natively 2026-10-31: grass instancing, the Environment page backend and the surface lights (terrain, M2, optional WMO; offline shader pack, light grid, baked occlusion, doodad light tables). **Surface lights are off by default** (`WXL_SURFACE_LIGHTS=0`, `lights.enabled=0`). Build, tests and a real-backend shader probe pass; rendering is not yet seen. See "Surface lights" above. |
| `wxl-db2` | Not ported: an extension-DLL service with no consumer in this tree and no host/extension loader on the native client. |
| `wxl-skybox-control` | Native Lua API and DayNight application already exist (`WxlSkyboxOverride`). It needs scenario verification. |
| `wxl-visual-time` | Native Lua API and time application already exist (`WxlVisualTimeOverride`). It needs scenario verification. |
| `wxl-weather-control` | Native Lua API, natural-weather restoration, and weather application already exist (`WxlWeatherOverride`). It needs scenario verification. |
| `wxl-control-panel` | The native control bridge exposes the expected Lua availability/version/options API. It is a compatibility bridge, not evidence that every GPU option behind it is implemented. |

The original Windows process also bundles ordinary Microsoft runtime DLLs and
third-party codecs. They are dependencies of the Windows executable, not
WarcraftXL feature providers, and therefore are not candidates for a Linux client
port. The native build links its own platform equivalents.

### Completion evidence

Set `WXL_NATIVE_DIAGNOSTICS=1` in the launch environment for the eventual
acceptance run to capture collection resolution and M2/BLP/WMO path traces on
stderr. These traces are available in the Release build; `DebugLog` is not.
Waist success/timeouts and rebuilt collection bone-map coverage also emit stderr
diagnostics. No runtime exercise is implied by their presence in the binary.

For each enabled row, completion requires all of the following:

1. Source-to-native mapping recorded in this file.
2. A compiled path in the Linux executable, checked by a focused automated test
   where one is practical.
3. A runtime diagnostic proving the path was exercised.
4. An in-game acceptance scenario appropriate to the feature.

Only then can the native profile be described as WarcraftXL-parity ready.

## Portable overlay for other machines (2026-11-01)

`tools/make-linux-portable.sh` builds `Client-Files/wd-ix-linux-native-portable/`: the `Wow64` executable,
its own glibc and loader, the C++ runtime, xvidcore, dxvk-native, `FTA-Linux/play.sh`, `install.sh`, a
README and the offline shader packs. No symlinks and no game data (the 19 GB `Data` is not copied): it is
installed into an existing From The Ashes base client (`./install.sh /path/to/client`) and started with
`FTA-Linux/play.sh`. The executable was linked against the Nix glibc, so its interpreter is rewritten to a
path relative to the client folder and the launcher starts it from there; `LD_LIBRARY_PATH` is applied to
the game process only. SDL3, libcurl, ALSA, PulseAudio client libraries and the Vulkan driver come from the
system (`play.sh --check` lists what is missing).

Verified: the install and `--check` outside nix on the build machine; a real launch to the login render
(dxvk initialised, swap chain, surface-light packs loaded) from a scratch folder that borrowed the real game
data; `--check`, missing-library reporting and engine start on Debian 13 (glibc 2.41) in Docker. Not
verified: other distributions, NVIDIA/other Vulkan drivers, Wayland vs X11 on other machines, a full play
session from the overlay, and glibc newer than 2.44 on the host.
