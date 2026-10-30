// Scoped WMO visual side data. Copyright (C) 2026 WarcraftXL. GPLv3.
#pragma once

#include "WmoTranslate.hpp"
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace wxl::modern::assets::wmo::visual
{
    struct Material
    {
        uint32_t index = 0, layers = 0, flags = 0, tint = 0;
        std::array<std::string, 8> textures;
    };
    struct Root
    {
        std::vector<Material> materials;
    };
    struct Vertex
    {
        float uv[4][2]{};
        uint32_t weights = 0;
    };
    static_assert(sizeof(Vertex) == 36);
    struct Group
    {
        uint32_t uvSets = 0;
        bool hasWeights = false;
        std::vector<Vertex> vertices;
    };

    // Called ONLY by the ZA/ZB storage transforms, after canonical conversion. Root is only tagged
    // when TranslateWmoRoot positively selected it; a scoped group may already be native-shaped.
    bool AppendRoot(std::span<const uint8_t> source, const ResolveCtx& resolver, std::vector<uint8_t>& body);
    bool AppendGroup(std::span<const uint8_t> source, std::vector<uint8_t>& body);
    // Validates the entire envelope before publishing anything. bodySize excludes the private tail.
    bool ReadRoot(std::span<const uint8_t> served, Root& out, uint32_t& bodySize);
    bool ReadGroup(std::span<const uint8_t> served, Group& out, uint32_t& bodySize);
    constexpr bool ReopensExterior(uint32_t flags, uint16_t transitionBatches)
    {
        return (flags & 0x40) || ((flags & 0x2000) && ((flags & 0x20000000) || transitionBatches));
    }
}
