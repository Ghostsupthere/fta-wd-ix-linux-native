local M = ...
local addon = M.addon()
M.load(addon, "settings_profiles.lua")
M.load(addon, "settings_commands.lua")

local native = getmetatable(CreateFrame("Frame")).__index
function native:SetCooldown(start, duration) self.start, self.duration = start, duration end
function native:GetCooldownTimes() return self.start, self.duration end
local function cooldown(button, width)
    local parent = CreateFrame("Button", button, UIParent)
    parent:SetWidth(width)
    return CreateFrame("Cooldown", button .. "Cooldown", parent)
end
local sample = cooldown("ActionButton1", 20)
local unrelated = cooldown("UnrelatedButton", 24)
M.load(addon, "action_cooldowns.lua")
M.event("ADDON_LOADED", "DrakeishUI")
local S = addon.Settings
local stored = DrakeishUI_Config.characters["Test Realm\031Fixture"].profile
local lines = M.capturePrint(function() S:HandleCommand("get cooldown.size") end)
assert(lines[1] == "cooldown.size = 14")
sample:SetCooldown(10, 80)
local text = assert(M.find("FontString", "1:20", true))
assert(text.fontSize == 6 and stored["cooldown.size"] == nil)
assert(S:Set("cooldown.fit", false))
assert(text.fontSize == 14 and S:Get("cooldown.size") == 14)
assert(S:Set("cooldown.main.override", true))
assert(S:Set("cooldown.main.size", 20))
assert(text.fontSize == 20 and S:Get("cooldown.size") == 14)
S:Reset("Action Bars")
assert(text.fontSize == 6 and stored["cooldown.main.size"] == nil)
assert(S:Set("cooldown.minutes", "rounded"))
assert(text:GetText() == "2m")
assert(S:Set("cooldown.sweep", false) and sample:GetAlpha() == 0)
assert(S:Set("cooldown.sweep", true) and sample:GetAlpha() == 1)
sample:Hide()
assert(not text:IsShown())
sample:Show()
sample:SetCooldown(10, 80)
assert(text:IsVisible())
M.tick(80)
assert(not text:IsShown())
M.gcdStart, M.gcdDuration = 90, 1.5
sample:SetCooldown(90, 1.5)
assert(not text:IsShown())
assert(S:Set("cooldown.gcd", true) and text:IsShown())
assert(S:Set("cooldown.tenths", 1))
M.tick(.8)
assert(text:GetText() == "0.7")
M.tick(2)
local dynamic = cooldown("ContainerFrame1Item77", 40)
dynamic:SetCooldown(M.time, 3660)
assert(M.find("FontString", "1h 1m", true))
unrelated:SetCooldown(M.time, 900)
assert(not M.find("FontString", "15m", true))
local timers = {}
for index = 1, 120 do
    local widget = cooldown("ContainerFrame2Item" .. index, 35)
    widget:SetCooldown(M.time, 10)
    for _, frame in ipairs(M.frames) do
        if frame.kind == "FontString" and frame.parent and frame.parent.parent == widget.parent then
            timers[#timers + 1] = frame
        end
    end
end
assert(#timers == 120)
M.tick(1)
for _, timer in ipairs(timers) do assert(timer:IsVisible() and timer:GetText() == "9") end
M.tick(10)
for _, timer in ipairs(timers) do assert(not timer:IsShown()) end
print("Cooldown formatting, fitting, defaults, overrides, sweep, expiry and dynamic widgets passed")
