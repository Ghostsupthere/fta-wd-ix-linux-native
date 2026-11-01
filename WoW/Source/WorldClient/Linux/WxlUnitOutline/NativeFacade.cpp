// Native implementations of the WarcraftXL core facades the supplied wxl-unit-outline calls (GPL-3.0-or-later,
// Copyright (C) 2026 WarcraftXL for the interfaces; this file is the native port's). Each function reproduces
// what the Windows core's accessor returned, on top of the reconstructed engine instead of fixed addresses.
#include "events/Event.hpp"
#include "game/camera/Camera.hpp"
#include "game/combat/Readability.hpp"
#include "game/gx/Gx.hpp"
#include "game/m2/M2.hpp"
#include "game/unit/Unit.hpp"
#include "game/world/Pick.hpp"
#include "game/world/World.hpp"

#include <cmath>
#include <cstring>

#include "Engine/Source/Model2/M2Data.h"
#include "Engine/Source/Model2/M2Model.h"
#include "Engine/Source/Model2/M2Shared.h"
#include "WoW/Source/ObjectMgrClient/ObjectMgrClient.h"
#include "WoW/Source/Object/ObjectClient/Player_C.h"
#include "WoW/Source/Object/ObjectClient/Unit_C.h"
#include "WoW/Source/Ui/GameUI.h"
#include "WoW/Source/Ui/Linux/WxlCombatReadability.h"
#include "WoW/Source/WorldClient/Linux/WxlWorldCamera.h"
#include "WoW/Source/WorldClient/World.h"

namespace wxl::game::world {
unsigned long long MouseoverGuid() { return CGGameUI::m_currentObjectTrack.m_guid; }
unsigned long long TargetGuid() { return CGGameUI::m_lockedTarget.m_guid; }
unsigned long long ActivePlayerGuid() { return ClntObjMgrGetActivePlayer().m_guid; }

void* ResolveObject(unsigned long long guid, unsigned typeMask) {
    if (!guid) return nullptr;
    WOWGUID key;
    key.m_guid = guid;
    return ClntObjMgrObjectPtr(key, static_cast<OBJECT_TYPE>(typeMask), __FILE__, __LINE__);
}

void UnitPosition(void* unit, float out[3]) {
    out[0] = out[1] = out[2] = 0.0f;
    if (!unit) return;
    const C3Vector position = static_cast<CGUnit_C*>(unit)->GetPosition();
    out[0] = position.x; out[1] = position.y; out[2] = position.z;
}

float UnitFacing(void* unit) { return unit ? static_cast<CGUnit_C*>(unit)->GetRawFacing() : 0.0f; }

int IntersectSegment(const Vec3& start, const Vec3& end, WorldHit& out) {
    out = WorldHit{};
    const C3Vector from(start.x, start.y, start.z), to(end.x, end.y, end.z);
    C3Vector hit;
    float t = 1.0f;
    // Terrain (0x100) and WMO faces (0x20) only: M2 geometry is not ground.
    if (!World::Intersect(from, to, &hit, &t, 0x120, nullptr)) return 0;
    out.type = 1;
    out.pos = {hit.x, hit.y, hit.z};
    out.t = t;
    return 1;
}
}  // namespace wxl::game::world

namespace wxl::game::unit {
void* Model(void* unit) { return unit ? static_cast<CGUnit_C*>(unit)->m_worldModel : nullptr; }
void* ModelParent(void* model) { return model ? static_cast<CM2Model*>(model)->m_attachParent : nullptr; }
int Reaction(void* self, void* other) {
    if (!self || !other) return 5;
    return static_cast<int>(static_cast<CGUnit_C*>(self)->UnitReaction(static_cast<const CGUnit_C*>(other)));
}
int CreatureRank(void* unit) { return unit ? static_cast<int>(static_cast<CGUnit_C*>(unit)->GetCreatureClassification()) : 0; }
int CreatureType(void* unit) { return unit ? static_cast<int>(static_cast<CGUnit_C*>(unit)->GetCreatureType()) : 0; }
}  // namespace wxl::game::unit

namespace wxl::game::m2 {
bool VisualFacing(void* model, float& outFacing) {
    if (!model) return false;
    const C44Matrix& matrix = static_cast<CM2Model*>(model)->m_worldTransform;
    const float lengthSquared = matrix.a0 * matrix.a0 + matrix.a1 * matrix.a1;
    if (!std::isfinite(lengthSquared) || lengthSquared < 1.0e-8f) return false;
    outFacing = std::atan2(matrix.a1, matrix.a0);
    return std::isfinite(outFacing);
}

bool FootprintInputs(void* model, float& extentX, float& extentY, float& instanceScale) {
    auto* m = static_cast<CM2Model*>(model);
    if (!m || !m->m_shared || !m->m_shared->m_data || !(m->m_shared->m_flags & 1)) return false;
    const CAaBox& box = m->m_shared->m_data->m_bounds.m_extent;
    extentX = std::max(std::fabs(box.b.x), std::fabs(box.t.x));
    extentY = std::max(std::fabs(box.b.y), std::fabs(box.t.y));
    const C44Matrix& matrix = m->m_worldTransform;
    const float basisX = std::sqrt(matrix.a0 * matrix.a0 + matrix.a1 * matrix.a1 + matrix.a2 * matrix.a2);
    const float basisY = std::sqrt(matrix.b0 * matrix.b0 + matrix.b1 * matrix.b1 + matrix.b2 * matrix.b2);
    instanceScale = (basisX + basisY) * 0.5f;
    return std::isfinite(extentX) && std::isfinite(extentY);
}
}  // namespace wxl::game::m2

namespace wxl::game::combat {
ReadabilitySettings Snapshot() { return WxlReadabilitySnapshot(); }
}  // namespace wxl::game::combat

namespace wxl::game::camera {
namespace {
float g_view[16], g_projection[16];
}
const float* View() { return g_view; }
const float* Projection() { return g_projection; }
void Store(const float* view, const float* projection) {
    std::memcpy(g_view, view, sizeof(g_view));
    std::memcpy(g_projection, projection, sizeof(g_projection));
}
}  // namespace wxl::game::camera

// Takes the world camera from the Gx state; called once per frame at the first model batch of the world pass.
bool WxlUnitOutlineStoreCamera() {
    WxlWorldCamera captured;
    if (!WxlCaptureWorldCamera(captured)) return false;
    wxl::game::camera::Store(captured.view, captured.projection);
    return true;
}

