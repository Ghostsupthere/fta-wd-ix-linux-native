#include "WoW/Source/Magic/MagicClient/MacroGroundTarget.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <limits>

struct WOWGUID {
    uint64_t value;
    explicit operator bool() const { return value != 0; }
};

struct C44Matrix {
    float translation;
    C44Matrix AffineInverse() const { return {-translation}; }
};

struct C3Vector {
    float x, y, z;
    void operator*=(C44Matrix matrix) { x += matrix.translation; }
};

struct CTerrainClickEvent {
    C3Vector m_point;
    WOWGUID m_guid;
};

struct CGUnit_C {
    float GetDistanceToPos(C3Vector point) const {
        return point.x * point.x + point.y * point.y + point.z * point.z;
    }
};

struct Player {
    C3Vector position;
    WOWGUID transport;
    C3Vector GetPosition() const { return position; }
    uint64_t GetTransportGUID() const { return transport.value; }
};

Player* activePlayer;
Player* ClntObjMgrGetActivePlayerObj() { return activePlayer; }

bool MovementGetTransportMtxSafe(WOWGUID guid, C44Matrix* matrix) {
    if (guid.value != 7) return false;
    *matrix = {10.0f};
    return true;
}

struct CGWorldFrame {
    static inline CGWorldFrame* s_currentWorldFrame;
    CTerrainClickEvent hit;
    bool focused;
    bool GetMacroGroundTarget(CTerrainClickEvent& event) const {
        event = hit;
        return focused;
    }
};

struct Spell_C {
    static void GetSpellRange(CGUnit_C*, uint32_t, float* minimum, float* maximum, void*) {
        *minimum = 0.0f;
        *maximum = 20.0f;
    }
};

struct PendingSpellCastData {
    C3Vector m_sourcePos{}, m_destPos{};
    WOWGUID m_sourceTarget{}, m_destTarget{};
    uint32_t m_targetFlags = 0x2;
};

#include "apply_target.inc"

struct MacroSpellTarget {
    static inline bool cursor;
    static bool IsCursor() { return cursor; }
};

struct CGGameUI {
    static inline bool targetedItem;
    static void CastItemSpell(WOWGUID, int) { targetedItem = true; }
};

bool stoppedTargeting;
bool Spell_C_IsTargeting() { return true; }
bool Spell_C_CanTargetItems() { return true; }
void Spell_C_StopTargeting() { stoppedTargeting = true; }
bool Spell_C_CanPlaceMacroGroundTarget(int spellId) { return spellId == 3; }

struct Item {
    struct Object { WOWGUID m_guid; } object{{1}};
    Object* m_obj = &object;
    int TryUse(int spellId, bool hasSpell) {
#include "item_use.inc"
        return 1;
    }
};

int main() {
    using MacroGroundPlacement::Cursor;
    using MacroGroundPlacement::Player;
    using namespace MacroGroundTarget;

    CGUnit_C caster;
    PendingSpellCastData result;
    assert(!ApplyMacroGroundTarget(Cursor, &caster, 3, Destination, result));
    assert(!ApplyMacroGroundTarget(Player, &caster, 3, Destination, result));
    assert(result.m_targetFlags == 0x2);

    CGWorldFrame world{{{2.0f, 0.0f, 0.0f}, {7}}, false};
    CGWorldFrame::s_currentWorldFrame = &world;
    assert(!ApplyMacroGroundTarget(Cursor, &caster, 3, Destination, result));
    world.focused = true;
    assert(ApplyMacroGroundTarget(Cursor, &caster, 3, Source | Destination, result));
    assert(result.m_sourcePos.x == 2.0f && result.m_destPos.x == 2.0f);
    assert(result.m_sourceTarget.value == 7 && result.m_destTarget.value == 7);
    assert(result.m_targetFlags == (Source | Destination | 0x2));

    world.hit.m_point.x = 11.0f; // Local point is in range, transformed world point is out of range.
    assert(!ApplyMacroGroundTarget(Cursor, &caster, 3, Destination, result));
    assert(result.m_destPos.x == 2.0f && result.m_destTarget.value == 7);
    world.hit.m_point.x = std::numeric_limits<float>::infinity();
    assert(!ApplyMacroGroundTarget(Cursor, &caster, 3, Destination, result));

    ::Player active{{12.0f, 0.0f, 0.0f}, {7}};
    activePlayer = &active;
    result = {};
    assert(ApplyMacroGroundTarget(Player, &caster, 3, Destination, result));
    assert(result.m_destPos.x == 2.0f && result.m_destTarget.value == 7);
    assert(result.m_sourceTarget.value == 0 && result.m_targetFlags == (Destination | 0x2));
    active.transport = {99};
    assert(!ApplyMacroGroundTarget(Player, &caster, 3, Destination, result));
    active.transport = {};
    assert(ApplyMacroGroundTarget(Player, &caster, 3, Destination, result));
    assert(result.m_destPos.x == 12.0f && !result.m_destTarget);

    Item item;
    MacroSpellTarget::cursor = true;
    assert(item.TryUse(3, false) == 0 && !stoppedTargeting && !CGGameUI::targetedItem);
    assert(item.TryUse(1, true) == 0 && !stoppedTargeting && !CGGameUI::targetedItem);
    assert(item.TryUse(3, true) == 1 && stoppedTargeting && !CGGameUI::targetedItem);

    MacroSpellTarget::cursor = false;
    stoppedTargeting = false;
    assert(item.TryUse(1, true) == 0 && !stoppedTargeting && CGGameUI::targetedItem);
}
