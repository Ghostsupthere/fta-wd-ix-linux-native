#pragma once
// Required-addon policy for the client-owned addon bundle (Data\\addon.MPQ), ported from the Waystone x64 client source
// (Integrations/WarcraftXL/RequiredAddons.h and RequiredAddonsPolicy.h, merged into one header). Header-only, no engine types,
// so tests/required-addons/policy.cpp builds it standalone.
#include <cstddef>
#include <initializer_list>
#include <string_view>

namespace wxl::source::required_addons
{
    // Match WXL's opt-in TOC values without a locale, allocation, Lua hook or polling frame.
    inline bool Tagged(std::string_view value)
    {
        auto space = [](char c) { return c == ' ' || (c >= '\t' && c <= '\r'); };
        while (!value.empty() && space(value.front())) value.remove_prefix(1);
        while (!value.empty() && space(value.back())) value.remove_suffix(1);
        for (auto accepted : {"1", "true", "yes", "on", "required", "locked"})
        {
            std::string_view expected(accepted);
            if (value.size() != expected.size()) continue;
            bool equal = true;
            for (size_t i = 0; i < value.size(); ++i)
            {
                auto c = value[i];
                if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
                equal = equal && c == expected[i];
            }
            if (equal) return true;
        }
        return false;
    }

    inline constexpr char ManifestPath[] = "Interface\\AddOns\\NativeAddons.manifest";
    inline constexpr std::size_t MaximumManifestBytes = 64 * 1024;
    inline constexpr std::size_t MaximumManifestEntries = 1024;
    inline constexpr std::size_t MaximumNameBytes = 96;

    // Visibility is independent of signature/trust and the full loader catalog.
    inline bool PubliclyListed(bool blizzard, bool required)
    {
        return !blizzard && !required;
    }

    inline bool ManifestName(std::string_view name)
    {
        if (name.empty() || name.size() > MaximumNameBytes || name == "." || name == ".." || name.back() == '.')
            return false;

        for (unsigned char c : name)
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                c == '_' || c == '-' || c == '!' || c == '.' || c == ' '))
                return false;

        return true;
    }

    // This manifest discovers TOCs; it never confers required status or executable trust.
    // The normal TOC parser, signature policy, dependency graph and load-on-demand rules still apply.
    template<class Visitor>
    void VisitManifest(std::string_view text, Visitor&& visit)
    {
        if (text.size() > MaximumManifestBytes)
            return;
        if (text.substr(0, 3) == "\xEF\xBB\xBF")
            text.remove_prefix(3);

        std::size_t entries = 0;
        while (!text.empty() && entries < MaximumManifestEntries)
        {
            auto end = text.find_first_of("\r\n");
            auto line = text.substr(0, end);
            text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
            while (!line.empty() && (line.front() == ' ' || line.front() == '\t'))
                line.remove_prefix(1);
            while (!line.empty() && (line.back() == ' ' || line.back() == '\t'))
                line.remove_suffix(1);
            if (line.empty() || line.front() == '#')
                continue;
            ++entries;
            if (ManifestName(line))
                visit(line);
        }
    }
}
