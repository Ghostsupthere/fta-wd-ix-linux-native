#include "WoW/Source/Object/ObjectClient/MountObjectEffectStates.h"

static_assert(MountObjectEffectStates::StateCount == 1024);
static_assert(MountObjectEffectStates::FromAnimation(ANIM_MOUNT_SPECIAL) == 90);
static_assert(MountObjectEffectStates::FromAnimation(ANIM_FLY_MOUNT_SPECIAL) == 90);
static_assert(MountObjectEffectStates::FromAnimation(ANIM_MOUNT) == 0);
static_assert(MountObjectEffectStates::FromAnimation(532) == 96);
static_assert(MountObjectEffectStates::FromAnimation(546) == 103);
static_assert(MountObjectEffectStates::FromAnimation(548) == 82);
static_assert(MountObjectEffectStates::FromAnimation(564) == 90);
static_assert(MountObjectEffectStates::FromAnimation(574) == 92);
static_assert(MountObjectEffectStates::FromAnimation(531) == 0);
static_assert(MountObjectEffectStates::FromAnimation(533) == 0);
static_assert(MountObjectEffectStates::FromAnimation(575) == 0);
static_assert(MountObjectEffectStates::FromAnimation(3000) == 0);
static_assert(MountObjectEffectStates::FromAnimation(-1) == 0);

int main() {}
