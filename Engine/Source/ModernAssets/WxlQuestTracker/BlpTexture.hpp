// Port of the supplied wxl-quest-marker TextureLoader.cpp (GPL-3.0-or-later, Copyright (C) 2026
// WarcraftXL): turns a BLP2 file (DXT1/3/5 or uncompressed BGRA, mip 0 only) into a managed D3D9
// texture for the marker atlas, plus the generated fallback icon. The file read is the caller's.
#pragma once

#include <d3d9.h>

#include <cstdint>
#include <cstring>
#include <limits>

namespace wxl::questtracker {

struct BlpInfo {
    uint32_t width = 0, height = 0;
    D3DFORMAT format = D3DFMT_UNKNOWN;
    uint32_t mipOffset = 0, mipSize = 0;
};

namespace blp_detail {
inline uint32_t Rd32(const uint8_t* p) {
    return p[0] | (p[1] << 8) | (p[2] << 16) | (uint32_t(p[3]) << 24);
}
inline D3DFORMAT AlphaToFormat(uint32_t alphaType) {
    switch (alphaType) {
        case 1: return D3DFMT_DXT3;
        case 7:
        case 8: return D3DFMT_DXT5;
        default: return D3DFMT_DXT1;
    }
}
}  // namespace blp_detail

inline bool ParseBlpHeader(const uint8_t* data, size_t size, BlpInfo& info) {
    if (!data || size < 0x94 || blp_detail::Rd32(data) != 0x32504C42) return false;
    const uint32_t encoding = data[0x08];
    const uint32_t width = blp_detail::Rd32(data + 0x0C);
    const uint32_t height = blp_detail::Rd32(data + 0x10);
    const uint32_t mipOffset = blp_detail::Rd32(data + 0x14);
    const uint32_t mipSize = blp_detail::Rd32(data + 0x54);
    if (!width || !height || width > 4096 || height > 4096) return false;
    if (encoding == 2) info.format = blp_detail::AlphaToFormat(data[0x0A]);
    else if (encoding == 3) info.format = D3DFMT_A8R8G8B8;
    else return false;
    if (mipOffset < 0x94 || !mipSize || mipOffset > size || mipSize > size - mipOffset) return false;
    info.width = width;
    info.height = height;
    info.mipOffset = mipOffset;
    info.mipSize = mipSize;
    return true;
}

inline IDirect3DTexture9* CreateBlpTexture(IDirect3DDevice9* device, const uint8_t* data, size_t size) {
    BlpInfo info;
    if (!device || !ParseBlpHeader(data, size, info)) return nullptr;
    IDirect3DTexture9* texture = nullptr;
    if (FAILED(device->CreateTexture(info.width, info.height, 1, 0, info.format, D3DPOOL_MANAGED, &texture, nullptr)) ||
        !texture)
        return nullptr;
    D3DLOCKED_RECT locked;
    if (FAILED(texture->LockRect(0, &locked, nullptr, 0))) {
        texture->Release();
        return nullptr;
    }
    const bool bgra = info.format == D3DFMT_A8R8G8B8;
    const uint64_t rowBytes = bgra ? uint64_t(info.width) * 4u
                                   : uint64_t((info.width + 3u) / 4u) * (info.format == D3DFMT_DXT1 ? 8u : 16u);
    const uint32_t rows = bgra ? info.height : (info.height + 3u) / 4u;
    if (rowBytes > uint64_t(std::numeric_limits<int>::max()) || rowBytes * rows > info.mipSize ||
        locked.Pitch < int(rowBytes)) {
        texture->UnlockRect(0);
        texture->Release();
        return nullptr;
    }
    const uint8_t* source = data + info.mipOffset;
    auto* destination = static_cast<uint8_t*>(locked.pBits);
    for (uint32_t row = 0; row < rows; ++row)
        std::memcpy(destination + size_t(row) * locked.Pitch, source + size_t(row) * size_t(rowBytes), size_t(rowBytes));
    texture->UnlockRect(0);
    return texture;
}

// Generated diamond used when the atlas is missing or unreadable.
inline IDirect3DTexture9* CreateFallbackTexture(IDirect3DDevice9* device) {
    if (!device) return nullptr;
    IDirect3DTexture9* texture = nullptr;
    if (FAILED(device->CreateTexture(64, 64, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &texture, nullptr)) || !texture)
        return nullptr;
    D3DLOCKED_RECT locked;
    if (FAILED(texture->LockRect(0, &locked, nullptr, 0))) {
        texture->Release();
        return nullptr;
    }
    auto* rows = static_cast<uint8_t*>(locked.pBits);
    for (int y = 0; y < 64; ++y) {
        auto* pixels = reinterpret_cast<uint32_t*>(rows + y * locked.Pitch);
        for (int x = 0; x < 64; ++x) {
            const float cx = float(x) - 31.5f, cy = float(y) - 31.5f;
            const float d = (cx < 0 ? -cx : cx) + (cy < 0 ? -cy : cy);
            uint32_t c = 0;
            if (d < 28.0f) {
                float t = 1.0f;
                if (d > 24.0f) t = (28.0f - d) / 4.0f;
                c = ((uint32_t(t * 255.0f) & 0xFF) << 24) | 0x00FFCC00;
            }
            pixels[x] = c;
        }
    }
    texture->UnlockRect(0);
    return texture;
}

}  // namespace wxl::questtracker
