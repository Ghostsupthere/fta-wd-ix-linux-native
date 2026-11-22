-- Lives in the Video options window, as a sub-page under Effects.
if WXLNativeAtmospherePanel or not (VideoOptionsFrame and OptionsFrame_AddCategory) then return end

local panel = CreateFrame("Frame", "WXLNativeAtmospherePanel", VideoOptionsFramePanelContainer)
panel.name = "Atmosphere & Water"
panel.parent = (VideoOptionsEffectsPanel and VideoOptionsEffectsPanel.name) or EFFECTS_LABEL
panel:Hide()

local scroll = CreateFrame("ScrollFrame", "WXLNativeAtmosphereScroll", panel, "UIPanelScrollFrameTemplate")
scroll:SetPoint("TOPLEFT", 12, -10)
scroll:SetPoint("BOTTOMRIGHT", -30, 12)
local content = CreateFrame("Frame", "WXLNativeAtmosphereContent", scroll)
content:SetSize(330, 1000)
scroll:SetScrollChild(content)

local title = content:CreateFontString(nil, "ARTWORK", "GameFontNormalLarge")
title:SetPoint("TOPLEFT", 4, -5)
title:SetText(panel.name)
local status = content:CreateFontString(nil, "ARTWORK", "GameFontHighlightSmall")
status:SetPoint("TOPLEFT", title, "BOTTOMLEFT", 0, -8)
status:SetPoint("RIGHT", content, "RIGHT", -10, 0)
status:SetJustifyH("LEFT")

local controls, before = {}, {}
local syncing, dirty, saveFailed
local preview = "atmosphere.preview"
local parents = {
    ["atmosphere.mist"] = "atmosphere.fog",
    ["atmosphere.drift"] = "atmosphere.fog",
    ["atmosphere.nativeLights"] = "atmosphere.fog",
    ["atmosphere.lantern"] = "atmosphere.fog",
    ["water.waves"] = "water.modern",
    ["water.ripples"] = "water.modern",
}
local function Read(key)
    return WarcraftXL_GetEnvironmentOption(key) or 0
end
local function UpdateStatus()
    local supported, state = WXL_AtmosphereStatus()
    status:SetText(saveFailed and "Settings could not be saved." or
        (supported and (state .. ". Use Okay to save or Cancel to undo.") or
            "Native world effects are unavailable on this graphics device."))
end
local function Synchronize()
    syncing = true
    for _, entry in ipairs(controls) do
        local value = Read(entry.key)
        local enabled = entry.key == preview or Read(preview) ~= 0
        local parent = entry.parent
        while enabled and parent do
            enabled = Read(parent) ~= 0
            parent = parents[parent]
        end
        if entry.slider then
            entry.frame:SetValue(value)
            entry.value:SetText(string.format(entry.format, value))
        else
            entry.frame:SetChecked(value ~= 0)
        end
        if enabled then entry.frame:Enable() else entry.frame:Disable() end
    end
    syncing = false
    UpdateStatus()
end
local function Change(key, value)
    if syncing then return end
    if WarcraftXL_SetEnvironmentOption(key, value) then
        dirty = true
        saveFailed = false
    end
    Synchronize()
end
local flow = 60
local function Section(label)
    flow = flow + 10
    local y = flow
    flow = flow + 24
    local heading = content:CreateFontString(nil, "ARTWORK", "GameFontNormal")
    heading:SetPoint("TOPLEFT", 4, -y)
    heading:SetText(label)
end
local function Check(label, key, _, _, parent, help)
    local y = flow
    flow = flow + 26
    local name = panel:GetName() .. "Check" .. (#controls + 1)
    local frame = CreateFrame("CheckButton", name, content, "UICheckButtonTemplate")
    frame:SetPoint("TOPLEFT", 2, -y)
    local text = frame:CreateFontString(nil, "ARTWORK", "GameFontHighlightSmall")
    text:SetPoint("LEFT", frame, "RIGHT", 1, 0)
    text:SetText(label)
    frame:SetHitRectInsets(0, -text:GetStringWidth() - 6, 0, 0)
    frame:SetScript("OnClick", function(self) Change(key, self:GetChecked() and 1 or 0) end)
    frame:SetScript("OnEnter", function(self)
        GameTooltip:SetOwner(self, "ANCHOR_RIGHT")
        GameTooltip:SetText(help or label, 1, 1, 1, 1, true)
    end)
    frame:SetScript("OnLeave", function() GameTooltip:Hide() end)
    controls[#controls + 1] = {frame=frame, key=key, parent=parent}
end
local function Slider(label, key, _, _, low, high, step, parent, format, help)
    local y = flow + 16
    flow = flow + 46
    local name = panel:GetName() .. "Slider" .. (#controls + 1)
    local frame = CreateFrame("Slider", name, content, "OptionsSliderTemplate")
    frame:SetPoint("TOPLEFT", 8, -y)
    frame:SetWidth(210)
    frame:SetMinMaxValues(low, high)
    frame:SetValueStep(step)
    _G[name .. "Text"]:SetText(label)
    _G[name .. "Low"]:SetText(tostring(low))
    _G[name .. "High"]:SetText(tostring(high))
    local valueText = frame:CreateFontString(nil, "ARTWORK", "GameFontHighlightSmall")
    valueText:SetPoint("LEFT", frame, "RIGHT", 5, 0)
    frame:SetScript("OnValueChanged", function(_, value) Change(key, value) end)
    frame:SetScript("OnEnter", function(self)
        GameTooltip:SetOwner(self, "ANCHOR_RIGHT")
        GameTooltip:SetText(help or label, 1, 1, 1, 1, true)
    end)
    frame:SetScript("OnLeave", function() GameTooltip:Hide() end)
    controls[#controls + 1] = {frame=frame, key=key, parent=parent,
        slider=true, value=valueText, format=format or "%.2f"}
end

Check("Use these settings", preview, 1, 64, nil, "Preview these controls in the world. Clear this switch to return to your launch profile.")

Section("Fog and shafts", 104)
Check("Depth fog", "atmosphere.fog", 1, 126)
Check("Sun and moon shafts", "atmosphere.shafts", 2, 126)
Slider("Fog density", "atmosphere.density", 1, 177, 0, 2, .05, "atmosphere.fog", nil, "Scales atmospheric fog without changing view distance.")
Slider("Shaft strength", "atmosphere.shaftStrength", 2, 177, 0, 2, .05, "atmosphere.shafts")
Check("Zone-specific fog", "atmosphere.regions", 1, 213, "atmosphere.fog",
    "Uses authored fog settings where the current area has a profile.")
Check("Fog transparent models", "atmosphere.transparentFog", 2, 213, "atmosphere.fog")
Check("Ground mist", "atmosphere.mist", 1, 246, "atmosphere.fog")
Check("Moving fog detail", "atmosphere.drift", 2, 246, "atmosphere.fog")
Slider("Ground mist strength", "atmosphere.mistStrength", 1, 299, 0, 2, .05, "atmosphere.mist")
Slider("Fog variation", "atmosphere.variation", 2, 299, 0, 2, .05, "atmosphere.drift")
Slider("Fog movement (yards/sec)", "atmosphere.wind", 1, 349, 0, 12, .25, "atmosphere.drift",
    nil, "Needs variation above 0. A speed of 0 freezes the pattern.")
Slider("Fog detail size (yards)", "atmosphere.patchSize", 2, 349, 10, 120, 5, "atmosphere.drift",
    "%.0f", "Needs variation above 0. Higher values make broader patches.")

Check("Weather-driven fog movement", "atmosphere.weatherDrift", 1, 0, "atmosphere.drift")
Check("Refine shaft edges", "atmosphere.shaftEdges", 1, 0, "atmosphere.shafts")

Section("Night and local lighting", 452)
Check("Outdoor night darkening", "atmosphere.night", 1, 474)
Check("Night light halos", "atmosphere.glow", 2, 474, nil, "Adds a soft glow around bright scene pixels after dark.")
Slider("Night darkening", "atmosphere.nightDarkness", 1, 526, 0, 1, .05, "atmosphere.night")
Slider("Halo strength", "atmosphere.glowStrength", 2, 526, 0, 2, .05, "atmosphere.glow")
Check("Native lights in fog", "atmosphere.nativeLights", 1, 559, "atmosphere.fog")
Check("Player lantern in fog", "atmosphere.lantern", 2, 559, "atmosphere.fog", "Adds a warm, local light to the fog near your character.")
Slider("Native light strength", "atmosphere.lightStrength", 1, 612, 0, 2, .05, "atmosphere.nativeLights")
Slider("Lantern strength", "atmosphere.lanternStrength", 2, 612, 0, 2, .05, "atmosphere.lantern")

Check("Lootable corpse lights", "atmosphere.corpseLights", 1, 0, nil,
    "Adds a faint light near a few nearby lootable corpses.")

Check("Rain-wet surfaces", "atmosphere.wetSurfaces", 1, 0, nil,
    "Adds damp terrain and model lighting outdoors during rain.")

Check("Cloud shadows", "atmosphere.cloudShadows", 1, 0, nil,
    "Moves soft cloud shade across outdoor terrain and dims nearby fog light.")

Section("Water", 671)
Check("Modern water", "water.modern", 1, 695)
Slider("Water quality", "water.quality", 2, 711, 1, 3, 1, "water.modern", "%.0f")
Check("Water waves", "water.waves", 1, 744, "water.modern")
Check("Movement ripples", "water.ripples", 2, 744, "water.modern")
Check("Rain rings", "water.rainRings", 1, 0, "water.modern")
Slider("Wave strength", "water.waveStrength", 1, 798, 0, 2, .05, "water.waves")
Slider("Ripple strength", "water.rippleStrength", 2, 798, 0, 2, .05, "water.ripples")
Slider("Shore foam", "water.foam", 1, 849, 0, 2, .05, "water.modern")
Slider("Water reflections", "water.reflections", 2, 849, 0, 2, .05, "water.modern")
Slider("Water clarity", "water.clarity", 1, 900, 0, 2, .05, "water.modern")

local restore = CreateFrame("Button", "WXLNativeAtmosphereRestore", content, "UIPanelButtonTemplate")
restore:SetSize(165, 24)
restore:SetPoint("TOPLEFT", 5, -(flow + 12))
content:SetHeight(flow + 56)
restore:SetText("Restore page defaults")
local function Defaults()
    for _, entry in ipairs(controls) do
        WarcraftXL_SetEnvironmentOption(entry.key, WarcraftXL_GetEnvironmentOptionDefault(entry.key))
    end
    dirty = true
    Synchronize()
end
restore:SetScript("OnClick", Defaults)

local function Remember()
    for _, entry in ipairs(controls) do before[entry.key] = Read(entry.key) end
end
panel.default = Defaults
panel.refresh = function()
    if not dirty then Remember() end
    Synchronize()
end
panel.okay = function()
    if not dirty then return end
    saveFailed = not WarcraftXL_SaveEnvironmentOptions()
    if not saveFailed then dirty = false; Remember() end
    UpdateStatus()
end
panel.cancel = function()
    if not dirty then return end
    for _, entry in ipairs(controls) do
        if before[entry.key] ~= nil then WarcraftXL_SetEnvironmentOption(entry.key, before[entry.key]) end
    end
    dirty, saveFailed = false, false
    Synchronize()
end

Remember()
OptionsFrame_AddCategory(VideoOptionsFrame, panel)
-- Show the sub-page under Effects without making the player expand the list first.
if VideoOptionsEffectsPanel and VideoOptionsEffectsPanel.name == panel.parent then
    VideoOptionsEffectsPanel.collapsed = false
    panel.hidden = false
    if VideoOptionsFrame.categoryFrame and VideoOptionsFrame.categoryFrame.update then
        VideoOptionsFrame.categoryFrame:update()
    end
end
local elapsedStatus = 0
panel:SetScript("OnUpdate", function(_, elapsed)
    elapsedStatus = elapsedStatus + elapsed
    if elapsedStatus >= 0.5 then
        elapsedStatus = 0
        UpdateStatus()
    end
end)
