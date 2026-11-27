-- Loads the real Kui Nameplates stack (in XML order, with its libraries) plus nameplate_options.lua, drives a
-- plate shaped like the client's CGNamePlateFrame through every style, and writes every generated setting.
-- Needs the full addon tree, so it is skipped for tools/drakeishui. Uses its own permissive widget mock.
local M = ...
local root = M.directory
local probe = io.open(root .. "/modules/Kui_Nameplates/Core.xml")
if not probe then print("Nameplates skipped (no modules/Kui_Nameplates)"); return end
probe:close()

local frames, now, cvars = {}, 100, {}
local methods = {}
local function noop() end
local generic = { "^Set%u", "^Register%u", "^Unregister%u", "^Enable", "^Disable", "^Clear%u", "^Lock%u",
    "^Unlock%u", "^Start%u", "^Stop%u", "^Add%u", "^Raise", "^Lower" }
local meta = { __index = function(_, key)
    if methods[key] then return methods[key] end
    if type(key) == "string" then
        for _, pattern in ipairs(generic) do if key:match(pattern) then return noop end end
    end
end }
local function new(kind, name, parent)
    local f = setmetatable({ _kind = kind, _objectName = name, _parent = parent, scripts = {}, events = {},
        _shown = true, _points = {}, _regions = {}, _children = {} }, meta)
    frames[#frames + 1] = f
    if name then _G[name] = f end
    if parent and rawget(parent, "_children") then
        local list = (kind == "Texture" or kind == "FontString") and parent._regions or parent._children
        list[#list + 1] = f
    end
    return f
end
local function run(self, kind, ...) if self.scripts[kind] then return self.scripts[kind](self, ...) end end
function methods:SetScript(kind, fn) self.scripts[kind] = fn end
function methods:GetScript(kind) return self.scripts[kind] end
function methods:HasScript() return true end
function methods:HookScript(kind, fn)
    local old = self.scripts[kind]
    self.scripts[kind] = function(...) if old then old(...) end fn(...) end
end
function methods:RegisterEvent(event) self.events[event] = true end
function methods:UnregisterEvent(event) self.events[event] = nil end
function methods:UnregisterAllEvents() self.events = {} end
function methods:RegisterAllEvents() self.allEvents = true end
function methods:Show() if not self._shown then self._shown = true; run(self, "OnShow") end end
function methods:Hide() if self._shown then self._shown = false; run(self, "OnHide") end end
function methods:IsShown() return self._shown end
function methods:IsVisible() return self._shown end
function methods:IsObjectType(kind) return self._kind == kind end
function methods:IsEnabled() return true end
function methods:IsProtected() return false end
function methods:IsMouseOver() return false end
function methods:GetObjectType() return self._kind end
function methods:GetName() return self._objectName end
function methods:GetParent() return self._parent end
function methods:SetParent(parent) self._parent = parent end
function methods:SetSize(w, h) self._width, self._height = w, h end
function methods:SetWidth(w) self._width = w end
function methods:SetHeight(h) self._height = h end
function methods:GetWidth() return self._width or 100 end
function methods:GetHeight() return self._height or 20 end
function methods:GetSize() return self:GetWidth(), self:GetHeight() end
function methods:GetCenter() return 400, 300 end
function methods:GetLeft() return 350 end
function methods:GetRight() return 450 end
function methods:GetTop() return 310 end
function methods:GetBottom() return 290 end
function methods:SetScale(s) self._scale = s end
function methods:GetScale() return self._scale or 1 end
function methods:GetEffectiveScale() return self._scale or 1 end
function methods:SetAlpha(a) self._alpha = a end
function methods:GetAlpha() return self._alpha or 1 end
function methods:SetFrameLevel(l) self._level = l end
function methods:GetFrameLevel() return self._level or 1 end
function methods:GetFrameStrata() return "BACKGROUND" end
function methods:SetPoint(...) self._points[#self._points + 1] = { ... } end
function methods:ClearAllPoints() self._points = {} end
function methods:SetAllPoints() self._points = { { "ALL" } } end
function methods:GetNumPoints() return #self._points end
function methods:GetPoint(i) local p = self._points[i or 1]; if p then return unpack(p) end end
function methods:CreateTexture(name, layer) return new("Texture", name, self) end
function methods:CreateFontString(name) return new("FontString", name, self) end
function methods:CreateAnimationGroup() return new("AnimationGroup") end
function methods:CreateAnimation() return new("Animation") end
function methods:GetRegions() return unpack(self._regions) end
function methods:GetChildren() return unpack(self._children) end
function methods:GetNumRegions() return #self._regions end
function methods:GetNumChildren() return #self._children end
function methods:SetTexture(t) self._texture = t end
function methods:GetTexture() return self._texture end
function methods:SetTexCoord(...) self._coords = { ... } end
function methods:GetTexCoord() return 0, 0, 0, 1, 1, 0, 1, 1 end
function methods:SetVertexColor(...) self._vertex = { ... } end
function methods:GetVertexColor() return unpack(self._vertex or { 1, 1, 1, 1 }) end
function methods:SetText(t) self._text = t end
function methods:GetText() return self._text or "" end
function methods:SetFormattedText(fmt, ...) self._text = fmt:format(...) end
function methods:GetStringWidth() return #tostring(self._text or "") * 6 end
function methods:GetStringHeight() return 12 end
function methods:SetFont(path, size, flags) self._font = { path, size, flags }; return true end
function methods:GetFont() if self._font then return unpack(self._font) end return "test.ttf", 12, "" end
function methods:SetFontObject() end
function methods:GetFontString() self._label = self._label or self:CreateFontString(); return self._label end
function methods:SetTextColor(...) self._textColor = { ... } end
function methods:GetTextColor() return unpack(self._textColor or { 1, 1, 1, 1 }) end
function methods:GetShadowColor() return 0, 0, 0, 1 end
function methods:GetShadowOffset() return 1, -1 end
function methods:GetJustifyH() return "CENTER" end
function methods:GetJustifyV() return "MIDDLE" end
function methods:GetDrawLayer() return "ARTWORK" end
function methods:GetStatusBarTexture() self._barTexture = self._barTexture or new("Texture"); return self._barTexture end
function methods:SetStatusBarTexture(t) self:GetStatusBarTexture()._texture = t end
function methods:SetStatusBarColor(...) self._barColor = { ... } end
function methods:GetStatusBarColor() return unpack(self._barColor or { 1, 0, 0, 1 }) end
function methods:SetMinMaxValues(a, b) self._minimum, self._maximum = a, b end
function methods:GetMinMaxValues() return self._minimum or 0, self._maximum or 100 end
function methods:SetValue(v) self._value = v; run(self, "OnValueChanged", v) end
function methods:GetValue() return self._value or 50 end
function methods:GetThumbTexture() return self:CreateTexture() end
function methods:GetNormalTexture() return self:CreateTexture() end
function methods:GetPushedTexture() return self:CreateTexture() end
function methods:GetHighlightTexture() return self:CreateTexture() end
function methods:GetNumber() return 0 end
function methods:GetID() return 0 end
function methods:GetAttribute() end
function methods:GetBackdrop() end
function methods:HasFocus() return false end

function CreateFrame(kind, name, parent) return new(kind, name, parent) end
UIParent = new("Frame", "UIParent"); UIParent:SetSize(1280, 720)
WorldFrame = new("Frame", "WorldFrame"); WorldFrame:SetSize(1280, 720)
GameTooltip = new("GameTooltip", "GameTooltip")
for _, name in ipairs({ "GameFontNormal", "GameFontNormalSmall", "GameFontHighlight", "GameFontHighlightSmall",
    "GameFontDisableSmall", "GameFontNormalLarge", "ChatFontNormal", "GameFontHighlightLarge",
    "NumberFontNormal", "NumberFontNormalSmall" }) do new("Font", name) end
-- Only what the client really has: there is no "EnemyCastBarsOnNameplates" checkbox.
for _, name in ipairs({ "InterfaceOptionsFrame", "InterfaceOptionsCombatPanel",
    "InterfaceOptionsCombatPanelNameplateClassColors" }) do
    new("Frame", name)
end
local errors = {}
function geterrorhandler() return function(message) errors[#errors + 1] = tostring(message) end end
function seterrorhandler() end
function securecall(fn, ...) if type(fn) == "string" then fn = _G[fn] end return fn(...) end
function issecurevariable() return false end
function hooksecurefunc(object, key, handler)
    if type(object) == "string" then handler, key, object = key, object, _G end
    local original = assert(object[key], key)
    object[key] = function(...) local r = { original(...) }; handler(...); return unpack(r) end
end
function ChatEdit_InsertLink() end
function OpenColorPicker() end
function CloseDropDownMenus() end
function LoadAddOn() end
function tDeleteItem(list, item)
    for i = #list, 1, -1 do if list[i] == item then table.remove(list, i) end end
end
tinsert, tremove, wipe = table.insert, table.remove, function(t) for k in pairs(t) do t[k] = nil end return t end
strmatch, strfind, strsub, strlen, strlower, strupper = string.match, string.find, string.sub, string.len, string.lower, string.upper
strrep, strbyte, strchar, gsub, gmatch, format = string.rep, string.byte, string.char, string.gsub, string.gmatch, string.format
floor, ceil, abs, min, max, mod = math.floor, math.ceil, math.abs, math.min, math.max, math.fmod
sort = table.sort
function strtrim(s) return (s:gsub("^%s+", ""):gsub("%s+$", "")) end
function strsplit(sep, s)
    local parts = {}
    for part in (s .. sep):gmatch("(.-)" .. sep:gsub("%p", "%%%0")) do parts[#parts + 1] = part end
    return unpack(parts)
end
function strjoin(sep, ...) return table.concat({ ... }, sep) end
function debugstack() return "" end
function debugprofilestop() return 0 end
function GetTime() return now end
function GetFramerate() return 60 end
function GetLocale() return "enUS" end
function GetRealmName() return "Test Realm" end
function UnitName(unit) if unit == "player" then return "Fixture" end end
function GetUnitName(unit) return UnitName(unit) end
function UnitClass() return "Warrior", "WARRIOR" end
function UnitRace() return "Human", "Human" end
function UnitFactionGroup() return "Alliance", "Alliance" end
function UnitLevel() return 40 end
function UnitExists() return false end
function UnitGUID() end
function UnitIsPlayer() return false end
function UnitCanAttack() return true end
function UnitIsFriend() return false end
function IsLoggedIn() return false end
function IsAddOnLoaded() return false end
function IsInInstance() return false, "none" end
function InCombatLockdown() return false end
function UnitAffectingCombat() return false end
function GetNumRaidMembers() return 0 end
function GetNumPartyMembers() return 0 end
function GetAddOnMetadata() end
function GetAddOnInfo() end
function SetCVar(name, value) cvars[name] = tostring(value) end
function GetCVar(name) return cvars[name] or "1" end
function GetCVarBool(name) return GetCVar(name) == "1" end
function PlaySound() end
function GetScreenWidth() return 1280 end
function GetScreenHeight() return 720 end
function GetSpellInfo() end
function UnitCastingInfo() end
function UnitChannelInfo() end
function GetComboPoints() return 0 end
function GetShapeshiftForm() return 0 end
function GetTalentTabInfo() return "", "", 0 end
function GetActiveTalentGroup() return 1 end
function GetNumTalentTabs() return 3 end
RAID_CLASS_COLORS = { WARRIOR = { r = .78, g = .61, b = .43 } }
CLASS_ICON_TCOORDS, SlashCmdList, StaticPopupDialogs, UISpecialFrames = {}, {}, {}, {}
YES, NO, OKAY, CANCEL, ACCEPT, CLOSE, NAME, LEVEL, HEALTH, NONE, UNKNOWN = "Yes", "No", "Okay", "Cancel", "Accept",
    "Close", "Name", "Level", "Health", "None", "Unknown"
STANDARD_TEXT_FONT, UNKNOWNOBJECT = "test.ttf", "Unknown"
bit = bit or { band = function() return 0 end, bor = function() return 0 end }

-- Load an XML manifest the way the client does, with the exact file name case.
local function loadXML(path)
    local handle = assert(io.open(path), "missing " .. path)
    local text = handle:read("*a"):gsub("<!%-%-.-%-%->", "")
    handle:close()
    local directory = path:match("^(.*)/")
    for tag, file in text:gmatch('<(%a+)%s+file="([^"]+)"') do
        local child = directory .. "/" .. file:gsub("\\", "/")
        if tag == "Include" then loadXML(child)
        elseif tag == "Script" then assert(loadfile(child))("DrakeishUI", {}) end
    end
end
local K = root .. "/modules/Kui_Nameplates/"
for _, manifest in ipairs({ "embeds.xml", "Locales.xml", "Core.xml", "Modules.xml" }) do loadXML(K .. manifest) end

local opened
local addon = { _dir = "test/", Menu = { Close = noop, Open = noop } }
local function load(path) assert(loadfile(root .. "/" .. path))("DrakeishUI", addon) end
load("settings_profiles.lua")
local S = addon.Settings
load("settings_window.lua")
local realOpen = S.Open
S.Open = function(_, section) opened = section end
load("nameplate_options.lua")
DrakeishUI_SocialNE = {
    buttonskin = { Skin = noop }, nineslice = { AttachInset = noop }, scrollbar = { BuildCustomPixel = noop },
    chrome = { Apply = function(frame) frame.CloseButton = CreateFrame("Button", nil, frame) end },
}

local function event(name, ...)
    local list = {}
    for _, f in ipairs(frames) do if f.events[name] or f.allEvents then list[#list + 1] = f end end
    for _, f in ipairs(list) do run(f, "OnEvent", name, ...) end
end
local function noErrors(stage) assert(#errors == 0, stage .. ": " .. tostring(errors[1])) end
event("ADDON_LOADED", "DrakeishUI"); noErrors("addon loaded")
function IsLoggedIn() return true end
event("PLAYER_LOGIN"); noErrors("login")
event("PLAYER_ENTERING_WORLD"); noErrors("entering world")
local Kui = assert(_G.KuiNameplates)
assert(Kui.db and Kui.enabledState ~= false, "Kui did not initialise")
assert(cvars.threatWarning == "3")
assert(_G.DrakeishUI_NameplateMode == 2, "default style is all units")

-- A plate in the client's order: two bars as children, eleven regions.
local plate = new("Frame", nil, WorldFrame); plate:SetSize(369, 92)
local regions = {}
local function region(kind, texture) local r = new(kind, nil, plate); r._texture = texture; regions[#regions + 1] = r; return r end
region("Texture", "Interface\\TargetingFrame\\UI-TargetingFrame-Flash")
local border = region("Texture", "Interface\\Tooltips\\Nameplate-Border")
region("Texture", "Interface\\Tooltips\\Nameplate-Border")
region("Texture", "Interface\\Tooltips\\Nameplate-CastBar-Shield")
region("Texture")
region("Texture", "Interface\\Tooltips\\Nameplate-Glow"):Hide()
local name = region("FontString"); name._text = "Wastewander Thief"
local level = region("FontString"); level._text = "41"
region("Texture", "Interface\\TargetingFrame\\UI-TargetingFrame-Skull"):Hide()
region("Texture"):Hide()
region("Texture"):Hide()
local health = new("StatusBar", nil, plate); health:SetMinMaxValues(0, 100); health._value = 80
health._barColor = { 1, 0, 0, 1 }
local cast = new("StatusBar", nil, plate); cast:Hide()

Kui:OnUpdate(); noErrors("scan")
assert(plate.kui, "the client's plate layout was not recognised")
local function tick()
    now = now + .2
    run(plate, "OnUpdate", .2)
    for _, f in ipairs(frames) do if f ~= plate and f._shown and f.scripts.OnUpdate then run(f, "OnUpdate", .2) end end
end
plate:Hide(); plate:Show(); tick(); tick(); noErrors("show")
assert(plate.kui:IsShown() and border._texture == nil, "Kui should replace the stock art")
assert(plate:GetWidth() == 369, "the client's own plate keeps its size")

assert(S:Set("nameplates.mode", "blizzard")); tick(); noErrors("blizzard")
assert(not plate.kui:IsShown() and border._texture == "Interface\\Tooltips\\Nameplate-Border")
plate:Hide(); plate:Show(); tick(); noErrors("blizzard reshow")
assert(not plate.kui:IsShown())
assert(S:Set("nameplates.mode", "selected")); tick(); noErrors("selected")
assert(plate.kui:IsShown() and border._texture == nil)
assert(S:Set("nameplates.mode", "all")); tick(); noErrors("all")

assert(S:Set("nameplates.height", 40)); noErrors("height")
assert(S:Get("nameplates.height") == 40 and Kui.db.profile.general.offsety == 40)

-- Opening and closing the game's Combat options must not error.
InterfaceOptionsCombatPanel:Hide(); InterfaceOptionsCombatPanel:Show()
InterfaceOptionsFrame:Hide(); noErrors("interface options")

assert(SlashCmdList.KUINAMEPLATES); SlashCmdList.KUINAMEPLATES(); assert(opened == "Nameplates")

-- Every generated row: reads as saved, accepts another value, and returns to its default.
local function same(a, b)
    if type(a) ~= "table" then return type(a) == "number" and math.abs(a - b) < 1e-6 or a == b end
    for i = 1, 3 do if math.abs(a[i] - b[i]) > 1e-6 then return false end end
    return true
end
local count, pages = 0, {}
for _, option in ipairs(S.options) do
    if option.id:match("^nameplates%.kui%.") then
        count = count + 1
        pages[option.page] = (pages[option.page] or 0) + 1
        local before = S:Get(option.id)
        assert(same(before, option.default), option.id .. " does not read its default: " .. tostring(before))
        local other
        if option.type == "toggle" then other = not before
        elseif option.type == "slider" then other = before == option.max and option.min or option.max
        elseif option.type == "colour" then other = { .25, .5, .75 }
        else
            for _, choice in ipairs(option.choices()) do if choice.value ~= before then other = choice.value; break end end
        end
        if other ~= nil then
            assert(S:Set(option.id, other), option.id .. " refused " .. tostring(other))
            tick(); noErrors(option.id)
            assert(same(S:Get(option.id), other), option.id .. " did not keep " .. tostring(other))
            plate:Hide(); plate:Show(); tick(); noErrors(option.id .. " (reshow)")
        end
        option.localSet(nil); tick(); noErrors(option.id .. " (reset)")
        assert(same(S:Get(option.id), option.default), option.id .. " did not reset")
    end
end
assert(count == 93, count .. " Kui settings generated")
-- Text positions apply to where Kui reads them.
for _, key in ipairs({ "nametext.nameoffsety", "leveltext.leveloffsetx", "healthtext.healthoffsety" }) do
    local id, field = "nameplates.kui.text." .. key, key:match("%.(.+)$")
    assert(S:Set(id, 7.5) and Kui.db.profile.text[field] == 7.5, id .. " is not stored where Kui reads it")
    S.byID[id].localSet(nil)
end
assert(Kui.db.profile.hp.reactioncolours.hatedcol[1] == 1)
S:Reset("Nameplates"); tick(); noErrors("category reset")
assert(S:Get("nameplates.height") == 0)
local summary = {}
for page, n in pairs(pages) do summary[#summary + 1] = page .. " " .. n end
table.sort(summary)
-- The real settings window draws every Nameplates page.
local before = #frames
realOpen(S, "Nameplates"); noErrors("settings window")
local function labels()
    local seen = {}
    for _, f in ipairs(frames) do if f._kind == "FontString" and f._text then seen[f._text] = true end end
    return seen
end
assert(#frames > before and labels()["Nameplate style"], "the Style page did not draw")
local search
for i = before + 1, #frames do
    if frames[i]._kind == "EditBox" and frames[i].scripts.OnTextChanged then search = frames[i]; break end
end
assert(search, "no search box")
local drawn = {}
for _, option in ipairs(S.options) do
    if option.id:match("^nameplates%.") then
        search._text = option.id
        run(search, "OnTextChanged"); noErrors("search " .. option.id)
        assert(labels()[option.name], "row not drawn: " .. option.id)
        drawn[#drawn + 1] = option.id
    end
end
assert(#drawn == 95, #drawn .. " rows drawn")

print("Nameplates: load, client plate layout, styles, height, /knp and " .. count .. " Kui settings passed ("
    .. table.concat(summary, ", ") .. ")")
