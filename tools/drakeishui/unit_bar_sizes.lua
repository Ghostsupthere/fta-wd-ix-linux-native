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

function B:Dimensions(group, width, height, powerWidth, powerHeight)
    return sized(group, "healthWidth", width), sized(group, "healthHeight", height),
        sized(group, "powerWidth", powerWidth), sized(group, "powerHeight", powerHeight)
end

function B:Register(group, category, apply)
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

-- The backing replaces the baked bar surround, so it carries its own thin metal edge (the baked art had one).
-- bare = true skips the edge (raid cells already have a border).
local EDGE = {.62, .60, .55, 1}
function B:CreateBacking(bar, bare)
    local background = bar:CreateTexture(nil, "BACKGROUND")
    background:SetPoint("TOPLEFT", bar, "TOPLEFT", -1, 1)
    background:SetPoint("BOTTOMRIGHT", bar, "BOTTOMRIGHT", 1, -1)
    background:SetTexture(.10, .08, .07, .95)
    if bare then return background end
    local edges = {}
    local function edge(a, b, c, d, horizontal)
        local line = bar:CreateTexture(nil, "OVERLAY")
        line:SetTexture(EDGE[1], EDGE[2], EDGE[3], EDGE[4])
        line:SetPoint(a, background, b)
        line:SetPoint(c, background, d)
        if horizontal then line:SetHeight(1) else line:SetWidth(1) end
        edges[#edges + 1] = line
    end
    edge("BOTTOMLEFT", "TOPLEFT", "BOTTOMRIGHT", "TOPRIGHT", true)
    edge("TOPLEFT", "BOTTOMLEFT", "TOPRIGHT", "BOTTOMRIGHT", true)
    edge("TOPRIGHT", "TOPLEFT", "BOTTOMRIGHT", "BOTTOMLEFT")
    edge("TOPLEFT", "TOPRIGHT", "BOTTOMLEFT", "BOTTOMRIGHT")
    local show, hide = background.Show, background.Hide
    background.Show = function(self) show(self); for _, line in ipairs(edges) do line:Show() end end
    background.Hide = function(self) hide(self); for _, line in ipairs(edges) do line:Hide() end end
    return background
end

local function refreshGroup()
    if InCombatLockdown() then return end
    if DrakeishUI_RefreshPartyFrames then DrakeishUI_RefreshPartyFrames() end
    if DrakeishUI_UpdateRaidFrames then DrakeishUI_UpdateRaidFrames() end
    if addon.RefreshUnitValues then addon.RefreshUnitValues() end
end

B:Register("party", "Party and Raid", refreshGroup)
S:Register({
    id = "unit.party.portraitScale", name = "Portrait size", category = "Party and Raid",
    group = "Portrait", type = "slider", default = 100, min = 50, max = 200, step = 5, protected = true,
    desc = "Percent of the normal portrait size. The ring around it scales too.",
    keywords = "party portrait scale size bigger smaller",
    validate = function(n)
        if type(n) ~= "number" or n ~= n or math.abs(n) == math.huge then return end
        return math.max(50, math.min(200, math.floor(n / 5 + .5) * 5))
    end,
    apply = refreshGroup,
})
B:Register("raid", "Party and Raid", refreshGroup)
B:Register("raidThin", "Party and Raid", refreshGroup)
for _, group in ipairs({"raid", "raidThin"}) do
    S:Register({
        id = "unit." .. group .. ".powerBar", name = "Show separate power bar", category = "Party and Raid",
        group = "Bar Size", type = "toggle", default = false, protected = true, apply = refreshGroup,
    })
end

function B:ResizeParty(frame)
    if InCombatLockdown() or not S:IsReady() then return end
    local hw, hh, pw, ph = self:Dimensions("party", 85, 11, 87, 7)
    local scale = (S:Get("unit.party.portraitScale") or 100) / 100
    local previous = frame.drakeishBarMetrics
    if previous and previous[1] == hw and previous[2] == hh and previous[3] == pw and previous[4] == ph
        and previous[5] == scale then return end
    local custom = hw ~= 85 or hh ~= 11 or pw ~= 87 or ph ~= 7 or scale ~= 1
    if not custom and not previous then return end
    local health, power = frame.healthBar, frame.powerBar
    health:SetSize(hw, hh)
    power:SetSize(pw, ph)

    if custom then
        -- Keep the portrait fixed and let the party's existing row anchors follow the new button bounds.
        -- The portrait scales about its own centre; the bars and name keep their place relative to that centre.
        frame:SetSize(35 + 20 * scale + math.max(hw, pw - 2), math.max(40, 28 + hh + ph, 40 * scale))
        frame.portrait:ClearAllPoints()
        frame.portrait:SetSize(40 * scale, 40 * scale)
        frame.portrait:SetPoint("CENTER", frame, "TOPLEFT", 24, -20 * math.max(1, scale))
        health:ClearAllPoints()
        health:SetPoint("TOPLEFT", frame.portrait, "TOPRIGHT", 0, 8 - 20 * scale)
        frame.name:ClearAllPoints()
        frame.name:SetPoint("TOPLEFT", frame.portrait, "TOPRIGHT", 10, 22 - 20 * scale)
        frame.level:ClearAllPoints()
        frame.level:SetPoint("TOPRIGHT", frame, "TOPRIGHT", -16, -1)
        if not frame.drakeishHealthBacking then
            frame.drakeishHealthBacking, frame.drakeishPowerBacking = self:CreateBacking(health), self:CreateBacking(power)
        end
        frame.drakeishHealthBacking:Show()
        frame.drakeishPowerBacking:Show()
        -- Keep the gold portrait ring from the baked overlay (its left third), placed where the full overlay put it.
        if not frame.drakeishRing then
            local ring = frame.overlayFrame:CreateTexture(nil, "ARTWORK")
            ring:SetTexture("Interface\\AddOns\\DrakeishUI\\assets\\PartyFrames")
            ring:SetTexCoord(0, .33, 0, 1)
            frame.drakeishRing = ring
        end
        frame.drakeishRing:SetSize(150 * .33 * scale, 75 * scale)
        frame.drakeishRing:ClearAllPoints()
        frame.drakeishRing:SetPoint("TOPLEFT", frame.portrait, "CENTER", -29 * scale, 28.5 * scale)
        frame.drakeishRing:Show()
        frame.overlay:Hide()
        frame.background:Hide()
    else
        frame:SetSize(140, 40)
        frame.portrait:ClearAllPoints()
        frame.portrait:SetSize(40, 40)
        frame.portrait:SetPoint("LEFT", frame, "LEFT", 4, 0)
        health:ClearAllPoints()
        health:SetPoint("TOPLEFT", frame.portrait, "TOPRIGHT", 0, -12)
        frame.name:ClearAllPoints()
        frame.name:SetPoint("TOPLEFT", frame.portrait, "TOPRIGHT", 10, 2)
        frame.level:ClearAllPoints()
        frame.level:SetPoint("RIGHT", frame, "RIGHT", -16, 17)
        frame.drakeishHealthBacking:Hide()
        frame.drakeishPowerBacking:Hide()
        if frame.drakeishRing then frame.drakeishRing:Hide() end
        frame.overlay:Show()
        frame.background:Show()
    end
    frame.drakeishBarMetrics = {hw, hh, pw, ph, scale}
end

function B:RaidMetrics(width, height, thin)
    local group = thin and "raidThin" or "raid"
    local hw, hh, pw, ph = self:Dimensions(group, width, height, width, 6)
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
            self:CreateBacking(power, true)
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
