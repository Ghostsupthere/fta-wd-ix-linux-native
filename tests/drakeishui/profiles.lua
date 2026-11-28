-- Shared profiles carried between characters, through real logouts and logins.
local M = ...

-- What the game saves: the account's file, and one layout and one nameplate profile per character.
local disk = { layouts = {}, plates = {} }
local function deep(value)
    if type(value) ~= "table" then return value end
    local result = {}
    for key, entry in pairs(value) do result[key] = deep(entry) end
    return result
end

local S, plates, current
local reloads = 0
local function login(name)
    for _, frame in ipairs(M.frames) do frame.events = {} end
    current, M.character, M.combat = name, name, false
    DrakeishUI_Config = deep(disk.config)
    DrakeishUI_LayoutDB = deep(disk.layouts[name])
    plates = deep(disk.plates[name]) or { width = 100 }
    local addon = M.addon()
    addon.Layout = {
        DB = function()
            DrakeishUI_LayoutDB = DrakeishUI_LayoutDB or {}
            DrakeishUI_LayoutDB.bars = DrakeishUI_LayoutDB.bars or {}
            DrakeishUI_LayoutDB.excludeSort = DrakeishUI_LayoutDB.excludeSort or {}
            return DrakeishUI_LayoutDB
        end,
    }
    M.load(addon, "settings_profiles.lua")
    S = addon.Settings
    S:Register({ id = "tooltip.size", name = "Size", type = "slider", min = 8, max = 32, default = 12 })
    S:Register({ id = "nameplates.width", name = "Width", type = "slider", category = "Nameplates", min = 50, max = 300,
        default = 100, localGet = function() return plates and plates.width end,
        localSet = function(value) plates.width = value end })
    -- A local option that profiles leave alone.
    S:Register({ id = "scale.player", name = "Scale", type = "slider", category = "Unit Frames", min = 1, max = 2,
        default = 1, localGet = function() return 1 end, localSet = function() error("not carried") end })
    M.event("ADDON_LOADED", "DrakeishUI")
    M.event("PLAYER_LOGIN")
    M.event("PLAYER_ENTERING_WORLD")
end

local function logout()
    M.event("PLAYER_LEAVING_WORLD")
    -- The nameplate library clears its saved values first; reading them now would give nothing useful.
    local saved = deep(plates)
    plates = {}
    M.event("PLAYER_LOGOUT")
    disk.config = deep(DrakeishUI_Config)
    disk.layouts[current] = deep(DrakeishUI_LayoutDB)
    disk.plates[current] = saved
end
local function reload() reloads = reloads + 1; local name = current; logout(); login(name) end

local function player() return DrakeishUI_LayoutDB.bars.PlayerFrame.x end
disk.layouts.Alice = { bars = { PlayerFrame = { x = .1 } }, columns = 10, excludeSort = { [1] = true } }
disk.layouts.Bob = { bars = { PlayerFrame = { x = .9 }, TargetFrame = { x = .8 } }, columns = 20, excludeSort = { [2] = true } }
disk.plates.Alice = { width = 150 }
disk.plates.Bob = { width = 80 }

-- Alice makes a profile from her current setup. Nothing of hers changes.
login("Alice")
S:Set("tooltip.size", 20)
assert(S:CreateShared("Mine") == nil and S:Active() == "Mine" and not S.reloadNeeded)
assert(not S:NeedsLayout() and player() == .1 and plates.width == 150)
logout()

-- Bob picks it: the report. Layout, nameplates and ordinary settings all arrive.
login("Bob")
assert(player() == .9 and plates.width == 80)
assert(S:Select("Mine") == nil and S.reloadNeeded)
assert(player() == .1 and DrakeishUI_LayoutDB.bars.TargetFrame == nil and DrakeishUI_LayoutDB.columns == 10)
assert(plates.width == 150 and S:Get("tooltip.size") == 20)
assert(DrakeishUI_LayoutDB.excludeSort[2] and not DrakeishUI_LayoutDB.excludeSort[1], "bag exclusions stay per character")
reload()
assert(S:Active() == "Mine" and player() == .1 and plates.width == 150)

-- Changes made on the shared profile reach the other character.
DrakeishUI_LayoutDB.bars.PlayerFrame.x = .5
S:Set("nameplates.width", 200)
logout()
login("Alice")
assert(S:Active() == "Mine" and player() == .5 and plates.width == 200)
logout()

-- Bob goes back to This Character and gets his own setup again, without touching the shared one.
login("Bob")
S.reloadNeeded = nil
assert(S:Select(false) == nil and S.reloadNeeded)
assert(player() == .9 and DrakeishUI_LayoutDB.bars.TargetFrame.x == .8 and plates.width == 80)
reload()
assert(S:Active() == false and player() == .9 and plates.width == 80)
DrakeishUI_LayoutDB.bars.PlayerFrame.x = .95
logout()
login("Alice")
assert(player() == .5 and plates.width == 200)

-- Switching between two shared profiles keeps both, and the character's own.
assert(S:CreateShared("Second") == nil)
DrakeishUI_LayoutDB.bars.PlayerFrame.x = .3
assert(S:Select("Second") == nil and player() == .3, "picking the profile already in use must not undo changes")
assert(S:Select("Mine") == nil and player() == .5)
assert(S:Select("Second") == nil and player() == .3)
assert(S:Select(false) == nil and player() == .1 and plates.width == 150)

-- Copying a profile brings its layout and nameplates.
assert(S:CopyFrom("Second") == nil and player() == .3 and plates.width == 200)
logout()
login("Bob")
assert(player() == .95, "another character is unaffected")

-- Combat blocks a switch that would move frames.
M.combat = true
assert(S:Select("Mine") and S:Active() == false and player() == .95)
assert(S:CreateShared("Third") and S:Active() == false)
M.combat = false

-- A profile saved before this change has no layout: picking it moves nothing until someone saves one into it.
DrakeishUI_Config.shared.Old = { ["tooltip.size"] = 15 }
S.reloadNeeded = nil
assert(S:Select("Old") == nil and S:NeedsLayout() and not S.reloadNeeded)
assert(player() == .95 and S:Get("tooltip.size") == 15)
logout()
assert(disk.config.setups.Old == nil, "logging out must not adopt a layout by itself")
login("Alice")
assert(S:Select("Old") == nil and S:NeedsLayout())
S:SaveLayout()
assert(not S:NeedsLayout())
local saved = player()
logout()
login("Bob")
assert(S:Active() == "Old" and player() == saved)
assert(S:Select(false) == nil and player() == .95 and plates.width == 80, "own setup survives a layout saved elsewhere")

-- Reloading while staying on This Character changes nothing.
reload()
assert(player() == .95 and plates.width == 80 and reloads == 3)
print("Shared profile layouts, nameplates, round trips and older profiles passed")
