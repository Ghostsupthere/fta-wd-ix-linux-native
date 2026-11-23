-- Lives in the Video options window, as a sub-page under Effects.
if WXLNativeAtmospherePanel or not (VideoOptionsFrame and OptionsFrame_AddCategory) then return end

local panel = CreateFrame("Frame", "WXLNativeAtmospherePanel", VideoOptionsFramePanelContainer)
panel.name = "Visuals & Guidance"
panel.parent = (VideoOptionsEffectsPanel and VideoOptionsEffectsPanel.name) or EFFECTS_LABEL
panel:Hide()

local scroll = CreateFrame("ScrollFrame", "WXLNativeAtmosphereScroll", panel, "UIPanelScrollFrameTemplate")
scroll:SetPoint("TOPLEFT", 12, -65)
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
local saveDue  -- seconds until the settings are written to disk; any change re-arms it
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
    if WXL_RefreshAreaNames then WXL_RefreshAreaNames() end
    status:SetText(saveFailed and "Settings could not be saved." or
        (supported and (state .. ". Changes are saved automatically.") or
            "Native world effects are unavailable on this graphics device."))
end
local function Synchronize()
    syncing = true
    for _, entry in ipairs(controls) do
        local value = Read(entry.key)
        local enabled = entry.independent or entry.key == preview or Read(preview) ~= 0
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
        saveDue = 1.0
    end
    Synchronize()
end
local flow = 60
local groups, group = {}, nil
local function SelectGroup(key)
    for name, row in pairs(groups) do
        if name == key then
            row.frame:Show()
            row.button:Disable()
            content:SetHeight(math.max(row.height or 0, 260))
        else
            row.frame:Hide()
            row.button:Enable()
        end
    end
    scroll:SetVerticalScroll(0)
end
local function Group(key, label, x, y)
    if group then group.height = flow + 52 end
    flow = 95
    local frame = CreateFrame("Frame", "WXLNativeVisuals" .. key .. "Controls", content)
    frame:SetPoint("TOPLEFT", content, "TOPLEFT", 0, 0)
    frame:SetPoint("BOTTOMRIGHT", content, "BOTTOMRIGHT", 0, 0)
    local button = CreateFrame("Button", "WXLNativeVisuals" .. key .. "Tab", panel, "UIPanelButtonTemplate")
    button:SetText(label)
    button:SetSize(108, 21)
    button:SetPoint("TOPLEFT", panel, "TOPLEFT", 12 + x * 109, -10 - y * 24)
    button:SetScript("OnClick", function() SelectGroup(key) end)
    group = {frame=frame, button=button}
    groups[key] = group
end
local function ControlParent()
    return group and group.frame or content
end
local function Section(label)
    flow = flow + 10
    local y = flow
    flow = flow + 24
    local heading = ControlParent():CreateFontString(nil, "ARTWORK", "GameFontNormal")
    heading:SetPoint("TOPLEFT", 4, -y)
    heading:SetText(label)
end
local function Check(label, key, _, _, parent, help, independent)
    local y = flow
    flow = flow + 26
    local name = panel:GetName() .. "Check" .. (#controls + 1)
    local frame = CreateFrame("CheckButton", name, ControlParent(), "UICheckButtonTemplate")
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
    controls[#controls + 1] = {frame=frame, key=key, parent=parent, independent=independent}
end
local function Slider(label, key, _, _, low, high, step, parent, format, help, independent)
    local y = flow + 16
    flow = flow + 46
    local name = panel:GetName() .. "Slider" .. (#controls + 1)
    local frame = CreateFrame("Slider", name, ControlParent(), "OptionsSliderTemplate")
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
        slider=true, value=valueText, format=format or "%.2f", independent=independent}
end

Check("Use these settings", preview, 1, 64, nil, "Preview these controls in the world. Clear this switch to return to your launch profile.")

Group("Environment", "Environment", 0, 0)
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

Group("World", "World & Combat", 1, 0)
Section("Character")
Check("Character rim light", "world.characterRim", 1, 0, nil, "Adds a directional edge light to your character and equipment.")
Slider("Rim strength", "world.rimStrength", 1, 0, 0, 1, .05, "world.characterRim")
Slider("Rim taper", "world.rimWidth", 1, 0, 1, 8, .25, "world.characterRim")
Slider("Rim tint (warm to cool)", "world.rimTint", 1, 0, 0, 1, .05, "world.characterRim")
Section("Local lights")
Check("Lootable corpse lights", "atmosphere.corpseLights", 1, 0, nil,
    "Adds a faint light near a few nearby lootable corpses.")

Check("Static lamps", "world.staticLamps", 1, 0, "lights.enabled",
    "Lights eligible nearby lamp models. Requires Dynamic lights and the lamp catalog.")
Slider("Static lamp strength", "world.staticLampStrength", 1, 0, 0, 2, .05, "world.staticLamps")

Group("Effects", "Effects & Physics", 2, 0)
Section("Spell effects")
Check("Spell effect lights (experimental)", "atmosphere.spellLights", 1, 0, nil,
    "Adds up to four small lights from nearby spell effects and projectiles.")
Slider("Spell light brightness", "atmosphere.spellLightStrength", 1, 0, 1, 5, .1, "atmosphere.spellLights", nil,
    "Multiplies the light from spell effects, 1 to 5. Raise it if walls stay dark.")
Slider("Spell light distance (yards)", "atmosphere.spellLightRange", 1, 0, 2, 16, .5, "atmosphere.spellLights", "%.1f",
    "How far each spell light reaches across nearby surfaces.")

Group("Terrain", "Terrain & Water", 0, 1)
Section("Terrain")
Check("Colorful footsteps", "world.cosmeticFootprints", 1, 0, nil,
    "Colors your character's existing footprint decals on surfaces that show footsteps.")
Slider("Footstep brightness", "world.footprintStrength", 1, 0, 0, 1, .05, "world.cosmeticFootprints")
Check("Dynamic lights", "lights.enabled", 1, 0, nil, "Enables the existing surface-light grid.", true)
Check("Instanced ground clutter", "grass.instanced", 1, 0, nil, nil, true)
Slider("Grass wind strength", "grass.windStrength", 1, 0, 0, 2, .05, nil, nil, nil, true)
Check("Rain-wet surfaces", "atmosphere.wetSurfaces", 1, 0, nil,
    "Adds damp terrain and model lighting outdoors during rain.")

Check("Cloud shadows", "atmosphere.cloudShadows", 1, 0, nil,
    "Moves soft cloud shade across outdoor terrain and buildings and dims nearby fog light.")
Check("Terrain shadows", "atmosphere.terrainShadows", 1, 0, nil,
    "Shades nearby ground behind hills when the sun is low.")

Section("Water", 671)
Check("Modern water", "water.modern", 1, 695)
Slider("Water quality", "water.quality", 2, 711, 1, 3, 1, "water.modern", "%.0f")
Check("Water waves", "water.waves", 1, 744, "water.modern")
Check("Movement ripples", "water.ripples", 2, 744, "water.modern")
Check("Rain rings", "water.rainRings", 1, 0, "water.modern")
Check("Lamp reflections", "water.lampGlints", 1, 0, "water.modern",
    "Adds glints from nearby static lamps and the player lantern at night.")
Slider("Wave strength", "water.waveStrength", 1, 798, 0, 2, .05, "water.waves")
Slider("Ripple strength", "water.rippleStrength", 2, 798, 0, 2, .05, "water.ripples")
Slider("Shore foam", "water.foam", 1, 849, 0, 2, .05, "water.modern")
Slider("Water reflections", "water.reflections", 2, 849, 0, 2, .05, "water.modern")
Slider("Water clarity", "water.clarity", 1, 900, 0, 2, .05, "water.modern")

Group("Text", "Text & Image", 1, 1)
Section("Area names")
Check("Compact area announcements", "text.compactAreaNames", 1, 0, nil,
    "Moves zone and district announcements near the top and halves their font size.", true)
Check("Hide area announcements", "text.hideAreaNames", 1, 0, nil,
    "Hides only zone and district announcements.", true)
group.height = flow + 54

local restore = CreateFrame("Button", "WXLNativeAtmosphereRestore", panel, "UIPanelButtonTemplate")
restore:SetSize(165, 24)
restore:SetPoint("BOTTOMLEFT", panel, "BOTTOMLEFT", 12, 7)
scroll:SetPoint("BOTTOMRIGHT", -30, 36)
restore:SetText("Restore visual defaults")
SelectGroup("Environment")
local function Defaults()
    for _, entry in ipairs(controls) do
        WarcraftXL_SetEnvironmentOption(entry.key, WarcraftXL_GetEnvironmentOptionDefault(entry.key))
    end
    dirty = true
    saveDue = 1.0
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
    saveDue = nil
    if not dirty then return end
    for _, entry in ipairs(controls) do
        if before[entry.key] ~= nil then WarcraftXL_SetEnvironmentOption(entry.key, before[entry.key]) end
    end
    dirty, saveFailed = false, false
    Synchronize()
end

local areaFrames, areaRegions
local function InstallAreaNames()
    if not ZoneTextFrame or not SubZoneTextFrame or not hooksecurefunc then return end
    areaFrames, areaRegions = {ZoneTextFrame, SubZoneTextFrame}, {}
    for _, frame in ipairs(areaFrames) do
        frame:HookScript("OnShow", function(self)
            if Read("text.hideAreaNames") ~= 0 then self:Hide() end
        end)
    end
    for _, name in ipairs({"ZoneTextString", "SubZoneTextString", "PVPInfoTextString", "PVPArenaTextString"}) do
        local region = _G[name]
        if region and region.GetFont and region.SetFont then areaRegions[#areaRegions + 1] = region end
    end
end
local original, compactApplied = {}, false
function WXL_RefreshAreaNames()
    local compact = Read("text.compactAreaNames") ~= 0
    local hide = Read("text.hideAreaNames") ~= 0
    if not areaFrames and (compact or hide) then InstallAreaNames() end
    if not areaFrames then return end
    if compact ~= compactApplied then
        if compact then
            for _, frame in ipairs(areaFrames) do
                local points = {}
                for i = 1, frame:GetNumPoints() do points[i] = {frame:GetPoint(i)} end
                original[frame] = points
                frame:ClearAllPoints()
                frame:SetPoint("TOP", UIParent, "TOP", 0, -64)
            end
            for _, region in ipairs(areaRegions) do
                local font, size, flags = region:GetFont()
                if font and size and size > 0 then
                    original[region] = {font, size, flags}
                    region:SetFont(font, size * .5, flags)
                end
            end
        else
            for _, frame in ipairs(areaFrames) do
                frame:ClearAllPoints()
                for _, point in ipairs(original[frame] or {}) do frame:SetPoint(unpack(point, 1, 5)) end
            end
            for _, region in ipairs(areaRegions) do
                if original[region] then region:SetFont(unpack(original[region])) end
            end
            original = {}
        end
        compactApplied = compact
    end
    if hide then
        for _, frame in ipairs(areaFrames) do if frame:IsShown() then frame:Hide() end end
    end
end

Remember()
WXL_RefreshAreaNames()
-- Save a moment after the last change, so the settings survive a restart even when the window is closed with
-- Escape instead of Okay. This runs on its own frame because the page stops updating once it is hidden.
local saver = CreateFrame("Frame", nil, UIParent)
saver:SetScript("OnUpdate", function(_, elapsed)
    if not saveDue then return end
    saveDue = saveDue - elapsed
    if saveDue > 0 then return end
    saveDue = nil
    saveFailed = not WarcraftXL_SaveEnvironmentOptions()
    if not saveFailed then dirty = false; Remember() end
    UpdateStatus()
end)
OptionsFrame_AddCategory(VideoOptionsFrame, panel)
if SlashCmdList and not SlashCmdList.WXLVISUALS then
    SLASH_WXLVISUALS1 = "/visuals"
    SlashCmdList.WXLVISUALS = function()
        VideoOptionsFrame:Show()
        if OptionsFrame_OpenToCategory then OptionsFrame_OpenToCategory(panel) end
    end
end
-- Show the sub-page under Effects without making the player expand the list first.
if VideoOptionsEffectsPanel and VideoOptionsEffectsPanel.name == panel.parent then
    VideoOptionsEffectsPanel.collapsed = false
    panel.hidden = false
    if VideoOptionsFrame.categoryFrame and VideoOptionsFrame.categoryFrame.update then
        VideoOptionsFrame.categoryFrame:update()
    end
end
local areaEvents = CreateFrame("Frame", "WXLNativeAreaNameEvents")
areaEvents:RegisterEvent("PLAYER_ENTERING_WORLD")
areaEvents:RegisterEvent("ADDON_LOADED")
areaEvents:SetScript("OnEvent", WXL_RefreshAreaNames)
local elapsedStatus = 0
panel:SetScript("OnUpdate", function(_, elapsed)
    elapsedStatus = elapsedStatus + elapsed
    if elapsedStatus >= 0.5 then
        elapsedStatus = 0
        UpdateStatus()
    end
end)
