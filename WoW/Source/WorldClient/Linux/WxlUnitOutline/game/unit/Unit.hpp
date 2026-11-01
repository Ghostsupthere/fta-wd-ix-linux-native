// Native stand-in for the core's unit accessors.
#pragma once

namespace wxl::game::unit {
void* Model(void* unit);          // the unit's body model (CM2Model*), or null
void* ModelParent(void* model);   // attachment-chain parent, or null at the root
int Reaction(void* self, void* other);   // 0..1 hostile, 2..3 neutral, 4+ friendly
int CreatureRank(void* unit);     // 0 normal, 1 elite, 2 rare elite, 3 world boss, 4 rare
int CreatureType(void* unit);     // 2 is Dragonkin
}  // namespace wxl::game::unit
