// Native port of the supplied wxl-goblin-glider (GPL-3.0-or-later, Copyright (C) 2026 WarcraftXL);
// see WxlGoblinGlider.h. Authorization, the movement gate and the fall-curve transform are the
// module's own (Engine/Source/ModernAssets/WxlGoblinGlider); the native movement calls are made on
// the reconstructed CMovement_C instead of fixed addresses.
#include "WoW/Source/Object/Linux/WxlGoblinGlider.h"

#if defined(WOWDECOMP_LINUX)

#include <cmath>
#include <cstdint>
#include <cstring>

#include "Engine/Source/FrameScript/FrameScript.h"
#include "Engine/Source/Model2/M2Data.h"
#include "Engine/Source/Model2/M2Model.h"
#include "Engine/Source/Model2/M2Shared.h"
#include "Engine/Source/ModernAssets/NativeDiagnostic.h"
#include "Engine/Source/ModernAssets/NativeVisualHooks.h"
#include "Engine/Source/ModernAssets/WxlGoblinGlider/GlidePolicy.hpp"
#include "Engine/Source/ModernAssets/WxlGoblinGlider/GliderVisualPolicy.hpp"
#include "Engine/Source/Os/TimeManager.h"
#include "Engine/Source/lua-5.1/src/lauxlib.h"
#include "Engine/Source/lua-5.1/src/lua.h"
#include "WoW/Source/Object/Movement.h"
#include "WoW/Source/Object/ObjectClient/Unit_C.h"
#include "WoW/Source/ObjectMgrClient/ObjectMgrClient.h"
#include "WoW/Source/Ui/Linux/WxlGameScripts.h"
#include "WoW/Source/WorldClient/Linux/WxlVisualEnvironment.h"

namespace {

namespace glide = wxl::glider;

bool g_enabled = false;
bool g_hasGrant = false;
bool g_hasCustomFall = false;
glide::Grant g_grant;
glide::FallBoundary g_boundary;
// Valid only during the authorized local player's current step; every other unit's step zeroes it.
float g_terminalSink = 0.0f;

uint32_t Now() { return OsGetAsyncTimeMs(); }

CGUnit_C* ActivePlayer() {
    const uint64_t guid = ClntObjMgrGetActivePlayer().m_guid;
    return guid ? static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(WOWGUID{guid}, TYPE_PLAYER, __FILE__, __LINE__)) : nullptr;
}

glide::NativeSample Sample(const CMovement_C& movement) {
    return {movement.m_flags, movement.m_fallTimeMs, movement.m_currentSpeed, movement.m_facing, movement.m_fallVelocity,
            movement.m_transportGuid.m_guid != 0};
}

float Velocity(const CMovement_C& movement, float terminal) {
    if (terminal == 7.0f) return movement.GetFeatherFallVelocity();
    return glide::ScaledFeatherVelocity(movement.m_fallTimeMs, movement.m_fallVelocity, terminal);
}

bool SetTerminal(CMovement_C* movement, uint64_t guid, float desired) {
    const auto address = reinterpret_cast<uintptr_t>(movement);
    if (g_boundary.active && !g_boundary.SameTrajectory(address, guid, Sample(*movement), movement->m_fallStartZ)) {
        g_boundary.Clear();  // Landing, another knockback or a native mode change already owns the new fall.
    }
    const float previous = g_boundary.active ? g_boundary.terminal : 7.0f;
    // One native rebase per terminal-mode boundary, preserving signed velocity even while ascending.
    if (!glide::ApplyTerminalBoundary(
            previous, desired, [movement](float sink) { return Velocity(*movement, sink); },
            [movement](float velocity) { return movement->StartFalling(velocity) != 0; }, [] { g_grant.DidRebaseFall(); })) {
        return false;
    }
    if (desired == 7.0f) g_boundary.Clear();
    else g_boundary.Capture(address, guid, Sample(*movement), movement->m_fallStartZ, desired);
    g_hasCustomFall = g_boundary.active;
    return true;
}

// ---------------------------------------------------------------------------------------------
// Glider attachment: Open once, then Opened
// ---------------------------------------------------------------------------------------------

namespace visual {

constexpr uint32_t kOpen = 148;
constexpr uint32_t kOpened = 149;
constexpr uint32_t kAttachment = 19;
constexpr const char* kPaths[] = {
    "creature\\dinkleglider\\dinkleglider.m2",
    "creature\\dinkleglider\\dinklegliderclassic.m2",
    "creature\\dinkleglider\\dinkleglideroutland.m2",
    "creature\\dinkleglider\\dinkleglidernorthrend.m2",
    "creature\\dinkleglider\\dinklegliderbeginner.m2",
};

struct Identity {
    const CM2Shared* shared = nullptr;
    const CM2Model* parent = nullptr;
    uint32_t openingDuration = 0;
};

struct Entry {
    const CM2Model* model = nullptr;  // identity key only; never dereferenced from here
    Identity identity;
    glide::OpeningSchedule schedule;
    uint32_t lastSeen = 0;
};
Entry g_entries[256];

bool ReadIdentity(const CM2Model* model, Identity& identity) {
    if (!model->m_attachParent || model->m_attachmentId != kAttachment || !(model->m_fFlags & 1) || !model->m_shared || !model->m_shared->m_data) {
        return false;
    }
    bool gliderPath = false;
    for (const char* path : kPaths) {
        if (strcasecmp(model->m_shared->m_fileName, path) == 0) {
            gliderPath = true;
            break;
        }
    }
    if (!gliderPath) return false;
    const M2Data* data = model->m_shared->m_data;
    if (data->m_magic != 0x3032444D || data->m_version != 264 || data->m_sequences.m_count > 16 || data->m_sequences.m_count < 3 ||
        !data->m_sequenceIdxHashById.m_count || data->m_sequenceIdxHashById.m_count > 128) {
        return false;
    }
    uint32_t duration = 0;
    bool opened = false;
    for (uint32_t i = 0; i < data->m_sequences.m_count; ++i) {
        const M2Sequence& sequence = data->m_sequences[i];
        if (sequence.m_id == kOpen) duration = sequence.m_duration;
        if (sequence.m_id == kOpened) opened = true;
    }
    if (!opened || !duration || duration > 5000) return false;
    identity = {model->m_shared, model->m_attachParent, duration};
    return true;
}

void Play(CM2Model* model, uint32_t animation) {
    // The setter's sixth argument selects blend history, not repetition. Open's final pose matches
    // Opened's first pose, so do not blend from the finished opening sequence.
    model->SetBoneSequence(0xFFFFFFFF, animation, 0xFFFFFFFF, 0, 1.0f, 0, 1);
}

void Forget(CM2Model* model) {
    for (Entry& entry : g_entries) {
        if (entry.model == model) entry = {};
    }
}

void Reset() {
    for (Entry& entry : g_entries) entry = {};
}

void Update(CM2Model* model) {
    Identity identity;
    if (!ReadIdentity(model, identity)) return;
    const uint32_t now = Now();
    Entry* target = nullptr;
    for (Entry& entry : g_entries) {
        if (entry.model == model) {
            target = &entry;
            break;
        }
        if (!target && (!entry.model || now - entry.lastSeen > 120000)) target = &entry;
    }
    if (!target) return;  // Bounded visual cache; the model's native Stand alias already provides Opened.
    if (target->model != model || target->identity.shared != identity.shared || target->identity.parent != identity.parent) {
        *target = {model, identity, {}, now};
    }
    target->lastSeen = now;
    const glide::VisualAction action = target->schedule.Tick(now, identity.openingDuration);
    if (action != glide::VisualAction::None) Play(model, action == glide::VisualAction::Open ? kOpen : kOpened);
}

}  // namespace visual

// ---------------------------------------------------------------------------------------------
// Lua
// ---------------------------------------------------------------------------------------------

bool UInt32Argument(lua_State* L, int index, uint32_t& value) {
    if (!lua_isnumber(L, index)) return false;
    const double number = lua_tonumber(L, index);
    if (!std::isfinite(number) || number < 0 || number > 4294967295.0 || std::floor(number) != number) return false;
    value = static_cast<uint32_t>(number);
    return true;
}

int Start(lua_State* L) {
    uint32_t token = 0, duration = 0, landingMs = 0;
    bool accepted = false;
    if (g_enabled && UInt32Argument(L, 1, token) && lua_isnumber(L, 2) && UInt32Argument(L, 3, duration) && lua_isnumber(L, 4) &&
        UInt32Argument(L, 5, landingMs)) {
        const double speed = lua_tonumber(L, 2);
        const double sink = lua_tonumber(L, 4);
        const uint64_t guid = ClntObjMgrGetActivePlayer().m_guid;
        if (std::isfinite(speed) && speed >= 7.0 && speed <= 40.0 && std::isfinite(sink) && sink >= 3.0 && sink <= 15.0 && guid &&
            ActivePlayer()) {
            accepted = g_grant.Start(token, guid, static_cast<float>(speed), duration, static_cast<float>(sink), Now(), landingMs);
            if (accepted) g_hasGrant = true;
        }
    }
    lua_pushboolean(L, accepted ? 1 : 0);
    return 1;
}

int ObserveAura(lua_State* L) {
    const bool allowed = lua_isnumber(L, 1) && lua_tonumber(L, 1) == 1.0;
    g_grant.ObserveAura(allowed, Now());
    if (g_grant.phase == glide::Phase::Ended) g_hasGrant = false;
    return 0;
}

int Stop(lua_State* L) {
    uint32_t token = 0;
    if (UInt32Argument(L, 1, token)) {
        g_grant.Stop(token);
        if (g_grant.phase == glide::Phase::Ended) g_hasGrant = false;
    }
    return 0;
}

int Reset(lua_State* L) {
    const uint32_t oldToken = g_grant.phase == glide::Phase::Active || g_grant.phase == glide::Phase::Pending ? g_grant.id : 0;
    g_grant.End();
    g_hasGrant = false;
    // /reload must restore a tuned native fall on the next movement step, not abandon its anchor.
    lua_pushnumber(L, oldToken);
    return 1;
}

int Status(lua_State* L) {
    lua_pushboolean(L, g_enabled ? 1 : 0);
    lua_pushnumber(L, g_grant.id);
    lua_pushnumber(L, static_cast<uint32_t>(g_grant.phase));
    return 3;
}

constexpr char kWatcher[] = R"lua(
local resetToken = WarcraftXL_GlideReset()
if type(CreateFrame) ~= "function" or type(UnitBuff) ~= "function" then return end
local f = CreateFrame("Frame")
local inWorld = type(IsLoggedIn) == "function" and IsLoggedIn() or false
local ready, token, acked, elapsed, helloAt = false, nil, false, 0, -10
local function Send(message)
    local name = UnitName("player")
    if inWorld and name then SendAddonMessage("WXLGLIDE", message, "WHISPER", name) end
end
if resetToken and resetToken > 0 then Send("CANCEL|" .. resetToken) end
local function Clear(notify)
    if token then
        WarcraftXL_GlideStop(token)
        if notify then Send("CANCEL|" .. token) end
    end
    token, acked = nil, false
end
local function Refresh()
    local allowed = inWorld and not UnitIsDeadOrGhost("player") and not IsMounted() and
        not UnitOnTaxi("player") and not IsSwimming() and
        not (UnitInVehicle and UnitInVehicle("player"))
    local aura = false
    if allowed then
        for index = 1, 40 do
            local name, _, _, _, _, _, _, _, _, _, spell = UnitBuff("player", index)
            if not name then break end
            if spell == 126389 then aura = true; break end
        end
    end
    WarcraftXL_GlideAura(aura and 1 or 0)
    if not allowed then Clear(true) end
    local hooks, nativeToken, phase = WarcraftXL_GlideStatus()
    if inWorld and hooks and not ready and GetTime() - helloAt >= 5 then
        helloAt = GetTime()
        Send("HELLO|5")
    end
    if token and nativeToken == token then
        if phase == 2 and not acked then
            acked = true
            Send("ACK|" .. token)
        elseif phase == 3 then
            Clear(true)
        end
    end
end
f:RegisterEvent("CHAT_MSG_ADDON")
f:RegisterEvent("UNIT_AURA")
f:RegisterEvent("PLAYER_ENTERING_WORLD")
f:RegisterEvent("PLAYER_LEAVING_WORLD")
f:RegisterEvent("PLAYER_LOGOUT")
f:RegisterEvent("PLAYER_DEAD")
f:RegisterEvent("PLAYER_ALIVE")
f:RegisterEvent("UNIT_ENTERED_VEHICLE")
f:RegisterEvent("UNIT_EXITED_VEHICLE")
f:SetScript("OnEvent", function(self, event, a, b, c, d)
    if event == "CHAT_MSG_ADDON" then
        if a ~= "WXLGLIDE" or c ~= "WHISPER" or d ~= UnitName("player") then return end
        if b == "READY|5" then ready = true; return end
        local stop = string.match(b, "^STOP|(%d+)$")
        if stop then
            if token == tonumber(stop) then Clear(false) end
            return
        end
        if not ready or not inWorld or token then return end
        local id, speed, duration, sink, landing = string.match(b,
            "^START|(%d+)|([%d%.]+)|(%d+)|([%d%.]+)|(%d+)$")
        id, speed, duration, sink = tonumber(id), tonumber(speed), tonumber(duration), tonumber(sink)
        landing = tonumber(landing)
        if id and speed and duration and sink and landing and
            WarcraftXL_GlideStart(id, speed, duration, sink, landing) then
            token, acked = id, false
            Refresh()
        end
        return
    elseif event == "PLAYER_ENTERING_WORLD" then
        Clear(false)
        WarcraftXL_GlideReset()
        inWorld, ready, helloAt = true, false, -10
    elseif event == "PLAYER_LEAVING_WORLD" or event == "PLAYER_LOGOUT" then
        Clear(true)
        WarcraftXL_GlideReset()
        inWorld, ready = false, false
    elseif event == "UNIT_AURA" or event == "UNIT_ENTERED_VEHICLE" or event == "UNIT_EXITED_VEHICLE" then
        if a ~= "player" then return end
    end
    Refresh()
end)
f:SetScript("OnUpdate", function(self, delta)
    elapsed = elapsed + delta
    if elapsed < 0.05 then return end
    elapsed = 0
    Refresh()
end)
Refresh()
)lua";

}  // namespace

// ---------------------------------------------------------------------------------------------
// Movement step
// ---------------------------------------------------------------------------------------------

float WxlGliderTerminalSink() { return g_terminalSink; }

WxlGliderStepScope::WxlGliderStepScope(CMovement_C* movement, uint32_t now, uint32_t delta) : m_movement(movement), m_previousSink(g_terminalSink) {
    // A nested step for another unit must never inherit this player's override.
    g_terminalSink = 0.0f;
    if (!g_enabled || (!g_hasGrant && !g_hasCustomFall)) return;
    m_engaged = true;

    // All other units and all ungranted movement pass directly through the original engine.
    const CGUnit_C* player = ActivePlayer();
    m_guid = ClntObjMgrGetActivePlayer().m_guid;
    const CMovement_C* local = player ? player->m_movement : nullptr;
    m_local = movement && movement == local;
    if (!m_local || !delta) return;

    bool boundaryValid = true;
    const bool steered = glide::Steer(
        g_grant, m_guid, Sample(*movement), Now(),
        [&] {
            boundaryValid = SetTerminal(movement, m_guid, g_grant.terminalSink);
            if (boundaryValid) {
                movement->UpdateAnchors(0);
                // A short native contact may replace impulse speed with walk speed. Restore only the
                // server-granted horizontal component after rebasing past travel. Gravity, fall
                // time/height, collision, flags and position remain native.
                movement->m_currentSpeed = g_grant.HorizontalSpeed();
            }
        },
        [&] {
            if (boundaryValid) movement->CalcDirection();
        });
    if (!boundaryValid) g_grant.End();
    if (steered) {
        g_terminalSink = boundaryValid ? g_grant.terminalSink : 0.0f;
    } else if (g_boundary.active) {
        if (g_boundary.SameTrajectory(reinterpret_cast<uintptr_t>(movement), m_guid, Sample(*movement), movement->m_fallStartZ)) {
            SetTerminal(movement, m_guid, 7.0f);
        } else {
            g_boundary.Clear();
        }
        g_hasCustomFall = g_boundary.active;
    }
    if (g_grant.phase == glide::Phase::Ended) g_hasGrant = false;
    if (g_boundary.active) {
        if (g_boundary.SameTrajectory(reinterpret_cast<uintptr_t>(movement), m_guid, Sample(*movement), movement->m_fallStartZ)) {
            g_boundary.lastTime = movement->m_fallTimeMs;
        } else {
            g_boundary.Clear();
        }
        g_hasCustomFall = g_boundary.active;
    }
}

WxlGliderStepScope::~WxlGliderStepScope() {
    // The stock step has run: native input, integration, falling, collision, landing and packets intact.
    if (m_engaged && m_local) {
        if (g_grant.phase == glide::Phase::Active) g_grant.Prepare(m_guid, Sample(*m_movement), Now());
        if (g_grant.phase == glide::Phase::Ended) g_hasGrant = false;
    }
    g_terminalSink = m_previousSink;
}

void WxlRegisterGoblinGliderLua() {
    g_enabled = WxlProfileEnabled("WXL_GOBLIN_GLIDER", true);
    if (!g_enabled) return;
    FrameScript_RegisterFunction("WarcraftXL_GlideStart", &Start);
    FrameScript_RegisterFunction("WarcraftXL_GlideStop", &Stop);
    FrameScript_RegisterFunction("WarcraftXL_GlideReset", &Reset);
    FrameScript_RegisterFunction("WarcraftXL_GlideAura", &ObserveAura);
    FrameScript_RegisterFunction("WarcraftXL_GlideStatus", &Status);
    WxlRegisterGameScript("WarcraftXL_GoblinGlider", kWatcher);
    NativeVisualHooks::gliderUpdate = &visual::Update;
    NativeVisualHooks::gliderForget = &visual::Forget;
    WxlNativeDiagnostic("goblin-glider: native step ready; waiting for server grant and aura 126389");
}

void WxlGoblinGliderWorldReset() {
    g_grant.Reset();
    g_boundary.Clear();
    g_hasGrant = false;
    g_hasCustomFall = false;
    visual::Reset();
}

#endif  // WOWDECOMP_LINUX
