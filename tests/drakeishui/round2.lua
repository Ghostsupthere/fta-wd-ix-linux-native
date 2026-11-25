local M = ...
local addon = M.addon()
local Layout = { entries = {} }
addon.Layout = Layout
function Layout:RefreshBars() end
function Layout:Select() end
function Layout:SetEditing(value)
    if not value or not InCombatLockdown() then self.editing = value end
end

local called = 0
function addon.RefreshEndCapPositions() called = called + 1 end
local function bounds(name, left, right)
    local frame = CreateFrame("Frame", name, UIParent)
    frame.left, frame.right = left, right
    return frame
end
pUiMainBarArt = bounds("pUiMainBarArt", 100, 500)
MainMenuBarLeftEndCap = bounds("MainMenuBarLeftEndCap")
MainMenuBarRightEndCap = bounds("MainMenuBarRightEndCap")
MainMenuBarLeftEndCap:SetWidth(105)
MainMenuBarRightEndCap:SetWidth(110)
local first = bounds("ActionButton1", 97, 141)
local extra = bounds("ActionButton14", 506, 550)
M.load(addon, "action_endcaps.lua")
M.tick(.05)
local left, right = addon.GetEndCapOffsets(-95, 95)
assert(left == -108 and right == 160 and called == 1)
extra:Hide()
M.tick(.05)
left, right = addon.GetEndCapOffsets(-130, 140)
assert(left == -133 and right == -219 and called == 2)
first:SetScale(2)
assert(addon.GetEndCapOffsets(-95, 95) == -11)
first:Hide()
left, right = addon.GetEndCapOffsets(-95, 95)
assert(left == -95 and right == 95)

M.load(addon, "settings_profiles.lua")
M.load(addon, "settings_commands.lua")
M.load(addon, "settings_window.lua")
M.load(addon, "keybind_mode.lua")
M.load(addon, "fta_ui_tweaks.lua")
M.event("ADDON_LOADED", "DrakeishUI")
local bindings = { ["SHIFT-X"] = "JUMP" }
local saved
function GetCurrentBindingSet() return 1 end
function GetBindingAction(key) return bindings[key] end
function GetBindingKey(command)
    local keys = {}
    for key, action in pairs(bindings) do if action == command then keys[#keys + 1] = key end end
    return unpack(keys)
end
function SetBinding(key, command) bindings[key] = command; return true end
function SetBindingClick(key, button, click) return SetBinding(key, "CLICK " .. button .. ":" .. click) end
function SaveBindings(set) saved = set end
function StaticPopup_Show(dialog, key, _, data) M.popup = {dialog = dialog, key = key, data = data}; return true end
function StaticPopup_Hide() M.popup = nil end
local function hover(frame)
    frame:Show()
    M.focus = frame
    M.tick(.01)
    for _, candidate in ipairs(M.frames) do
        if candidate.anchor == frame and candidate.scripts.OnKeyDown and candidate:IsVisible() then return candidate end
    end
    error("No binding overlay for " .. frame:GetName())
end
local function key(overlay, name)
    overlay.scripts.OnKeyDown(overlay, name)
end
local B = addon.KeybindMode
addon.Settings:Open()
M.click("Move UI")
assert(Layout.editing and not B.active)
M.click("Keybind Mode")
assert(B.active and not Layout.editing)
local overlay = hover(first)
key(overlay, "Z")
assert(not bindings.Z)
Layout:SetEditing(true)
assert(Layout.editing and not B.active and not bindings.Z)
assert(addon.Settings:HandleCommand("bind"))
assert(B.active and not Layout.editing)
overlay = hover(first)
M.shift = true
key(overlay, "X")
assert(M.popup.key == "SHIFT-X" and bindings["SHIFT-X"] == "JUMP")
local popup = StaticPopupDialogs[M.popup.dialog]
popup.OnAccept(nil, M.popup.data)
popup.OnHide()
M.shift = false
overlay = hover(extra)
overlay.scripts.OnMouseUp(overlay, "Button4")
B:Save()
assert(saved == 1 and bindings["SHIFT-X"] == "ACTIONBUTTON1" and bindings.BUTTON4 == "CLICK ActionButton14:LeftButton")
B:Start()
key(hover(first), "ESCAPE")
B:Save()
assert(not bindings["SHIFT-X"])
B:Start()
M.combat = true
M.event("PLAYER_REGEN_DISABLED")
assert(not B.active)
B:Start()
assert(not B.active and not Layout.editing)
print("End cap geometry, banner modes, draft bindings, conflicts, mouse and combat passed")
