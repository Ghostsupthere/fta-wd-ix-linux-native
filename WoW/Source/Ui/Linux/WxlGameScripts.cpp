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

// FTA: character names of several words. A whisper target ends at the first space unless it is a name the client already knows (friends, guild,
// group, recent whispers), so an unknown spaced name is written in quotes: /w "Name With Spaces" message. Whisper clicks and Reply quote such a
// name themselves, and /friend takes the quoted form. These wrap the interface's own functions (nothing of the interface is copied or replaced).
const char* const kLongNamesScript = R"LUA(
if ChatEdit_ExtractTellTarget and ChatFrame_SendTell and SlashCmdList and SlashCmdList["FRIENDS"] then
  local originalExtract = ChatEdit_ExtractTellTarget
  function ChatEdit_ExtractTellTarget(editBox, msg)
    local target = strmatch(msg, "%s*(.*)")
    if target and strsub(target, 1, 1) == '"' then
      local name, rest = strmatch(target, '^"([^"]+)"%s(.*)$')
      if name then
        editBox:SetAttribute("tellTarget", name)
        editBox:SetAttribute("chatType", "WHISPER")
        editBox:SetText(rest)
        ChatEdit_UpdateHeader(editBox)
        return true
      end
      return false   -- the closing quote is not typed yet
    end
    return originalExtract(editBox, msg)
  end
  local originalSendTell = ChatFrame_SendTell
  function ChatFrame_SendTell(name, chatFrame)
    if name and strfind(name, " ", 1, true) then name = '"' .. name .. '"' end
    return originalSendTell(name, chatFrame)
  end
  local originalFriend = SlashCmdList["FRIENDS"]
  SlashCmdList["FRIENDS"] = function(msg, ...)
    local player, note = strmatch(msg or "", '%s*"([^"]+)"%s*(.*)')
    if player then AddOrRemoveFriend(player, note) return end
    return originalFriend(msg, ...)
  end
end
)LUA";

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
    FrameScript_Execute(kLongNamesScript, "FTA_LongNames", nullptr);
    for (const Script& script : Scripts()) {
        FrameScript_Execute(script.source.c_str(), script.name.c_str(), nullptr);
        WxlNativeDiagnostic("game-scripts: ran '%s'", script.name.c_str());
    }
}

#endif  // WOWDECOMP_LINUX
