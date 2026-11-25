local M = ...
local addon = M.addon()
DrakeishUI_Config = {version = 1, characters = {
    ["Test Realm\031Fixture"] = {profile = {["unit.player.portrait"] = "3d"}},
}}
M.load(addon, "settings_profiles.lua")
M.load(addon, "unit_portraits.lua")
M.event("ADDON_LOADED", "DrakeishUI")
local S = addon.Settings
assert(S:Get("unit.player.portrait") == "2d")
for _, choice in ipairs(S.byID["unit.player.portrait"].choices) do assert(choice.value ~= "3d") end
assert(DrakeishUI_Config.characters["Test Realm\031Fixture"].profile["unit.player.portrait"] == "3d")
print("Older-client portrait fallback and saved-choice preservation passed")
