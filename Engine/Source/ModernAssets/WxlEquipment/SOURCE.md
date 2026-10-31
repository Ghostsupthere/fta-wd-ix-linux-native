# Portable equipment presentation policy

Imported unchanged from the supplied WarcraftXL `scripts/wxl-back-sheath-flip/src/` tree.
Copyright WarcraftXL contributors; GPL-3.0-or-later, as identified in each source header.
The native adapter uses these unchanged scale and weapon-family/attachment policies.
`../NativeWeaponFlip.inl` and the UI `WxlWeaponFlipLua.inl` are native adaptations
of `BackSheathFlip.cpp`: row-vector X/Y half-turns, eligibility distinctions,
local settings and bounded remote group synchronization. They replace Windows
palette hooks with fresh native animation transforms and UI-thread model
resolution/immutable worker snapshots. They are not a copy of the DLL hooks.
The adapted source's SHA-256 at port time is
`ee02de6562d640e1b0740aa759fb52b2a5ce509e1948a01843997ff524de4e0b`.
Native adaptations retain GPL-3.0-or-later provenance.

SHA-256:

- `EquipmentScale.hpp`: `546b448d3f99ad8e2b732a2b46ed434974ed57f88e08fb75a24a32499112c127`
- `WeaponPalette.hpp`: `cb8304ddf1ab1eabcf1361ff2413f1f7bf8dc31a3ddb9a8bdabb8d59a90917ef`
