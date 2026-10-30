# Native PHYS source provenance

Copied without modification on 2026-10-30 from the supplied WarcraftXL
`scripts/wxl-item-physics/shared/` directory. The decoder declares
GPL-3.0-or-later; retain the original source and license notices.

These are the portable decoder, skeleton binder, solver and matrix helpers.
Windows runtime hooks and offset-based memory access are excluded. Native
scene/lifecycle integration is separate; compiling these files alone does not
enable equipment motion or establish rendered parity.

`../ModernPhysics.cpp` supplies original-file companion/PFDC/PFID association.
`../NativeItemPhysics.cpp` supplies the Linux scene and model lifecycle adapter.
The latter restores native poses before animation workers start and applies
staged simulation output after they join. Runtime motion and performance still
require in-game acceptance; portable fixture results alone do not establish it.

Imported SHA-256 hashes:

- `PhysicsData.hpp`: `27521f667c0fc73fe350dcb32f7cdfda5a667deaf8badeed30d8e70fd0c3b170`
- `PhysicsData.cpp`: `dfbdcda0c88bfa14862f4548c9e463b2ff19baa9d76f32bac767f9d4053f28c0`
- `domino/physics_domino.h`: `aaa96f376cb76b9d3810b2065e002505a9b22b8fe26520eeaa6a3efcacb2766c`
- `domino/physics_domino.cpp`: `5b6030d5e6885a87938bf70fa2c795ae33196a69860fe0f8083c57c3acba9449`
