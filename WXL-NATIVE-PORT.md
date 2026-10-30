# WarcraftXL native-port inventory

This is the implementation tracker for replacing client behaviors that the Windows
WarcraftXL runtime supplied. It is not a claim of visual parity until each line has
an in-game test against the same FTA content.

## Overall status and next steps (2026-10-30)

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
| Collection armor and waist models | Slot filtering/ownership, Icon2 and sidecar policies, texture-readiness queues, attachment-53 buckles, owner/world replay, cached and post-worker palette copying | Remaining source-policy/weapon-side contracts, remaining missing armor and full transition/retry acceptance; reported wisps/Human Female deformation are resolved |
| Item physics | PHYS companions and PFDC/PFID associations, skeleton binding, native-pose restoration, post-worker simulation and model lifecycle cleanup | Private virtual-source associations, rendered motion, live lifecycle and frame-cost acceptance |
| WMO assets and materials | Conversion, scoped visibility handling, immutable layered texture/UV/weight snapshots and bounded CPU ownership | GPU layered material draw integration, shader preparation, stream/texture binding and state restoration |
| Native environment/renderer | Distance/profile controls and scoped fog-range handling; offline compilation of selected upstream shaders | Water/MSAA/contact simulation, atmosphere, lighting and postprocessing integration |
| Other extensions | Sound filtering and existing skybox/time/weather/control Lua bridges | Back-sheath flips, grass motion, loot beams, editor features and the remaining modules audited below |

The latest complete native build passed. ASan/UBSan fixtures cover skin rebuilding
and rejection, collection texture queues and placement policies, production
palette copying/scene traversal, physics core/native-adapter behavior, and WMO
visual-data preservation/lifetime accounting. Sanitizer runs used disabled leak
detection in this sandbox; they are not leak-check results. Installed shield and
waist PHYS samples also passed binding and finite solver-step checks. These checks
do not establish live GPU behavior, visual parity or teleport/logout correctness.

### Transmogrification and PaperDoll follow-up (2026-10-30)

The addon's equipment-size and preview-camera warnings now have native APIs
behind them. Local weapon/shield sizing applies the source policy to fresh
attachment transforms, retains attachment origins, and excludes remote world
characters and body meshes. Registered UI roots and frame-local preview zoom/pan
use lifetime generations and reset on UI context teardown. Source policy imports
have recorded GPL provenance. Production policy/Lua fixtures pass ownership,
input, projection and reset checks, and the full native Release build passes.
Rendered sizing, preview controls and physics interaction still need acceptance.
Shared UI-frame registration does not mean the separate weapon-flip feature is
implemented.

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

1. **Finish WMO GPU material integration.** Consume the preserved root/group
   snapshots at native WMO batch submission. Add two/four-layer shader edits,
   extra UV/weight streams, texture readiness and sampler/tint binding. Preserve
   native lighting/fog and restore GPU state after each draw. Cover missing data,
   device/resource cleanup and fallback behavior. Shader preparation is currently
   under investigation; no native WMO layered shader/draw path is implemented yet.
2. **Close remaining armor/model policies and recovery gaps.** Audit source path,
   attachment and material cases against the native implementation; cover cached
   previews, owner replacement, incomplete loads, removal and retries. Resolve the
   weapon-side sidecar contract/source gap and private physics source associations.
   Continue diagnosing remaining missing armor without treating successful
   bone-ID matching as proof of full appearance parity. The reported wisps and
   Human Female deformation are resolved by the accepted HD index-window fix.
3. **Finish unsupported model-format paths.** Packed wide skin index offsets
   now decode into 32-bit runtime sections, with installed HD character fixtures.
   Keep the existing 16-bit vertex-lookup limits explicit and finish split
   external-animation rebasing. Unsupported inputs still require protection;
   rejection alone does not establish format support. M3/MD34
   and DDS drop-in support remains a separate unported provider with no supplied
   source assets to exercise.
4. **Port the remaining enabled renderer passes.** Integrate modern water and
   multisample/depth handling, contact ripples, visual-environment/atmosphere,
   local lighting, interior transitions, scattering, god rays and glow
   compensation through the native renderer. Existing offline shader compilation
   is preparation only.
5. **Port the remaining enabled equipment/presentation modules.** Back-sheath
   flips, grass motion, loot beams and game-object editor behavior remain open.
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
  provider implementation. Exact weapon-side parity needs recovered contracts or
  a separately verified native implementation.
- The supplied animation provider also rejects split AFSA/AFSB streams. Complete
  split-animation support requires additional rebasing work beyond copying its
  existing behavior.
- No loose M3/M3A/M3CFG/DDS inputs or matching loose collection-sidecar tables were
  found in this client. Their absence limits content-based acceptance; it does
  not mark those provider/features complete.

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
| WMO material and visibility draw interception | Partial: converted-root antiportals/exterior reopening and owned layered material/UV/weight snapshots are implemented and build-verified; GPU material draw integration remains | source WMO with modern material/portal behavior |
| M3/MD34 provider + DDS sidecars | Not yet ported | no loose M3, M3A, M3CFG, or DDS source exists in this client tree |

The M3 provider cannot affect the supplied FTA MPQ assets unless an M3 drop-in is
actually added: WarcraftXL's own implementation is host-only and scans only loose
Data/patch-ZA.mpq/ or Data/patch-ZB.mpq/ folders. No such source files are present
here.

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
- Weapon-side comments reference `WeaponExtension.cpp`, `WXLWeaponModels.csv`
  and `WXLWeaponTextures.csv`, but the shipped WarcraftXL source contains only
  those references, not that implementation. Exact provider parity therefore
  needs additional source/contracts or a separately verified implementation.
- Still open: weapon-side sidecars, full path and
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
| Modern Assets + asset host | enabled | Partial: direct M2/BLP/WMO conversion, checked material/batch rebuilding and palette subdivision, WMO visibility handling and layered source-data preservation are compiled. WMO GPU material draws, unsupported M2 formats and M3 remain open. |
| Collection armor | enabled | Partial: race/sex resolution, section filtering, Icon2/sidecar placement, cached and post-worker bone remapping, bounded texture readiness, owner/world replay and cached-preview polling are implemented and build/fixture-verified. Weapon-side contracts, remaining policies and full runtime/transition acceptance remain open. |
| Additional-race creation cache / capacity API | required by shipped GlueXML | Implemented with bounded 256-race/three-gender storage and full cleanup; sanitizer and native build pass. User confirms additional-race creation works; optional customization controls are separate work. |
| Transmogrification sizing / preview camera / title API | required by shipped addon | Native local equipment sizing, registered previews, frame projection controls and corrected numeric title contract/bounds and mask enumeration implemented. Production Lua/policy fixtures and native build pass. User reports Transmogrification working without the update warning and no further title errors; individual sizing/preview/physics acceptance remains open. |
| Game-object editor | enabled | Not yet ported |
| Back-sheath flip | enabled | Not yet ported |
| Grass motion | enabled | Not yet ported |
| Visual environment companion | enabled | Not yet ported |
| Modern water / multisample integration | enabled | Not yet ported |
| Water ripples | enabled | Modern-water contact simulation is not yet ported; stock splash/wake sprites are preserved |
| Native fog-density scale | enabled | World-draw scope scales linear fog range and restores it, with underwater excluded; camera-liquid state is refreshed before the scope. Compile and arithmetic checks pass, runtime and waterline verification pending. |
| Forward scattering, local lights, interior transitions, god rays, glow compensation | enabled | Not yet ported |
| Extended view distance | enabled | Native world far-clip equivalent implemented, using client-local `WarcraftXL.cfg`; compile-verified, not yet runtime-verified |
| Object-distance scale 1.5 | enabled | Native `environmentDetail` equivalent implemented; compile-verified, not yet runtime-verified |
| Grass-distance scale 1.0 | enabled but native value | Native equivalent implemented; the checked value leaves stock distance unchanged |
| Loot beam | enabled | Not yet ported |
| Item-physics belts | enabled | Native companion/PFDC/PFID loading, skeleton binding and post-worker simulation implemented; core/adapter sanitizer fixtures and native build pass. Rendered motion, private-source association and performance/lifecycle acceptance remain open. |
| Cast-failure mute | enabled | Native sound-kit filter implemented and build-verified; diagnostic and in-game verification pending |
| Shadow-distance scale 1.0 | enabled but native value | Native behavior already equivalent |
| Unit overlays, quest rendering, tree wind, density variation, distance fog/haze, ground mist, transparent fog | disabled | Excluded from the parity build unless explicitly enabled later |

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
acceptance. Frame cost, live transitions and private virtual-source associations
remain open.

## Full binary/module audit

This is a source-level inventory of the three Windows binary roles and every
WarcraftXL runtime extension that can supply a client function.  It deliberately
includes modules which are not enabled by the checked profile: an inactive module
is not silently counted as ported, and turning it on in a later profile requires a
new native implementation and acceptance test.

| Windows provider/module | Native replacement status |
| --- | --- |
| `d3d9.dll` proxy / bundled DXVK selection | Native-DXVK launch path exists; no Windows proxy is used. Its proxy-specific selection logic is not applicable on Linux. |
| `WarcraftXLHost.exe` asset IPC, M3/M3A/M3CFG/DDS provider | Direct M2/BLP/WMO loaders are in-process. The loose M3 family/provider is not ported and has no supplied input files. |
| `WarcraftXL.dll`: modern-assets | Partial; see the direct-asset table. Native material/batch rebuilding, palette subdivision, WMO visibility handling and layered source-data preservation are implemented. WMO GPU material draws, unsupported M2 formats and M3 remain open. |
| `WarcraftXLVisuals.dll`: renderer wrapper | Partial: native distance and fog-range handling compile; water, multisample depth, atmosphere and postprocessing passes remain unported. The native renderer uses D3D9 through dxvk-native; an offline Wine D3DCompiler helper successfully compiled upstream fullscreen, ray-mask, and low-quality water shaders to D3D9 shader-model-3 bytecode. This establishes shader compilation only, not GPU integration or runtime parity. |
| `WarcraftXL.dll`: cast-failure sound filter | Native `SI2::PlaySoundKit` filters SoundEntries 1424–1428 when `WXL_MUTE_CAST_FAILURE_SOUNDS` is enabled, returns success and clears the optional sound handle like the Windows hook. The existing profile/environment reader supplies the option; a one-time stderr diagnostic records actual suppression. Build verified; gameplay and diagnostic verification pending. |
| `wxl-collection-armor` | Partial: direct `CCharacterComponent` integration provides race/sex resolution, section filtering, independent slot ownership, Icon2/sidecar placement, bounded texture readiness, owner/world replay, cached-preview polling and cached/post-worker semantic bone copying. Model/material CSV and loose overlays are supported. Weapon-side contracts and remaining policy cases are open. Production cache/copy and lifecycle fixtures pass; rendered appearance, full transition recovery and performance remain unverified. |
| `wxl-waist-models` | Native attachment-53 buckle path implemented with nonblocking texture readiness, bounded retries, slot cleanup and owner identity checks. Path/name/retry fixture and native build pass; appearance and lifecycle acceptance remain unverified. |
| `wxl-item-physics` | Partial: portable PHYS/Domino core and native scene/model adapter are implemented and compile/fixture-verified. Private virtual-source association and rendered/lifecycle/performance acceptance remain open. |
| `wxl-back-sheath-flip`, `wxl-loot-beam`, `wxl-treewind`, `wxl-render-modern` | Not ported. Back-sheath flip, loot beams and grass motion are enabled by the checked profile; tree wind is disabled. |
| `wxl-gameobject-editor`, `wxl-character-animation-lab`, `wxl-character-customizer`, `wxl-gear-recolor` | Not ported. UI/editor behavior requires native UI/event integration. |
| `wxl-combat-readability`, `wxl-quest-marker`, `wxl-selcircle-poc`, `wxl-selection-circle`, `wxl-unit-outline` | Not ported. Quest and outline paths are disabled in the checked profile; the others remain unavailable until separately ported. |
| `wxl-db2`, `wxl-living-azeroth`, `wxl-underwear`, `wxl-goblin-glider`, `wxl-lone-wolf`, `wxl-npcbot-enchant`, `wxl-npcbot-targets`, `wxl-required-addons` | Not ported. These require separate native API, gameplay, or addon-contract review before they can be claimed supported. |
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
