#include "Engine/Source/ModernAssets/WxlGrass/GrassRigid.hpp"
#include "Engine/Source/ModernAssets/WxlGrass/GrassShaderText.hpp"
#include <cstdio>
#include <cstdlib>
using wxl::grass::IsRigidDetailDoodadName;
static void Require(bool value, const char* label) { if (!value) { std::fprintf(stderr, "FAILED: %s\n", label); std::exit(1); } }
int main() {
    // Shipped GroundEffectDoodad categories: terminal Roc, Bon and Stn words.
    for (const char* name : {"PlagueLandsRoc01", "EplBon04", "ZDStn02", "world\\nodxx\\Detail\\HellfireRock03.m2",
                             "x/y/BoneS_02.mdx", "Boulder", "SKULL07", "BadBus01", "badbus03.m2", "BadBus04", "BadBus06"})
        Require(IsRigidDetailDoodadName(name), name);
    // Foliage and look-alikes keep bending (DurIbu contains the letters "rib" and must not match).
    for (const char* name : {"DurIbu01", "BadBus02", "BadBus05", "BadBus07", "ElwGrs03", "Ferns_01", "Rocket01", "Bonus02", "Stnx", "", "01"})
        Require(!IsRigidDetailDoodadName(name), name);
    // Long or malformed names are bounded and never overrun.
    char longName[600]; for (int i = 0; i < 599; ++i) longName[i] = 'a'; longName[599] = '\0';
    Require(!IsRigidDetailDoodadName(longName), "long name");
    // The shader patch needs both anchors; a shader without them stays stock.
    const std::string good = "vs_3_0\n    add r0, r0, c3\n    mov o1.w, v2.w\n";
    const std::string patched = wxl::grass::text::BuildGrassMotionText(good);
    Require(!patched.empty() && patched.find("if_gt r2.x, c13.x") != std::string::npos && patched.find("sge o1.w, r2.x, c13.y") != std::string::npos, "patch applies");
    Require(patched.find("mov o1.w, v2.w") == std::string::npos, "alpha endpoint restore replaces the pass-through");
    Require(wxl::grass::text::BuildGrassMotionText("vs_3_0\n    mov o1.w, v2.w\n").empty(), "missing transform anchor rejected");
    Require(wxl::grass::text::BuildGrassMotionText("vs_3_0\n    add r0, r0, c3\n").empty(), "missing alpha anchor rejected");
    std::puts("PASS: rigid detail classification and grass shader patch anchors");
}
