// Portable weapon-family and palette-eligibility policy. GPL-3.0-or-later.
#pragma once

#include <cstddef>
#include <cstdint>

namespace wxl::weaponflip
{
    enum class Hand : uint8_t { None, Main, Off };

    constexpr bool MayUseWeaponAttachment(uint32_t attachment) noexcept
    {
        switch (attachment)
        {
            case 1: case 2:                         // Held hands.
            case 26: case 27: case 30: case 31:     // Back/sheath anchors.
            case 32: case 33:                      // Hip eligibility probes.
                return true;
            default:
                return false;
        }
    }

    constexpr bool NeedsPathClassification(uint32_t attachment, uintptr_t context, uintptr_t ownerRootHint) noexcept
    {
        // Unusual child anchors can still provide an ancestry edge; unrelated root models cannot.
        return context && (MayUseWeaponAttachment(attachment) || (ownerRootHint && context != ownerRootHint));
    }

    constexpr unsigned char LowerAscii(unsigned char value) noexcept
    {
        return value >= 'A' && value <= 'Z' ? static_cast<unsigned char>(value + ('a' - 'A')) : value;
    }

    inline bool ContainsAsciiInsensitive(const char* value, size_t capacity, const char* token) noexcept
    {
        if (!value || !token || !*token)
            return false;
        size_t tokenLength = 0;
        while (token[tokenLength])
            ++tokenLength;
        if (tokenLength > capacity)
            return false;
        for (size_t offset = 0; offset + tokenLength <= capacity; ++offset)
        {
            if (!value[offset])
                return false;
            size_t index = 0;
            for (; index < tokenLength; ++index)
            {
                const auto actual = static_cast<unsigned char>(value[offset + index]);
                if (!actual || LowerAscii(actual) != LowerAscii(static_cast<unsigned char>(token[index])))
                    break;
            }
            if (index == tokenLength)
                return true;
        }
        return false;
    }

    struct WeaponFamily
    {
        bool oneHanded = false;
        bool swordAxeOrMace = false;
        bool explicitTwoHanded = false;
        bool weaponAsset = false;

        constexpr bool Relevant() const noexcept { return weaponAsset || oneHanded || swordAxeOrMace; }
    };

    // The shared model's bounded path is classified once per callback, never cached across
    // asynchronous model completion, instance reuse, or model/attachment changes.
    inline WeaponFamily ClassifyWeaponPath(const char* path, size_t capacity) noexcept
    {
        WeaponFamily family;
        if (!ContainsAsciiInsensitive(path, capacity, "item\\objectcomponents\\weapon\\") &&
            !ContainsAsciiInsensitive(path, capacity, "item/objectcomponents/weapon/"))
            return family;

        family.weaponAsset = true;
        family.oneHanded = ContainsAsciiInsensitive(path, capacity, "1h") ||
            ContainsAsciiInsensitive(path, capacity, "dagger") ||
            ContainsAsciiInsensitive(path, capacity, "knife") ||
            ContainsAsciiInsensitive(path, capacity, "fistweapon") ||
            ContainsAsciiInsensitive(path, capacity, "fist_weapon") ||
            ContainsAsciiInsensitive(path, capacity, "wand");
        family.swordAxeOrMace = ContainsAsciiInsensitive(path, capacity, "sword") ||
            ContainsAsciiInsensitive(path, capacity, "axe") ||
            ContainsAsciiInsensitive(path, capacity, "mace") ||
            ContainsAsciiInsensitive(path, capacity, "hammer") ||
            ContainsAsciiInsensitive(path, capacity, "club");
        family.explicitTwoHanded = family.swordAxeOrMace &&
            ContainsAsciiInsensitive(path, capacity, "2h");
        return family;
    }

    struct AttachmentEligibility
    {
        bool back = false;
        bool drawnBack = false;
        Hand held = Hand::None;
        Hand sheathed = Hand::None;

        bool Any() const noexcept { return back || drawnBack || held != Hand::None || sheathed != Hand::None; }
    };

    constexpr AttachmentEligibility EvaluateAttachment(uint32_t attachment, WeaponFamily family) noexcept
    {
        AttachmentEligibility result;
        const bool hand = attachment == 1 || attachment == 2;
        result.back = (attachment == 26 || attachment == 27 || attachment == 30 || attachment == 31) &&
            family.Relevant();
        result.drawnBack = hand && family.explicitTwoHanded;
        if (hand && !result.drawnBack && family.oneHanded)
            result.held = attachment == 1 ? Hand::Main : Hand::Off;
        if (family.oneHanded)
        {
            if (attachment == 26 || attachment == 30 || attachment == 32)
                result.sheathed = Hand::Main;
            else if (attachment == 27 || attachment == 31 || attachment == 33)
                result.sheathed = Hand::Off;
        }
        return result;
    }
}
