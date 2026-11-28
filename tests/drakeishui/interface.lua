local M = ...
local addon = M.addon()
local state = {
    combined = false, bagScale = 1, bagSpacing = 14, columns = 20, bagSlotSpacing = 4, actionSpacing = 7,
    sortMode = "Type", bagGlow = false, bagGlowAlpha = .65, bagAutoSort = false, bagGroupSpacing = true,
    bagGlowTypes = { Equipment = true, Junk = true },
    bagGlowColors = { Equipment = { .25, .6, 1 }, Junk = { .55, .55, .55 } },
}
addon.Layout = {
    entries = {}, DB = function() return state end, outlineTypes = { { "Equipment", .25, .6, 1 }, { "Junk", .55, .55, .55 } },
    RefreshBars = function() end, Select = function() end,
}
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

-- The old bag windows' settings are gone; Bagster's are here.
for _, id in ipairs({ "bags.bagster", "layout.combined", "layout.bagScale", "layout.columns", "layout.sortMode",
    "bagGlow.enabled" }) do assert(not S.byID[id], id .. " still exists") end
assert(S:Set("bagster.itemSpacing", 99) and S:Get("bagster.itemSpacing") == 12)
assert(S:Set("bagster.bagBreak", "bag") and S:Get("bagster.bagBreak") == "bag")
S:Set("bagster.bagBreak", "sideways")
assert(S:Get("bagster.bagBreak") == "bag", "an unknown choice was accepted")

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

-- Several bars changed before the server answers (loading a profile does this): the last message keeps them all.
local sent
hooksecurefunc("SetActionBarToggles", function(...) sent = { ... } end)
toggles = {}
S:Get("layout.bottomLeft.shown"); S:Get("layout.bottomRight.shown"); S:Get("layout.right.shown"); S:Get("layout.left.shown")
applyNow = false
assert(S:Set("layout.bottomLeft.shown", true))
assert(S:Set("layout.bottomRight.shown", true))
assert(S:Set("layout.right.shown", true))
assert(sent[1] and sent[2] and sent[3] and not sent[4], "an earlier bar was switched off again")
toggles, applyNow = { 1, 1, 1 }, true

-- The player's own later change, made in the game's Interface options, is what counts.
assert(S:Set("layout.right.shown", false))
applyNow = false
SetActionBarToggles(1, 1, 1, nil, nil)
assert(S:Get("layout.right.shown") == true, "a bar switched on in the game's options still reads as off")
toggles, applyNow = { 1, 1, 1 }, true
assert(S:Get("layout.right.shown") == true)
-- The game sends "0" for a bar that is off.
SetActionBarToggles(1, "0", 1, "0", nil)
toggles = { 1, nil, 1 }
assert(S:Get("layout.bottomRight.shown") == false and S:Get("layout.bottomLeft.shown") == true)

-- The game's own checkboxes follow what DrakeishUI changes.
local box = { value = "1", checked = true, SetChecked = function(self, on) self.checked = not not on end }
InterfaceOptionsActionBarsPanelRight = box
assert(S:Set("layout.right.shown", false) and box.value == "0" and box.checked == false)
assert(S:Set("layout.right.shown", true) and box.value == "1" and box.checked == true)
InterfaceOptionsActionBarsPanelRight = nil

-- Old openers now open the settings window; the Interface page is only a pointer.
addon.Layout:OpenOptions()
assert(DrakeishUISettingsWindow and DrakeishUISettingsWindow:IsShown())
assert(opened and opened.name == "DrakeishUI" and M.find("Button", "Open DrakeishUI Settings"))
print("Interface options moved into settings passed")
