#include "WoW/Source/WorldClient/RelinkProbe.h"

#include <cassert>

int main() {
    using namespace RelinkProbe;
    assert(!BeginMovementFrame(0));
    assert(!BeginMovementFrame(63));
    EndMovementFrame(4);
    const uint16_t faces[] = {9, 4, 3};
    const uint16_t reordered[] = {4, 9, 3};
    const auto base = Fingerprint(faces, 3);
    assert(base.admitted == 3 && base.fingerprint != Fingerprint(reordered, 3).fingerprint);

    assert(BeginMovementFrame(64));
    {
        MoverScope wrongOrdinal(64, 0, 50);
        assert(!Sampling());
    }
    {
        MoverScope first(64, 1, 51);
        assert(Sampling());
        RecordGroup(10, 2, "House Interior.wmo", 1000.1, base, 8, 3);
        RecordGroup(10, 2, "House Interior.wmo", 1000, base, 5, 3);
        RecordGroup(20, 1, "Barn.wmo", 500, base, 4, 3);
        for (unsigned i = 0; i < 22; ++i) RecordGroup(30, i, "Bulk.wmo", 1000, base, 4, 3);
    }
    assert(!Sampling() && !BeginMovementFrame(64));

    assert(BeginMovementFrame(65));
    {
        MoverScope wrongMover(65, 1, 100);
        assert(!Sampling());
    }
    {
        // A change in order is allowed. The owner identity selects the second sample.
        MoverScope second(65, 3, 51);
        assert(Sampling());
        RecordGroup(10, 2, "House Interior.wmo", 1000.1, base, 8, 3);
        RecordGroup(10, 2, "House Interior.wmo", 1000, Fingerprint(reordered, 3), 6, 3);
        RecordGroup(40, 4, "Bridge.wmo", 90, base, 3, 3);
    }
    assert(!Sampling() && !BeginMovementFrame(66));
    assert(BeginMovementFrame(128));
    {
        MoverScope nextPair(128, 2, 53);
        assert(Sampling());
    }
    assert(BeginMovementFrame(130) == false);
}
