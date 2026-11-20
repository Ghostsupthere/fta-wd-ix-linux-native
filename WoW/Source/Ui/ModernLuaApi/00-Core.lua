local flags, native = ...
local unpack, type, select, error = unpack, type, select, error
local CreateFrame, UIParent, GetTime = CreateFrame, UIParent, GetTime
local pairs, rawget, rawset = pairs, rawget, rawset
local math, table = math, table
local geterrorhandler = geterrorhandler
local G = _G
local version = 1
local records, surfaces = {}, {}

local function enabled(bit)
    return flags % (bit * 2) >= bit
end

local function namespace(name)
    if type(rawget(G, name)) ~= "table" then
        if rawget(G, name) ~= nil then return end
        rawset(G, name, {})
    end
    return rawget(G, name)
end

local function expose(scope, name, implementation, group, contract)
    -- Linux port: an unavailable native must not leave an empty C_* table behind (addons test for the table).
    if type(implementation) ~= "function" then return end
    local surface = scope == "_G" and G or namespace(scope)
    if not surface then return end
    local key = scope == "_G" and name or scope .. "." .. name
    if rawget(surface, name) == nil then
        rawset(surface, name, implementation)
    end
    records[#records + 1] = { key, scope, name, implementation, group, contract }
    return rawget(surface, name) == implementation
end

local function addSurface(name, predicate, group, contract)
    surfaces[#surfaces + 1] = { name, predicate, group, contract }
end

local function getCapabilities()
    local results = {}
    for i = 1, #records do
        local entry = records[i]
        local surface = rawget(G, entry[2])
        local implementation = type(surface) == "table" and rawget(surface, entry[3])
        local owned = implementation == entry[4]
        results[entry[1]] = {
            available = type(implementation) == "function",
            source = owned and "FTA" or (type(implementation) == "function" and "external" or "unavailable"),
            group = entry[5],
            contract = owned and entry[6] or nil,
        }
    end
    for i = 1, #surfaces do
        local entry = surfaces[i]
        local available = entry[2]()
        results[entry[1]] = { available = available, source = available and "FTA" or "unavailable",
            group = entry[3], contract = entry[4] }
    end
    return results
end

local traceErrors = enabled(8)
local errorRing, nextError, errorCount = {}, 1, 0
local maxErrors = 32
local function bindCallback(callback, keepResult)
    return native.bindCallback(callback, keepResult, traceErrors)
end

local function reportError(failure, stack)
    if traceErrors then
        local message = type(failure) == "string" and failure:sub(1, 1024) or "Lua error (" .. type(failure) .. ")"
        errorRing[nextError] = { message = message, stack = stack, time = GetTime() }
        nextError = nextError % maxErrors + 1
        errorCount = math.min(errorCount + 1, maxErrors)
    end
    local handler = geterrorhandler and geterrorhandler()
    if type(handler) == "function" then native.bindCallback(handler)(failure) end
end

local function checkFinite(value, label, level)
    if type(value) ~= "number" or value ~= value or value >= math.huge or value <= -math.huge then
        error(label .. " must be a finite number", level or 3)
    end
    return value
end

expose("C_FTA", "GetModernAPIVersion", function() return version end, "core", "NATIVE_REQUIRED")
expose("C_FTA", "GetModernAPIGroups", function() return flags end, "core", "NATIVE_REQUIRED")
expose("C_FTA", "GetModernAPICapabilities", getCapabilities, "core", "NATIVE_REQUIRED")
