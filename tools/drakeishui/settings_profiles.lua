local addonName, addon = ...
local S = { options = {}, byID = {}, effective = {}, pending = {} }
addon.Settings = S

local db, character
local localPending = {}
local loginSetup -- defined with the rest of the setup code below
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
    if option.type == "text" and (#value > (option.maxLength or 64) or value:find("[%c|]")) then return nil end
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
    for _, option in ipairs(S.options) do
        if not option.localGet then apply(option, desired(option), callbacks) end
    end
    finish(callbacks)
end

local function migrate()
    local private = privateProfile()
    for _, option in ipairs(S.options) do
        local id = option.id
        if not option.localGet and not private.migrated[id] then
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
    loginSetup()
    applyProfile()
    return true
end

function S:Desired(id)
    local option = self.byID[id]
    if option.localGet then return self:Get(id) end
    return desired(option)
end

function S:Get(id)
    local option=self.byID[id]
    if option and option.localGet then
        local value=validate(option,option.localGet())
        if value==nil then return copy(option.default) end
        return value
    end
    return self.effective[id]
end

local function setLocal(option,value)
    local id=option.id
    if option.protected and InCombatLockdown() then
        localPending[id]={value=value}
        S.pending[id]=true
    else
        option.localSet(value)
        S.pending[id]=nil
    end
end

-- FTA addition: a "setup" is everything a profile holds besides its own settings: the layout (frame positions
-- and scales, bar sizes, bags, screen messages, chat size) and the settings that are saved elsewhere
-- (nameplates, raid and pet frames, optional action bars). A shared profile's setup is db.setups[name].
-- While a character uses one, private.using holds that name, the character's own storage holds a working
-- copy, and private.setup keeps the character's own setup for going back to "This Character".
local LAYOUT_SKIP = { excludeSort = true, settingsPage = true, chatClassColors = true } -- stay with the character
local loginOptions -- setup options still to be applied after logging in

local function deep(value)
    if type(value) ~= "table" then return value end
    local result = {}
    for key, entry in pairs(value) do result[key] = deep(entry) end
    return result
end

local function same(a, b)
    if type(a) ~= "table" or type(b) ~= "table" then return a == b end
    for key, entry in pairs(a) do if not same(entry, b[key]) then return false end end
    for key in pairs(b) do if a[key] == nil then return false end end
    return true
end

local function carried(option)
    return option.localGet ~= nil and (option.carry or option.category == "Nameplates")
end

local function captureLayout()
    local layout = addon.Layout
    if not layout then return nil end
    local result = {}
    for key, value in pairs(layout:DB()) do
        if not LAYOUT_SKIP[key] then result[key] = deep(value) end
    end
    return result
end

local function loadLayout(source)
    local layout = addon.Layout
    if not layout or type(source) ~= "table" then return end
    local target, stale = layout:DB(), {}
    for key in pairs(target) do
        if not LAYOUT_SKIP[key] then stale[#stale + 1] = key end
    end
    for _, key in ipairs(stale) do target[key] = nil end
    for key, value in pairs(source) do
        if not LAYOUT_SKIP[key] then target[key] = deep(value) end
    end
    layout:DB() -- restores anything the source lacks
end

-- A value that cannot be read (its module did not load) keeps what the setup already holds.
local function captureOptions(previous)
    local result = deep(previous or {})
    for _, option in ipairs(S.options) do
        if carried(option) then
            local value = S:Get(option.id)
            local queued = localPending[option.id]
            if queued and queued.value ~= nil then value = queued.value end
            if value ~= nil then result[option.id] = deep(value) end
        end
    end
    return result
end

local function loadOptions(values)
    if type(values) ~= "table" then return end
    for _, option in ipairs(S.options) do
        local value = values[option.id]
        if value ~= nil and carried(option) then
            value = validate(option, value)
            if value ~= nil and not same(value, S:Get(option.id)) then
                -- One setting that fails must not leave the rest of the profile unapplied.
                local ok, err = pcall(setLocal, option, deep(value))
                if not ok and geterrorhandler then geterrorhandler()(err) end
            end
        end
    end
end

local function capture(previous)
    local layout = captureLayout()
    if not layout then return nil end
    -- Until the login values are applied, the character's current ones are not the profile's.
    local options = previous and previous.options
    if not loginOptions then options = captureOptions(options) end
    return { layout = layout, options = deep(options or {}) }
end

-- Writes the working copy back to the shared profile it came from.
local function sync(layoutOnly)
    local name = privateProfile().using
    local setup = name and db.setups[name]
    if not setup then return end
    if layoutOnly then setup.layout = captureLayout() or setup.layout
    else db.setups[name] = capture(setup) or setup end
end

local function startUsing(name)
    local private = privateProfile()
    if not private.using then private.setup = capture() end
    private.using = name
end

-- Loads a setup into the character's storage. Positions only move after the interface reloads.
local function loadSetup(setup)
    if not setup then return end
    local before = captureLayout()
    loadLayout(setup.layout)
    loadOptions(setup.options)
    if not same(before, captureLayout()) then S.reloadNeeded = true end
end

-- Puts the setup that goes with `name` (false: This Character) in place.
local function useSetup(name)
    local private = privateProfile()
    sync() -- first: it replaces the stored setup of the profile in use, which may be the one picked
    local target = name and db.setups[name]
    if target then
        startUsing(name)
        loadSetup(target)
    elseif private.using then
        local own = private.setup
        private.using, private.setup = nil, nil
        loadSetup(own)
    end
end

-- True for a shared profile saved before profiles held a setup.
function S:NeedsLayout()
    local name = character and self:Active()
    return not not (name and not db.setups[name] and addon.Layout)
end

function S:SaveLayout()
    if not self:NeedsLayout() then return end
    local name = privateProfile().shared
    local setup = capture()
    startUsing(name)
    db.setups[name] = setup
    self:Refresh()
end

function loginSetup()
    local private = privateProfile()
    local target = private.shared and db.setups[private.shared]
    if target then
        if not private.using then
            -- The other settings are added once they can be read, before the profile's replace them.
            private.setup = { layout = captureLayout() }
            private.using = private.shared
        end
    elseif private.using then
        target = private.setup
        private.using, private.setup = nil, nil
    end
    if not target then return end
    loadLayout(target.layout)
    loginOptions = target.options or {}
end

local function finishLogin()
    local values = loginOptions
    if not values then return end
    local own = privateProfile().using and privateProfile().setup
    if own and not own.options then own.options = captureOptions() end
    loginOptions = nil
    loadOptions(values)
    S:Refresh()
end

function S:Set(id, value)
    local option = self.byID[id]
    if not character or not option then return end
    value = validate(option, value)
    if value == nil then return end
    local callbacks = {}
    if option.localGet then setLocal(option,copy(value))
    else
        activeProfile()[id] = copy(value)
        apply(option, value, callbacks)
    end
    finish(callbacks)
    return true
end

function S:Reset(category, includeLocal)
    if not character then return end
    local profile = activeProfile()
    local callbacks = {}
    for _, option in ipairs(self.options) do
        if not category or option.category == category then
            if option.localGet then
                if category or includeLocal then setLocal(option,nil) end
            else
                profile[option.id] = nil
                apply(option, option.default, callbacks)
            end
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
    if InCombatLockdown() and (db.setups[name] or privateProfile().using) then
        return "Leave combat before switching profiles."
    end
    useSetup(name)
    privateProfile().shared = name or nil
    applyProfile()
end

local function copyOptions(source, target)
    for _, option in ipairs(S.options) do
        if not option.localGet then target[option.id] = copy(source[option.id]) end
    end
end

local function sharedName(name)
    name = (name or ""):match("^%s*(.-)%s*$")
    if name == "" or #name > 32 or name:find("[%c|]") or name:lower() == "this character" then
        return nil, "Enter a profile name up to 32 bytes without control characters or pipes."
    end
    for existing in pairs(db.shared) do
        if type(existing) == "string" and existing:lower() == name:lower() then
            return nil, "A profile with that name already exists."
        end
    end
    return name
end

function S:CreateShared(name)
    local key, err=sharedName(name)
    if not key then return err end
    if InCombatLockdown() then return "Leave combat before creating a profile." end
    local profile={}
    copyOptions(activeProfile(), profile)
    db.shared[key]=profile
    sync()
    db.setups[key]=capture()
    self:Select(key)
end

function S:SnapshotProfile()
    local snapshot={}
    for _, option in ipairs(self.options) do
        if not option.localGet then
            local value=validate(option,activeProfile()[option.id])
            if value~=nil then snapshot[option.id]=copy(value) end
        end
    end
    return snapshot
end

function S:ValidateProfileValue(id,value)
    local option=self.byID[id]
    if option and not option.localGet then return validate(option,value) end
end

function S:InstallImportedProfile(name,values)
    local key,err=sharedName(name)
    if not key then return err end
    if InCombatLockdown() and privateProfile().using then return "Leave combat before importing a profile." end
    db.shared[key]=values
    self:Select(key)
end

function S:CopyFrom(name)
    local source
    if name == false then source = privateProfile().profile
    else source = db.shared[name] end
    local target = activeProfile()
    if type(source) ~= "table" or source == target then return end
    local private, setup = privateProfile(), nil
    if name then setup = db.setups[name]
    elseif private.using then setup = private.setup
    elseif private.shared then setup = capture() end -- the character's own setup is the one in place
    if setup and InCombatLockdown() then return "Leave combat before copying a profile." end
    copyOptions(source, target)
    if setup then
        if private.shared then
            startUsing(private.shared)
            loadSetup(setup)
            db.setups[private.shared] = capture(setup)
        else
            loadSetup(setup)
        end
    end
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
        local hadConfig = type(DrakeishUI_Config) == "table"
        if not hadConfig then DrakeishUI_Config = {} end
        local saved = DrakeishUI_Config
        -- Players new to DrakeishUI get the starting look from shipped_defaults.lua.
        if addon.ShippedDefaults then addon.ShippedDefaults:Start(saved, hadConfig, S.options, addon.Layout) end
        if saved.version == nil then saved.version = 1 end
        if saved.version == 1 then
            if type(saved.characters) ~= "table" then saved.characters = {} end
            if type(saved.shared) ~= "table" then saved.shared = {} end
            if type(saved.setups) ~= "table" then saved.setups = {} end
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
            local queued=localPending[option.id]
            if queued then
                localPending[option.id]=nil
                setLocal(option,queued.value)
            elseif S.pending[option.id] then apply(option, desired(option), callbacks) end
        end
        finish(callbacks)
    end
end)

local setupEvents = CreateFrame("Frame")
setupEvents:RegisterEvent("PLAYER_ENTERING_WORLD")
setupEvents:RegisterEvent("PLAYER_LEAVING_WORLD")
setupEvents:RegisterEvent("PLAYER_LOGOUT")
setupEvents:SetScript("OnEvent", function(_, event)
    if not character then return end
    if event == "PLAYER_ENTERING_WORLD" then finishLogin()
    -- Comes before every logout and reload. At PLAYER_LOGOUT the nameplate library has already cleared its
    -- values, so only the layout is read there.
    else sync(event == "PLAYER_LOGOUT") end
end)
