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
local labeledNow=true
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
option("style","Footer text style","dropdown","labeled",{choices=choices({
    {"labeled","Labels and values"},{"compact","Compact"}
})})
option("zoneColor","Use zone PvP color with labels","toggle",true)
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
    -- The WarcraftXL visual time option pins the client's game clock (GetGameTime) to a fixed hour, so a server
    -- clock would read the same time forever; show the computer's clock in that case.
    local pinned=WarcraftXL_GetVisualTime and WarcraftXL_GetVisualTime()
    if cfg.time:match("^local") or pinned then h,m=tonumber(date("%H")),tonumber(date("%M"))
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
    return savedCoordinates or "--"
end
local readers={
    zone=function() return GetRealZoneText() end,
    time=clock,
    itemLevel=function()
        local value=addon.GetAverageItemLevel and addon.GetAverageItemLevel("player")
        if value and value>0 then return ("iLvl %.1f"):format(value) end
        return "iLvl --"
    end,
    durability=function()
        local lowest
        for i=1,18 do
            local value,maximum=GetInventoryItemDurability(i)
            if maximum and maximum>0 then lowest=math.min(lowest or 1,value/maximum) end
        end
        return lowest and ("Dur %.0f%%"):format(lowest*100) or "Dur --"
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
    system=function() local _,_,lag=GetNetStats(); return ("%d ms / %.0f FPS"):format(lag or 0,GetFramerate()) end,
    memory=function()
        if not UpdateAddOnMemoryUsage or not GetAddOnMemoryUsage then return "Memory --" end
        UpdateAddOnMemoryUsage()
        local total=0
        for i=1,GetNumAddOns() do total=total+GetAddOnMemoryUsage(i) end
        return ("%.1f MB"):format(total/1024)
    end,
    coordinates=coordinates,
    coordsFPS=function()
        local format=labeledNow and "%.1f" or "%.0f FPS"
        return coordinates(), format:format(GetFramerate())
    end,
    xp=function()
        local faction,_,low,high,value=GetWatchedFactionInfo()
        if faction and high>low then return ("Rep %.0f%%"):format((value-low)*100/(high-low)) end
        local max=UnitXPMax("player")
        return max>0 and ("XP %.0f%%"):format(UnitXP("player")*100/max) or "XP --"
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
local labels={zone="Zone:",time="Time:",coordinates="Coords:",coordsFPS="Coords:"}
local zoneColors={
    friendly={.1,1,.1}, hostile={1,.1,.1}, contested={1,.7,0}, sanctuary={.4,.8,.94},
    arena={1,.1,.1}, combat={1,.1,.1}, neutral={1,1,1},
}
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
    slot.label=slot:CreateFontString(nil,"OVERLAY","GameFontHighlightSmall")
    slot.fs=slot:CreateFontString(nil,"OVERLAY","GameFontHighlightSmall")
    slot.extraLabel=slot:CreateFontString(nil,"OVERLAY","GameFontHighlightSmall")
    slot.extra=slot:CreateFontString(nil,"OVERLAY","GameFontHighlightSmall")
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

local function placeText(text, slot, value, x)
    if not value then text:Hide(); return x end
    text:SetText(value)
    text:ClearAllPoints()
    text:SetPoint("LEFT",slot,"LEFT",x,0)
    text:Show()
    return x+math.ceil(text:GetStringWidth())
end

-- Fills one slot's texts and returns its width, or nil when the readout has nothing to show.
local function fill(slot, labeled)
    local reader=readers[slot.entry]
    if not reader then return end
    labeledNow=labeled
    local value, extra=reader()
    if not value or value=="" then return end
    slot.value=extra and (value.."  "..extra) or value
    local x=placeText(slot.label,slot,labeled and labels[slot.entry] or nil,2)
    x=placeText(slot.fs,slot,value,x)
    local color=slot.color
    if labeled and cfg.zoneColor and slot.entry=="zone" then
        local zoneType=GetZonePVPInfo and GetZonePVPInfo() or "neutral"
        color=zoneColors[zoneType:lower()] or zoneColors.neutral
    end
    slot.fs:SetTextColor(unpack(color))
    if extra then
        x=placeText(slot.extraLabel,slot,labeled and "FPS:" or nil,x+6)
        x=placeText(slot.extra,slot,extra,x)
    else
        slot.extraLabel:Hide()
        slot.extra:Hide()
    end
    return x+2
end

-- Slots sit side by side from the left edge. When the labelled text is wider than the chat panel the labels are
-- dropped; a slot is never hidden for lack of room (it runs past the panel instead).
local function render()
    if not cfg.enabled or not host:IsVisible() then return end
    local width=anchor:GetWidth()
    local labeled=cfg.style=="labeled"
    local sizes={}
    for pass=1,2 do
        local total=4
        for i,slot in ipairs(slots) do
            sizes[i]=i<=cfg.count and fill(slot,labeled) or nil
            total=total+(sizes[i] or 0)
        end
        if total<=width or not labeled then break end
        labeled=false
    end
    local offset=4
    for i,slot in ipairs(slots) do
        if sizes[i] then
            slot:ClearAllPoints()
            slot:SetPoint("LEFT",host,"LEFT",offset,0)
            slot:SetWidth(sizes[i])
            slot:Show()
            offset=offset+sizes[i]
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
        slot.color=cfg[prefix.."color"]
        local fontKey=cfg[prefix.."font"]
        local font=fontKey=="expressway" and addon._dir.."expressway.ttf" or
                   fontKey=="calibri" and addon._dir.."Calibri1.ttf" or "Fonts\\FRIZQT__.TTF"
        for _, text in ipairs({slot.label,slot.fs,slot.extraLabel,slot.extra}) do
            if fontKey=="inherit" and addon.Appearance then addon.Appearance:ApplyFont(text,"chat",font,cfg[prefix.."size"],"")
            else text:SetFont(font,cfg[prefix.."size"],"") end
            text:SetShadowOffset(1,-1)
            text:SetShadowColor(0,0,0,.7)
        end
        slot.label:SetTextColor(1,1,1)
        slot.extraLabel:SetTextColor(1,1,1)
        slot.extra:SetTextColor(unpack(slot.color))
    end
    DrakeishUI_PositionChatInfo()
    ticker:Show()
end
addon.RefreshChatFooter=function() if S:IsReady() then refresh() end end
host:HookScript("OnShow",render)
