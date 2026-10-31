#include "WoW/Source/Ui/Linux/WxlGameScripts.h"

#if defined(WOWDECOMP_LINUX)

#include <cstring>
#include <string>
#include <vector>

#include "Engine/Source/FrameScript/FrameScript.h"
#include "Engine/Source/ModernAssets/NativeDiagnostic.h"

namespace {

struct Script {
    std::string name;
    std::string source;
};

std::vector<Script>& Scripts() {
    static std::vector<Script> scripts;
    return scripts;
}

}  // namespace

void WxlRegisterGameScript(const char* name, const char* source) {
    if (!name || !*name || !source || !*source) return;
    auto& scripts = Scripts();
    for (const Script& existing : scripts) {
        if (existing.name == name) return;  // function registration repeats on every UI load
    }
    scripts.push_back({name, source});
}

void WxlRunGameScripts() {
    for (const Script& script : Scripts()) {
        FrameScript_Execute(script.source.c_str(), script.name.c_str(), nullptr);
        WxlNativeDiagnostic("game-scripts: ran '%s'", script.name.c_str());
    }
}

#endif  // WOWDECOMP_LINUX
