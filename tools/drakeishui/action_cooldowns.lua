local _, addon = ...
local settings = addon.Settings
local category = "Action Bars"

local bars = {
    { id = "main", name = "Main bar", prefixes = { "ActionButton", "BonusActionButton" }, count = 12 },
    { id = "bottomLeft", name = "Bottom left bar", prefixes = { "MultiBarBottomLeftButton" }, count = 12 },
    { id = "bottomRight", name = "Bottom right bar", prefixes = { "MultiBarBottomRightButton" }, count = 12 },
    { id = "right", name = "Right bar", prefixes = { "MultiBarRightButton" }, count = 12 },
    { id = "left", name = "Left bar", prefixes = { "MultiBarLeftButton" }, count = 12 },
    { id = "pet", name = "Pet bar", prefixes = { "PetActionButton" }, count = NUM_PET_ACTION_SLOTS or 10 },
    { id = "stance", name = "Stance bar", prefixes = { "ShapeshiftButton" }, count = NUM_SHAPESHIFT_SLOTS or 10 },
    { id = "totem", name = "Totem bar", prefixes = { "MultiCastActionButton" }, count = 12,
      names = { "MultiCastSummonSpellButton", "MultiCastRecallSpellButton" } },
    { id = "vehicle", name = "Vehicle bar", prefixes = { "VehicleMenuBarActionButton", "PossessButton" },
      count = math.max(VEHICLE_MAX_ACTIONBUTTONS or 6, NUM_POSSESS_SLOTS or 2) },
    { id = "bags", name = "Bags and micro menu",
      patterns = { "^ContainerFrame%d+Item%d+$", "^BankFrameItem%d+$", "^CharacterBag%d+Slot$",
                   "^MainMenuBarBackpackButton$", "MicroButton$" } },
}

local byButtonName = {}
for _, bar in ipairs(bars) do
    for _, prefix in ipairs(bar.prefixes or {}) do
        for i = 1, bar.count do byButtonName[prefix .. i] = bar end
    end
    for _, name in ipairs(bar.names or {}) do byButtonName[name] = bar end
end

local function findBar(name)
    if not name then return end
    local bar = byButtonName[name]
    if bar then return bar end
    for _, pattern in ipairs(bars[#bars].patterns) do
        if name:match(pattern) then return bars[#bars] end
    end
end

local styles = {
    { id = "font", name = "Font", type = "font", default = "expressway", choices = {
        { value = "expressway", name = "Expressway", path = addon._dir .. "expressway.ttf" },
        { value = "client", name = "Client default", path = STANDARD_TEXT_FONT },
        { value = "friz", name = "Friz Quadrata", path = "Fonts\\FRIZQT__.TTF" },
        { value = "calibri", name = "Calibri", path = addon._dir .. "Calibri1.ttf" },
    } },
    { id = "size", name = "Font size", type = "slider", default = 14, min = 8, max = 32, step = 1 },
    { id = "outline", name = "Font outline", type = "dropdown", default = "OUTLINE", choices = {
        { value = "", name = "None" }, { value = "OUTLINE", name = "Outline" },
        { value = "THICKOUTLINE", name = "Thick outline" },
    } },
    { id = "anchor", name = "Text anchor", type = "dropdown", default = "CENTER", choices = {
        { value = "TOPLEFT", name = "Top left" }, { value = "TOP", name = "Top" },
        { value = "TOPRIGHT", name = "Top right" }, { value = "LEFT", name = "Left" },
        { value = "CENTER", name = "Center" }, { value = "RIGHT", name = "Right" },
        { value = "BOTTOMLEFT", name = "Bottom left" }, { value = "BOTTOM", name = "Bottom" },
        { value = "BOTTOMRIGHT", name = "Bottom right" },
    } },
    { id = "x", name = "Text horizontal offset", type = "slider", default = 0, min = -24, max = 24, step = 1 },
    { id = "y", name = "Text vertical offset", type = "slider", default = 0, min = -24, max = 24, step = 1 },
}
local fontPaths = {}
for _, choice in ipairs(styles[1].choices) do fontPaths[choice.value] = choice.path end

local config, active = {}, {}
local timers = setmetatable({}, { __mode = "k" })
local driver = CreateFrame("Frame")
driver:Hide()
local ceil, floor = math.ceil, math.floor
local function milliseconds(seconds)
    return floor(seconds * 1000 + .5)
end
local function nowMs()
    return milliseconds(GetTime())
end
local updateAt = 0

local function stop(timer)
    if timer.text then timer.text:Hide() end
    local index = timer.index
    if not index then return end
    local last = #active
    if index < last then
        local replacement = active[last]
        active[index] = replacement
        replacement.index = index
    end
    active[last] = nil
    timer.index = nil
    if last == 1 then driver:Hide() end
end

local function paint(timer, remaining)
    local color = remaining <= 1000 and 1 or remaining < 3000 and 2 or remaining < 60000 and 3 or 4
    local text = timer.text
    if color ~= timer.color then
        local rgb = config.colors[color]
        text:SetTextColor(rgb[1], rgb[2], rgb[3])
        timer.color = color
    end
    local unit, value
    if timer.gcd or remaining < config.tenths then
        unit, value = 1, ceil(remaining / 100)
    else
        local seconds = ceil(remaining / 1000)
        if seconds >= 3600 then
            unit, value = 4, floor(seconds / 60)
        elseif seconds >= 60 then
            if config.minutes == "rounded" then unit, value = 3, ceil(seconds / 60)
            else unit, value = 2, seconds end
        else
            unit, value = 0, seconds
        end
    end
    if unit == timer.unit and value == timer.value then return end
    timer.unit, timer.value = unit, value
    -- Use the client's native numeric formatter so this path does not intern Lua strings.
    if unit == 1 then text:SetFormattedText("%d.%d", floor(value / 10), value % 10)
    elseif unit == 4 then text:SetFormattedText("%dh %dm", floor(value / 60), value % 60)
    elseif unit == 2 then text:SetFormattedText("%d:%02d", floor(value / 60), value % 60)
    elseif unit == 3 then text:SetFormattedText("%dm", value)
    else text:SetFormattedText("%d", value) end
end

driver:SetScript("OnUpdate", function()
    local now = nowMs()
    if now < updateAt then return end
    updateAt = now + 50
    local index, count = 1, #active
    while index <= count do
        local timer = active[index]
        local remaining = timer.ends - now
        if remaining > 0 then
            paint(timer, remaining)
            index = index + 1
        else
            stop(timer)
            count = count - 1
        end
    end
end)

local function applyStyle(timer)
    local bar, text = timer.bar, timer.text
    if not text then return end
    local style = bar.style
    local size = style.size
    local width = timer.button.GetWidth and timer.button:GetWidth()
    if config.fit and width and width > 0 and width < 30 then
        size = math.min(size, math.max(5, floor(width / 2.9)))
    end
    if not text:SetFont(fontPaths[style.font] or STANDARD_TEXT_FONT or "Fonts\\FRIZQT__.TTF", size, style.outline) then
        text:SetFontObject(GameFontNormal)
    end
    text:ClearAllPoints()
    text:SetPoint(style.anchor, timer.overlay, style.anchor, style.x, style.y)
    timer.overlay:SetFrameLevel(math.max(timer.button:GetFrameLevel(), timer.cooldown:GetFrameLevel()) + 1)
    timer.unit, timer.color = nil, nil
end

local function makeText(timer)
    local frame = CreateFrame("Frame", nil, timer.button)
    frame:SetAllPoints(timer.button)
    frame:EnableMouse(false)
    if frame.HookScript then frame:HookScript("OnSizeChanged", function() applyStyle(timer) end) end
    timer.overlay = frame
    timer.text = frame:CreateFontString(nil, "OVERLAY", "GameFontNormal")
    applyStyle(timer)
end

local function updateTimer(timer)
    local remaining = timer.ends - nowMs()
    if not config.enabled or remaining <= 0 or not timer.cooldown:IsVisible() or timer.alpha == 0
        or (timer.gcd and not config.gcd) or (not timer.gcd and timer.duration < config.minimum) then
        stop(timer)
        return
    end
    if not timer.text then makeText(timer) end
    if not timer.index then
        timer.index = #active + 1
        active[timer.index] = timer
        if timer.index == 1 then
            updateAt = 0
            driver:Show()
        end
    end
    paint(timer, remaining)
    timer.text:Show()
end

local function setSwipeAlpha(timer, value)
    timer.settingAlpha = true
    timer.cooldown:SetAlpha(value)
    timer.settingAlpha = nil
end

local function applySweep(timer)
    local hide = config.sweep == false
    if hide == timer.suppressed then return end
    timer.suppressed = hide
    setSwipeAlpha(timer, hide and 0 or timer.alpha)
end

local function onAlpha(self)
    local timer = timers[self]
    if timer.settingAlpha then return end
    timer.alpha = self:GetAlpha()
    if timer.suppressed and timer.alpha ~= 0 then setSwipeAlpha(timer, 0) end
    if config.enabled then updateTimer(timer) end
end

local function onShow(self)
    local timer = timers[self]
    if timer.overlay then
        timer.overlay:SetFrameLevel(math.max(timer.button:GetFrameLevel(), self:GetFrameLevel()) + 1)
    end
    updateTimer(timer)
end

local function onHide(self)
    local timer = timers[self]
    if not self:IsShown() then timer.ends = 0 end
    stop(timer)
end

local function register(cooldown, button, bar)
    local timer = { cooldown = cooldown, button = button, bar = bar, alpha = cooldown:GetAlpha(),
                    suppressed = false, ends = 0, duration = 0 }
    timers[cooldown] = timer
    cooldown:HookScript("OnShow", onShow)
    cooldown:HookScript("OnHide", onHide)
    hooksecurefunc(cooldown, "SetAlpha", onAlpha)
    applySweep(timer)
    return timer
end

local gcdQueryTime, gcdStart, gcdDuration
local function isGCD(start, duration, now)
    if duration > 2 or not GetSpellCooldown then return false end
    if now ~= gcdQueryTime or not gcdStart or gcdStart == 0 then
        gcdQueryTime = now
        gcdStart, gcdDuration = GetSpellCooldown(61304)
    end
    return start == gcdStart and duration == gcdDuration
end

local function onCooldown(cooldown, start, duration)
    local timer = timers[cooldown]
    if timer == false then return end
    if not timer then
        local button = cooldown:GetParent()
        local bar = button and findBar(button:GetName())
        if not bar then timers[cooldown] = false; return end
        timer = register(cooldown, button, bar)
    end
    local now = nowMs()
    start, duration = tonumber(start), tonumber(duration)
    if start and duration and start > 0 and duration > 0 then
        timer.duration, timer.ends = milliseconds(duration), milliseconds(start + duration)
        timer.gcd = isGCD(start, duration, now)
    else
        timer.ends = 0
    end
    updateTimer(timer)
end

local function settingValue(id)
    return settings:Get("cooldown." .. id)
end

local function refreshConfig()
    config.enabled = settingValue("enabled")
    config.fit = settingValue("fit")
    config.sweep = settingValue("sweep")
    config.gcd = settingValue("gcd")
    config.minimum = milliseconds(settingValue("minimum"))
    config.tenths = milliseconds(settingValue("tenths"))
    config.minutes = settingValue("minutes")
    config.colors = { settingValue("expiring"), settingValue("underThree"), settingValue("seconds"), settingValue("long") }
    for _, bar in ipairs(bars) do
        local prefix = settingValue(bar.id .. ".override") and bar.id .. "." or ""
        bar.style = bar.style or {}
        for _, style in ipairs(styles) do bar.style[style.id] = settingValue(prefix .. style.id) end
    end
    for _, timer in pairs(timers) do
        if timer then
            applyStyle(timer)
            applySweep(timer)
            updateTimer(timer)
        end
    end
end

local function addOption(id, group, option)
    option.id = "cooldown." .. id
    option.category = category
    option.group = group
    option.keywords = "cooldown countdown " .. group
    option.apply = refreshConfig
    if option.choices then
        local allowed = {}
        for _, choice in ipairs(option.choices) do allowed[choice.value] = true end
        option.validate = function(value) if allowed[value] then return value end end
    end
    settings:Register(option)
end

addOption("enabled", "Cooldown numbers", { name = "Show cooldown numbers", type = "toggle", default = true })
addOption("fit", "Cooldown numbers", { name = "Fit text to small buttons", type = "toggle", default = true })
addOption("gcd", "Cooldown numbers", { name = "Show global cooldown", type = "toggle", default = false })
addOption("sweep", "Cooldown numbers", { name = "Keep Blizzard cooldown sweep", type = "toggle", default = true })
addOption("minimum", "Cooldown numbers", {
    name = "Minimum cooldown (seconds)", type = "slider", default = 3, min = 0, max = 10, step = .1,
})
addOption("tenths", "Cooldown numbers", {
    name = "Tenths below (seconds)", type = "slider", default = 0, min = 0, max = 5, step = .1,
})
addOption("minutes", "Cooldown numbers", {
    name = "Minutes format", type = "dropdown", default = "clock",
    choices = { { value = "clock", name = "Minutes and seconds" }, { value = "rounded", name = "Rounded up minutes" } },
})
addOption("expiring", "Cooldown colors", { name = "Expiring (1 second or less)", type = "colour", default = { 1, .20, .20 } })
addOption("underThree", "Cooldown colors", { name = "Under 3 seconds", type = "colour", default = { 1, .50, .15 } })
addOption("seconds", "Cooldown colors", { name = "Under a minute", type = "colour", default = { 1, .82, .25 } })
addOption("long", "Cooldown colors", { name = "A minute or more", type = "colour", default = { .67, .80, .93 } })

local function addStyles(prefix, group)
    for _, style in ipairs(styles) do
        local option = {}
        for key, value in pairs(style) do option[key] = value end
        addOption(prefix .. style.id, group, option)
    end
end
addStyles("", "Default text style")
for _, bar in ipairs(bars) do
    local prefix = bar.id .. "."
    addOption(prefix .. "override", bar.name, { name = "Override " .. bar.name:lower() .. " style", type = "toggle", default = false })
    addStyles(prefix, bar.name)
end

-- The existing compatibility layer records native cooldown times in seconds. The
-- shared method hook also picks up newly created bag slots without polling bags.
local sample = ActionButton1Cooldown
if sample then
    hooksecurefunc(getmetatable(sample).__index, "SetCooldown", onCooldown)
    for name in pairs(byButtonName) do
        local cooldown = _G[name .. "Cooldown"]
        if cooldown and cooldown:IsShown() and cooldown.GetCooldownTimes then
            onCooldown(cooldown, cooldown:GetCooldownTimes())
        end
    end
end
