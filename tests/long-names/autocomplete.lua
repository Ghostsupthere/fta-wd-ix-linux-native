-- Name suggestion click (AutoCompleteButton_OnClick) with several-word character names, against the Lua embedded in
-- WoW/Source/Ui/Linux/WxlGameScripts.cpp.   Run from the repository root:  lua5.1 tests/long-names/autocomplete.lua
strupper, strfind, strmatch, strsub, gsub, strlen = string.upper, string.find, string.match, string.sub, string.gsub, string.len
-- WoW's string.format understands positional arguments (%2$s); emulate the two forms used here
local realformat = string.format
function string.format(fmt, ...)
  local args = {...}
  if fmt == "%2$s%1$s" then return args[2] .. args[1] end
  if fmt == "%1$s" then return args[1] end
  return realformat(fmt, ...)
end
SLASH_WHISPER1, SLASH_WHISPER2, SLASH_WHISPER3 = "/whisper", "/w", "/tell"
SLASH_SMART_WHISPER1 = "/t"
AUTOCOMPLETE_SIMPLE_REGEX = "(.+)"
AUTOCOMPLETE_SIMPLE_FORMAT_REGEX = "%1$s"
local original = 0
function AutoCompleteButton_OnClick(self) original = original + 1 end
-- stubs so the first block of the script also runs
ChatEdit_ExtractTellTarget = function() end; ChatFrame_SendTell = function() end; SlashCmdList = { FRIENDS = function() end }
local src = io.open(arg[1] or "WoW/Source/Ui/Linux/WxlGameScripts.cpp"):read("*a")
local script = assert(src:match('R"LUA%((.-)%)LUA"'), "no embedded script found")
local f = assert(loadstring(script)); f()
local function click(text, name, chat)
  local box = { text = text, GetText = function(s) return s.text end, SetText = function(s, t) s.text = t end }
  if chat then box.autoCompleteRegex = "^(([^%s]+)%s+)([^%s]+)"; box.autoCompleteFormatRegex = "%2$s%1$s" end
  local popup = { parent = box, Hide = function() end }
  local button = { GetText = function() return name end, GetParent = function() return popup end }
  AutoCompleteButton_OnClick(button)
  return box.text
end
local N = "Arthas Did Nothing Wrong"
local r
r = click("/w Arth hello", N, true);   print(r); assert(r == '/w "Arthas Did Nothing Wrong" hello')
r = click("/w Arth", N, true);         print(r); assert(r == '/w "Arthas Did Nothing Wrong" ')
r = click("/whisper Arth", N, true);   assert(r == '/whisper "Arthas Did Nothing Wrong" ')
r = click("/invite Arth", N, true);    print(r); assert(r == '/invite Arthas Did Nothing Wrong')
r = click("Arth", N, false);           print(r); assert(r == N)
local before = original
r = click("/w Bob", "Bobby", true);    assert(original == before + 1)   -- single word: stock handler
print("ok")
