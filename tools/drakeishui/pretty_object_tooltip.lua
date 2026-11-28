-- FTA addition: the "pretty" tooltip for world objects and corpses, drawn with the pieces in
-- pretty_tooltip.lua. The name and what the object needs form the header; quest lines go below a divider.
-- Needs the client's OnTooltipSetObject script.
local _, addon = ...
local S, pretty, tip = addon.Settings, addon.PrettyTooltip, GameTooltip
if not (pretty and pretty.states[tip] and tip:HasScript("OnTooltipSetObject")) then return end

local kit = pretty.kit
local plain, height = kit.plain, kit.height

local GOLD, GREY = { 1, .82, 0 }, { .6, .62, .66 }
local CORPSE_TITLE = { .86, .88, .92 }

S:Register({ id = "tooltip.prettyObjects", name = "Pretty world object tooltips", category = "Tooltips",
    group = "Style", type = "toggle", default = true, keywords = "object chest herb ore corpse frame pretty",
    desc = "The same frame for chests, herbs, ore, other world objects and corpses.",
    apply = function() pretty.Refresh() end })

local function build(frame, st)
    if not st.objectArt then
        st.objectArt = { objectID = kit.fontString(frame), divider = kit.newDivider(frame) }
    end
    return st.objectArt
end

local function hideArt(art)
    art.objectID:Hide()
    kit.hideDivider(art.divider)
end

-- The client says how many lines describe the object itself; anything after them is about quests.
local function read(frame, info, u)
    local name = frame:GetName()
    local inset = kit.lineIcons(frame, u)
    local model = { lines = {}, status = {}, body = {}, hidden = {} }
    for index = 1, frame:NumLines() do
        local left, right = _G[name .. "TextLeft" .. index], _G[name .. "TextRight" .. index]
        local line = { left = left, right = right, inset = inset[left] or 0,
            hasRight = right:IsShown() and plain(right) ~= "" }
        line.text = plain(left)
        model.lines[index] = line
        if index == 1 then
            model.title = line
        elseif line.text == "Object ID" and line.hasRight then
            -- Questie's optional ID row; like other IDs it shows while Alt is held.
            model.objectID = plain(right)
            model.hidden[#model.hidden + 1] = line
        elseif line.text == "" and not line.hasRight then
            model.hidden[#model.hidden + 1] = line
            if index > info.lines then model.body[#model.body + 1] = { gap = true } end
        elseif index <= info.lines then
            model.status[#model.status + 1] = line
        else
            model.body[#model.body + 1] = line
        end
    end
    return model
end

local function style(frame, st)
    local info = st.info
    if not info or frame:NumLines() < 1 then return false end

    local base = kit.prepare(frame, st, "object")
    local art = build(frame, st)
    hideArt(art)
    local name = frame:GetName()
    local u = kit.metrics(frame)
    local model = read(frame, info, u)
    local color = info.corpse and GREY or GOLD
    local titleColor = info.corpse and CORPSE_TITLE or GOLD
    kit.boldTitle(st, model.title.left)
    model.title.left:SetTextColor(titleColor[1], titleColor[2], titleColor[3])

    for _, line in ipairs(model.lines) do
        line.left:SetAlpha(1)
        line.right:SetAlpha(1)
        line.left:SetWidth(0)
    end

    -- Only as wide as its text: most objects are just a short name.
    local content, gap, wrapLimit = u(96), u(24), u(300)
    local function need(value) content = math.max(content, value) end
    local function measure(line)
        if line.gap then return end
        local value = line.inset + line.left:GetStringWidth()
        if line.hasRight then value = value + gap + line.right:GetStringWidth() end
        if line.hasRight or value <= wrapLimit then need(value) else need(wrapLimit) end
    end
    need(model.title.left:GetStringWidth() + u(4))
    for _, line in ipairs(model.status) do measure(line) end
    local hasBody = false
    for _, line in ipairs(model.body) do
        measure(line)
        if not line.gap then hasBody = true end
    end
    -- Room for the divider to look like one.
    if hasBody then need(u(176)) end

    local showID = model.objectID and IsAltKeyDown()
    if showID then
        local file, size, flags = _G[name .. "TextLeft1"]:GetFont()
        local second = _G[name .. "TextLeft2"]
        if second and second:GetFont() then file, size, flags = second:GetFont() end
        if file then art.objectID:SetFont(file, math.max(8, (size or 12) - 1), flags or "") end
        art.objectID:SetText("Object ID " .. model.objectID)
        art.objectID:SetTextColor(GREY[1], GREY[2], GREY[3])
        need(art.objectID:GetStringWidth())
    end

    local pad = u(16)
    local place, hide, row = kit.lineTools(frame, pad, content, u)
    for _, line in ipairs(model.hidden) do hide(line) end

    -- Header: the name, then locks and required skills in the game's colors.
    local top = u(12)
    place(model.title, pad, top)
    local y = top + height(model.title) + u(3)
    for _, line in ipairs(model.status) do y = row(line, y) end
    local headerBottom = y + u(5)
    y = headerBottom + u(6)
    local start = y

    if hasBody then
        y = kit.drawDivider(frame, art.divider, pad, y, content, u)
        y = kit.bodyRows(model.body, y, row, u)
    end
    if showID then
        y = y + u(5)
        art.objectID:ClearAllPoints()
        art.objectID:SetPoint("TOPLEFT", frame, "TOPLEFT", pad, -y)
        art.objectID:Show()
        y = y + art.objectID:GetHeight()
    end
    -- Only destructible buildings have a health bar.
    y = kit.placeHealth(frame, st, pad, y, content, u)

    local headerOnly = y == start
    frame:SetWidth(content + 2 * pad)
    frame:SetHeight(headerOnly and headerBottom + u(3) or y + u(12))
    kit.decorate(frame, base, u, headerBottom, color, nil, kit.WORLD_OPACITY)
    if headerOnly then base.body:Hide() end
    return true
end

pretty.kinds.object = {
    option = "tooltip.prettyObjects",
    style = style,
    restore = function(_, st)
        if st.objectArt then hideArt(st.objectArt) end
        kit.restoreHealth(st)
    end,
}

tip:HookScript("OnTooltipSetObject", function(self, lines, corpse)
    local st = pretty.states[self]
    st.kind = "object"
    st.info = { lines = tonumber(lines) or 1, corpse = corpse and true or false }
end)
