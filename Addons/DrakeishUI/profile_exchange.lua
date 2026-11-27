local _, addon = ...
local S = addon.Settings
local HEADER, MAX_LENGTH = "DUI1;", 16384
local function integerHex(char)
    return ("%02x"):format(char:byte())
end
local function encode(value)
    local kind=type(value)
    if kind=="boolean" then return value and "b1" or "b0" end
    if kind=="number" then return ("n%.17g"):format(value) end
    if kind=="string" then return "s"..value:gsub(".",integerHex) end
    if kind=="table" then return ("c%.17g,%.17g,%.17g"):format(value[1],value[2],value[3]) end
end
local function decode(kind,text)
    if kind=="b" then
        if text=="1" then return true end
        if text=="0" then return false end
    elseif kind=="n" and text:match("^[%+%-%.%deE]+$") then
        local n=tonumber(text)
        if n and n==n and math.abs(n)<math.huge then return n end
    elseif kind=="s" and #text%2==0 and not text:find("[^%x]") then
        return (text:gsub("(%x%x)",function(hex) return string.char(tonumber(hex,16)) end))
    elseif kind=="c" then
        local r,g,b=text:match("^([%+%-%.%deE]+),([%+%-%.%deE]+),([%+%-%.%deE]+)$")
        r,g,b=tonumber(r),tonumber(g),tonumber(b)
        if r and g and b then return {r,g,b} end
    end
end

function S:ExportProfile()
    if not self:IsReady() then return nil,"Profiles are unavailable." end
    local values=self:SnapshotProfile()
    local records={HEADER}
    for _, option in ipairs(self.options) do
        local value=values[option.id]
        if value~=nil then records[#records+1]=option.id.."="..encode(value)..";" end
    end
    local text=table.concat(records)
    if #text>MAX_LENGTH then return nil,"This profile is too large to export." end
    return text
end

function S:ImportProfile(name,text)
    if not self:IsReady() then return "Profiles are unavailable." end
    if type(text)~="string" or #text>MAX_LENGTH then return "The profile text is too large or invalid." end
    if text:sub(1,#HEADER)~=HEADER then return "Unsupported profile text version." end
    local values, offset={},#HEADER+1
    while offset<=#text do
        local finish=text:find(";",offset,true)
        if not finish then return "Incomplete profile text." end
        local id,kind,raw=text:sub(offset,finish-1):match("^([%w_.]+)=([bnsc])(.*)$")
        if not id or values[id]~=nil then return "Invalid or duplicate profile entry." end
        local value=decode(kind,raw)
        local validated=self:ValidateProfileValue(id,value)
        if validated==nil or (type(value)~="table" and validated~=value) then
            return "Invalid value for profile option "..id.."."
        end
        values[id]=validated
        offset=finish+1
    end
    return self:InstallImportedProfile(name,values)
end
