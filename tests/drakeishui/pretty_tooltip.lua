-- The pretty item tooltip against a stand-in for the client's GameTooltip: lines anchored in a chain, a money
-- frame, other addons' rows and the OnTooltipLayout script. Uses its own widget mock.
local M = ...
local root = M.directory
local probe = io.open(root .. "/pretty_tooltip.lua")
if not probe then print("Pretty tooltip skipped (no pretty_tooltip.lua)"); return end
probe:close()

local function plain(text) return ((text or ""):gsub("|c%x%x%x%x%x%x%x%x", ""):gsub("|r", "")) end

local methods = {}
methods.__index = methods
local function region(kind) return setmetatable({ kind = kind, points = {}, order = {}, shown = true, alpha = 1 }, methods) end
function methods:SetPoint(point, ...)
    if not self.points[point] then self.order[#self.order + 1] = point end
    self.points[point] = { point, ... }
end
function methods:ClearAllPoints() self.points, self.order = {}, {} end
function methods:GetPoint(index)
    local p = self.points[self.order[index or 1] or ""]
    if p then return unpack(p, 1, 5) end
end
function methods:Show() self.shown = true end
function methods:Hide() self.shown = false end
function methods:IsShown() return self.shown end
function methods:SetAlpha(a) self.alpha = a end
function methods:SetText(text) self.text = text and tostring(text) end
function methods:GetText() return self.text end
function methods:SetWidth(w) self.width = w end
function methods:SetHeight(h) self.height = h end
function methods:GetStringWidth() return 6 * #plain(self.text) end
function methods:GetWidth() return (self.width and self.width > 0) and self.width or self:GetStringWidth() end
function methods:GetHeight()
    if self.kind ~= "FontString" then return self.height or 0 end
    if self.width and self.width > 0 then return 12 * math.max(1, math.ceil(self:GetStringWidth() / self.width)) end
    return 12
end
function methods:GetFont()
    if self.font then return unpack(self.font) end
    return "font.ttf", 15, ""
end
function methods:SetFont(file, size, flags) self.font = { file, size, flags } end
function methods:SetTextColor(r, g, b) self.color = { r, g, b } end
function methods:GetTextColor() local c = self.color or { 1, 1, 1 }; return c[1], c[2], c[3] end
function methods:SetTexture(t) self.texture = t end
function methods:SetTexCoord(...) self.coords = { ... } end
function methods:SetVertexColor(...) self.vertex = { ... } end
function methods:SetGradientAlpha(...) self.gradient = { ... } end

-- Globals the module reads.
local alt, hookScripts = false, true
function IsAltKeyDown() return alt end
function GetCVar(name) if name == "gxResolution" then return "1024x768" end end
function geterrorhandler() return function(message) error(message, 0) end end
function CreateFrame()
    local frame = { events = {} }
    function frame:RegisterEvent(event) self.events[event] = true end
    function frame:SetScript(_, fn) self.handler = fn end
    _G.modifierFrame = frame
    return frame
end
ITEM_BIND_ON_EQUIP, ITEM_SOULBOUND = "Binds when equipped", "Soulbound"
DURABILITY_TEMPLATE, ITEM_MIN_LEVEL = "Durability %d / %d", "Requires Level %d"
CURRENTLY_EQUIPPED, INVTYPE_HAND = "Currently Equipped", "Hands"
TOOLTIP_DEFAULT_COLOR, TOOLTIP_DEFAULT_BACKGROUND_COLOR = { r = 1, g = 1, b = 1 }, { r = 0, g = 0, b = .1 }
ITEM_QUALITY_COLORS = { [1] = { r = 1, g = 1, b = 1 }, [3] = { r = 0, g = .44, b = .87 } }
local items = {
    ["item:100:0"] = { "Dunestalker Grips", nil, 3, 47, 42, "Armor", "Mail", 1, "INVTYPE_HAND", "icon-grips" },
    ["item:200:0"] = { "Silk Cloth", nil, 1, 30, 0, "Trade Goods", "Cloth", 20, "", "icon-cloth" },
}
function GetItemInfo(link) if items[link] then return unpack(items[link], 1, 10) end end
IdTipItemDisplayIDs = { [100] = 67890 }

local function makeTip(name)
    local tip = region("Tooltip")
    tip.name, tip.lines, tip.hooks, tip.backdrop = name, {}, {}, { edgeFile = "stock" }
    tip.created = {}
    local function strings(index)
        if not tip.lines[index] then
            local left, right = region("FontString"), region("FontString")
            if index == 1 then left:SetPoint("TOPLEFT", tip, "TOPLEFT", 10, -10)
            else left:SetPoint("TOPLEFT", tip.lines[index - 1].left, "BOTTOMLEFT", 0, -2) end
            right:SetPoint("RIGHT", left, "LEFT", 40, 0)
            tip.lines[index] = { left = left, right = right }
            _G[name .. "TextLeft" .. index], _G[name .. "TextRight" .. index] = left, right
        end
        return tip.lines[index]
    end
    for index = 1, 8 do strings(index) end
    tip.count = 0
    function tip:NumLines() return self.count end
    function tip:AddLine(left, right)
        self.count = self.count + 1
        local line = strings(self.count)
        line.left:SetText(left); line.left:Show()
        if right then line.right:SetText(right); line.right:Show() else line.right:SetText(""); line.right:Hide() end
        return line
    end
    function tip:Run(script) for _, fn in ipairs(self.hooks[script] or {}) do fn(self) end end
    function tip:Clear()
        for index = 1, self.count do
            local line = self.lines[index]
            line.left:SetWidth(0); line.left:SetText(""); line.left:Hide(); line.right:SetText(""); line.right:Hide()
        end
        local had = self.count > 0
        self.count, self.item, self.shownMoneyFrames = 0, nil, nil
        if had then self:Run("OnTooltipCleared") end
    end
    function tip:HasScript(script) return hookScripts or script ~= "OnTooltipLayout" end
    function tip:HookScript(script, fn) self.hooks[script] = self.hooks[script] or {}; table.insert(self.hooks[script], fn) end
    function tip:GetItem() if self.item then return "Item", self.item end end
    function tip:GetName() return name end
    function tip:GetEffectiveScale() return 1 end
    function tip:GetBackdrop() return self.backdrop end
    function tip:SetBackdrop(value) self.backdrop = value end
    function tip:SetBackdropColor() end
    function tip:SetBackdropBorderColor() end
    function tip:CreateTexture(_, layer)
        local t = region("Texture"); t.layer = layer; self.created[#self.created + 1] = t; return t
    end
    function tip:CreateFontString() local t = region("FontString"); self.created[#self.created + 1] = t; return t end
    _G[name] = tip
    return tip
end

local options, values = {}, {}
local S = {
    Register = function(_, option) options[option.id] = option; values[option.id] = option.default end,
    Get = function(_, id) return values[id] end,
    IsReady = function() return true end,
}
local function load()
    local addon = { Settings = S }
    assert(loadfile(root .. "/pretty_tooltip.lua"))("DrakeishUI", addon)
    return addon
end

-- A client without OnTooltipLayout keeps the stock tooltips and gets no option.
hookScripts = false
makeTip("GameTooltip")
load()
assert(not options["tooltip.pretty"], "an older client must not offer the option")
hookScripts = true

local tip = makeTip("GameTooltip")
local compare = makeTip("ShoppingTooltip1")
load()
assert(options["tooltip.pretty"] and values["tooltip.pretty"] == true)

local moneyFrame = region("Frame"); moneyFrame:SetWidth(60); moneyFrame:SetHeight(13)
-- The client's order, with the rows the installed addons add.
local function fillGear()
    tip:Clear()
    tip.item = "item:100:0"
    local rows = {}
    local function add(key, left, right) rows[key] = tip:AddLine(left, right) end
    add("name", "Dunestalker Grips")
    add("bind", "Binds when equipped")
    add("slot", "Hands", "Mail")
    add("armor", "212 Armor")
    add("agility", "+11 Agility")
    add("durability", "Durability 50 / 60")
    add("level", "Requires Level 42")
    add("itemlevel", "Item Level 47")
    add("effect", "Equip: " .. string.rep("long effect words ", 8))
    add("money", " ")
    moneyFrame:SetPoint("LEFT", rows.money.left, "LEFT", 4, 0)
    _G.GameTooltipMoneyFrame1, tip.shownMoneyFrames = moneyFrame, 1
    tip:Run("OnTooltipSetItem")
    add("gearscore", "GearScore: 31", "(iLevel: 47)")
    add("yours", "YourScore: 40", "(+9)")
    add("itemid", "ItemID:", "|cffffffff100")
    add("display", "DisplayID:", "|cffffffff67890")
    add("drakeid", "ID: 100")
    add("reagent", "Reagent Bank", "140")
    add("spacer", " ")
    add("bot", "|cff66ccffAshen Bot Upgrade:|r |cff00ff00YES|r")
    local function botRow(key, who, slot)
        add(key, who, slot .. "  |cff00ff00+13 ilvl|r  |cff66ff66+8.4 score|r  |cffd7b46aGS 120|r  |cff55ddffserver checked|r")
    end
    botRow("bot1", "Fenwick Marksman", "HANDS")
    botRow("bot2", "Marrow Enhancement", "HANDS")
    add("bot3", "Sable Beast Mastery", "HANDS  |cffffcc00+4 ilvl, uncertain|r")
    add("late", "A line from an unknown addon")
    return rows
end

local function x(line) local _, _, _, px = line.left:GetPoint(); return px end
local function depth(line) local _, _, _, _, py = line.left:GetPoint(); return -py end
local function visible(line) return line.left.alpha == 1 end
local function ownText(wanted)
    for _, t in ipairs(tip.created) do
        if t.kind == "FontString" and t.shown and plain(t.text) == wanted then return t end
    end
end

local rows = fillGear()
tip:Run("OnTooltipLayout")
assert(tip.backdrop == nil, "the frame replaces the stock backdrop")
assert(x(rows.name) > x(rows.armor), "the name sits beside the icon")
assert(not visible(rows.slot) and ownText("Mail \194\183 Hands"), "slot and type form the subtitle")
assert(depth(rows.name) < depth(rows.bind) and depth(rows.bind) == depth(rows.level), "binding and level share a row")
assert(x(rows.bind) == x(rows.name), "binding sits under the type, in the name's column")
assert(x(rows.level) > x(rows.bind) and depth(rows.level) < depth(rows.armor), "the level moves up into the header")
assert(depth(rows.armor) < depth(rows.agility) and depth(rows.agility) < depth(rows.effect))
assert(rows.effect.left.width and rows.effect.left.width > 0 and rows.effect.left:GetHeight() > 12, "long lines wrap")
for _, key in ipairs({ "gearscore", "yours", "itemid", "display", "drakeid", "spacer", "slot", "itemlevel",
    "durability", "bot2", "bot3" }) do
    assert(not visible(rows[key]), key .. " should be hidden")
    assert(x(rows[key]) < -10000, key .. " must be off the tooltip, not only transparent")
    -- Another addon recolouring a hidden line (the armour type) must not bring it into view.
    rows[key].left.alpha, rows[key].right.alpha = 1, 1
    local _, anchor, _, offset = rows[key].right:GetPoint()
    assert(not rows[key].right.shown or (anchor == rows[key].left and offset == 0), key .. " right column")
    rows[key].left.alpha, rows[key].right.alpha = 0, 0
end
-- A name wider than the usual tooltip widens it instead of running into anything.
assert(visible(rows.late) and depth(rows.late) > depth(rows.effect), "unknown lines are kept")
assert(depth(rows.late) < depth(rows.bot), "bot upgrades form their own section below the item")
assert(plain(rows.bot.left:GetText()) == "Bot upgrade: YES")
assert(visible(rows.bot1) and not visible(rows.bot2) and not visible(rows.bot3) and ownText("2 more"))
assert(plain(rows.bot1.right:GetText()) == "+13 ilvl  +8.4 score", plain(rows.bot1.right:GetText()))
assert(plain(rows.bot3.right:GetText()) == "+4 ilvl, uncertain", plain(rows.bot3.right:GetText()))
assert(depth(rows.reagent) > depth(rows.bot1) and depth(rows.money) > depth(rows.reagent), "footer comes last")
assert(not visible(rows.durability) and ownText("50 / 60"), "durability becomes a bar")
local badge = assert(ownText("iLvl 47"), "item level box")
do
    local _, fill = badge:GetPoint()
    local _, _, _, fillX, fillY = fill:GetPoint()
    assert(fillX > 16 and -fillY > 14 + 44, "the box sits under the icon")
    -- Eight quality-coloured strips frame the icon and the box, all above the header's layer.
    local strips = 0
    for _, t in ipairs(tip.created) do
        if t.kind == "Texture" and t.shown and t.vertex and math.abs(t.vertex[3] - .87) < .001 and t.vertex[4] == 1 then
            assert(t.layer == "ARTWORK", "a border under the header's layer would be hidden")
            strips = strips + 1
        end
        if t.shown and tostring(t.texture):match("frame$") then assert(t.layer == "ARTWORK") end
    end
    assert(strips == 8, "icon and item level borders, found " .. strips)
end
do
    local file, _, flags = rows.name.left:GetFont()
    assert(file:match("expressway%-bold%.ttf$") and flags == "OUTLINE", "bold outlined name")
    assert(rows.bind.left:GetFont() == "font.ttf", "only the name is bold")
end
assert(not visible(rows.itemlevel), "the box replaces the Item Level line")
assert(not ownText("Item ID 100"), "no IDs without Alt")
-- The money frame hangs 4 units right of its line; it must end at the right padding.
local right = x(rows.money) + 4 + moneyFrame:GetWidth()
assert(math.abs(right - (tip.width - 16)) < 1.5, "sell price is right-aligned: " .. right .. " of " .. tip.width)
local _, relative, _, offset = rows.armor.right:GetPoint()
assert(offset == nil or relative, "right column anchors to its own line")
assert(depth(rows.money) < tip.height, "everything fits inside the tooltip")
local collapsed, width = tip.height, tip.width

-- Laying out again changes nothing (the client calls it after every Show).
tip:Run("OnTooltipLayout")
assert(tip.height == collapsed and tip.width == width and visible(rows.bot1) and not visible(rows.bot2))

do
    fillGear()
    rows = tip.lines
    local long = "Stalwart Shoulderpads of the Bear and a Very Much Longer Ending Than Usual"
    local named = fillGear()
    named.name.left:SetText(long)
    tip:Run("OnTooltipLayout")
    assert(x(named.name) + named.name.left:GetStringWidth() <= tip.width - 16, "a long name must fit")
    assert(x(named.slot) < -10000)
    rows = fillGear()
    tip:Run("OnTooltipLayout")
    assert(tip.width == width and tip.height == collapsed)
end

-- Alt shows every bot match and the IDs; letting go collapses again.
alt = true
modifierFrame.handler(modifierFrame, "MODIFIER_STATE_CHANGED", "LALT")
assert(visible(rows.bot2) and visible(rows.bot3) and not ownText("2 more"))
assert(ownText("Item ID 100") and ownText("Display ID 67890") and tip.height > collapsed)
assert(depth(rows.bot1) < depth(rows.bot2) and depth(rows.bot2) < depth(rows.bot3))
alt = false
modifierFrame.handler(modifierFrame, "MODIFIER_STATE_CHANGED", "LALT")
assert(tip.height == collapsed and not visible(rows.bot2) and not ownText("Item ID 100"))

-- Clearing puts every line back on the stock chain for the next tooltip.
local function stock()
    for index = 1, 12 do
        local line = tip.lines[index]
        assert(line.left.alpha == 1 and line.right.alpha == 1, "alpha not restored on line " .. index)
        local point, to, toPoint, px, py = line.left:GetPoint()
        assert(point == "TOPLEFT")
        if index == 1 then assert(to == tip and px == 10 and py == -10)
        else assert(to == tip.lines[index - 1].left and toPoint == "BOTTOMLEFT" and px == 0 and py == -2, "line " .. index) end
        local rpoint, rto, rtoPoint, rx = line.right:GetPoint()
        assert(rpoint == "RIGHT" and rto == line.left and rtoPoint == "LEFT" and rx == 40)
    end
    for _, region in ipairs(tip.created) do assert(not region.shown, "decoration left showing") end
    assert(tip.lines[1].left:GetFont() == "font.ttf", "the first line's font is not restored")
    assert(tip.backdrop and tip.backdrop.edgeFile == "stock", "stock backdrop not restored")
end
tip:Clear()
stock()
-- A spell or unit tooltip is left alone.
tip:AddLine("Fireball"); tip:AddLine("Cast time 3 sec")
tip.width, tip.height = 111, 22
tip:Run("OnTooltipLayout")
assert(tip.width == 111 and tip.height == 22 and tip.backdrop.edgeFile == "stock")

-- A common item without equipment slot: its type is the subtitle, and there is no quality glow.
tip:Clear()
tip.item = "item:200:0"
tip:AddLine("Silk Cloth"); tip:AddLine("Crafting Reagent")
tip:Run("OnTooltipSetItem")
tip:Run("OnTooltipLayout")
assert(ownText("Trade Goods \194\183 Cloth") and not ownText("iLvl 30"))
for _, region in ipairs(tip.created) do
    assert(not (region.shown and tostring(region.texture):match("glow$")), "common items have no glow")
end

-- Switching the option off restores the game's look.
rows = fillGear()
tip:Run("OnTooltipLayout")
assert(tip.backdrop == nil)
values["tooltip.pretty"] = false
options["tooltip.pretty"].apply(false)
assert(not tip.shown, "the open tooltip is closed so the game rebuilds it")
stock()
tip.shown = true
fillGear()
tip:Run("OnTooltipLayout")
assert(tip.backdrop and tip.backdrop.edgeFile == "stock")
values["tooltip.pretty"] = true

-- Comparison tooltips keep their "Currently Equipped" tag above the header.
compare.item = "item:100:0"
local tag = compare:AddLine("Currently Equipped")
local title = compare:AddLine("Dunestalker Grips")
compare:Run("OnTooltipSetItem")
compare:Run("OnTooltipLayout")
assert(depth(tag) < depth(title) and x(title) > x(tag) and compare.backdrop == nil)

print("Pretty item tooltip: header, sections, bot summary, Alt details, footer, restore and older client passed")
