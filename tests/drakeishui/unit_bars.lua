local M = ...
local addon = M.addon()
local frame = CreateFrame("Button", "CustomPartyFrame1", UIParent)
frame:SetSize(140, 40)
frame.portrait = frame:CreateTexture()
frame.level = frame:CreateFontString()
frame.overlay, frame.background = frame:CreateTexture(), frame:CreateTexture()
frame.healthBar, frame.powerBar = CreateFrame("StatusBar", nil, frame), CreateFrame("StatusBar", nil, frame)
frame.healthBar:SetSize(85, 11)
frame.powerBar:SetSize(87, 7)
local raid = CreateFrame("Button", "DrakeishUI_RaidUnit1", UIParent)
raid.health = CreateFrame("StatusBar", nil, raid)
raid.bg, raid.border = raid:CreateTexture(), raid:CreateTexture()
raid.powerText = raid:CreateFontString()
M.load(addon, "settings_profiles.lua")
M.load(addon, "unit_bar_sizes.lua")
local S, B = addon.Settings, addon.UnitBarSizes
function DrakeishUI_RefreshPartyFrames() B:ResizeParty(frame) end
function DrakeishUI_UpdateRaidFrames() B:ResizeRaid(raid, B:RaidMetrics(72, 36, false)) end
M.event("ADDON_LOADED", "DrakeishUI")
assert(frame:GetWidth() == 140 and frame.overlay:IsShown() and not raid.drakeishPowerBar)
assert(S:Set("unit.party.powerWidth", 150))
assert(frame:GetWidth() == 203 and frame.powerBar:GetWidth() == 150 and frame.healthBar:GetWidth() == 85)
assert(not frame.overlay:IsShown() and frame.drakeishPowerBacking:IsShown())
M.combat = true
assert(S:Set("unit.party.healthHeight", 30) and S.pending["unit.party.healthHeight"])
assert(frame.healthBar:GetHeight() == 11)
M.combat = false
M.event("PLAYER_REGEN_ENABLED")
assert(frame.healthBar:GetHeight() == 30 and frame:GetHeight() == 65)
assert(S:Set("unit.raid.powerBar", true))
assert(S:Set("unit.raid.powerWidth", 130))
assert(S:Set("unit.raid.healthHeight", 25))
assert(raid:GetWidth() == 130 and raid:GetHeight() == 33)
assert(raid.health:GetHeight() == 25 and raid.powerText.point[2] == raid.drakeishPowerBar)
assert(raid.bg.anchor == raid.health and raid.border.anchor == raid.health)
assert(B:RaidMetrics(72, 18, true) == 72)
function UnitPowerType() return 1, "RAGE" end
function UnitIsDeadOrGhost() return M.dead end
function UnitIsConnected() return true end
PowerBarColor = { RAGE = {r=1,g=0,b=0} }
local power = raid.drakeishPowerBar
function power:SetStatusBarColor(r,g,b) self.rgb = {r,g,b} end
B:UpdateRaidPower(raid, "raid1", 30, 100)
assert(power.value == 30 and power.rgb[1] == 1)
M.dead = true
B:UpdateRaidPower(raid, "raid1", 30, 100)
assert(power.value == 0)
S:Reset("Party and Raid")
assert(frame:GetWidth() == 140 and frame:GetHeight() == 40 and frame.overlay:IsShown())
assert(not frame.drakeishPowerBacking:IsShown() and frame.level.point[1] == "RIGHT")
assert(not power:IsShown() and raid:GetWidth() == 72 and raid:GetHeight() == 36)
assert(raid.health.anchor == raid and raid.powerText.point[2] == raid.health)
print("Party and raid bar sizes, power strip, combat queue, and stock reset passed")
