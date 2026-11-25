local addonName, addon = ...
local S = { options = {}, byID = {}, effective = {}, pending = {} }
addon.Settings = S

local db, character
local function copy(value)
    if type(value) ~= "table" then return value end
    local result = {}
    for key, entry in pairs(value) do result[key] = entry end
    return result
end

function S:Register(option)
    assert(type(option.id) == "string" and not self.byID[option.id], "Duplicate settings ID")
    self.byID[option.id] = option
    self.options[#self.options + 1] = option
end

local function privateProfile()
    return db.characters[character]
end

local function activeProfile()
    local name = privateProfile().shared
    return name and db.shared[name] or privateProfile().profile
end

local function validate(option, value)
    if option.validate then return option.validate(value) end
    if type(value) ~= type(option.default) then return nil end
    if option.type == "slider" then
        if value ~= value or math.abs(value) == math.huge then return nil end
        local step = option.step or 1
        value = option.min + math.floor((value - option.min) / step + .5) * step
        return math.max(option.min, math.min(option.max, value))
    end
    if option.type == "colour" then
        for i = 1, 3 do
            local channel = value[i]
            if type(channel) ~= "number" or not (channel >= 0 and channel <= 1) then return nil end
        end
        return { value[1], value[2], value[3] }
    end
    if option.type == "text" and #value > (option.maxLength or 64) then return nil end
    return value
end

local function desired(option)
    local value = validate(option, activeProfile()[option.id])
    if value == nil then return option.default end
    return value
end

function S:Refresh()
    if self.onRefresh then self.onRefresh() end
end

local function apply(option, value, callbacks)
    local id = option.id
    if option.protected and InCombatLockdown() then
        S.pending[id] = true
        return
    end
    S.pending[id] = nil
    S.effective[id] = copy(value)
    if option.apply then callbacks[option.apply] = true end
end

local function finish(callbacks)
    for callback in pairs(callbacks) do callback() end
    S:Refresh()
end

local function applyProfile()
    local callbacks = {}
    for _, option in ipairs(S.options) do apply(option, desired(option), callbacks) end
    finish(callbacks)
end

local function migrate()
    local private = privateProfile()
    for _, option in ipairs(S.options) do
        local id = option.id
        if not private.migrated[id] then
            if option.legacy and private.profile[id] == nil then
                local old = validate(option, option.legacy())
                if old ~= nil then private.profile[id] = copy(old) end
            end
            private.migrated[id] = true
        end
    end
end

function S:Ready()
    if character then return true end
    if not db then return false end
    local name, realm = UnitName("player"), GetRealmName()
    if not name or name == "" or name == UNKNOWNOBJECT or not realm or realm == "" then return false end
    character = realm .. "\031" .. name
    local private = db.characters[character]
    if type(private) ~= "table" then
        private = {}
        db.characters[character] = private
    end
    if type(private.profile) ~= "table" then private.profile = {} end
    if type(private.migrated) ~= "table" then private.migrated = {} end
    if type(private.shared) ~= "string" or type(db.shared[private.shared]) ~= "table" then private.shared = nil end
    migrate()
    applyProfile()
    return true
end

function S:Desired(id)
    return desired(self.byID[id])
end

function S:Get(id)
    return self.effective[id]
end

function S:Set(id, value)
    local option = self.byID[id]
    if not character or not option then return end
    value = validate(option, value)
    if value == nil then return end
    activeProfile()[id] = copy(value)
    local callbacks = {}
    apply(option, value, callbacks)
    finish(callbacks)
end

function S:Reset(category)
    if not character then return end
    local profile = activeProfile()
    local callbacks = {}
    for _, option in ipairs(self.options) do
        if not category or option.category == category then
            profile[option.id] = nil
            apply(option, option.default, callbacks)
        end
    end
    finish(callbacks)
end

function S:Active()
    return privateProfile().shared or false
end

function S:Profiles()
    local choices = {}
    for name, profile in pairs(db.shared) do
        if type(name) == "string" and type(profile) == "table" then choices[#choices + 1] = name end
    end
    table.sort(choices, function(a, b) return a:lower() < b:lower() end)
    local result = { { value = false, name = "This Character" } }
    for _, name in ipairs(choices) do result[#result + 1] = { value = name, name = name } end
    return result
end

function S:Select(name)
    if name ~= false and type(db.shared[name]) ~= "table" then return end
    privateProfile().shared = name or nil
    applyProfile()
end

local function copyOptions(source, target)
    for _, option in ipairs(S.options) do
        target[option.id] = copy(source[option.id])
    end
end

function S:CreateShared(name)
    name = name:match("^%s*(.-)%s*$")
    if name == "" or #name > 32 or name:find("[%c|]") or name:lower() == "this character" then
        return "Enter a profile name up to 32 bytes without control characters or pipes."
    end
    for existing in pairs(db.shared) do
        if type(existing) == "string" and existing:lower() == name:lower() then
            return "A profile with that name already exists."
        end
    end
    local profile = {}
    copyOptions(activeProfile(), profile)
    db.shared[name] = profile
    self:Select(name)
end

function S:CopyFrom(name)
    local source
    if name == false then source = privateProfile().profile
    else source = db.shared[name] end
    local target = activeProfile()
    if type(source) ~= "table" or source == target then return end
    copyOptions(source, target)
    applyProfile()
end

function S:IsReady()
    return character ~= nil
end

local events = CreateFrame("Frame")
events:RegisterEvent("ADDON_LOADED")
events:RegisterEvent("PLAYER_LOGIN")
events:RegisterEvent("PLAYER_ENTERING_WORLD")
events:RegisterEvent("PLAYER_REGEN_ENABLED")
events:SetScript("OnEvent", function(self, event, name)
    if event == "ADDON_LOADED" and name == addonName then
        if type(DrakeishUI_Config) ~= "table" then DrakeishUI_Config = {} end
        local saved = DrakeishUI_Config
        if saved.version == nil then saved.version = 1 end
        if saved.version == 1 then
            if type(saved.characters) ~= "table" then saved.characters = {} end
            if type(saved.shared) ~= "table" then saved.shared = {} end
            db = saved
            S:Ready()
        end
        self:UnregisterEvent("ADDON_LOADED")
    elseif event == "PLAYER_LOGIN" or event == "PLAYER_ENTERING_WORLD" then
        if S:Ready() then
            self:UnregisterEvent("PLAYER_LOGIN")
            self:UnregisterEvent("PLAYER_ENTERING_WORLD")
        end
    elseif event == "PLAYER_REGEN_ENABLED" and next(S.pending) then
        local callbacks = {}
        for _, option in ipairs(S.options) do
            if S.pending[option.id] then apply(option, desired(option), callbacks) end
        end
        finish(callbacks)
    end
end)
