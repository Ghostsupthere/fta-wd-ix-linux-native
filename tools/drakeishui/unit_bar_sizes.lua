local _, addon = ...
local S = addon.Settings
local B = {}
addon.UnitBarSizes = B

local fields = {
    {"healthWidth", "Health width", 32, 360}, {"healthHeight", "Health height", 6, 60},
    {"powerWidth", "Power width", 24, 360}, {"powerHeight", "Power height", 3, 40},
}

local function sized(group, key, fallback)
    local saved = S:Get("unit." .. group .. "." .. key)
    if saved and saved > 0 then return saved end
    return fallback
end

local function dimensions(group, width, height, powerWidth, powerHeight)
    return sized(group, "healthWidth", width), sized(group, "healthHeight", height),
        sized(group, "powerWidth", powerWidth), sized(group, "powerHeight", powerHeight)
end

local function register(group, category, apply)
    for _, field in ipairs(fields) do
        local key, name, minimum, maximum = unpack(field)
        S:Register({
            id = "unit." .. group .. "." .. key, name = name, category = category, group = "Bar Size",
            type = "slider", default = -1, min = -1, max = maximum, step = 1, protected = true,
            desc = "-1 uses the existing size.",
            validate = function(n)
                if type(n) ~= "number" or n ~= n or math.abs(n) == math.huge then return end
                if n == -1 then return -1 end
                return math.max(minimum, math.min(maximum, math.floor(n + .5)))
            end,
            apply = apply,
        })
    end
end

local function baseTexture(bar)
    local background = bar:CreateTexture(nil, "BACKGROUND")
    background:SetPoint("TOPLEFT", bar, "TOPLEFT", -1, 1)
    background:SetPoint("BOTTOMRIGHT", bar, "BOTTOMRIGHT", 1, -1)
    background:SetTexture(.10, .08, .07, .95)
    return background
end

local function refreshGroup()
    if InCombatLockdown() then return end
    if DrakeishUI_RefreshPartyFrames then DrakeishUI_RefreshPartyFrames() end
    if DrakeishUI_UpdateRaidFrames then DrakeishUI_UpdateRaidFrames() end
    if addon.RefreshUnitValues then addon.RefreshUnitValues() end
end

register("party", "Party and Raid", refreshGroup)
register("raid", "Party and Raid", refreshGroup)
register("raidThin", "Party and Raid", refreshGroup)
for _, group in ipairs({"raid", "raidThin"}) do
    S:Register({
        id = "unit." .. group .. ".powerBar", name = "Show separate power bar", category = "Party and Raid",
        group = "Bar Size", type = "toggle", default = false, protected = true, apply = refreshGroup,
    })
end

function B:ResizeParty(frame)
    if InCombatLockdown() or not S:IsReady() then return end
    local hw, hh, pw, ph = dimensions("party", 85, 11, 87, 7)
    local previous = frame.drakeishBarMetrics
    if previous and previous[1] == hw and previous[2] == hh and previous[3] == pw and previous[4] == ph then return end
    local custom = hw ~= 85 or hh ~= 11 or pw ~= 87 or ph ~= 7
    if not custom and not previous then return end
    local health, power = frame.healthBar, frame.powerBar
    health:SetSize(hw, hh)
    power:SetSize(pw, ph)

    if custom then
        -- Keep the portrait fixed and let the party's existing row anchors follow the new button bounds.
        frame:SetSize(55 + math.max(hw, pw - 2), math.max(40, 28 + hh + ph))
        frame.portrait:ClearAllPoints()
        frame.portrait:SetPoint("TOPLEFT", frame, "TOPLEFT", 4, 0)
        frame.level:ClearAllPoints()
        frame.level:SetPoint("TOPRIGHT", frame, "TOPRIGHT", -16, -1)
        if not frame.drakeishHealthBacking then
            frame.drakeishHealthBacking, frame.drakeishPowerBacking = baseTexture(health), baseTexture(power)
        end
        frame.drakeishHealthBacking:Show()
        frame.drakeishPowerBacking:Show()
        frame.overlay:Hide()
        frame.background:Hide()
    else
        frame:SetSize(140, 40)
        frame.portrait:ClearAllPoints()
        frame.portrait:SetPoint("LEFT", frame, "LEFT", 4, 0)
        frame.level:ClearAllPoints()
        frame.level:SetPoint("RIGHT", frame, "RIGHT", -16, 17)
        frame.drakeishHealthBacking:Hide()
        frame.drakeishPowerBacking:Hide()
        frame.overlay:Show()
        frame.background:Show()
    end
    frame.drakeishBarMetrics = {hw, hh, pw, ph}
end

function B:RaidMetrics(width, height, thin)
    local group = thin and "raidThin" or "raid"
    local hw, hh, pw, ph = dimensions(group, width, height, width, 6)
    if not S:Get("unit." .. group .. ".powerBar") then return hw, hh, hw, hh end
    return math.max(hw, pw), hh + ph + 2, hw, hh, pw, ph
end

function B:ResizeRaid(frame, width, height, hw, hh, pw, ph)
    if InCombatLockdown() then return end
    local previous = frame.drakeishBarMetrics
    if previous and previous[1] == width and previous[2] == height and previous[3] == hw
        and previous[4] == hh and previous[5] == pw and previous[6] == ph then return end
    frame.drakeishBarMetrics = {width, height, hw, hh, pw, ph}
    frame:SetSize(width, height)
    local power = frame.drakeishPowerBar
    if not pw and not power then return end

    local health = frame.health
    health:ClearAllPoints()
    frame.bg:ClearAllPoints()
    frame.border:ClearAllPoints()
    frame.powerText:ClearAllPoints()
    frame.bg:SetAllPoints(health)
    frame.border:SetAllPoints(health)
    if pw then
        health:SetPoint("TOPLEFT", frame, "TOPLEFT")
        health:SetSize(hw, hh)
        if not power then
            power = CreateFrame("StatusBar", nil, frame)
            power:SetStatusBarTexture("Interface\\AddOns\\DrakeishUI\\assets\\DF-StatusBar")
            power:EnableMouse(false)
            baseTexture(power)
            frame.drakeishPowerBar = power
        end
        power:SetPoint("TOPLEFT", health, "BOTTOMLEFT", 0, -2)
        power:SetSize(pw, ph)
        power:Show()
        frame.powerText:SetPoint("RIGHT", power, "RIGHT", -3, 0)
    else
        health:SetAllPoints(frame)
        power:Hide()
        frame.powerText:SetPoint("BOTTOMLEFT", health, "BOTTOMLEFT", 3, 2)
        frame.powerText:SetPoint("BOTTOMRIGHT", health, "BOTTOMRIGHT", -3, 2)
    end
end

function B:UpdateRaidPower(frame, unit, value, maximum)
    local power = frame.drakeishPowerBar
    if not power or not power:IsShown() then return end
    local index, token = UnitPowerType(unit)
    local colour = PowerBarColor and (PowerBarColor[token] or PowerBarColor[index])
    if colour then power:SetStatusBarColor(colour.r, colour.g, colour.b) end
    power:SetMinMaxValues(0, math.max(1, maximum))
    power:SetValue((UnitIsDeadOrGhost(unit) or UnitIsConnected and not UnitIsConnected(unit)) and 0 or value)
end
