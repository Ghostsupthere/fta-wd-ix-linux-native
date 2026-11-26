// Standalone check of the required-addon policy header. Build and run:
//   g++ -std=c++17 -I. -o /tmp/required-addons-policy tests/required-addons/policy.cpp && /tmp/required-addons-policy
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>
#include "WoW/Source/UIUtil/RequiredAddons.h"

using namespace wxl::source::required_addons;

int main() {
    for (const char* yes : {"1", "true", "TRUE", " yes ", "On", "required", "Locked"}) assert(Tagged(yes));
    for (const char* no : {"", "0", "false", "no", "2", "requiredx", "off"}) assert(!Tagged(no));

    assert(PubliclyListed(false, false));
    assert(!PubliclyListed(true, false));
    assert(!PubliclyListed(false, true));

    for (const char* ok : {"DrakeishUI", "Blizzard_MacroUI", "My Addon", "WoW-Ace3", "!BugGrabber"}) assert(ManifestName(ok));
    for (const char* bad : {"", ".", "..", "x.", "a/b", "a\\b", "a:b", "tab\tname"}) assert(!ManifestName(bad));
    assert(!ManifestName(std::string(MaximumNameBytes + 1, 'a')));
    assert(ManifestName(std::string(MaximumNameBytes, 'a')));

    std::vector<std::string> seen;
    VisitManifest("\xEF\xBB\xBF# comment\r\nDrakeishUI\r\n  NetherBot  \n\n../evil\nGood_One\n", [&](std::string_view n) { seen.emplace_back(n); });
    assert((seen == std::vector<std::string>{"DrakeishUI", "NetherBot", "Good_One"}));

    seen.clear();
    std::string many;
    for (int i = 0; i < 2000; ++i) many += "A" + std::to_string(i) + "\n";
    VisitManifest(many, [&](std::string_view n) { seen.emplace_back(n); });
    assert(seen.size() == MaximumManifestEntries);

    seen.clear();
    VisitManifest(std::string(MaximumManifestBytes + 1, 'a'), [&](std::string_view n) { seen.emplace_back(n); });
    assert(seen.empty());

    std::puts("required-addon policy checks passed");
}
