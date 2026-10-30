#include "WoW/Common/ComponentCore/WxlEquipmentOwner.h"
#include <cassert>

int main() {
    int model, shared, data, scene, replacement;
    WxlEquipmentOwner owner{&model, &shared, &data, &scene, 1, 0, true};
    WxlEquipmentOwnerWatch watch;
    assert(watch.Observe(owner));
    assert(!watch.Observe(owner));
    owner.race = 22;
    assert(watch.AppearanceChanged(owner));
    assert(watch.Observe(owner)); // Same model pointer, changed appearance.
    assert(!watch.AppearanceChanged(owner));
    owner.sex = 1;
    assert(watch.Observe(owner));
    owner.data = &replacement;
    assert(watch.Observe(owner)); // Reloaded skeleton under the same native model.
    owner.scene = &replacement;
    assert(watch.Observe(owner));
    owner.collectionEnabled = false;
    assert(watch.Observe(owner));
    owner.generation = WxlEquipmentGeneration();
    assert(watch.Observe(owner));
    WxlInvalidateWorldEquipment();
    owner.generation = WxlEquipmentGeneration();
    assert(watch.Observe(owner)); // World unload invalidates even an identical cached owner.
    assert(!watch.Observe({}));
    assert(watch.Observe(owner)); // Return to a cached character after no model.
    watch.Reset();
    assert(watch.Observe(owner));
    assert(!watch.Observe(owner));
}
