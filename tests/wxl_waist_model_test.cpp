#include "WoW/Common/ComponentCore/WxlWaistModel.h"
#include <cassert>
#include <limits>

int main() {
    using namespace WxlWaistModel;
    std::string path;
    assert(BuildPath("Buckle", true, path) && path == "Item\\ObjectComponents\\Waist\\Buckle.mdx");
    assert(BuildPath("Item/ObjectComponents/Waist/Buckle.M2", true, path) &&
           path == "Item\\ObjectComponents\\Waist\\Buckle.M2");
    assert(BuildPath("Buckle.BLP", false, path) && path == "Item\\ObjectComponents\\Waist\\Buckle.BLP");
    assert(!BuildPath("../Buckle", true, path));
    assert(!BuildPath("/Buckle", true, path));
    assert(!BuildPath("Buckle.mdx;Other.mdx", true, path));
    assert(!BuildPath(std::string(260, 'x'), true, path));
    assert(!BuildPath("Buckle.tga", false, path));
    assert(TextureMatches("Item\\ObjectComponents\\Waist\\BUCKLE", "item/objectcomponents/waist/buckle.blp"));
    assert(TextureMatches("Item\\ObjectComponents\\Waist\\Buckle.blp", "item/objectcomponents/waist/buckle.blp"));
    assert(!TextureMatches("SolidTexture", "Item\\ObjectComponents\\Waist\\Buckle.blp"));
    assert(!TextureMatches("Other", "Item\\ObjectComponents\\Waist\\Buckle.blp"));
    RetryGate retry{100, 100, 0};
    assert(retry.Due(100) && !retry.Expired(100));
    retry.Failed(100);
    assert(!retry.Due(149) && retry.Due(150));
    assert(retry.Expired(10100));
    for (int i = 1; i < 7; ++i) retry.Failed(retry.next);
    assert(retry.Expired(retry.next));
    const uint32_t wrap = std::numeric_limits<uint32_t>::max() - 25;
    RetryGate wrapped{wrap, wrap, 0};
    wrapped.Failed(wrap);
    assert(!wrapped.Due(20) && wrapped.Due(24) && !wrapped.Expired(24));
}
