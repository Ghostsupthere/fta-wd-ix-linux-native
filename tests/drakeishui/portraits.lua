local M = ...
M.enableClip()
local addon = M.addon()
M.units = { player = "A", target = "B", party1 = "C" }
M.classes = { player = "MAGE", party1 = "MAGE" }
CLASS_ICON_TCOORDS = { MAGE = { 0, .25, .25, .5 } }

local player = CreateFrame("Frame", "PlayerFrame", UIParent)
local target = CreateFrame("Frame", "TargetFrame", UIParent)
PlayerPortrait = player:CreateTexture("PlayerPortrait")
TargetFramePortrait = target:CreateTexture("TargetFramePortrait")
M.load(addon, "settings_profiles.lua")
M.load(addon, "unit_portraits.lua")
M.event("ADDON_LOADED", "DrakeishUI")
local S = addon.Settings
assert(S:Get("unit.player.portrait") == "2d" and PlayerPortrait:GetAlpha() == 1)
assert(S:Set("unit.player.portrait", "3d"))
local function modelFor(parent)
    for _, frame in ipairs(M.frames) do
        if frame.kind == "PlayerModel" and frame.parent == parent then return frame end
    end
end
local model = assert(modelFor(player))
assert(model.clip and model.model == "A" and PlayerPortrait:GetAlpha() == 1)
assert(S:Set("unit.target.portrait", "3d"))
local other = assert(modelFor(target))
assert(other.model == "B")
M.loading = true
M.units.target = "B2"
M.event("PLAYER_TARGET_CHANGED")
assert(not other.model and TargetFramePortrait:GetAlpha() == 1)
M.loading = false
M.tick(.25)
assert(other.model == "B2" and not other.scripts.OnUpdate)
local requested = other.requested
M.event("UNIT_MODEL_CHANGED", "target")
assert(other.requested == requested + 1)
target:Hide()
assert(not other:IsShown())
target:Show()
assert(other:IsShown() and other.model == "B2")
assert(S:Set("unit.target.portrait", "class"))
assert(not other:IsVisible() and TargetFramePortrait:GetAlpha() == 1)
assert(S:Set("unit.player.portrait", "class"))
assert(not model:IsVisible() and PlayerPortrait:GetAlpha() == 0)
local party = CreateFrame("Frame", "CustomPartyFrame1", UIParent)
party.portrait = party:CreateTexture()
assert(S:Set("unit.party.portrait", "3d"))
addon.RegisterPartyPortrait("party1", party.portrait)
assert(modelFor(party).model == "C")
S:Reset("Unit Frames")
assert(PlayerPortrait:GetAlpha() == 1 and not modelFor(party):IsVisible())
-- Combat glow is a ring sized from the portrait, not the frame-wide art.
local function ring()
    for _, frame in ipairs(M.frames) do
        if frame.kind == "Texture" and frame:GetTexture() == "Interface\\AddOns\\DrakeishUI\\assets\\PortraitGlow" then return frame end
    end
end
PlayerPortrait:SetSize(40, 40)
M.combat = false
M.tick(.1)
assert(ring() and not ring():IsShown())
M.combat = true
M.tick(.1)
assert(ring():IsShown() and ring():GetWidth() == 60 and ring().point[2] == PlayerPortrait)
assert(S:Set("unit.player.combatGlow", false))
M.tick(.1)
assert(not ring():IsShown())
M.combat = false
print("Portrait choices, reuse, fallback, retarget, hiding and late party attachment passed")
