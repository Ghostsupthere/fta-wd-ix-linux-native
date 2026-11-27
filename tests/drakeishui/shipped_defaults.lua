-- The starting settings in shipped_defaults.lua. It needs the full addon tree, so it is skipped for
-- tools/drakeishui.
local M = ...
local probe = io.open(M.directory .. "/shipped_defaults.lua")
if not probe or not io.open(M.directory .. "/modules/native_layout.lua") then
    print("Shipped defaults skipped (no shipped_defaults.lua or modules/native_layout.lua)"); return
end
probe:close()

local function read(path)
    local file = assert(io.open(M.directory .. "/" .. path, "rb"))
    local text = file:read("*a"); file:close(); return text
end
local function near(a, b, tolerance) return math.abs(a - b) <= (tolerance or 1e-6) end

local addon = M.addon()
M.load(addon, "shipped_defaults.lua")
local D = addon.ShippedDefaults
assert(read("DrakeishUI.toc"):find("shipped_defaults.lua", 1, true) < read("DrakeishUI.toc"):find("DrakeishUI.xml", 1, true),
    "the defaults load before the modules that read them")

-- Frame positions ---------------------------------------------------------------------------------
local function unitFrame(name)
    local f = CreateFrame("Button", name, UIParent)
    f:SetScale(1)
    function f:GetPoint() end
    return f
end
local player = unitFrame("PlayerFrame")
M.load(addon, "modules/native_layout.lua")
local L = addon.Layout

-- The screen these were taken from (3432x1362 at UI scale .83); the fractions are the saved ones.
UIParent:SetSize(2331.59, 925.30)
DrakeishUI_LayoutDB = nil
local db = L:DB()
local expected = {
    PlayerFrame = { .39925, .26376 }, TargetFrame = { .60152, .26376 }, FocusFrame = { .28083, .37184 },
    MultiBarBottomLeft = { .50854, .09824 }, MultiBarRight = { .99035, .45157 }, MultiBarLeft = { .97105, .45157 },
    BuffFrame = { .88894, .96434 }, CustomPartyFrame1 = { .05017, .93049 }, WatchFrame = { .90213, .57412 },
    MinimalChatAnchor = { .07120, .09727 },
}
for name, position in pairs(expected) do
    local saved = assert(db.bars[name], name)
    assert(near(saved.x, position[1], .0006) and near(saved.y, position[2], .0008), name .. " matches the source screen")
end
assert(near(db.bagX, .54916, .0006) and near(db.bagY, .72359, .0008))
assert(db.combined == true and db.columns == 10 and db.actionSpacing == 10 and db.bagSlotSpacing == 1)
assert(db.sizes.main.size == 50 and db.sizes.bottomLeft.size == 35 and db.sizes.minimap.size == 181)
assert(db.chatSize.width == 441 and db.messages.zone == 5 and db.messages.info == 18)
assert(db.bagSpacing == 14 and db.sortMode == "Type", "the module's own defaults still fill the rest")
assert(db.bars.CustomPartyFrame2.scale == 1.6 and db.bars.CustomPartyFrame2.x == nil)
db.sizes.main.size = 20
assert(D.layout.sizes.main.size == 50, "characters get copies")

-- On a 16:9 screen the distances stay the same.
UIParent:SetSize(1365.33, 768)
DrakeishUI_LayoutDB = nil
db = L:DB()
for name, spec in pairs(D.frames) do
    local saved = db.bars[name]
    if spec[1] then
        assert(saved.x > 0 and saved.x < 1 and saved.y > 0 and saved.y < 1, name .. " is on screen")
    end
end
local gap = (db.bars.TargetFrame.x - db.bars.PlayerFrame.x) * 1365.33
assert(near(gap, 472, .01), "player and target keep their spacing")
assert(near((1 - db.bars.MultiBarRight.x) * 1365.33, 23, .01), "the right bar stays by the right edge")

-- A saved layout is left alone.
DrakeishUI_LayoutDB = { columns = 20, bars = { PlayerFrame = { x = .1, y = .2 } } }
db = L:DB()
assert(db.columns == 20 and db.combined == false and db.bars.PlayerFrame.x == .1 and not db.bars.TargetFrame)

-- Reset Positions returns to the shipped positions.
L:RefreshBars()
L:ResetBars()
db = L:DB()
assert(near(db.bars.PlayerFrame.x, (1365.33 / 2 - 235) / 1365.33) and db.bars.PlayerFrame.scale == 1.15)
assert(near(player:GetScale(), 1.15))
assert(near(db.bagY, (768 / 2 + 207) / 768))

-- Nameplates: each name must be one Kui knows, or one of ours -------------------------------------
local core = read("modules/Kui_Nameplates/Core/Core.lua")
assert(core:find("shipped:Merge(defaults.profile, shipped.nameplates)", 1, true))
local block = assert(core:match("\n(local defaults = %b{})"), "Kui defaults table")
local environment = { latin = true, DEFAULT_FONT = "Yanone Kaffeesatz",
    LSM = { GetDefault = function() return "other" end, MediaType = {} } }
local chunk = assert(loadstring(block .. "\nreturn defaults"))
setfenv(chunk, environment)
local kui = chunk().profile
local ours = { ["general.drakeishmarker"] = true, ["general.namebold"] = true }
local function check(shipped, stock, path)
    for key, value in pairs(shipped) do
        local name = path .. key
        if type(value) == "table" then
            assert(type(stock[key]) == "table", name)
            check(value, stock[key], name .. ".")
        else
            assert(ours[name] or type(stock[key]) == type(value), name .. " is a Kui setting")
        end
    end
end
check(D.nameplates, kui, "")
D:Merge(kui, D.nameplates)
assert(kui.general.bartexture == "Drakeish" and kui.general.width == 124 and kui.general.thheight == 7)
assert(kui.fonts.sizes.name == 15 and kui.fonts.sizes.spellname == 11)

-- A new character gets the shipped values, and a value the player chose wins --------------------
local fresh = M.addon()
fresh.ShippedDefaults = D
M.load(fresh, "settings_profiles.lua")
local R = fresh.Settings
R:Register({ id = "chat.size", type = "slider", min = 8, max = 24, default = 12 })
R:Register({ id = "tooltip.y", type = "slider", min = -300, max = 300, default = 0 })
DrakeishUI_Config = nil
M.event("ADDON_LOADED", "DrakeishUI")
assert(R:Desired("chat.size") == 14 and R:Desired("tooltip.y") == -26)
R:Set("chat.size", 18)
assert(R:Desired("chat.size") == 18)
R:Reset()
assert(R:Desired("chat.size") == 14, "Reset returns to the shipped value")

-- Settings window options -------------------------------------------------------------------------
M.enableClip() -- this client can draw round 3D portraits
-- The tooltip stand-ins are the ones tooltip.lua sets up before it loads the addon files.
local tooltipTest = assert(io.open("tests/drakeishui/tooltip.lua")):read("*a")
local stubs = assert(tooltipTest:match('^(.-)\nM%.load%(addon, "settings_profiles%.lua"%)'))
assert(loadstring(stubs))(M)
MinimalChatAnchor = CreateFrame("Frame", "MinimalChatAnchor", UIParent)
UnifiedInfoFrame = CreateFrame("Frame", "UnifiedInfoFrame", MinimalChatAnchor)
M.load(addon, "settings_profiles.lua")
local S = addon.Settings
DrakeishUI_Config = nil
for _, file in ipairs({ "unit_values.lua", "chat_options.lua", "chat_footer.lua", "appearance.lua",
    "action_cooldowns.lua", "tooltip_options.lua", "unit_portraits.lua" }) do
    M.load(addon, file)
end
local count = 0
for id, value in pairs(D.settings) do
    local option = assert(S.byID[id], id .. " is a registered option")
    assert(not option.localGet, id .. " is stored in the profile")
    assert(S:ValidateProfileValue(id, value) == value, id .. " accepts the shipped value")
    assert(option.default == value, id)
    count = count + 1
end
assert(S.byID["cooldown.main.size"] and S.byID["tooltip.spellID"])
-- A client without a choice keeps the built-in default.
D.settings["fixture.unsupported"] = "b"
S:Register({ id = "fixture.unsupported", type = "dropdown", default = "a", validate = function(v) if v == "a" then return v end end })
assert(S.byID["fixture.unsupported"].default == "a")
-- One whose check swaps in a fallback (3D portraits become 2D) uses the fallback.
D.settings["fixture.fallback"] = "3d"
S:Register({ id = "fixture.fallback", type = "dropdown", default = "old", validate = function() return "2d" end })
assert(S.byID["fixture.fallback"].default == "2d")
D.settings["fixture.unsupported"], D.settings["fixture.fallback"] = nil, nil

print("Shipped defaults: " .. count .. " options, nameplate settings, new-character layout, 16:9 spacing and Reset Positions passed")
