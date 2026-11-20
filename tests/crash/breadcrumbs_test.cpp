// Build: g++ -std=c++20 -I. tests/crash/breadcrumbs_test.cpp Engine/Source/Storm/Source/Linux/CrashBreadcrumbs.cpp -o /tmp/breadcrumbs_test && /tmp/breadcrumbs_test
#include "Engine/Source/Storm/Source/Linux/CrashBreadcrumbs.h"
#include <cstdio>
#include <cstring>
#include <string>
using namespace native_crash;
static std::string out;
static void Collect(char const* line) { out += line; }
static int fails = 0, checks = 0;
#define CHECK(c) do { ++checks; if (!(c)) { ++fails; std::printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)
static int Lines() { int n = 0; for (char c : out) n += c == '\n'; return n; }

int main() {
    out.clear(); WriteRecent(Collect);
    CHECK(out.find("(none recorded)") != std::string::npos);
    CHECK(out.find("not initialized") != std::string::npos);

    SetBackend(Backend::D3d9, 4);
    Record(Event::MapLoad, nullptr, 1519);
    Record(Event::ModelLoad, "Creature/Murloc/Murloc.m2");
    Record(Event::ModelFailed, "World/Generic/Human/Passive Doodads/Bad.m2");
    RecordIndexGuard("Item/ObjectComponents/Weapon/Sword.m2", 77, 40);
    Record(Event::MapUnload);
    out.clear(); WriteRecent(Collect);
    CHECK(out.find("Direct3D 9") != std::string::npos);
    CHECK(out.find("map load requested zone=1519") != std::string::npos);
    CHECK(out.find("model load requested Creature/Murloc/Murloc.m2") != std::string::npos);
    CHECK(out.find("model load rejected World/Generic/Human/Passive Doodads/Bad.m2") != std::string::npos);
    CHECK(out.find("index=77 sectionStart=40 Item/ObjectComponents/Weapon/Sword.m2") != std::string::npos);
    CHECK(out.find("map unload requested") != std::string::npos);
    CHECK(out.find("map load") < out.find("map unload"));          // oldest first

    // paths that are not bounded virtual asset paths are never printed
    Record(Event::ModelLoad, "/home/shawn/secret/model.m2");
    Record(Event::ModelLoad, "world/../../etc/passwd");
    Record(Event::ModelLoad, "world/doodad\".m2");
    Record(Event::ModelLoad, "notaroot/model.m2");
    out.clear(); WriteRecent(Collect);
    CHECK(out.find("/home/shawn") == std::string::npos && out.find("passwd") == std::string::npos);
    CHECK(out.find("notaroot") == std::string::npos);
    CHECK(out.find("asset reference omitted") != std::string::npos);

    // the ring keeps the newest 64 events
    for (int i = 0; i < 200; ++i) Record(Event::MapLoad, nullptr, 10000 + i);
    out.clear(); WriteRecent(Collect);
    CHECK(out.find("zone=10199") != std::string::npos);
    CHECK(out.find("zone=10136") != std::string::npos);
    CHECK(out.find("zone=10135") == std::string::npos);
    CHECK(out.find("zone=1519") == std::string::npos);
    if (Lines() != 2 + 64) std::printf("lines=%d\n", Lines());
    CHECK(Lines() == 2 + 64);

    std::printf("%d checks, %d failed\n", checks, fails);
    return fails != 0;
}
