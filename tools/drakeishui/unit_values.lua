local _, addon = ...
local S = addon.Settings
local groups = {
    { "party", "Party", "Party and Raid" }, { "raid", "Full raid cells", "Party and Raid", true },
    { "raidThin", "Thin raid cells", "Party and Raid", true }, { "pet", "Pet", "Unit Frames" },
    { "target", "Target", "Unit Frames" }, { "targettarget", "Target of target", "Unit Frames" },
    { "focus", "Focus", "Unit Frames" }, { "boss", "Boss", "Unit Frames" }, { "arena", "Arena", "Unit Frames" },
}
local entries, byFrame = {}, {}
local keys = {"health","power","format","short","separator","font","size","outline",
              "align","healthColor","powerColor","hideFull","hideZero","hideDead","hideOffline"}
local get, render
local enabled
local function refresh()
    enabled = S:Get("unit.enabled")
    for _, entry in ipairs(entries) do
        entry.style = nil
        render(entry)
    end
end
local function choices(...)
    local args, result = {...}, {}
    for i=1,#args,2 do result[#result+1] = {value=args[i], name=args[i+1]} end
    return result
end
local function register(id, name, category, group, kind, default, extra)
    local option = extra or {}
    option.id, option.name, option.category, option.group = id, name, category, group
    option.type, option.default, option.apply = kind, default, refresh
    option.keywords = "health mana power energy text " .. group
    if option.choices then
        local valid = {}
        for _, choice in ipairs(option.choices) do valid[choice.value] = true end
        option.validate = function(value) if valid[value] then return value end end
    end
    S:Register(option)
end
register("unit.enabled", "Custom unit values", "Unit Frames", "Unit values", "toggle", true)
for _, group in ipairs(groups) do
    local id, title, category, legacy = unpack(group)
    local prefix = "unit." .. id .. "."
    local function option(key, name, kind, default, extra)
        register(prefix..key, name, category, title, kind, default, extra)
    end
    local modes = choices("always","Always", "hover","Mouse-over", "never","Never", "legacy","Existing behavior")
    option("health", "Health text", "dropdown", legacy and "legacy" or "always", {choices=modes})
    option("power", "Power text", "dropdown", legacy and "legacy" or "always", {choices=modes})
    option("format", "Value format", "dropdown", "current", {choices=choices("current","Current","max","Current / max",
        "percent","Percent","both","Current + percent","deficit","Deficit")})
    option("short", "Compact numbers", "toggle", true)
    option("separator", "Value separator", "text", " / ", {maxLength=8})
    option("font", "Value font", "font", "inherit", {choices=choices("inherit","Inherit", "client","Client default","expressway","Expressway")})
    option("size", "Value font size", "slider", legacy and 9 or 11, {min=8,max=24,step=1})
    option("outline", "Value outline", "dropdown", "OUTLINE", {choices=choices("","None","OUTLINE","Outline","THICKOUTLINE","Thick outline")})
    option("align", "Value position", "dropdown", "CENTER", {choices=choices("LEFT","Left","CENTER","Center","RIGHT","Right")})
    option("healthColor", "Health class color", "toggle", false)
    option("powerColor", "Color power by resource", "toggle", true)
    option("hideFull", "Hide at full", "toggle", false)
    option("hideZero", "Hide at zero", "toggle", false)
    option("hideDead", "Hide when dead", "toggle", false)
    option("hideOffline", "Hide when offline", "toggle", false)
end

local function short(value)
    local abs = math.abs(value)
    if abs >= 1000000 then return ("%.1fm"):format(value/1000000) end
    if abs >= 1000 then return ("%.1fk"):format(value/1000) end
    return tostring(value)
end

local function valueText(current, maximum, style)
    local format = style.format
    local number = style.short and short or tostring
    local percent = math.floor(current/maximum*100+.5)
    if format == "max" then return number(current)..style.separator..number(maximum) end
    if format == "percent" then return percent.."%" end
    if format == "both" then return number(current)..style.separator..percent.."%" end
    if format == "deficit" then return number(current-maximum) end
    return number(current)
end

local function styleFor(entry)
    local group = entry.group
    if group == "raid" and DrakeishUI_GetRaidFrameStyle and DrakeishUI_GetRaidFrameStyle() == 2 then group = "raidThin" end
    if entry.style and entry.style.group == group then return entry.style end
    local style = { group = group }
    local prefix = "unit."..group.."."
    for _, key in ipairs(keys) do
        style[key] = S:Get(prefix..key)
    end
    entry.style = style
    return style
end

local function stock(entry, key, suppress)
    local fonts = entry[key.."Stock"] or {}
    for _, font in ipairs(fonts) do
        local alpha = entry.alpha[font]
        if suppress and alpha == nil then
            entry.alpha[font] = font:GetAlpha()
            font:SetAlpha(0)
        elseif not suppress and alpha ~= nil then
            font:SetAlpha(alpha)
            entry.alpha[font] = nil
        end
    end
end

local function valueRegion(entry, key, bar)
    local fs = entry[key.."Text"]
    if fs then return fs end
    local overlay = CreateFrame("Frame", nil, entry.frame)
    overlay:EnableMouse(false)
    overlay:SetAllPoints(bar)
    overlay:SetFrameLevel(math.max(bar:GetFrameLevel(), entry.frame:GetFrameLevel())+8)
    fs = overlay:CreateFontString(nil,"OVERLAY","GameFontHighlightSmall")
    if fs.SetWordWrap then fs:SetWordWrap(false) end
    if fs.SetNonSpaceWrap then fs:SetNonSpaceWrap(false) end
    entry[key.."Text"] = fs
    return fs
end

local function paint(entry, key, style, unit, blocked)
    local bar = entry[key]
    if not bar then return end
    local mode = enabled and style[key] or "legacy"
    stock(entry, key, mode ~= "legacy")
    local fs = entry[key.."Text"]
    if blocked or mode == "never" or mode == "legacy" or (mode == "hover" and not entry.hover) or not bar:IsVisible() then
        if fs then fs:Hide() end
        return
    end
    local current, maximum
    if key == "health" then current, maximum = UnitHealth(unit), UnitHealthMax(unit)
    else current, maximum = UnitPower(unit), UnitPowerMax(unit) end
    if not maximum or maximum <= 0 or (style.hideFull and current >= maximum) or (style.hideZero and current == 0) then
        if fs then fs:Hide() end
        return
    end
    fs = valueRegion(entry, key, bar)
    local color
    if key == "health" and style.healthColor then
        local _, class = UnitClass(unit)
        color = RAID_CLASS_COLORS and RAID_CLASS_COLORS[class]
    elseif key == "power" and style.powerColor and PowerBarColor then
        local index, token = UnitPowerType(unit)
        color = PowerBarColor[token] or PowerBarColor[index]
    end
    local height, width = bar:GetHeight(), bar:GetWidth()
    local anchor, x, y = "CENTER", 0, 0
    if entry.raid then
        width = width*.46
        height = height/2
        anchor, x, y = "RIGHT", -2, key == "health" and height/2 or -height/2
    end
    local size = math.max(5, math.min(style.size, math.floor(height+1)))
    width = math.max(1, width-4)
    local look = entry[key.."Look"]
    local appearance = addon.Appearance
    local font = style.font == "expressway" and addon._dir.."expressway.ttf" or STANDARD_TEXT_FONT
    local revision = appearance and appearance.revision
    if not look or look.style ~= style or look.w ~= width or look.h ~= height or look.rev ~= revision then
        fs:ClearAllPoints()
        fs:SetPoint(anchor, fs:GetParent(), anchor, x, y)
        fs:SetJustifyH(style.align)
        fs:SetHeight(height)
        entry[key.."Look"] = { style=style,w=width,h=height,rev=revision }
        look = entry[key.."Look"]
    end
    local valueChanged=look.current~=current or look.maximum~=maximum
    if valueChanged then
        local value=valueText(current,maximum,style)
        fs:SetWidth(0)
        if appearance and style.font == "inherit" then
            size = appearance:ApplyFont(fs,"units",font,size,style.outline,math.floor(height+1))
        else fs:SetFont(font,size,style.outline) end
        fs:SetText(value)
        local measured = fs:GetStringWidth()
        if measured > width then
            local fit = math.max(4,math.floor(size*width/measured))
            local actualFont, _, flags = fs:GetFont()
            fs:SetFont(actualFont or font,fit,flags or style.outline)
        end
        fs:SetWidth(width)
        look.current,look.maximum=current,maximum
    end
    local r,g,b
    if color then r,g,b=color.r,color.g,color.b end
    if look.r~=r or look.g~=g or look.b~=b or look.changed==nil then
        if color then fs:SetTextColor(r,g,b)
        else
            fs:SetTextColor(1,1,1)
            if appearance then appearance:ApplyColor(fs,"units") end
        end
        look.r,look.g,look.b,look.changed=r,g,b,true
    end
    fs:Show()
end

render = function(entry)
    local unit = entry.unit or entry.frame.unit
    local style = styleFor(entry)
    local blocked = not entry.frame:IsVisible() or not unit or not UnitExists(unit) or
        (style.hideDead and UnitIsDeadOrGhost(unit)) or (style.hideOffline and not UnitIsConnected(unit))
    paint(entry,"health",style,unit,blocked)
    paint(entry,"power",style,unit,blocked)
    local name=entry.raid and entry.frame.nameText
    if name then
        local custom=enabled and ((style.health~="legacy" and style.health~="never") or
                                  (style.power~="legacy" and style.power~="never"))
        local layout=custom and "custom" or "existing"
        if entry.nameLayout~=layout or entry.nameGroup~=style.group then
            if custom or entry.nameLayout=="custom" or entry.redoName then
                name:ClearAllPoints()
                if custom then
                    name:SetPoint("TOPLEFT",entry.health,"TOPLEFT",3,-2)
                    name:SetPoint("RIGHT",entry.health,"CENTER",-3,0)
                elseif DrakeishUI_GetRaidFrameStyle and DrakeishUI_GetRaidFrameStyle()==2 then
                    name:SetPoint("LEFT",entry.health,"LEFT",3,0)
                    name:SetPoint("RIGHT",entry.health,"RIGHT",-3,0)
                else
                    name:SetPoint("TOPLEFT",entry.health,"TOPLEFT",3,-2)
                    name:SetPoint("RIGHT",entry.frame.healthText,"LEFT",-2,0)
                end
            end
            entry.nameLayout,entry.nameGroup,entry.redoName=layout,style.group,nil
        end
    end
end

local function adopt(frame, unit, group, health, power, healthStock, powerStock, raid)
    if not frame or not health or byFrame[frame] then return end
    local entry = { frame=frame,unit=unit,group=group,health=health,power=power,raid=raid,
        healthStock=healthStock,powerStock=powerStock,alpha={} }
    entries[#entries+1], byFrame[frame] = entry, entry
    frame:HookScript("OnEnter",function() entry.hover=true; if S:IsReady() then render(entry) end end)
    frame:HookScript("OnLeave",function() entry.hover=false; if S:IsReady() then render(entry) end end)
    frame:HookScript("OnShow",function() if S:IsReady() then render(entry) end end)
    if S:IsReady() then render(entry) end
end

local function stockFonts(frame, bar, kind)
    local found = {}
    local base = bar and bar:GetName()
    for _, suffix in ipairs({"Text","TextLeft","TextRight"}) do
        local fs = (base and _G[base..suffix]) or frame[kind..suffix] or (bar and bar[suffix])
        if fs then found[#found+1] = fs end
        local name = frame:GetName()
        local extra = name and _G[name.."TextureFrame"..kind..suffix]
        if extra and extra ~= fs then found[#found+1] = extra end
    end
    local custom = frame[frame:GetName()..kind.."Text"]
    if custom then found[#found+1] = custom end
    return found
end

local function named(name, unit, group)
    local f = _G[name]
    if not f or byFrame[f] then return end
    local health = _G[name.."HealthBar"] or f.healthbar or f.healthBar
    local power = _G[name.."ManaBar"] or f.manabar or f.powerBar
    if health then adopt(f,unit,group,health,power,stockFonts(f,health,"HealthBar"),stockFonts(f,power,"ManaBar")) end
end

local function discover()
    named("TargetFrame","target","target")
    named("TargetFrameToT","targettarget","targettarget")
    named("FocusFrame","focus","focus")
    named("PetFrame","pet","pet")
    for i=1,5 do
        named("Boss"..i.."TargetFrame","boss"..i,"boss")
        named("ArenaEnemyFrame"..i,"arena"..i,"arena")
    end
    for i=1,4 do
        local f = _G["CustomPartyFrame"..i]
        if f then adopt(f,"party"..i,"party",f.healthBar,f.powerBar,{f.healthText},{f.powerText}) end
    end
    for i=1,41 do
        local f = _G["DrakeishUI_RaidUnit"..(i==41 and "Pet" or i)]
        if f then adopt(f,nil,i==41 and "pet" or "raid",f.health,f.health,{f.healthText},{f.powerText},true) end
    end
end

local events = CreateFrame("Frame")
for _, event in ipairs({"PLAYER_ENTERING_WORLD","ADDON_LOADED","PLAYER_TARGET_CHANGED","PLAYER_FOCUS_CHANGED",
    "PARTY_MEMBERS_CHANGED","RAID_ROSTER_UPDATE","UNIT_PET","INSTANCE_ENCOUNTER_ENGAGE_UNIT","ARENA_OPPONENT_UPDATE",
    "UNIT_HEALTH","UNIT_MAXHEALTH","UNIT_MANA","UNIT_RAGE","UNIT_ENERGY","UNIT_FOCUS","UNIT_RUNIC_POWER",
    "UNIT_MAXMANA","UNIT_MAXRAGE","UNIT_MAXENERGY","UNIT_MAXFOCUS","UNIT_MAXRUNIC_POWER",
    "UNIT_DISPLAYPOWER","UNIT_CONNECTION","UNIT_TARGET","PLAYER_FLAGS_CHANGED"}) do pcall(events.RegisterEvent,events,event) end
events:SetScript("OnEvent",function(_,event,unit)
    if not S:IsReady() then return end
    local unitEvent=event:match("^UNIT_")
    if not unitEvent or event=="UNIT_PET" then discover() end
    for _, entry in ipairs(entries) do
        local token = entry.unit or entry.frame.unit
        if not unitEvent or token==unit or event=="UNIT_PET" or
            (event=="UNIT_TARGET" and unit=="target" and token=="targettarget") then render(entry) end
    end
end)
addon.RefreshUnitValues = function() if S:IsReady() then discover(); refresh() end end
if DrakeishUI_UpdateRaidFrames then
    hooksecurefunc("DrakeishUI_UpdateRaidFrames", function()
        if not S:IsReady() then return end
        discover()
        for _, entry in ipairs(entries) do
            if entry.raid then
                if entry.nameLayout=="custom" then entry.nameLayout=nil;entry.redoName=true end
                render(entry)
            end
        end
    end)
end
