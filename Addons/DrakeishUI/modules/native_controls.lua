-- Small native-client controls. The master UI switch belongs to native settings
-- and reloads the interface; there is no hidden second addon enable policy here.
local addon = select(2, ...)
local panel, controls
local petDriverActive = false
function RefreshBlizzardPetFrame()
    if not PetFrame or InCombatLockdown() then return end
    local raidOwnsPet = DrakeishUI_ShouldShowRaidStylePet and DrakeishUI_ShouldShowRaidStylePet()
    local hide = hide_pet_frame_state == 1 or raidOwnsPet
    if hide then
        if RegisterStateDriver then RegisterStateDriver(PetFrame, "visibility", "hide") end
        petDriverActive = true
        PetFrame:Hide()
    elseif petDriverActive then
        if UnregisterStateDriver then UnregisterStateDriver(PetFrame, "visibility") end
        petDriverActive = false
        if PetFrame_Update then PetFrame_Update(PetFrame) end
    end
end

function DrakeishUI_GetRaidFramesState()
    return (raid_frames_state == 1 or darken_ui_state == 1) and 1 or 0
end
function DrakeishUI_SetRaidFramesState(value)
    raid_frames_state = (value == true or value == 1) and 1 or 0
    darken_ui_state = raid_frames_state
end
function DrakeishUI_GetRaidFrameStyle()
    local db = DrakeishUI_SocialDB
    return ((type(db) == "table" and tonumber(db.raidFrameStyle) == 2)
        or tonumber(raid_frame_style_state) == 2) and 2 or 1
end
function DrakeishUI_SetRaidFrameStyle(style)
    raid_frame_style_state = tonumber(style) == 2 and 2 or 1
    DrakeishUI_SocialDB = DrakeishUI_SocialDB or {}
    DrakeishUI_SocialDB.raidFrameStyle = raid_frame_style_state
end
local function updateFrames()
    if DrakeishUI_UpdateRaidFrames then DrakeishUI_UpdateRaidFrames() end
    if DrakeishUI_RefreshPartyFrames then DrakeishUI_RefreshPartyFrames() end
    if RefreshBlizzardPetFrame then RefreshBlizzardPetFrame() end
end

local function showPanel()
    if not panel then
        panel = CreateFrame("Frame", "DrakeishUINativeOptions", UIParent)
        panel.name = "DrakeishUI"
        local title = panel:CreateFontString(nil, "ARTWORK", "GameFontNormalLarge")
        title:SetPoint("TOPLEFT", 16, -16)
        title:SetText("DrakeishUI")
        local description = panel:CreateFontString(nil, "ARTWORK", "GameFontHighlightSmall")
        description:SetPoint("TOPLEFT", title, "BOTTOMLEFT", 0, -10)
        description:SetPoint("RIGHT", panel, "RIGHT", -24, 0)
        description:SetHeight(0)
        description:SetJustifyH("LEFT")
        description:SetText("Modern panels use your server's live talents, glyphs and professions. Use Modern UI layout below to move and resize elements, adjust spacing, and open the bag settings. The main interface-style switch remains in the client's Interface style settings.")
        controls = {}
        local previous = description
        local function check(label, read, write)
            local button = CreateFrame("CheckButton", nil, panel, "UICheckButtonTemplate")
            button:SetPoint("TOPLEFT", previous, "BOTTOMLEFT", 0, -12)
            local text = button:CreateFontString(nil, "ARTWORK", "GameFontHighlight")
            text:SetPoint("LEFT", button, "RIGHT", 4, 0)
            text:SetText(label)
            button:SetScript("OnClick", function(self)
                if InCombatLockdown() then self:SetChecked(read()); return end
                write(self:GetChecked() and true or false)
                updateFrames()
            end)
            controls[#controls + 1] = { button = button, read = read }
            previous = button
        end
        check("Compact Drakeish raid frames",
            function() return DrakeishUI_GetRaidFramesState() == 1 end,
            function(on) DrakeishUI_SetRaidFramesState(on) end)
        check("Thin raid frame layout",
            function() return DrakeishUI_GetRaidFrameStyle() == 2 end,
            function(on) DrakeishUI_SetRaidFrameStyle(on and 2 or 1) end)
        check("Hide the pet frame",
            function() return hide_pet_frame_state == 1 end,
            function(on) hide_pet_frame_state = on and 1 or 0 end)
        local layout = CreateFrame("Button", nil, panel, "UIPanelButtonTemplate")
        layout:SetSize(220,26)
        layout:SetPoint("TOPLEFT",previous,"BOTTOMLEFT",4,-20)
        layout:SetText("Modern UI layout and bags")
        layout:SetScript("OnClick",function() if addon.Layout then addon.Layout:OpenOptions() end end)
        local messages = CreateFrame("Button", nil, panel, "UIPanelButtonTemplate")
        messages:SetSize(190,26)
        messages:SetPoint("LEFT",layout,"RIGHT",8,0)
        messages:SetText("Screen messages")
        messages:SetScript("OnClick",function() if addon.Messages then addon.Messages:OpenOptions() end end)
        panel:SetScript("OnShow", function()
            for _, entry in ipairs(controls) do entry.button:SetChecked(entry.read()) end
        end)
        if InterfaceOptions_AddCategory then InterfaceOptions_AddCategory(panel) end
        if addon.Layout then addon.Layout:OptionsPanel() end
        if addon.Messages then addon.Messages:OptionsPanel() end
    end
    return panel
end

-- Put the bag controls directly on the same native Interface style page as the
-- master style switch. That page is created after LoadAddOn returns, so attach
-- at login/world entry instead of assuming it already exists at module load.
local function attachStyleSettings()
    local host, style = WaystoneInterfaceStylePanel, WaystoneUseModernInterface
    if not (host and style and addon.Layout) then return false end
    if host._drakeishBagSettings then return true end

    local L = addon.Layout
    local section = CreateFrame("Frame", "DrakeishUIIntegratedBagSettings", host)
    section:SetPoint("TOPLEFT", style, "BOTTOMLEFT", 4, -58)
    section:SetSize(480, 294)
    host._drakeishBagSettings = section
    local title = section:CreateFontString(nil, "ARTWORK", "GameFontNormal")
    title:SetPoint("TOPLEFT", 0, 0); title:SetText("Bag settings")
    local combine = CreateFrame("CheckButton", "DrakeishUIIntegratedCombineBags", section, "UICheckButtonTemplate")
    combine:SetPoint("TOPLEFT", title, "BOTTOMLEFT", -6, -5)
    local combineText = combine:CreateFontString(nil, "ARTWORK", "GameFontHighlight")
    combineText:SetPoint("LEFT", combine, "RIGHT", 4, 0); combineText:SetText("Combine carried bags into one window")
    local syncing = false
    local refreshers = {}
    combine:SetScript("OnClick", function(self)
        if InCombatLockdown() then self:SetChecked(L:DB().combined); return end
        L:DB().combined = self:GetChecked() and true or false; L:RefreshBags()
    end)
    refreshers[#refreshers + 1] = function() combine:SetChecked(L:DB().combined) end

    local function slider(key, label, x, y, low, high, step, format)
        local s = CreateFrame("Slider", "DrakeishUIIntegrated" .. key, section, "OptionsSliderTemplate")
        s:SetSize(200, 18); s:SetPoint("TOPLEFT", section, "TOPLEFT", x, y)
        s:SetMinMaxValues(low, high); s:SetValueStep(step)
        _G[s:GetName() .. "Low"]:SetText(tostring(low)); _G[s:GetName() .. "High"]:SetText(tostring(high))
        local function setTitle(value)
            _G[s:GetName() .. "Text"]:SetText(label .. " (" .. string.format(format, value) .. ")")
        end
        s:SetScript("OnValueChanged", function(self, value)
            value = math.floor(value / step + .5) * step
            if syncing then setTitle(value); return end
            if InCombatLockdown() then
                syncing = true; self:SetValue(L:DB()[key]); setTitle(L:DB()[key]); syncing = false
                return
            end
            L:DB()[key] = value; setTitle(value); L:RefreshBags()
        end)
        refreshers[#refreshers + 1] = function()
            local value = L:DB()[key]; s:SetValue(value); setTitle(value)
        end
        return s
    end
    slider("bagScale", "Bag scale", 12, -91, .5, 1.5, .05, "%.2f")
    slider("bagSpacing", "Space between bags", 250, -91, 4, 40, 1, "%d")
    slider("columns", "Combined bag columns", 12, -147, 8, 28, 1, "%d")
    slider("bagSlotSpacing", "Combined item spacing", 250, -147, 0, 16, 1, "%d")

    local function button(name, text, x, y, width, action)
        local b = CreateFrame("Button", name, section, "UIPanelButtonTemplate")
        b:SetSize(width, 24); b:SetPoint("TOPLEFT", section, "TOPLEFT", x, y)
        b:SetText(text); b:SetScript("OnClick", action)
        return b
    end
    local mode = button("DrakeishUIIntegratedSortMode", "", 0, -183, 145, function(self)
        if addon.Inventory and addon.Inventory.sorting then return end
        local text = "Sort by: " .. L:CycleBagSortMode()
        self:SetText(text)
        if DrakeishUIBagTools and DrakeishUIBagTools.order then
            DrakeishUIBagTools.order:SetText(text)
        end
    end)
    refreshers[#refreshers + 1] = function() mode:SetText("Sort by: " .. L:DB().sortMode) end
    button("DrakeishUIIntegratedSortBags", "Sort bags", 152, -183, 100, function()
        if addon.Inventory then HideUIPanel(InterfaceOptionsFrame); addon.Inventory:StartSort() end
    end)
    button("DrakeishUIIntegratedBagTools", "Search / filter", 259, -183, 140, function()
        if addon.Inventory then HideUIPanel(InterfaceOptionsFrame); addon.Inventory:ShowTools() end
    end)
    button("DrakeishUIIntegratedMoveUI", "Move UI", 0, -222, 110, function()
        if not InCombatLockdown() then L:SetEditing(true); HideUIPanel(InterfaceOptionsFrame) end
    end)
    button("DrakeishUIIntegratedLockUI", "Lock UI", 117, -222, 110, function() L:SetEditing(false) end)
    button("DrakeishUIIntegratedMoreLayout", "More layout settings", 234, -222, 165, function() L:OpenOptions() end)
    button("DrakeishUIIntegratedItemOutlines", "Item glow outlines", 0, -258, 180, function() L:OpenOutlineOptions() end)

    local function refresh()
        syncing = true
        for _, update in ipairs(refreshers) do update() end
        syncing = false
    end
    host:HookScript("OnShow", refresh)
    refresh()
    return true
end

local boot = CreateFrame("Frame")
boot:RegisterEvent("PLAYER_LOGIN")
boot:RegisterEvent("PLAYER_ENTERING_WORLD")
boot:RegisterEvent("PLAYER_REGEN_ENABLED")
boot:RegisterEvent("UNIT_PET")
local initialized = false
local function initialize()
    if not initialized then
        if caps_state == nil then caps_state = 1 end
        if buffs_state == nil then buffs_state = 1 end
        if hide_pet_frame_state == nil then hide_pet_frame_state = 0 end
        showPanel(); updateFrames(); initialized = true
    end
    attachStyleSettings()
end
boot:SetScript("OnEvent", function(self, event, unit)
    if event == "UNIT_PET" and unit ~= "player" then return end
    if event == "PLAYER_LOGIN" or event == "PLAYER_ENTERING_WORLD" then initialize(); return end
    RefreshBlizzardPetFrame()
end)
if IsLoggedIn and IsLoggedIn() then initialize() end
SLASH_DRAKEISHUINATIVE1 = "/drakeish"
SlashCmdList.DRAKEISHUINATIVE = function(message)
    message=string.lower(message or "")
    if addon.Layout and message=="move" then addon.Layout:SetEditing(true)
    elseif addon.Layout and message=="lock" then addon.Layout:SetEditing(false)
    elseif addon.Layout and message=="layout" then addon.Layout:OpenOptions()
    elseif addon.Messages and (message=="text" or message=="messages") then addon.Messages:OpenOptions()
    elseif addon.Inventory and message=="bags" then addon.Inventory:ShowTools()
    elseif InterfaceOptionsFrame_OpenToCategory then InterfaceOptionsFrame_OpenToCategory(showPanel()) end
end
