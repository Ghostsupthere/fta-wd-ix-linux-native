local _, addon = ...
local S, tip = addon.Settings, GameTooltip
if not tip then return end

local prefix = "tooltip."
local comparisons = { ShoppingTooltip1, ShoppingTooltip2, ShoppingTooltip3 }
local ownerAnchor, defaultOwner, stock, lastX, lastY
local originalAlpha, idLine, idKind, idValue
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
register("y", "Vertical offset", "Placement", "slider", 0, {min = -300, max = 300, step = 1})
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
    stock, lastX, lastY = nil, nil, nil
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
    local shift = mode == "cursor" and width * .5 or (mode == "left" or mode == "fixed") and width or 0
    local x = math.max(0, math.min(screenW - width, cursorX - shift + value("x") * screenScale))
    local y = math.max(0, math.min(screenH - height, cursorY + value("y") * screenScale))
    x, y = x / scale, y / scale
    if x ~= lastX or y ~= lastY then
        tip:ClearAllPoints()
        tip:SetPoint("BOTTOMLEFT", UIParent, "BOTTOMLEFT", x, y)
        lastX, lastY = x, y
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
    defaultOwner, stock, lastX, lastY = nil, nil, nil, nil
    originalAlpha, idLine, idKind, idValue = nil, nil, nil, nil
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
    defaultOwner = nil
end)
for _, frame in pairs(comparisons) do frame:HookScript("OnShow", updateFonts) end
controls:SetScript("OnUpdate", function()
    local mode = value("anchor")
    if stock and mode and mode ~= "stock" then place(mode) end
end)
controls:RegisterEvent("MODIFIER_STATE_CHANGED")
controls:RegisterEvent("PLAYER_REGEN_DISABLED")
controls:RegisterEvent("PLAYER_REGEN_ENABLED")
controls:SetScript("OnEvent", sync)
