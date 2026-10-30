// Local equipment scale policy; no shared M2 data or owner state. GPL-3.0-or-later.
#pragma once

#include "WeaponPalette.hpp"

#include <cmath>

namespace wxl::equipmentscale
{
    enum class Kind : uint8_t { None, Weapon, Shield };

    constexpr float kMinimum = 1.0f / 1.5f;
    constexpr float kMaximum = 1.5f;

    constexpr bool IsShieldAttachment(uint32_t attachment) noexcept
    {
        return attachment == 0 || attachment == 28 || attachment == 49;
    }

    constexpr bool MayUseAttachment(uint32_t attachment) noexcept
    {
        // Canonical M2 attachment IDs: shield, shield sheath, shield without glove,
        // plus the existing hand/back/hip weapon anchors.
        return IsShieldAttachment(attachment) || weaponflip::MayUseWeaponAttachment(attachment);
    }

    constexpr bool NeedsPathClassification(uint32_t attachment, uintptr_t context, uintptr_t ownerRootHint) noexcept
    {
        return context && (MayUseAttachment(attachment) || (ownerRootHint && context != ownerRootHint));
    }

    inline Kind ClassifyPath(char const* path, size_t capacity) noexcept
    {
        // Equipment directories cover bucklers, staves, polearms and custom
        // filenames without confusing a shield-themed weapon with a real shield.
        if (weaponflip::ContainsAsciiInsensitive(path, capacity, "item\\objectcomponents\\shield\\") ||
            weaponflip::ContainsAsciiInsensitive(path, capacity, "item/objectcomponents/shield/"))
            return Kind::Shield;
        if (weaponflip::ContainsAsciiInsensitive(path, capacity, "item\\objectcomponents\\weapon\\") ||
            weaponflip::ContainsAsciiInsensitive(path, capacity, "item/objectcomponents/weapon/"))
            return Kind::Weapon;
        return Kind::None;
    }

    constexpr Kind ForAttachment(uint32_t attachment, Kind modelKind) noexcept
    {
        if (modelKind == Kind::None || !MayUseAttachment(attachment))
            return Kind::None;
        // Custom shields can live in the Weapon directory. Shield-only anchors
        // still select the shield setting without relying on filename tokens.
        return IsShieldAttachment(attachment) ? Kind::Shield : modelKind;
    }

    constexpr bool MayScaleOwner(bool ui, bool registeredUi, bool localIdentity,
        bool positiveAncestry, bool hasRootHint, bool unchangedParent) noexcept
    {
        return ui ? registeredUi : localIdentity && (positiveAncestry || (!hasRootHint && unchangedParent));
    }

    inline bool Normalize(double requested, float& result) noexcept
    {
        if (!std::isfinite(requested))
            return false;
        result = requested < kMinimum ? kMinimum : requested > kMaximum ? kMaximum :
            static_cast<float>(requested);
        return true;
    }

    inline void Apply(float matrix[16], float desired, float inherited = 1.0f) noexcept
    {
        // Pre-multiply the row-vector attachment transform by uniform scale.
        // Preserve the attachment origin, translation and homogeneous row. A
        // recursive palette build for the same instance replaces its outer scale.
        float const factor = desired / inherited;
        for (uint32_t index = 0; index < 12; ++index)
            matrix[index] *= factor;
    }
}
