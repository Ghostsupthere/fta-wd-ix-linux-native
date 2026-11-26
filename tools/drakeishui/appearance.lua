local _, addon = ...
local S = addon.Settings
local A = { revision=0 }
addon.Appearance=A
local cfg={}
local originals=setmetatable({},{__mode="k"})
local textures=setmetatable({},{__mode="k"})
local reapply
local function refresh() reapply() end
local function choices(...)
    local a,t={...},{}
    for i=1,#a,2 do t[#t+1]={value=a[i],name=a[i+1]} end
    return t
end
local fontChoices=choices("inherit","Inherit","client","Client default","expressway","Expressway","calibri","Calibri",
    "friz","Friz Quadrata")
fontChoices[2].path=STANDARD_TEXT_FONT
fontChoices[3].path=addon._dir.."expressway.ttf"
fontChoices[4].path=addon._dir.."Calibri1.ttf"
fontChoices[5].path=addon._dir.."FRIZQT__.ttf"
local function register(key,label,kind,default,extra,group,category)
    local o=extra or {}
    o.id,o.name,o.type,o.default="appearance."..key,label,kind,default
    o.category,o.group,o.apply,o.keywords=category or "General",group or "Appearance",refresh,"font appearance "..label
    if o.choices then
        local valid={}
        for _, c in ipairs(o.choices) do valid[c.value]=true end
        o.validate=function(v) if valid[v] then return v end end
    end
    S:Register(o)
    cfg[key]=default
end
register("enabled","Enable global appearance settings","toggle",true)
register("font","Global font","font","inherit",{choices=fontChoices})
register("bar","Status bar texture","dropdown","current",{choices=choices("current","Existing texture","dragon","Dragonflight","flat","Flat")})
register("accent","Use custom settings accent","toggle",false)
register("accentColor","Settings accent color","colour",{1,.82,0})
register("class","Class-colored unit names","toggle",false)
local groups={
    {"units","Unit frame text","Unit Frames"}, {"actions","Action bar text","Action Bars"},
    {"bags","Bag text","Bags"}, {"chat","Chat text","Chat"}, {"tooltips","Tooltip text","Tooltips"},
    {"layout","Layout editor text","Layout"},
}
for _, group in ipairs(groups) do
    local key,label,category=unpack(group)
    local function option(id,name,kind,default,extra)
        register(key.."."..id,name,kind,default,extra,label,category)
    end
    option("font","Font family","font","inherit",{choices=fontChoices})
    option("size","Font size (0 keeps current)","slider",0,{min=0,max=32,step=1,validate=function(n)
        if type(n)~="number" or n~=n or math.abs(n)==math.huge then return end
        if n==0 then return 0 end
        return math.floor(math.max(8,math.min(32,n))+.5)
    end})
    option("outline","Outline","dropdown","current",{choices=choices("current","Existing outline","","None","OUTLINE","Outline","THICKOUTLINE","Thick outline")})
    option("shadow","Shadow","dropdown","current",{choices=choices("current","Existing shadow","on","On","off","Off")})
    option("color","Text color preset","dropdown","current",{choices=choices("current","Existing color","white","White","gold","Gold","custom","Custom")})
    option("customColor","Custom text color","colour",{1,1,1})
end

local function state(fs)
    if not originals[fs] then
        local font,size,flags=fs:GetFont()
        if not font then return end
        local x,y
        if fs.GetShadowOffset then x,y=fs:GetShadowOffset() end
        local r,g,b,alpha
        if fs.GetTextColor then r,g,b,alpha=fs:GetTextColor() end
        originals[fs]={font=font,size=size,flags=flags,x=x,y=y,r=r,g=g,b=b,alpha=alpha}
    end
    return originals[fs]
end
local function fontPath(key)
    if key=="inherit" then return end
    if key~="client" and addon.Fonts and addon.Fonts.needsSystemFont then return addon.Fonts.PRIMARY end
    for _, choice in ipairs(fontChoices) do if choice.value==key then return choice.path end end
end
function A:ApplyFont(fs,group,font,size,flags,cap,roleSize)
    local old=state(fs)
    if not old and not font then return size end
    local prefix=group.."."
    local override=cfg.enabled and cfg[prefix.."font"]
    local path=override and fontPath(override) or nil
    if cfg.enabled and not path then path=fontPath(cfg.font) end
    path=path or font or old.font
    size=size or old.size
    flags=flags or (old and old.flags) or ""
    if cfg.enabled then
        if cfg[prefix.."size"] and cfg[prefix.."size"]>0 then size=cfg[prefix.."size"] end
        local outline=cfg[prefix.."outline"]
        if outline and outline~="current" then flags=outline end
    end
    if roleSize and roleSize > 0 then size = roleSize end
    if cap then size=math.min(size,cap) end
    local current,currentSize,currentFlags=fs:GetFont()
    if current~=path or currentSize~=size or (currentFlags or "")~=flags then
        if not fs:SetFont(path,size,flags) and old then fs:SetFont(old.font,size,flags) end
    end
    if old and fs.SetShadowOffset then
        local shadow=cfg.enabled and cfg[prefix.."shadow"]
        if shadow=="on" then fs:SetShadowOffset(1,-1)
        elseif shadow=="off" then fs:SetShadowOffset(0,0)
        elseif old.x then fs:SetShadowOffset(old.x,old.y) end
    end
    return size
end

function A:ApplyColor(fs,group,unit)
    local old=state(fs)
    if not old or not old.r then return end
    local mode=cfg.enabled and cfg[group..".color"]
    if unit and cfg.enabled and cfg.class and UnitIsPlayer(unit) then
        local _,class=UnitClass(unit)
        local c=RAID_CLASS_COLORS and RAID_CLASS_COLORS[class]
        if c then fs:SetTextColor(c.r,c.g,c.b);old.painted=true;return end
    end
    if mode=="white" then fs:SetTextColor(1,1,1)
    elseif mode=="gold" then fs:SetTextColor(1,.82,0)
    elseif mode=="custom" then fs:SetTextColor(unpack(cfg[group..".customColor"]))
    elseif old.painted then fs:SetTextColor(old.r,old.g,old.b,old.alpha or 1);old.painted=nil;return
    else return end
    old.painted=true
end

function A:GetAccent()
    if cfg.enabled and cfg.accent then return unpack(cfg.accentColor) end
end

local function stylize(fs,group,unit)
    if fs and fs.GetFont and fs.SetFont then
        A:ApplyFont(fs,group)
        A:ApplyColor(fs,group,unit)
    end
end
local textSuffixes={"HotKey","Name","Count"}
local function action(button)
    local name=button and button:GetName()
    if not name then return end
    for _, key in ipairs(textSuffixes) do stylize(_G[name..key] or button[key],"actions") end
end

local function statusBar(bar)
    if not bar or not bar.GetStatusBarTexture then return end
    local texture=bar:GetStatusBarTexture()
    if not texture or not texture:GetTexture() then return end
    local path=texture:GetTexture()
    local old=textures[bar]
    local override=cfg.enabled and cfg.bar~="current"
    if not override then
        if old and path==old.applied then bar:SetStatusBarTexture(old.original) end
        textures[bar]=nil
        return
    end
    local desired=cfg.bar=="flat" and "Interface\\Buttons\\WHITE8X8" or addon._dir.."DF-StatusBar"
    if not old then old={original=path};textures[bar]=old
    elseif path~=old.applied then old.original=path end
    if old.applied~=desired or old.applied~=path then
        bar:SetStatusBarTexture(desired)
        local applied=bar:GetStatusBarTexture()
        if applied then old.applied=applied:GetTexture() end
    end
end

local function tooltip(tip)
    local name=tip:GetName()
    if not name then return end
    for i=1,tip:NumLines() do
        local left,right=_G[name.."TextLeft"..i],_G[name.."TextRight"..i]
        local role = tip == GameTooltip and (i == 1 and "header" or "body") or "comparison"
        local size = S:Get("tooltip." .. role)
        if left then A:ApplyFont(left,"tooltips",nil,nil,nil,nil,size) end
        if right then A:ApplyFont(right,"tooltips",nil,nil,nil,nil,size) end
        if i==1 and left then A:ApplyColor(left,"tooltips") end
    end
end
A.StyleTooltip = tooltip
local function layout()
    local coords=DrakeishUILayoutCoords
    if not coords or not coords:IsShown() then return end
    for _, region in ipairs({coords:GetRegions()}) do stylize(region,"layout") end
    for _, child in ipairs({coords:GetChildren()}) do
        if child:IsObjectType("EditBox") then stylize(child,"layout")
        elseif child.GetFontString then stylize(child:GetFontString(),"layout") end
    end
end
A.StyleLayout=layout
local function owners()
    local units={{"TargetFrame","target"},{"TargetFrameToT","targettarget"},{"FocusFrame","focus"},{"PetFrame","pet"}}
    for _, row in ipairs(units) do
        stylize(_G[row[1].."TextureFrameName"],"units",row[2])
        stylize(_G[row[1].."Name"],"units",row[2])
    end
    for i=1,4 do
        local party=_G["CustomPartyFrame"..i]
        if party then
            stylize(party.name,"units","party"..i)
            statusBar(party.healthBar);statusBar(party.powerBar)
        end
    end
    for i=1,40 do
        local raid=_G["DrakeishUI_RaidUnit"..i]
        if raid then stylize(raid.nameText,"units",raid.unit) end
    end
    statusBar(CastingBarFrame)
    statusBar(TargetFrameSpellBar)
    statusBar(FocusFrameSpellBar)
    local bagHeader=DrakeishUICombinedBags
    if bagHeader then stylize(bagHeader.summary,"bags") end
end
reapply=function()
    for key in pairs(cfg) do cfg[key]=S:Get("appearance."..key) end
    A.revision=A.revision+1
    for _, prefix in ipairs({"ActionButton","BonusActionButton","MultiBarBottomLeftButton","MultiBarBottomRightButton",
        "MultiBarLeftButton","MultiBarRightButton","PetActionButton","ShapeshiftButton","MultiCastActionButton","VehicleMenuBarActionButton"}) do
        for i=1,12 do action(_G[prefix..i]) end
    end
    owners();layout()
    if GameTooltip and GameTooltip:IsShown() then tooltip(GameTooltip) end
    if addon.RefreshItemLevelFont then addon:RefreshItemLevelFont() end
    if addon.RefreshUnitValues then addon.RefreshUnitValues() end
    if addon.ChatOptions then addon.ChatOptions.Refresh() end
    if addon.RefreshChatFooter then addon.RefreshChatFooter() end
end
if ActionButton_Update then hooksecurefunc("ActionButton_Update",function(button) if S:IsReady() then action(button) end end) end
if GameTooltip then
    GameTooltip:HookScript("OnTooltipSetUnit",tooltip)
    GameTooltip:HookScript("OnTooltipSetItem",tooltip)
    GameTooltip:HookScript("OnShow",tooltip)
end
local events=CreateFrame("Frame")
for _, event in ipairs({"PLAYER_ENTERING_WORLD","PLAYER_TARGET_CHANGED","PLAYER_FOCUS_CHANGED","UNIT_PET","RAID_ROSTER_UPDATE",
    "UNIT_SPELLCAST_START","UNIT_SPELLCAST_CHANNEL_START","UNIT_SPELLCAST_INTERRUPTED","UNIT_SPELLCAST_STOP"}) do
    pcall(events.RegisterEvent,events,event)
end
events:SetScript("OnEvent",function() if S:IsReady() then owners() end end)
