local _, addon = ...
local S = addon.Settings

local function display(value)
    if type(value) == "table" then
        return ("%02x%02x%02x"):format(math.floor(value[1]*255+.5), math.floor(value[2]*255+.5), math.floor(value[3]*255+.5))
    end
    if type(value) == "boolean" then return value and "on" or "off" end
    return tostring(value)
end

local function parse(option, value)
    if option.type == "toggle" then
        value = value:lower()
        if value == "true" or value == "1" or value == "on" then return true end
        if value == "false" or value == "0" or value == "off" then return false end
    elseif option.type == "slider" then
        return tonumber(value)
    elseif option.type == "colour" then
        local r,g,b = value:match("^(%x%x)(%x%x)(%x%x)$")
        if r then return { tonumber(r,16)/255, tonumber(g,16)/255, tonumber(b,16)/255 } end
    elseif option.choices then
        local choices = type(option.choices) == "function" and option.choices() or option.choices
        for _, choice in ipairs(choices) do
            if tostring(choice.value):lower() == value:lower() or choice.name:lower() == value:lower() then
                return choice.value
            end
        end
    elseif option.type == "text" then
        return value
    end
end

function S:HandleCommand(message)
    local command, rest = message:match("^%s*(%S+)%s*(.-)%s*$")
    if not command then return false end
    command = command:lower()
    if command ~= "get" and command ~= "set" and command ~= "list" then return false end
    if not self:Ready() then print("DrakeishUI settings are unavailable."); return true end
    local id, value = rest:match("^(%S+)%s*(.*)$")
    local option = id and self.byID[id]
    if command == "list" or not option then
        local filter = command == "list" and rest:lower() or ""
        for _, item in ipairs(self.options) do
            if filter == "" or item.category:lower() == filter then
                print(item.id .. " (" .. item.type .. ") = " .. display(self:Get(item.id)))
            end
        end
        return true
    end
    if command == "set" then
        local parsed = parse(option, value)
        if parsed == nil or not self:Set(id, parsed) then
            print("Invalid value for " .. id .. ".")
            return true
        end
    end
    print(id .. " = " .. display(self:Get(id)) .. (self.pending[id] and " (change pending)" or ""))
    return true
end
