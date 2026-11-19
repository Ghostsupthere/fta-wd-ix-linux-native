// FTA housing: the native part of the placement editor. The editor window and the server protocol are Lua (the
// FtaHousing add-on, a port of the Esteria housing UI); this file only does what Lua cannot: the mouse cursor's ray into
// the world, a client-only "ghost" copy of the decor being placed (the way the client draws a ground-targeted spell's
// object), and the translate / rotate / scale handles, drawn as camera-facing ribbons. Lua picks and drags the handles.
//
// The Lua functions keep the names the Esteria add-on calls (EsteriaHousing_*), so its code runs unchanged. The ghost,
// cursor and handle logic follows the Esteria housing extension (wowdecomp, Extensions/Housing); the engine calls are this
// tree's own.
#include "WoW/Source/Ui/Linux/WxlHousing.h"

#if defined(WOWDECOMP_LINUX)

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "Engine/Source/Base/Coordinate.h"
#include "Engine/Source/Base/Status.h"
#include "Engine/Source/Frame/CSimpleTop.h"
#include "Engine/Source/FrameScript/FrameScript.h"
#include "Engine/Source/Gx/CGxDevice/CGxDevice.h"
#include "Engine/Source/Gx/Gx.h"
#include "Engine/Source/Model2/M2Model.h"
#include "Engine/Source/Model2/M2Scene.h"
#include "Engine/Source/Services/Texture.h"
#include "Engine/Source/Tempest/C4Quaternion.h"
#include "Engine/Source/lua-5.1/src/lauxlib.h"
#include "Engine/Source/lua-5.1/src/lua.h"
#include "Engine/Source/ModernAssets/FileDataIdResolver.h"
#include "WoW/Source/Object/ObjectClient/Player_C.h"
#include "WoW/Source/ObjectMgrClient/ObjectMgrClient.h"
#include "WoW/Source/UIUtil/Camera.h"
#include "WoW/Source/Ui/WorldFrame.h"
#include "WoW/Source/WorldClient/Map.h"
#include "WoW/Source/WorldClient/World.h"

namespace {

constexpr double kPi = 3.14159265358979323846;

C4Quaternion Quat(float x, float y, float z, float w) {
    C4Quaternion q;
    q.x = x; q.y = y; q.z = z; q.w = w;
    return q;
}

struct Ghost {
    CM2Model* model = nullptr;
    CMapEntity* entity = nullptr;
    CMapBaseObj* wmo = nullptr;   // a WMO decor's ghost (CWorld::ObjectCreate, as a game object's WMO)
    C4Quaternion rot = Quat(0, 0, 0, 1);
    float scale = 1;
    bool onFloor = false;         // the cursor met the floor this frame (or the ghost is pinned)
    bool pinned = false;          // stays at pos instead of following the cursor
    C3Vector pos;
    std::string shown;            // the model path that is up
} g_ghost;

bool g_snapSurfaces = true;       // the cursor lands on decor too (game objects), so decor can sit on tables and shelves

C44Matrix GhostMatrix() {
    C44Matrix matrix(g_ghost.rot);
    matrix.Scale(g_ghost.scale);
    matrix.d0 = g_ghost.pos.x; matrix.d1 = g_ghost.pos.y; matrix.d2 = g_ghost.pos.z;
    return matrix;
}

void DestroyGhost() {
    if (g_ghost.wmo) CWorld::ObjectDestroy(g_ghost.wmo);
    if (g_ghost.entity) World::ObjectDestroy(g_ghost.entity);
    if (g_ghost.model) g_ghost.model->Release();
    g_ghost = Ghost{};
}

int32_t UpdateGhost(void*, uint32_t, uint64_t, uintptr_t) {
    if (!g_ghost.model) return 1;
    g_ghost.model->m_fFlags |= 0x8000;
    g_ghost.model->m_worldTransform = GhostMatrix();
    g_ghost.model->m_baseAlpha = 0.7f;
    return 1;
}

// The cursor's ray through the world camera: start and end points.
bool CursorSegment(C3Vector& start, C3Vector& end) {
    CGWorldFrame* frame = CGWorldFrame::s_currentWorldFrame;
    if (!frame || !frame->m_camera || !frame->m_simpleTop) return false;
    float x, y;
    NDCToDDC(frame->m_simpleTop->m_mousePosition.m_x, frame->m_simpleTop->m_mousePosition.m_y, &x, &y);
    // The ray needs the world camera's projection (the same dance as CGWorldFrame::HitTestPoint).
    CGxDevice* device = g_theGxDevicePtr;
    C44Matrix const projection = device->m_projection;
    CGxMatrixStack& stack = device->m_xforms[GxXform_View];
    C44Matrix const view = stack.m_mtx[stack.m_level];
    frame->m_camera->SetupWorldProjection(frame->m_viewportDDC);
    bool const ok = frame->GetLineSegment(x, y, &start, &end) != 0;
    device->XformSetProjection(projection);
    device->XformSetView(view);
    return ok;
}

// Where the cursor's ray meets terrain, a world model or (surface snap) a game object.
bool CursorHit(C3Vector& out) {
    CGWorldFrame* frame = CGWorldFrame::s_currentWorldFrame;
    if (!frame || !frame->m_camera || !frame->m_simpleTop) return false;
    float x, y;
    NDCToDDC(frame->m_simpleTop->m_mousePosition.m_x, frame->m_simpleTop->m_mousePosition.m_y, &x, &y);
    CGxDevice* device = g_theGxDevicePtr;
    C44Matrix const projection = device->m_projection;
    CGxMatrixStack& stack = device->m_xforms[GxXform_View];
    C44Matrix const view = stack.m_mtx[stack.m_level];
    frame->m_camera->SetupWorldProjection(frame->m_viewportDDC);
    C3Vector start, end;
    HITTESTRESULT hit{};
    bool const found = frame->GetLineSegment(x, y, &start, &end) && frame->HitTest(start, end, g_snapSurfaces ? 0x7 : 0x3, &hit) != 0;
    device->XformSetProjection(projection);
    device->XformSetView(view);
    if (!found) return false;
    out = hit.m_point;
    return true;
}

// ----------------------------------------------------------------------------------------------- the handles

struct Gizmo {
    int mode = 0;          // 0 off, 1 translate (arrows), 2 rotate (rings), 3 scale (a handle above)
    C3Vector pos;
    float radius = 1.5f;
    int hover = 0;         // axis 1 x, 2 y, 3 z (4 the scale handle) under the cursor, or being dragged
    bool dragging = false;
} g_gizmo;
CTexture* g_white = nullptr;

void Ribbon(C3Vector const* points, uint32_t count, CImVector color, float width, C3Vector const& eye) {
    if (count < 2 || count > 200) return;
    C3Vector positions[400];
    CImVector colors[400];
    C2Vector tex[400];
    uint16_t indices[400];
    for (uint32_t i = 0; i < count; ++i) {
        C3Vector const along = points[i + 1 < count ? i + 1 : i] - points[i > 0 ? i - 1 : 0];
        C3Vector const view = points[i] - eye;
        C3Vector side(along.y * view.z - along.z * view.y, along.z * view.x - along.x * view.z, along.x * view.y - along.y * view.x);
        float const length = std::sqrt(side.x * side.x + side.y * side.y + side.z * side.z);
        if (length > 1e-6f) side = side * (width * 0.5f / length);
        positions[2 * i] = points[i] + side;
        positions[2 * i + 1] = points[i] - side;
        colors[2 * i] = colors[2 * i + 1] = color;
        tex[2 * i] = C2Vector(0, 0);
        tex[2 * i + 1] = C2Vector(1, 1);
        indices[2 * i] = uint16_t(2 * i);
        indices[2 * i + 1] = uint16_t(2 * i + 1);
    }
    C3Vector const normal(0, 0, 1);
    GxPrimLockVertexPtrs(2 * count, positions, sizeof(C3Vector), &normal, 0, colors, sizeof(CImVector), nullptr, 0, tex,
        sizeof(C2Vector), nullptr, 0);
    GxDrawLockedElements(GxPrim_TriangleStrip, 2 * count, indices);
    GxPrimUnlockVertexPtrs();
}

CImVector AxisColor(int axis) {
    CImVector c;
    c.a = 0xE6;
    bool const hot = g_gizmo.hover == axis;
    if (hot && g_gizmo.dragging) { c.r = c.g = c.b = 0xFF; return c; }  // the handle being dragged: white
    if (hot) { c.r = 0xFF; c.g = 0xE0; c.b = 0x30; return c; }           // under the cursor: yellow
    c.r = axis == 1 ? 0xF0 : 0x40; c.g = axis == 2 ? 0xE0 : 0x50; c.b = axis == 3 ? 0xFF : 0x40;
    if (axis == 4) { c.r = 0xE0; c.g = 0xE0; c.b = 0xE0; }
    return c;
}

// ----------------------------------------------------------------------------------------------- Lua

// EsteriaHousing_GetPlayerPosition() -> x, y, z, facing (degrees); 3.3.5 Lua has no world position API.
int32_t GetPlayerPosition(lua_State* L) {
    CGPlayer_C* player = ClntObjMgrGetActivePlayerObj();
    if (!player) return 0;
    C3Vector const pos = player->GetPosition();
    lua_pushnumber(L, pos.x); lua_pushnumber(L, pos.y); lua_pushnumber(L, pos.z);
    lua_pushnumber(L, player->GetFacing() * 180.0 / kPi);
    return 4;
}

// x, y, z where the mouse cursor's ray meets terrain or a world model, or nothing.
int32_t GetCursorWorld(lua_State* L) {
    C3Vector hit;
    if (!CursorHit(hit)) return 0;
    // A ray that slips past the room's floor meets the map's ground plane; keep things at the player's floor then.
    if (CGPlayer_C* player = ClntObjMgrGetActivePlayerObj())
        if (std::fabs(hit.z - player->GetPosition().z) > 40.0f) hit.z = player->GetPosition().z;
    lua_pushnumber(L, hit.x); lua_pushnumber(L, hit.y); lua_pushnumber(L, hit.z);
    return 3;
}

// sx, sy, sz, ex, ey, ez of the cursor's ray, or nothing.
int32_t GetCursorRay(lua_State* L) {
    C3Vector start, end;
    if (!CursorSegment(start, end)) return 0;
    for (float v : { start.x, start.y, start.z, end.x, end.y, end.z }) lua_pushnumber(L, v);
    return 6;
}

// EsteriaHousing_SetSurfaceSnap(on): whether placing lands on decor (tables, shelves) as well as floors and walls.
int32_t SetSurfaceSnap(lua_State* L) {
    g_snapSurfaces = lua_toboolean(L, 1) != 0;
    return 0;
}

// The model to draw for a ghost argument: a path string as given, or a FileDataID through the sidecar.
bool GhostPath(lua_State* L, int index, std::string& path) {
    if (lua_type(L, index) == LUA_TSTRING) {
        path = lua_tostring(L, index);
        return !path.empty();
    }
    if (lua_type(L, index) == LUA_TNUMBER) return ResolveModernAssetFileDataId(uint32_t(lua_tonumber(L, index)), path);
    return false;
}

bool EndsWith(std::string const& s, char const* suffix) {
    size_t n = std::strlen(suffix);
    if (s.size() < n) return false;
    for (size_t i = 0; i < n; ++i)
        if (std::tolower(static_cast<unsigned char>(s[s.size() - n + i])) != suffix[i]) return false;
    return true;
}

// EsteriaHousing_SetGhost(path | fileDataId, yawDegrees | qx, qy, qz, qw, scale) shows the decor under the cursor;
// EsteriaHousing_SetGhost() removes it. Called again with the same model it only changes the turn and the size.
int32_t SetGhost(lua_State* L) {
    std::string path;
    bool const have = GhostPath(L, 1, path);
    if (lua_isnumber(L, 5)) {
        g_ghost.rot = Quat(float(lua_tonumber(L, 2)), float(lua_tonumber(L, 3)), float(lua_tonumber(L, 4)), float(lua_tonumber(L, 5)));
        g_ghost.scale = lua_isnumber(L, 6) ? float(lua_tonumber(L, 6)) : 1.0f;
    } else if (lua_isnumber(L, 2)) {
        double const half = lua_tonumber(L, 2) * kPi / 360.0;
        g_ghost.rot = Quat(0, 0, float(std::sin(half)), float(std::cos(half)));
        g_ghost.scale = 1;
    }
    if (!have) { DestroyGhost(); return 0; }
    if ((g_ghost.model || g_ghost.wmo) && g_ghost.shown == path) return 0;
    Ghost const keep = g_ghost;
    DestroyGhost();
    g_ghost.rot = keep.rot;
    g_ghost.scale = keep.scale;
    g_ghost.pinned = keep.pinned;
    g_ghost.pos = keep.pos;
    g_ghost.shown = path;
    if (EndsWith(path, ".wmo")) {
        g_ghost.wmo = CWorld::ObjectCreate(path.c_str(), g_ghost.pos, 0, 0, 0, 0, 0, nullptr, nullptr, 0);
        return 0;
    }
    g_ghost.model = World::s_m2Scene ? World::s_m2Scene->CreateModel(path.c_str(), 0) : nullptr;
    if (g_ghost.model) g_ghost.entity = CMap::ObjectCreate(g_ghost.model, UpdateGhost, nullptr, 0, 0, 10);
    return 0;
}

// EsteriaHousing_PinGhost(x, y, z) holds the ghost there (a handle drag); EsteriaHousing_PinGhost() lets it follow the cursor.
int32_t PinGhost(lua_State* L) {
    g_ghost.pinned = lua_isnumber(L, 3) != 0;
    if (g_ghost.pinned) g_ghost.pos = C3Vector(float(lua_tonumber(L, 1)), float(lua_tonumber(L, 2)), float(lua_tonumber(L, 3)));
    return 0;
}

// x, y, z of the ghost (where a click places it), or nothing when the cursor is off the floor.
int32_t GetGhost(lua_State* L) {
    if ((!g_ghost.model && !g_ghost.wmo) || !g_ghost.onFloor) return 0;
    lua_pushnumber(L, g_ghost.pos.x); lua_pushnumber(L, g_ghost.pos.y); lua_pushnumber(L, g_ghost.pos.z);
    return 3;
}

// EsteriaHousing_SetGizmo(mode, x, y, z, radius, hoverAxis, dragging); EsteriaHousing_SetGizmo(0) hides it.
int32_t SetGizmo(lua_State* L) {
    g_gizmo.mode = lua_isnumber(L, 1) ? int(lua_tonumber(L, 1)) : 0;
    if (g_gizmo.mode && lua_isnumber(L, 4)) {
        g_gizmo.pos = C3Vector(float(lua_tonumber(L, 2)), float(lua_tonumber(L, 3)), float(lua_tonumber(L, 4)));
        g_gizmo.radius = lua_isnumber(L, 5) ? float(lua_tonumber(L, 5)) : 1.5f;
        g_gizmo.hover = lua_isnumber(L, 6) ? int(lua_tonumber(L, 6)) : 0;
        g_gizmo.dragging = lua_toboolean(L, 7) != 0;
    }
    return 0;
}

// Not drawn in this port (the retail plot-fence model is not shipped); the add-on calls it, so it exists.
int32_t ShowPlotBounds(lua_State*) { return 0; }

}  // namespace

void WxlRegisterHousingLua() {
    FrameScript_RegisterFunction("EsteriaHousing_GetPlayerPosition", &GetPlayerPosition);
    FrameScript_RegisterFunction("EsteriaHousing_GetCursorWorld", &GetCursorWorld);
    FrameScript_RegisterFunction("EsteriaHousing_GetCursorRay", &GetCursorRay);
    FrameScript_RegisterFunction("EsteriaHousing_SetSurfaceSnap", &SetSurfaceSnap);
    FrameScript_RegisterFunction("EsteriaHousing_SetGhost", &SetGhost);
    FrameScript_RegisterFunction("EsteriaHousing_PinGhost", &PinGhost);
    FrameScript_RegisterFunction("EsteriaHousing_GetGhost", &GetGhost);
    FrameScript_RegisterFunction("EsteriaHousing_SetGizmo", &SetGizmo);
    FrameScript_RegisterFunction("EsteriaHousing_ShowPlotBounds", &ShowPlotBounds);
    FrameScript_RegisterFunction("EsteriaHousing_NativeVersion", [](lua_State* L) -> int { lua_pushnumber(L, 1); return 1; });
}

// Once a frame, from the world update: keep the ghost under the cursor.
void WxlHousingUpdate() {
    if (!g_ghost.model && !g_ghost.wmo) return;
    if (g_ghost.wmo) {
        // A map object has collision: park it far below while the cursor's ray looks for the floor, so the ray never lands
        // on the ghost itself, and leave it there while the cursor is off the floor (hidden).
        C44Matrix away;
        away.Identity();
        away.d2 = -100000.0f;
        World::ObjectUpdate(reinterpret_cast<CMapEntity*>(g_ghost.wmo), away);
        g_ghost.onFloor = g_ghost.pinned || CursorHit(g_ghost.pos);
        if (g_ghost.onFloor) World::ObjectUpdate(reinterpret_cast<CMapEntity*>(g_ghost.wmo), GhostMatrix());
    }
    if (g_ghost.model && g_ghost.entity) {
        g_ghost.onFloor = g_ghost.pinned || CursorHit(g_ghost.pos);
        // Move the map entity too (CGWorldFrame does this for a spell's ground object), or it stays culled at 0,0,0.
        CAaBox box;
        box.b = C3Vector(0, 0, 0);
        box.t = C3Vector(0, 0, 0);
        CAaSphere sphere;
        C3Vector center(0, 0, 0);
        if (g_ghost.model->IsLoaded(0, 0)) {
            box = g_ghost.model->GetBoundingBox();
            sphere = g_ghost.model->GetBoundingSphere();
            center = sphere.c;
        }
        C44Matrix matrix = GhostMatrix();
        CMap::ObjectUpdate(g_ghost.entity, matrix, box, sphere, center, false, 0xFFFFFFFF);
        World::ObjectSetHidden(g_ghost.entity, !g_ghost.onFloor);
    }
}

// From the end of the world render, with the world's transforms still set.
void WxlHousingDrawGizmo() {
    if (!g_gizmo.mode || !CGWorldFrame::GetActiveCamera()) return;
    if (!g_white) {
        CStatus status;
        g_white = TextureCreate("Interface\\Buttons\\WHITE8X8", CGxTexFlags(GxTex_Linear, 1, 1, 0, 0, 0, 1, 0, 0, 0), &status, 0);
    }
    CGxTex* white = g_white ? TextureGetGxTex(g_white, 0, nullptr) : nullptr;
    if (!white) return;
    C3Vector const eye = CGWorldFrame::GetActiveCamera()->m_position;
    g_theGxDevicePtr->RsPush();
    GxXformPush(GxXform_World);
    GxXformTranslate(GxXform_World, -eye);
    GxRsSet(GxRs_Culling, 0);
    GxRsSet(GxRs_Fog, 0);
    GxRsSet(GxRs_Lighting, 0);
    GxRsSet(GxRs_DepthTest, 0);   // always on top, like retail's handles
    GxRsSet(GxRs_DepthWrite, 0);
    GxRsSet(GxRs_ColorOp0, 0);
    GxRsSet(GxRs_AlphaOp0, 0);
    GxRsSet(GxRs_BlendingMode, 3);
    g_theGxDevicePtr->RsSet(GxRs_Texture0, white);
    GxRsSet(GxRs_TextureShader0, 1);
    float const r = g_gizmo.radius, width = r * 0.045f;
    C3Vector const c = g_gizmo.pos;
    C3Vector const axes[3] = { C3Vector(1, 0, 0), C3Vector(0, 1, 0), C3Vector(0, 0, 1) };
    for (int a = 0; a < 3; ++a) {
        C3Vector const& d = axes[a];
        if (g_gizmo.mode == 1) {
            C3Vector shaft[2] = { c, c + d * r };
            Ribbon(shaft, 2, AxisColor(a + 1), width, eye);
            C3Vector head[2] = { c + d * r, c + d * (r * 1.28f) };  // a wide-to-thin tip
            Ribbon(head, 2, AxisColor(a + 1), width * 3.5f, eye);
        } else if (g_gizmo.mode == 2) {
            C3Vector ring[49];
            C3Vector const u = axes[(a + 1) % 3], v = axes[(a + 2) % 3];
            for (int i = 0; i <= 48; ++i) {
                float const t = float(i) / 48.0f * 6.2831853f;
                ring[i] = c + (u * std::cos(t) + v * std::sin(t)) * r;
            }
            Ribbon(ring, 49, AxisColor(a + 1), width, eye);
        }
    }
    if (g_gizmo.mode == 3) {
        C3Vector stem[2] = { c, c + axes[2] * (r * 1.3f) };
        Ribbon(stem, 2, AxisColor(4), width, eye);
        C3Vector knob[2] = { c + axes[2] * (r * 1.24f), c + axes[2] * (r * 1.42f) };
        Ribbon(knob, 2, AxisColor(4), width * 6.0f, eye);
    }
    GxXformPop(GxXform_World);
    g_theGxDevicePtr->RsPop();
}

// Leaving the world (map unload, logout): the ghost's entity belongs to the old scene.
void WxlHousingWorldLeave() {
    DestroyGhost();
    g_gizmo.mode = 0;
}

#endif  // WOWDECOMP_LINUX
