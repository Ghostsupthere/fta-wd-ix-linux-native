local _, addon = ...
local S, B = addon.Settings, addon.UnitBarSizes
local dimensionKeys = {"healthWidth", "healthHeight", "powerWidth", "powerHeight"}
local layouts = {
    {group="player", frame="PlayerFrame", portrait="PlayerPortrait", side="left", method="ChangePlayerframe"},
    {group="target", frame="TargetFrame", portrait="TargetFramePortrait", side="right", method="ChangeTargetFrame"},
    {group="targettarget", frame="TargetFrameToT", portrait="TargetFrameToTPortrait", side="left",
        method="ChangeToT", power="DragonflightUIToTManaBar", name="TargetFrameToTTextureFrameName"},
    {group="focus", frame="FocusFrame", portrait="FocusFramePortrait", side="right", method="ChangeFocusFrame"},
    {group="focustarget", frame="FocusFrameToT", portrait="FocusFrameToTPortrait", side="left",
        method="ChangeFocusToT", name="FocusFrameToTTextureFrameName"},
    {group="pet", frame="PetFrame", portrait="PetPortrait", side="left", method="ChangePetFrame", name="PetName"},
}
for i = 1, 5 do
    layouts[#layouts + 1] = {group="boss", frame="Boss" .. i .. "TargetFrame", side="right"}
    layouts[#layouts + 1] = {group="arena", frame="ArenaEnemyFrame" .. i, side="left"}
end

local function capture(region)
    local points = {}
    for i = 1, region:GetNumPoints() do
        local point, relative, relativePoint, x, y = region:GetPoint(i)
        if type(relative) == "number" then
            points[i] = {point, relative, relativePoint}
        else
            points[i] = {point, relative or UIParent, relativePoint, x, y}
        end
    end
    return {width = region:GetWidth(), height = region:GetHeight(), points = points}
end

local function restore(region, baseline)
    region:SetSize(baseline.width, baseline.height)
    region:ClearAllPoints()
    for _, point in ipairs(baseline.points) do region:SetPoint(unpack(point)) end
end

local function addEdge(entry, source, portrait, right)
    local left, rightEdge = source:GetLeft(), source:GetRight()
    local base = entry.portraitBase
    local cut
    if portrait then
        cut = base and (right and base.left or base.right) or right and portrait:GetLeft() or portrait:GetRight()
        if cut then cut = cut + (right and -4 or 4) end
    end
    local path = source:GetTexture()
    if not cut or not left or not rightEdge or rightEdge <= left or not path or path == "" then return end
    local width = rightEdge - left
    local split = math.min(1, math.max(0, (cut - left) / width))
    if split <= 0 or split >= 1 then return end
    local ulx, uly, llx, lly, urx, ury, lrx, lry = source:GetTexCoord()
    local function between(a, b) return a + (b - a) * split end
    local slice = entry.slices[source]
    if not slice then
        slice = source:GetParent():CreateTexture(nil, source:GetDrawLayer(), nil, 5)
        entry.slices[source] = slice
    end
    slice:SetTexture(path)
    slice:ClearAllPoints()
    -- A scaled portrait scales the baked ring with it, about the portrait centre.
    local scale = entry.scale or 1
    local scaled = scale ~= 1 and base and base.cx and source:GetTop()
    local ox, oy
    if scaled then
        ox = ((right and rightEdge or left) - (base.left + base.right) / 2) * scale
        oy = (source:GetTop() - (base.top + base.bottom) / 2) * scale
    end
    if right then
        slice:SetTexCoord(between(ulx, urx), between(uly, ury), between(llx, lrx), between(lly, lry), urx, ury, lrx, lry)
        if scaled then slice:SetPoint("TOPRIGHT", portrait, "CENTER", ox, oy)
        else slice:SetPoint("TOPRIGHT", source, "TOPRIGHT") end
        slice:SetSize(width * (1 - split) * (scaled and scale or 1), source:GetHeight() * (scaled and scale or 1))
    else
        slice:SetTexCoord(ulx, uly, llx, lly, between(ulx, urx), between(uly, ury), between(llx, lrx), between(lly, lry))
        if scaled then slice:SetPoint("TOPLEFT", portrait, "CENTER", ox, oy)
        else slice:SetPoint("TOPLEFT", source, "TOPLEFT") end
        slice:SetSize(width * split * (scaled and scale or 1), source:GetHeight() * (scaled and scale or 1))
    end
    slice:SetAlpha(entry.hidden[source] or source:GetAlpha())
    if source.GetVertexColor then slice:SetVertexColor(source:GetVertexColor()) end
    if source:IsShown() then slice:Show() else slice:Hide() end
    return slice
end

local function artSources(spec, frame)
    local result, seen = {}, {}
    local function add(texture)
        if texture and texture.GetTexture and not seen[texture] then
            result[#result + 1], seen[texture] = texture, true
        end
    end
    local f = spec.frame
    add(_G["DragonflightUI" .. f .. "Background"])
    add(_G["DragonflightUI" .. f .. "Border"])
    add(_G[f .. "NameBackground"])
    if not spec.method then
        add(_G[f .. "Texture"])
        add(_G[f .. "TextureFrameTexture"])
        add(frame.texture)
    end
    return result
end

local function enableArt(entry, spec, frame, health, power)
    if not entry.healthBacking then entry.healthBacking = B:CreateBacking(health) end
    if power and not entry.powerBacking then entry.powerBacking = B:CreateBacking(power) end
    entry.healthBacking:Show()
    if entry.powerBacking then entry.powerBacking:Show() end
    local portrait = _G[spec.portrait or spec.frame .. "Portrait"] or frame.portrait
    for _, source in ipairs(artSources(spec, frame)) do
        -- Keep the original-sized portrait side of the composite; the bars get
        -- neutral backings so the straight pieces can use independent lengths.
        local keep = addEdge(entry, source, portrait, spec.side == "right")
        if keep or not portrait or source == _G[spec.frame .. "NameBackground"] then
            if entry.hidden[source] == nil then entry.hidden[source] = source:GetAlpha() end
            source:SetAlpha(0)
        end
    end
end

local function resetArt(entry)
    if not entry.healthBacking then return end
    entry.healthBacking:Hide()
    if entry.powerBacking then entry.powerBacking:Hide() end
    for source, alpha in pairs(entry.hidden) do source:SetAlpha(alpha) end
    for _, slice in pairs(entry.slices) do slice:Hide() end
    wipe(entry.hidden)
end

local function findBars(f, frame)
    return _G[f .. "HealthBar"] or _G[f .. "Health"] or frame.healthbar or frame.healthBar or frame.HealthBar,
        _G[f .. "ManaBar"] or _G[f .. "PowerBar"] or _G[f .. "Mana"] or frame.manabar or frame.manaBar
            or frame.powerbar or frame.powerBar or frame.PowerBar
end

local function portraitScale(group)
    local value = S:Get("unit." .. group .. ".portraitScale")
    if type(value) ~= "number" or value <= 0 then return 1 end
    return value / 100
end

local function resize(spec, rebuilt)
    if not S:IsReady() or InCombatLockdown() then return end
    local f = spec.frame
    local frame = _G[f]
    if not frame then return end
    local health, power = findBars(f, frame)
    power = _G[spec.power or ""] or power
    if not health or not health:GetWidth() or health:GetWidth() <= 0 then return end

    local entry = spec.entry
    local requested = false
    for _, key in ipairs(dimensionKeys) do
        if (S:Get("unit." .. spec.group .. "." .. key) or -1) > 0 then requested = true; break end
    end
    local scale = portraitScale(spec.group)
    if scale ~= 1 then requested = true end
    if not requested and not (entry and entry.active) then return end
    if not entry then
        entry = {hidden = {}, slices = {}}
        spec.entry = entry
        frame:HookScript("OnShow", function() resize(spec) end)
    end
    if not entry.active or rebuilt then
        entry.health = capture(health)
        entry.power = power and capture(power)
        entry.primary = power
        if not entry.active then entry.insets = {frame:GetHitRectInsets()} end
        -- The portrait is captured before anything scales it; the ring slices and the
        -- scaled size both work from this baseline.
        local portrait = _G[spec.portrait or ""]
        entry.portraitMoved = nil
        entry.portraitBase = nil
        if portrait and portrait.GetCenter and portrait.GetWidth then
            local pl, pr, pt, pb = portrait:GetLeft(), portrait:GetRight(), portrait:GetTop(), portrait:GetBottom()
            local fl, fb = frame:GetLeft(), frame:GetBottom()
            if pl and pr and pt and pb and fl and fb then
                entry.portraitBase = {
                    left = pl, right = pr, top = pt, bottom = pb,
                    cx = (pl + pr) / 2 - fl, cy = (pt + pb) / 2 - fb,
                    layout = capture(portrait),
                }
            end
        end
        local name = _G[spec.name or ""]
        if name and name.GetNumPoints then
            entry.name = name
            if not entry.active or name:GetNumPoints() ~= 1 or select(2, name:GetPoint(1)) ~= health then
                entry.nameLayout = capture(name)
            end
        end
        local right = spec.side == "right"
        local hx, px = right and health:GetRight() or health:GetLeft(), nil
        if power then px = right and power:GetRight() or power:GetLeft() end
        entry.powerX = hx and px and px - hx or 0
        local bottom, top = health:GetBottom(), power and power:GetTop()
        entry.gap = bottom and top and math.max(-2, math.min(12, bottom - top)) or 2
    end
    if power ~= entry.primary then
        if entry.powerBacking then entry.powerBacking:Hide(); entry.powerBacking = nil end
        entry.primary, entry.power = power, power and capture(power)
    end
    local hw, hh, pw, ph = B:Dimensions(spec.group, entry.health.width, entry.health.height,
        entry.power and entry.power.width, entry.power and entry.power.height)
    if not entry.portraitBase then scale = 1 end
    entry.scale = scale
    local custom = hw ~= entry.health.width or hh ~= entry.health.height
        or power and entry.power and (pw ~= entry.power.width or ph ~= entry.power.height)
        or scale ~= 1
    if not custom then
        if not entry.active then return end
        restore(health, entry.health)
        if entry.primary and entry.power then restore(entry.primary, entry.power) end
        if entry.name then restore(entry.name, entry.nameLayout) end
        if entry.portraitMoved and entry.portraitBase then
            restore(_G[spec.portrait], entry.portraitBase.layout)
            entry.portraitMoved = nil
        end
        frame:SetHitRectInsets(unpack(entry.insets))
        resetArt(entry)
        entry.active = nil
        return
    end

    local base = entry.portraitBase
    local portraitRegion = base and _G[spec.portrait]
    if portraitRegion and scale ~= 1 then
        portraitRegion:ClearAllPoints()
        portraitRegion:SetPoint("CENTER", frame, "BOTTOMLEFT", base.cx, base.cy)
        portraitRegion:SetSize((base.right - base.left) * scale, (base.top - base.bottom) * scale)
        entry.portraitMoved = true
    elseif portraitRegion and entry.portraitMoved then
        restore(portraitRegion, base.layout)
        entry.portraitMoved = nil
    end
    enableArt(entry, spec, frame, health, power)
    health:ClearAllPoints()
    local first = entry.health.points[1]
    if first then health:SetPoint(unpack(first))
    else health:SetPoint("TOPLEFT", frame, "TOPLEFT") end
    health:SetSize(hw, hh)
    if power and entry.power then
        local right = spec.side == "right"
        power:ClearAllPoints()
        power:SetPoint(right and "TOPRIGHT" or "TOPLEFT", health,
            right and "BOTTOMRIGHT" or "BOTTOMLEFT", entry.powerX, -entry.gap)
        power:SetSize(pw, ph)
    end
    if entry.name then
        entry.name:ClearAllPoints()
        entry.name:SetPoint("BOTTOMLEFT", health, "TOPLEFT", 0, 1)
        entry.name:SetWidth(hw)
    end
    local extraW = math.max(0, hw - entry.health.width, entry.power and pw - entry.power.width or 0)
    if base and scale > 1 then extraW = extraW + (base.right - base.left) * (scale - 1) / 2 end
    local extraH = math.max(0, hh - entry.health.height) + math.max(0, entry.power and ph - entry.power.height or 0)
    local left, right, top, bottom = unpack(entry.insets)
    if spec.side == "right" then left = left - extraW else right = right - extraW end
    frame:SetHitRectInsets(left, right, top - extraH / 2, bottom - extraH)
    entry.active = true
end

local function refresh()
    for _, spec in ipairs(layouts) do resize(spec) end
    if addon.RefreshUnitValues then addon.RefreshUnitValues() end
end

for _, group in ipairs({"player", "target", "targettarget", "focus", "focustarget", "pet", "boss", "arena"}) do
    B:Register(group, "Unit Frames", refresh)
end
for _, group in ipairs({"player", "target", "targettarget", "focus", "focustarget", "pet"}) do
    S:Register({
        id = "unit." .. group .. ".portraitScale", name = "Portrait size", category = "Unit Frames",
        group = "Portrait", type = "slider", default = 100, min = 50, max = 200, step = 5, protected = true,
        desc = "Percent of the normal portrait size. The ring around it scales too.",
        keywords = group .. " portrait scale size bigger smaller",
        validate = function(n)
            if type(n) ~= "number" or n ~= n or math.abs(n) == math.huge then return end
            return math.max(50, math.min(200, math.floor(n / 5 + .5) * 5))
        end,
        apply = refresh,
    })
end

local lib = LibStub and LibStub("AceAddon-3.0", true)
local dragonflight = lib and lib:GetAddon("DragonflightUI", true)
local unitframe = dragonflight and dragonflight:GetModule("Unitframe", true)
if unitframe then
    for _, spec in ipairs(layouts) do
        local method = spec.method
        if method and unitframe[method] then hooksecurefunc(unitframe, method, function() resize(spec, true) end) end
    end
end

local events = CreateFrame("Frame")
for _, event in ipairs({"PLAYER_ENTERING_WORLD", "PLAYER_TARGET_CHANGED", "PLAYER_FOCUS_CHANGED",
    "UNIT_TARGET", "UNIT_PET", "PLAYER_REGEN_ENABLED", "ADDON_LOADED"}) do events:RegisterEvent(event) end
events:SetScript("OnEvent", function(_, event, unit)
    if event == "ADDON_LOADED" and unit ~= "DrakeishUI" and unit ~= "Blizzard_ArenaUI" then return end
    if event == "UNIT_TARGET" and unit ~= "target" and unit ~= "focus" then return end
    if event == "UNIT_PET" and unit ~= "player" then return end
    refresh()
end)
