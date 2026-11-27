-- From the Ashes native integration. Shared panel services; no replacement bags,
-- nameplates, boss mods, transmogrification, or orb implementation is loaded here.
local addon = select(2, ...)
addon.L = addon.L or setmetatable({}, { __index = function(_, key) return key end })
addon.LO = addon.LO or addon.L

local defaults = {
    worldmap = { enabled = true, questLog = true, maximized = false, objectives = true,
        landmarks = true, fog = true, entrances = true, graveyards = true,
        flightPoints = true, inns = true, coordinates = true, zoneLevels = true,
        fadeWhenMoving = false, moveAlpha = 0.4 },
    collections = { enabled = true },
    characterpanel = { enabled = true, class_portrait = true, class_level_text = true,
        dark_background = false, grey_model_backdrop = true, hide_model_controls = false,
        model_controls_reset_only = false, skills_hide_maxed = false, rep_standing = 0,
        rep_hide_inactive = false, currency_hide_empty = false, show_item_level = true,
        show_gear_score = false },
    itemlevel = { enabled = true, font_size = 12, font_family = "expressway",
        font_outline = "THICKOUTLINE", position = "BOTTOM", show_average = true,
        tooltip_cvar = false, bags = true, bank = true, guildbank = true,
        character = true, inspect = true, merchant = true, trade = true,
        loot = true, lootroll = true, mail = true, auction = true },
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

local activeDB
local function ensureDB(force)
    -- Keep the existing SavedVariable name and all unknown/legacy keys. No migration
    -- discards user settings, and disabled/unrelated modules are never force-enabled.
    if type(DrakeishUI_BagDB) ~= "table" then DrakeishUI_BagDB = {} end
    local db = DrakeishUI_BagDB
    if activeDB == db and not force then return db end
    if type(db.profile) ~= "table" then db.profile = {} end
    if type(db.profile.modules) ~= "table" then db.profile.modules = {} end
    fill(db.profile.modules, defaults)
    if type(db.global) ~= "table" then db.global = {} end
    if type(db.chars) ~= "table" then db.chars = {} end
    local name, realm = UnitName and UnitName("player"), GetRealmName and GetRealmName()
    if name and realm then
        local key = realm .. "-" .. name
        if type(db.chars[key]) ~= "table" then db.chars[key] = {} end
        db.char = db.chars[key]
    elseif type(db.char) ~= "table" then db.char = {} end
    addon.db = db
    activeDB = db
    return db
end

function addon:ExternalCharacterPanel()
    if type(IsAddOnLoaded) ~= "function" then return false end
    -- OptionalDeps makes an enabled external owner load before this LoadOnDemand UI.
    for _, name in ipairs({ "Transmogrification", "Transmogrification-2.8.0" }) do
        if IsAddOnLoaded(name) then return true end
    end
    return false
end

function addon:GetModuleConfig(name)
    return ensureDB().profile.modules[name]
end
function addon:IsModuleEnabled(name)
    local config = self:GetModuleConfig(name)
    return type(config) == "table" and config.enabled ~= false
end
function addon:RegisterModule(name, module)
    self._modules = self._modules or {}
    self._modules[name] = module
    return module
end
function addon:ShouldDeferModuleDisable() return true end
addon.UpdateItemLevelSlot = addon.UpdateItemLevelSlot or function() end

ensureDB()
-- Saved settings load after the files and replace this table; shipped_defaults.lua compares the two.
if addon.ShippedDefaults then addon.ShippedDefaults.freshBags = DrakeishUI_BagDB end
local boot = CreateFrame("Frame")
boot:RegisterEvent("ADDON_LOADED")
boot:RegisterEvent("PLAYER_LOGIN")
boot:SetScript("OnEvent", function(self, event, name)
    if event == "PLAYER_LOGIN" or name == "DrakeishUI" then ensureDB(true) end
    if event == "PLAYER_LOGIN" then self:UnregisterAllEvents() end
end)
