// Native port of the supplied wxl-quest-marker (QuestMarker.cpp) and the quest-objective ring half
// of wxl-selcircle-poc (SelCirclePoc.cpp), GPL-3.0-or-later, Copyright (C) 2026 WarcraftXL. The
// Windows modules hooked Wow.exe by address and drew through the proxy's device; here the Lua API is
// registered natively, the marker pass runs inside the native world pass beside the loot beams, and
// the rings are drawn from CGUnit_C::Render through the engine's own selection-circle path. The
// state, validation and maths are in Engine/Source/ModernAssets/WxlQuestTracker/TrackerState.hpp.
#if defined(WOWDECOMP_LINUX)
#define WOWDECOMP_GX_DXVK
#include <d3d9.h>
#endif
#include "WoW/Source/WorldClient/Linux/WxlQuestTracker.h"
#if defined(WOWDECOMP_LINUX)
#include "Engine/Source/BlizzardCore/SFile2-Core.h"
#include "Engine/Source/FrameScript/FrameScript.h"
#include "Engine/Source/Gx/CGxDeviceD3d9Ex/CGxDeviceD3d9Ex.h"
#include "Engine/Source/Gx/Gx.h"
#include "Engine/Source/ModernAssets/NativeDiagnostic.h"
#include "Engine/Source/ModernAssets/WxlLootBeam/BeamRenderer.hpp"
#include "Engine/Source/ModernAssets/WxlQuestTracker/BlpTexture.hpp"
#include "Engine/Source/ModernAssets/WxlQuestTracker/MarkerRenderer.hpp"
#include "Engine/Source/ModernAssets/WxlQuestTracker/TrackerState.hpp"
#include "Engine/Source/Services/Texture.h"
#include "Engine/Source/Tempest/C44Matrix.h"
#include "Engine/Source/lua-5.1/src/lauxlib.h"
#include "Engine/Source/lua-5.1/src/lua.h"
#include "WoW/Source/Game/GameClient/ProjectTex.h"
#include "WoW/Source/ObjectMgrClient/ObjectMgrClient.h"
#include "WoW/Source/Object/ObjectClient/Linux/WxlSelectionCircle.h"
#include "WoW/Source/Object/ObjectClient/Player_C.h"
#include "WoW/Source/Object/ObjectClient/Unit_C.h"
#include "WoW/Source/Ui/GameUI.h"
#include "WoW/Source/WorldClient/FrameTime.h"
#include "WoW/Source/WorldClient/Linux/WxlVisualEnvironment.h"
#include "WoW/Source/WorldClient/Linux/WxlWorldCamera.h"
#include "WoW/Source/WorldClient/World.h"

#include <cmath>
#include <cstring>
#include <vector>

namespace {
namespace qt = wxl::questtracker;

qt::State g_state;

// The camera of the last drawn frame, in world coordinates, for the Lua label projections (the
// engine's view matrix has the eye at zero; see beam::RebaseWorldView).
WxlWorldCamera g_camera;
int g_framesInWorld = 0;
uint32_t g_ringsThisFrame = 0;

bool Enabled() {
    static const bool enabled = WxlProfileEnabled("WXL_QUEST_RENDERING", false);
    return enabled;
}

// ---------------------------------------------------------------- atlas texture
IDirect3DTexture9* g_atlas = nullptr;
IDirect3DDevice9* g_atlasDevice = nullptr;

bool ReadArchiveFile(const char* path, std::vector<uint8_t>& out) {
    out.clear();
    SFileImpl* file = nullptr;
    if (!SFile::OpenEx(nullptr, path, 0, &file) || !file) return false;
    const uint32_t size = SFileGetFileSize(file, nullptr);
    bool ok = size && size <= 128u * 1024u * 1024u;
    if (ok) {
        out.resize(size);
        ok = SFileReadFile(file, out.data(), size, nullptr, nullptr, 0) != 0;
    }
    SFileCloseFile(file);
    if (!ok) out.clear();
    return ok;
}

IDirect3DTexture9* EnsureAtlas(IDirect3DDevice9* device) {
    if (g_atlas && g_atlasDevice == device) return g_atlas;
    if (g_atlas) { g_atlas->Release(); g_atlas = nullptr; }
    g_atlasDevice = device;
    std::vector<uint8_t> bytes;
    if (ReadArchiveFile("interface\\navigation\\ingamenavigationui.blp", bytes))
        g_atlas = qt::CreateBlpTexture(device, bytes.data(), bytes.size());
    if (!g_atlas) {
        WxlNativeDiagnostic("quest-tracker: navigation atlas unavailable, using the generated icon");
        g_atlas = qt::CreateFallbackTexture(device);
    }
    return g_atlas;
}

// ---------------------------------------------------------------- Lua natives
bool Number(lua_State* L, int index, double& value) {
    if (!lua_isnumber(L, index)) return false;
    value = lua_tonumber(L, index);
    return std::isfinite(value);
}
int Push(lua_State* L, bool value) { lua_pushboolean(L, value ? 1 : 0); return 1; }

bool PlayerPosition(float out[3]) {
    CGPlayer_C* player = ClntObjMgrGetActivePlayerObj();
    if (player) {
        const C3Vector position = player->GetPosition();
        out[0] = position.x; out[1] = position.y; out[2] = position.z;
        return qt::AllFinite(out, 3);
    }
    if (g_camera.valid) { std::memcpy(out, g_camera.eye, sizeof(g_camera.eye)); return true; }
    return false;
}

int SetMarker(lua_State* L) {
    double v[5];
    for (int i = 0; i < 5; ++i)
        if (!Number(L, i + 1, v[i])) return Push(L, false);
    return Push(L, g_state.SetMarker(v[0], v[1], v[2], v[3], v[4]));
}
int ClearMarkers(lua_State* L) { g_state.ClearMarkers(); return Push(L, true); }

int PushNone(lua_State* L) {
    lua_pushnil(L); lua_pushnil(L); lua_pushnil(L); lua_pushboolean(L, 0);
    return 4;
}
// ndcX, ndcY, distance, true for the first active marker that projects on screen.
int GetMarkerInfo(lua_State* L) {
    float player[3];
    const qt::Settings& s = g_state.GetSettings();
    if (!s.enabled || !s.showMarker || g_state.Dead() || g_state.CorpseActive() || !g_camera.valid || !PlayerPosition(player))
        return PushNone(L);
    for (const qt::Marker& m : g_state.Markers()) {
        if (!m.active) continue;
        const float dx = m.x - player[0], dy = m.y - player[1], dz = m.z - player[2];
        const float distanceSquared = dx * dx + dy * dy + dz * dz;
        qt::Projected p;
        if (distanceSquared > qt::kMaxMarkerDistance * qt::kMaxMarkerDistance ||
            !qt::Project(g_camera.view, g_camera.projection, m.x, m.y, m.z, p) || !p.Usable())
            continue;
        lua_pushnumber(L, p.ndcX); lua_pushnumber(L, p.ndcY); lua_pushnumber(L, std::sqrt(distanceSquared));
        lua_pushboolean(L, 1);
        return 4;
    }
    return PushNone(L);
}
int SetMarkerAlpha(lua_State* L) {
    double v;
    return Push(L, Number(L, 1, v) && g_state.SetMarkerAlpha(v));
}
int SetSettings(lua_State* L) {
    double v[3];
    for (int i = 0; i < 3; ++i)
        if (!Number(L, i + 1, v[i])) return Push(L, false);
    return Push(L, g_state.SetSettings(v[0], v[1], v[2]));
}
int SetCorpseMarker(lua_State* L) {
    double v[3];
    for (int i = 0; i < 3; ++i)
        if (!Number(L, i + 1, v[i])) return Push(L, false);
    return Push(L, g_state.SetCorpse(v[0], v[1], v[2]));
}
int ClearCorpseMarker(lua_State* L) { g_state.ClearCorpse(); return Push(L, true); }
int IsCorpseActive(lua_State* L) { return Push(L, g_state.CorpseActive()); }
int GetCorpseInfo(lua_State* L) {
    float player[3];
    qt::Projected p;
    if (!g_state.CorpseActive() || !g_camera.valid || !PlayerPosition(player) ||
        !qt::Project(g_camera.view, g_camera.projection, g_state.Corpse()[0], g_state.Corpse()[1], g_state.Corpse()[2], p) ||
        !p.Usable())
        return PushNone(L);
    const float dx = g_state.Corpse()[0] - player[0], dy = g_state.Corpse()[1] - player[1], dz = g_state.Corpse()[2] - player[2];
    lua_pushnumber(L, p.ndcX); lua_pushnumber(L, p.ndcY); lua_pushnumber(L, std::sqrt(dx * dx + dy * dy + dz * dz));
    lua_pushboolean(L, 1);
    return 4;
}
int SetPlayerDead(lua_State* L) {
    double v;
    return Push(L, Number(L, 1, v) && g_state.SetDead(v));
}
int GetPlayerPosition(lua_State* L) {
    float position[3];
    CGPlayer_C* player = ClntObjMgrGetActivePlayerObj();
    if (!player) { lua_pushnil(L); return 1; }
    const C3Vector at = player->GetPosition();
    position[0] = at.x; position[1] = at.y; position[2] = at.z;
    for (float c : position) lua_pushnumber(L, c);
    return 3;
}
int GetMapId(lua_State* L) {
    const int32_t id = World::QueryMapId();
    if (id < 0) { lua_pushnil(L); return 1; }
    lua_pushnumber(L, id);
    return 1;
}
int SetDiamond(lua_State* L) {
    double v[5];
    for (int i = 0; i < 5; ++i)
        if (!Number(L, i + 1, v[i])) return Push(L, false);
    return Push(L, g_state.SetDiamond(v[0], v[1], v[2], v[3], v[4]));
}
// WarcraftXL_TrackerSetObjectiveEntries(count, entry1 ... entryN)
int SetObjectiveEntries(lua_State* L) {
    double args[1 + qt::kMaxObjectiveEntries] = {};
    if (!Number(L, 1, args[0]) || args[0] < 0.0 || args[0] > qt::kMaxObjectiveEntries) return Push(L, false);
    const int count = static_cast<int>(args[0]);
    for (int i = 0; i < count; ++i)
        if (!Number(L, 2 + i, args[1 + i])) return Push(L, false);
    return Push(L, g_state.SetObjectiveEntries(args, 1 + static_cast<size_t>(count)));
}
int ClearObjectiveEntries(lua_State* L) { g_state.ClearObjectiveEntries(); return Push(L, true); }
int SetRingSettings(lua_State* L) {
    double enabled, alpha;
    return Push(L, Number(L, 1, enabled) && Number(L, 2, alpha) && g_state.SetRingSettings(enabled, alpha));
}

// ---------------------------------------------------------------- drawing
}  // namespace

void WxlRegisterQuestTrackerLua() {
    if (!Enabled()) return;
    FrameScript_RegisterFunction("WarcraftXL_TrackerSetMarker", &SetMarker);
    FrameScript_RegisterFunction("WarcraftXL_TrackerClearMarkers", &ClearMarkers);
    FrameScript_RegisterFunction("WarcraftXL_TrackerGetMarkerInfo", &GetMarkerInfo);
    FrameScript_RegisterFunction("WarcraftXL_TrackerSetMarkerAlpha", &SetMarkerAlpha);
    FrameScript_RegisterFunction("WarcraftXL_TrackerSetSettings", &SetSettings);
    FrameScript_RegisterFunction("WarcraftXL_TrackerSetCorpseMarker", &SetCorpseMarker);
    FrameScript_RegisterFunction("WarcraftXL_TrackerClearCorpseMarker", &ClearCorpseMarker);
    FrameScript_RegisterFunction("WarcraftXL_TrackerIsCorpseActive", &IsCorpseActive);
    FrameScript_RegisterFunction("WarcraftXL_TrackerGetCorpseInfo", &GetCorpseInfo);
    FrameScript_RegisterFunction("WarcraftXL_TrackerSetPlayerDead", &SetPlayerDead);
    FrameScript_RegisterFunction("WarcraftXL_TrackerGetPlayerPosition", &GetPlayerPosition);
    FrameScript_RegisterFunction("WarcraftXL_TrackerGetMapId", &GetMapId);
    FrameScript_RegisterFunction("WarcraftXL_TrackerSetDiamond", &SetDiamond);
    FrameScript_RegisterFunction("WarcraftXL_TrackerSetObjectiveEntries", &SetObjectiveEntries);
    FrameScript_RegisterFunction("WarcraftXL_TrackerClearObjectiveEntries", &ClearObjectiveEntries);
    FrameScript_RegisterFunction("WarcraftXL_TrackerSetRingSettings", &SetRingSettings);
    WxlNativeDiagnostic("quest-tracker: Lua bridge ready");
}

void WxlQuestTrackerWorldLeave() {
    g_state.ResetSession();
    g_camera.valid = false;
    g_framesInWorld = 0;
}

void WxlDrawQuestTracker() {
    g_ringsThisFrame = 0;   // the per-unit ring draws of this frame are done
    if (!Enabled()) return;
    auto* gx = g_theGxDevicePtr;
    if (!gx || gx->m_api != GxApi_D3d9Ex) return;
    auto* device = static_cast<CGxDeviceD3d9Ex*>(gx)->m_d3dDevice;
    if (!device) return;

    // Not in a world (character select, login, logout): anything left is stale. The add-on only sets
    // markers while in world, so it will set them again.
    if (World::QueryMapId() < 0 || !ClntObjMgrGetActivePlayerObj()) {
        g_state.ClearMarkers();
        g_state.ClearCorpse();
        g_state.SetDead(0);
        g_state.SetDiamond(0, 0, 0, 0, 0);
        g_camera.valid = false;
        g_framesInWorld = 0;
        return;
    }
    // Always track the camera so the Lua label queries have something to project against.
    if (!WxlCaptureWorldCamera(g_camera)) return;
    // The first frames after entering a world can carry a stale camera.
    if (g_framesInWorld < 3) { ++g_framesInWorld; return; }

    float minX, maxX, minY, maxY, minZ, maxZ;
    GxXformViewport(minX, maxX, minY, maxY, minZ, maxZ);
    float player[3] = {g_camera.eye[0], g_camera.eye[1], g_camera.eye[2]};
    PlayerPosition(player);
    if (!qt::NeedsDraw(g_state)) return;
    IDirect3DTexture9* atlas = EnsureAtlas(device);
    if (!atlas) return;
    const qt::DrawResult result = qt::Draw(device, g_state, g_camera.view, g_camera.projection, minZ, maxZ, player, atlas);
    static qt::DrawResult reported = qt::DrawResult::Drawn;
    if (result != reported) {
        reported = result;
        WxlNativeDiagnostic("quest-tracker: draw %s", qt::DrawResultName(result));
    }
}

// ---------------------------------------------------------------- objective rings
namespace {
CTexture* g_ringTexture = nullptr;
uint32_t g_ringRetryAt = 0;

CGxTex* RingGxTexture() {
    if (!g_ringTexture) {
        const uint32_t now = FrameTime::s_curTimeMs;
        if (static_cast<int32_t>(now - g_ringRetryAt) < 0) return nullptr;
        g_ringRetryAt = now + 3000;
        CStatus status;
        g_ringTexture = TextureCreate("textures\\questmarker\\unitselecttexture-quest.blp", CGxTexFlags(), &status, 1);
        if (!g_ringTexture) WxlNativeDiagnostic("quest-tracker: objective ring texture unavailable, using the stock circle");
    }
    // Not ready yet (asynchronous load) returns null, and the unit gets the stock circle texture this frame.
    return g_ringTexture ? TextureGetGxTex(g_ringTexture, 0, nullptr) : nullptr;
}
}  // namespace

void WxlObjectiveRingDraw(const CGUnit_C* unit) {
    if (!unit || !Enabled() || !g_state.RingsActive() || g_ringsThisFrame >= qt::kRingMaxPerFrame) return;
    if (!CGGameUI::m_visible || !unit->m_obj || !unit->m_unit) return;
    // Units only: players never get a quest ring, and the target already has the engine's own circle.
    if (unit->m_obj->m_type & TYPE_PLAYER) return;
    if (unit->m_obj->m_guid == CGGameUI::m_lockedTarget) return;
    if (!g_state.ObjectiveMatches(unit->m_obj->m_entryID)) return;
    // The ring vanishes when the creature dies and returns on respawn: only a health of exactly 0 hides it.
    if (unit->GetPredictedHealth() == 0) return;
    const float radius = unit->m_selectionRadius;
    if (!qt::RingRadiusUsable(radius)) return;

    C3Vector position = const_cast<CGUnit_C*>(unit)->GetModelPosition();
    if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z) ||
        std::abs(position.x) > 20000.0f || std::abs(position.y) > 20000.0f)
        return;
    if (CGPlayer_C* player = ClntObjMgrGetActivePlayerObj()) {
        const C3Vector at = player->GetPosition();
        const float dx = position.x - at.x, dy = position.y - at.y, dz = position.z - at.z;
        if (dx * dx + dy * dy + dz * dz > qt::kRingMaxDistance * qt::kRingMaxDistance) return;
    }

    const uint32_t argb = qt::RingColour(g_state.GetRingSettings().alpha);
    CImVector color;
    color.a = static_cast<uint8_t>(argb >> 24);
    color.r = static_cast<uint8_t>(argb >> 16);
    color.g = static_cast<uint8_t>(argb >> 8);
    color.b = static_cast<uint8_t>(argb);

    CAaBox box;
    box.b = C3Vector(position.x - radius, position.y - radius, position.z - radius);
    box.t = C3Vector(position.x + radius, position.y + radius, position.z + radius);
    C44Matrix matrix;
    CGObject_C::BuildSelectionRotMatrix(&matrix, &position);
    WxlSelectionCircleApply(unit, &matrix);

    // The same recipe as CGUnit_C::RenderTargetSelection; the push/pop also restores the texture swap.
    g_theGxDevicePtr->RsPush();
    if (CGObject_C::SetCircleRenderStates()) {
        if (CGxTex* texture = RingGxTexture()) g_theGxDevicePtr->RsSet(GxRs_Texture0, texture);
        ProjectTex::Render2d(box, color, &matrix, 0.5f, 0x200122, 1, 0.4f);
        ++g_ringsThisFrame;
    }
    g_theGxDevicePtr->RsPop();
}

#else
void WxlRegisterQuestTrackerLua() {}
void WxlDrawQuestTracker() {}
void WxlQuestTrackerWorldLeave() {}
void WxlObjectiveRingDraw(const CGUnit_C*) {}
#endif
