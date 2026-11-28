local _, addon = ...
local S = addon.Settings
local B = {}
addon.BagQuality = B
local config, painted, retry = {}, {}, {}
local refresh
local function option(key, name, kind, default, range)
    local o = range or {}
    o.id, o.name, o.type, o.default = "bagQuality."..key, name, kind, default
    local group = key:match("^level") and "Bag item levels"
        or (key=="enabled" or key=="nameColor") and "Item quality" or "Vendor and equipment outlines"
    o.category, o.group, o.keywords = "Bags", group, "item slot rarity quality "..name
    o.apply = function() refresh() end
    if o.choices then
        local valid = {}
        for _, choice in ipairs(o.choices) do valid[choice.value]=true end
        o.validate=function(value) if valid[value] then return value end end
    end
    S:Register(o)
    config[key] = default
end
local function choices(...)
    local values, result = {...}, {}
    for i=1,#values,2 do result[#result+1]={value=values[i],name=values[i+1]} end
    return result
end
option("enabled","Enable item quality styling","toggle",true)
option("mode","Quality outline","dropdown","border",{choices=choices("border","Border","glow","Glow","off","Off")})
option("thickness","Outline thickness","slider",1,{min=1,max=4,step=1})
option("strength","Outline opacity","slider",.8,{min=0,max=1,step=.01})
option("hidePoor","Hide poor quality border","toggle",true)
option("hideCommon","Hide common quality border","toggle",true)
option("vendor","Apply to vendors","toggle",false)
option("equipment","Apply to equipment","toggle",false)
option("levels","Bag slot item levels","dropdown","legacy",{choices=choices("legacy","Existing behavior","on","On","off","Off")})
option("levelColor","Color bag item level by quality","toggle",true)
option("nameColor","Color bag tooltip name by quality","toggle",false)
option("levelAnchor","Bag item level position","dropdown","legacy",{choices=choices("legacy","Existing position","BOTTOM","Bottom",
    "TOPLEFT","Top left","TOPRIGHT","Top right","BOTTOMLEFT","Bottom left","BOTTOMRIGHT","Bottom right")})
option("levelFont","Bag item level font","font","legacy",{choices=choices("legacy","Existing font","client","Client default","expressway","Expressway")})
option("levelSize","Bag item level font size","slider",12,{min=8,max=24,step=1})
-- Every setting uses the same apply callback so resetting the section repaints once.
local function apply() refresh() end
for key in pairs(config) do S.byID["bagQuality."..key].apply = apply end

local function colorFor(quality)
    if type(quality) ~= "number" or quality < 0 then return end
    if GetItemQualityColor then return GetItemQualityColor(quality) end
    local color = ITEM_QUALITY_COLORS and ITEM_QUALITY_COLORS[quality]
    if color then return color.r,color.g,color.b end
end

local function hide(button)
    local border = painted[button]
    if border then border:Hide() end
end
local function draw(button, r,g,b)
    local frame = painted[button]
    if not frame then
        frame = CreateFrame("Frame",nil,button)
        frame:EnableMouse(false)
        frame:SetAllPoints(button)
        frame:SetFrameLevel(button:GetFrameLevel()+2)
        local glow = frame:CreateTexture(nil,"OVERLAY")
        glow:SetTexture("Interface\\Buttons\\UI-ActionButton-Border")
        glow:SetBlendMode("ADD")
        glow:SetPoint("TOPLEFT",-9,9)
        glow:SetPoint("BOTTOMRIGHT",9,-9)
        frame.glow = glow
        frame.lines = {}
        for i=1,4 do
            local line = frame:CreateTexture(nil,"OVERLAY")
            line:SetTexture("Interface\\Buttons\\WHITE8X8")
            frame.lines[i] = line
        end
        local top,bottom,left,right = unpack(frame.lines)
        top:SetPoint("TOPLEFT",1,-1); top:SetPoint("TOPRIGHT",-1,-1)
        bottom:SetPoint("BOTTOMLEFT",1,1); bottom:SetPoint("BOTTOMRIGHT",-1,1)
        left:SetPoint("TOPLEFT",1,-1); left:SetPoint("BOTTOMLEFT",1,1)
        right:SetPoint("TOPRIGHT",-1,-1); right:SetPoint("BOTTOMRIGHT",-1,1)
        painted[button] = frame
    end
    if config.mode == "glow" then
        frame.glow:SetVertexColor(r,g,b,config.strength)
        frame.glow:Show()
    else frame.glow:Hide() end
    for i, line in ipairs(frame.lines) do
        if config.mode == "border" then
            if i<=2 then line:SetHeight(config.thickness) else line:SetWidth(config.thickness) end
            line:SetVertexColor(r,g,b,config.strength)
            line:Show()
        else line:Hide() end
    end
    frame:Show()
end

local function paint(button,link)
    if not button then return end
    if not config.enabled or config.mode=="off" or config.strength==0 or not link then hide(button); return end
    local quality=select(3,GetItemInfo(link))
    if quality==nil then retry[link]=true end
    if quality==0 and config.hidePoor or quality==1 and config.hideCommon then hide(button); return end
    local r,g,b=colorFor(quality)
    if r then draw(button,r,g,b) else hide(button) end
end

local function scan()
    wipe(retry)
    if MerchantFrame and MerchantFrame:IsVisible() then
        local buyback=MerchantFrame.selectedTab==2
        local per=buyback and (BUYBACK_ITEMS_PER_PAGE or 12) or (MERCHANT_ITEMS_PER_PAGE or 10)
        for i=1,per do
            local button=_G["MerchantItem"..i.."ItemButton"]
            if button then
                local link
                if config.vendor then
                    if buyback then link=GetBuybackItemLink(i)
                    else link=GetMerchantItemLink(((MerchantFrame.page or 1)-1)*per+i) end
                end
                paint(button,link)
            end
        end
        paint(MerchantBuyBackItemItemButton,config.vendor and GetBuybackItemLink(GetNumBuybackItems()))
    end
    if PaperDollFrame and PaperDollFrame:IsVisible() then
        for _,name in ipairs({"Head","Neck","Shoulder","Shirt","Chest","Waist","Legs","Feet","Wrist","Hands","Finger0","Finger1",
            "Trinket0","Trinket1","Back","MainHand","SecondaryHand","Ranged","Tabard"}) do
            local button=_G["Character"..name.."Slot"]
            if button then paint(button,config.equipment and GetInventoryItemLink("player",button:GetID())) end
        end
    end
end

local function bagSlot(button)
    local name=button:GetName() or ""
    return name:match("^DrakeishUI_BagsterItem%d+$") or name:match("^DrakeishUI_BagsterGuildItem%d+$")
end

function B:LevelContext(context)
    if not config.enabled or (context~="bags" and context~="bank" and context~="guildbank") then return end
    if config.levels=="on" then return true end
    if config.levels=="off" then return false end
end

function B:PaintLevel(button, fs, anchor)
    if not config.enabled or not bagSlot(button) then return end
    if config.levels=="off" then fs:Hide(); return end
    if config.levelAnchor~="legacy" then
        local point=config.levelAnchor
        local x=point:find("LEFT",1,true) and 2 or point:find("RIGHT",1,true) and -2 or 0
        local y=point:find("TOP",1,true) and -2 or 2
        fs:ClearAllPoints(); fs:SetPoint(point,anchor,point,x,y)
        fs.__DrakeishUI_ILvlPos=nil
    end
    if config.levelFont~="legacy" or config.levelSize~=12 then
        local font, _, flags=fs:GetFont()
        if config.levelFont=="client" then font=STANDARD_TEXT_FONT
        elseif config.levelFont=="expressway" then font=addon._dir.."expressway.ttf" end
        if font then fs:SetFont(font,config.levelSize,flags) end
    end
    if not config.levelColor then fs:SetTextColor(1,1,1) end
end

local worker=CreateFrame("Frame")
worker:Hide()
local wait, attempts = 0, 0
local function queue()
    if not S:IsReady() then return end
    attempts,wait=2,0
    worker:Show()
end
worker:SetScript("OnUpdate",function(self,dt)
    wait=wait-dt
    if wait>0 then return end
    scan()
    attempts=attempts-1
    if not next(retry) or attempts<0 then self:Hide() else wait=.35 end
end)
refresh=function()
    for key in pairs(config) do config[key]=S:Get("bagQuality."..key) end
    for button in pairs(painted) do hide(button) end
    if addon.RefreshItemLevelFont then addon:RefreshItemLevelFont() end
    if config.enabled then queue() else worker:Hide() end
end
for _, event in ipairs({"PLAYER_ENTERING_WORLD","MERCHANT_SHOW","MERCHANT_UPDATE",
    "PLAYER_EQUIPMENT_CHANGED","GET_ITEM_INFO_RECEIVED"}) do pcall(worker.RegisterEvent,worker,event) end
worker:SetScript("OnEvent",queue)
if PaperDollFrame then PaperDollFrame:HookScript("OnShow",queue) end
if GameTooltip and GameTooltip.SetBagItem then
    hooksecurefunc(GameTooltip,"SetBagItem",function(tip,bag,slot)
        if not config.enabled or not config.nameColor or not GameTooltipTextLeft1 then return end
        local _,_,_,quality=GetContainerItemInfo(bag,slot)
        local r,g,b=colorFor(quality)
        if r then GameTooltipTextLeft1:SetTextColor(r,g,b) end
    end)
end
