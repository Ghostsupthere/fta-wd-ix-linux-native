-- One optional carried-bag window, using the actual stock buttons/parents/IDs.
-- No button is reparented; ordinary use, split, drag, cooldown and tooltip logic stays native.
local addon=select(2,...)
local I={filter="All",query="",layoutKey=nil,dimmed={},glowing={}}
addon.Inventory=I
local filters={"All","Equipment","Consumables","Trade goods","Quest items","Junk"}
local layouting,bulk=false,false
local host,tools
local toolsWidth,toolsHeight=450,232
local changingToolsParent
local worker=CreateFrame("Frame")
local dirtyBagPaint, lateSortPaint = {}, {}
local repainting, scheduled = false, false
local function message(text)
    if UIErrorsFrame then UIErrorsFrame:AddMessage(text,1,.82,0) end
end
function I:SetStatus(text)
    if self.status==text then return end
    self.status=text
    if tools and tools.status then tools.status:SetText(text or "") end
    if host and host:IsShown() then host.summary:SetText(text or host.capacityText)
    elseif not self.sorting and text and text~="" and not (tools and tools:IsVisible()) then message(text) end
end
function I:UpdateSortButtons()
    local label=self.sorting and "Stop" or "Sort"
    if host then host.sort:SetText(label) end
    if tools then tools.sort:SetText(label) end
end
function I:CombinedEnabled()
    return addon.Layout:DB().combined and not (BankFrame and BankFrame:IsShown())
end
local function visibleFrames()
    local result={}
    for _,name in ipairs(ContainerFrame1.bags or {}) do
        local f=_G[name]
        if f then result[#result+1]=f end
    end
    return result
end
local function clearFilters()
    I.query="";I.filter="All"
    if tools then tools.search:SetText("");tools.filter:SetText("All") end
end
local function closeCarried()
    for bag=0,4 do CloseBag(bag) end
end
local function openCarried()
    for bag=0,4 do if GetContainerNumSlots(bag)>0 then OpenBag(bag) end end
end
local function makeHost()
    if host then return host end
    host=CreateFrame("Frame","DrakeishUICombinedBags",UIParent)
    host:Hide();host:SetFrameStrata("MEDIUM");host:SetFrameLevel(5)
    host:SetClampedToScreen(true);host:SetMovable(true);host:EnableMouse(true);host:RegisterForDrag("LeftButton")
    local function beginMove()
        if not InCombatLockdown() and (addon.Layout.editing or IsShiftKeyDown()) then host.moving=true;host:StartMoving() end
    end
    local function endMove()
        if not host.moving then return end
        host.moving=false
        host:StopMovingOrSizing()
        if InCombatLockdown() then return end
        local x,y=host:GetCenter()
        if x and y then
            local ratio=host.GetEffectiveScale and host:GetEffectiveScale()/UIParent:GetEffectiveScale() or host:GetScale()
            local db=addon.Layout:DB();db.bagX=x*ratio/UIParent:GetWidth();db.bagY=y*ratio/UIParent:GetHeight()
        end
    end
    host:SetScript("OnDragStart",beginMove);host:SetScript("OnDragStop",endMove)
    local NE=DrakeishUI_SocialNE
    if NE and NE.panelchrome then NE.panelchrome.Apply(host,{title="Carried bags",layout="ButtonFrameTemplateNoPortrait",noPortrait=true,noCloseButton=true}) end
    if host.Bg then
        host.Bg:ClearAllPoints()
        host.Bg:SetPoint("TOPLEFT",host,"TOPLEFT",6,-4);host.Bg:SetPoint("BOTTOMRIGHT",host,"BOTTOMRIGHT",-1,2)
    end
    -- A header above the native container children remains draggable while
    -- locked. Shift-dragging an item keeps its normal split/link behavior.
    local header=CreateFrame("Frame",nil,host);host.header=header
    header:SetFrameLevel(host:GetFrameLevel()+30)
    header:SetPoint("TOPLEFT",host,"TOPLEFT",0,0);header:SetPoint("TOPRIGHT",host,"TOPRIGHT",0,0);header:SetHeight(30)
    header:EnableMouse(true);header:RegisterForDrag("LeftButton")
    header:SetScript("OnDragStart",beginMove);header:SetScript("OnDragStop",endMove)
    header:SetScript("OnEnter",function(self)
        GameTooltip:SetOwner(self,"ANCHOR_TOP");GameTooltip:SetText("Carried bags")
        GameTooltip:AddLine("Shift-drag the title to move this window.",1,1,1,true);GameTooltip:Show()
    end)
    header:SetScript("OnLeave",function() GameTooltip:Hide() end)
    local close=CreateFrame("Button",nil,header,"UIPanelCloseButton")
    close:SetPoint("TOPRIGHT",0,0);close:SetScript("OnClick",closeCarried)
    local function control(text,x,width,fn)
        local b=CreateFrame("Button",nil,host,"UIPanelButtonTemplate")
        b:SetFrameLevel(host:GetFrameLevel()+30)
        b:SetSize(width,24);b:SetPoint("TOPLEFT",x,-34);b:SetText(text);b:SetScript("OnClick",fn)
        return b
    end
    control("Search / filter",14,130,function() I:ShowTools(true) end)
    host.sort=control("Sort",150,74,function() I:StartSort() end)
    control("Layout",230,84,function() addon.Layout:OpenBagOptions() end)
    host.summary=host:CreateFontString(nil,"OVERLAY","GameFontHighlightSmall")
    host.summary:SetPoint("BOTTOMLEFT",14,10)
    host.summary:SetPoint("BOTTOMRIGHT",-118,10)
    host.summary:SetJustifyH("LEFT")
    if host.summary.SetWordWrap then host.summary:SetWordWrap(false) end
    local money=CreateFrame("Frame","DrakeishUICombinedMoney",host,"SmallMoneyFrameTemplate")
    money:SetPoint("BOTTOMRIGHT",-12,8)
    if MoneyFrame_SetType then MoneyFrame_SetType(money,"PLAYER") end
    local mover=CreateFrame("Frame",nil,UIParent);host.mover=mover
    mover:SetFrameStrata("DIALOG");mover:SetPoint("TOPLEFT",host,"TOPLEFT",0,0);mover:SetPoint("TOPRIGHT",host,"TOPRIGHT",0,0);mover:SetHeight(30)
    mover:EnableMouse(true);mover:RegisterForDrag("LeftButton")
    mover:SetBackdrop({bgFile="Interface\\Buttons\\WHITE8X8",edgeFile="Interface\\Tooltips\\UI-Tooltip-Border",edgeSize=12})
    mover:SetBackdropColor(.05,.35,.55,.65)
    local moveLabel=mover:CreateFontString(nil,"OVERLAY","GameFontNormalSmall");moveLabel:SetPoint("CENTER");moveLabel:SetText("Carried bags")
    mover:SetScript("OnDragStart",beginMove);mover:SetScript("OnDragStop",endMove);mover:Hide()
    host:SetScript("OnShow",function() I:SetEditing(addon.Layout.editing) end)
    host:SetScript("OnHide",function() mover:Hide();if not layouting then closeCarried() end end)
    UISpecialFrames[#UISpecialFrames+1]=host:GetName()
    return host
end
function I:SetEditing(value)
    if host and host.mover then
        if not value and host.moving then host:StopMovingOrSizing();host.moving=false end
        if value and host:IsShown() and not InCombatLockdown() then host.mover:Show() else host.mover:Hide() end
    end
end
local function restoreFrame(f)
    if not f or not f._duiCombined then return end
    f._duiCombined=nil
    f:SetFrameLevel(f._duiOldLevel or 2);f:EnableMouse(true)
    ContainerFrame_GenerateFrame(f,f.size,f:GetID(),true)
    if addon.SkinNativeBag then addon.SkinNativeBag(f) end
    if addon.SetNativeBagCombined then addon.SetNativeBagCombined(f,false) end
    return true
end
local function placeTools(h)
    if not tools or not tools:IsShown() then return end
    local parent=h or UIParent
    if tools:GetParent()~=parent then
        changingToolsParent=true
        tools:StopMovingOrSizing()
        tools:SetParent(parent)
        changingToolsParent=nil
    end
    tools:SetFrameStrata(h and "MEDIUM" or "DIALOG")
    tools:SetFrameLevel(parent:GetFrameLevel()+32)
    if h then
        tools:SetScale(1)
        tools:ClearAllPoints();tools:SetPoint("TOPLEFT",h,"TOPLEFT",14,-63)
    else
        tools:SetScale(math.min(1,math.max(1,UIParent:GetWidth()-24)/toolsWidth,math.max(1,UIParent:GetHeight()-24)/toolsHeight))
        if tools.embedded~=false then
            tools:ClearAllPoints();tools:SetPoint("BOTTOMLEFT",UIParent,"BOTTOMLEFT",20,CONTAINER_OFFSET_Y+16)
        end
    end
    tools.embedded=h and true or false
end
local function combinedGrid(carried,columns,pitch,groupMode)
    -- Every slot sits on a fixed column, so columns line up in every row. When the bags are sorted by the chosen
    -- grouping, each item group starts on a new row with a small gap above it; when they are not sorted (one group
    -- would be split into many runs) the grid stays dense, because a break at every change would leave mostly
    -- empty rows.
    local positions,groups={},{}
    local groupSpace=6
    for _,frame in ipairs(carried) do
        local bag=frame:GetID()
        for slot=1,frame.size do
            local group
            if groupMode then
                local _,_,_,_,_,_,link=GetContainerItemInfo(bag,slot)
                if link then
                    local _,_,quality,_,_,class=GetItemInfo(link)
                    if groupMode=="Type" then group=class else group=quality end
                end
            end
            if group==nil then group=false end
            groups[#groups+1]=group
        end
    end
    local sorted=groupMode and true or false
    if sorted then
        local seen,last={},nil
        for _,group in ipairs(groups) do
            if group then
                if group~=last then
                    if seen[group] then sorted=false;break end
                    seen[group]=true;last=group
                end
            end
        end
    end
    local column,y,widest,previous=0,0,0,nil
    for index,group in ipairs(groups) do
        local newGroup=sorted and group and previous and group~=previous
        if column>=columns or (newGroup and column>0) then
            column=0;y=y+pitch
            if newGroup then y=y+groupSpace end
        elseif newGroup and index>1 then y=y+groupSpace end
        if group then previous=group end
        positions[index]={x=column*pitch,y=y}
        widest=math.max(widest,column*pitch+pitch)
        column=column+1
    end
    return positions,widest+24,y+pitch+94
end
function I:InvalidateLayout() self.layoutKey=nil;self:Schedule() end
function I:Arrange()
    if layouting then return end
    local frames=visibleFrames()
    -- Native container/item templates are ordinary frames: opening/closing bags
    -- must keep working in combat. Defer only if an external owner protected one.
    if InCombatLockdown() then
        for _,f in ipairs(frames) do if f.IsProtected and f:IsProtected() then self.pending=true;return end end
    end
    self.pending=false;layouting=true
    local carried,carriedSet,total={},{},0
    local db=addon.Layout:DB()
    local combined=self:CombinedEnabled()
    local toolsOpen=tools and tools:IsShown()
    local groupMode=db.bagGroupSpacing and db.sortMode~="Name" and db.sortMode
    local key={tostring(combined),db.columns,db.bagScale,db.bagSlotSpacing,
        UIParent:GetWidth(),UIParent:GetHeight(),tostring(toolsOpen),tostring(groupMode)}
    for _,f in ipairs(frames) do
        local bag=f:GetID()
        if bag>=0 and bag<=4 then
            carried[#carried+1]=f;carriedSet[f]=true;total=total+f.size
        end
        key[#key+1]=f:GetName()..":"..bag..":"..f.size
    end
    key=table.concat(key,";")
    local combine=combined and total>0
    local refreshGroups=combine and groupMode and self.groupsDirty and not self.sorting and not GetCursorInfo()
        and not (StackSplitFrame and StackSplitFrame.IsShown and StackSplitFrame:IsShown())
    if key~=self.layoutKey or refreshGroups then
        local restored
        for n=1,NUM_CONTAINER_FRAMES do
            local frame=_G["ContainerFrame"..n]
            if not combine or not carriedSet[frame] then
                if restoreFrame(frame) then restored=true end
            end
        end
        if not combine then
            placeTools(nil)
            if host then host:Hide() end
        else
            table.sort(carried,function(a,b) return a:GetID()<b:GetID() end)
            local h=makeHost()
            local columns=math.min(db.columns,total)
            local pitch=37+db.bagSlotSpacing
            local positions,width,height=combinedGrid(carried,columns,pitch,groupMode)
            local toolsSpace=toolsOpen and toolsHeight or 0
            width=math.max(width,toolsOpen and toolsWidth+28 or 328)
            height=height+toolsSpace
            local scale=math.min(db.bagScale,math.max(1,UIParent:GetWidth()-32)/width,
                math.max(1,UIParent:GetHeight()-CONTAINER_OFFSET_Y-40)/height)
            h:SetSize(width,height);h:SetScale(scale);h:ClearAllPoints()
            if type(db.bagX)=="number" and type(db.bagY)=="number" and db.bagX==db.bagX and db.bagY==db.bagY then
                h:SetPoint("CENTER",UIParent,"BOTTOMLEFT",math.max(0,math.min(1,db.bagX))*UIParent:GetWidth()/scale,math.max(0,math.min(1,db.bagY))*UIParent:GetHeight()/scale)
            else h:SetPoint("BOTTOMRIGHT",UIParent,"BOTTOMRIGHT",-12/scale,(CONTAINER_OFFSET_Y+8)/scale) end
            local position=1
            for _,f in ipairs(carried) do
                f._duiOldLevel=f._duiOldLevel or f:GetFrameLevel()
                f._duiCombined=true;f:EnableMouse(false);f:SetScale(scale)
                f:ClearAllPoints();f:SetPoint("TOPLEFT",h,"TOPLEFT",0,0);f:SetSize(width,height)
                f:SetFrameLevel(h:GetFrameLevel()+1)
                for slot=1,f.size do
                    local b=_G[f:GetName().."Item"..(f.size-slot+1)]
                    local point=positions[position]
                    b:ClearAllPoints();b:SetPoint("TOPLEFT",f,"TOPLEFT",14+point.x,-70-toolsSpace-point.y)
                    position=position+1
                end
                if addon.SetNativeBagCombined then addon.SetNativeBagCombined(f,true) end
            end
            h.capacityText=total.." slots"
            h.summary:SetText(self.status or h.capacityText);h:Show();placeTools(h)
        end
        if restored then updateContainerFrameAnchors() end
        self.groupsDirty=false
        self.layoutKey=key
    end
    local open=#carried>0
    if open~=self.carriedOpen then
        self.carriedOpen=open
        if open and self.QueueAutoSort then self:QueueAutoSort()
        elseif not open and self.CancelAutoSort then self:CancelAutoSort() end
    end
    if #frames==0 then
        clearFilters()
        if tools then tools:Hide() end
        if self.StopSort then self:StopSort() end
    end
    layouting=false
    self:FilterItems()
end
function I:FilterItems()
    local needle=string.lower(self.query or "")
    local db=addon.Layout:DB()
    if needle=="" and self.filter=="All" and not db.bagGlow then
        for button in pairs(self.dimmed) do button:SetAlpha(1);self.dimmed[button]=nil end
        for button in pairs(self.glowing) do button._duiCategoryGlow:Hide();self.glowing[button]=nil end
        return
    end
    local classes={GetAuctionItemClasses()}
    local consumable,goods,recipe,quest=classes[4],classes[6],classes[9],classes[12]
    for _,f in ipairs(visibleFrames()) do
        for slot=1,f.size do
            local button=_G[f:GetName().."Item"..(f.size-slot+1)]
            local texture,_,_,quality,_,_,link=GetContainerItemInfo(f:GetID(),slot)
            local matches=true
            local name,_,q,_,_,class,subclass,equip
            if link then name,_,q,_,_,class,subclass,_,equip=GetItemInfo(link) end
            local isQuest=GetContainerItemQuestInfo(f:GetID(),slot)
            if needle~="" or self.filter~="All" then
                if not texture then matches=false
                elseif self.filter=="Equipment" then matches=equip and equip~=""
                elseif self.filter=="Consumables" then matches=class~=nil and class==consumable
                elseif self.filter=="Trade goods" then matches=class~=nil and (class==goods or class==recipe)
                elseif self.filter=="Quest items" then matches=isQuest or (class~=nil and class==quest)
                elseif self.filter=="Junk" then matches=(q or quality)==0 end
                if needle~="" then matches=matches and string.find(string.lower((name or "").." "..(class or "").." "..(subclass or "")),needle,1,true)~=nil end
            end
            if matches and self.dimmed[button] then button:SetAlpha(1);self.dimmed[button]=nil
            elseif not matches and not self.dimmed[button] then button:SetAlpha(.2);self.dimmed[button]=true end
            local category
            if texture then
                if isQuest or (class and class==quest) then category="Quest items"
                elseif (q or quality)==0 then category="Junk"
                elseif equip and equip~="" then category="Equipment"
                elseif class and class==consumable then category="Consumables"
                elseif class and class==goods then category="Trade goods"
                elseif class and class==recipe then category="Recipes"
                elseif class and class==classes[10] then category="Gems" end
            end
            if db.bagGlow and category and db.bagGlowTypes[category] then
                local glow=button._duiCategoryGlow
                if not glow then
                    glow=button:CreateTexture(nil,"OVERLAY");button._duiCategoryGlow=glow
                    glow:SetTexture("Interface\\Buttons\\UI-ActionButton-Border");glow:SetBlendMode("ADD")
                    glow:SetPoint("TOPLEFT",button,"TOPLEFT",-10,10);glow:SetPoint("BOTTOMRIGHT",button,"BOTTOMRIGHT",10,-10)
                end
                local color=db.bagGlowColors[category]
                glow:SetVertexColor(color[1],color[2],color[3],db.bagGlowAlpha);glow:Show();self.glowing[button]=true
            elseif button._duiCategoryGlow then button._duiCategoryGlow:Hide();self.glowing[button]=nil end
        end
    end
end
local function updateInventoryView(self)
    self:SetScript("OnUpdate",nil)
    scheduled=false
    if next(dirtyBagPaint) then
        local bags=dirtyBagPaint
        dirtyBagPaint={}
        repainting=true
        for _,frame in ipairs(visibleFrames()) do
            local bag=frame:GetID()
            if bags[bag] and frame:IsShown() then
                if InCombatLockdown() and frame.IsProtected and frame:IsProtected() then
                    dirtyBagPaint[bag]=true
                else
                    ContainerFrame_Update(frame)
                end
            end
        end
        repainting=false
    end
    I:Arrange()
end

function I:Schedule()
    if not scheduled then
        scheduled=true
        worker:SetScript("OnUpdate",updateInventoryView)
    end
end

function I:RefreshAfterSort(bags, pending)
    if not next(bags) then return end
    local deadline=pending and GetTime()+3
    for bag in pairs(bags) do
        dirtyBagPaint[bag]=true
        if deadline then lateSortPaint[bag]=deadline end
    end
    self.groupsDirty=true
    self:Schedule()
end
function I:ShowTools(toggle)
    if toggle and tools and tools:IsShown() then tools:Hide();return end
    if not tools then
        tools=CreateFrame("Frame","DrakeishUIBagTools",UIParent)
        tools:SetSize(toolsWidth,toolsHeight);tools:Hide()
        tools:SetFrameStrata("DIALOG");tools:SetClampedToScreen(true);tools:SetMovable(true);tools:EnableMouse(true);tools:RegisterForDrag("LeftButton")
        tools:SetBackdrop({bgFile="Interface\\Tooltips\\UI-Tooltip-Background",edgeFile="Interface\\Tooltips\\UI-Tooltip-Border",edgeSize=16,insets={left=4,right=4,top=4,bottom=4}})
        tools:SetBackdropColor(.04,.04,.06,.98)
        tools:SetScript("OnDragStart",function() if not tools.embedded then tools:StartMoving() end end)
        tools:SetScript("OnDragStop",function() if not tools.embedded then tools:StopMovingOrSizing() end end)
        tools:SetScript("OnHide",function()
            if changingToolsParent then return end
            clearFilters();I:InvalidateLayout()
        end)
        local title=tools:CreateFontString(nil,"ARTWORK","GameFontNormal");title:SetPoint("TOPLEFT",14,-12);title:SetText("Bag tools - search, filter and sort")
        local close=CreateFrame("Button",nil,tools,"UIPanelCloseButton");close:SetPoint("TOPRIGHT",0,0)
        close:SetScript("OnClick",function() tools:Hide() end)
        local search=CreateFrame("EditBox",nil,tools,"InputBoxTemplate");tools.search=search
        search:SetSize(200,24);search:SetPoint("TOPLEFT",20,-40);search:SetAutoFocus(false);search:SetMaxLetters(120)
        search:SetScript("OnTextChanged",function(self) I.query=self:GetText() or "";I:FilterItems() end)
        search:SetScript("OnEscapePressed",function(self) self:ClearFocus() end)
        local filter=CreateFrame("Button",nil,tools,"UIPanelButtonTemplate");tools.filter=filter
        filter:SetSize(120,24);filter:SetPoint("LEFT",search,"RIGHT",10,0);filter:SetText("All")
        filter:SetScript("OnClick",function()
            local index=1;for n,name in ipairs(filters) do if name==I.filter then index=n end end
            I.filter=filters[index%#filters+1];filter:SetText(I.filter);I:FilterItems()
        end)
        local sort=CreateFrame("Button",nil,tools,"UIPanelButtonTemplate");tools.sort=sort
        sort:SetSize(70,24);sort:SetPoint("LEFT",filter,"RIGHT",8,0);sort:SetText("Sort")
        sort:SetScript("OnClick",function() I:StartSort() end)
        local order=CreateFrame("Button",nil,tools,"UIPanelButtonTemplate");tools.order=order
        order:SetSize(180,24);order:SetPoint("TOPLEFT",14,-76)
        order:SetText("Sort by: "..addon.Layout:DB().sortMode)
        order:SetScript("OnClick",function(self)
            if I.sorting then return end
            local text="Sort by: "..addon.Layout:CycleBagSortMode()
            self:SetText(text)
            if DrakeishUIIntegratedSortMode then DrakeishUIIntegratedSortMode:SetText(text) end
        end)
        local clear=CreateFrame("Button",nil,tools,"UIPanelButtonTemplate")
        clear:SetSize(130,24);clear:SetPoint("LEFT",order,"RIGHT",8,0);clear:SetText("Clear filters")
        clear:SetScript("OnClick",function() clearFilters();I:FilterItems() end)
        local function toggleOption(key,label,x)
            local box=CreateFrame("CheckButton",nil,tools,"UICheckButtonTemplate")
            box:SetSize(22,22);box:SetPoint("TOPLEFT",x,-103)
            local caption=box:CreateFontString(nil,"ARTWORK","GameFontHighlightSmall")
            caption:SetPoint("LEFT",box,"RIGHT",0,0);caption:SetText(label)
            box:SetScript("OnClick",function(self)
                local enabled=self:GetChecked() and true or false
                addon.Layout:DB()[key]=enabled
                if key=="bagGroupSpacing" then I:InvalidateLayout()
                elseif not enabled then I:CancelAutoSort() end
            end)
            tools[key]=box
        end
        toggleOption("bagAutoSort","Auto-sort on open",14)
        toggleOption("bagGroupSpacing","Space between groups",205)
        local includes=tools:CreateFontString(nil,"ARTWORK","GameFontHighlightSmall")
        includes:SetPoint("TOPLEFT",16,-134);includes:SetText("Include in sorting:")
        tools.includes={}
        for bag=0,4 do
            local id=bag
            local check=CreateFrame("CheckButton",nil,tools,"UICheckButtonTemplate")
            check:SetSize(22,22);check:SetPoint("TOPLEFT",14+id*80,-154)
            local name=check:CreateFontString(nil,"ARTWORK","GameFontHighlightSmall")
            name:SetPoint("LEFT",check,"RIGHT",0,0);name:SetText(id==0 and "Pack" or ("Bag "..id))
            check:SetScript("OnClick",function(self)
                local db=addon.Layout:DB()
                if I.sorting then self:SetChecked(not db.excludeSort[id]);return end
                db.excludeSort[id]=not self:GetChecked()
            end)
            tools.includes[id]=check
        end
        tools.status=tools:CreateFontString(nil,"ARTWORK","GameFontHighlightSmall")
        tools.status:SetPoint("TOPLEFT",14,-188);tools.status:SetPoint("TOPRIGHT",-14,-188);tools.status:SetHeight(0);tools.status:SetJustifyH("LEFT")
        tools.status:SetText("Search names/types. Filter cycles categories. Nonmatches are dimmed; slots stay in place.")
        UISpecialFrames[#UISpecialFrames+1]=tools:GetName()
    end
    local db=addon.Layout:DB()
    for id,check in pairs(tools.includes) do check:SetChecked(not db.excludeSort[id]) end
    tools.bagAutoSort:SetChecked(db.bagAutoSort);tools.bagGroupSpacing:SetChecked(db.bagGroupSpacing)
    tools.order:SetText("Sort by: "..db.sortMode)
    if self.status then tools.status:SetText(self.status) end
    if #(ContainerFrame1.bags or {})==0 then OpenAllBags(true) end
    tools:Show();self:UpdateSortButtons();self:Arrange()
end

-- Wrappers delegate unchanged to the previous owner unless combined mode is on.
-- Group operations use OpenBag/CloseBag directly, avoiding recursive ToggleBag calls.
local oldToggle,oldBackpack,oldAll=ToggleBag,ToggleBackpack,OpenAllBags
ToggleBag=function(bag,...)
    if bulk or not I:CombinedEnabled() or bag<0 or bag>4 then return oldToggle(bag,...) end
    if IsOptionFrameOpen() then return end
    bulk=true
    if IsBagOpen(bag) then closeCarried() else openCarried() end
    bulk=false;I:Arrange()
end
ToggleBackpack=function(...)
    if bulk or not I:CombinedEnabled() then return oldBackpack(...) end
    return ToggleBag(0)
end
OpenAllBags=function(force,...)
    if bulk or not I:CombinedEnabled() then return oldAll(force,...) end
    if not UIParent:IsShown() then return end
    bulk=true
    local all=true
    for bag=0,4 do if GetContainerNumSlots(bag)>0 and not IsBagOpen(bag) then all=false end end
    if all and not force then closeCarried() else openCarried() end
    bulk=false;I:Arrange()
end
hooksecurefunc("updateContainerFrameAnchors",function() if not bulk then I:Arrange() end end)
hooksecurefunc("ContainerFrame_Update",function() if not repainting then I:Schedule() end end)
hooksecurefunc("ContainerFrame_GenerateFrame",function() if not layouting then I:InvalidateLayout() end end)
for _,event in ipairs({"BAG_UPDATE","ITEM_LOCK_CHANGED","GET_ITEM_INFO_RECEIVED","CURSOR_UPDATE",
    "PLAYER_REGEN_ENABLED","DISPLAY_SIZE_CHANGED","BANKFRAME_OPENED","BANKFRAME_CLOSED"}) do worker:RegisterEvent(event) end
worker:SetScript("OnEvent",function(_,event,bag)
    local deadline=lateSortPaint[bag]
    if deadline and (event=="BAG_UPDATE" or event=="ITEM_LOCK_CHANGED") then
        if GetTime()<deadline then dirtyBagPaint[bag]=true
        else lateSortPaint[bag]=nil end
    end
    if event=="BAG_UPDATE" or event=="GET_ITEM_INFO_RECEIVED" then I.groupsDirty=true end
    if #(ContainerFrame1.bags or {})>0 or I.pending then I:Schedule() end
end)
addon.Layout:RefreshBags()
