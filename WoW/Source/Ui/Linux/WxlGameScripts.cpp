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

-- The suggestion list under the chat box and the mail recipient box (guild, friends, group, recent names) put the clicked name into the box
-- with every space removed, so a several-word name came out as one run-together word that no player has. Keep the spaces. A whisper command
-- splits its target at the first space, so there the name goes in quotes (and the box turns into a whisper at once); any other box takes it as it is.
if AutoCompleteButton_OnClick and AUTOCOMPLETE_SIMPLE_REGEX and AUTOCOMPLETE_SIMPLE_FORMAT_REGEX then
  local originalAutoComplete = AutoCompleteButton_OnClick
  local function isWhisperCommand(command)
    command = strupper(command)
    for _, base in ipairs({"SLASH_WHISPER", "SLASH_SMART_WHISPER"}) do
      local i = 1
      local slash = _G[base .. i]
      while slash do
        if strupper(slash) == command then return true end
        i = i + 1
        slash = _G[base .. i]
      end
    end
    return false
  end
  function AutoCompleteButton_OnClick(self)
    local name = self:GetText()
    local autoComplete = self:GetParent()
    local editBox = autoComplete and autoComplete.parent
    if not (name and editBox and strfind(name, " ", 1, true)) then return originalAutoComplete(self) end
    local text = editBox:GetText()
    local regex = editBox.autoCompleteRegex or AUTOCOMPLETE_SIMPLE_REGEX
    local format = editBox.autoCompleteFormatRegex or AUTOCOMPLETE_SIMPLE_FORMAT_REGEX
    local insert = name
    if editBox.autoCompleteRegex then
      local command = strmatch(text, "^(/%S+)")
      if command and isWhisperCommand(command) then insert = '"' .. name .. '"' end
    end
    local newText = gsub(text, regex, function(...) return string.format(format, insert, ...) end, 1)
    if insert ~= name and strsub(newText, -1) == '"' then newText = newText .. " " end
    editBox:SetText(newText)
    autoComplete:Hide()
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
