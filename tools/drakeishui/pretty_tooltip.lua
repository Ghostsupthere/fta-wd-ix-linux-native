-- FTA addition: the "pretty" tooltips. A DrakeishUI metal frame, a shaded header, and sections split by a
-- metal bar with a gold star. This file holds the shared drawing and the item layout; pretty_unit_tooltip.lua
-- adds NPCs and players. The design follows PrettyTooltip (MIT, Copyright (c) FallenShard) for the quality
-- header, glow and body colours.
--
-- The game's own lines stay where other addons expect them: this only moves, hides and decorates them after
-- the engine has laid the tooltip out. That needs the client's OnTooltipLayout script; without it, tooltips
-- keep the stock look.
local _, addon = ...
local S = addon.Settings
if not (GameTooltip and GameTooltip.HasScript and GameTooltip:HasScript("OnTooltipLayout")) then return end

local MEDIA = "Interface\\AddOns\\DrakeishUI\\assets\\Tooltip\\"
local BAR = "Interface\\AddOns\\DrakeishUI\\assets\\UI\\uidialog-divider"
local WHITE = "Interface\\Buttons\\WHITE8X8"
local BLUE = "|cff66ccff"
-- Where replaced and unwanted lines go. Transparency alone does not hide them: another addon recolouring a
-- line (WarcraftXLControl does that to the armour type) makes it opaque again.
local OFFSCREEN = -20000
-- Between the parts of a subtitle. Plain ASCII: with the game's own font a middle dot came out as "?",
-- drawn over the letter after it.
local SEPARATOR = " - "
-- The item name is bold and outlined. Only these fonts have a matching bold file.
local BOLD_NARROW = "Interface\\AddOns\\DrakeishUI\\modules\\Kui_Nameplates\\Media\\f\\nimbus-sans-narrow-bold.otf"
local BOLD = "Interface\\AddOns\\DrakeishUI\\modules\\Kui_Nameplates\\Media\\f\\expressway-bold.ttf"

local TOOLTIPS = { "GameTooltip", "ItemRefTooltip", "ShoppingTooltip1", "ShoppingTooltip2", "ShoppingTooltip3",
    "ItemRefShoppingTooltip1", "ItemRefShoppingTooltip2", "ItemRefShoppingTooltip3" }
local states = {}
-- What each kind of tooltip needs: its option, its layout and, if it moved something else, how to put it back.
local kinds = {}
local reported

local refreshAll
S:Register({ id = "tooltip.pretty", name = "Pretty item tooltips", category = "Tooltips", group = "Style",
    type = "toggle", default = true, keywords = "item quality header frame star pretty",
    desc = "A metal frame, a header colored by item quality and star dividers. Hold Alt for item IDs and every bot upgrade.",
    apply = function() refreshAll() end })

-------------------------------------------------------------------------------------------------- helpers --
local function plain(fontString)
    local text = fontString and fontString:GetText() or ""
    text = text:gsub("|c%x%x%x%x%x%x%x%x", ""):gsub("|r", "")
    return (text:gsub("^%s+", ""):gsub("%s+$", ""))
end

-- "Durability %d / %d" -> a pattern capturing each number.
local function toPattern(template)
    local pattern = template:gsub("[%^%$%(%)%.%[%]%*%+%-%?]", "%%%0")
    return "^" .. pattern:gsub("%%d", "(%%d+)") .. "$"
end

local function bindingTexts()
    local set = {}
    for _, key in ipairs({ "ITEM_SOULBOUND", "ITEM_BIND_ON_PICKUP", "ITEM_BIND_ON_EQUIP", "ITEM_BIND_ON_USE",
        "ITEM_BIND_TO_ACCOUNT", "ITEM_ACCOUNTBOUND", "ITEM_BIND_QUEST" }) do
        if _G[key] then set[_G[key]] = true end
    end
    return set
end

local function isRed(fontString)
    local r, g, b = fontString:GetTextColor()
    return r > .9 and g < .2 and b < .2
end

local function screenHeight()
    local resolution = GetCVar("gxResolution")
    return tonumber(resolution and resolution:match("x(%d+)")) or 768
end

-- Sizes follow the tooltip's body text, snapped to whole screen pixels.
local function metrics(tip)
    local second = _G[tip:GetName() .. "TextLeft2"]
    local _, size = (second or _G[tip:GetName() .. "TextLeft1"]):GetFont()
    local scale = (size or 12) / 15
    local pixel = 768 / (screenHeight() * tip:GetEffectiveScale())
    return function(design)
        return math.max(1, math.floor(design * scale / pixel + .5)) * pixel
    end
end

---------------------------------------------------------------------------------------------- decorations --
local function texture(tip, layer, path)
    local t = tip:CreateTexture(nil, layer)
    t:SetTexture(path)
    return t
end

local function fontString(tip)
    return tip:CreateFontString(nil, "OVERLAY", "GameTooltipTextSmall")
end

-- Eight pieces of a 64 px square texture: four corners and four stretched edges.
local function nineSlice(tip, layer, path, slice)
    local a, b = slice / 64, 1 - slice / 64
    local set = {}
    local coords = {
        topLeft = { 0, a, 0, a }, top = { a, b, 0, a }, topRight = { b, 1, 0, a },
        left = { 0, a, a, b }, right = { b, 1, a, b },
        bottomLeft = { 0, a, b, 1 }, bottom = { a, b, b, 1 }, bottomRight = { b, 1, b, 1 },
    }
    for key, c in pairs(coords) do
        set[key] = texture(tip, layer, path)
        set[key]:SetTexCoord(c[1], c[2], c[3], c[4])
    end
    return set
end

-- outset 0 draws the pieces inside the tooltip's edge; 1 draws them outside it.
local function placeSlice(tip, set, size, outset)
    for _, t in pairs(set) do t:ClearAllPoints() end
    local o = outset * size
    local function corner(t, point, x, y)
        t:SetPoint(point, tip, point, x * o, y * o)
        t:SetWidth(size)
        t:SetHeight(size)
    end
    corner(set.topLeft, "TOPLEFT", -1, 1)
    corner(set.topRight, "TOPRIGHT", 1, 1)
    corner(set.bottomLeft, "BOTTOMLEFT", -1, -1)
    corner(set.bottomRight, "BOTTOMRIGHT", 1, -1)
    local function edge(t, from, fromCorner, fromPoint, to, toCorner, toPoint)
        t:SetPoint(from, set[fromCorner], fromPoint)
        t:SetPoint(to, set[toCorner], toPoint)
    end
    edge(set.top, "TOPLEFT", "topLeft", "TOPRIGHT", "BOTTOMRIGHT", "topRight", "BOTTOMLEFT")
    edge(set.bottom, "TOPLEFT", "bottomLeft", "TOPRIGHT", "BOTTOMRIGHT", "bottomRight", "BOTTOMLEFT")
    edge(set.left, "TOPLEFT", "topLeft", "BOTTOMLEFT", "BOTTOMRIGHT", "bottomLeft", "TOPRIGHT")
    edge(set.right, "TOPLEFT", "topRight", "BOTTOMLEFT", "BOTTOMRIGHT", "bottomRight", "TOPRIGHT")
end

-- A thin border drawn as four strips, so nothing has to be layered under what it frames.
local function newOutline(tip)
    local strips = {}
    for index = 1, 4 do strips[index] = texture(tip, "ARTWORK", WHITE) end
    return strips
end

local function placeOutline(tip, strips, x, y, width, boxHeight, thickness, r, g, b)
    local rects = {
        { x, y, width, thickness },
        { x, y + boxHeight - thickness, width, thickness },
        { x, y + thickness, thickness, boxHeight - 2 * thickness },
        { x + width - thickness, y + thickness, thickness, boxHeight - 2 * thickness },
    }
    for index, strip in ipairs(strips) do
        local rect = rects[index]
        strip:SetVertexColor(r, g, b, 1)
        strip:ClearAllPoints()
        strip:SetPoint("TOPLEFT", tip, "TOPLEFT", rect[1], -rect[2])
        strip:SetWidth(rect[3])
        strip:SetHeight(rect[4])
        strip:Show()
    end
end

local function setShown(region, shown)
    if shown then region:Show() else region:Hide() end
end

local function sliceShown(set, shown)
    for _, t in pairs(set) do setShown(t, shown) end
end

-- DrakeishUI's metal divider is 193 x 16 of a 256 x 32 file, with 12 px end caps.
-- It is drawn at BAR_HEIGHT design pixels (its metal covers about half of that).
local BAR_HEIGHT = 9

local function newBar(tip)
    local bar = {}
    local cuts = { { 0, 12 }, { 12, 181 }, { 181, 193 } }
    for index, cut in ipairs(cuts) do
        bar[index] = texture(tip, "ARTWORK", BAR)
        bar[index]:SetTexCoord(cut[1] / 256, cut[2] / 256, 0, .5)
    end
    return bar
end

local function placeBar(tip, bar, x, y, width, u)
    local cap, height = u(12 * BAR_HEIGHT / 16), u(BAR_HEIGHT)
    local widths = { cap, math.max(1, width - 2 * cap), cap }
    for index, piece in ipairs(bar) do
        piece:ClearAllPoints()
        piece:SetPoint("TOPLEFT", tip, "TOPLEFT", x, -y)
        piece:SetWidth(widths[index])
        piece:SetHeight(height)
        piece:Show()
        x = x + widths[index]
    end
end

local function newDivider(tip)
    local divider = { left = newBar(tip), right = newBar(tip), star = texture(tip, "ARTWORK", MEDIA .. "star") }
    return divider
end

local function hideDivider(divider)
    for index = 1, 3 do
        divider.left[index]:Hide()
        divider.right[index]:Hide()
    end
    divider.star:Hide()
end

-- A star between two bars. Returns the y below the divider.
local function drawDivider(tip, divider, x, y, width, u)
    y = y + u(4)
    local star, gap = u(22), u(5)
    local half = (width - star) / 2 - gap
    local barTop = y + (star - u(BAR_HEIGHT)) / 2
    placeBar(tip, divider.left, x, barTop, half, u)
    placeBar(tip, divider.right, x + width - half, barTop, half, u)
    divider.star:ClearAllPoints()
    divider.star:SetPoint("TOPLEFT", tip, "TOPLEFT", x + (width - star) / 2, -y)
    divider.star:SetWidth(star)
    divider.star:SetHeight(star)
    divider.star:Show()
    return y + star + u(2)
end

local function build(tip)
    local st = states[tip]
    if st.art then return st.art end
    local art = {}
    st.art = art
    art.glow = nineSlice(tip, "BACKGROUND", MEDIA .. "glow", 16)
    art.body = texture(tip, "BACKGROUND", WHITE)
    art.header = texture(tip, "BORDER", WHITE)
    -- The client draws textures of one layer in no promised order, so anything over the header is above BORDER.
    art.frame = nineSlice(tip, "ARTWORK", MEDIA .. "frame", 6)
    art.icon = texture(tip, "ARTWORK", WHITE)
    art.icon:SetTexCoord(.07, .93, .07, .93)
    art.iconOutline = newOutline(tip)
    -- The item level box, under the icon: a darker fill inside the same thin border.
    art.badgeFill = texture(tip, "ARTWORK", WHITE)
    art.badgeOutline = newOutline(tip)
    art.badge = fontString(tip)
    art.subtitle = fontString(tip)
    art.more = fontString(tip)
    art.durabilityBack = texture(tip, "ARTWORK", WHITE)
    art.durabilityFill = texture(tip, "OVERLAY", WHITE)
    art.durability = fontString(tip)
    art.itemID = fontString(tip)
    art.displayID = fontString(tip)
    art.botDivider = newDivider(tip)
    art.footerDivider = newDivider(tip)
    return art
end

local TEXTS = { "badge", "subtitle", "more", "durability", "itemID", "displayID" }
local REGIONS = { "body", "header", "icon", "badgeFill", "durabilityBack", "durabilityFill" }

local function hideArt(art)
    sliceShown(art.glow, false)
    sliceShown(art.frame, false)
    sliceShown(art.iconOutline, false)
    sliceShown(art.badgeOutline, false)
    for _, key in ipairs(TEXTS) do art[key]:Hide() end
    for _, key in ipairs(REGIONS) do art[key]:Hide() end
    hideDivider(art.botDivider)
    hideDivider(art.footerDivider)
end

-- The first line is shared by every kind of tooltip, so its own font must come back.
local function restoreTitle(st)
    local title = st.title
    if not title then return end
    st.title = nil
    if title.line:GetFont() == title.bold then
        title.line:SetFont(title.font, title.size, title.flags)
    end
end

local function boldTitle(st, fontStringTitle)
    if st.title and st.title.line ~= fontStringTitle then restoreTitle(st) end
    local font, size, flags = fontStringTitle:GetFont()
    if not font then return end
    local bold = font:lower():find("arialn", 1, true) and BOLD_NARROW or BOLD
    -- Anything but our own bold file is the font to go back to.
    if font ~= BOLD and font ~= BOLD_NARROW then
        st.title = { line = fontStringTitle, font = font, size = size, flags = flags or "", bold = bold }
    end
    if st.title then fontStringTitle:SetFont(st.title.bold, st.title.size, "OUTLINE") end
end

------------------------------------------------------------------------------------------------- restore --
-- Puts the lines back on the stock chain of anchors and brings the game's backdrop back.
local function restore(tip)
    local st = states[tip]
    if not st.active then return end
    local kind = kinds[st.active]
    st.active = nil
    if kind.restore then kind.restore(tip, st) end
    hideArt(st.art)
    restoreTitle(st)
    local name = tip:GetName()
    local index = 1
    while _G[name .. "TextLeft" .. index] do
        local left, right = _G[name .. "TextLeft" .. index], _G[name .. "TextRight" .. index]
        left:SetAlpha(1)
        right:SetAlpha(1)
        left:ClearAllPoints()
        if index == 1 then
            left:SetPoint(unpack(st.firstPoint))
        else
            left:SetPoint("TOPLEFT", _G[name .. "TextLeft" .. (index - 1)], "BOTTOMLEFT", 0, -2)
        end
        right:ClearAllPoints()
        right:SetPoint("RIGHT", left, "LEFT", 40, 0)
        index = index + 1
    end
    if st.backdrop then
        tip:SetBackdrop(st.backdrop)
        local border, fill = TOOLTIP_DEFAULT_COLOR, TOOLTIP_DEFAULT_BACKGROUND_COLOR
        if border then tip:SetBackdropBorderColor(border.r, border.g, border.b) end
        if fill then tip:SetBackdropColor(fill.r, fill.g, fill.b) end
    end
end

------------------------------------------------------------------------------------------------- reading --
-- Socket and threat icons hang off the left of their line, further out than our margin. Pull each one close
-- to its line; returns how far the line then has to move right, so the icon starts at the margin.
local function lineIcons(tip, u)
    local inset = {}
    for index = 1, 10 do
        local icon = _G[tip:GetName() .. "Texture" .. index]
        if icon and icon:IsShown() then
            local _, anchor = icon:GetPoint(1)
            if anchor then
                local gap = u(5)
                icon:ClearAllPoints()
                icon:SetPoint("TOPRIGHT", anchor, "TOPLEFT", -gap, 0)
                inset[anchor] = icon:GetWidth() + gap
            end
        end
    end
    return inset
end

local function read(tip, equipLoc, itemType, boxed, u)
    local name = tip:GetName()
    local money, inset = {}, lineIcons(tip, u)
    for index = 1, tip.shownMoneyFrames or 0 do
        local frame = _G[name .. "MoneyFrame" .. index]
        if frame and frame:IsShown() then
            local _, anchor = frame:GetPoint(1)
            if anchor then money[anchor] = frame end
        end
    end
    local model = { lines = {}, body = {}, botRows = {}, money = {}, hidden = {} }
    local binds = bindingTexts()
    local durabilityPattern = toPattern(DURABILITY_TEMPLATE or "Durability %d / %d")
    local levelPattern = toPattern(ITEM_MIN_LEVEL or "Requires Level %d")
    local slotName = equipLoc and equipLoc ~= "" and _G[equipLoc]
    local itemLevelPattern = boxed and toPattern(ITEM_LEVEL or "Item Level %d")
    -- A recipe holds a second item inside it, so its lines keep their order.
    local recipe = itemType == "Recipe"

    for index = 1, tip:NumLines() do
        local left, right = _G[name .. "TextLeft" .. index], _G[name .. "TextRight" .. index]
        local line = { left = left, right = right, inset = inset[left] or 0,
            hasRight = right:IsShown() and plain(right) ~= "" }
        line.text = plain(left)
        model.lines[index] = line
        local text = line.text
        local current, total = text:match(durabilityPattern)

        if not model.title then
            if CURRENTLY_EQUIPPED and text == CURRENTLY_EQUIPPED then
                model.tag = line
            else
                model.title = line
            end
        elseif money[left] then
            line.money = money[left]
            model.money[#model.money + 1] = line
        elseif text == "" and not line.hasRight then
            model.hidden[#model.hidden + 1] = line
            if not model.bot then model.body[#model.body + 1] = { gap = true } end
        elseif itemLevelPattern and not line.hasRight and text:match(itemLevelPattern) then
            model.hidden[#model.hidden + 1] = line
        elseif text:match("^GearScore:") or text:match("^YourScore:") or text == "ItemID:" or text == "DisplayID:"
            or text == "SpellID:" or text:match("^ID: %d+$") then
            -- No GearScore on items; IDs have their own row while Alt is held.
            model.hidden[#model.hidden + 1] = line
        elseif not model.bot and (text:find("Bot Upgrade:", 1, true) or text:find("Bot upgrade:", 1, true)) then
            model.bot = line
        elseif model.bot and not model.botDone and line.hasRight and plain(right):find("ilvl", 1, true) then
            model.botRows[#model.botRows + 1] = line
        else
            if model.bot then model.botDone = true end
            if text == "Reagent Bank" then
                model.reagent = line
            elseif current and not model.durability then
                model.durability, model.current, model.total = line, tonumber(current), tonumber(total)
            elseif not recipe and not model.bind and binds[text] and not line.hasRight then
                model.bind = line
            elseif not recipe and not model.level and text:match(levelPattern) and not line.hasRight then
                model.level = line
            elseif not recipe and not model.slot and slotName and text == slotName then
                model.slot = line
            else
                model.body[#model.body + 1] = line
            end
        end
    end
    return model
end

-- "|cff..SLOT  +13 ilvl  +8.4 score  GS 1234  server checked" -> ilvl and score only.
local function shortenBotRow(right)
    local text = right:GetText() or ""
    local start = text:find("|c", 1, true)
    if start then text = text:sub(start) end
    text = text:gsub("%s*|cff%x%x%x%x%x%xGS %d+|r", ""):gsub("%s*|cff%x%x%x%x%x%xserver checked|r", "")
    right:SetText(text)
end

local function subtitle(model, itemType, subType)
    if model.slot then
        local parts = {}
        local function add(fontStringPart, text)
            if text ~= "" then
                parts[#parts + 1] = isRed(fontStringPart) and "|cffff2020" .. text .. "|r" or text
            end
        end
        if model.slot.hasRight then add(model.slot.right, plain(model.slot.right)) end
        add(model.slot.left, model.slot.text)
        return table.concat(parts, SEPARATOR)
    end
    if subType and subType ~= "" and subType ~= itemType then return (itemType or "") .. SEPARATOR .. subType end
    return itemType or ""
end

-------------------------------------------------------------------------------------------------- layout --
local function height(line)
    local value = line.left:GetHeight()
    if line.hasRight then value = math.max(value, line.right:GetHeight()) end
    return value
end

-- Takes over a tooltip: remembers how to put it back and removes the game's backdrop.
local function prepare(tip, st, kind)
    if not st.firstPoint then
        local point, relative, relativePoint, x, y = _G[tip:GetName() .. "TextLeft1"]:GetPoint(1)
        st.firstPoint = { point or "TOPLEFT", relative or tip, relativePoint or "TOPLEFT", x or 10, y or -10 }
    end
    if not st.active then
        st.backdrop = tip:GetBackdrop()
        tip:SetBackdrop(nil)
    end
    st.active = kind
    return build(tip)
end

-- How to move lines in a tooltip with this padding and content width.
local function lineTools(tip, pad, content, u)
    local function place(line, x, y)
        line.left:ClearAllPoints()
        line.left:SetPoint("TOPLEFT", tip, "TOPLEFT", x, -y)
        if line.hasRight then
            line.right:ClearAllPoints()
            line.right:SetPoint("RIGHT", line.left, "LEFT", pad + content - x, 0)
        end
    end
    -- The line stays shown and keeps its text for other addons; it is only moved out of sight.
    local function hide(line)
        line.left:SetAlpha(0)
        line.right:SetAlpha(0)
        line.left:ClearAllPoints()
        line.left:SetPoint("TOPLEFT", tip, "TOPLEFT", OFFSCREEN, 0)
        if line.hasRight then
            line.right:ClearAllPoints()
            line.right:SetPoint("RIGHT", line.left, "LEFT", 0, 0)
        end
    end
    -- A normal body line, wrapped when it is longer than the tooltip.
    local function row(line, y)
        if not line.hasRight and line.left:GetStringWidth() > content - line.inset then
            line.left:SetWidth(content - line.inset)
        end
        place(line, pad + line.inset, y)
        return y + height(line) + u(3)
    end
    return place, hide, row
end

-- Body lines in the game's order; blank lines become a small space between them.
local function bodyRows(lines, y, row, u)
    local drawn, pendingGap = false, false
    for _, line in ipairs(lines) do
        if line.gap then
            pendingGap = drawn
        else
            if pendingGap then y = y + u(6) end
            pendingGap, drawn = false, true
            y = row(line, y)
        end
    end
    return y
end

local OPACITY = { body = .94, header = .96 }

-- Background, shaded header, metal frame and an optional glow, once the size is known.
local function decorate(tip, art, u, headerBottom, header, glow, opacity)
    opacity = opacity or OPACITY
    local tr, tg, tb = 0, 0, 0
    if glow then tr, tg, tb = glow[1], glow[2], glow[3] end
    local r, g, b = header[1], header[2], header[3]
    local frameSize = u(6)

    art.body:ClearAllPoints()
    -- The header lies over the body, so their transparencies multiply. For a header that should let the
    -- world through, the body has to start below it.
    local bodyTop = opacity.split and headerBottom or frameSize / 2
    art.body:SetPoint("TOPLEFT", tip, "TOPLEFT", frameSize / 2, -bodyTop)
    art.body:SetPoint("BOTTOMRIGHT", tip, "BOTTOMRIGHT", -frameSize / 2, frameSize / 2)
    art.body:SetGradientAlpha("VERTICAL", .016 + tr * .02, .012 + tg * .02, .016 + tb * .02, opacity.body,
        .052 + tr * .06, .040 + tg * .06, .050 + tb * .06, opacity.body)
    art.body:Show()

    art.header:ClearAllPoints()
    art.header:SetPoint("TOPLEFT", tip, "TOPLEFT", frameSize / 2, -frameSize / 2)
    art.header:SetPoint("BOTTOMRIGHT", tip, "TOPRIGHT", -frameSize / 2, -headerBottom)
    art.header:SetGradientAlpha("VERTICAL", r * .12, g * .12, b * .12, opacity.header,
        r * .38, g * .38, b * .38, opacity.header)
    art.header:Show()

    placeSlice(tip, art.frame, frameSize, 0)
    sliceShown(art.frame, true)
    sliceShown(art.glow, false)
    if glow then
        placeSlice(tip, art.glow, u(16), 1)
        for _, piece in pairs(art.glow) do piece:SetVertexColor(tr, tg, tb, .55) end
        sliceShown(art.glow, true)
    end
end

local function style(tip, st)
    local _, link = tip:GetItem()
    if not link then return false end
    local itemName, _, quality, level, _, itemType, subType, _, equipLoc, icon = GetItemInfo(link)
    if not itemName then return false end

    local art = prepare(tip, st, "item")
    local name = tip:GetName()
    local u = metrics(tip)
    local alt = IsAltKeyDown()
    local boxed = equipLoc and equipLoc ~= "" and (level or 0) > 1
    local model = read(tip, equipLoc, itemType, boxed, u)
    if not model.title then return false end
    hideArt(art)
    boldTitle(st, model.title.left)

    -- Fonts for our own text follow the tooltip's body font.
    local fontFile, fontSize, fontFlags = _G[name .. "TextLeft2"]:GetFont()
    local small = math.max(8, (fontSize or 12) - 1)
    for _, key in ipairs(TEXTS) do
        if fontFile then art[key]:SetFont(fontFile, key == "subtitle" and fontSize or small, fontFlags or "") end
    end

    if model.bot then
        local header = model.bot.left:GetText() or ""
        model.bot.left:SetText((header:gsub("|cff66ccffAshen Bot Upgrade:|r", BLUE .. "Bot upgrade:|r")))
        for _, row in ipairs(model.botRows) do shortenBotRow(row.right) end
    end

    -- Measure every line at its natural width.
    for _, line in ipairs(model.lines) do
        line.left:SetAlpha(1)
        line.right:SetAlpha(1)
        line.left:SetWidth(0)
    end

    art.subtitle:SetText(subtitle(model, itemType, subType))
    local color = ITEM_QUALITY_COLORS and ITEM_QUALITY_COLORS[quality]
    local r, g, b = 1, 1, 1
    if color then r, g, b = color.r, color.g, color.b end
    art.subtitle:SetTextColor(r + (1 - r) * .45, g + (1 - g) * .45, b + (1 - b) * .45)

    local gap, iconSize, iconGap = u(24), u(44), u(12)
    art.badge:SetText("iLvl " .. (level or 0))
    iconSize = math.max(iconSize, art.badge:GetStringWidth() + u(8))
    local content = u(288)
    local function need(value) content = math.max(content, value) end
    local function width(line)
        local value = line.inset + line.left:GetStringWidth()
        if line.hasRight then value = value + gap + line.right:GetStringWidth() end
        return value
    end
    local close = tip == ItemRefTooltip and u(24) or 0
    -- The outline makes the name a little wider than its measured text.
    need(iconSize + iconGap + math.max(model.title.left:GetStringWidth() + u(4), art.subtitle:GetStringWidth()) + close)
    if model.bind or model.level then
        need(iconSize + iconGap + (model.bind and width(model.bind) or 0)
            + (model.level and width(model.level) + (model.bind and gap or 0) or 0))
    end
    local wrapLimit = u(320)
    for _, line in ipairs(model.body) do
        if not line.gap and (line.hasRight or width(line) <= wrapLimit) then need(width(line)) end
    end
    if model.bot then need(width(model.bot)) end
    for _, line in ipairs(model.botRows) do need(width(line)) end
    if model.reagent then need(width(model.reagent)) end
    for _, line in ipairs(model.money) do
        need(line.money:GetWidth() + (model.durability and u(140) or 0))
    end

    local pad = u(16)
    local place, hide, row = lineTools(tip, pad, content, u)

    for _, line in ipairs(model.hidden) do hide(line) end

    -- Header: icon, name, subtitle, then binding and required level.
    local y = u(14)
    if model.tag then y = row(model.tag, y) end
    local top = y

    -- A thin border in the quality colour around the icon and the item level box.
    local edge = u(2)
    art.icon:SetTexture(icon or (GetItemIcon and GetItemIcon(link)) or WHITE)
    art.icon:ClearAllPoints()
    art.icon:SetPoint("TOPLEFT", tip, "TOPLEFT", pad + edge, -(top + edge))
    art.icon:SetWidth(iconSize - 2 * edge)
    art.icon:SetHeight(iconSize - 2 * edge)
    art.icon:Show()
    placeOutline(tip, art.iconOutline, pad, top, iconSize, iconSize, edge, r, g, b)
    local iconColumn = iconSize
    if boxed then
        art.badge:SetText("iLvl " .. level)
        art.badge:SetTextColor(.86, .89, .94)
        local boxTop, boxHeight = top + iconSize + u(4), art.badge:GetHeight() + u(6)
        art.badgeFill:SetVertexColor(.03 + r * .07, .035 + g * .07, .05 + b * .07, 1)
        art.badgeFill:ClearAllPoints()
        art.badgeFill:SetPoint("TOPLEFT", tip, "TOPLEFT", pad + edge, -(boxTop + edge))
        art.badgeFill:SetWidth(iconSize - 2 * edge)
        art.badgeFill:SetHeight(boxHeight - 2 * edge)
        art.badgeFill:Show()
        placeOutline(tip, art.badgeOutline, pad, boxTop, iconSize, boxHeight, edge, r, g, b)
        art.badge:ClearAllPoints()
        art.badge:SetPoint("CENTER", art.badgeFill, "CENTER", 0, 0)
        art.badge:Show()
        iconColumn = iconSize + u(4) + boxHeight
    end

    -- Beside them: name, type, then binding with the required level at the right.
    local textX = pad + iconSize + iconGap
    place(model.title, textX, top + u(2))
    local textY = top + u(2) + height(model.title) + u(3)
    art.subtitle:ClearAllPoints()
    art.subtitle:SetPoint("TOPLEFT", tip, "TOPLEFT", textX, -textY)
    art.subtitle:Show()
    if model.slot then hide(model.slot) end
    textY = textY + art.subtitle:GetHeight()
    if model.bind or model.level then
        textY = textY + u(5)
        local rowHeight = 0
        if model.bind then
            place(model.bind, textX, textY)
            rowHeight = height(model.bind)
        end
        if model.level then
            place(model.level, pad + content - model.level.left:GetStringWidth(), textY)
            rowHeight = math.max(rowHeight, height(model.level))
        end
        textY = textY + rowHeight
    end
    y = math.max(top + iconColumn, textY)
    local headerBottom = y + u(9)
    y = headerBottom + u(6)

    -- Stats, enchants, effects and every line this does not recognise, in the game's order.
    y = bodyRows(model.body, y, row, u)

    if model.bot then
        y = drawDivider(tip, art.botDivider, pad, y, content, u)
        y = row(model.bot, y)
        for index, line in ipairs(model.botRows) do
            if alt or index == 1 then y = row(line, y) else hide(line) end
        end
        if not alt and #model.botRows > 1 then
            art.more:SetText((#model.botRows - 1) .. " more")
            art.more:SetTextColor(.6, .62, .66)
            art.more:ClearAllPoints()
            art.more:SetPoint("TOPLEFT", tip, "TOPLEFT", pad, -y)
            art.more:Show()
            y = y + art.more:GetHeight() + u(3)
        end
    end

    -- Footer: reagent bank, durability and sell price.
    if model.reagent or model.durability or model.money[1] then
        y = drawDivider(tip, art.footerDivider, pad, y, content, u)
        if model.reagent then y = row(model.reagent, y) end
        if model.durability or model.money[1] then
            local rowHeight = model.durability and height(model.durability) or 0
            local first = model.money[1]
            if first then rowHeight = math.max(rowHeight, height(first), first.money:GetHeight()) end
            if model.durability then
                hide(model.durability)
                local fraction = model.total > 0 and model.current / model.total or 0
                local barWidth, barHeight = u(70), u(6)
                art.durabilityBack:SetVertexColor(.15, .17, .21, 1)
                art.durabilityBack:ClearAllPoints()
                art.durabilityBack:SetPoint("TOPLEFT", tip, "TOPLEFT", pad, -(y + (rowHeight - barHeight) / 2))
                art.durabilityBack:SetWidth(barWidth)
                art.durabilityBack:SetHeight(barHeight)
                art.durabilityBack:Show()
                if fraction > 0 then
                    if fraction > .5 then
                        art.durabilityFill:SetVertexColor(.44, .75, .37, 1)
                    elseif fraction > .2 then
                        art.durabilityFill:SetVertexColor(.95, .75, .2, 1)
                    else
                        art.durabilityFill:SetVertexColor(.9, .25, .2, 1)
                    end
                    art.durabilityFill:ClearAllPoints()
                    art.durabilityFill:SetPoint("TOPLEFT", art.durabilityBack, "TOPLEFT")
                    art.durabilityFill:SetWidth(barWidth * math.min(1, fraction))
                    art.durabilityFill:SetHeight(barHeight)
                    art.durabilityFill:Show()
                end
                art.durability:SetText(model.current .. " / " .. model.total)
                art.durability:SetTextColor(.71, .68, .62)
                art.durability:ClearAllPoints()
                art.durability:SetPoint("LEFT", art.durabilityBack, "RIGHT", u(8), 0)
                art.durability:Show()
            end
            -- The money frame hangs 4 units right of its line's left edge.
            for index, line in ipairs(model.money) do
                if index > 1 then rowHeight = math.max(height(line), line.money:GetHeight()) end
                place(line, pad + content - line.money:GetWidth() - 4,
                    y + (rowHeight - line.left:GetHeight()) / 2)
                y = y + rowHeight + u(3)
            end
            if not first then y = y + rowHeight + u(3) end
        end
    end

    if alt then
        local id = tonumber(link:match("item:(%d+)"))
        if id then
            y = y + u(5)
            art.itemID:SetText("Item ID " .. id)
            art.itemID:SetTextColor(.6, .62, .66)
            art.itemID:ClearAllPoints()
            art.itemID:SetPoint("TOPLEFT", tip, "TOPLEFT", pad, -y)
            art.itemID:Show()
            local display = IdTipItemDisplayIDs and IdTipItemDisplayIDs[id]
            if display then
                art.displayID:SetText("Display ID " .. display)
                art.displayID:SetTextColor(.6, .62, .66)
                art.displayID:ClearAllPoints()
                art.displayID:SetPoint("TOPRIGHT", tip, "TOPLEFT", pad + content, -y)
                art.displayID:Show()
            end
            y = y + art.itemID:GetHeight()
        end
    end

    tip:SetWidth(content + 2 * pad)
    tip:SetHeight(y + u(12))

    -- Uncommon and better items and quest items glow.
    local glow
    if itemType == "Quest" then
        r, g, b = 1, .82, 0
        glow = { r, g, b }
    elseif (quality or 0) >= 2 then
        glow = { r, g, b }
    end
    decorate(tip, art, u, headerBottom, { r, g, b }, glow)
    return true
end

kinds.item = { option = "tooltip.pretty", style = style }

local function layout(tip)
    local st = states[tip]
    if st.busy then return end
    local kind = st.kind and kinds[st.kind]
    if not (kind and S:IsReady() and S:Get(kind.option)) then
        restore(tip)
        return
    end
    st.busy = true
    local ok, result = pcall(kind.style, tip, st)
    st.busy = nil
    if not (ok and result) then
        restore(tip)
        -- A broken layout must never take the tooltip with it; report it once.
        if not ok and not reported then
            reported = true
            geterrorhandler()(result)
        end
    end
end

refreshAll = function()
    for tip, st in pairs(states) do
        if st.active or st.kind then
            -- Hidden tooltips are rebuilt in the chosen style the next time they show.
            restore(tip)
            tip:Hide()
        end
    end
end

for _, name in ipairs(TOOLTIPS) do
    local tip = _G[name]
    if tip and tip.HasScript and tip:HasScript("OnTooltipLayout") then
        states[tip] = {}
        tip:HookScript("OnTooltipCleared", function(self)
            states[self].kind, states[self].info = nil, nil
            restore(self)
        end)
        tip:HookScript("OnTooltipSetItem", function(self) states[self].kind = "item" end)
        tip:HookScript("OnTooltipLayout", layout)
        -- Other modules set tooltip fonts when it appears; measure again with those.
        tip:HookScript("OnShow", layout)
    end
end

local events = CreateFrame("Frame")
events:RegisterEvent("MODIFIER_STATE_CHANGED")
events:SetScript("OnEvent", function(_, _, key)
    if key ~= "LALT" and key ~= "RALT" then return end
    for tip, st in pairs(states) do
        if st.active and tip:IsShown() then layout(tip) end
    end
end)

addon.PrettyTooltip = {
    Layout = layout,
    Refresh = refreshAll,
    states = states,
    kinds = kinds,
    kit = {
        WHITE = WHITE, SEPARATOR = SEPARATOR, plain = plain, toPattern = toPattern, metrics = metrics, texture = texture,
        fontString = fontString, newOutline = newOutline, placeOutline = placeOutline, sliceShown = sliceShown,
        newDivider = newDivider, hideDivider = hideDivider, drawDivider = drawDivider, boldTitle = boldTitle,
        height = height, lineIcons = lineIcons, prepare = prepare, lineTools = lineTools, bodyRows = bodyRows, decorate = decorate,
    },
}
