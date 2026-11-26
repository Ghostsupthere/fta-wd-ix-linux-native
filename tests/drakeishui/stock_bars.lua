local M = ...
local addon = M.addon()
local methods = getmetatable(UIParent).__index
function methods:GetNumPoints() return self.point and 1 or 0 end
function methods:GetPoint() return unpack(self.point, 1, 5) end
function methods:ClearAllPoints() self.point = nil end
function methods:GetHitRectInsets() return unpack(self.insets or {0,0,0,0}) end
function methods:SetHitRectInsets(...) self.insets = {...} end
function methods:GetBottom() return self.bottom end
function methods:GetTop() return self.top end
function methods:GetTexCoord() return 0,0,0,1,1,0,1,1 end
function methods:GetDrawLayer() return "OVERLAY" end

local function frame(name, powerAvailable)
    local unit = CreateFrame("Button", name, UIParent)
    unit.healthBar = CreateFrame("StatusBar", name .. "HealthBar", unit)
    unit.healthBar:SetSize(125, 20)
    unit.healthBar:SetPoint("LEFT", unit, "LEFT", 8, 3)
    unit.healthBar.left, unit.healthBar.right, unit.healthBar.bottom = 100, 225, 90
    if powerAvailable then
        unit.powerBar = CreateFrame("StatusBar", name .. "ManaBar", unit)
        unit.powerBar:SetSize(125, 8)
        unit.powerBar:SetPoint("LEFT", unit, "LEFT", 8, -12)
        unit.powerBar.left, unit.powerBar.right, unit.powerBar.top = 102, 227, 88
    end
    return unit
end

local player = frame("PlayerFrame", true)
PlayerPortrait = player:CreateTexture("PlayerPortrait")
PlayerPortrait.left, PlayerPortrait.right = 44, 100
local art = player:CreateTexture("DragonflightUIPlayerFrameBorder")
art:SetTexture("test art")
art:SetSize(210, 66)
art.left, art.right = 40, 250
local target = frame("TargetFrame", true)
local tot = frame("TargetFrameToT", true)
DragonflightUIToTManaBar = CreateFrame("StatusBar", "DragonflightUIToTManaBar", tot)
DragonflightUIToTManaBar:SetSize(120, 9)
DragonflightUIToTManaBar:SetPoint("LEFT", tot, "LEFT", 5, -10)
local focus = frame("FocusFrame", true)
local focusTarget = frame("FocusFrameToT", true)
local pet = frame("PetFrame", true)
local boss = frame("Boss1TargetFrame", false)
M.load(addon, "settings_profiles.lua")
M.load(addon, "unit_bar_sizes.lua")
M.load(addon, "stock_unit_bar_sizes.lua")
M.event("ADDON_LOADED", "DrakeishUI")
local S = addon.Settings
assert(player.healthBar:GetWidth() == 125 and art:GetAlpha() == 1)
assert(S:Set("unit.player.healthWidth", 200) and player.healthBar:GetWidth() == 200)
assert(S:Set("unit.player.healthHeight", 32) and player.healthBar:GetHeight() == 32)
assert(player.powerBar.point[1] == "TOPLEFT" and player.powerBar.point[2] == player.healthBar)
assert(player.powerBar.point[4] == 2 and player.powerBar.point[5] == -2)
assert(player.insets[2] == -75 and art:GetAlpha() == 0)
local slice
for _, item in ipairs(M.frames) do
    if item:GetTexture() == "test art" and item ~= art then slice = item end
end
assert(slice and slice:GetWidth() == 64 and slice.coords[5] == 64/210)
assert(S:Set("unit.target.powerWidth", 170) and target.powerBar:GetWidth() == 170)
assert(target.powerBar.point[1] == "TOPRIGHT" and target.insets[1] == -45)
assert(S:Set("unit.targettarget.powerHeight", 13))
assert(DragonflightUIToTManaBar:GetHeight() == 13 and tot.powerBar:GetHeight() == 8)
assert(S:Set("unit.focus.healthHeight", 23) and focus.healthBar:GetHeight() == 23)
assert(S:Set("unit.focustarget.powerHeight", 17) and focusTarget.powerBar:GetHeight() == 17)
assert(S:Set("unit.pet.healthWidth", 97) and pet.healthBar:GetWidth() == 97)
assert(S:Set("unit.boss.powerWidth", 80) and boss.healthBar:GetWidth() == 125)
assert(S:Set("unit.boss.healthWidth", 155) and boss.healthBar:GetWidth() == 155)
assert(S:Set("unit.arena.healthWidth", 160))
local arena = frame("ArenaEnemyFrame1", true)
M.event("ADDON_LOADED", "Blizzard_ArenaUI")
assert(arena.healthBar:GetWidth() == 160)
M.combat = true
S:Reset("Unit Frames")
assert(player.healthBar:GetWidth() == 200 and S.pending["unit.player.healthWidth"])
M.combat = false
M.event("PLAYER_REGEN_ENABLED")
assert(player.healthBar:GetWidth() == 125 and player.powerBar.point[1] == "LEFT")
assert(player.insets[2] == 0 and art:GetAlpha() == 1 and not slice:IsShown())
assert(DragonflightUIToTManaBar:GetHeight() == 9)
assert(arena.healthBar:GetWidth() == 125)
print("Stock unit bar dimensions, art cropping, late arena frames, optional power and combat reset passed")
