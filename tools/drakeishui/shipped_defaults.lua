-- FTA addition: the starting look of DrakeishUI. To change what a new player gets, edit this file.
--
--  * settings   replace the built-in default of a settings window option. They apply to every character that
--               has not changed that option, and "Reset" returns to them.
--  * nameplates replace Kui Nameplates' built-in defaults in the same way.
--  * layout     is copied into a character that has no saved layout yet.
--  * frames     are the starting frame positions and scales, also used by "Reset Positions".
--
-- This file loads before everything that reads it.
local _, addon = ...
local D = {}
addon.ShippedDefaults = D

D.settings = {
    ["appearance.font"] = "expressway",
    ["appearance.tooltips.size"] = 8,
    ["chat.fade"] = 0,
    ["chat.size"] = 14,
    ["cooldown.size"] = 25,
    ["cooldown.main.size"] = 17,
    ["footer.time"] = "local12",
    ["footer.3.entry"] = "coordsFPS",
    ["footer.3.size"] = 11,
    ["tooltip.anchor"] = "right",
    ["tooltip.x"] = 31,
    ["tooltip.y"] = -26,
    ["tooltip.header"] = 12,
    ["tooltip.body"] = 11,
    ["tooltip.comparison"] = 9,
    ["tooltip.combat"] = false,
    ["tooltip.instant"] = true,
    ["unit.party.portrait"] = "3d",
    ["unit.target.hideZero"] = true,
}

D.nameplates = {
    general = {
        bartexture = "Drakeish",
        drakeishmarker = false,
        width = 124,
        hheight = 12,
        offsety = 25,
        namebold = true,
    },
    fonts = {
        options = { font = "Expressway", fontscale = 1.16, outline = true },
        sizes = { name = 15, health = 9 },
    },
    text = { healthanchorpoint = "BOTTOM", healthoffsety = 10, leveloffsety = 0 },
    hp = { text = { hp_hostile_max = 1, hp_hostile_low = 1 } },
}

D.layout = {
    combined = true,
    columns = 10,
    actionSpacing = 10,
    bagSlotSpacing = 1,
    chatSize = { width = 441, height = 162 },
    messages = { enabled = true, zone = 5, info = 18, raid = 38, boss = 46 },
    sizes = {
        main = { size = 50 },
        bottomLeft = { size = 35 },
        bottomRight = { size = 35 },
        right = { size = 50 },
        minimap = { size = 181 },
    },
}

-- { screen point, x, y, scale }: the frame's centre, as a distance in interface units from that point of the
-- screen. Distances (not fractions of the screen) keep the spacing the same on narrower and wider screens.
-- Entries without a screen point only set a scale.
D.frames = {
    PlayerFrame = { "BOTTOM", -235, 244, scale = 1.15 },
    TargetFrame = { "BOTTOM", 237, 244, scale = 1.15 },
    FocusFrame = { "BOTTOM", -511, 344 },
    PetFrame = { "BOTTOM", -395, 155 },
    CastingBarFrame = { "BOTTOM", 0, 270, scale = 1.05 },
    MultiBarBottomLeft = { "BOTTOM", 20, 91 },
    MultiBarBottomRight = { "BOTTOM", 20, 129 },
    pUiPetBarHolder = { "BOTTOM", -91, 213 },
    pUiStanceHolder = { "BOTTOM", -348, 107 },
    MultiBarLeft = { "RIGHT", -68, -45 },
    MultiBarRight = { "RIGHT", -23, -45 },
    WatchFrame = { "RIGHT", -228, 69 },
    BuffFrame = { "TOPRIGHT", -259, -33, scale = .95 },
    CustomPartyFrame1 = { "TOPLEFT", 117, -64, scale = 1.6 },
    CustomPartyFrame2 = { scale = 1.6 },
    CustomPartyFrame3 = { scale = 1.6 },
    CustomPartyFrame4 = { scale = 1.6 },
    DrakeishUI_RaidFrames = { "TOPLEFT", 160, -244 },
    MinimalChatAnchor = { "BOTTOMLEFT", 166, 90, scale = 1 },
    Minimap = { scale = 1 },
}
-- The combined bag window.
D.bags = { "CENTER", 115, 207 }

local points = {
    TOPLEFT = { 0, 1 }, TOP = { .5, 1 }, TOPRIGHT = { 1, 1 },
    LEFT = { 0, .5 }, CENTER = { .5, .5 }, RIGHT = { 1, .5 },
    BOTTOMLEFT = { 0, 0 }, BOTTOM = { .5, 0 }, BOTTOMRIGHT = { 1, 0 },
}

local function copy(value)
    if type(value) ~= "table" then return value end
    local result = {}
    for key, entry in pairs(value) do result[key] = copy(entry) end
    return result
end

-- Writes every value in source over target, keeping whatever else target holds.
function D:Merge(target, source)
    for key, value in pairs(source) do
        if type(value) == "table" and type(target[key]) == "table" then
            self:Merge(target[key], value)
        else
            target[key] = copy(value)
        end
    end
end

-- The layout stores a position as a fraction of the screen.
local function fraction(spec)
    local width, height = UIParent:GetWidth(), UIParent:GetHeight()
    if not spec[1] or not width or not height or width <= 0 or height <= 0 then return end
    local point = points[spec[1]]
    return (point[1] * width + spec[2]) / width, (point[2] * height + spec[3]) / height
end

function D:SeedPositions(layout)
    layout.bars = {}
    for name, spec in pairs(self.frames) do
        local x, y = fraction(spec)
        layout.bars[name] = { x = x, y = y, scale = spec.scale }
    end
    layout.bagX, layout.bagY = fraction(self.bags)
end

function D:SeedLayout(layout)
    self:Merge(layout, self.layout)
    self:SeedPositions(layout)
end
