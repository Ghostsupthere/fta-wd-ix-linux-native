# Native loot-beam source

Adapted from the supplied WarcraftXL `scripts/wxl-loot-beam`, which attributes
manatactical/wxl-loot-beam commit `fd75b76bf0661f2a3950bd83b835c31258f151c2`.
GPL-3.0-or-later; the supplied LICENSE is included here.

`LootBeamPolicy.hpp` is imported unchanged. `BeamRenderer.hpp` was imported from
the source below and now adds diagnostic result codes and null-matrix checks;
its draw/state/restore policy is preserved. The latter
uses the already-created native D3D9Ex device; it does not load a Windows DLL or
link a second Direct3D frontend. The WorldClient target consumes the same
header-only DXVK include target as Gx.

`LootBeamScene.hpp` adapts procedural colour/shaft/mote generation and GUID/position
tracking from LootBeam.cpp. Geometry output is templated for the production
renderer and standalone tests. Native timing uses a monotonic 64-bit clock and
explicit initial-state flags; candidate counts are bounded. Scene interruptions
retain the last valid camera for at most 200ms, complete scans update at 100ms,
and map unload clears the state. No object pointers or persistent GPU resources
are retained.

`WoW/Source/WorldClient/Linux/WxlLootBeam.cpp` replaces Windows offsets/event hooks
with typed native unit fields, visible-object enumeration and a draw before world
FFX/depth resolve. The matching server module writes the per-viewer remaining-loot
quality to UNIT_FIELD_PADDING; zero must not be inferred as permission to show a
beam. Native `m_projNative` already has the D3D clip-depth conversion; the original
Windows camera conversion must not be applied a second time. World viewport depth
range is forwarded explicitly. Profile/disable-marker handling is retained.

Source SHA-256 at port time:

- LootBeam.cpp: `dfbc4217b417ae59d9a48ef52743b1039a1992101ed0b84e0d5f9fe96b6ad4c3`
- BeamRenderer.hpp: `eec06a2f2db1bd4a0cd0dcced9771316fc62dcf309a7f16e4aa0df2231cb51f7`
- LootBeamPolicy.hpp: `f2781a58bdf39eb5790a7da99fc839ddb173d8c27030a193aa9c049c58211312`

Validation covers production geometry/tracking and the imported renderer with
strict COM doubles (no graphics context). The interface declarations in the test
doubles retain the supplied DXVK/Wine D3D9 header's LGPL-2.1-or-later notice.
These are not rendered visual or live multiplayer acceptance results.

## Missing-beam follow-up (2026-10-31)

The user did not see beams in game. `Logs/LootBeam.log` now automatically reports
backend/config/camera readiness, dead/lootable/hinted corpse counts, raw first hint,
selected candidates, vertex/clip counts and exact draw rejection stage. Reports
are throttled (at most 10Hz on stage changes; stable stages every 3 seconds).
Native typed field offsets are asserted against the source's wire indices.
No loot-eligibility or authoritative-hint requirement was relaxed.

`tools/wxl_loot_beam_gpu_probe.cpp` tests the real renderer with a hidden SDL3
window, native DXVK and readback of synthetic procedural geometry, without game
assets or a login. Local result: 372 vertices, 961 coloured pixels with clear
depth, zero coloured pixels when occluded. This establishes renderer operation
in isolation, not the live world pass or receipt of server hints.

`Logs/TextureFailures.log` separately records deduplicated/capped native texture
file/header/decode/GPU allocation failures, including addon paths. The reported
black Retribution Paladin/Fury Warrior proc graphics have no established cause.
A fresh live run is needed to correlate these logs with the missing effects.

## Camera-relative correction (2026-10-31)

Live Northshire diagnostics received correct hints and submitted beams but had
zero in-frustum vertices. Native CSimpleCamera creates its Gx view with eye zero;
world draw geometry is camera-relative. `RebaseWorldView` composes the actual
camera position into that rotation-only view while retaining world-space corpse
tracking/geometry. It leaves the mote billboard basis unchanged and does not
repeat the projection depth conversion.

The GPU probe now uses Northshire-sized world offsets. `--unrebased` reproduces
zero visible pixels; the corrected view yields 960 coloured pixels with clear
depth and zero when occluded. The standalone core fixture reproduces the
out-of-frustum point before rebasing and its valid projection afterward. These
are isolated regression results. The user subsequently confirms loot beams work
in game (2026-10-31). Broader quality/occlusion/reset/transition coverage remains
separate from that accepted scenario.
