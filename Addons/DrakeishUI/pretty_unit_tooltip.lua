-- FTA addition: the "pretty" tooltip for NPCs and players, drawn with the pieces in pretty_tooltip.lua.
-- A portrait with the level under it, a header in the reaction or class colour, then the game's and other
-- addons' lines. GearScore, NPC abilities and IDs show while Alt is held. The health bar moves inside.
local _, addon = ...
local S, pretty, tip = addon.Settings, addon.PrettyTooltip, GameTooltip
if not (pretty and pretty.states[tip]) then return end

local kit = pretty.kit
local WHITE, SEPARATOR, plain = kit.WHITE, kit.SEPARATOR, kit.plain
local height, texture, setSlice = kit.height, kit.texture, kit.sliceShown

-- Much more see-through than items: over dim ground, anything above about .8 looks solid.
local OPACITY = { body = .68, header = .74, split = true }

local ORANGE, SOFT, GREY = { 1, .5, .25 }, { .71, .68, .62 }, { .6, .62, .66 }
local RED, GOLD, WHITE_TEXT = { 1, .23, .19 }, { 1, .82, 0 }, { 1, 1, 1 }
local RANKS = {
    worldboss = { label = "Boss", glow = { 1, .82, .3 } },
    elite = { label = "Elite", glow = { 1, .82, .3 } },
    rareelite = { label = "Rare Elite", glow = { .8, .86, .95 } },
    rare = { label = "Rare", glow = { .8, .86, .95 } },
}

S:Register({ id = "tooltip.prettyUnits", name = "Pretty NPC and player tooltips", category = "Tooltips",
    group = "Style", type = "toggle", default = true, keywords = "unit creature player portrait frame pretty",
    desc = "A portrait, a header in reaction or class colour and the health bar inside the frame. "
        .. "Hold Alt for GearScore, NPC abilities and creature IDs.",
    apply = function() pretty.Refresh() end })

------------------------------------------------------------------------------------------------- the unit --
local function colorCode(color)
    return string.format("|cff%02x%02x%02x",
        math.floor(color[1] * 255 + .5), math.floor(color[2] * 255 + .5), math.floor(color[3] * 255 + .5))
end

local function classColor(unit)
    local _, class = UnitClass(unit)
    local colors = CUSTOM_CLASS_COLORS or RAID_CLASS_COLORS
    local color = class and colors and colors[class]
    if color then return { color.r, color.g, color.b } end
end

-- How the unit feels about the player: hostile, neutral or friendly. Grey when someone else tagged it.
local function reactionColor(unit)
    if UnitIsTapped(unit) and not UnitIsTappedByPlayer(unit) then return { .6, .6, .6 } end
    local reaction = UnitReaction(unit, "player") or 4
    if reaction >= 5 then return { .12, .82, .12 } end
    if reaction == 4 then return { 1, .82, 0 } end
    return { 1, .23, .19 }
end

local function levelColor(unit, level)
    if level <= 0 then return RED end
    if not UnitCanAttack("player", unit) then return GOLD end
    local difficulty = GetQuestDifficultyColor or GetDifficultyColor
    local color = difficulty and difficulty(level)
    if color then return { color.r, color.g, color.b } end
    return GOLD
end

-- The creature entry is inside the GUID of NPCs and vehicles.
local function creatureID(guid)
    local hex = guid and guid:match("^0[xX](%x+)$")
    if hex and #hex == 16 and hex:match("^[fF].[35]") then
        local id = tonumber(hex:sub(4, 10), 16)
        if id and id > 0 then return id end
    end
end

-- The arguments that are there, joined.
local function join(separator, ...)
    local result
    for index = 1, select("#", ...) do
        local part = select(index, ...)
        if part and part ~= "" then result = result and (result .. separator .. part) or part end
    end
    return result or ""
end

-- Everything asked of the unit. The last answer is kept: a tooltip fading away no longer has a unit to ask.
local function capture(frame, st)
    local _, unit = frame:GetUnit()
    if not (unit and UnitExists(unit)) then return st.info end
    local info = { unit = unit, guid = UnitGUID(unit), player = UnitIsPlayer(unit) and true or false }
    local level = UnitLevel(unit) or 0
    info.level = level > 0 and tostring(level) or "??"
    info.levelColor = levelColor(unit, level)
    local dead = UnitIsDead(unit) and (CORPSE or "Corpse") or nil
    if info.player then
        info.color = classColor(unit) or reactionColor(unit)
        info.guild = GetGuildInfo(unit)
        local race, class = UnitRace(unit), UnitClass(unit)
        info.kindText = join(SEPARATOR, join(" ", race, class), dead)
    else
        info.color = reactionColor(unit)
        local rank = RANKS[UnitClassification(unit) or ""]
        info.glow = rank and rank.glow
        -- Like the game, the creature type is only named for units that are not friendly.
        local creatureType
        if (UnitReaction("player", unit) or 4) < 5 then creatureType = UnitCreatureType(unit) end
        if creatureType == "Not specified" then creatureType = nil end
        info.creatureType, info.rank, info.dead = creatureType, rank and rank.label, dead
        info.id = creatureID(info.guid)
    end
    local targetUnit = unit .. "target"
    if UnitExists(targetUnit) then
        if UnitIsUnit(targetUnit, "player") then
            info.target, info.targetColor = YOU or "You", RED
        else
            info.target = UnitName(targetUnit)
            info.targetColor = UnitIsPlayer(targetUnit) and classColor(targetUnit) or reactionColor(targetUnit)
        end
    end
    st.info = info
    return info
end

------------------------------------------------------------------------------------------------- reading --
local function isWhite(fontString)
    local r, g, b = fontString:GetTextColor()
    return r > .95 and g > .95 and b > .95
end

-- White lines the game can put right after the level line that are not the NPC's faction.
local function otherStatus(text)
    if text:find("^%-") or text:find("%", 1, true) or text:find(":", 1, true) then return true end
    for _, key in ipairs({ "PVP_RANK_LEADER", "RESURRECTABLE", "UNIT_SKINNABLE_LEATHER", "UNIT_SKINNABLE_HERB",
        "UNIT_SKINNABLE_ROCK", "UNIT_SKINNABLE_BOLTS" }) do
        if _G[key] == text then return true end
    end
    return false
end

local function levelPattern()
    local prefix = (TOOLTIP_UNIT_LEVEL or "Level %s"):match("^(.-)%%s") or "Level "
    return "^" .. prefix:gsub("[%^%$%(%)%.%[%]%*%+%-%?%%]", "%%%0") .. "[%d%?]"
end

local function read(frame, info, u)
    local name = frame:GetName()
    local inset = kit.lineIcons(frame, u)

    local model = { lines = {}, roles = {}, body = {}, abilities = {}, hidden = {} }
    local pattern = levelPattern()
    local levelIndex
    local function drop(line) model.hidden[#model.hidden + 1] = line end

    for index = 1, frame:NumLines() do
        local left, right = _G[name .. "TextLeft" .. index], _G[name .. "TextRight" .. index]
        local line = { left = left, right = right, inset = inset[left] or 0,
            hasRight = right:IsShown() and plain(right) ~= "" }
        line.text = plain(left)
        model.lines[index] = line
        local text, raw = line.text, left:GetText() or ""

        if index == 1 then
            model.title = line
        elseif not levelIndex and text:match(pattern) and not line.hasRight then
            levelIndex = index
            drop(line)
        elseif not levelIndex then
            -- Between the name and the level: the guild, an NPC's role or the pet's owner.
            if info.guild and text == info.guild then
                drop(line)
            elseif text == "" then
                drop(line)
            else
                model.roles[#model.roles + 1] = line
            end
        elseif text == (PVP_ENABLED or "PvP") and not line.hasRight then
            model.pvp = true
            drop(line)
        elseif text:match("^GearScore: %d+") then
            model.score = line
            drop(line)
        elseif text:match("^YourScore: %d+") then
            model.yours = line
            drop(line)
        elseif text == "NPC ID:" or text == "NPC DisplayID:" or text == "NPC ID" or text:match("^ID: %d+$")
            or text:match("Abilities:%s*$") then
            -- IDs get their own row while Alt is held; the ability label is not needed.
            drop(line)
        elseif text == "Hold Alt to show abilities" then
            -- MobSpells adds this only for creatures it has abilities for.
            model.abilityHint = line
            drop(line)
        elseif raw:find("^|T") then
            model.abilities[#model.abilities + 1] = line
        elseif text:find("view creature loot", 1, true) then
            model.hint = line
            drop(line)
        elseif text == "" and not line.hasRight then
            drop(line)
            model.body[#model.body + 1] = { gap = true }
        elseif not info.player and not model.faction and index == levelIndex + 1 and not line.hasRight
            and isWhite(left) and not raw:find("|c", 1, true) and not otherStatus(text) then
            -- Drawn in our own text: the line keeps its color, which is how it is recognised next time.
            model.faction = line.text
            drop(line)
        else
            model.body[#model.body + 1] = line
        end
    end
    -- Without a level line this is not a layout we know.
    if not levelIndex then return end
    return model
end

-------------------------------------------------------------------------------------------------- layout --
local TEXTS = { "badge", "guild", "type", "kind", "pvp", "targetLabel", "targetName", "score", "average",
    "yours", "difference", "abilityHint", "hint", "creatureID", "displayID" }

local function build(frame, st)
    if st.unitArt then return st.unitArt end
    local art = {}
    st.unitArt = art
    for _, key in ipairs(TEXTS) do art[key] = kit.fontString(frame) end
    -- The portrait has its own layer, so it is always above its dark backing.
    art.portraitBack = texture(frame, "ARTWORK", WHITE)
    art.portrait = frame:CreateTexture(nil, "OVERLAY")
    art.portraitOutline = kit.newOutline(frame)
    art.badgeFill = texture(frame, "ARTWORK", WHITE)
    art.badgeOutline = kit.newOutline(frame)
    art.healthBack = texture(frame, "ARTWORK", WHITE)
    art.dividers = { kit.newDivider(frame), kit.newDivider(frame) }
    return art
end

local function hideArt(art)
    for _, key in ipairs(TEXTS) do art[key]:Hide() end
    for _, key in ipairs({ "portraitBack", "portrait", "badgeFill", "healthBack" }) do art[key]:Hide() end
    setSlice(art.portraitOutline, false)
    setSlice(art.badgeOutline, false)
    for _, divider in ipairs(art.dividers) do kit.hideDivider(divider) end
end

local function restoreHealth(st)
    local saved = st.health
    if not saved then return end
    st.health = nil
    local bar = saved.bar
    bar:ClearAllPoints()
    for _, point in ipairs(saved.points) do bar:SetPoint(unpack(point, 1, 5)) end
    bar:SetHeight(saved.height)
end

-- The game's own health bar, moved to the bottom of the frame. Returns the y below it.
local function placeHealth(frame, st, art, x, y, width, u)
    local bar = GameTooltipStatusBar
    if not (bar and bar:IsShown()) then return y end
    if not st.health then
        local points = {}
        for index = 1, bar:GetNumPoints() do points[index] = { bar:GetPoint(index) } end
        st.health = { bar = bar, points = points, height = bar:GetHeight() }
    end
    y = y + u(5)
    local barHeight, edge = u(7), u(1)
    art.healthBack:SetVertexColor(0, 0, 0, .9)
    art.healthBack:ClearAllPoints()
    art.healthBack:SetPoint("TOPLEFT", frame, "TOPLEFT", x, -y)
    art.healthBack:SetWidth(width)
    art.healthBack:SetHeight(barHeight + 2 * edge)
    art.healthBack:Show()
    bar:ClearAllPoints()
    bar:SetPoint("TOPLEFT", frame, "TOPLEFT", x + edge, -(y + edge))
    bar:SetWidth(width - 2 * edge)
    bar:SetHeight(barHeight)
    return y + barHeight + 2 * edge
end

local function style(frame, st)
    local info = capture(frame, st)
    if not info then return false end

    local frameArt = kit.prepare(frame, st, "unit")
    local art = build(frame, st)
    hideArt(art)
    local name = frame:GetName()
    local u = kit.metrics(frame)
    local alt = IsAltKeyDown()
    local model = read(frame, info, u)
    if not model then return false end
    local color = info.color
    local r, g, b = color[1], color[2], color[3]
    kit.boldTitle(st, model.title.left)
    model.title.left:SetTextColor(r, g, b)

    local fontFile, fontSize, fontFlags = _G[name .. "TextLeft2"]:GetFont()
    local small = math.max(8, (fontSize or 12) - 1)
    local large = { guild = true, type = true, kind = true, pvp = true, targetLabel = true, targetName = true,
        score = true, average = true, yours = true, difference = true }
    for _, key in ipairs(TEXTS) do
        if fontFile then art[key]:SetFont(fontFile, large[key] and fontSize or small, fontFlags or "") end
    end
    local function write(key, text, textColor)
        art[key]:SetText(text)
        art[key]:SetTextColor(textColor[1], textColor[2], textColor[3])
        return art[key]
    end
    -- Our own text for a pair of columns, from another addon's line. Returns the width it needs.
    local gap = u(24)
    local function pair(leftKey, leftText, rightKey, rightText, textColor, rightColor)
        local width = write(leftKey, leftText, textColor):GetStringWidth()
        if rightText then width = width + gap + write(rightKey, rightText, rightColor):GetStringWidth() end
        return width
    end

    for _, line in ipairs(model.lines) do
        line.left:SetAlpha(1)
        line.right:SetAlpha(1)
        line.left:SetWidth(0)
    end

    local tinted = { r + (1 - r) * .55, g + (1 - g) * .55, b + (1 - b) * .55 }
    write("badge", "Lvl " .. info.level, info.levelColor)
    local portraitSize = math.max(u(44), art.badge:GetStringWidth() + u(8))
    local textOffset = portraitSize + u(12)

    local content = u(232)
    local function need(value) content = math.max(content, value) end
    local function width(line)
        local value = line.inset + line.left:GetStringWidth()
        if line.hasRight then value = value + gap + line.right:GetStringWidth() end
        return value
    end
    need(textOffset + model.title.left:GetStringWidth() + u(4))
    for _, line in ipairs(model.roles) do
        line.left:SetTextColor(tinted[1], tinted[2], tinted[3])
        need(textOffset + width(line))
    end

    -- Guild for players; creature type and rank for NPCs.
    local guildText = info.player and info.guild and ("<" .. info.guild .. ">") or nil
    if guildText then need(textOffset + write("guild", guildText, tinted):GetStringWidth()) end
    local typeText
    if not info.player then
        local rank = info.rank and (colorCode(GOLD) .. info.rank .. "|r")
        typeText = join(SEPARATOR, info.creatureType, rank, info.dead)
        if typeText == "" then typeText = nil end
        if typeText then need(textOffset + write("type", typeText, tinted):GetStringWidth()) end
    end

    -- The last header row: faction, or race and class, with PvP at the right.
    local rowWidth, rowText = 0, nil
    if info.player then
        rowText = info.kindText ~= "" and info.kindText or nil
    else
        rowText = model.faction
    end
    if rowText then rowWidth = write("kind", rowText, SOFT):GetStringWidth() end
    if model.pvp then
        rowWidth = rowWidth + gap + write("pvp", PVP_ENABLED or "PvP", ORANGE):GetStringWidth()
    end
    if rowWidth > 0 then need(textOffset + rowWidth) end

    if info.target then
        need(pair("targetLabel", "Targeting", "targetName", info.target, GREY, info.targetColor or WHITE_TEXT))
    end
    local wrapLimit = u(300)
    for _, line in ipairs(model.body) do
        if not line.gap and (line.hasRight or width(line) <= wrapLimit) then need(width(line)) end
    end
    if alt then
        for _, line in ipairs(model.abilities) do
            if line.hasRight or width(line) <= wrapLimit then need(width(line)) end
        end
    end

    -- GearScoreLite's lines, in two short columns.
    local score, yours = alt and model.score, alt and model.yours
    local function scoreColor(line)
        local sr, sg, sb = line.left:GetTextColor()
        return { sr, sg, sb }
    end
    local average, difference
    if score then
        average = score.hasRight and plain(score.right):match("iLevel:?%s*(%d+)")
        need(pair("score", "GearScore " .. (score.text:match("%d+") or ""), "average",
            average and ("iLvl " .. average), scoreColor(score), SOFT))
    end
    if yours then
        difference = yours.hasRight and plain(yours.right):gsub("[%(%)]", "")
        local yourColor = scoreColor(yours)
        need(pair("yours", "Yours " .. (yours.text:match("%d+") or ""), "difference", difference or nil,
            yourColor, yourColor))
    end

    local abilityHint = not alt and model.abilityHint
    if abilityHint then need(write("abilityHint", abilityHint.text, GREY):GetStringWidth()) end
    if model.hint then need(write("hint", model.hint.text, GREY):GetStringWidth()) end
    local displayID
    if alt and info.id then
        displayID = IdTipCreatureDisplayIDs and IdTipCreatureDisplayIDs[info.id]
        need(pair("creatureID", "Creature ID " .. info.id, "displayID",
            displayID and ("Display ID " .. displayID), GREY, GREY))
    end

    local pad = u(16)
    local place, hide, row = kit.lineTools(frame, pad, content, u)
    for _, line in ipairs(model.hidden) do hide(line) end
    local function put(key, x, y)
        art[key]:ClearAllPoints()
        art[key]:SetPoint("TOPLEFT", frame, "TOPLEFT", x, -y)
        art[key]:Show()
        return art[key]:GetHeight()
    end
    local function putRight(key, y)
        art[key]:ClearAllPoints()
        art[key]:SetPoint("TOPRIGHT", frame, "TOPLEFT", pad + content, -y)
        art[key]:Show()
    end

    -- Header: the portrait with the level under it.
    local top, edge = u(14), u(2)
    art.portraitBack:SetVertexColor(.04, .05, .07, 1)
    art.portraitBack:ClearAllPoints()
    art.portraitBack:SetPoint("TOPLEFT", frame, "TOPLEFT", pad + edge, -(top + edge))
    art.portraitBack:SetWidth(portraitSize - 2 * edge)
    art.portraitBack:SetHeight(portraitSize - 2 * edge)
    art.portraitBack:Show()
    -- Drawing a portrait costs the client work, so only do it for a new unit.
    if st.portraitGUID ~= info.guid and UnitExists(info.unit) then
        st.portraitGUID = info.guid
        SetPortraitTexture(art.portrait, info.unit)
    end
    art.portrait:ClearAllPoints()
    art.portrait:SetPoint("TOPLEFT", frame, "TOPLEFT", pad + edge + u(1), -(top + edge + u(1)))
    art.portrait:SetWidth(portraitSize - 2 * edge - u(2))
    art.portrait:SetHeight(portraitSize - 2 * edge - u(2))
    art.portrait:Show()
    kit.placeOutline(frame, art.portraitOutline, pad, top, portraitSize, portraitSize, edge, r, g, b)

    local boxTop, boxHeight = top + portraitSize + u(4), art.badge:GetHeight() + u(6)
    art.badgeFill:SetVertexColor(.03 + r * .07, .035 + g * .07, .05 + b * .07, 1)
    art.badgeFill:ClearAllPoints()
    art.badgeFill:SetPoint("TOPLEFT", frame, "TOPLEFT", pad + edge, -(boxTop + edge))
    art.badgeFill:SetWidth(portraitSize - 2 * edge)
    art.badgeFill:SetHeight(boxHeight - 2 * edge)
    art.badgeFill:Show()
    kit.placeOutline(frame, art.badgeOutline, pad, boxTop, portraitSize, boxHeight, edge, r, g, b)
    art.badge:ClearAllPoints()
    art.badge:SetPoint("CENTER", art.badgeFill, "CENTER", 0, 0)
    art.badge:Show()

    -- Beside them.
    local textX = pad + textOffset
    place(model.title, textX, top + u(2))
    local textY = top + u(2) + height(model.title) + u(3)
    for _, line in ipairs(model.roles) do
        place(line, textX, textY)
        textY = textY + height(line) + u(1)
    end
    if guildText then textY = textY + put("guild", textX, textY) + u(1) end
    if typeText then textY = textY + put("type", textX, textY) + u(1) end
    if rowWidth > 0 then
        textY = textY + u(4)
        local lineHeight = rowText and put("kind", textX, textY) or 0
        if model.pvp then
            putRight("pvp", textY)
            lineHeight = math.max(lineHeight, art.pvp:GetHeight())
        end
        textY = textY + lineHeight
    end
    local headerBottom = math.max(boxTop + boxHeight, textY) + u(9)
    local y = headerBottom + u(6)
    local start = y

    if info.target then
        putRight("targetName", y)
        y = y + put("targetLabel", pad, y) + u(3)
    end

    -- Quests and everything else, then what Alt adds, each behind its own divider.
    local used = 0
    local function section()
        used = used + 1
        y = kit.drawDivider(frame, art.dividers[used], pad, y, content, u)
    end
    local hasBody = false
    for _, line in ipairs(model.body) do
        if not line.gap then hasBody = true end
    end
    if hasBody then
        section()
        y = kit.bodyRows(model.body, y, row, u)
    end
    if model.abilities[1] then
        if alt then
            section()
            for _, line in ipairs(model.abilities) do y = row(line, y) end
        else
            for _, line in ipairs(model.abilities) do hide(line) end
        end
    end
    if score or yours then
        if used < #art.dividers then section() end
        if score then
            if average then putRight("average", y) end
            y = y + put("score", pad, y) + u(3)
        end
        if yours then
            if difference then putRight("difference", y) end
            y = y + put("yours", pad, y) + u(3)
        end
    end

    if abilityHint then
        y = y + u(3)
        y = y + put("abilityHint", pad, y)
    end
    if model.hint then
        y = y + u(3)
        y = y + put("hint", pad, y)
    end
    if alt and info.id then
        y = y + u(5)
        if displayID then putRight("displayID", y) end
        y = y + put("creatureID", pad, y)
    end

    local drawn = y > start
    y = placeHealth(frame, st, art, pad, y, content, u)
    drawn = drawn or y > start

    frame:SetWidth(content + 2 * pad)
    frame:SetHeight(drawn and y + u(12) or headerBottom + u(3))
    kit.decorate(frame, frameArt, u, headerBottom, color, info.glow, OPACITY)
    return true
end

pretty.kinds.unit = {
    option = "tooltip.prettyUnits",
    style = style,
    restore = function(_, st)
        if st.unitArt then hideArt(st.unitArt) end
        restoreHealth(st)
    end,
}

tip:HookScript("OnTooltipSetUnit", function(self)
    local st = pretty.states[self]
    st.kind, st.info = "unit", nil
end)
tip:HookScript("OnTooltipCleared", function(self) pretty.states[self].portraitGUID = nil end)

-- The target can change while the tooltip stays up.
local waited = 0
tip:HookScript("OnUpdate", function(self, elapsed)
    local st = pretty.states[self]
    local info = st.info
    if st.active ~= "unit" or not info then return end
    waited = waited + (elapsed or 0)
    if waited < .2 then return end
    waited = 0
    local unit = info.unit
    if not (UnitExists(unit) and UnitGUID(unit) == info.guid) then return end
    local targetUnit, target = unit .. "target", nil
    if UnitExists(targetUnit) then
        target = UnitIsUnit(targetUnit, "player") and (YOU or "You") or UnitName(targetUnit)
    end
    if target ~= info.target then pretty.Layout(self) end
end)
