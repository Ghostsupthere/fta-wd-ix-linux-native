// Native port of the supplied wxl-character-animation-lab (GPL-3.0-or-later, Copyright (C) 2026
// WarcraftXL). The Lua contract, the substitution priorities and the Wraith Walk shoulder hold follow
// scripts/wxl-character-animation-lab; see WxlAnimationLab.h for how the core events and the attack
// detour became hooks. The Windows module reads the M2 header and unit memory defensively because it
// sees raw 32-bit memory; here the same facts come from the reconstructed CM2Model / M2Data / CGUnit_C
// types, so the SEH guards and header relocation checks are not needed.
#include "WoW/Source/Ui/Linux/WxlAnimationLab.h"

#if defined(WOWDECOMP_LINUX)

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

#include "Engine/Source/FrameScript/FrameScript.h"
#include "Engine/Source/Model2/M2Data.h"
#include "Engine/Source/Model2/M2Model.h"
#include "Engine/Source/Model2/M2Shared.h"
#include "Engine/Source/ModernAssets/NativeDiagnostic.h"
#include "Engine/Source/ModernAssets/NativeVisualHooks.h"
#include "Engine/Source/ModernAssets/WxlAnimationLab/AnimationLabRules.hpp"
#include "Engine/Source/Os/TimeManager.h"
#include "Engine/Source/lua-5.1/src/lauxlib.h"
#include "Engine/Source/lua-5.1/src/lua.h"
#include "WoW/Common/ComponentCore/CharacterComponent.h"
#include "WoW/Source/DB/DBClient/DBClient.h"
#include "WoW/Source/DB/Rec/AnimationDataRec.h"
#include "WoW/Source/DB/Rec/SpellDurationRec.h"
#include "WoW/Source/DB/Rec/SpellRec.h"
#include "WoW/Source/DB/Rec/SpellVisualKitRec.h"
#include "WoW/Source/DB/Rec/SpellVisualRec.h"
#include "WoW/Source/Magic/MagicClient/Spell_C.h"
#include "WoW/Source/Object/ObjectClient/Unit_C.h"
#include "WoW/Source/ObjectMgrClient/ObjectMgrClient.h"
#include "WoW/Source/WorldClient/Linux/WxlVisualEnvironment.h"

namespace {

namespace rules = wxl::animlab;
using rules::PlayableModel;
using rules::SequenceWeaponPolicy;

constexpr uint32_t kMaxSequenceCount = 4096;
constexpr uint32_t kMaxLookupCount = 4096;
constexpr uint32_t kApiVersion = 6;
constexpr uint32_t kCapabilities = 511;
constexpr uint32_t kMissingAnimation = 0xFFFFFFFFu;
constexpr uint32_t kSpellVisualCacheSize = 256;
constexpr uint32_t kAnimationNameCacheSize = 1024;
constexpr uint32_t kMotionOverrideCapacity = 256;
constexpr uint32_t kSequenceRedirectCapacity = 512;
constexpr uint32_t kPendingWeaponPolicyCapacity = 16;
constexpr uint32_t kSpellPhaseOverrideCapacity = 4;
constexpr uint32_t kMinimumMaintainTtlMs = 1;
constexpr uint32_t kMaximumMaintainTtlMs = 60000;
constexpr uint32_t kWraithShoulderHoldAnimationId = 1076;
constexpr uint32_t kWraithRightShoulderKeyBone = 3;
constexpr uint32_t kWraithHoldCapacity = 64;
constexpr uint32_t kMaxAuraScan = 256;
constexpr uint32_t kMd20Magic = 0x3032444D;  // 'MD20'
constexpr uint32_t kClientM2Version = 264;
constexpr uint32_t kSequenceExternalFlag = 0x20;

bool g_enabled = false;

struct LiveModel {
    CGUnit_C* player = nullptr;
    CM2Model* model = nullptr;  // the animation root, CGUnit_C::m_worldModel
    M2Data* data = nullptr;
    const uint16_t* lookup = nullptr;
    uint32_t sequenceCount = 0;
    uint32_t lookupCount = 0;
    uint32_t classId = 0;
    uint32_t raceId = 0;
    uint32_t genderId = 0;
    const PlayableModel* playable = nullptr;
    bool ownerTypeValid = false;
    bool playerOwner = false;
    bool playerClassValid = false;
    bool identityValid = false;  // the unit's character component agrees about race/gender
    char path[rules::kModelPathCapacity] = {};
    char diagnostic[384] = {};
};

struct VisualPhase {
    uint32_t kitId = 0;
    int32_t startAnimation = -1;
    int32_t animation = -1;

    int32_t EffectiveAnimation() const { return animation >= 0 ? animation : startAnimation; }
};

struct SpellVisualInfo {
    bool found = false;
    uint32_t visualId = 0;
    uint32_t alternateVisualId = 0;
    VisualPhase precast, cast, channel, impact, state, stateDone;
    VisualPhase alternatePrecast, alternateCast, alternateChannel, alternateImpact, alternateState, alternateStateDone;
    uint32_t durationIndex = 0;
    int32_t durationMs = 0;
    int32_t maxDurationMs = 0;
};

struct SpellVisualCacheEntry {
    uint32_t spellId = 0;
    SpellVisualInfo info;
    bool occupied = false;
};

struct AnimationNameCacheEntry {
    uint32_t animationId = 0;
    bool occupied = false;
    bool found = false;
    char name[96] = {};
};

struct ModelIdentity {
    CGUnit_C* player = nullptr;
    CM2Model* model = nullptr;
    M2Data* data = nullptr;
    uint32_t raceId = 0;
    uint32_t genderId = 0;
    char path[rules::kModelPathCapacity] = {};
};

struct MaintainedAnimation {
    bool active = false;
    uint32_t animationId = 0;
    uint32_t sequenceIndex = 0;
    uint32_t variation = 0;
    uint32_t duration = 0;
    uint32_t policy = 0;
    uint32_t deadlineMs = 0;
    uint32_t generation = 0;
    CGUnit_C* player = nullptr;
    CM2Model* model = nullptr;
};

struct DirectPlayback {
    bool active = false;
    uint32_t animationId = 0;
    uint32_t generation = 0;
    CGUnit_C* player = nullptr;
    CM2Model* model = nullptr;
};

struct MotionOverride {
    bool occupied = false;
    uint32_t rawId = 0;
    uint32_t resolvedId = 0;
    uint32_t targetId = 0;
    uint32_t generation = 0;
};

struct SequenceRedirect {
    bool occupied = false;
    uint32_t sourceId = 0;
    uint32_t targetId = 0;
    uint32_t weaponPolicy = 0;
    uint32_t generation = 0;
};

struct PendingWeaponPolicy {
    bool occupied = false;
    uint64_t requestToken = 0;
    uint32_t sourceId = 0;
    uint32_t policy = 0;
    uint32_t generation = 0;
    CGUnit_C* player = nullptr;
    CM2Model* model = nullptr;
};

struct SpellPhaseOverride {
    bool occupied = false;
    bool matched = false;
    uint32_t spellId = 0;
    uint32_t phase = 0;
    uint32_t sourceId = 0;
    uint32_t targetId = 0;
    uint32_t deadlineMs = 0;
    uint32_t generation = 0;
    uint64_t matchedRequestToken = 0;
    CGUnit_C* player = nullptr;
    CM2Model* model = nullptr;
};

struct LastMotion {
    bool valid = false;
    uint32_t rawId = 0;
    uint32_t resolvedId = 0;
    uint32_t finalId = 0;
    uint32_t flags = 0;
    uint32_t observedAtMs = 0;
    uint32_t generation = 0;
};

struct WraithShoulderHold {
    uint64_t guid = 0;
    CM2Model* model = nullptr;
    M2Data* data = nullptr;
    uint32_t bone = 0;
    uint32_t originalSheath = 0;
};

// Mirrors the core's M2SetBoneSequenceArgs / UnitSetAnimationCompleteArgs.
struct SetBoneSequenceArgs {
    CM2Model* instance;
    CGUnit_C* unit;
    uint64_t requestToken;
    uint32_t rawAnimationId;
    uint32_t rawFlags;
    uint32_t slot;
    uint32_t requestedAnimationId;
    uint32_t* animationId;
    bool fromUnitAnimation;
};

struct UnitSetAnimationCompleteArgs {
    CGUnit_C* unit;
    CM2Model* root;
    uint64_t requestToken;
    uint32_t rawAnimationId;
    uint32_t rawFlags;
};

struct UnitAnimationScope {
    CGUnit_C* unit = nullptr;
    CM2Model* root = nullptr;
    uint64_t requestToken = 0;
    uint32_t animationId = 0;
    uint32_t flags = 0;
};

std::array<SpellVisualCacheEntry, kSpellVisualCacheSize> g_spellVisualCache = {};
uint32_t g_nextSpellVisualCacheEntry = 0;
std::array<AnimationNameCacheEntry, kAnimationNameCacheSize> g_animationNameCache = {};
uint32_t g_nextAnimationNameCacheEntry = 0;
std::array<MotionOverride, kMotionOverrideCapacity> g_motionOverrides = {};
std::array<SequenceRedirect, kSequenceRedirectCapacity> g_sequenceRedirects = {};
std::array<PendingWeaponPolicy, kPendingWeaponPolicyCapacity> g_pendingWeaponPolicies = {};
std::array<SpellPhaseOverride, kSpellPhaseOverrideCapacity> g_spellPhaseOverrides = {};
std::array<WraithShoulderHold, kWraithHoldCapacity> g_wraithShoulderHolds = {};
ModelIdentity g_identity;
MaintainedAnimation g_maintained;
DirectPlayback g_directPlayback;
LastMotion g_lastMotion;
uint32_t g_modelGeneration = 0;
uint32_t g_safePostureId = 0;
uint32_t g_safePostureFlags = 0;
uint32_t g_combatClassId = 0;
uint32_t g_restoreBypassDepth = 0;
bool g_identityValid = false;
bool g_combatEnabled = false;
bool g_sequenceRedirectsEnabled = false;
bool g_weaponPolicyFaultLogged = false;
bool g_weaponPolicyOverflowLogged = false;

bool g_lastInfoLogValid = false;
bool g_lastInfoLogReady = false;
char g_lastInfoLog[512] = {};

constexpr uint32_t kMaxUnitAnimationScopeDepth = 8;
UnitAnimationScope g_unitAnimationScopes[kMaxUnitAnimationScopeDepth] = {};
uint32_t g_unitAnimationScopeDepth = 0;
uint32_t g_unitAnimationScopeOverflow = 0;
uint32_t g_unitAnimationRequestSerial = 0;

uint32_t Now() { return OsGetAsyncTimeMs(); }

uint64_t ActivePlayerGuid() { return ClntObjMgrGetActivePlayer().m_guid; }

CGUnit_C* PlayerByGuid(uint64_t guid) {
    if (!guid) return nullptr;
    return static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(WOWGUID{guid}, TYPE_PLAYER, __FILE__, __LINE__));
}

bool IsPlayerUnit(const CGUnit_C* unit) { return unit && unit->m_obj && (unit->m_obj->m_type & TYPE_PLAYER); }

// ---------------------------------------------------------------------------------------------
// Auras and the hover presentation
// ---------------------------------------------------------------------------------------------

// A helpful (non-harmful) Levitate or Wraith Walk aura on a player.
bool HasHoverMovementAura(CGUnit_C* owner, uint32_t requiredSpell = 0) {
    if (!IsPlayerUnit(owner)) return false;
    const uint32_t count = owner->GetAuraCount();
    if (count > kMaxAuraScan) return false;
    for (uint32_t index = 0; index < count; ++index) {
        const AuraData* aura = owner->GetAura(index);
        // The harmful flag is bit 7 of the aura flags.
        if (!aura || (aura->m_flags & 0x80u)) continue;
        const uint32_t spellId = static_cast<uint32_t>(aura->m_spellId);
        if ((spellId == rules::kLevitateSpellId || spellId == rules::kWraithWalkSpellId) &&
            (!requiredSpell || spellId == requiredSpell)) {
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------------------------
// Model resolution
// ---------------------------------------------------------------------------------------------

void ReadComponentIdentity(LiveModel& live) {
    const CCharacterComponent* component = live.player->m_characterComponent;
    // The component must still describe this unit's own model, not a recycled one.
    if (!component || component->m_data.m_preferences.m_raceID > 36 || component->m_data.m_preferences.m_sexID > 1 ||
        component->m_data.m_model != live.model) {
        return;
    }
    live.identityValid = true;
    live.raceId = component->m_data.m_preferences.m_raceID;
    live.genderId = component->m_data.m_preferences.m_sexID;
}

bool ReadPlayerClass(const CGUnit_C* player, uint32_t& classId, bool* typeValid, bool* playerOwner) {
    classId = 0;
    if (typeValid) *typeValid = false;
    if (playerOwner) *playerOwner = false;
    if (!player || !player->m_obj || !player->m_unit) return false;
    if (typeValid) *typeValid = true;
    const bool isPlayer = (player->m_obj->m_type & TYPE_PLAYER) != 0;
    if (playerOwner) *playerOwner = isPlayer;
    if (!isPlayer) return false;
    classId = player->m_unit->m_unitFieldBytes0[1];
    return rules::IsPlayableClass(classId);
}

void ClearModelRuntime() {
    g_maintained = {};
    g_directPlayback = {};
    g_motionOverrides.fill(MotionOverride{});
    g_sequenceRedirects.fill(SequenceRedirect{});
    g_pendingWeaponPolicies.fill(PendingWeaponPolicy{});
    g_spellPhaseOverrides.fill(SpellPhaseOverride{});
    g_lastMotion = {};
    g_safePostureId = 0;
    g_safePostureFlags = 0;
    g_combatClassId = 0;
    g_combatEnabled = false;
    g_sequenceRedirectsEnabled = false;
}

bool SameIdentity(const LiveModel& live) {
    return g_identityValid && g_identity.player == live.player && g_identity.model == live.model &&
           g_identity.data == live.data && g_identity.raceId == live.raceId && g_identity.genderId == live.genderId &&
           rules::CompareNoCase(g_identity.path, live.path) == 0;
}

void AcceptIdentity(const LiveModel& live) {
    if (SameIdentity(live)) return;
    ClearModelRuntime();
    g_identity = {};
    g_identity.player = live.player;
    g_identity.model = live.model;
    g_identity.data = live.data;
    g_identity.raceId = live.raceId;
    g_identity.genderId = live.genderId;
    std::snprintf(g_identity.path, sizeof(g_identity.path), "%s", live.path);
    g_identityValid = true;
    ++g_modelGeneration;
    if (!g_modelGeneration) ++g_modelGeneration;
    WxlNativeDiagnostic("animation-lab: active model generation %u: %s %s '%s'", g_modelGeneration,
                        live.playable ? live.playable->raceName : "unknown",
                        live.playable ? live.playable->genderName : "unknown", live.path);
}

void InvalidateIdentity() {
    ClearModelRuntime();
    g_identity = {};
    g_identityValid = false;
}

bool ResolveModelRoot(CGUnit_C* owner, CM2Model* instance, bool acceptLocalIdentity, bool requirePlayerClass, LiveModel& out,
                      const char*& reason, bool allowHoverFallback = false) {
    out = {};
    reason = "A playable character model is not currently available";

    out.player = owner;
    out.model = instance;
    if (!out.player || !out.model) {
        reason = "The player model is not ready";
        return false;
    }
    if (out.player->m_worldModel != out.model) {
        reason = "The animation root is no longer owned by that player";
        return false;
    }
    out.playerClassValid = ReadPlayerClass(out.player, out.classId, &out.ownerTypeValid, &out.playerOwner);
    if (!out.playerClassValid && requirePlayerClass) {
        reason = "The player class update field is unavailable or unsupported";
        return false;
    }
    if (!(out.model->m_fFlags & 1) || !out.model->m_shared || !out.model->m_shared->m_data) {
        reason = "The character M2 is not parsed yet";
        return false;
    }

    // The scene-node/component identity is only an extra race/gender consistency check: it is
    // legitimately absent while an in-world unit model is being assembled, so it must never be
    // required just to browse or play an otherwise canonical character model.
    ReadComponentIdentity(out);

    std::snprintf(out.path, sizeof(out.path), "%s", out.model->m_shared->m_fileName);
    out.playable = rules::ClassifyPlayablePath(out.path);
    if (!out.playable && allowHoverFallback) out.playable = rules::ClassifyHoverFallbackPath(out.path);
    if (!out.playable) {
        std::snprintf(out.diagnostic, sizeof(out.diagnostic),
                      "This character model is not supported by Animation Studio (observed: %s)",
                      out.path[0] ? out.path : "<empty>");
        reason = out.diagnostic;
        return false;
    }
    if (out.identityValid && (out.raceId != out.playable->raceId ||
                              (out.genderId != out.playable->genderId && !(allowHoverFallback && out.raceId == 17)))) {
        std::snprintf(out.diagnostic, sizeof(out.diagnostic),
                      "The model path and character owner disagree (path=%s/%s, owner race=%u gender=%u)",
                      out.playable->raceName, out.playable->genderName, out.raceId, out.genderId);
        reason = out.diagnostic;
        return false;
    }
    if (!out.identityValid) {
        out.raceId = out.playable->raceId;
        out.genderId = out.playable->genderId;
    }

    out.data = out.model->m_shared->m_data;
    if (out.data->m_magic != kMd20Magic || out.data->m_version != kClientM2Version) {
        std::snprintf(out.diagnostic, sizeof(out.diagnostic),
                      "The active playable model is not client-shaped MD20 v264 (magic=0x%08X version=%u)",
                      out.data->m_magic, out.data->m_version);
        reason = out.diagnostic;
        return false;
    }

    out.sequenceCount = out.data->m_sequences.m_count;
    out.lookupCount = out.data->m_sequenceIdxHashById.m_count;
    if (!out.sequenceCount || out.sequenceCount > kMaxSequenceCount || !out.lookupCount || out.lookupCount > kMaxLookupCount) {
        std::snprintf(out.diagnostic, sizeof(out.diagnostic),
                      "The active model has an invalid animation table size (sequences=%u lookup=%u)", out.sequenceCount,
                      out.lookupCount);
        reason = out.diagnostic;
        return false;
    }
    out.lookup = out.data->m_sequenceIdxHashById.Data();

    if (acceptLocalIdentity) AcceptIdentity(out);
    reason = "ready";
    return true;
}

bool ResolvePlayerModel(uint64_t guid, bool local, LiveModel& out, const char*& reason) {
    CGUnit_C* player = PlayerByGuid(guid);
    if (!player) {
        out = {};
        reason = guid ? "The player object is not visible" : "No player GUID";
        return false;
    }
    return ResolveModelRoot(player, player->m_worldModel, local, !local, out, reason);
}

bool ResolveLiveModel(LiveModel& out, const char*& reason) { return ResolvePlayerModel(ActivePlayerGuid(), true, out, reason); }

bool ReadSequence(const LiveModel& live, uint32_t index, M2Sequence& sequence) {
    if (index >= live.sequenceCount || !live.data) return false;
    sequence = live.data->m_sequences[index];
    return true;
}

bool FindSequenceByAnimationId(const LiveModel& live, uint32_t animationId, uint32_t& index, M2Sequence& sequence) {
    const int32_t found = rules::FindSequenceByAnimationId(live.lookup, live.lookupCount, live.sequenceCount, animationId,
                                                           [&](uint32_t i) { return live.data->m_sequences[i].m_id; });
    if (found < 0) return false;
    index = static_cast<uint32_t>(found);
    sequence = live.data->m_sequences[index];
    return true;
}

// The AnimationData id currently playing on the root bone.
bool ReadCurrentAnimationId(const LiveModel& live, uint32_t& animationId) {
    M2BoneSequenceInfo info = {};
    if (!live.model->GetBoneSequenceInfo(0xFFFFFFFF, info) || info.m_sequenceId == 0xFFFFFFFF) return false;
    animationId = info.m_sequenceId;
    return true;
}

bool CurrentSequenceIs(const LiveModel& live, uint32_t animationId) {
    uint32_t current = 0;
    return ReadCurrentAnimationId(live, current) && current == animationId;
}

// ---------------------------------------------------------------------------------------------
// Spell and animation metadata (the client's own loaded DBC tables)
// ---------------------------------------------------------------------------------------------

int32_t NormalizeAnimation(int32_t animation) { return static_cast<uint32_t>(animation) == kMissingAnimation ? -1 : animation; }

VisualPhase ReadVisualPhase(int32_t kitId) {
    VisualPhase phase;
    phase.kitId = static_cast<uint32_t>(kitId);
    if (kitId <= 0) return phase;
    const SpellVisualKitRec* kit = g_spellVisualKitDB.GetRecord(kitId);
    if (kit) {
        phase.startAnimation = NormalizeAnimation(kit->m_startAnimID);
        phase.animation = NormalizeAnimation(kit->m_animID);
    }
    return phase;
}

bool ReadVisual(int32_t visualId, VisualPhase& precast, VisualPhase& cast, VisualPhase& channel, VisualPhase& impact,
                VisualPhase& state, VisualPhase& stateDone) {
    if (visualId <= 0) return false;
    const SpellVisualRec* visual = g_spellVisualDB.GetRecord(visualId);
    if (!visual) return false;
    precast = ReadVisualPhase(visual->m_precastKit);
    cast = ReadVisualPhase(visual->m_castKit);
    impact = ReadVisualPhase(visual->m_impactKit);
    state = ReadVisualPhase(visual->m_stateKit);
    stateDone = ReadVisualPhase(visual->m_stateDoneKit);
    channel = ReadVisualPhase(visual->m_channelKit);
    return true;
}

SpellVisualInfo QuerySpellVisual(uint32_t spellId) {
    for (const SpellVisualCacheEntry& entry : g_spellVisualCache) {
        if (entry.occupied && entry.spellId == spellId) return entry.info;
    }

    auto cacheResult = [spellId](const SpellVisualInfo& result) {
        SpellVisualCacheEntry& entry = g_spellVisualCache[g_nextSpellVisualCacheEntry++ % g_spellVisualCache.size()];
        entry.spellId = spellId;
        entry.info = result;
        entry.occupied = true;
        return result;
    };

    SpellVisualInfo info;
    SpellRec spell;
    if (spellId > static_cast<uint32_t>(std::numeric_limits<int32_t>::max()) ||
        !g_spellDB.GetRecord(static_cast<int32_t>(spellId), &spell)) {
        return cacheResult(info);
    }

    const int32_t primaryVisual = spell.m_spellVisualID[0];
    const int32_t alternateVisual = spell.m_spellVisualID[1];
    info.visualId = static_cast<uint32_t>(primaryVisual ? primaryVisual : alternateVisual);
    info.alternateVisualId = primaryVisual ? static_cast<uint32_t>(alternateVisual) : 0;

    const bool primaryFound = ReadVisual(static_cast<int32_t>(info.visualId), info.precast, info.cast, info.channel, info.impact,
                                         info.state, info.stateDone);
    const bool alternateFound = ReadVisual(static_cast<int32_t>(info.alternateVisualId), info.alternatePrecast, info.alternateCast,
                                           info.alternateChannel, info.alternateImpact, info.alternateState,
                                           info.alternateStateDone);

    info.durationIndex = static_cast<uint32_t>(spell.m_durationIndex);
    if (info.durationIndex) {
        const SpellDurationRec* duration = g_spellDurationDB.GetRecord(static_cast<int32_t>(info.durationIndex));
        if (duration) {
            info.durationMs = duration->m_duration;
            info.maxDurationMs = duration->m_maxDuration;
        }
    }

    info.found = primaryFound || alternateFound;
    return cacheResult(info);
}

bool QueryAnimationName(uint32_t animationId, const char*& name) {
    name = nullptr;
    for (AnimationNameCacheEntry& entry : g_animationNameCache) {
        if (entry.occupied && entry.animationId == animationId) {
            if (entry.found) name = entry.name;
            return entry.found;
        }
    }

    AnimationNameCacheEntry& entry = g_animationNameCache[g_nextAnimationNameCacheEntry++ % g_animationNameCache.size()];
    entry = {};
    entry.animationId = animationId;
    entry.occupied = true;
    const AnimationDataRec* rec = g_animationDataDB.GetRecord(static_cast<int32_t>(animationId));
    if (rec && rec->m_name && rec->m_name[0]) {
        std::snprintf(entry.name, sizeof(entry.name), "%s", rec->m_name);
        entry.found = true;
        name = entry.name;
    }
    return entry.found;
}

// ---------------------------------------------------------------------------------------------
// Playback, leases and rule tables
// ---------------------------------------------------------------------------------------------

bool PreviewSequence(LiveModel& live, const M2Sequence& sequence, bool loop) {
    if (!(live.model->m_fFlags & 1)) return false;
    // Request the record's real AnimationData id. The native resolver verifies that the sequence
    // record carries the requested id, so an unrelated lookup slot cannot select an arbitrary record.
    live.model->SetBoneSequence(0xFFFFFFFF, sequence.m_id, 0xFFFFFFFF, 0, 1.0f, loop ? 1 : 0, 1);
    return true;
}

void RememberDirectPlayback(const LiveModel& live, uint32_t animationId) {
    // Direct preview/playback owns the root until Stop. It supersedes a maintained lease without
    // restoring first because the direct sequence has already become authoritative.
    g_maintained = {};
    g_directPlayback.active = true;
    g_directPlayback.animationId = animationId;
    g_directPlayback.generation = g_modelGeneration;
    g_directPlayback.player = live.player;
    g_directPlayback.model = live.model;
}

bool RestoreSafePosture(uint32_t generation, CGUnit_C* player, CM2Model* model, uint32_t postureId, uint32_t postureFlags,
                        uint32_t ownedTargetId) {
    LiveModel live;
    const char* reason = nullptr;
    if (!ResolveLiveModel(live, reason) || g_modelGeneration != generation || live.player != player || live.model != model) {
        return false;
    }

    uint32_t currentId = 0;
    const bool hasCurrent = ReadCurrentAnimationId(live, currentId);
    const bool protectedCurrent = hasCurrent && rules::IsProtectedMotion(currentId) && currentId != ownedTargetId;
    const bool protectedRequest = g_lastMotion.valid && g_lastMotion.generation == generation &&
                                  rules::IsProtectedMotion(g_lastMotion.rawId) &&
                                  (!hasCurrent || currentId == g_lastMotion.finalId || rules::IsProtectedMotion(currentId)) &&
                                  (!hasCurrent || currentId != ownedTargetId);
    const bool unrelatedActive = hasCurrent && ownedTargetId != (std::numeric_limits<uint32_t>::max)() &&
                                 currentId != ownedTargetId && !rules::IsIdleReadyAnimation(currentId);
    // Releasing ownership must not turn death, locomotion, swimming, or a mount transition into
    // Stand. It also must not interrupt an attack/spell that an idle-ready aura lease deliberately
    // did not claim. The lease is already gone, so that request stays authoritative.
    if (protectedCurrent || protectedRequest || unrelatedActive) return true;

    ++g_restoreBypassDepth;
    live.player->PlayBaseAnimation(postureId, static_cast<int32_t>(postureFlags));
    --g_restoreBypassDepth;
    return true;
}

bool MaintainedTargetValid(const LiveModel& live, M2Sequence& sequence, uint32_t& index) {
    return g_maintained.active && g_maintained.generation == g_modelGeneration && g_maintained.player == live.player &&
           g_maintained.model == live.model && FindSequenceByAnimationId(live, g_maintained.animationId, index, sequence);
}

bool ShouldStartMaintained(const LiveModel& live, uint32_t policy, uint32_t targetId) {
    uint32_t currentId = 0;
    const bool hasCurrent = ReadCurrentAnimationId(live, currentId);
    if (hasCurrent && currentId == targetId) return false;

    if (policy == 1) {
        if (hasCurrent) return rules::IsIdleReadyAnimation(currentId);
        return g_lastMotion.valid && g_lastMotion.generation == g_modelGeneration &&
               rules::IsIdleReadyAnimation(g_lastMotion.rawId);
    }

    if (g_lastMotion.valid && g_lastMotion.generation == g_modelGeneration && rules::IsProtectedMotion(g_lastMotion.rawId)) {
        return false;
    }
    return !hasCurrent || !rules::IsProtectedMotion(currentId);
}

void ExpireMaintained(bool restore) {
    if (!g_maintained.active) return;
    const MaintainedAnimation expired = g_maintained;
    const uint32_t postureId = g_safePostureId;
    const uint32_t postureFlags = g_safePostureFlags;
    g_maintained = {};
    if (restore) RestoreSafePosture(expired.generation, expired.player, expired.model, postureId, postureFlags, expired.animationId);
}

template <typename T, size_t N>
T* FreeSlot(std::array<T, N>& table) {
    for (T& slot : table) {
        if (!slot.occupied) return &slot;
    }
    return nullptr;
}

MotionOverride* FindMotionOverride(uint32_t rawId, uint32_t resolvedId) {
    for (MotionOverride& rule : g_motionOverrides) {
        if (rule.occupied && rule.rawId == rawId && rule.resolvedId == resolvedId) return &rule;
    }
    return nullptr;
}

SequenceRedirect* FindSequenceRedirect(uint32_t sourceId) {
    for (SequenceRedirect& rule : g_sequenceRedirects) {
        if (rule.occupied && rule.sourceId == sourceId) return &rule;
    }
    return nullptr;
}

PendingWeaponPolicy* FindPendingWeaponPolicy(uint64_t requestToken) {
    if (!requestToken) return nullptr;
    for (PendingWeaponPolicy& pending : g_pendingWeaponPolicies) {
        if (pending.occupied && pending.requestToken == requestToken) return &pending;
    }
    return nullptr;
}

SpellPhaseOverride* FindSpellPhaseOverride(uint32_t spellId, uint32_t phase) {
    for (SpellPhaseOverride& rule : g_spellPhaseOverrides) {
        if (rule.occupied && rule.spellId == spellId && rule.phase == phase) return &rule;
    }
    return nullptr;
}

void ExpireSpellPhaseOverrides(uint32_t now) {
    for (SpellPhaseOverride& rule : g_spellPhaseOverrides) {
        if (rule.occupied && rules::DeadlineReached(now, rule.deadlineMs)) rule = {};
    }
}

SpellPhaseOverride* MatchSpellPhaseOverride(const LiveModel& live, uint32_t rawId, uint64_t requestToken, uint32_t now) {
    ExpireSpellPhaseOverrides(now);
    for (SpellPhaseOverride& rule : g_spellPhaseOverrides) {
        if (!rule.occupied || rule.generation != g_modelGeneration || rule.player != live.player || rule.model != live.model) {
            continue;
        }
        if (rule.matched) {
            if (rule.matchedRequestToken == requestToken) return &rule;
            continue;
        }
        if (rule.sourceId == rawId && requestToken) {
            rule.matched = true;
            rule.matchedRequestToken = requestToken;
            return &rule;
        }
    }
    return nullptr;
}

void QueuePendingWeaponPolicy(uint64_t requestToken, const LiveModel& live, uint32_t sourceId, uint32_t policy) {
    PendingWeaponPolicy* destination = FindPendingWeaponPolicy(requestToken);
    if (!rules::RequiresPostWeaponPolicy(policy)) {
        if (destination) *destination = {};
        return;
    }

    if (!requestToken || !live.player || !live.model) return;
    if (!destination) destination = FreeSlot(g_pendingWeaponPolicies);
    if (!destination) {
        if (!g_weaponPolicyOverflowLogged) {
            g_weaponPolicyOverflowLogged = true;
            WxlNativeDiagnostic("animation-lab: pending weapon policy capacity exhausted; action skipped");
        }
        return;
    }

    destination->occupied = true;
    destination->requestToken = requestToken;
    destination->sourceId = sourceId;
    destination->policy = policy;
    destination->generation = g_modelGeneration;
    destination->player = live.player;
    destination->model = live.model;
}

bool ResolveTarget(const LiveModel& live, uint32_t targetId) {
    uint32_t index = 0;
    M2Sequence sequence = {};
    return FindSequenceByAnimationId(live, targetId, index, sequence);
}

bool ApplyHoverSequence(const SetBoneSequenceArgs& args) {
    if (!HasHoverMovementAura(args.unit)) return false;
    const bool wraith = HasHoverMovementAura(args.unit, rules::kWraithWalkSpellId);
    if (!rules::IsHoverRequest(args.rawAnimationId, wraith)) return false;
    LiveModel live;
    const char* reason = nullptr;
    if (!ResolveModelRoot(args.unit, args.instance, false, false, live, reason, true)) return false;
    // Broken has Hover. Tuskarr instead glides in its native Stand pose.
    const uint32_t target = ResolveTarget(live, rules::kHoverAnimationId) ? rules::kHoverAnimationId : 0;
    if (!ResolveTarget(live, target)) return false;
    *args.animationId = target;
    return true;
}

// ---------------------------------------------------------------------------------------------
// Wraith Walk shoulder hold
// ---------------------------------------------------------------------------------------------

bool ReadWraithHoldOwner(const CGUnit_C* owner, uint64_t& guid, uint32_t& sheath) {
    if (!IsPlayerUnit(owner)) return false;
    guid = owner->m_obj->m_guid.m_guid;
    // The client-local visual weapon state; SetSheatheState compares and writes it.
    sheath = static_cast<uint32_t>(owner->m_sheatheState);
    return guid != 0 && sheath <= 2;
}

// Modern playable rigs carry 287-291 key-bone entries; the key bone must resolve to a real bone.
bool ResolveWraithShoulderBone(const LiveModel& live, uint32_t& bone) {
    const M2Data* data = live.data;
    if (data->m_boneIndicesById.m_count <= kWraithRightShoulderKeyBone || data->m_boneIndicesById.m_count > 4096 ||
        !data->m_bones.m_count || data->m_bones.m_count > 4096) {
        return false;
    }
    const uint16_t resolved = data->m_boneIndicesById.Data()[kWraithRightShoulderKeyBone];
    if (resolved == 0 || resolved >= data->m_bones.m_count) return false;
    bone = resolved;
    return true;
}

bool MatchesWraithHold(const WraithShoulderHold& hold, const LiveModel& live) {
    return hold.model == live.model && hold.data == live.data;
}

bool OwnsWraithShoulderSequence(const WraithShoulderHold& hold, const LiveModel& live) {
    if (hold.bone >= live.data->m_bones.m_count) return false;
    M2BoneSequenceInfo info = {};
    return live.model->GetBoneSequenceInfo(kWraithRightShoulderKeyBone, info) && info.m_sequenceId == kWraithShoulderHoldAnimationId;
}

WraithShoulderHold* FindWraithShoulderHold(uint64_t guid) {
    for (WraithShoulderHold& hold : g_wraithShoulderHolds) {
        if (hold.guid == guid) return &hold;
    }
    return nullptr;
}

void ReleaseWraithShoulderHold(WraithShoulderHold& slot, bool restoreSheath) {
    const WraithShoulderHold hold = slot;
    slot = {};
    if (!hold.guid) return;
    ++g_restoreBypassDepth;
    CGUnit_C* owner = PlayerByGuid(hold.guid);
    LiveModel live;
    const char* reason = nullptr;
    if (owner && ResolveModelRoot(owner, owner->m_worldModel, false, false, live, reason, true) && MatchesWraithHold(hold, live)) {
        if (OwnsWraithShoulderSequence(hold, live)) {
            // A missing sequence unsets this limb only. Do not reset the body or erase a newer
            // cast/attack's arm sequence.
            live.model->SetBoneSequence(kWraithRightShoulderKeyBone, kMissingAnimation, kMissingAnimation, 0, 0.0f, 0, 1);
        }
        uint64_t guid = 0;
        uint32_t sheath = 0;
        if (restoreSheath && hold.originalSheath != 1 && ReadWraithHoldOwner(owner, guid, sheath) && guid == hold.guid &&
            sheath == 1) {
            owner->SetSheatheState(hold.originalSheath, 1, 1);
        }
    }
    --g_restoreBypassDepth;
}

void ApplyWraithShoulderHold(const UnitSetAnimationCompleteArgs& args) {
    uint64_t guid = 0;
    uint32_t sheath = 0;
    if (!ReadWraithHoldOwner(args.unit, guid, sheath)) return;
    WraithShoulderHold* hold = FindWraithShoulderHold(guid);
    const bool hoverRequest = rules::IsHoverRequest(args.rawAnimationId, true);
    if (!hoverRequest || !HasHoverMovementAura(args.unit, rules::kWraithWalkSpellId)) {
        if (hold) ReleaseWraithShoulderHold(*hold, hoverRequest);
        return;
    }
    LiveModel live;
    const char* reason = nullptr;
    uint32_t bone = 0;
    if (!ResolveModelRoot(args.unit, args.root, false, false, live, reason, true) ||
        !ResolveTarget(live, kWraithShoulderHoldAnimationId) || !ResolveWraithShoulderBone(live, bone)) {
        return;
    }
    if (hold && !MatchesWraithHold(*hold, live)) {
        *hold = {};
        hold = nullptr;
    }
    if (!hold) {
        hold = FindWraithShoulderHold(0);
        if (!hold) return;
        *hold = WraithShoulderHold{guid, live.model, live.data, bone, sheath};
    }
    ++g_restoreBypassDepth;
    // Draw visually only. No CMSG_SET_SHEATHED, extra unit animation, cast kit or channel.
    args.unit->SetSheatheState(1, 1, 1);
    // Key bone 3 includes the shoulder and its right-arm descendants. Hover retains the spine,
    // legs, left arm and root; the native pose has only one idle variation.
    live.model->SetBoneSequence(kWraithRightShoulderKeyBone, kWraithShoulderHoldAnimationId, kMissingAnimation, 0, 0.15f, 1, 1);
    --g_restoreBypassDepth;
}

void UpdateWraithShoulderHolds(bool releaseAll = false) {
    for (WraithShoulderHold& hold : g_wraithShoulderHolds) {
        if (!hold.guid) continue;
        CGUnit_C* owner = PlayerByGuid(hold.guid);
        LiveModel live;
        const char* reason = nullptr;
        if (!owner || !ResolveModelRoot(owner, owner->m_worldModel, false, false, live, reason, true) ||
            !MatchesWraithHold(hold, live)) {
            hold = {};
            continue;
        }
        if (releaseAll || !HasHoverMovementAura(owner, rules::kWraithWalkSpellId)) {
            uint32_t currentId = 0;
            const bool restoreSheath = releaseAll || (ReadCurrentAnimationId(live, currentId) && rules::IsHoverRequest(currentId, true));
            ReleaseWraithShoulderHold(hold, restoreSheath);
        }
    }
}

bool IsLocalWraithChannel(const CGUnit_C* owner) {
    if (!owner || owner != PlayerByGuid(ActivePlayerGuid()) || !owner->m_unit) return false;
    // The attack validator reads the channel spell from the unit descriptor; m_castSpellId is the
    // client's own record of the active cast.
    return owner->m_unit->m_unitChannelSpell == rules::kWraithWalkSpellId &&
           static_cast<uint32_t>(owner->m_castSpellId) == rules::kWraithWalkSpellId;
}

// ---------------------------------------------------------------------------------------------
// Substitution
// ---------------------------------------------------------------------------------------------

bool CouldBeLocalRoot(CGUnit_C* player, CM2Model* model) {
    if (!player || !model) return false;
    if (g_identityValid) {
        if (player != g_identity.player) return false;
        if (model == g_identity.model) return true;
        return player->m_worldModel == model;
    }
    const uint64_t guid = ActivePlayerGuid();
    if (!guid) return false;
    CGUnit_C* active = PlayerByGuid(guid);
    return active == player && active->m_worldModel == model;
}

bool ApplySequenceWeaponPolicy(CGUnit_C* player, uint32_t policy) {
    uint32_t sheathState = 0;
    switch (static_cast<SequenceWeaponPolicy>(policy)) {
        case SequenceWeaponPolicy::Inherit:
        case SequenceWeaponPolicy::LegacyNoAction:
            return true;
        case SequenceWeaponPolicy::LegacyFlag4:
        case SequenceWeaponPolicy::Sheath:
            // Build 12340 routes both weapon flags through sheath state 0. The flag-4 path's
            // additional undocumented unit-state cleanup is intentionally omitted.
            sheathState = 0;
            break;
        case SequenceWeaponPolicy::Draw:
            sheathState = 1;
            break;
        default:
            return false;
    }
    if (!player) return false;

    ++g_restoreBypassDepth;
    // Completion runs after the stock request has applied the source row's weapon flags. Override
    // only this client's visual state and suppress CMSG_SET_SHEATHED.
    player->SetSheatheState(sheathState, 1, 1);
    --g_restoreBypassDepth;
    return true;
}

void ApplyRemoteMappedSequence(const SetBoneSequenceArgs& args) {
    if (!g_identityValid || !args.animationId) return;

    MotionOverride* motionRule = nullptr;
    if (g_combatEnabled && args.rawAnimationId <= 505 && args.requestedAnimationId <= (std::numeric_limits<uint16_t>::max)()) {
        motionRule = FindMotionOverride(args.rawAnimationId, args.requestedAnimationId);
        if (motionRule && motionRule->generation != g_modelGeneration) motionRule = nullptr;
    }

    SequenceRedirect* redirectRule = nullptr;
    if (g_sequenceRedirectsEnabled) {
        redirectRule = FindSequenceRedirect(args.rawAnimationId);
        if (redirectRule && redirectRule->generation != g_modelGeneration) redirectRule = nullptr;
    }
    if (!motionRule && !redirectRule) return;

    // Resolve the event's actual owner rather than the active-player GUID. Scope by the exact
    // body-model root and one of the canonical playable paths; this intentionally adds no
    // NPC/NPCBot type gate. Presentation-only Tuskarr/Broken paths still fail closed.
    LiveModel live;
    const char* reason = nullptr;
    if (!ResolveModelRoot(args.unit, args.instance, false, false, live, reason) || live.raceId != g_identity.raceId ||
        live.genderId != g_identity.genderId) {
        return;
    }

    // Combat tables remain class profiles for real players. A non-player owner is not gated per
    // policy, so a supported matching model may still use the table.
    const bool combatOwnerMatches = live.ownerTypeValid && (!live.playerOwner || (live.playerClassValid && live.classId == g_combatClassId));
    if (motionRule && combatOwnerMatches && ResolveTarget(live, motionRule->targetId)) {
        *args.animationId = motionRule->targetId;
        return;
    }

    if (redirectRule && ResolveTarget(live, redirectRule->targetId)) {
        *args.animationId = redirectRule->targetId;
        QueuePendingWeaponPolicy(args.requestToken, live, args.rawAnimationId, redirectRule->weaponPolicy);
    }
    // A target miss belongs only to this transient remote model. Never erase the shared
    // active-profile rule; the local model (or another matching player) may still support it.
}

const UnitAnimationScope* CurrentUnitAnimationScope() {
    if (g_unitAnimationScopeOverflow != 0 || g_unitAnimationScopeDepth == 0) return nullptr;
    return &g_unitAnimationScopes[g_unitAnimationScopeDepth - 1];
}

void OnSetBoneSequence(const SetBoneSequenceArgs& args) {
    if (g_restoreBypassDepth || !args.fromUnitAnimation || !args.animationId) return;

    if (ApplyHoverSequence(args)) return;

    if (!CouldBeLocalRoot(args.unit, args.instance)) {
        if (rules::IsLevitateMovement(args.rawAnimationId) && HasHoverMovementAura(args.unit)) {
            LiveModel remote;
            const char* reason = nullptr;
            // Canonical model classification excludes Tuskarr/Broken race illusions too.
            if (ResolveModelRoot(args.unit, args.instance, false, false, remote, reason) &&
                ResolveTarget(remote, rules::kHoverAnimationId)) {
                *args.animationId = rules::kHoverAnimationId;
                return;
            }
        }
        ApplyRemoteMappedSequence(args);
        return;
    }

    LiveModel live;
    const char* reason = nullptr;
    if (!ResolveLiveModel(live, reason) || args.unit != live.player || args.instance != live.model) return;

    const uint32_t now = Now();
    if (g_maintained.active && rules::DeadlineReached(now, g_maintained.deadlineMs)) g_maintained = {};

    const uint32_t rawId = args.rawAnimationId;
    const uint32_t resolvedId = args.requestedAnimationId;
    uint32_t finalId = *args.animationId;
    const bool levitatingMovement = rules::IsLevitateMovement(rawId) && HasHoverMovementAura(live.player) &&
                                    ResolveTarget(live, rules::kHoverAnimationId);
    if (levitatingMovement) finalId = rules::kHoverAnimationId;
    if (rules::IsIdleReadyAnimation(rawId)) {
        g_safePostureId = rawId;
        g_safePostureFlags = args.rawFlags;
    }

    bool spellClaimed = false;
    SpellPhaseOverride* spellRule = levitatingMovement ? nullptr : MatchSpellPhaseOverride(live, rawId, args.requestToken, now);
    if (spellRule) {
        if (ResolveTarget(live, spellRule->targetId)) {
            finalId = spellRule->targetId;
            spellClaimed = true;
        } else {
            *spellRule = {};
        }
    }

    bool leaseClaimed = false;
    bool exclusiveLease = false;
    if (!levitatingMovement && !spellClaimed && g_maintained.active && g_maintained.generation == g_modelGeneration &&
        g_maintained.player == live.player && g_maintained.model == live.model) {
        uint32_t leaseIndex = 0;
        M2Sequence leaseSequence = {};
        if (!MaintainedTargetValid(live, leaseSequence, leaseIndex)) {
            g_maintained = {};
        } else if (g_maintained.policy == 0) {
            exclusiveLease = true;
            // The raw outer Unit request is the authoritative behavior family. A custom model may
            // resolve that safe spell/posture request to a later-expansion ID whose numeric value
            // has no WotLK motion semantics.
            if (!rules::IsProtectedMotion(rawId)) {
                finalId = g_maintained.animationId;
                leaseClaimed = true;
            }
        } else if (rules::IsIdleReadyAnimation(rawId)) {
            finalId = g_maintained.animationId;
            leaseClaimed = true;
        }
    }

    bool combatClaimed = false;
    if (!levitatingMovement && !spellClaimed && !leaseClaimed && !exclusiveLease && g_combatEnabled && rawId <= 505 &&
        resolvedId <= (std::numeric_limits<uint16_t>::max)()) {
        MotionOverride* rule = FindMotionOverride(rawId, resolvedId);
        if (rule && rule->generation == g_modelGeneration) {
            if (ResolveTarget(live, rule->targetId)) {
                finalId = rule->targetId;
                combatClaimed = true;
            } else {
                *rule = {};
            }
        }
    }

    if (!levitatingMovement && !spellClaimed && !leaseClaimed && !exclusiveLease && !combatClaimed && g_sequenceRedirectsEnabled) {
        SequenceRedirect* rule = FindSequenceRedirect(rawId);
        if (rule && rule->generation == g_modelGeneration) {
            if (ResolveTarget(live, rule->targetId)) {
                finalId = rule->targetId;
                QueuePendingWeaponPolicy(args.requestToken, live, rawId, rule->weaponPolicy);
            } else {
                *rule = {};
            }
        }
    }

    *args.animationId = finalId;
    g_lastMotion.valid = true;
    g_lastMotion.rawId = rawId;
    g_lastMotion.resolvedId = resolvedId;
    g_lastMotion.finalId = finalId;
    g_lastMotion.flags = args.rawFlags;
    g_lastMotion.observedAtMs = now;
    g_lastMotion.generation = g_modelGeneration;
}

void OnUnitSetAnimationComplete(const UnitSetAnimationCompleteArgs& args) {
    if (g_restoreBypassDepth) return;

    ApplyWraithShoulderHold(args);
    PendingWeaponPolicy* pending = FindPendingWeaponPolicy(args.requestToken);
    if (!pending) return;

    const PendingWeaponPolicy action = *pending;
    *pending = {};
    if (!rules::RequiresPostWeaponPolicy(action.policy) || action.generation != g_modelGeneration || action.player != args.unit ||
        action.model != args.root || action.sourceId != args.rawAnimationId) {
        return;
    }
    ApplySequenceWeaponPolicy(action.player, action.policy);
}

// The CM2Model::SetBoneSequence entry point (NativeVisualHooks::m2SetBoneSequence).
void HookSetBoneSequence(CM2Model* model, unsigned boneId, unsigned* sequenceId) {
    // Only requests made under an outer unit animation are rewritten; everything else (previews,
    // the lab's own playback, UI models) passes through untouched.
    const UnitAnimationScope* scope = CurrentUnitAnimationScope();
    if (!scope || scope->root != model) return;
    const SetBoneSequenceArgs args{model, scope->unit, scope->requestToken, scope->animationId, scope->flags,
                                   boneId, *sequenceId, sequenceId, true};
    OnSetBoneSequence(args);
}

// ---------------------------------------------------------------------------------------------
// Lua
// ---------------------------------------------------------------------------------------------

bool ReadUnsignedInteger(lua_State* L, int argument, uint32_t minimum, uint32_t maximum, uint32_t& value) {
    if (!L || !lua_isnumber(L, argument)) return false;
    const double requested = lua_tonumber(L, argument);
    if (!std::isfinite(requested) || requested < static_cast<double>(minimum) || requested > static_cast<double>(maximum) ||
        std::floor(requested) != requested) {
        return false;
    }
    value = static_cast<uint32_t>(requested);
    return true;
}

bool ReadGuidString(lua_State* L, int argument, uint64_t& guid) {
    guid = 0;
    if (!L || !lua_isstring(L, argument)) return false;
    size_t length = 0;
    const char* value = lua_tolstring(L, argument, &length);
    if (!value || !length || length >= 32) return false;
    char buffer[32] = {};
    std::memcpy(buffer, value, length);
    char* end = nullptr;
    guid = std::strtoull(buffer, &end, 0);
    return guid && end && end != buffer && *end == '\0';
}

void LogInfoResult(bool ready, const LiveModel& live, const char* reason) {
    char message[512] = {};
    if (ready) {
        std::snprintf(message, sizeof(message), "ready path='%s' owner=%s race=%u gender=%u sequences=%u lookup=%u",
                      live.path[0] ? live.path : "<empty>", live.identityValid ? "valid" : "unavailable", live.raceId,
                      live.genderId, live.sequenceCount, live.lookupCount);
    } else {
        std::snprintf(message, sizeof(message), "unavailable reason='%s' path='%s' owner=%s race=%u gender=%u sequences=%u lookup=%u",
                      reason ? reason : "unknown", live.path[0] ? live.path : "<empty>", live.identityValid ? "valid" : "unavailable",
                      live.raceId, live.genderId, live.sequenceCount, live.lookupCount);
    }
    if (!g_lastInfoLogValid || ready != g_lastInfoLogReady || std::strcmp(message, g_lastInfoLog) != 0) {
        WxlNativeDiagnostic("animation-lab: GetInfo %s", message);
        std::snprintf(g_lastInfoLog, sizeof(g_lastInfoLog), "%s", message);
        g_lastInfoLogReady = ready;
        g_lastInfoLogValid = true;
    }
}

int Fail(lua_State* L) {
    lua_pushboolean(L, 0);
    return 1;
}

int LuaGetInfo(lua_State* L) {
    LiveModel live;
    const char* reason = nullptr;
    const bool ready = ResolveLiveModel(live, reason);
    LogInfoResult(ready, live, reason);
    lua_pushboolean(L, ready ? 1 : 0);
    lua_pushstring(L, ready ? live.path : reason);
    lua_pushnumber(L, ready ? static_cast<double>(live.sequenceCount) : 0.0);
    lua_pushnumber(L, ready ? static_cast<double>(live.lookupCount) : 0.0);
    lua_pushnumber(L, ready ? static_cast<double>(live.raceId) : 0.0);
    lua_pushnumber(L, ready ? static_cast<double>(live.genderId) : 0.0);
    lua_pushstring(L, ready && live.playable ? live.playable->raceName : "");
    lua_pushstring(L, ready && live.playable ? live.playable->genderName : "");
    lua_pushnumber(L, static_cast<double>(g_modelGeneration));
    return 9;
}

int LuaGetVisiblePlayerInfo(lua_State* L) {
    uint64_t guid = 0;
    LiveModel live;
    const char* reason = "Invalid player GUID";
    const bool ready = ReadGuidString(L, 1, guid) && ResolvePlayerModel(guid, false, live, reason);
    lua_pushboolean(L, ready ? 1 : 0);
    lua_pushstring(L, ready ? live.path : reason);
    lua_pushnumber(L, ready ? static_cast<double>(live.classId) : 0.0);
    lua_pushnumber(L, ready ? static_cast<double>(live.raceId) : 0.0);
    lua_pushnumber(L, ready ? static_cast<double>(live.genderId) : 0.0);
    lua_pushstring(L, ready && live.playable ? live.playable->raceName : "");
    lua_pushstring(L, ready && live.playable ? live.playable->genderName : "");
    return 7;
}

int LuaGetSequence(lua_State* L) {
    uint32_t index = 0;
    if (!ReadUnsignedInteger(L, 1, 0, kMaxSequenceCount - 1, index)) return Fail(L);

    LiveModel live;
    const char* reason = nullptr;
    M2Sequence sequence = {};
    if (!ResolveLiveModel(live, reason) || !ReadSequence(live, index, sequence)) return Fail(L);

    lua_pushboolean(L, 1);
    lua_pushnumber(L, static_cast<double>(sequence.m_id));
    lua_pushnumber(L, static_cast<double>(sequence.m_variationIndex));
    lua_pushnumber(L, static_cast<double>(sequence.m_duration));
    lua_pushnumber(L, static_cast<double>(sequence.m_flags));
    lua_pushboolean(L, (sequence.m_flags & kSequenceExternalFlag) ? 0 : 1);
    return 6;
}

int LuaPreview(lua_State* L) {
    uint32_t index = 0;
    if (!ReadUnsignedInteger(L, 1, 0, kMaxSequenceCount - 1, index)) return Fail(L);

    LiveModel live;
    const char* reason = nullptr;
    M2Sequence sequence = {};
    const bool loop = lua_isnumber(L, 2) && lua_tonumber(L, 2) != 0.0;
    const bool played = ResolveLiveModel(live, reason) && ReadSequence(live, index, sequence) && PreviewSequence(live, sequence, loop);
    if (played) RememberDirectPlayback(live, sequence.m_id);

    lua_pushboolean(L, played ? 1 : 0);
    lua_pushnumber(L, played ? static_cast<double>(sequence.m_id) : 0.0);
    lua_pushnumber(L, played ? static_cast<double>(sequence.m_variationIndex) : 0.0);
    lua_pushnumber(L, played ? static_cast<double>(sequence.m_duration) : 0.0);
    return 4;
}

int LuaFindSequence(lua_State* L) {
    uint32_t animationId = 0;
    if (!ReadUnsignedInteger(L, 1, 0, (std::numeric_limits<uint16_t>::max)(), animationId)) return Fail(L);

    LiveModel live;
    const char* reason = nullptr;
    M2Sequence sequence = {};
    uint32_t index = 0;
    const bool found = ResolveLiveModel(live, reason) && FindSequenceByAnimationId(live, animationId, index, sequence);
    lua_pushboolean(L, found ? 1 : 0);
    if (!found) return 1;

    lua_pushnumber(L, static_cast<double>(index));
    lua_pushnumber(L, static_cast<double>(sequence.m_variationIndex));
    lua_pushnumber(L, static_cast<double>(sequence.m_duration));
    lua_pushnumber(L, static_cast<double>(sequence.m_flags));
    lua_pushboolean(L, (sequence.m_flags & kSequenceExternalFlag) ? 0 : 1);
    return 6;
}

int LuaPlay(lua_State* L) {
    uint32_t animationId = 0;
    if (!ReadUnsignedInteger(L, 1, 0, (std::numeric_limits<uint16_t>::max)(), animationId)) return Fail(L);

    LiveModel live;
    const char* reason = nullptr;
    M2Sequence sequence = {};
    uint32_t index = 0;
    const bool loop = lua_isnumber(L, 2) && lua_tonumber(L, 2) != 0.0;
    const bool played = ResolveLiveModel(live, reason) && FindSequenceByAnimationId(live, animationId, index, sequence) &&
                        PreviewSequence(live, sequence, loop);
    if (played) RememberDirectPlayback(live, sequence.m_id);
    lua_pushboolean(L, played ? 1 : 0);
    lua_pushnumber(L, played ? static_cast<double>(index) : 0.0);
    lua_pushnumber(L, played ? static_cast<double>(sequence.m_variationIndex) : 0.0);
    lua_pushnumber(L, played ? static_cast<double>(sequence.m_duration) : 0.0);
    return 4;
}

int LuaPlayVisiblePlayer(lua_State* L) {
    uint64_t guid = 0;
    uint32_t animationId = 0;
    if (!ReadGuidString(L, 1, guid) || !ReadUnsignedInteger(L, 2, 0, (std::numeric_limits<uint16_t>::max)(), animationId) ||
        guid == ActivePlayerGuid()) {
        return Fail(L);
    }

    LiveModel live;
    const char* reason = nullptr;
    M2Sequence sequence = {};
    uint32_t index = 0;
    const bool played = ResolvePlayerModel(guid, false, live, reason) && FindSequenceByAnimationId(live, animationId, index, sequence) &&
                        PreviewSequence(live, sequence, false);
    lua_pushboolean(L, played ? 1 : 0);
    lua_pushnumber(L, played ? static_cast<double>(index) : 0.0);
    lua_pushnumber(L, played ? static_cast<double>(sequence.m_variationIndex) : 0.0);
    lua_pushnumber(L, played ? static_cast<double>(sequence.m_duration) : 0.0);
    return 4;
}

int LuaGetApiVersion(lua_State* L) {
    lua_pushnumber(L, static_cast<double>(kApiVersion));
    lua_pushnumber(L, static_cast<double>(kCapabilities));
    return 2;
}

int LuaGetAnimationName(lua_State* L) {
    uint32_t animationId = 0;
    if (!ReadUnsignedInteger(L, 1, 0, (std::numeric_limits<uint16_t>::max)(), animationId)) return Fail(L);

    const char* name = nullptr;
    const bool found = QueryAnimationName(animationId, name);
    lua_pushboolean(L, found ? 1 : 0);
    if (!found) return 1;
    lua_pushstring(L, name);
    return 2;
}

int LuaMaintain(lua_State* L) {
    uint32_t animationId = 0;
    uint32_t policy = 0;
    uint32_t ttlMs = 0;
    if (!ReadUnsignedInteger(L, 1, 0, (std::numeric_limits<uint16_t>::max)(), animationId) ||
        !ReadUnsignedInteger(L, 2, 0, 1, policy) ||
        !ReadUnsignedInteger(L, 3, kMinimumMaintainTtlMs, kMaximumMaintainTtlMs, ttlMs)) {
        return Fail(L);
    }

    LiveModel live;
    const char* reason = nullptr;
    uint32_t index = 0;
    M2Sequence sequence = {};
    if (!ResolveLiveModel(live, reason) || !FindSequenceByAnimationId(live, animationId, index, sequence)) return Fail(L);

    const uint32_t now = Now();
    if (g_maintained.active && rules::DeadlineReached(now, g_maintained.deadlineMs)) g_maintained = {};
    const bool changed = !g_maintained.active || g_maintained.animationId != animationId || g_maintained.policy != policy ||
                         g_maintained.generation != g_modelGeneration || g_maintained.player != live.player ||
                         g_maintained.model != live.model;
    g_maintained.active = true;
    g_maintained.animationId = animationId;
    g_maintained.sequenceIndex = index;
    g_maintained.variation = sequence.m_variationIndex;
    g_maintained.duration = sequence.m_duration;
    g_maintained.policy = policy;
    g_maintained.deadlineMs = now + ttlMs;
    g_maintained.generation = g_modelGeneration;
    g_maintained.player = live.player;
    g_maintained.model = live.model;

    bool accepted = true;
    // A heartbeat extends only the lease. The initial/changed lease may start directly, but an
    // unchanged lease resumes exclusively through a later engine Stand/Ready request. This avoids
    // repeatedly restarting a sequence (or an async external .anim load).
    if (changed && !CurrentSequenceIs(live, animationId) && ShouldStartMaintained(live, policy, animationId)) {
        accepted = PreviewSequence(live, sequence, true);
    }
    if (!accepted) {
        g_maintained = {};
    } else {
        g_directPlayback = {};
    }

    lua_pushboolean(L, accepted ? 1 : 0);
    lua_pushnumber(L, accepted ? static_cast<double>(index) : 0.0);
    lua_pushnumber(L, accepted ? static_cast<double>(sequence.m_variationIndex) : 0.0);
    lua_pushnumber(L, accepted ? static_cast<double>(sequence.m_duration) : 0.0);
    return 4;
}

int LuaReleaseMaintained(lua_State* L) {
    if (!g_maintained.active) {
        lua_pushboolean(L, 1);
        return 1;
    }

    const MaintainedAnimation released = g_maintained;
    const uint32_t postureId = g_safePostureId;
    const uint32_t postureFlags = g_safePostureFlags;
    g_maintained = {};
    const bool restored = RestoreSafePosture(released.generation, released.player, released.model, postureId, postureFlags,
                                             released.animationId);
    lua_pushboolean(L, restored ? 1 : 0);
    return 1;
}

int LuaSetCombatEnabled(lua_State* L) {
    uint32_t enabled = 0;
    if (!ReadUnsignedInteger(L, 1, 0, 1, enabled)) return Fail(L);

    if (enabled) {
        LiveModel live;
        const char* reason = nullptr;
        if (!ResolveLiveModel(live, reason) || !live.playerClassValid) return Fail(L);
        g_combatClassId = live.classId;
    }
    g_combatEnabled = enabled != 0;
    if (!g_combatEnabled) g_combatClassId = 0;
    lua_pushboolean(L, 1);
    return 1;
}

int LuaClearMotionOverrides(lua_State* L) {
    g_motionOverrides.fill(MotionOverride{});
    lua_pushboolean(L, 1);
    return 1;
}

int LuaSetMotionOverride(lua_State* L) {
    uint32_t rawId = 0;
    uint32_t resolvedId = 0;
    uint32_t targetId = 0;
    if (!ReadUnsignedInteger(L, 1, 0, 505, rawId) || !ReadUnsignedInteger(L, 2, 0, (std::numeric_limits<uint16_t>::max)(), resolvedId) ||
        !ReadUnsignedInteger(L, 3, 0, (std::numeric_limits<uint16_t>::max)(), targetId)) {
        return Fail(L);
    }

    LiveModel live;
    const char* reason = nullptr;
    if (!ResolveLiveModel(live, reason) || !ResolveTarget(live, targetId)) return Fail(L);

    MotionOverride* destination = FindMotionOverride(rawId, resolvedId);
    if (!destination) destination = FreeSlot(g_motionOverrides);
    if (!destination) return Fail(L);

    destination->occupied = true;
    destination->rawId = rawId;
    destination->resolvedId = resolvedId;
    destination->targetId = targetId;
    destination->generation = g_modelGeneration;
    lua_pushboolean(L, 1);
    return 1;
}

int LuaClearSpellPhaseOverrides(lua_State* L) {
    if (lua_gettop(L) >= 2) {
        uint32_t spellId = 0;
        uint32_t phase = 0;
        if (!ReadUnsignedInteger(L, 1, 1, (std::numeric_limits<uint32_t>::max)(), spellId) || !ReadUnsignedInteger(L, 2, 1, 5, phase)) {
            return Fail(L);
        }
        SpellPhaseOverride* rule = FindSpellPhaseOverride(spellId, phase);
        if (rule) *rule = {};
    } else {
        g_spellPhaseOverrides.fill(SpellPhaseOverride{});
    }
    lua_pushboolean(L, 1);
    return 1;
}

int LuaArmSpellPhaseOverride(lua_State* L) {
    uint32_t spellId = 0;
    uint32_t phase = 0;
    uint32_t sourceId = 0;
    uint32_t targetId = 0;
    uint32_t ttlMs = 0;
    if (!ReadUnsignedInteger(L, 1, 1, (std::numeric_limits<uint32_t>::max)(), spellId) || !ReadUnsignedInteger(L, 2, 1, 5, phase) ||
        !ReadUnsignedInteger(L, 3, 0, (std::numeric_limits<uint16_t>::max)(), sourceId) ||
        !ReadUnsignedInteger(L, 4, 0, (std::numeric_limits<uint16_t>::max)(), targetId) ||
        !ReadUnsignedInteger(L, 5, kMinimumMaintainTtlMs, kMaximumMaintainTtlMs, ttlMs)) {
        return Fail(L);
    }

    LiveModel live;
    const char* reason = nullptr;
    if (!ResolveLiveModel(live, reason) || !ResolveTarget(live, targetId)) return Fail(L);

    SpellPhaseOverride* destination = FindSpellPhaseOverride(spellId, phase);
    if (!destination) destination = FreeSlot(g_spellPhaseOverrides);
    if (!destination) return Fail(L);

    destination->occupied = true;
    destination->matched = false;
    destination->spellId = spellId;
    destination->phase = phase;
    destination->sourceId = sourceId;
    destination->targetId = targetId;
    destination->deadlineMs = Now() + ttlMs;
    destination->generation = g_modelGeneration;
    destination->matchedRequestToken = 0;
    destination->player = live.player;
    destination->model = live.model;
    lua_pushboolean(L, 1);
    return 1;
}

int LuaGetSpellPhaseOverrideStatus(lua_State* L) {
    uint32_t spellId = 0;
    uint32_t phase = 0;
    if (!ReadUnsignedInteger(L, 1, 1, (std::numeric_limits<uint32_t>::max)(), spellId) || !ReadUnsignedInteger(L, 2, 1, 5, phase)) {
        return Fail(L);
    }

    ExpireSpellPhaseOverrides(Now());
    SpellPhaseOverride* rule = FindSpellPhaseOverride(spellId, phase);
    lua_pushboolean(L, rule ? 1 : 0);
    if (!rule) return 1;
    lua_pushboolean(L, rule->matched ? 1 : 0);
    lua_pushnumber(L, static_cast<double>(rule->sourceId));
    lua_pushnumber(L, static_cast<double>(rule->targetId));
    return 4;
}

int LuaSetSequenceRedirectsEnabled(lua_State* L) {
    uint32_t enabled = 0;
    if (!ReadUnsignedInteger(L, 1, 0, 1, enabled)) return Fail(L);

    if (enabled) {
        LiveModel live;
        const char* reason = nullptr;
        if (!ResolveLiveModel(live, reason)) return Fail(L);
    }
    g_sequenceRedirectsEnabled = enabled != 0;
    if (!g_sequenceRedirectsEnabled) g_pendingWeaponPolicies.fill(PendingWeaponPolicy{});
    lua_pushboolean(L, 1);
    return 1;
}

int LuaClearSequenceRedirects(lua_State* L) {
    g_sequenceRedirects.fill(SequenceRedirect{});
    g_pendingWeaponPolicies.fill(PendingWeaponPolicy{});
    lua_pushboolean(L, 1);
    return 1;
}

int LuaSetSequenceRedirect(lua_State* L) {
    uint32_t sourceId = 0;
    uint32_t targetId = 0;
    uint32_t weaponPolicy = 0;
    if (!ReadUnsignedInteger(L, 1, 0, (std::numeric_limits<uint16_t>::max)(), sourceId) ||
        !ReadUnsignedInteger(L, 2, 0, (std::numeric_limits<uint16_t>::max)(), targetId)) {
        return Fail(L);
    }
    if (lua_gettop(L) >= 3 && (!ReadUnsignedInteger(L, 3, 0, 32, weaponPolicy) || !rules::IsSequenceWeaponPolicy(weaponPolicy))) {
        return Fail(L);
    }

    LiveModel live;
    const char* reason = nullptr;
    if (!ResolveLiveModel(live, reason) || !ResolveTarget(live, targetId)) return Fail(L);

    SequenceRedirect* destination = FindSequenceRedirect(sourceId);
    if (!destination) destination = FreeSlot(g_sequenceRedirects);
    if (!destination) return Fail(L);

    destination->occupied = true;
    destination->sourceId = sourceId;
    destination->targetId = targetId;
    destination->weaponPolicy = weaponPolicy;
    destination->generation = g_modelGeneration;
    lua_pushboolean(L, 1);
    return 1;
}

int LuaGetLastMotion(lua_State* L) {
    const bool valid = g_lastMotion.valid && g_lastMotion.generation == g_modelGeneration;
    lua_pushboolean(L, valid ? 1 : 0);
    if (!valid) return 1;

    lua_pushnumber(L, static_cast<double>(g_lastMotion.rawId));
    lua_pushnumber(L, static_cast<double>(g_lastMotion.resolvedId));
    lua_pushnumber(L, static_cast<double>(g_lastMotion.finalId));
    lua_pushnumber(L, static_cast<double>(g_lastMotion.flags));
    lua_pushnumber(L, static_cast<double>(Now() - g_lastMotion.observedAtMs));
    return 6;
}

int LuaGetSpellVisual(lua_State* L) {
    uint32_t spellId = 0;
    if (!ReadUnsignedInteger(L, 1, 1, (std::numeric_limits<uint32_t>::max)(), spellId)) return Fail(L);

    const SpellVisualInfo info = QuerySpellVisual(spellId);
    lua_pushboolean(L, info.found ? 1 : 0);
    if (!info.found) return 1;

    auto number = [L](double value) { lua_pushnumber(L, value); };
    auto phaseKit = [&](const VisualPhase& phase) {
        number(static_cast<double>(phase.kitId));
        number(static_cast<double>(phase.startAnimation));
        number(static_cast<double>(phase.animation));
    };
    // Stable v3 contract: primary effective precast/cast, full primary phase kits, SpellDuration
    // metadata, then all six effective alternate-visual phases.
    number(static_cast<double>(info.precast.EffectiveAnimation()));
    number(static_cast<double>(info.cast.EffectiveAnimation()));
    number(static_cast<double>(info.visualId));
    number(static_cast<double>(info.alternateVisualId));
    phaseKit(info.precast);
    phaseKit(info.cast);
    phaseKit(info.channel);
    phaseKit(info.impact);
    phaseKit(info.state);
    phaseKit(info.stateDone);
    number(static_cast<double>(info.durationIndex));
    number(static_cast<double>(info.durationMs));
    number(static_cast<double>(info.maxDurationMs));
    number(static_cast<double>(info.alternatePrecast.EffectiveAnimation()));
    number(static_cast<double>(info.alternateCast.EffectiveAnimation()));
    number(static_cast<double>(info.alternateChannel.EffectiveAnimation()));
    number(static_cast<double>(info.alternateImpact.EffectiveAnimation()));
    number(static_cast<double>(info.alternateState.EffectiveAnimation()));
    number(static_cast<double>(info.alternateStateDone.EffectiveAnimation()));
    return 32;
}

int LuaStop(lua_State* L) {
    LiveModel live;
    const char* reason = nullptr;
    const bool ready = ResolveLiveModel(live, reason);
    if (ready) {
        const uint32_t generation = g_modelGeneration;
        const uint32_t postureId = g_safePostureId;
        const uint32_t postureFlags = g_safePostureFlags;
        uint32_t ownedTargetId = (std::numeric_limits<uint32_t>::max)();
        if (g_maintained.active && g_maintained.generation == generation && g_maintained.player == live.player &&
            g_maintained.model == live.model) {
            ownedTargetId = g_maintained.animationId;
        } else if (g_directPlayback.active && g_directPlayback.generation == generation && g_directPlayback.player == live.player &&
                   g_directPlayback.model == live.model) {
            ownedTargetId = g_directPlayback.animationId;
        }
        g_maintained = {};
        g_directPlayback = {};
        const bool restored = RestoreSafePosture(generation, live.player, live.model, postureId, postureFlags, ownedTargetId);
        lua_pushboolean(L, restored ? 1 : 0);
        return 1;
    }
    g_maintained = {};
    g_directPlayback = {};
    lua_pushboolean(L, 0);
    return 1;
}

}  // namespace

// ---------------------------------------------------------------------------------------------
// Public entry points
// ---------------------------------------------------------------------------------------------

WxlUnitAnimationScope::WxlUnitAnimationScope(CGUnit_C* unit, uint32_t animationId, int32_t flags) : m_pushed(false) {
    if (!g_enabled) return;
    if (g_unitAnimationScopeDepth >= kMaxUnitAnimationScopeDepth) {
        ++g_unitAnimationScopeOverflow;
        return;
    }
    ++g_unitAnimationRequestSerial;
    if (!g_unitAnimationRequestSerial) ++g_unitAnimationRequestSerial;
    g_unitAnimationScopes[g_unitAnimationScopeDepth++] = {unit, unit ? unit->m_worldModel : nullptr, g_unitAnimationRequestSerial,
                                                          animationId, static_cast<uint32_t>(flags)};
    m_pushed = true;
}

WxlUnitAnimationScope::~WxlUnitAnimationScope() {
    if (!g_enabled && !m_pushed) return;
    if (!m_pushed) {
        // An overflowed request never pushed; only its overflow count unwinds.
        if (g_unitAnimationScopeOverflow != 0) --g_unitAnimationScopeOverflow;
        return;
    }
    if (g_unitAnimationScopeDepth == 0) return;
    // Emitted before the scope is popped, after every stock weapon handler has run.
    const UnitAnimationScope& scope = g_unitAnimationScopes[g_unitAnimationScopeDepth - 1];
    const UnitSetAnimationCompleteArgs args{scope.unit, scope.root, scope.requestToken, scope.animationId, scope.flags};
    OnUnitSetAnimationComplete(args);
    g_unitAnimationScopes[--g_unitAnimationScopeDepth] = {};
}

void WxlRegisterAnimationLabLua() {
    g_enabled = WxlProfileEnabled("WXL_ANIMATION_LAB", true);
    if (!g_enabled) return;
    NativeVisualHooks::m2SetBoneSequence = &HookSetBoneSequence;

    FrameScript_RegisterFunction("WarcraftXL_AnimationLabGetApiVersion", &LuaGetApiVersion);
    FrameScript_RegisterFunction("WarcraftXL_AnimationLabGetInfo", &LuaGetInfo);
    FrameScript_RegisterFunction("WarcraftXL_AnimationLabGetVisiblePlayerInfo", &LuaGetVisiblePlayerInfo);
    FrameScript_RegisterFunction("WarcraftXL_AnimationLabGetSequence", &LuaGetSequence);
    FrameScript_RegisterFunction("WarcraftXL_AnimationLabPreview", &LuaPreview);
    FrameScript_RegisterFunction("WarcraftXL_AnimationLabFindSequence", &LuaFindSequence);
    FrameScript_RegisterFunction("WarcraftXL_AnimationLabPlay", &LuaPlay);
    FrameScript_RegisterFunction("WarcraftXL_AnimationLabPlayVisiblePlayer", &LuaPlayVisiblePlayer);
    FrameScript_RegisterFunction("WarcraftXL_AnimationLabMaintain", &LuaMaintain);
    FrameScript_RegisterFunction("WarcraftXL_AnimationLabReleaseMaintained", &LuaReleaseMaintained);
    FrameScript_RegisterFunction("WarcraftXL_AnimationLabSetCombatEnabled", &LuaSetCombatEnabled);
    FrameScript_RegisterFunction("WarcraftXL_AnimationLabClearMotionOverrides", &LuaClearMotionOverrides);
    FrameScript_RegisterFunction("WarcraftXL_AnimationLabSetMotionOverride", &LuaSetMotionOverride);
    FrameScript_RegisterFunction("WarcraftXL_AnimationLabClearSpellPhaseOverrides", &LuaClearSpellPhaseOverrides);
    FrameScript_RegisterFunction("WarcraftXL_AnimationLabArmSpellPhaseOverride", &LuaArmSpellPhaseOverride);
    FrameScript_RegisterFunction("WarcraftXL_AnimationLabGetSpellPhaseOverrideStatus", &LuaGetSpellPhaseOverrideStatus);
    FrameScript_RegisterFunction("WarcraftXL_AnimationLabSetSequenceRedirectsEnabled", &LuaSetSequenceRedirectsEnabled);
    FrameScript_RegisterFunction("WarcraftXL_AnimationLabClearSequenceRedirects", &LuaClearSequenceRedirects);
    FrameScript_RegisterFunction("WarcraftXL_AnimationLabSetSequenceRedirect", &LuaSetSequenceRedirect);
    FrameScript_RegisterFunction("WarcraftXL_AnimationLabGetLastMotion", &LuaGetLastMotion);
    FrameScript_RegisterFunction("WarcraftXL_AnimationLabGetAnimationName", &LuaGetAnimationName);
    FrameScript_RegisterFunction("WarcraftXL_AnimationLabGetSpellVisual", &LuaGetSpellVisual);
    FrameScript_RegisterFunction("WarcraftXL_AnimationLabStop", &LuaStop);
    WxlNativeDiagnostic("animation-lab: Animation Studio Lua API v%u ready", kApiVersion);
}

void WxlAnimationLabFrame() {
    if (!g_enabled) return;
    UpdateWraithShoulderHolds();
    const uint32_t now = Now();
    ExpireSpellPhaseOverrides(now);
    if (g_maintained.active && rules::DeadlineReached(now, g_maintained.deadlineMs)) ExpireMaintained(true);
}

void WxlAnimationLabWorldLeave() {
    if (!g_enabled) return;
    UpdateWraithShoulderHolds(true);
    InvalidateIdentity();
}

void WxlAnimationLabBeforeAttackCheck(CGUnit_C* owner) {
    // Right-click, AttackTarget, StartAttack and the Attack action share the attack validator. Use
    // the same cancellation path as SpellStopCasting: send the cancel before the attack and let
    // every original attack check run. Passive availability checks and other channels are untouched.
    if (g_enabled && IsLocalWraithChannel(owner)) Spell_C_StopCasting();
}

#endif  // WOWDECOMP_LINUX
