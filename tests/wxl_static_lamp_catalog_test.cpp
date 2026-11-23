#include "Engine/Source/ModernAssets/WxlSurfaceLights/StaticLampCatalog.hpp"
#include <cassert>
#include <fstream>

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
    if (argc == 2) {
        std::ifstream file(argv[1]);
        assert(file);
        assert(ReadStaticLampCatalog(file).size() >= 100);
    }
}
