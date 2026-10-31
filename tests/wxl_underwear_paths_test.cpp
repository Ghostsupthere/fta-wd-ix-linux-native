#include "Engine/Source/ModernAssets/WxlUnderwear/UnderwearPaths.hpp"
#include <cassert>

int main() {
    using namespace wxl::underwear;
    // Entry zero clears; tops are even and bottoms odd, inside the supported ranges only.
    assert(Allowed(0, 0) && Allowed(0, 1));
    assert(Allowed(335000, 0) && !Allowed(335000, 1) && Allowed(335001, 1) && !Allowed(335001, 0));
    assert(Allowed(335145, 1) && !Allowed(335146, 0) && !Allowed(334999, 1));
    assert(Allowed(335200, 0) && Allowed(335279, 1) && !Allowed(335280, 0));
    assert(Allowed(365200, 0) && Allowed(365229, 1) && !Allowed(365230, 0) && Allowed(365240, 0) && Allowed(365287, 1));
    // The last range accepts tops only.
    assert(Allowed(365300, 0) && Allowed(365342, 0) && !Allowed(365301, 1) && !Allowed(365343, 0));

    assert(TexturePath(0, true).empty());
    assert(TexturePath(335000, true) == "Item\\TextureComponents\\TorsoUpperTexture\\WXL_UW_001_TU_U.blp");
    assert(TexturePath(335001, false) == "Item\\TextureComponents\\LegUpperTexture\\WXL_UW_001_LU_U.blp");
    assert(TexturePath(335144, true) == "Item\\TextureComponents\\TorsoUpperTexture\\WXL_UW_073_TU_U.blp");
    assert(TexturePath(335200, true) == "Item\\TextureComponents\\TorsoUpperTexture\\WXL_RM_01_TU_U.blp");
    assert(TexturePath(335279, false) == "Item\\TextureComponents\\LegUpperTexture\\WXL_RM_40_LU_U.blp");
    assert(TexturePath(365200, true) == "Item\\TextureComponents\\TorsoUpperTexture\\WXL_CT_465200_TU_U.blp");

    // Only even collection tops in the two ranges have a torso-lower band.
    assert(LowerTexturePath(365200) == "Item\\TextureComponents\\TorsoLowerTexture\\WXL_CT_465200_TL_U.blp");
    assert(LowerTexturePath(365228) == "Item\\TextureComponents\\TorsoLowerTexture\\WXL_CT_465228_TL_U.blp");
    assert(LowerTexturePath(365230).empty() && LowerTexturePath(365201).empty() && LowerTexturePath(335000).empty());
    assert(LowerTexturePath(365300) == "Item\\TextureComponents\\TorsoLowerTexture\\WXL_CT_465300_TL_U.blp");
    assert(LowerTexturePath(365343).empty() && LowerTexturePath(0).empty());
    return 0;
}
