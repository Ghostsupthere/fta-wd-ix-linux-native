#include "Engine/Source/ModernAssets/WxlVisuals/ground_floor.h"

#include <cassert>
#include <limits>

namespace {
using namespace wxl::atmosphere;

bool resident = true;

bool Probe(int map, float x, float y, FloorSample& sample)
{
    sample.height = map * 100.0f + y;
    if (x < 0)
        sample.surface = FloorSurface::FreshWater;
    else if (x >= 16)
        sample.surface = FloorSurface::Sea;
    if (x == 12 && y == 4)
        sample.height = std::numeric_limits<float>::quiet_NaN();
    return resident;
}

const FloorTexel& Texel(const GroundFloor& floor, int x, int y)
{
    return floor.Texels()[(y + 16) * GroundFloor::Side + x + 16];
}

void Fill(GroundFloor& floor, int map, const float* camera, double start)
{
    for (int i = 0; i < 256; ++i)
        assert(floor.Update(map, camera, start + i * 0.001, Probe).probes <= GroundFloor::ProbeLimit);
}
}

int main()
{
    GroundFloor floor;
    float camera[3] = {4, 4, 20};
    assert(!floor.HasGrid());
    assert(floor.Update(-1, camera, 0, Probe).probes == 0);
    assert(!floor.HasGrid());
    resident = false;
    Fill(floor, 0, camera, 0);
    assert(floor.HasGrid());
    assert(floor.Known() == 0);
    const auto missingVersion = floor.Version();
    resident = true;
    assert(floor.Update(0, camera, 0.26, Probe).probes == 0);
    assert(floor.Version() == missingVersion);

    Fill(floor, 0, camera, 1);
    assert(floor.Known() == GroundFloor::CellCount - 1);
    assert((Texel(floor, 0, 0) == FloorTexel{4, 0, 0, 1}));
    assert((Texel(floor, -1, 0) == FloorTexel{4, 1, 0, 1}));
    assert((Texel(floor, 2, 0) == FloorTexel{4, 0, 1, 1}));
    assert(Texel(floor, 1, 0).weight == 0); // Reject nonfinite heights.
    auto fullVersion = floor.Version();
    Fill(floor, 0, camera, 1.3);
    assert(floor.Version() == fullVersion);

    float uv[2];
    floor.CameraUV(camera, uv);
    assert(uv[0] == 16.5f / 32 && uv[1] == 16.5f / 32);
    camera[0] = 12;
    floor.Update(0, camera, 1.6, Probe);
    assert(floor.Version() > fullVersion);
    assert((Texel(floor, -1, 0) == FloorTexel{4, 0, 0, 1}));
    assert(floor.Known() >= GroundFloor::CellCount - 33);

    floor.Update(1, camera, 1.7, Probe);
    assert(floor.Known() <= GroundFloor::ProbeLimit);
    assert((Texel(floor, -1, 0) == FloorTexel{104, 0, 0, 1}));
    camera[0] = -516;
    floor.Update(1, camera, 1.8, Probe);
    assert(floor.Known() <= GroundFloor::ProbeLimit);
    assert((Texel(floor, 0, 0) == FloorTexel{104, 1, 0, 1}));
    Fill(floor, 1, camera, 2);
    assert(floor.Known() == GroundFloor::CellCount);
    camera[2] += 150;
    floor.Update(1, camera, 2.3, Probe);
    assert(floor.Known() <= GroundFloor::ProbeLimit);

    resident = false;
    Fill(floor, 1, camera, 5);
    assert(floor.Known() == 0);
    floor.Reset();
    assert(!floor.HasGrid());
    assert(floor.Known() == 0);
    assert(Texel(floor, 0, 0) == FloorTexel{});
}
