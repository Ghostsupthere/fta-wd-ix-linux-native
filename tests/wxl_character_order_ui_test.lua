local file = assert(io.open("WoW/Source/Glue/CharacterOrderUI.h"))
local header = file:read("*a")
file:close()
local source = assert(header:match('R"lua%((.*)%)lua"'))
local frames, hooks = {}, {}
CharacterSelect = {selectedIndex = 1}
CharacterSelectCharacterFrame = {}
MAX_CHARACTERS_DISPLAYED = 2
WORKT_CHARACTER_OFFSET = 0
local function noop() end
function CreateFrame(kind, name, parent, template)
    assert(not template or name)
    assert(kind == "Button" and parent == CharacterSelectCharacterFrame and not frames[name])
    local frame = setmetatable({
        Enable = function(self) self.enabled = true end,
        Disable = function(self) self.enabled = false end,
        SetScript = function(self, event, action) self[event] = action end,
    }, {__index = function() return noop end})
    frames[name], _G[name] = frame, frame
    return frame
end
function hooksecurefunc(name, hook) hooks[name] = hook end
function GetNumCharacters() return 4 end
function UpdateCharacterList() if hooks.UpdateCharacterList then hooks.UpdateCharacterList() end end
function WXL_MoveSelectedCharacter(direction) return CharacterSelect.selectedIndex + direction end
assert(loadstring(source))()
local up, down = WXLCharacterOrderUp, WXLCharacterOrderDown
assert(not up.enabled and down.enabled)
down.OnClick()
assert(CharacterSelect.selectedIndex == 2 and up.enabled and down.enabled)
down.OnClick()
assert(CharacterSelect.selectedIndex == 3 and WORKT_CHARACTER_OFFSET == 2)
down.OnClick()
assert(CharacterSelect.selectedIndex == 4 and not down.enabled)
up.OnClick()
assert(CharacterSelect.selectedIndex == 3 and up.enabled and down.enabled)
assert(loadstring(source))()
print("Character ordering buttons, bounds, paging and repeat installation pass")
