// Native stand-in for the core's world/object accessors. Object pointers are the native CGUnit_C /
// CGPlayer_C objects, passed around as void* exactly as the supplied code does.
#pragma once

namespace wxl::game::world {
constexpr unsigned kTypeMaskUnit = 0x8;     // TYPE_UNIT
constexpr unsigned kTypeMaskPlayer = 0x10;  // TYPE_PLAYER

unsigned long long MouseoverGuid();
unsigned long long TargetGuid();
unsigned long long ActivePlayerGuid();
void* ResolveObject(unsigned long long guid, unsigned typeMask);
void UnitPosition(void* unit, float out[3]);
float UnitFacing(void* unit);
}  // namespace wxl::game::world
