local _, addon = ...
local S = addon.Settings
local groups = {
    player = { "player", "PlayerPortrait" },
    target = { "target", "TargetFramePortrait" },
    focus = { "focus", "FocusFramePortrait" },
    targettarget = { "targettarget", "TargetFrameToTPortrait" },
    party = { "party1", "party2", "party3", "party4" },
}
local order = { "player", "target", "focus", "targettarget", "party" }
local portraits = {}
local probe = CreateFrame("PlayerModel", nil, UIParent)
probe:Hide()
local supports3D = probe.SetClipCircle and probe:SetClipCircle(false)

local choices = {
    { value = "2d", name = "2D" },
    { value = "class", name = "Class icon" },
    { value = "off", name = "Off" },
}
if supports3D then table.insert(choices, 2, { value = "3d", name = "3D" }) end

local function setBaseVisible(entry, visible)
    if visible and entry.baseAlpha then
        entry.base:SetAlpha(entry.baseAlpha)
        entry.baseAlpha = nil
    elseif not visible and not entry.baseAlpha then
        entry.baseAlpha = entry.base:GetAlpha()
        entry.base:SetAlpha(0)
    end
end

local function stopModel(entry)
    if not entry.model then return end
    entry.model:Hide()
    entry.model:SetScript("OnUpdate", nil)
    entry.guid = nil
end

local function requestModel(entry, guid)
    local model = entry.model
    if not model then
        model = CreateFrame("PlayerModel", nil, entry.parent)
        model:SetAllPoints(entry.base)
        model:SetFrameLevel(entry.parent:GetFrameLevel())
        model:EnableMouse(false)
        model:SetClipCircle(true)
        entry.model = model
    end

    -- Clear the previous unit first so a load failure can only reveal the 2D face.
    entry.guid = guid
    model:ClearModel()
    model:Show()
    model:SetUnit(entry.unit)
    if model:GetModel() then
        model:SetScript("OnUpdate", nil)
        return
    end

    local elapsed = 0
    model:SetScript("OnUpdate", function(self, dt)
        elapsed = elapsed + dt
        if elapsed < .25 then return end
        elapsed = 0
        if UnitGUID(entry.unit) ~= entry.guid then
            self:ClearModel()
            self:SetScript("OnUpdate", nil)
            return
        end
        self:SetUnit(entry.unit)
        if self:GetModel() then self:SetScript("OnUpdate", nil) end
    end)
end

local function refresh(entry, reload)
    local mode = S:Get("unit." .. entry.group .. ".portrait") or "2d"
    local guid = entry.parent:IsVisible() and UnitGUID(entry.unit)
    if mode ~= "3d" or not guid then
        stopModel(entry)
    elseif entry.guid ~= guid or reload then
        requestModel(entry, guid)
    end

    local class
    if mode == "class" and guid and UnitIsPlayer(entry.unit) then
        local _, token = UnitClass(entry.unit)
        class = CLASS_ICON_TCOORDS and CLASS_ICON_TCOORDS[token]
    end
    if class then
        if not entry.icon then
            entry.icon = entry.parent:CreateTexture(nil, "OVERLAY", nil, 0)
            entry.icon:SetAllPoints(entry.base)
            entry.icon:SetTexture("Interface\\TargetingFrame\\UI-Classes-Circles")
        end
        entry.icon:SetTexCoord(unpack(class))
        entry.icon:Show()
    elseif entry.icon then
        entry.icon:Hide()
    end
    setBaseVisible(entry, not class and mode ~= "off")
end

local function adopt(group, unit, base)
    if not base then return end
    local entry = portraits[unit]
    if not entry then
        entry = { group = group, unit = unit, base = base, parent = base:GetParent() }
        portraits[unit] = entry
        entry.parent:HookScript("OnShow", function() refresh(entry, true) end)
        entry.parent:HookScript("OnHide", function() stopModel(entry) end)
    end
    return entry
end

local function refreshAll()
    for _, group in ipairs(order) do
        local definition = groups[group]
        if group == "party" then
            for i, unit in ipairs(definition) do
                local frame = _G["CustomPartyFrame" .. i]
                adopt(group, unit, frame and frame.portrait)
            end
        else
            adopt(group, definition[1], _G[definition[2]])
        end
    end
    for _, entry in pairs(portraits) do refresh(entry, false) end
end

-- Custom party buttons are created after the settings profile applies. The
-- packager calls this once after each portrait region is attached.
function addon.RegisterPartyPortrait(unit, base)
    local entry = adopt("party", unit, base)
    if entry then refresh(entry, false) end
end

for _, group in ipairs(order) do
    S:Register({
        id = "unit." .. group .. ".portrait", name = "Portrait", category = "Unit Frames",
        group = "Portrait", type = "dropdown", default = "2d", choices = choices,
        keywords = group .. " face class icon round portrait", apply = refreshAll,
        validate = function(value)
            -- Retain a saved 3D selection when booting an older client, while
            -- the effective choice falls back to 2D for this session.
            if value == "3d" then return supports3D and value or "2d" end
            if value == "2d" or value == "class" or value == "off" then return value end
        end,
    })
end

local events = CreateFrame("Frame")
for _, event in ipairs({
    "PLAYER_ENTERING_WORLD", "PLAYER_TARGET_CHANGED", "PLAYER_FOCUS_CHANGED",
    "PARTY_MEMBERS_CHANGED", "UNIT_TARGET", "UNIT_PORTRAIT_UPDATE", "UNIT_MODEL_CHANGED",
}) do events:RegisterEvent(event) end
events:SetScript("OnEvent", function(_, event, unit)
    if not S:IsReady() then return end
    if event == "UNIT_PORTRAIT_UPDATE" or event == "UNIT_MODEL_CHANGED" then
        local guid = unit and UnitGUID(unit)
        if not guid then return end
        for token, entry in pairs(portraits) do
            if token == unit or UnitGUID(token) == guid then refresh(entry, true) end
        end
    elseif event ~= "UNIT_TARGET" or unit == "target" then
        refreshAll()
    end
end)
