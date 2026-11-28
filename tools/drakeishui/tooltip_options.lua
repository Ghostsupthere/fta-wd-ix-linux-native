local _, addon = ...
local S, tip = addon.Settings, GameTooltip
if not tip then return end

local prefix = "tooltip."
local comparisons = { ShoppingTooltip1, ShoppingTooltip2, ShoppingTooltip3 }
local ownerAnchor, defaultOwner, stock, lastPoint, lastX, lastY
local settledOwner, settledHeight
local originalAlpha, idLine, idKind, idValue
local worldUnit, lastAlpha
local refresh
local controls = CreateFrame("Frame")
controls:Hide()

local function value(key)
    return S:Get(prefix .. key)
end

local function register(key, name, group, kind, default, extra)
    local option = extra or {}
    option.id, option.name, option.category, option.group = prefix .. key, name, "Tooltips", group
    option.type, option.default = kind, default
    option.apply = function() refresh() end
    if option.choices then
        local allowed = {}
        for _, choice in ipairs(option.choices) do allowed[choice.value] = true end
        option.validate = function(input) if allowed[input] then return input end end
    end
    S:Register(option)
end

local function choices(...)
    local args, result = {...}, {}
    for i = 1, #args, 2 do result[#result + 1] = {value = args[i], name = args[i + 1]} end
    return result
end

register("anchor", "Placement", "Placement", "dropdown", "stock", {choices = choices(
    "stock", "Stock", "cursor", "Cursor", "left", "Cursor left", "right", "Cursor right", "fixed", "Fixed")})
register("x", "Horizontal offset", "Placement", "slider", 0, {min = -300, max = 300, step = 1})
register("y", "Vertical offset", "Placement", "slider", 0, {min = -300, max = 300, step = 1,
    desc = "In the cursor modes, a negative value is the gap from the cursor down to the top of the tooltip."})
register("instant", "Hide tooltips instantly (no fade)", "Visibility", "toggle", false,
    {keywords = "fade fading slow linger disappear"})
register("combat", "Hide in combat", "Visibility", "toggle", false)
register("modifier", "Show hidden tips with", "Visibility", "dropdown", "shift", {choices = choices(
    "none", "None", "shift", "Shift", "ctrl", "Control", "alt", "Alt")})
register("actions", "Show action bar tooltips", "Visibility", "toggle", true)
register("bags", "Show bags and bank tooltips", "Visibility", "toggle", true)
register("units", "Show unit frame tooltips", "Visibility", "toggle", true)
register("spellID", "Spell and item IDs", "Details", "toggle", false)
register("npcID", "Creature IDs", "Details", "toggle", false)
for _, size in ipairs({{"header", "Header"}, {"body", "Body"}, {"comparison", "Comparison"}}) do
    register(size[1], size[2] .. " font size", "Text Sizes", "slider", 0, {
        min = 0, max = 32, step = 1, desc = "0 uses the existing tooltip text style.",
        validate = function(num)
            if type(num) ~= "number" or num ~= num or math.abs(num) == math.huge then return end
            return num == 0 and 0 or math.min(32, math.max(8, math.floor(num + .5)))
        end,
    })
end

local actionPrefixes = {
    "ActionButton", "BonusActionButton", "MultiBarBottomLeftButton", "MultiBarBottomRightButton",
    "MultiBarRightButton", "MultiBarLeftButton", "PetActionButton", "ShapeshiftButton",
    "MultiCastActionButton", "VehicleMenuBarActionButton", "PossessButton",
}

local function context(frame)
    for _ = 1, 8 do
        if not frame or frame == UIParent or frame == WorldFrame then return end
        local name = frame.GetName and frame:GetName() or ""
        if frame.bagID and frame.slotID or name:match("^ContainerFrame%d+Item")
            or name:match("^BankFrameItem") or name:match("^GuildBankColumn%d+Button")
            or frame == DrakeishUICombinedBags then
            return "bags"
        end
        for _, prefix in ipairs(actionPrefixes) do
            if name:match("^" .. prefix .. "%d+$") then return "actions" end
        end
        if name == "MultiCastSummonSpellButton" or name == "MultiCastRecallSpellButton" then return "actions" end
        if frame == PlayerFrame or frame == TargetFrame or frame == TargetFrameToT or frame == FocusFrame
            or frame == FocusFrameToT or frame == PetFrame or name:match("^PartyMemberFrame%d")
            or name:match("^CustomPartyFrame%d") or name:match("^DrakeishUI_RaidUnit")
            or name:match("^Boss%d+TargetFrame") or name:match("^ArenaEnemyFrame%d") then
            return "units"
        end
        frame = frame.GetParent and frame:GetParent()
    end
end

local function savePlacement()
    local points = {}
    for i = 1, tip:GetNumPoints() do points[i] = {tip:GetPoint(i)} end
    local anchor = tip:GetAnchorType()
    local offsets = ownerAnchor and ownerAnchor[1] == anchor and ownerAnchor
    stock = {anchor = anchor, points = points, x = offsets and offsets[2], y = offsets and offsets[3]}
    -- PRESERVE stops native repositioning without changing the owner or contents.
    tip:SetAnchorType("ANCHOR_PRESERVE")
end

local function restorePlacement()
    if not stock then return end
    local saved = stock
    stock, lastPoint, lastX, lastY = nil, nil, nil, nil
    tip:SetAnchorType(saved.anchor, saved.x, saved.y)
    tip:ClearAllPoints()
    for _, point in ipairs(saved.points) do tip:SetPoint(unpack(point, 1, 5)) end
end

local function place(mode)
    if not stock then savePlacement() end
    local scale = tip:GetEffectiveScale()
    local screenScale = UIParent:GetEffectiveScale()
    local screenW, screenH = UIParent:GetWidth() * screenScale, UIParent:GetHeight() * screenScale
    local cursorX, cursorY = screenW, 0
    if mode ~= "fixed" then cursorX, cursorY = GetCursorPosition() end
    local width, height = tip:GetWidth() * scale, tip:GetHeight() * scale
    -- Attach the corner or edge that faces the cursor. Refreshed tooltips are
    -- shown before other addons finish adding lines, so the position must not
    -- depend on the size, or it jumps for a frame on every refresh.
    local horizontal = mode == "cursor" and "" or mode == "right" and "LEFT" or "RIGHT"
    local shift = mode == "cursor" and width * .5 or mode == "right" and 0 or width
    local x = cursorX + value("x") * screenScale
    x = math.max(shift, math.min(screenW - width + shift, x))
    local vertical = "BOTTOM"
    local offsetY = value("y") * screenScale
    local y = cursorY + offsetY
    if mode ~= "fixed" and offsetY < 0 then
        -- Decide the side using the finished height from the last refresh.
        local needed = height
        if settledHeight and settledOwner == tip:GetOwner() then needed = math.max(height, settledHeight) end
        if y - needed >= 0 then
            vertical, y = "TOP", math.min(screenH, y)
        else
            -- No room underneath: use the same gap above instead.
            y = cursorY - offsetY
        end
    end
    if vertical == "BOTTOM" then y = math.max(0, math.min(screenH - height, y)) end
    x, y = x / scale, y / scale
    local point = vertical .. horizontal
    if point ~= lastPoint or x ~= lastX or y ~= lastY then
        tip:ClearAllPoints()
        tip:SetPoint(point, UIParent, "BOTTOMLEFT", x, y)
        lastPoint, lastX, lastY = point, x, y
    end
end

local modifiers = {shift = IsShiftKeyDown, ctrl = IsControlKeyDown, alt = IsAltKeyDown}
local function sync()
    if not S:IsReady() or not tip:IsShown() then return end
    local liveOwner = tip:GetOwner()
    local fromDefault = liveOwner ~= nil and liveOwner == defaultOwner
    local kind = context(liveOwner)
    local eligible = kind or fromDefault
    local override = modifiers[value("modifier")]
    local hidden = eligible and ((kind and not value(kind)) or (value("combat") and InCombatLockdown()))
        and not (override and override())
    if hidden then
        if originalAlpha == nil then originalAlpha = tip:GetAlpha() end
        tip:SetAlpha(0)
    elseif originalAlpha ~= nil then
        tip:SetAlpha(originalAlpha)
        originalAlpha = nil
    end

    local mode = value("anchor")
    if eligible and mode and mode ~= "stock" then
        place(mode)
        if mode ~= "fixed" then controls:Show() else controls:Hide() end
    else
        restorePlacement()
        controls:Hide()
    end
end

local function addID(id, kind)
    if not id or id <= 0 then return end
    if idLine and idValue == id and idKind == kind then idLine:Show(); return end
    local name = tip:GetName()
    for i = 1, tip:NumLines() do
        local line = _G[name .. "TextLeft" .. i]
        local text = line and line:IsShown() and line:GetText()
        if text then
            text = text:gsub("|c%x%x%x%x%x%x%x%x", ""):gsub("|r", "")
            if text:lower():match("id%s*[:#]?%s*" .. id .. "%f[%D]") then return end
        end
    end
    tip:AddLine("ID: " .. id, .8, .8, .8)
    idLine, idKind, idValue = _G[name .. "TextLeft" .. tip:NumLines()], kind, id
end

local function itemID(frame)
    if not value("spellID") then return end
    local _, link = frame:GetItem()
    addID(tonumber(link and link:match("item:(%d+)")), "spellID")
end

local function spellID(frame)
    if value("spellID") then addID(select(3, frame:GetSpell()), "spellID") end
end

local function npcID(frame)
    if not value("npcID") then return end
    local _, unit = frame:GetUnit()
    local guid = unit and UnitGUID(unit)
    -- Match the creature/vehicle high-type mask in Script_UnitGUID and read bits 24–51.
    local hex = guid and guid:match("^0[xX]([%x]+)$")
    if hex and #hex == 16 and hex:match("^[fF].[35]") then
        addID(tonumber(hex:sub(4, 10), 16), "npcID")
    end
end

local function updateFonts(frame)
    if addon.Appearance and addon.Appearance.StyleTooltip then addon.Appearance.StyleTooltip(frame) end
end

refresh = function()
    if not tip:IsShown() then return end
    local before = tip:NumLines()
    local wasShown = idLine and idLine:IsShown()
    if idLine and not value(idKind) then idLine:Hide() end
    if tip:GetItem() then itemID(tip)
    elseif tip:GetSpell() then spellID(tip)
    else npcID(tip) end
    updateFonts(tip)
    if before ~= tip:NumLines() or (idLine and wasShown ~= idLine:IsShown()) then tip:Show() end
    for _, frame in pairs(comparisons) do if frame:IsShown() then updateFonts(frame) end end
    sync()
end

hooksecurefunc(tip, "SetOwner", function(_, _, anchor, x, y)
    defaultOwner, stock, lastPoint, lastX, lastY = nil, nil, nil, nil, nil
    originalAlpha, idLine, idKind, idValue = nil, nil, nil, nil
    worldUnit, lastAlpha = nil, nil
    -- Native SetOwner clears content, so a new owner takes precedence over
    -- the previous snapshot even when it happens to use the same frame.
    ownerAnchor = {anchor or "ANCHOR_LEFT", x, y}
end)

if GameTooltip_SetDefaultAnchor then
    hooksecurefunc("GameTooltip_SetDefaultAnchor", function(frame, parent)
        if frame == tip then defaultOwner = parent end
    end)
end

tip:HookScript("OnTooltipCleared", function() idLine, idKind, idValue = nil, nil, nil end)
tip:HookScript("OnTooltipSetItem", itemID)
tip:HookScript("OnTooltipSetSpell", spellID)
tip:HookScript("OnTooltipSetUnit", npcID)
tip:HookScript("OnShow", sync)
tip:HookScript("OnHide", function()
    controls:Hide()
    if originalAlpha ~= nil then tip:SetAlpha(originalAlpha); originalAlpha = nil end
    restorePlacement()
    defaultOwner, worldUnit, lastAlpha = nil, nil, nil
end)

local function instant()
    return S:IsReady() and value("instant")
end
-- Unit frames and Lua code fade through this method.
if tip.FadeOut then
    hooksecurefunc(tip, "FadeOut", function() if instant() then tip:Hide() end end)
end
-- The client fades world tooltips natively, without calling the Lua method. Newer clients say when that
-- starts, so world objects can go as fast as creatures.
if tip.HasScript and tip:HasScript("OnTooltipFadeOut") then
    tip:HookScript("OnTooltipFadeOut", function(self) if instant() then self:Hide() end end)
end
-- Without that script:
tip:HookScript("OnTooltipSetUnit", function()
    worldUnit = GetMouseFocus() == WorldFrame and UnitExists("mouseover") and true or nil
    lastAlpha = nil
end)
tip:HookScript("OnUpdate", function()
    if not instant() or originalAlpha ~= nil then lastAlpha = nil; return end
    if GetMouseFocus() ~= WorldFrame then lastAlpha = nil; return end
    -- Creatures: the mouseover unit goes away as soon as the cursor leaves.
    -- World objects have no unit, so catch the first falling alpha instead.
    local alpha = tip:GetAlpha()
    if (worldUnit and not UnitExists("mouseover"))
        or (defaultOwner and lastAlpha and alpha < lastAlpha) then
        tip:Hide()
    else
        lastAlpha = alpha
    end
end)
for _, frame in pairs(comparisons) do frame:HookScript("OnShow", updateFonts) end
local function reposition()
    local mode = value("anchor")
    if stock and mode and mode ~= "stock" then place(mode); return true end
end
-- Screen edge limits depend on the size, so apply them again when it changes.
tip:HookScript("OnSizeChanged", function() if S:IsReady() then reposition() end end)
controls:SetScript("OnUpdate", function()
    -- By the next frame every addon has added its lines.
    if tip:IsShown() then settledOwner, settledHeight = tip:GetOwner(), tip:GetHeight() * tip:GetEffectiveScale() end
    reposition()
end)
controls:RegisterEvent("MODIFIER_STATE_CHANGED")
controls:RegisterEvent("PLAYER_REGEN_DISABLED")
controls:RegisterEvent("PLAYER_REGEN_ENABLED")
controls:SetScript("OnEvent", sync)
