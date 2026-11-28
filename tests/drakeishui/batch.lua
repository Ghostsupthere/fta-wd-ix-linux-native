local M = ...
local addon = M.addon()
M.load(addon, "settings_profiles.lua")
local S = addon.Settings
local layout = {}
addon.Layout = {
    entries = {}, DB = function() return layout end, RefreshBars = function() end,
    RefreshBags = function() end, SetEditing = function() end, Select = function() end,
}
M.units = { target = "fixture target" }
function UnitHealth() return 8300 end
function UnitHealthMax() return 16600 end
function UnitPower() return 5200 end
function UnitPowerMax() return 5200 end
function UnitIsDeadOrGhost() return false end
function UnitIsConnected() return true end
function GetGameTime() return 17, 42 end
local zone = "Stormwind"
function GetRealZoneText() return zone end
function SetMapToCurrentZone() end
function GetPlayerMapPosition() return .767, .65 end
function GetFramerate() return 588.2 end

TargetFrame = CreateFrame("Frame", "TargetFrame", UIParent)
TargetFrameHealthBar = CreateFrame("StatusBar", "TargetFrameHealthBar", TargetFrame)
TargetFrameManaBar = CreateFrame("StatusBar", "TargetFrameManaBar", TargetFrame)
MinimalChatAnchor = CreateFrame("Frame", "MinimalChatAnchor", UIParent)
MinimalChatAnchor:SetWidth(300)
local metrics = getmetatable(MinimalChatAnchor).__index
function metrics:GetStringWidth() return #self:GetText() * 5 end
UnifiedInfoFrame = CreateFrame("Frame", "UnifiedInfoFrame", MinimalChatAnchor)
local original = UnifiedInfoFrame:CreateFontString()
M.load(addon, "unit_values.lua")
M.load(addon, "bag_quality.lua")
local function chatWindow(name)
    local frame = CreateFrame("Frame", name, UIParent)
    frame.spacing, frame.fading, frame.visibleTime, frame.lines = 3, true, 12, 128
    function frame:GetSpacing() return self.spacing end
    function frame:SetSpacing(value) self.spacing = value end
    function frame:GetFading() return self.fading end
    function frame:SetFading(value) self.fading = value end
    function frame:GetTimeVisible() return self.visibleTime end
    function frame:SetTimeVisible(value) self.visibleTime = value end
    function frame:GetMaxLines() return self.lines end
    function frame:SetMaxLines(value) self.lines = value end
    function frame:AddMessage(message) self.message = message end
    return frame
end
local chat, combat = chatWindow("ChatFrame1"), chatWindow("ChatFrame2")
date = os.date
Minimap = CreateFrame("Frame", "Minimap", UIParent)
Minimap:SetSize(140, 140)
M.load(addon, "chat_options.lua")
M.load(addon, "chat_footer.lua")
M.load(addon, "layout_dimensions.lua")
M.load(addon, "appearance.lua")

M.event("ADDON_LOADED", "DrakeishUI")
M.event("PLAYER_ENTERING_WORLD")
assert(M.find("FontString", "8.3k", true))
assert(M.find("FontString", "5.2k", true))
assert(S:Set("unit.target.format", "percent"))
assert(M.find("FontString", "50%", true))
assert(S:Set("unit.target.power", "never"))
assert(not M.find("FontString", "100%", true))
assert(S:Set("bagQuality.levels", "on"))
assert(addon.BagQuality:LevelContext("bank") and addon.BagQuality:LevelContext("guildbank"))
local bag = CreateFrame("Button", "DrakeishUI_BagsterItem1", UIParent)
local itemLevel = bag:CreateFontString()
assert(S:Set("bagQuality.levelAnchor", "TOPLEFT"))
addon.BagQuality:PaintLevel(bag, itemLevel, bag)
assert(itemLevel.point[1] == "TOPLEFT")
assert(S:Set("bagQuality.levels", "off"))
addon.BagQuality:PaintLevel(bag, itemLevel, bag)
assert(not itemLevel:IsShown())

assert(original:GetAlpha() == 0)
local function slots()
    local result = {}
    for _, frame in ipairs(M.frames) do
        if frame.parent == UnifiedInfoFrame and frame.kind == "Button" and frame:IsVisible() then
            result[#result + 1] = frame
        end
    end
    return result
end
local line = slots()
assert(#line == 3)
for i = 2, #line do
    local previous = line[i - 1]
    assert(previous.point[4] + previous:GetWidth() <= line[i].point[4])
end
zone = string.rep("Long zone name", 20)
M.tick(1)
-- Too wide for the panel: nothing is hidden, labels are dropped and the slots still do not overlap.
local wide = slots()
assert(#wide == 3)
for i = 2, #wide do assert(wide[i - 1].point[4] + wide[i - 1]:GetWidth() <= wide[i].point[4]) end
assert(not M.find("FontString", "Zone:", true))
assert(S:Set("footer.2.entry", "itemLevel"))
assert(M.find("FontString", "iLvl --", true))
assert(S:Set("footer.enabled", false) and original:GetAlpha() == 1 and #slots() == 0)

assert(S:Set("chat.width", 520) and MinimalChatAnchor:GetWidth() == 520)
assert(S:Set("chat.fade", 0) and not chat.fading and not combat.fading)
assert(S:Set("chat.lines", 256) and chat.lines == 256 and combat.lines == 256)
assert(S:Set("chat.border", true) and addon.ChatOptions:GetInset() == 5)
assert(S:Set("chat.time", "short"))
chat:AddMessage("Fixture message")
assert(chat.message:match("^%[%d%d:%d%d%] Fixture message$"))
assert(S:Set("layout.minimap.size", 180) and Minimap:GetWidth() == 180)
assert(S:Set("layout.minimap.size", -1) and Minimap:GetWidth() == 140)
assert(S:Set("appearance.chat.size", 18) and chat.fontSize == 18)
assert(S:Set("chat.enabled", false))
assert(chat.fading and combat.fading and chat.lines == 128 and MinimalChatAnchor:GetWidth() == 300)

local exported = S:SnapshotProfile()
assert(not exported["chat.width"] and not exported["layout.minimap.size"])
assert(exported["unit.target.power"] == "never" and exported["footer.2.entry"] == "itemLevel")
print("Combined options, health and power, bag item levels, footer layout and reset passed")
