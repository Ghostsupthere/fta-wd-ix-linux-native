#include "WoW/Common/ComponentCore/CharacterCompositor.hpp"

#include <cassert>

int main()
{
    using namespace wxl::compositor;
    static_assert(ScaleCoordinate(256, 10) == 512);
    static_assert(ScaleCoordinate(128, 10) == 256);
    static_assert(ScaleCoordinate(64, 9) == 64);
    static_assert(ScaleCoordinate(64, 8) == 32);
    static_assert(ScaleCoordinate(64, 7) == 16);

    BlpPalPixel palette[256]{};
    palette[1].m_r = 200;
    palette[2].m_g = 160;
    palette[3].m_b = 100;
    // The rightmost column is outside the source section.
    uint8_t source[] = {1, 2, 3, 1, 2, 3};
    C4Pixel output[80]{};
    output[79].m_g = 99;
    ScaleSection(source, palette, 3, 2, 0, 0, 0, 2, 2, output, 10, 2);
    assert(output[0].m_r == 200 && output[0].m_g == 0);
    assert(output[2].m_r == 100 && output[2].m_g == 80);
    assert(output[7].m_b == 0 && output[7].m_g == 160);
    assert(output[79].m_g == 99);
    ScaleSection(source, palette, 3, 2, 0, 2, 0, 1, 1, output, 10, 3);
    assert(output[77].m_b == 100);

    uint8_t alpha4[] = {1, 2, 0xF0};
    output[0] = {11, 22, 33, 255};
    ScaleSection(alpha4, palette, 2, 1, 4, 0, 0, 2, 1, output, 10, 2);
    assert(output[0].m_b == 11 && output[0].m_g == 22 && output[0].m_r == 33);
    assert(output[4].m_r == 0 && output[4].m_g == 160);
    const uint8_t alpha[] = {0b10, 127};
    assert(ReadAlpha(alpha, 0, 1) == 0 && ReadAlpha(alpha, 1, 1) == 255);
    assert(ReadAlpha(alpha, 1, 8) == 127);

    C4Pixel mip8[64]{}, mip4[16]{};
    mip8[0] = {12, 24, 36, 255};
    mip8[1] = {24, 36, 48, 255};
    mip8[8] = {36, 48, 60, 255};
    mip8[9] = {48, 60, 72, 255};
    mip8[63] = {4, 8, 12, 255};
    DownsampleFinalBlock(mip8, mip4);
    assert(mip4[0].m_b == 30 && mip4[0].m_g == 42 && mip4[0].m_r == 54);
    assert(mip4[15].m_b == 1 && mip4[15].m_g == 2 && mip4[15].m_r == 3);

}
