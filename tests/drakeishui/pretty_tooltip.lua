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
function methods:GetNumPoints() return #self.order end
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
local alt, hookScripts, objectScript = false, true, true
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
    function tip:HasScript(script)
        if script == "OnTooltipSetObject" then return objectScript end
        return hookScripts or script ~= "OnTooltipLayout"
    end
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
    assert(loadfile(root .. "/pretty_unit_tooltip.lua"))("DrakeishUI", addon)
    assert(loadfile(root .. "/pretty_object_tooltip.lua"))("DrakeishUI", addon)
    return addon
end

-- A client without OnTooltipLayout keeps the stock tooltips and gets no option.
hookScripts, objectScript = false, false
makeTip("GameTooltip")
load()
assert(not options["tooltip.pretty"], "an older client must not offer the option")
assert(not options["tooltip.prettyUnits"], "an older client must not offer the unit option")
hookScripts = true
-- A client with the layout script but not the object script styles everything except world objects.
makeTip("GameTooltip")
load()
assert(options["tooltip.prettyUnits"] and not options["tooltip.prettyObjects"], "no object option without its script")
objectScript = true

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
    local found
    for _, t in ipairs(tip.created) do
        if t.kind == "FontString" and t.shown then
            -- The test's own names are ASCII, so anything else is ours, and not every game font can draw it.
            assert(not (t.text or ""):find("[\128-\255]"), "non-ASCII text: " .. tostring(t.text))
            if plain(t.text) == wanted then found = found or t end
        end
    end
    return found
end

local rows = fillGear()
tip:Run("OnTooltipLayout")
assert(tip.backdrop == nil, "the frame replaces the stock backdrop")
assert(x(rows.name) > x(rows.armor), "the name sits beside the icon")
assert(not visible(rows.slot) and ownText("Mail - Hands"), "slot and type form the subtitle")
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

-- A socket icon starts at the same margin as the stats, with its text just right of it. The client hangs
-- the icon further left of its line than our margin.
do
    local socket = region("Texture")
    GameTooltipTexture1 = socket
    local gear = fillGear()
    socket:SetWidth(14)
    socket:SetPoint("TOPRIGHT", gear.agility.left, "TOPLEFT", -12, 0)
    for _ = 1, 2 do
        tip:Run("OnTooltipLayout")
        local point, anchor, relativePoint, offset = socket:GetPoint(1)
        assert(socket:GetNumPoints() == 1 and point == "TOPRIGHT" and anchor == gear.agility.left
            and relativePoint == "TOPLEFT", "the icon stays tied to its line")
        assert(x(gear.agility) + offset - 14 == x(gear.armor), "icon not in line with the stats")
        assert(offset < 0 and offset >= -8, "icon too far from its text")
    end
    GameTooltipTexture1 = nil
    rows = fillGear()
    tip:Run("OnTooltipLayout")
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
assert(ownText("Trade Goods - Cloth") and not ownText("iLvl 30"))
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

---------------------------------------------------------------------------------------------- NPCs and players --
values["tooltip.pretty"] = true
local units, portraits = {}, 0
local function unitInfo(token) return units[token] end
function UnitExists(token) return units[token] ~= nil end
function UnitGUID(token) return unitInfo(token) and unitInfo(token).guid end
function UnitIsPlayer(token) return unitInfo(token) and unitInfo(token).player end
function UnitIsUnit(a, b) local one, two = unitInfo(a), unitInfo(b); return one and two and one.guid == two.guid end
function UnitName(token) return unitInfo(token) and unitInfo(token).name end
function UnitLevel(token) return unitInfo(token).level end
function UnitIsDead(token) return unitInfo(token).dead end
function UnitClass(token) local u = unitInfo(token); return u.className, u.classFile end
function UnitRace(token) local u = unitInfo(token); return u.race, u.race end
function GetGuildInfo(token) return unitInfo(token).guild end
function UnitReaction(a, b) local u = unitInfo(a == "player" and b or a); return u and u.reaction or 4 end
function UnitCanAttack(_, token) return unitInfo(token).reaction < 5 end
function UnitClassification(token) return unitInfo(token).classification or "normal" end
function UnitCreatureType(token) return unitInfo(token).creatureType end
function UnitIsTapped(token) return unitInfo(token).tapped end
function UnitIsTappedByPlayer() return false end
function SetPortraitTexture(texture, token) portraits = portraits + 1; texture.portrait = token end
function GetQuestDifficultyColor() return { r = 1, g = .5, b = .25 } end
RAID_CLASS_COLORS = { HUNTER = { r = .67, g = .83, b = .45 } }
TOOLTIP_UNIT_LEVEL, PVP_ENABLED, CORPSE, YOU = "Level %s", "PvP", "Corpse", "You"
UNIT_SKINNABLE_LEATHER = "Skinnable"
IdTipCreatureDisplayIDs = { [2612] = "11563" }

local bar = region("StatusBar")
bar:SetPoint("TOPLEFT", tip, "BOTTOMLEFT", 2, -1)
bar:SetPoint("TOPRIGHT", tip, "BOTTOMRIGHT", -2, -1)
bar:SetHeight(8)
GameTooltipStatusBar = bar
local unitToken
function tip:GetUnit() if unitToken then return "Unit", unitToken end end

local function line(rows, key, left, right, color)
    rows[key] = tip:AddLine(left, right)
    rows[key].left:SetTextColor(unpack(color or { 1, 1, 1 }))
end
local function show(fill)
    tip:Clear()
    unitToken = "mouseover"
    bar:Show()
    local rows = {}
    fill(rows)
    tip:Run("OnTooltipSetUnit")
    tip:Run("OnTooltipLayout")
    return rows
end
local function update(elapsed) for _, fn in ipairs(tip.hooks.OnUpdate or {}) do fn(tip, elapsed) end end
local function glow()
    for _, t in ipairs(tip.created) do
        if t.shown and tostring(t.texture):match("glow$") then return t.vertex end
    end
end
local function colored(fontString, r, g, b)
    local cr, cg, cb = fontString:GetTextColor()
    return math.abs(cr - r) < .01 and math.abs(cg - g) < .01 and math.abs(cb - b) < .01
end
local function barInside()
    local point, to, toPoint, bx, by = bar:GetPoint(1)
    assert(bar:GetNumPoints() == 1 and point == "TOPLEFT" and to == tip and toPoint == "TOPLEFT", "bar not moved in")
    assert(bx > 0 and -by > 0 and -by + bar.height < tip.height, "health bar must be inside the frame")
    assert(bar.width < tip.width)
end

assert(options["tooltip.prettyUnits"] and values["tooltip.prettyUnits"] == true)
units.mouseover = { guid = "0xF130000A34000001", name = "Gorrak Stonefist", level = 43, reaction = 2,
    classification = "elite", creatureType = "Humanoid" }
local abilities = false
local function elite(rows)
    line(rows, "name", "Gorrak Stonefist")
    line(rows, "level", "Level 43 Humanoid (Elite)")
    line(rows, "faction", "Boulderfist Clan")
    line(rows, "quest", "Wanted: Gorrak", nil, { 1, .82, 0 })
    line(rows, "objective", " - Gorrak's Tusk: 0/1")
    line(rows, "drake", "ID: 2612", nil, { .8, .8, .8 })
    line(rows, "npcid", "NPC ID:", "|cffffffff2612")
    line(rows, "npcdisplay", "NPC DisplayID:", "|cffffffff11563")
    line(rows, "loot", "Ctrl-left-click: view creature loot", nil, { .3, .85, 1 })
    if abilities then
        line(rows, "label", "|r|nAbilities: ")
        line(rows, "cleave", "|TInterface\\Icons\\x:24|t|cfff0d9a8 Cleave|r|n")
        line(rows, "heal", "|TInterface\\Icons\\y:24|t|cff7ee07e Lesser Heal|r|n")
    else
        line(rows, "hold", "Hold Alt to show abilities", nil, { .6, .6, .6 })
    end
end
rows = show(elite)
assert(tip.backdrop == nil, "unit tooltips use the frame")
assert(x(rows.name) > 40 and colored(rows.name.left, 1, .23, .19), "hostile name is red, beside the portrait")
for _, key in ipairs({ "level", "drake", "npcid", "npcdisplay", "hold", "loot" }) do
    assert(not visible(rows[key]) and x(rows[key]) < -10000, key .. " should be off the tooltip")
end
local function headerText(wanted)
    local t = assert(ownText(wanted), wanted .. " missing")
    local _, _, _, tx, ty = t:GetPoint()
    assert(tx == x(rows.name) and -ty > depth(rows.name), wanted .. " belongs in the header")
    return -ty
end
assert(not visible(rows.faction) and headerText("Boulderfist Clan"))
assert(ownText("Lvl 43") and colored(ownText("Lvl 43"), 1, .5, .25), "level box with difficulty color")
assert(ownText("Humanoid - Elite"), "type and rank")
assert(x(rows.quest) == 16 and depth(rows.quest) > headerText("Boulderfist Clan") + 20, "quest lines after a divider")
assert(depth(rows.objective) > depth(rows.quest))
assert(ownText("Ctrl-left-click: view creature loot"), "loot hint stays visible")
do
    local _, _, _, _, abilityY = assert(ownText("Hold Alt to show abilities"), "ability hint"):GetPoint()
    local _, _, _, _, lootY = ownText("Ctrl-left-click: view creature loot"):GetPoint()
    assert(-abilityY > depth(rows.objective) and -lootY >= -abilityY + 12, "the two hints stack below the quest")
end
assert(not ownText("Creature ID 2612") and not ownText("Targeting"))
assert(glow() and math.abs(glow()[3] - .3) < .001, "elite glow is gold")
assert(portraits == 1, "portrait drawn")
barInside()
-- Unit backgrounds let more of the world through; items keep their own.
local function backgrounds()
    local alphas = {}
    for _, t in ipairs(tip.created) do
        if t.kind == "Texture" and t.shown and t.gradient then alphas[#alphas + 1] = t.gradient[5] end
    end
    table.sort(alphas)
    return table.concat(alphas, " ")
end
assert(backgrounds() == "0.68 0.74", "unit background opacity: " .. backgrounds())
-- How far apart the header's bottom and the body's top are: negative means the two overlap.
function seam()
    local body, header
    for _, t in ipairs(tip.created) do
        if t.kind == "Texture" and t.shown and t.gradient then
            if t.points.BOTTOMRIGHT[3] == "TOPRIGHT" then header = t else body = t end
        end
    end
    return header.points.BOTTOMRIGHT[5] - body.points.TOPLEFT[5]
end
assert(seam() == 0, "the unit header must not lie over the body, or it looks solid: " .. seam())
local unitHeight, unitWidth = tip.height, tip.width
tip:Run("OnTooltipLayout")
assert(tip.height == unitHeight and tip.width == unitWidth and portraits == 1, "laying out again changes nothing")

-- Alt: abilities and IDs. MobSpells rebuilds the tooltip when Alt changes.
alt, abilities = true, true
rows = show(elite)
assert(visible(rows.cleave) and visible(rows.heal) and not visible(rows.label))
assert(depth(rows.cleave) > depth(rows.objective) + 20, "abilities have their own section")
assert(ownText("Creature ID 2612") and ownText("Display ID 11563") and tip.height > unitHeight)
assert(not ownText("Hold Alt to show abilities"), "no hint while Alt is held")
barInside()
alt = false
modifierFrame.handler(modifierFrame, "MODIFIER_STATE_CHANGED", "LALT")
assert(not visible(rows.cleave) and not ownText("Creature ID 2612"), "abilities only while Alt is held")
abilities = false

-- A tooltip fading out has lost its unit and must keep its look.
rows = show(elite)
local mouseover = units.mouseover
units.mouseover, unitToken = nil, nil
tip:Run("OnTooltipLayout")
update(1)
assert(tip.backdrop == nil and tip.height == unitHeight and ownText("Lvl 43"), "fading tooltip changed")
units.mouseover = mouseover

-- Clearing restores the lines and the health bar.
tip:Clear()
stock()
assert(bar:GetNumPoints() == 2 and bar.height == 8)
do
    local point, to, toPoint, bx, by = bar:GetPoint(1)
    assert(point == "TOPLEFT" and to == tip and toPoint == "BOTTOMLEFT" and bx == 2 and by == -1, "bar not restored")
end

-- A player: guild, race and class, PvP, target, and GearScore only with Alt.
units.mouseover = { guid = "0x0000000000000007", name = "Private Thalia", player = true, level = 45, reaction = 5,
    race = "Night Elf", className = "Hunter", classFile = "HUNTER", guild = "The Night Watch" }
units.player = { guid = "0x0000000000000001", name = "Me", player = true, level = 45, reaction = 5 }
units.mouseovertarget = units.player
local function someone(rows)
    line(rows, "name", "Private Thalia")
    line(rows, "guild", "The Night Watch")
    line(rows, "level", "Level 45 Night Elf Hunter (Player)")
    line(rows, "pvp", "PvP")
    line(rows, "score", "GearScore: 1482", "(iLevel: 52)", { .3, .6, 1 })
    line(rows, "yours", "YourScore: 1630", "(+148)", { 0, 1, 0 })
end
rows = show(someone)
for _, key in ipairs({ "guild", "level", "pvp", "score", "yours" }) do
    assert(not visible(rows[key]) and x(rows[key]) < -10000, key .. " should be off the tooltip")
end
assert(colored(rows.name.left, .67, .83, .45), "class color")
assert(ownText("<The Night Watch>") and ownText("Night Elf Hunter") and ownText("PvP") and ownText("Lvl 45"))
assert(ownText("Targeting") and ownText("You"))
assert(not ownText("GearScore 1482") and not glow(), "no score without Alt and no player glow")
assert(rows.guild.left:GetText() == "The Night Watch" and rows.score.left:GetText() == "GearScore: 1482",
    "other addons' lines keep their text")
barInside()
local playerHeight = tip.height
alt = true
modifierFrame.handler(modifierFrame, "MODIFIER_STATE_CHANGED", "RALT")
assert(ownText("GearScore 1482") and ownText("iLvl 52") and ownText("Yours 1630") and ownText("+148"))
assert(not ownText("Creature ID 7") and tip.height > playerHeight)
alt = false
modifierFrame.handler(modifierFrame, "MODIFIER_STATE_CHANGED", "RALT")
assert(tip.height == playerHeight and not ownText("GearScore 1482"))

-- The target changes while the tooltip is up.
units.boar = { guid = "0xF130000071000002", name = "Young Boar", reaction = 4 }
units.mouseovertarget = units.boar
update(.1)
assert(ownText("You"), "the target is checked a few times a second, not every frame")
update(.2)
assert(ownText("Young Boar") and not ownText("You"))
units.mouseovertarget = nil
update(.3)
assert(not ownText("Targeting") and tip.height < playerHeight)

-- A tagged NPC is grey, a status line is not taken for its faction, and it does not glow like the last unit.
units.mouseover = { guid = "0xF130000071000003", name = "Plains Strider", level = 7, reaction = 4, tapped = true,
    creatureType = "Beast" }
rows = show(function(r)
    line(r, "name", "Plains Strider"); line(r, "level", "Level 7 Beast"); line(r, "skin", "Skinnable")
end)
assert(colored(rows.name.left, .6, .6, .6), "tagged is grey")
assert(x(rows.skin) == 16 and depth(rows.skin) > depth(rows.name) + 20, "skinnable belongs to the body")
assert(ownText("Beast") and not glow())

-- A friendly NPC keeps its role in the header and does not name its type.
units.mouseover = { guid = "0xF130000071000004", name = "Marla Tanner", level = 30, reaction = 6,
    creatureType = "Humanoid" }
rows = show(function(r)
    line(r, "name", "Marla Tanner"); line(r, "role", "Innkeeper"); line(r, "level", "Level 30")
    line(r, "faction", "Stormwind")
end)
assert(visible(rows.role) and x(rows.role) == x(rows.name) and depth(rows.role) < headerText("Stormwind"))
assert(not ownText("Humanoid") and colored(rows.name.left, .12, .82, .12))

-- An item right after a unit is an item tooltip again.
fillGear()
tip:Run("OnTooltipLayout")
assert(ownText("iLvl 47") and not ownText("Lvl 30") and bar:GetNumPoints() == 2)
assert(backgrounds() == "0.94 0.96", "item background opacity: " .. backgrounds())
assert(seam() < -20, "items keep the body behind their header: " .. seam())

-- The unit option leaves item tooltips alone and restores the game's unit tooltip.
rows = show(elite)
values["tooltip.prettyUnits"] = false
options["tooltip.prettyUnits"].apply(false)
stock()
assert(bar:GetNumPoints() == 2)
tip.shown = true
rows = show(elite)
assert(tip.backdrop and tip.backdrop.edgeFile == "stock" and visible(rows.level))
fillGear()
tip:Run("OnTooltipLayout")
assert(tip.backdrop == nil, "items keep their own option")

print("Pretty unit tooltips: NPC and player headers, add-on lines, Alt, target, fading, health bar and option passed")

------------------------------------------------------------------------------------- world objects and corpses --
values["tooltip.prettyUnits"], values["tooltip.prettyObjects"] = true, true
assert(options["tooltip.prettyObjects"])
unitToken = nil
-- The client's order: it adds the object's own lines and quest lines, says how many were the object's, then
-- shows the tooltip. Questie adds more lines later and shows it again.
local function showObject(fill, statusLines, corpse, health)
    tip:Clear()
    if health then bar:Show() else bar:Hide() end
    local rows = {}
    fill(rows)
    for _, fn in ipairs(tip.hooks.OnTooltipSetObject) do fn(tip, statusLines, corpse) end
    tip:Run("OnTooltipLayout")
    return rows
end
local function shownTexture(pattern)
    for _, t in ipairs(tip.created) do
        if t.kind == "Texture" and t.shown and tostring(t.texture):match(pattern) then return t end
    end
end
local function bodyShown()
    for _, t in ipairs(tip.created) do
        if t.kind == "Texture" and t.shown and t.gradient and t.points.BOTTOMRIGHT[3] == "BOTTOMRIGHT" then
            return true
        end
    end
    return false
end

rows = showObject(function(r)
    line(r, "name", "Briarthorn", nil, { 1, .82, 0 })
    line(r, "skill", "Requires Herbalism", nil, { 1, .5, .25 })
end, 2)
assert(tip.backdrop == nil and x(rows.name) == 16 and x(rows.skill) == 16, "name and need form the header")
assert(colored(rows.skill.left, 1, .5, .25), "the skill keeps the game's color")
assert(depth(rows.skill) > depth(rows.name) and depth(rows.skill) < tip.height)
assert(tip.height < depth(rows.skill) + 30, "a header-only tooltip ends with its header: " .. tip.height)
assert(not shownTexture("divider") and not bodyShown() and not shownTexture("glow$"))
assert(rows.name.left:GetFont():match("expressway%-bold"), "bold name")
-- As wide as its longest line (18 letters, 6 units each) plus the margins.
assert(tip.width == 18 * 6 + 32, "no spare width: " .. tip.width)
local small = tip.height
showObject(function(r) line(r, "name", "Forge", nil, { 1, .82, 0 }) end, 1)
assert(tip.width == 96 + 32, "a very short name keeps a small minimum: " .. tip.width)
rows = showObject(function(r)
    line(r, "name", "Briarthorn", nil, { 1, .82, 0 })
    line(r, "skill", "Requires Herbalism", nil, { 1, .5, .25 })
end, 2)

-- Quest lines go below a divider, wherever their colors come from.
local function chest(r)
    line(r, "name", "Battered Footlocker", nil, { 1, .82, 0 })
    line(r, "lock", "Locked", nil, { 1, .13, .13 })
    line(r, "quest", "The Lost Supplies", nil, { 1, .82, 0 })
    line(r, "objective", " - Missing Supplies: 2/5")
end
rows = showObject(chest, 2)
assert(x(rows.lock) == 16 and depth(rows.quest) > depth(rows.lock) + 20, "quest lines after a divider")
assert(depth(rows.objective) > depth(rows.quest) and depth(rows.objective) < tip.height and tip.height > small)
assert(shownTexture("divider") and bodyShown() and backgrounds() == "0.68 0.74" and seam() == 0)
assert(tip.width >= 176 + 32, "the quest section needs room for its divider")

-- Questie adds its lines after the first layout.
local before = tip.height
line(rows, "late", "|cffffff00[12] Another quest|r")
line(rows, "idrow", "Object ID", "|cffffffff2844")
tip:Run("OnTooltipLayout")
assert(visible(rows.late) and depth(rows.late) > depth(rows.objective) and tip.height > before)
assert(not visible(rows.idrow) and x(rows.idrow) < -10000 and not ownText("Object ID 2844"))
alt = true
modifierFrame.handler(modifierFrame, "MODIFIER_STATE_CHANGED", "LALT")
assert(ownText("Object ID 2844"), "the ID shows with Alt")
alt = false
modifierFrame.handler(modifierFrame, "MODIFIER_STATE_CHANGED", "LALT")
assert(not ownText("Object ID 2844"))

-- A destructible building keeps its health bar, inside the frame; clearing puts it back.
showObject(function(r) line(r, "name", "Siege Wall", nil, { 1, .82, 0 }) end, 1, nil, true)
barInside()
assert(bodyShown())
tip:Clear()
stock()
assert(bar:GetNumPoints() == 2 and bar.height == 8)

-- A corpse gets a grey header.
rows = showObject(function(r) line(r, "name", "Corpse of Thalia") end, 1, 1)
assert(tip.backdrop == nil and colored(rows.name.left, .86, .88, .92) and not bodyShown())

-- A unit and an item after an object are themselves again.
units.mouseover = mouseover
show(elite)
assert(ownText("Lvl 43"))
rows = showObject(chest, 2)
assert(not ownText("Lvl 43") and bar:GetNumPoints() == 2)
fillGear()
tip:Run("OnTooltipLayout")
assert(ownText("iLvl 47"))

-- Its own option restores the game's look and leaves the others alone.
showObject(chest, 2)
values["tooltip.prettyObjects"] = false
options["tooltip.prettyObjects"].apply(false)
stock()
tip.shown = true
showObject(chest, 2)
assert(tip.backdrop and tip.backdrop.edgeFile == "stock")
show(elite)
assert(tip.backdrop == nil, "units keep their own option")

print("Pretty object tooltips: header, quest section, late lines, IDs, health bar, corpses and option passed")
