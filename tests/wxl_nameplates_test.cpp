#include "Engine/Source/ModernAssets/WxlNamePlates/Registry.hpp"
#include <cassert>
#include <cstdint>

using wxl::nameplates::Registry;

int main() {
    Registry reg;
    int a, b, c;  // stand-ins for plate frame objects

    // First sighting: the frame gets slot 0, so its token is nameplate1, and is reported as created.
    auto s1 = reg.Show(&a, 0x100);
    assert(s1.slot == 0 && s1.created && !s1.ownerChanged);
    uint64_t guid = 0;
    assert(reg.GuidAtIndex(1, guid) && guid == 0x100);
    assert(reg.ActiveSlotForGuid(0x100) == 0);

    // A second frame takes the next slot.
    auto s2 = reg.Show(&b, 0x200);
    assert(s2.slot == 1 && s2.created);
    assert(reg.ActiveSlotForGuid(0x200) == 1 && reg.ActiveSlotForGuid(0x300) == -1);

    // Hiding keeps the unit readable until Release (handlers of UNIT_REMOVED can still resolve the token).
    const int hidden = reg.Hide(&a);
    assert(hidden == 0);
    assert(reg.GuidAtIndex(1, guid) && guid == 0x100);
    reg.Release(hidden);
    assert(!reg.GuidAtIndex(1, guid));
    assert(reg.ActiveSlotForGuid(0x100) == -1);
    assert(reg.Hide(&a) == -1);   // already hidden
    assert(reg.Hide(&c) == -1);   // never seen

    // A recycled frame keeps its slot and token, and is not reported as created again.
    auto again = reg.Show(&a, 0x400);
    assert(again.slot == 0 && !again.created && !again.ownerChanged);
    assert(reg.ActiveSlotForGuid(0x400) == 0);

    // Showing a different unit on a frame that is still active is an owner change.
    auto owner = reg.Show(&a, 0x500);
    assert(owner.slot == 0 && owner.ownerChanged);
    assert(reg.ActiveSlotForGuid(0x400) == -1 && reg.ActiveSlotForGuid(0x500) == 0);

    // A destroyed frame frees its slot for another frame.
    reg.Destroy(&b);
    assert(!reg.ActiveAt(1) && reg.FrameAt(1) == nullptr);
    auto reuse = reg.Show(&c, 0x600);
    assert(reuse.slot == 1 && reuse.created);

    // Indices outside 1..64 never resolve.
    assert(!reg.GuidAtIndex(0, guid) && !reg.GuidAtIndex(65, guid));

    // A guid of zero is not a unit.
    assert(reg.ActiveSlotForGuid(0) == -1);

    // The 64 frame limit: the 65th frame gets no token, and the others are unaffected.
    Registry full;
    static int frames[70];
    for (int i = 0; i < 64; ++i) {
        auto r = full.Show(&frames[i], 1000 + i);
        assert(r.slot == i && r.created);
    }
    assert(full.Show(&frames[64], 2000).slot == -1);
    assert(full.ActiveSlotForGuid(2000) == -1);
    assert(full.GuidAtIndex(64, guid) && guid == 1063);
    full.Destroy(&frames[10]);
    assert(full.Show(&frames[64], 2000).slot == 10);

    reg.Reset();
    assert(reg.ActiveSlotForGuid(0x500) == -1 && !reg.GuidAtIndex(1, guid));
    return 0;
}
