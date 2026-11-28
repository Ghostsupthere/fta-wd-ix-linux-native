local M = ...
local addon = M.addon()
local layoutState = { settingsPage = {category = "Action Bars", page = "Cooldowns"} }
addon.Layout = { DB = function() return layoutState end }
M.load(addon, "settings_profiles.lua")
M.load(addon, "settings_commands.lua")
M.load(addon, "profile_exchange.lua")
local S = addon.Settings
local applies = 0
S:Register({id = "sample", name = "Legacy toggle", type = "toggle", category = "Character Panel",
            default = true, legacy = function() return false end})
S:Register({id = "protected", name = "Protected toggle", type = "toggle", category = "Character Panel",
            default = false, protected = true, apply = function() applies = applies + 1 end})
S:Register({id = "cooldown.size", name = "Cooldown font size", type = "slider", category = "Action Bars",
            min = 8, max = 32, default = 14})
S:Register({id = "cooldown.totem.size", name = "Font size", type = "slider", category = "Action Bars",
            keywords = "totem", min = 8, max = 32, default = 14})
S:Register({id = "chat.fade", name = "Fade delay", type = "slider", category = "Chat",
            min = -1, max = 120, default = -1})
M.load(addon, "settings_window.lua")
M.event("ADDON_LOADED", "DrakeishUI")
assert(not DrakeishUISettingsWindow and S:Get("sample") == false)
assert(S:CreateShared("Preset") == nil and S:Active() == "Preset")
assert(S:Set("cooldown.size", 27))
local serialized = S:ExportProfile()
assert(S:ImportProfile("Imported", serialized) == nil)
-- Profile text exported while the old bag windows existed still loads.
assert(S:ImportProfile("Old bags", serialized .. "bags.bagster=b1;bagQuality.quest=b0;appearance.bags.size=n12;") == nil)
assert(S:Get("cooldown.size") == 27 and DrakeishUI_Config.shared["Old bags"]["bags.bagster"] == nil)
assert(S:ImportProfile("Unknown", serialized .. "no.such.option=b1;"))
S:Select("Imported")
assert(S:Active() == "Imported" and S:Get("cooldown.size") == 27)
assert(S:Set("cooldown.size", 22))
S:Select("Preset")
assert(S:Get("cooldown.size") == 27)
S:Select(false)
assert(S:Get("cooldown.size") == 14)
M.combat = true
local count = applies
assert(S:Set("protected", true))
assert(S.pending.protected and S:Get("protected") == false and applies == count)
M.combat = false
M.event("PLAYER_REGEN_ENABLED")
assert(not S.pending.protected and S:Get("protected") == true and applies == count + 1)

S:Open()
assert(DrakeishUISettingsWindow:IsVisible())
local slider = assert(M.find("Slider", nil, true))
assert(slider.value == 14)
slider:SetValue(27)
assert(S:Get("cooldown.size") == 27)
S:Reset("Action Bars")
assert(S:Get("cooldown.size") == 14 and slider.value == 14)
S:Open("Chat")
local reused = assert(M.find("Slider", nil, true))
assert(reused == slider and slider.value == -1)
slider:SetValue(120)
S:Open("Action Bars")
assert(S:Get("cooldown.size") == 14 and slider.value == 14)
local saved = DrakeishUI_Config.characters["Test Realm\031Fixture"].profile
assert(saved["cooldown.size"] == nil and saved["chat.fade"] == 120)

local search
for _, frame in ipairs(M.frames) do
    if frame.kind == "EditBox" and frame.scripts.OnTextChanged and frame.parent == DrakeishUISettingsWindow then
        search = frame
        break
    end
end
assert(search)
search:SetText("totem size")
assert(M.find("Button", "Per-bar Cooldowns", true))
search.scripts.OnEnterPressed(search)
assert(layoutState.settingsPage.tab == "Totem")
search:SetText("no such colour or numbers option")
assert(M.find("FontString", "No matching settings.", true))
-- The Profiles page: an older profile gets a layout from the button, and a switch that moves things offers a reload.
local popups = {}
function StaticPopup_Show(which) popups[#popups + 1] = which end
local reloaded
function ReloadUI() reloaded = true end
search:SetText("")
DrakeishUI_Config.shared.Legacy = {}
assert(S:Select("Legacy") == nil and S:NeedsLayout())
layoutState.columns = 12
S:Open("Profiles")
local save = assert(M.find("Button", "Save layout to profile", true))
assert(save:IsEnabled())
M.click("Save layout to profile")
assert(not S:NeedsLayout() and not save:IsEnabled() and DrakeishUI_Config.setups.Legacy.layout.columns == 12)
assert(DrakeishUI_Config.setups.Legacy.layout.settingsPage == nil)
local active
for _, option in ipairs({ "Preset", false }) do active = option end
S.reloadNeeded = nil
S:Select(active)
layoutState.columns = 20
S.reloadNeeded = nil
local dropdownRow
for _, frame in ipairs(M.frames) do
    if frame.option and frame.option.name == "Active profile" and frame:IsVisible() then dropdownRow = frame end
end
assert(dropdownRow)
dropdownRow.option.set("Legacy")
assert(layoutState.columns == 12 and popups[#popups] == "DRAKEISHUI_SETTINGS_RELOAD" and not S.reloadNeeded)
StaticPopupDialogs.DRAKEISHUI_SETTINGS_RELOAD.OnAccept()
assert(reloaded)
S:Close()
assert(not DrakeishUISettingsWindow:IsVisible())
print("Settings lazy window, profiles, combat queue, pooled sliders, navigation and search passed")
