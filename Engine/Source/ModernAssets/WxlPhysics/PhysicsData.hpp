// Retail PHYS v0-v6 decoding and model association. GPL-3.0-or-later.
#pragma once

#include "domino/physics_domino.h"
#include <span>
#include <string>
#include <vector>

namespace wxl::scripts::itemphysics
{
    namespace domino = wow::ec::physics;

    // Uses the path actually loaded by the client, so ItemDisplayInfo/collection aliases need no IDs.
    std::vector<std::string> CompanionPaths(std::string const& model);
    bool Decode(std::span<uint8_t const> bytes, domino::Model& out, std::string& error);
    bool ValidateBones(domino::Model const& model, size_t count, std::string& error);
    // Match authored rest origins to the effective native skeleton. Never animate an unrelated bone.
    bool BindSkeleton(domino::Model& model, std::span<domino::V3 const> pivots,
        std::span<int16_t const> parents, std::string& error);
}
