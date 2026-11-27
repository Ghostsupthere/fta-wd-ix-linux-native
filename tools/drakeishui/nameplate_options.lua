-- Nameplate settings in the DrakeishUI window. The plates are Kui Nameplates (modules\Kui_Nameplates); this
-- file builds one settings row for each entry in Kui's own options table and writes through Kui's handlers, so
-- changes apply the same way they did in Kui's window. Kui keeps its values in KuiNameplatesGDB.
local addonName, addon = ...
local S = addon.Settings
if not S then return end

local CATEGORY = "Nameplates"
local STYLE_PAGE = "Style"
local MODES = { selected = 1, all = 2, blizzard = 3 }
local built

local function kui()
    local nameplates = _G.KuiNameplates
    if nameplates and nameplates.db then return nameplates end
end

-- Style ------------------------------------------------------------------------------------------------------
local function applyMode()
    _G.DrakeishUI_NameplateMode = MODES[S:Get("nameplates.mode")] or MODES.all
    local nameplates = kui()
    if nameplates and nameplates.frameList then nameplates:ApplyDrakeishNameplateMode() end
end
-- Until the profile is read, plates created early use the default style.
_G.DrakeishUI_NameplateMode = MODES.all

S:Register({
    id = "nameplates.mode", name = "Nameplate style", type = "dropdown", default = "all",
    category = CATEGORY, page = STYLE_PAGE, group = "Style", keywords = "kui blizzard stock enemy health bars",
    desc = "Selected unit only hides the other plates; the Frame fading page controls how.",
    choices = {
        { value = "all", name = "All units" },
        { value = "selected", name = "Selected unit only" },
        { value = "blizzard", name = "Blizzard (original)" },
    },
    validate = function(value) if MODES[value] then return value end end,
    apply = applyMode,
})
S:Register({
    id = "nameplates.height", name = "Height offset", type = "slider", default = 0, min = -100, max = 150, step = 1,
    category = CATEGORY, page = STYLE_PAGE, group = "Style", keywords = "kui vertical position move up down above",
    desc = "Positive values move the bars higher. The clickable area stays where the original plate is.",
    localGet = function() local n = kui(); return n and n.db.profile.general.offsety end,
    localSet = function(value)
        local n = kui()
        if not n then return end
        n.db.profile.general.offsety = value or 0
        if n.frameList then n:ApplyHeightOffset() end
    end,
})

-- Kui's options ----------------------------------------------------------------------------------------------
local function plain(text, fallback)
    if type(text) ~= "string" then return fallback end
    text = text:gsub("|c%x%x%x%x%x%x%x%x", ""):gsub("|r", ""):gsub("^%s+", ""):gsub("%s+$", "")
    return text ~= "" and text or fallback
end

local function sortedEntries(args)
    local list = {}
    for key, entry in pairs(args) do list[#list + 1] = { key = key, entry = entry } end
    table.sort(list, function(a, b)
        local x, y = tonumber(a.entry.order) or 100, tonumber(b.entry.order) or 100
        if x ~= y then return x < y end
        return tostring(a.key) < tostring(b.key)
    end)
    return list
end

local function copy(value)
    if type(value) ~= "table" then return value end
    return { unpack(value) }
end

-- Mirrors Kui's handler: the first path parts that exist in the profile lead to the value's table.
local function walkProfile(profile, path)
    local child
    for i = 1, #path - 1 do
        if not child then child = profile[path[i]] else child = child[path[i]] end
    end
    return child or profile, path[#path]
end

local function mediaChoices(source)
    local values = source.values
    if type(values) == "function" then values = values() end
    local result = {}
    local media = source.dialogControl == "LSM30_Font" and "font"
        or source.dialogControl == "LSM30_Statusbar" and "statusbar"
    local library = media and LibStub("LibSharedMedia-3.0", true)
    if library then
        for _, name in ipairs(library:List(media)) do
            result[#result + 1] = { value = name, name = name, path = media == "font" and library:Fetch(media, name) or nil }
        end
        return result
    end
    local keys = {}
    for key in pairs(values or {}) do keys[#keys + 1] = key end
    table.sort(keys, function(a, b)
        if type(a) == "number" and type(b) == "number" then return a < b end
        return tostring(a) < tostring(b)
    end)
    for _, key in ipairs(keys) do
        result[#result + 1] = { value = key, name = plain(values[key], tostring(key)) }
    end
    return result
end

local function addRow(entry, handler, path, page, group)
    local owner = handler.dbPath
    path = { unpack(path) } -- the walker keeps reusing its own list
    -- Kui lists the text positions under sub-groups but stores them directly under "text"; its change
    -- callbacks, which receive the listed path, write them there.
    local stored = path
    if path[1] == "text" and #path == 3 then stored = { "text", path[3] } end
    local function value(profile)
        local parent, key = walkProfile(profile, stored)
        return parent[key]
    end
    local defaults = owner.db.defaults and owner.db.defaults.profile
    local default = defaults and value(defaults)
    if default == nil then return end
    local info = { type = entry.type }
    for i, part in ipairs(path) do info[i] = part end

    local option = {
        id = "nameplates.kui." .. table.concat(path, "."):gsub("%s+", "_"),
        name = plain(entry.name, tostring(path[#path])),
        desc = plain(entry.desc),
        category = CATEGORY, page = page, group = group, keywords = "kui nameplate " .. page .. " " .. group,
        localGet = function()
            local current = value(owner.db.profile)
            if entry.type == "color" and current then return { current[1], current[2], current[3] } end
            return current
        end,
    }
    local function write(new)
        if entry.type == "color" then
            -- The settings window picks red, green and blue; keep whatever alpha Kui had.
            local current = value(owner.db.profile) or default
            local alpha = new[4] or current[4]
            handler:Set(info, new[1], new[2], new[3], alpha)
        else
            handler:Set(info, new)
        end
    end
    option.localSet = function(new)
        if new == nil then new = copy(default) end
        write(new)
    end

    if entry.type == "toggle" then
        option.type, option.default = "toggle", not not default
        option.localGet = function() return not not value(owner.db.profile) end
    elseif entry.type == "color" then
        option.type, option.default = "colour", { default[1], default[2], default[3] }
    elseif entry.type == "range" then
        local minimum = entry.softMin or entry.min or 0
        local maximum = entry.softMax or entry.max or 100
        local step = entry.step or entry.bigStep or 1
        if maximum - minimum <= 5 then step = .01 end
        if path[#path] == "fontscale" then minimum = .5 end
        option.type, option.default, option.min, option.max, option.step = "slider", default, minimum, maximum, step
    elseif entry.type == "select" then
        option.type = entry.dialogControl == "LSM30_Font" and "font" or "dropdown"
        option.default = default
        option.choices = function() return mediaChoices(entry) end
    else
        return
    end
    if not S.byID[option.id] then S:Register(option) end
end

local function walk(args, handler, path, page, group)
    for _, item in ipairs(sortedEntries(args)) do
        local entry = item.entry
        path[#path + 1] = item.key
        if entry.type == "group" then
            walk(entry.args, entry.handler or handler, path, page, plain(entry.name, group))
        else
            addRow(entry, handler, path, page, group)
        end
        path[#path] = nil
    end
end

local function build()
    local nameplates = kui()
    if built or not nameplates then return end
    local registry = LibStub("AceConfigRegistry-3.0", true)
    local getter = registry and registry:GetOptionsTable("kuinameplates")
    if not getter then return end
    local root = getter("dialog", "DrakeishUI-1.0")
    for _, item in ipairs(sortedEntries(root.args)) do
        local entry = item.entry
        -- Kui's profile switching is left out: these settings follow Kui's own per-character profile.
        if entry.type == "group" and item.key ~= "profiles" then
            local page = plain(entry.name, tostring(item.key))
            walk(entry.args, entry.handler or root.handler, { item.key }, page, "General")
        end
    end
    built = true
    applyMode()
    S:Refresh()
end

-- /knp used to open Kui's own window.
if SlashCmdList then
    SlashCmdList.KUINAMEPLATES = function() S:Open(CATEGORY) end
end

-- Kui initialises on ADDON_LOADED before this frame hears it; entering the world is the fallback.
local events = CreateFrame("Frame")
events:RegisterEvent("ADDON_LOADED")
events:RegisterEvent("PLAYER_ENTERING_WORLD")
events:SetScript("OnEvent", function(self, event, name)
    if event == "ADDON_LOADED" and name ~= addonName then return end
    build()
    if built then self:UnregisterAllEvents() end
end)
