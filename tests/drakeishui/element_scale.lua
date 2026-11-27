-- Element scaling in the Move UI layout code. modules/native_layout.lua is only
-- present in the full addon tree, so this is skipped for tools/drakeishui.
local M = ...
local probe = io.open(M.directory .. "/modules/native_layout.lua")
if not probe then print("Element scale skipped (no modules/native_layout.lua)"); return end
probe:close()
local addon = M.addon()
local function unitFrame(name, scale)
    local f = CreateFrame("Button", name, UIParent)
    f:SetScale(scale or 1)
    function f:GetPoint() end
    return f
end
for i = 1, 4 do unitFrame("CustomPartyFrame" .. i, .9) end
local player, tot, boss = unitFrame("PlayerFrame"), unitFrame("TargetFrameToT"), unitFrame("Boss1TargetFrame", .75)
local map, ring = unitFrame("Minimap"), unitFrame("DrakeishUI_MinimapBorder")
M.load(addon, "modules/native_layout.lua")
local L = addon.Layout
local function near(a, b) return math.abs(a - b) < 1e-6 end
L:RefreshBars()
assert(L:ElementScale("PlayerFrame") == 1)
L:SetElementScale({"PlayerFrame"}, 1)
assert(not L:DB().bars.PlayerFrame, "normal size on an untouched element saves nothing")
L:SetElementScale({"PlayerFrame"}, 1.4)
assert(near(player:GetScale(), 1.4) and near(L:ElementScale("PlayerFrame"), 1.4))
local party = {"CustomPartyFrame1", "CustomPartyFrame2", "CustomPartyFrame3", "CustomPartyFrame4"}
L:SetElementScale(party, 1.2)
for i = 1, 4 do assert(near(_G[party[i]]:GetScale(), 1.08), "factor applies on top of the base scale") end
CustomPartyFrame3:SetScale(.9)
assert(near(CustomPartyFrame3:GetScale(), 1.08), "survives the addon rescaling the frame")
L:SetElementScale({"TargetFrameToT", "Boss1TargetFrame", "ArenaEnemyFrame1"}, 2)
assert(near(tot:GetScale(), 2) and near(boss:GetScale(), 1.5))
boss:SetScale(.75)
assert(near(boss:GetScale(), 1.5))
unitFrame("ArenaEnemyFrame1")
L:RefreshBars()
assert(near(ArenaEnemyFrame1:GetScale(), 2), "late arena frames pick up the saved scale")
L:SetElementScale({"Minimap"}, .8)
assert(near(map:GetScale(), .8) and near(ring:GetScale(), .8))
L:SetElementScale({"PlayerFrame"}, 5)
assert(near(player:GetScale(), 2))
M.combat = true
L:SetElementScale({"PlayerFrame"}, 1)
assert(near(player:GetScale(), 2))
M.combat = false
L:ResetBars()
assert(near(player:GetScale(), 1) and near(CustomPartyFrame1:GetScale(), .9))
assert(near(tot:GetScale(), 1) and near(boss:GetScale(), .75) and near(ring:GetScale(), 1))
print("Element scale, base scale, rescale hooks, late frames, minimap ring, combat and reset passed")
