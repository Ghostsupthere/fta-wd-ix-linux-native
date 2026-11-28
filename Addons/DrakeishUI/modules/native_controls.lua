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
    if addon.Settings then return nil end
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

-- Put the layout buttons directly on the same native Interface style page as the
-- master style switch. That page is created after LoadAddOn returns, so attach
-- at login/world entry instead of assuming it already exists at module load.
local function attachStyleSettings()
    local host, style = WaystoneInterfaceStylePanel, WaystoneUseModernInterface
    if not (host and style and addon.Layout) then return false end
    if host._drakeishLayoutSettings then return true end

    local L = addon.Layout
    local section = CreateFrame("Frame", "DrakeishUIIntegratedLayoutSettings", host)
    section:SetPoint("TOPLEFT", style, "BOTTOMLEFT", 4, -58)
    section:SetSize(480, 60)
    host._drakeishLayoutSettings = section
    local title = section:CreateFontString(nil, "ARTWORK", "GameFontNormal")
    title:SetPoint("TOPLEFT", 0, 0); title:SetText("Layout")

    local function button(name, text, x, y, width, action)
        local b = CreateFrame("Button", name, section, "UIPanelButtonTemplate")
        b:SetSize(width, 24); b:SetPoint("TOPLEFT", section, "TOPLEFT", x, y)
        b:SetText(text); b:SetScript("OnClick", action)
        return b
    end
    button("DrakeishUIIntegratedMoveUI", "Move UI", 0, -22, 110, function()
        if not InCombatLockdown() then L:SetEditing(true); HideUIPanel(InterfaceOptionsFrame) end
    end)
    button("DrakeishUIIntegratedLockUI", "Lock UI", 117, -22, 110, function() L:SetEditing(false) end)
    button("DrakeishUIIntegratedMoreLayout", "More layout settings", 234, -22, 165, function() L:OpenOptions() end)
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
    if addon.Settings and addon.Settings.HandleCommand and addon.Settings:HandleCommand(message or "") then return end
    message=string.lower(message or "")
    if addon.Layout and message=="move" then addon.Layout:SetEditing(true)
    elseif addon.Layout and message=="lock" then addon.Layout:SetEditing(false)
    elseif addon.Layout and message=="layout" then addon.Layout:OpenOptions()
    elseif addon.Messages and (message=="text" or message=="messages") then addon.Messages:OpenOptions()
    elseif addon.Settings and message=="bags" then addon.Settings:Open("Bags")
    elseif addon.Settings then addon.Settings:Open()
    elseif InterfaceOptionsFrame_OpenToCategory then InterfaceOptionsFrame_OpenToCategory(showPanel()) end
end
