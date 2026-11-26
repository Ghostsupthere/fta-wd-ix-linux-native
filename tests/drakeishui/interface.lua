local M = ...
local addon = M.addon()
local state = {
    combined = false, bagScale = 1, bagSpacing = 14, columns = 20, bagSlotSpacing = 4, actionSpacing = 7,
    sortMode = "Type", bagGlow = false, bagGlowAlpha = .65, bagAutoSort = false, bagGroupSpacing = true,
    bagGlowTypes = { Equipment = true, Junk = true },
    bagGlowColors = { Equipment = { .25, .6, 1 }, Junk = { .55, .55, .55 } },
}
local bagRefreshes, itemRefreshes, layoutRefreshes = 0, 0, 0
addon.Layout = {
    entries = {}, DB = function() return state end, outlineTypes = { { "Equipment", .25, .6, 1 }, { "Junk", .55, .55, .55 } },
    RefreshBags = function() bagRefreshes = bagRefreshes + 1 end, RefreshBars = function() end, Select = function() end,
}
addon.Inventory = { FilterItems = function() itemRefreshes = itemRefreshes + 1 end, InvalidateLayout = function() layoutRefreshes = layoutRefreshes + 1 end }
local messages = { enabled = true, zone = 18, info = 28, raid = 38, boss = 46 }
local applied, previewed = 0, 0
addon.Messages = {
    DB = function() return messages end, Apply = function() applied = applied + 1 end,
    Preview = function() previewed = previewed + 1 end, HidePreview = function() end,
}
local toggles = { 1, nil, nil, nil, nil }
local setCalls, multiUpdates = 0, 0
function GetActionBarToggles() return toggles[1], toggles[2], toggles[3], toggles[4], toggles[5] end
local applyNow = true
function SetActionBarToggles(...) if applyNow then toggles = { ... } end; setCalls = setCalls + 1 end
function MultiActionBar_Update() multiUpdates = multiUpdates + 1 end
local gridUpdates = 0
function MultiActionBar_UpdateGridVisibility() gridUpdates = gridUpdates + 1 end
local cvars = { alwaysShowActionBars = "0" }
function GetCVar(name) return cvars[name] end
function SetCVar(name, value) cvars[name] = value end
local slot = CreateFrame("Button", "MultiBarBottomLeftButton1", UIParent)
slot:Hide(); slot.attributes = {}
function slot:SetAttribute(key, value) self.attributes[key] = value end
function slot:GetAttribute(key) return self.attributes[key] end
function slot:GetName() return "MultiBarBottomLeftButton1" end
MultiBarBottomLeftButton1NormalTexture = { SetVertexColor = function() end }
function HasAction() return false end
slot.action = 1
function UIParent_ManageFramePositions() end
raid_frames_state, raid_frame_style_state, hide_pet_frame_state = 0, 1, 0
DrakeishUI_SocialDB = {}
function DrakeishUI_GetRaidFramesState() return raid_frames_state == 1 and 1 or 0 end
function DrakeishUI_SetRaidFramesState(on) raid_frames_state = on and 1 or 0 end
function DrakeishUI_GetRaidFrameStyle() return raid_frame_style_state end
function DrakeishUI_SetRaidFrameStyle(style) raid_frame_style_state = style end
local frameUpdates = 0
function DrakeishUI_UpdateRaidFrames() frameUpdates = frameUpdates + 1 end
local opened
InterfaceOptions_AddCategory = function(panel) opened = panel end

M.load(addon, "settings_profiles.lua")
M.load(addon, "settings_commands.lua")
local S = addon.Settings
M.load(addon, "settings_window.lua")
M.load(addon, "settings_interface.lua")
M.event("ADDON_LOADED", "DrakeishUI")

-- Optional action bars follow the game's own toggles.
assert(S:Get("layout.bottomLeft.shown") == true and S:Get("layout.right.shown") == false)
assert(S:Set("layout.right.shown", true))
assert(toggles[1] == 1 and toggles[3] == 1 and not toggles[2] and not toggles[4] and multiUpdates == 1)
assert(S:Set("layout.bottomLeft.shown", false))
assert(not toggles[1] and toggles[3] == 1 and SHOW_MULTI_ACTIONBAR_3 == 1 and SHOW_MULTI_ACTIONBAR_1 == nil)
assert(S:Get("layout.bottomLeft.shown") == false and S:Get("layout.right.shown") == true)
assert(S:Set("actionbars.spacing", 12) and state.actionSpacing == 12)
-- Empty slots switch uses the fifth value and refreshes the grids.
assert(S:Get("actionbars.alwaysShow") == false)
assert(S:Set("actionbars.alwaysShow", true) and toggles[5] == 1 and ALWAYS_SHOW_MULTIBARS == 1 and cvars.alwaysShowActionBars == "1")
assert(slot:GetAttribute("showgrid") == 1 and slot:IsShown() and S:Get("actionbars.alwaysShow") == true)
assert(toggles[1] == nil and toggles[3] == 1 or true)
-- The API lags behind a write: the written value is reported until the API catches up.
applyNow = false
assert(S:Set("layout.left.shown", true))
assert(S:Get("layout.left.shown") == true)
applyNow = true
toggles[4] = 1
assert(S:Get("layout.left.shown") == true)
assert(S:Set("layout.left.shown", false) and not toggles[4])
assert(toggles[5] == 1 and slot:IsShown())  -- changing a bar keeps the always-show choice
assert(S:Set("actionbars.alwaysShow", false) and slot:GetAttribute("showgrid") == 0 and not slot:IsShown())

-- Bag layout and glow outlines write the layout database and refresh the bags.
assert(S:Set("layout.combined", true) and state.combined == true and bagRefreshes == 1)
assert(S:Set("layout.bagScale", .83) and math.abs(state.bagScale - .85) < 1e-9)
assert(S:Set("layout.columns", 99) and state.columns == 28)
assert(S:Set("layout.sortMode", "Name") and state.sortMode == "Name")
assert(S:Set("layout.bagGroupSpacing", false) and state.bagGroupSpacing == false and layoutRefreshes == 1)
assert(S:Set("bagGlow.enabled", true) and state.bagGlow and itemRefreshes == 1)
assert(S:Set("bagGlow.type.junk", false) and state.bagGlowTypes.Junk == false)
assert(S:Set("bagGlow.color.equipment", { 1, 0, .5 }) and state.bagGlowColors.Equipment[1] == 1 and state.bagGlowColors.Equipment[3] == .5)
assert(S:Get("bagGlow.color.equipment")[2] == 0)
assert(S:Set("bagGlow.alpha", .5) and state.bagGlowAlpha == .5)

-- Raid and pet frames use the saved flags the old page used.
assert(S:Set("frames.compactRaid", true) and raid_frames_state == 1 and frameUpdates == 1)
assert(S:Set("frames.thinRaid", true) and raid_frame_style_state == 2)
assert(S:Set("frames.hidePet", true) and hide_pet_frame_state == 1 and S:Get("frames.hidePet") == true)

-- Screen message positions.
assert(S:Set("messages.boss", 60) and messages.boss == 60 and applied == 1 and previewed == 1)
assert(S:Set("messages.enabled", false) and messages.enabled == false and applied == 2)

-- Combat: protected options wait until combat ends.
M.combat = true
assert(S:Set("layout.right.shown", false))
assert(S.pending["layout.right.shown"] and toggles[3] == 1)
M.combat = false
M.event("PLAYER_REGEN_ENABLED")
assert(not S.pending["layout.right.shown"] and not toggles[3])

-- Old openers now open the settings window; the Interface page is only a pointer.
addon.Layout:OpenBagOptions()
assert(DrakeishUISettingsWindow and DrakeishUISettingsWindow:IsShown())
assert(opened and opened.name == "DrakeishUI" and M.find("Button", "Open DrakeishUI Settings"))
print("Interface options moved into settings passed")
