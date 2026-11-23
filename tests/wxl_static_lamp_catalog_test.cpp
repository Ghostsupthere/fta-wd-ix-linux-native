#include "Engine/Source/ModernAssets/WxlSurfaceLights/StaticLampCatalog.hpp"
#include "Engine/Source/ModernAssets/WxlSurfaceLights/LightTextureLayout.hpp"
#include <cassert>
#include <fstream>

namespace sl = wxl::surfacelights;
namespace tex = sl::texture;

void CheckProjectedPool(const sl::StaticLamp& lamp) {
    const float world[] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 12,13,0,1};
    const auto placement = sl::PlaceStaticLamp(lamp, world);
    const float eye[] = {12, 10, 2};
    sl::Candidate candidate;
    std::copy_n(placement.pos, 3, candidate.pos);
    candidate.radius = placement.radius;
    candidate.falloff = 1;
    candidate.staticLamp = candidate.supplemental = true;
    candidate.flickerMode = sl::kFlickerOff;
    for (unsigned c = 0; c < 3; ++c) candidate.color[c] = lamp.color[c] * placement.strength;
    const sl::Settings settings;
    const auto pool = sl::ChooseActive({candidate}, settings, eye, 0, {});
    assert(pool.active.size() == 1);
    sl::Grid grid;
    sl::GridStats stats;
    sl::GridBuilder builder;
    builder.Build(pool.active, pool.baseLuminance, eye, settings, grid, stats);
    assert(sl::CountGridCells(grid, 0, pool.active[0]) > 0);
    std::vector<float> pixels(tex::kWidth * 4 * tex::kPoolRows);
    assert(tex::WritePool(pixels.data(), tex::kWidth * 4, pool.active) == 1);
    assert(pixels[14] < 0 && pixels[15] == 0);
    // Evaluate the terrain shader's attenuation at ground pixels under the
    // lamp, using the actual pool/grid representation read by the shader.
    unsigned illuminated = 0;
    for (int y = -4; y <= 4; ++y) {
        for (int x = -4; x <= 4; ++x) {
            const float px = 12.0f + x, py = 13.0f + y;
            const int cellX = static_cast<int>(std::floor((px - grid.originX) / grid.cellSize));
            const int cellY = static_cast<int>(std::floor((py - grid.originY) / grid.cellSize));
            const size_t cell = cellY * grid.cells + cellX;
            if (!grid.count[cell] || grid.index[cell * grid.perCell] != 0) continue;
            const float dx = pixels[0] - px, dy = pixels[1] - py, dz = pixels[2];
            const float distanceSquared = dx * dx + dy * dy + dz * dz;
            const float distance = std::sqrt(distanceSquared);
            const float normalized = std::min(distance * pixels[3], 1.0f);
            const float window = std::pow(1.0f - normalized * normalized, 2.0f * pixels[7]);
            const float normal = std::clamp((dz / distance + 0.5f) / 1.5f, 0.0f, 1.0f);
            const float softened = std::sqrt(distanceSquared + 1.0f);
            const float red = pixels[4] * window * normal / ((0.7f + 0.03f * softened) * softened);
            illuminated += red > 0.05f;
        }
    }
    assert(illuminated >= 21);
}

int main(int argc, char** argv) {
    using namespace wxl::surfacelights;
    std::istringstream sample{
        "# comment\n"
        "World/Fixture/Torch.m2,1,2,3,1,.5,0,10,.75\n"
        "world\\fixture\\torch.m2,2,2,3,1,0,0,9,.75\n"
        "world\\fixture\\dark.m2,0,0,0,1,0,0,99,1\n"
        "world\\fixture\\broken.m2,0,0,0,1,0,0,9,1,extra\n"
        "Spells/Fire.m2,0,0,0,1,0,0,9,1\n"};
    const auto parsed = ReadStaticLampCatalog(sample);
    assert(parsed.size() == 1);
    const auto& lamp = parsed.at("world\\fixture\\torch.m2");
    assert(lamp.offset[0] == 1 && lamp.color[1] == 0.5f && lamp.strength == 0.75f);
    assert(NormalizeLampPath("World/Fixture/Torch.MDX") == "world\\fixture\\torch.m2");
    const StaticLamp streetLamp{{0, 0, 4.25f}, {1, 0.73f, 0.38f}, 15, 1};
    CheckProjectedPool(streetLamp);
    const StaticLamp hanging{{0, 0, 6}, {1, .73f, .25f}, 13, .85f};
    CheckProjectedPool(hanging);
    if (argc == 2) {
        std::ifstream file(argv[1]);
        assert(file);
        const auto catalog = ReadStaticLampCatalog(file);
        assert(catalog.size() >= 100);
        CheckProjectedPool(catalog.at("world\\generic\\human\\passive doodads\\lamps\\stormwindstreetlamp01.m2"));
    }
}
