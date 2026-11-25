local _, addon = ...
local S, CP = addon.Settings, addon.CharacterPanel
local category = "Character Panel"

local function applyBackground()
    CP.ApplyBodyBackground()
end

local function applyModel()
    CP.ApplyModelBackdrop()
end

local function applyGear()
    CP.ApplyGearSummaryVisibility()
end

local function register(id, option, apply)
    option.id = id
    option.category = category
    option.legacy = function()
        local config = addon:GetModuleConfig("characterpanel")
        return config and config[id]
    end
    option.apply = apply
    S:Register(option)
end

register("dark_background", {
    name = "Background", type = "dropdown", default = false, keywords = "stone dark character",
    choices = { { value = false, name = "Stone" }, { value = true, name = "Dark" } },
}, applyBackground)
register("grey_model_backdrop", {
    name = "Model backdrop", type = "dropdown", default = true, keywords = "greyscale grayscale full colour color",
    choices = { { value = true, name = "Greyscale" }, { value = false, name = "Full colour" } },
}, applyModel)
register("show_item_level", {
    name = "Item Level", type = "toggle", default = true, group = "Gear summary", keywords = "ilvl equipment",
}, applyGear)
register("show_gear_score", {
    name = "GearScore", type = "toggle", default = false, group = "Gear summary", keywords = "gear score equipment",
}, applyGear)

local legacyConfig = CP.Config
local legacy
local proxy = setmetatable({}, {
    __index = function(_, key)
        if S.byID[key] and S.byID[key].category == category then return S:Get(key) end
        return legacy[key]
    end,
    __newindex = function(_, key, value)
        if S.byID[key] and S.byID[key].category == category then S:Set(key, value)
        else legacy[key] = value end
    end,
})

function CP:Config()
    legacy = legacyConfig(self)
    if S:IsReady() then return proxy end
    return legacy
end

local function addGameMenuButton()
    local menu = GameMenuFrame
    local previous = GameMenuButtonWXLVisuals or GameMenuButtonOptions
    local nextButton = GameMenuButtonSoundOptions
    if not menu or not previous or not nextButton then return end

    local button = CreateFrame("Button", "DrakeishUISettingsGameMenuButton", menu, "GameMenuButtonTemplate")
    button:SetText("DrakeishUI Settings")
    button:SetPoint("TOP", previous, "BOTTOM", 0, -1)
    nextButton:ClearAllPoints()
    nextButton:SetPoint("TOP", button, "BOTTOM", 0, -1)
    menu:SetHeight(menu:GetHeight() + button:GetHeight() + 1)
    button:SetScript("OnClick", function()
        HideUIPanel(menu)
        S:Open()
    end)
end

if InCombatLockdown() then
    local menuEvents = CreateFrame("Frame")
    menuEvents:RegisterEvent("PLAYER_REGEN_ENABLED")
    menuEvents:SetScript("OnEvent", function(self)
        self:UnregisterEvent("PLAYER_REGEN_ENABLED")
        addGameMenuButton()
    end)
else
    addGameMenuButton()
end
