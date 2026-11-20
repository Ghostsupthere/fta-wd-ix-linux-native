#ifndef WOW_SOURCE_OBJECT_OBJECTCLIENT_MOUNTOBJECTEFFECTSTATES_H
#define WOW_SOURCE_OBJECT_OBJECTCLIENT_MOUNTOBJECTEFFECTSTATES_H

#include <cstdint>
#include "WoW/Source/Object/ObjectClient/Types.h"

namespace MountObjectEffectStates {

constexpr uint32_t StateCount = 1024;

constexpr int32_t FromAnimation(int32_t animId) {
    if (animId == ANIM_MOUNT_SPECIAL || animId == ANIM_FLY_MOUNT_SPECIAL) {
        return 90;
    }

    // Converted mount packages associate these even-numbered sequences with
    // authored object-effect states.
    constexpr int32_t first = 532;
    constexpr int32_t last = 574;
    constexpr int32_t states[] = {
        96, 97, 98, 99, 100, 101, 102, 103, 82, 86, 84,
        85, 87, 83, 88, 89, 90, 93, 94, 95, 91, 92,
    };
    static_assert(sizeof(states) / sizeof(states[0]) == (last - first) / 2 + 1);

    if (animId < first || animId > last || (animId - first) % 2 != 0) {
        return 0;
    }

    return states[(animId - first) / 2];
}

}

#endif
