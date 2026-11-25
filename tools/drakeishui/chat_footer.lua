local _, addon = ...
local S = addon.Settings
local anchor, host = MinimalChatAnchor, UnifiedInfoFrame
if not anchor or not host then return end
local originalWidth, originalScale = host:GetWidth(), host:GetScale()
local originals, originalDrivers = {}, {}
for _, region in ipairs({host:GetRegions()}) do originals[region]=region:GetAlpha() end
for _, child in ipairs({host:GetChildren()}) do originalDrivers[child]=child:IsShown() end

local entries = {
    {"none","None"},{"zone","Zone"},{"time","Time"},{"itemLevel","Item Level"},{"durability","Durability"},
    {"gold","Gold"},{"bags","Bag space"},{"system","Latency and FPS"},{"memory","Memory"},
    {"coordinates","Coordinates"},{"coordsFPS","Coordinates and FPS"},{"xp","XP or reputation"},{"spec","Specialization"},
}
local function choices(values)
    local result = {}
    for _, row in ipairs(values) do result[#result+1]={value=row[1],name=row[2]} end
    return result
end
local names={}
for _, item in ipairs(entries) do names[item[1]]=item[2] end
local cfg, slots, savedCoordinates = {}, {}, nil
local refresh
local function apply() refresh() end
local function option(key,label,kind,default,extra,group)
    local o=extra or {}
    o.id,o.name,o.type,o.default="footer."..key,label,kind,default
    o.category,o.group,o.apply,o.keywords="Chat",group or "Chat footer",apply,"chat footer data text "..label
    if o.choices then
        local valid={}
        for _, choice in ipairs(o.choices) do valid[choice.value]=true end
        o.validate=function(value) if valid[value] then return value end end
    end
    S:Register(o)
    cfg[key]=default
end
option("enabled","Enable configurable chat footer","toggle",true)
option("count","Footer slot count","slider",3,{min=2,max=3,step=1})
option("time","Footer clock format","dropdown","server24",{choices=choices({
    {"server24","Server 24-hour"},{"server12","Server 12-hour"},{"local24","Local 24-hour"},{"local12","Local 12-hour"}})})
local defaultSlots={"zone","time","coordsFPS"}
for i=1,3 do
    local group="Footer slot "..i
    local prefix=i.."."
    option(prefix.."entry","Readout","dropdown",defaultSlots[i],{choices=choices(entries)},group)
    option(prefix.."font","Readout font","font","client",{choices=choices({
        {"client","Client default"},{"inherit","Inherit"},{"expressway","Expressway"},{"calibri","Calibri"}})},group)
    option(prefix.."size","Readout font size","slider",11,{min=8,max=20,step=1},group)
    option(prefix.."color","Readout color","colour",{1,.85,0},nil,group)
    option(prefix.."action","On click","dropdown","auto",{choices=choices({
        {"auto","Readout default"},{"none","None"},{"character","Open Character"},{"bags","Open bags"},
        {"map","Open map"},{"talents","Open talents"}})},group)
    option(prefix.."tooltip","Show tooltip","toggle",true,nil,group)
end

local function clock()
    local h,m
    if cfg.time:match("^local") then h,m=tonumber(date("%H")),tonumber(date("%M"))
    else h,m=GetGameTime() end
    if cfg.time:match("12$") then return ("%d:%02d %s"):format((h-1)%12+1,m,h<12 and "AM" or "PM") end
    return ("%02d:%02d"):format(h,m)
end
local function coordinates()
    if not (WorldMapFrame and WorldMapFrame:IsShown()) and SetMapToCurrentZone and GetPlayerMapPosition then
        SetMapToCurrentZone()
        local x,y=GetPlayerMapPosition("player")
        savedCoordinates=(x and y and (x>0 or y>0)) and ("%.1f, %.1f"):format(x*100,y*100) or nil
    end
    return savedCoordinates or "—"
end
local readers={
    zone=function() return GetRealZoneText() end,
    time=clock,
    itemLevel=function()
        local value=addon.GetAverageItemLevel and addon.GetAverageItemLevel("player")
        if value and value>0 then return ("iLvl %.1f"):format(value) end
        return "iLvl —"
    end,
    durability=function()
        local lowest
        for i=1,18 do
            local value,maximum=GetInventoryItemDurability(i)
            if maximum and maximum>0 then lowest=math.min(lowest or 1,value/maximum) end
        end
        return lowest and ("Dur %.0f%%"):format(lowest*100) or "Dur —"
    end,
    gold=function() return ("%dg"):format(math.floor(GetMoney()/10000)) end,
    bags=function()
        local used,total=0,0
        for i=0,NUM_BAG_SLOTS or 4 do
            used=used+(GetContainerNumFreeSlots(i) or 0)
            total=total+(GetContainerNumSlots(i) or 0)
        end
        return ("%d/%d free"):format(used,total)
    end,
    system=function() local _,_,lag=GetNetStats(); return ("%d ms · %.0f FPS"):format(lag or 0,GetFramerate()) end,
    memory=function()
        if not UpdateAddOnMemoryUsage or not GetAddOnMemoryUsage then return "Memory —" end
        UpdateAddOnMemoryUsage()
        local total=0
        for i=1,GetNumAddOns() do total=total+GetAddOnMemoryUsage(i) end
        return ("%.1f MB"):format(total/1024)
    end,
    coordinates=coordinates,
    coordsFPS=function() return ("%s · %.0f FPS"):format(coordinates(),GetFramerate()) end,
    xp=function()
        local faction,_,low,high,value=GetWatchedFactionInfo()
        if faction and high>low then return ("Rep %.0f%%"):format((value-low)*100/(high-low)) end
        local max=UnitXPMax("player")
        return max>0 and ("XP %.0f%%"):format(UnitXP("player")*100/max) or "XP —"
    end,
    spec=function()
        local best, points=nil,-1
        for i=1,GetNumTalentTabs() do
            local name,_,spent=GetTalentTabInfo(i)
            if spent and spent>points then best,points=name,spent end
        end
        return points>0 and best or "Unassigned"
    end,
}
local preferred={itemLevel="character",durability="character",bags="bags",coordinates="map",coordsFPS="map",zone="map",spec="talents"}
local function click(slot)
    if InCombatLockdown() then return end
    local action=slot.action=="auto" and preferred[slot.entry] or slot.action
    if action=="character" and ToggleCharacter then ToggleCharacter("PaperDollFrame")
    elseif action=="map" and ToggleWorldMap then ToggleWorldMap()
    elseif action=="bags" and OpenAllBags then OpenAllBags()
    elseif action=="talents" and ToggleTalentFrame then ToggleTalentFrame() end
end
local function makeSlot()
    local slot=CreateFrame("Button",nil,host)
    slot:SetHeight(19)
    slot.fs=slot:CreateFontString(nil,"OVERLAY","GameFontHighlightSmall")
    slot.fs:SetPoint("LEFT",3,0)
    slot:SetScript("OnClick",click)
    slot:SetScript("OnEnter",function(self)
        if not self.tooltip or not GameTooltip then return end
        GameTooltip:SetOwner(self,"ANCHOR_TOP")
        GameTooltip:SetText(names[self.entry])
        GameTooltip:AddLine(self.value or "",1,1,1)
        GameTooltip:Show()
    end)
    slot:SetScript("OnLeave",function() if GameTooltip then GameTooltip:Hide() end end)
    slot:Hide()
    return slot
end

local function render()
    if not cfg.enabled or not host:IsVisible() then return end
    local width, offset = anchor:GetWidth(), 4
    for i,slot in ipairs(slots) do
        local reader=i<=cfg.count and readers[slot.entry]
        local value=reader and reader()
        if value and value~="" then
            slot.value=value
            slot.fs:SetText(value)
            local size=math.ceil(slot.fs:GetStringWidth())+9
            if offset+size <= width then
                slot:ClearAllPoints()
                slot:SetPoint("LEFT",host,"LEFT",offset,0)
                slot:SetWidth(size)
                slot:Show()
                offset=offset+size
            else slot:Hide() end
        else slot:Hide() end
    end
end

local elapsed=0
local ticker=CreateFrame("Frame",nil,host)
ticker:Hide()
ticker:SetScript("OnUpdate",function(_,dt)
    elapsed=elapsed+dt
    if elapsed>=1 then elapsed=0;render() end
end)
local oldPosition=DrakeishUI_PositionChatInfo
DrakeishUI_PositionChatInfo=function()
    if oldPosition then oldPosition() end
    if cfg.enabled then
        host:SetScale(anchor:GetScale())
        host:SetWidth(anchor:GetWidth())
        render()
    end
end
refresh=function()
    for key in pairs(cfg) do cfg[key]=S:Get("footer."..key) end
    for region, alpha in pairs(originals) do region:SetAlpha(cfg.enabled and 0 or alpha) end
    for child, shown in pairs(originalDrivers) do if cfg.enabled or not shown then child:Hide() else child:Show() end end
    if not cfg.enabled then
        for _, slot in ipairs(slots) do slot:Hide() end
        ticker:Hide()
        host:SetScale(originalScale);host:SetWidth(originalWidth)
        if oldPosition then oldPosition() end
        return
    end
    for i=1,3 do
        local slot=slots[i] or makeSlot()
        slots[i]=slot
        local prefix=i.."."
        slot.entry,slot.action,slot.tooltip=cfg[prefix.."entry"],cfg[prefix.."action"],cfg[prefix.."tooltip"]
        local fontKey=cfg[prefix.."font"]
        local font=fontKey=="expressway" and addon._dir.."expressway.ttf" or
                   fontKey=="calibri" and addon._dir.."Calibri1.ttf" or "Fonts\\FRIZQT__.TTF"
        if fontKey=="inherit" and addon.Appearance then addon.Appearance:ApplyFont(slot.fs,"chat",font,cfg[prefix.."size"],"")
        else slot.fs:SetFont(font,cfg[prefix.."size"],"") end
        slot.fs:SetTextColor(unpack(cfg[prefix.."color"]))
        slot.fs:SetShadowOffset(1,-1)
    end
    DrakeishUI_PositionChatInfo()
    ticker:Show()
end
addon.RefreshChatFooter=function() if S:IsReady() then refresh() end end
host:HookScript("OnShow",render)
