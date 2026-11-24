#include "WoW/Common/ComponentCore/CharacterSectionCache.hpp"
#include "Engine/Source/BLPFile/ExpandPalette.h"
#include "Engine/Source/Services/TextureAsyncBudget.h"
#include <array>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <vector>

int main() {
    using TextureAsyncBudget::CanAdmit;
    using TextureAsyncBudget::Limit;
    assert(CanAdmit(0, Limit + 1));
    assert(CanAdmit(Limit - 1, 1));
    assert(!CanAdmit(Limit - 1, 2));
    assert(!CanAdmit(Limit + 1, 1));
    uint64_t charged = 160;
    TextureAsyncBudget::Release(charged, 100);
    assert(charged == 60);
    TextureAsyncBudget::Release(charged, 100);
    assert(charged == 0);
    std::array<BlpPalPixel, 256> palette{};
    for (unsigned i = 0; i < palette.size(); ++i)
        palette[i] = {uint8_t(i), uint8_t(i ^ 0xa3), uint8_t(i / 2), uint8_t(i ^ 0x57)};

    const auto check = [&](unsigned count) {
        std::vector<uint8_t> source(count * 2 + 1);
        std::vector<BlpPalPixel> stock(count), fused(count);
        for (unsigned i = 0; i < source.size(); ++i) source[i] = (i * 17 + i / 5) & 255;
        for (unsigned i = 0; i < count; ++i) {
            stock[i] = palette[source[i]];
            stock[i].m_pad = 255;
        }
        for (unsigned i = 0; i < count; ++i) stock[i].m_pad = source[count + i];
        BlpDecode::ExpandAlpha8(fused.data(), source.data(), palette.data(), count);
        if (count) assert(std::memcmp(stock.data(), fused.data(), count * sizeof(BlpPalPixel)) == 0);
    };
    for (unsigned count : {0u, 1u, 7u, 33u, 1023u, 1024u * 1024u}) check(count);

    constexpr unsigned Width = 104, Height = 76, Columns = 90, Rows = 63, Shift = 2, Stride = 370;
    std::vector<uint8_t> pixels(Width * Height);
    std::vector<C4Pixel> stock(Stride * (Rows << Shift)), actual(stock.size());
    for (unsigned i = 0; i < pixels.size(); ++i) pixels[i] = (i * 23 + i / 9) & 255;
    const auto direct = [&] {
        wxl::compositor::ScaleSection(pixels.data(), palette.data(), Width, Height, 0,
            4, 6, Columns, Rows, stock.data(), Stride, Shift);
    };
    const auto cached = [&] {
        const bool handled = wxl::compositor::ReplayOpaqueScale(pixels.data(), palette.data(), Width, Height,
            4, 6, Columns, Rows, actual.data(), Stride, Shift);
        assert(handled);
    };
    const auto same = [&] {
        direct();
        std::memset(actual.data(), 0, actual.size() * sizeof(C4Pixel));
        cached();
        assert(std::memcmp(stock.data(), actual.data(), stock.size() * sizeof(C4Pixel)) == 0);
    };
    same();
    same();
    pixels[6 * Width + 4] ^= 0xff;
    same();
    palette[pixels[6 * Width + 4]].m_r ^= 0xff;
    same();

    assert(!wxl::compositor::ReplayOpaqueScale(pixels.data(), palette.data(), Width, Height,
        4, 6, Columns, Rows, actual.data(), Stride, 5));

    const auto time = [](auto fn) {
        const auto start = std::chrono::steady_clock::now();
        for (unsigned i = 0; i < 160; ++i) fn();
        return std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count() / 160;
    };
    const auto old = time(direct);
    const auto warm = time(cached);
    assert(std::memcmp(stock.data(), actual.data(), stock.size() * sizeof(C4Pixel)) == 0);
    std::printf("Alpha8 byte parity and opaque section parity passed. Opaque section %.1f us stock, %.1f us warm cache\n",
        old, warm);
}
