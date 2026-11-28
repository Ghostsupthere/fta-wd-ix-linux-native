-- Bagster is the bag system: it takes over from the game's windows, its bag, bank and guild bank windows open
-- and close without errors, and the settings window's choices reach it. Loads the real files with a permissive
-- widget mock. Needs the full addon tree.
local M = ...
local root = M.directory
local probe = io.open(root .. "/modules/bagster/bagster.lua")
if not probe then print("Bagster skipped (no modules/bagster)"); return end
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


function methods:Show()
    if self._shown then return end
    self._shown = true
    run(self, "OnShow")
end
function methods:Hide()
    if not self._shown then return end
    self._shown = false
    run(self, "OnHide")
end
function methods:IsShown() return self._shown end
function methods:IsVisible()
    return self._shown and (not self._parent or not self._parent.IsVisible or self._parent:IsVisible())
end
function methods:GetName() return self._objectName end
function methods:GetParent() return self._parent end
function methods:SetParent(parent) self._parent = parent end
function methods:SetID(id) self._id = id end
function methods:GetID() return self._id or 0 end
function methods:SetWidth(w) self._width = w end
function methods:SetHeight(h) self._height = h end
function methods:SetSize(w, h) self._width, self._height = w, h end
function methods:GetWidth() return self._width or 400 end
function methods:SetChecked(on) self._checked = on end
function methods:GetChecked() return self._checked end
function methods:RegisterEvent(event) self.events[event] = true end
function methods:UnregisterEvent(event) self.events[event] = nil end
function methods:UnregisterAllEvents() self.events = {} end
function methods:IsEventRegistered(event) return self.events[event] end
function methods:NumLines() return 0 end
function methods:IsOwned() return false end
function methods:GetTextWidth() return #self:GetText() * 6 end
function methods:GetCheckedTexture() return self:CreateTexture() end
function methods:GetDisabledTexture() return self:CreateTexture() end
function methods:GetDisabledCheckedTexture() return self:CreateTexture() end
function methods:GetOwner() end
function methods:IsEnabled() return true end
function methods:IsMouseOver() return false end
function methods:GetCursorPosition() return 0 end
function methods:GetVerticalScroll() return 0 end
function methods:GetVerticalScrollRange() return 0 end
function methods:GetItem() end

-- Templates create named child regions; make them on demand for the names the code looks up.
local suffixes = { "IconTexture", "Count", "Stock", "NormalTexture", "Cooldown", "IconQuestTexture", "CloseButton",
    "LeftDisabled", "MiddleDisabled", "RightDisabled", "HighlightTexture", "Text", "Left", "Middle", "Right", "ScrollBar", "ScrollUpButton", "ScrollDownButton", "SearchIcon", "Instructions" }
function CreateFrame(kind, name, parent, template)
    local f = new(kind, name, parent)
    f._shown = true
    f._template = template
    if name and template then
        for _, suffix in ipairs(suffixes) do
            if not _G[name .. suffix] then new("Texture", name .. suffix, f) end
        end
    end
    return f
end
UIParent = new("Frame", "UIParent"); UIParent:SetSize(1365, 768)
WorldFrame = new("Frame", "WorldFrame")
GameTooltip = new("GameTooltip", "GameTooltip")
BankFrame = new("Frame", "BankFrame", UIParent); BankFrame._shown = false
BankFrame:RegisterEvent("BANKFRAME_OPENED"); BankFrame:RegisterEvent("BANKFRAME_CLOSED")
MainMenuBarBackpackButton = new("CheckButton", "MainMenuBarBackpackButton")
local errors = {}
function geterrorhandler() return function(message) errors[#errors + 1] = tostring(message) end end
function hooksecurefunc(object, key, handler)
    if type(object) == "string" then handler, key, object = key, object, _G end
    local original = assert(object[key], tostring(key))
    object[key] = function(...) local r = { original(...) }; handler(...); return unpack(r) end
end
local noopNames = { "PlaySound", "SetItemButtonTexture", "SetItemButtonCount", "SetItemButtonDesaturated",
    "SetItemButtonTextureVertexColor", "SetItemButtonNormalTextureVertexColor", "CooldownFrame_SetTimer",
    "ContainerFrame_UpdateCooldown", "SetPortraitTexture", "SetPortraitToTexture", "SetBagPortraitTexture",
    "ResetCursor", "CursorUpdate", "ClearCursor", "QueryGuildBankTab", "QueryGuildBankLog", "QueryGuildBankText",
    "SetCurrentGuildBankTab", "StaticPopup_Show", "StaticPopup_Hide", "CloseDropDownMenus", "ToggleDropDownMenu",
    "UIDropDownMenu_Initialize", "UIDropDownMenu_AddButton", "UIDropDownMenu_SetWidth", "UIDropDownMenu_SetText",
    "FauxScrollFrame_Update", "FauxScrollFrame_SetOffset", "PanelTemplates_SetTab", "PanelTemplates_SetNumTabs",
    "PanelTemplates_TabResize", "PanelTemplates_UpdateTabs", "PanelTemplates_SelectTab", "PanelTemplates_DeselectTab", "ChatEdit_InsertLink", "PickupContainerItem",
    "SearchBoxTemplate_OnTextChanged", "EditBox_ClearFocus", "EditBox_HighlightText", "MoneyFrame_Update" }
for _, name in ipairs(noopNames) do _G[name] = noop end
function ShowUIPanel(frame) frame:Show() end
function HideUIPanel(frame) frame:Hide() end
local closedBank, closedGuild = 0, 0
function CloseBankFrame() closedBank = closedBank + 1 end
function CloseGuildBankFrame() closedGuild = closedGuild + 1 end
tinsert, tremove = table.insert, table.remove
bit = { band = function(a, b) local result, place = 0, 1; while a > 0 and b > 0 do if a % 2 == 1 and b % 2 == 1 then result = result + place end; a, b, place = math.floor(a / 2), math.floor(b / 2), place * 2 end; return result end }
wipe = function(t) for k in pairs(t) do t[k] = nil end return t end
strmatch, strfind, strsub, strlen, strlower, strupper = string.match, string.find, string.sub, string.len, string.lower, string.upper
format, gsub, floor, ceil, abs, min, max, mod = string.format, string.gsub, math.floor, math.ceil, math.abs, math.min, math.max, math.fmod
function strsplit(sep, s)
    local parts = {}
    for part in (s .. sep):gmatch("(.-)" .. sep:gsub("%p", "%%%0")) do parts[#parts + 1] = part end
    return unpack(parts)
end
function strtrim(s) return (s:gsub("^%s+", ""):gsub("%s+$", "")) end
NUM_BAG_SLOTS, NUM_BANKBAGSLOTS, NUM_BANKGENERIC_SLOTS, NUM_CONTAINER_FRAMES = 4, 7, 28, 13
BACKPACK_CONTAINER, BANK_CONTAINER, KEYRING_CONTAINER, MAX_WATCHED_TOKENS = 0, -1, -2, 3
MAX_GUILDBANK_SLOTS_PER_TAB, MAX_GUILDBANK_TABS = 98, 6
STANDARD_TEXT_FONT, UNKNOWNOBJECT, YES, NO, OKAY, CANCEL, ACCEPT, CLOSE = "test.ttf", "Unknown", "Yes", "No", "Okay", "Cancel", "Accept", "Close"
BACKPACK_TOOLTIP, KEYRING, EQUIPMENT_MANAGER, SEARCH, MONEY, UNKNOWN, NONE = "Backpack", "Keyring", "Equipment", "Search", "Money", "Unknown", "None"
SlashCmdList, StaticPopupDialogs, UISpecialFrames = {}, {}, {}
RAID_CLASS_COLORS = { WARRIOR = { r = .8, g = .6, b = .4 } }
ITEM_QUALITY_COLORS = setmetatable({}, { __index = function() return { r = 1, g = 1, b = 1, hex = "|cffffffff" } end })
function GetItemQualityColor() return 1, 1, 1, "|cffffffff" end
function GetTime() return now end
time, date = os.time, os.date
function GetLocale() return "enUS" end
function GetRealmName() return "Test Realm" end
function UnitName() return "Fixture" end
function UnitClass() return "Warrior", "WARRIOR" end
function UnitLevel() return 80 end
function UnitFactionGroup() return "Alliance", "Alliance" end
function InCombatLockdown() return false end
function IsAddOnLoaded() return false end
function GetAddOnInfo() end
function IsAltKeyDown() return false end
function IsShiftKeyDown() return false end
function IsControlKeyDown() return false end
function IsModifiedClick() return false end
function IsOptionFrameOpen() return false end
function GetCVar() return "0" end
function GetScreenWidth() return 1365 end
function GetScreenHeight() return 768 end
function GetCursorPosition() return 0, 0 end
function GetCursorInfo() end
function CursorHasItem() return false end
function GetCursorMoney() return 0 end
function GetMoney() return 123456 end
function GetAuctionItemClasses()
    return "Weapon", "Armor", "Container", "Consumable", "Glyph", "Trade Goods", "Projectile", "Quiver", "Recipe", "Gem", "Miscellaneous", "Quest"
end
function GetAuctionItemSubClasses() return "a", "b", "c", "d", "e", "f", "g", "h", "i", "j", "k", "l", "m", "n", "o", "p", "q" end
local sizes = { [0] = 16, 18, 18, 0, 0, [-1] = 28, [5] = 20, [-2] = 4 }
function GetContainerNumSlots(bag) return sizes[bag] or 0 end
function GetContainerNumFreeSlots(bag) return sizes[bag] or 0, 0 end
function GetKeyRingSize() return 4 end
local link = "|cffffffff|Hitem:6948:0:0:0|h[Hearthstone]|h|r"
function GetContainerItemInfo(bag, slot) if slot == 1 and GetContainerNumSlots(bag) > 0 then return "Interface\\Icons\\X", 1, false, 1, false, false, link end end
function GetContainerItemLink(bag, slot) if slot == 1 and GetContainerNumSlots(bag) > 0 then return link end end
function GetContainerItemID(bag, slot) if slot == 1 and GetContainerNumSlots(bag) > 0 then return 6948 end end
function GetContainerItemCooldown() return 0, 0, 1 end
function GetContainerItemQuestInfo() return nil end
function GetItemInfo() return "Hearthstone", link, 1, 1, 1, "Miscellaneous", "Junk", 1, "", "Interface\\Icons\\X", 0 end
function GetItemIcon() return "Interface\\Icons\\X" end
function GetItemFamily() return 0 end
function GetItemCount() return 1 end
function GetNumBankSlots() return 2, false end
function GetBankSlotCost() return 10000 end
function GetInventoryItemLink() end
function GetInventoryItemTexture() end
function GetInventoryItemCount() return 0 end
function IsInventoryItemLocked() return false end
function ContainerIDToInventoryID(bag) return 19 + bag end
function BankButtonIDToInvSlotID(id) return 39 + id end
function GetBagName(bag) return "Bag " .. bag end
function GetBackpackCurrencyInfo() end
function GetNumEquipmentSets() return 0 end
function GetEquipmentSetInfo() end
function GetEquipmentSetItemIDs() return {} end
function GetCurrentGuildBankTab() return 1 end
function GetNumGuildBankTabs() return 2 end
function GetGuildBankTabInfo(tab) return "Tab " .. tab, "Interface\\Icons\\X", true, true, -1, -1 end
function GetGuildBankItemInfo(_, slot) if slot == 1 then return "Interface\\Icons\\X", 5, false end end
function GetGuildBankItemLink(_, slot) if slot == 1 then return link end end
function GetGuildBankMoney() return 5000 end
function GetGuildBankWithdrawMoney() return 0 end
function CanWithdrawGuildBankMoney() return false end
function CanGuildBankRepair() return false end
function IsGuildLeader() return false end
function CanEditGuildTabInfo() return false end
function GetGuildBankTabCost() return 100000 end
function GetNumGuildBankTransactions() return 0 end
function GetNumGuildBankMoneyTransactions() return 0 end
function GetGuildBankText() return "" end
function GetGuildInfo() return "Guild" end
function GetNumMacroItemIcons() return 0 end

-- The game's bag functions, only as far as Bagster replacing them matters.
local stockCalls = {}
local function stock(name) local fn = function() stockCalls[name] = (stockCalls[name] or 0) + 1 end; _G[name] = fn; return fn end
local originals = {}
for _, name in ipairs({ "ToggleBag", "ToggleBackpack", "OpenAllBags", "OpenBackpack", "CloseBackpack", "CloseAllBags",
    "ToggleKeyRing", "GuildBankFrame_LoadUI", "updateContainerFrameAnchors", "ContainerFrame_Update", "ContainerFrame_GenerateFrame" }) do
    originals[name] = stock(name)
end
function IsBagOpen() end

local later = {}
local addon = { _dir = "test/", Menu = { Close = noop, Open = noop }, After = function(_, _, fn) later[#later + 1] = fn end }
local function load(path) assert(loadfile(root .. "/" .. path))("DrakeishUI", addon) end
LibStub = nil
load("libs/LibStub/LibStub.lua")
load("libs/LibItemSearch-1.0/LibItemSearch-1.0.lua")
DrakeishUI_BagDB, DrakeishUI_Config = nil, nil
load("modules/native_bridge.lua")
-- The order the client loads them in; the old bag files are gone.
local modules = assert(io.open(root .. "/modules/modules.xml")):read("*a")
local usability, bridge = modules:find('file="bags_usability.lua"', 1, true), modules:find('file="bagster_native.lua"', 1, true)
assert(usability and bridge and usability < bridge, "bagster_native.lua replaces a function of bags_usability.lua")
for _, old in ipairs({ "native_bags", "native_inventory" }) do assert(not modules:find(old, 1, true), old .. " is still loaded") end
load("modules/bags_usability.lua")
load("modules/bagster_native.lua")
for _, file in ipairs({ "bagster", "bagster_data", "bagster_sets", "bagster_classes", "bagster_frame", "bagster_guild", "bagster_system" }) do
    load("modules/bagster/" .. file .. ".lua")
end
load("modules/bagsort.lua")
load("settings_profiles.lua")
local S = addon.Settings
addon.Layout = { entries = {}, DB = function() return { actionSpacing = 7, bars = {} } end, RefreshBars = function() end,
    Select = function() end }
load("settings_interface.lua")
SlashCmdList.REAGENTBANKUI = function() stockCalls.reagents = (stockCalls.reagents or 0) + 1 end
SlashCmdList.GEARBANKCLIENT = function() end

local function event(name, ...)
    local list = {}
    for _, f in ipairs(frames) do if f.events[name] then list[#list + 1] = f end end
    for _, f in ipairs(list) do run(f, "OnEvent", name, ...) end
end
local function settle()
    for _ = 1, 4 do
        local pending = later; later = {}
        for _, fn in ipairs(pending) do fn() end
        for _, f in ipairs(frames) do
            if f.scripts.OnUpdate and f:IsVisible() then run(f, "OnUpdate", .2) end
        end
        now = now + .2
    end
    assert(#errors == 0, tostring(errors[1]))
end
event("ADDON_LOADED", "DrakeishUI"); event("PLAYER_LOGIN"); event("PLAYER_ENTERING_WORLD"); settle()
local module = addon.BagsterModule
local function open(index) return module.frames and module.frames[index] and module.frames[index]:IsShown() or false end

assert(module.BagsterModule.applied, "Bagster did not start")
assert(not BankFrame.events.BANKFRAME_OPENED and ToggleBag ~= originals.ToggleBag)
-- A forced open leaves open bags open; the keys toggle them.
OpenAllBags(true); settle(); assert(open(1))
OpenAllBags(true); settle(); assert(open(1), "a forced open closed the bags")
ToggleBackpack(); settle(); assert(not open(1))
OpenBackpack(); settle(); assert(open(1))
CloseBackpack(); settle(); assert(not open(1), "bags the game opened close again")
ToggleBag(2); settle(); assert(open(1))
ToggleBackpack(); settle()
-- The banker: bank and bags open, with the reagent and gear bank buttons, and close on leaving.
event("BANKFRAME_OPENED"); settle()
assert(open(1) and open(2), "bank windows")
assert(DrakeishUI_BagsterREAGENTBANKUI and DrakeishUI_BagsterGEARBANKCLIENT)
run(DrakeishUI_BagsterREAGENTBANKUI, "OnClick"); assert(stockCalls.reagents == 1)
event("BAG_UPDATE", 0); event("PLAYERBANKSLOTS_CHANGED", 1); event("ITEM_LOCK_CHANGED", 0, 1); event("PLAYER_MONEY"); settle()
event("BANKFRAME_CLOSED"); settle()
assert(not open(1) and not open(2), "left the banker")
event("BANKFRAME_OPENED"); settle(); assert(open(1) and open(2), "second visit")
module.frames[2]:Hide(); settle(); assert(closedBank >= 1, "closing the bank window ends the visit")
event("BANKFRAME_CLOSED"); settle()
-- The guild bank.
event("GUILDBANKFRAME_OPENED"); settle()
assert(module.guildFrame and module.guildFrame:IsShown(), "guild bank window")
event("GUILDBANKBAGSLOTS_CHANGED"); event("GUILDBANK_UPDATE_TABS"); event("GUILDBANK_UPDATE_MONEY"); settle()
event("GUILDBANKFRAME_CLOSED"); settle()
assert(not module.guildFrame:IsShown())
assert(SlashCmdList.DRAGONUI_SORT and SlashCmdList.DRAGONUI_SORTBANK, "sorting is available")
-- The settings window's choices are what Bagster and its sorting read, with the windows open to repaint.
OpenAllBags(true); event("BANKFRAME_OPENED"); settle()
local function config(name) return addon:GetModuleConfig(name or "bagster") end
assert(config().item_spacing == 2 and config().bag_break == 1 and config().money_display == "icons")
assert(config().glow_quality and config().glow_quest and config().show_quality_filter)
assert(config("bagsort").bank_fill_from_bags == true and config("bagsort").reverse_stack == false)
for _, choice in ipairs({ { "itemScale", .8, "bagster", "item_scale" }, { "itemSpacing", 6, "bagster", "item_spacing" },
    { "bagBreak", "bag", "bagster", "bag_break", 2 }, { "bagBreak", "none", "bagster", "bag_break", 0 },
    { "money", "text", "bagster", "money_display" }, { "qualityFilter", false, "bagster", "show_quality_filter" },
    { "glow.quality", false, "bagster", "glow_quality" }, { "glow.quest", false, "bagster", "glow_quest" },
    { "glow.alpha", .5, "bagster", "glow_alpha" }, { "sort.fillBank", false, "bagsort", "bank_fill_from_bags" },
    { "sort.reverse", true, "bagsort", "reverse_stack" } }) do
    local id, value, name, key, expected = unpack(choice)
    assert(S:Set("bagster." .. id, value), id); settle()
    if expected == nil then expected = value end
    assert(config(name)[key] == expected, id .. " did not reach Bagster")
end
assert(not module.frames[1].qualityFilter:IsShown())
assert(not addon:IsUnusableItemTintEnabled())
assert(S:Set("bagster.glow.unusable", true)); settle()
assert(addon:IsUnusableItemTintEnabled())
print("Bagster: takes over the bags, bank and guild bank open and close, and its settings reach it")
