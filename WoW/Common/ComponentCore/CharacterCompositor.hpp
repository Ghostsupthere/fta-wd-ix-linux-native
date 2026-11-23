#pragma once

#include "Engine/Source/BLPFile/Types.h"
#include <algorithm>
#include <array>
#include <cstdint>

namespace wxl::compositor {

constexpr uint32_t MaximumLevel = 10;

constexpr int ScaleCoordinate(int coordinate, uint32_t level)
{
    return level > 9 ? coordinate * 2 : coordinate >> (9 - level);
}

inline uint32_t ReadAlpha(const uint8_t* alpha, uint32_t index, unsigned bits)
{
    switch (bits) {
        case 8: return alpha[index];
        case 4: return ((alpha[index >> 1] >> ((index & 1) * 4)) & 15) * 17;
        case 1: return ((alpha[index >> 3] >> (index & 7)) & 1) * 255;
        default: return 255;
    }
}

// Interpolate inside one atlas section. The four taps never cross its edges.
inline void ScaleSection(const uint8_t* pixels, const BlpPalPixel* palette,
                         unsigned width, unsigned height, unsigned alphaBits,
                         unsigned sourceX, unsigned sourceY, unsigned sourceWidth, unsigned sourceHeight,
                         C4Pixel* output, unsigned stride, unsigned shift)
{
    const unsigned factor = 1u << shift;
    const auto* alpha = pixels + width * height;
    for (unsigned y = 0; y < sourceHeight * factor; ++y) {
        const unsigned row = (sourceY + (y >> shift)) * width + sourceX;
        const unsigned nextRow = std::min((y >> shift) + 1, sourceHeight - 1) * width + sourceY * width + sourceX;
        const unsigned fy = y & (factor - 1);
        for (unsigned x = 0; x < sourceWidth * factor; ++x) {
            const unsigned col = x >> shift;
            const unsigned nextCol = std::min(col + 1, sourceWidth - 1);
            const unsigned fx = x & (factor - 1);
            const std::array<unsigned, 4> indices{row + col, row + nextCol, nextRow + col, nextRow + nextCol};
            const std::array<unsigned, 4> weights{(factor - fx) * (factor - fy), fx * (factor - fy), (factor - fx) * fy, fx * fy};
            unsigned b = 0, g = 0, r = 0, a = 0;
            for (unsigned i = 0; i < 4; ++i) {
                const auto& color = palette[pixels[indices[i]]];
                b += color.m_b * weights[i];
                g += color.m_g * weights[i];
                r += color.m_r * weights[i];
                a += ReadAlpha(alpha, indices[i], alphaBits) * weights[i];
            }
            const unsigned opacity = a >> (shift * 2);
            const auto store = [&](uint8_t& channel, unsigned sum) {
                const unsigned value = sum >> (shift * 2);
                if (opacity == 255)
                    channel = static_cast<uint8_t>(value);
                else if (opacity > 0)
                    channel = static_cast<uint8_t>((value * opacity + channel * (255 - opacity) + 127) / 255);
            };
            auto& pixel = output[y * stride + x];
            store(pixel.m_b, b);
            store(pixel.m_g, g);
            store(pixel.m_r, r);
            pixel.m_a = 255;
        }
    }
}

inline void DownsampleFinalBlock(const C4Pixel* source, C4Pixel* destination)
{
    for (unsigned y = 0; y < 4; ++y) {
        for (unsigned x = 0; x < 4; ++x) {
            const auto* top = source + y * 16 + x * 2;
            const auto* bottom = top + 8;
            const auto average = [&](uint8_t C4Pixel::*channel) {
                return static_cast<uint8_t>((top[0].*channel + top[1].*channel +
                                             bottom[0].*channel + bottom[1].*channel + 2) / 4);
            };
            destination[y * 4 + x] = {average(&C4Pixel::m_b), average(&C4Pixel::m_g), average(&C4Pixel::m_r), 255};
        }
    }
}

}
