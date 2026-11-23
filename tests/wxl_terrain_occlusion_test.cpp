#include "Engine/Source/ModernAssets/WxlVisuals/terrain_occlusion.h"

#include <cassert>

int main()
{
    using namespace wxl::atmosphere;
    constexpr int side = GroundFloor::Side;
    std::array<FloorTexel, GroundFloor::CellCount> heights;
    TerrainShade shade;
    heights.fill({0, 0, 0, 1});
    BuildTerrainShade(heights.data(), 1, 0, 0.25f, shade);
    for (unsigned texel : shade) assert(texel == 0);

    heights[16 * side + 20].height = 30;
    BuildTerrainShade(heights.data(), 1, 0, 0.25f, shade);
    assert(shade[16 * side + 18] == 0xFF000000u);
    assert(shade[16 * side + 22] == 0);
    assert(shade[16 * side] == 0);
    BuildTerrainShade(heights.data(), -1, 0, 0.25f, shade);
    assert(shade[16 * side + 18] == 0);
    assert(shade[16 * side + 22] == 0xFF000000u);
    BuildTerrainShade(heights.data(), 1, 0, 1, shade);
    assert(shade[16 * side + 18] == 0);
    heights[16 * side + 20].weight = 0;
    BuildTerrainShade(heights.data(), 1, 0, 0.25f, shade);
    assert(shade[16 * side + 18] == 0);
}
