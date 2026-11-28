-- FTA addition: connects Bagster (the bag, bank and guild bank windows of the upstream DrakeishUI) to this
-- build. It replaces the game's own windows. Loads after bags_usability.lua and before the Bagster files.
local addon = select(2, ...)

-- What Bagster and its sorting expect to find in the bag database. Frame positions are added by Bagster.
local defaults = {
    bagster = { money_display = "icons", item_scale = 1, item_spacing = 2, bag_break = 1, break_space = 1.3,
        glow_quality = true, glow_quest = true, glow_alpha = 1, show_quality_filter = true },
    bagsort = { bank_fill_from_bags = true, lockedSlots = {}, move_interval = 0.1, lock_hotkey = "ALT_LEFT",
        lock_color = { 0.15, 0.80, 1.00, 0.95 }, reverse_stack = false },
}
local function fill(target, source)
    for key, value in pairs(source) do
        if type(value) == "table" then
            if type(target[key]) ~= "table" then target[key] = {} end
            fill(target[key], value)
        elseif target[key] == nil then target[key] = value end
    end
    return target
end

-- Bagster reads its options from this database; the settings window's choices are laid over it.
local breaks = { none = 0, type = 1, bag = 2 }
local settings = {
    bagster = { money_display = "bagster.money", item_scale = "bagster.itemScale", item_spacing = "bagster.itemSpacing",
        bag_break = "bagster.bagBreak", show_quality_filter = "bagster.qualityFilter",
        glow_quality = "bagster.glow.quality", glow_quest = "bagster.glow.quest", glow_alpha = "bagster.glow.alpha" },
    bagsort = { bank_fill_from_bags = "bagster.sort.fillBank", reverse_stack = "bagster.sort.reverse" },
}
local function chosen(id)
    local S = addon.Settings
    if S and S.byID[id] then return S:Get(id) end
end

local moduleConfig, moduleEnabled = addon.GetModuleConfig, addon.IsModuleEnabled
function addon:GetModuleConfig(name)
    local config = moduleConfig(self, name)
    if defaults[name] then
        config = fill(type(config) == "table" and config or {}, defaults[name])
        for key, id in pairs(settings[name]) do
            local value = chosen(id)
            if key == "bag_break" then value = breaks[value] end
            if value ~= nil then config[key] = value end
        end
        self.db.profile.modules[name] = config
    end
    return config
end
function addon:IsModuleEnabled(name)
    if defaults[name] then return true end
    return moduleEnabled(self, name)
end
-- Replaces the version in bags_usability.lua, which reads an options page this build does not have.
function addon:IsUnusableItemTintEnabled()
    return chosen("bagster.glow.unusable") == true
end

-- The reagent bank and gear bank add-ons put their tabs on the game's bank window, which Bagster hides.
local buttons
local function addBankButtons()
    local module = addon.BagsterModule
    local bank = module and module.frames and module.frames[2]
    if buttons or not bank or not SlashCmdList then return end
    buttons = {}
    for _, spec in ipairs({ { "REAGENTBANKUI", "Reagents" }, { "GEARBANKCLIENT", "Gear Bank" } }) do
        local command, title = spec[1], spec[2]
        if SlashCmdList[command] then
            local button = CreateFrame("Button", "DrakeishUI_Bagster" .. command, bank, "UIPanelButtonTemplate")
            button:SetSize(96, 22)
            button:SetText(title)
            if #buttons == 0 then button:SetPoint("TOPLEFT", bank, "BOTTOMLEFT", 4, -4)
            else button:SetPoint("LEFT", buttons[#buttons], "RIGHT", 6, 0) end
            button:SetScript("OnClick", function() SlashCmdList[command]("") end)
            buttons[#buttons + 1] = button
        end
    end
end
local events = CreateFrame("Frame")
events:RegisterEvent("BANKFRAME_OPENED")
events:SetScript("OnEvent", addBankButtons)
