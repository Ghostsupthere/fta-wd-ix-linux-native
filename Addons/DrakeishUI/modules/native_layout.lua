-- Per-character layout on the retained secure bars and native bag frames.
local addon=select(2,...)
local L={entries={},editing=false}
addon.Layout=L
local defaults={bagSpacing=14,bagScale=1,bagSlotSpacing=4,actionSpacing=7,combined=false,columns=20,sortMode="Type",
    bagGlow=false,bagGlowAlpha=.65,bagAutoSort=false,bagGroupSpacing=true}
local nextSortMode={Type="Name",Name="Quality",Quality="Type"}
L.outlineTypes={{"Equipment",.25,.6,1},{"Consumables",.25,1,.35},{"Trade goods",1,.65,.2},
    {"Quest items",1,.9,.15},{"Recipes",.8,.35,1},{"Gems",1,.3,.5},{"Junk",.55,.55,.55}}
local function finite(n) return type(n)=="number" and n==n and math.abs(n)<100000 end
function L:DB()
    if type(DrakeishUI_LayoutDB)~="table" then DrakeishUI_LayoutDB={} end
    local db=DrakeishUI_LayoutDB
    for key,value in pairs(defaults) do
        if type(db[key])~=type(value) or (type(value)=="number" and not finite(db[key])) then db[key]=value end
    end
    db.bagSpacing=math.max(4,math.min(40,db.bagSpacing))
    db.bagScale=math.max(.5,math.min(1.5,db.bagScale))
    db.bagSlotSpacing=math.max(0,math.min(16,math.floor(db.bagSlotSpacing+.5)))
    db.actionSpacing=math.max(0,math.min(16,math.floor(db.actionSpacing+.5)))
    db.columns=math.max(8,math.min(28,math.floor(db.columns)))
    if type(db.bars)~="table" then db.bars={} end
    if type(db.excludeSort)~="table" then db.excludeSort={} end
    db.bagGlowAlpha=math.max(.1,math.min(1,db.bagGlowAlpha))
    if type(db.bagGlowTypes)~="table" then db.bagGlowTypes={} end
    if type(db.bagGlowColors)~="table" then db.bagGlowColors={} end
    for _,category in ipairs(self.outlineTypes) do
        local key=category[1]
        if type(db.bagGlowTypes[key])~="boolean" then db.bagGlowTypes[key]=true end
        if type(db.bagGlowColors[key])~="table" then db.bagGlowColors[key]={} end
        for i=1,3 do
            local v=db.bagGlowColors[key][i]
            db.bagGlowColors[key][i]=finite(v) and math.max(0,math.min(1,v)) or category[i+1]
        end
    end
    if db.sortMode~="Type" and db.sortMode~="Name" and db.sortMode~="Quality" then db.sortMode="Type" end
    return db
end
function L:CycleBagSortMode()
    local db=self:DB()
    db.sortMode=nextSortMode[db.sortMode]
    if addon.Inventory then addon.Inventory:InvalidateLayout() end
    return db.sortMode
end
local specs={{"pUiMainBar","Main action bar"},{"MultiBarBottomLeft","Bottom left bar"},
    {"MultiBarBottomRight","Bottom right bar"},{"MultiBarRight","Right bar"},{"MultiBarLeft","Left bar"},
    {"pUiPetBarHolder","Pet bar"},{"pUiStanceHolder","Stance / form bar"},
    {"PlayerFrame","Player"},{"TargetFrame","Target"},{"FocusFrame","Focus"},
    {"PetFrame","Pet"},{"CustomPartyFrame1","Party 1"},{"CustomPartyFrame2","Party 2"},
    {"CustomPartyFrame3","Party 3"},{"CustomPartyFrame4","Party 4"},
    {"Minimap","Minimap"},{"BuffFrame","Buffs",220,70},{"CastingBarFrame","Cast bar"},
    {"MinimalChatAnchor","Chat"}, {"ImprovedUICastBar","Player cast bar"}, {"WatchFrame","Objective tracker"},
    {"DrakeishUICombinedBags","Combined bags"}, {"DrakeishUI_RaidFrames","Raid frames"},
    {"pUiMicroMenu","Micro menu",96,24}}
-- Frames that can be scaled but have no Move UI handle.
local scaleOnly={"TargetFrameToT","FocusFrameToT","Boss1TargetFrame","Boss2TargetFrame","Boss3TargetFrame",
    "Boss4TargetFrame","ArenaEnemyFrame1","ArenaEnemyFrame2","ArenaEnemyFrame3","ArenaEnemyFrame4",
    "ArenaEnemyFrame5","pUiBagsBar"}
L.scaleBases={}
local function clampScale(n) return math.max(.5,math.min(2,n)) end
local function effectiveScale(frame)
    return frame.GetEffectiveScale and frame:GetEffectiveScale() or frame:GetScale()
end
function L:BaseScale(name)
    local entry=self.entries[name]
    return entry and entry.scale or (addon.config and addon.config.mainbars.scale_actionbar) or 1
end
function L:ApplyBar(frame)
    if self.applying or InCombatLockdown() then self.pending=true;return end
    local entry=frame and self.entries[frame:GetName()]
    local saved=entry and self:DB().bars[entry.name]
    if not entry or self.moving==entry or type(saved)~="table" then return end
    self.applying=true
    if finite(saved.scale) then
        frame:SetScale(entry.scale*clampScale(saved.scale))
        -- The minimap ring is not a child of the map, so scale it alongside.
        if entry.name=="Minimap" and DrakeishUI_MinimapBorder then DrakeishUI_MinimapBorder:SetScale(clampScale(saved.scale)) end
    end
    local width,height=UIParent:GetWidth(),UIParent:GetHeight()
    if width>0 and height>0 and finite(saved.x) and finite(saved.y) then
        local ratio=effectiveScale(UIParent)/effectiveScale(frame)
        local x=math.max(0,math.min(1,saved.x))*width
        local y=math.max(0,math.min(1,saved.y))*height
        if entry.name=="MinimalChatAnchor" then
            -- Reserve room for the typing line/toggles above and footer below.
            local w,h=frame:GetWidth()/ratio,frame:GetHeight()/ratio
            x=math.max(8+w/2,math.min(width-8-w/2,x))
            y=math.max(28+h/2,math.min(height-8-h/2-40/ratio,y))
            -- Store the user's position only on drag. A temporary display mode
            -- clamp must not permanently move the chat anchor.
            frame:SetUserPlaced(false)
        end
        frame:ClearAllPoints()
        frame:SetPoint("CENTER",UIParent,"BOTTOMLEFT",x*ratio,y*ratio)
        frame:SetClampedToScreen(true)
    end
    self.applying=false
    if frame._drakeishLayoutChanged then frame._drakeishLayoutChanged() end
end
local function makeMover(entry)
    local bar=entry.frame
    local handle=CreateFrame("Frame",nil,UIParent)
    entry.handle=handle
    handle:SetFrameStrata("DIALOG")
    if entry.width then handle:SetSize(entry.width,entry.height);handle:SetPoint("TOPRIGHT",bar,"TOPRIGHT",0,0)
    else handle:SetAllPoints(bar) end
    handle:EnableMouse(true);handle:RegisterForDrag("LeftButton")
    handle:SetBackdrop({bgFile="Interface\\Buttons\\WHITE8X8",edgeFile="Interface\\Tooltips\\UI-Tooltip-Border",edgeSize=12})
    handle:SetBackdropColor(.05,.35,.55,.65)
    local text=handle:CreateFontString(nil,"OVERLAY","GameFontNormalSmall")
    text:SetPoint("CENTER");text:SetWidth(160);text:SetText(entry.label)
    handle:SetScript("OnMouseDown",function() L:Select(entry.name) end)
    handle:SetScript("OnDragStart",function()
        if InCombatLockdown() then return end
        L:Select(entry.name)
        L.moving=entry;bar:SetMovable(true);bar:SetClampedToScreen(true);bar:StartMoving()
    end)
    handle:SetScript("OnDragStop",function()
        bar:StopMovingOrSizing();L.moving=nil
        if entry.name=="MinimalChatAnchor" then bar:SetUserPlaced(false) end
        if InCombatLockdown() then L.pending=true;return end
        local x,y=bar:GetCenter()
        if x and y then
            local saved=L:DB().bars[entry.name]
            if type(saved)~="table" then saved={} end
            local ratio=effectiveScale(bar)/effectiveScale(UIParent)
            saved.x=x*ratio/UIParent:GetWidth();saved.y=y*ratio/UIParent:GetHeight()
            L:DB().bars[entry.name]=saved
            L:ApplyBar(bar)
        end
    end)
    handle:Hide()
end
function L:ApplyScaleOnly()
    if InCombatLockdown() then self.pending=true;return end
    for _,name in ipairs(scaleOnly) do
        local frame,saved=_G[name],self:DB().bars[name]
        if frame and type(saved)=="table" and finite(saved.scale) then
            local entry=self.scaleBases[name]
            if not entry then
                -- Captured before our first change, so this is the frame's own scale.
                entry={frame=frame,scale=frame:GetScale()};self.scaleBases[name]=entry
                hooksecurefunc(frame,"SetScale",function()
                    if not L.applying and not InCombatLockdown() then L:ApplyScaleOnly() end
                end)
            end
            self.applying=true
            frame:SetScale(entry.scale*clampScale(saved.scale))
            self.applying=false
        end
    end
end
function L:ElementScale(name)
    local saved=self:DB().bars[name]
    return type(saved)=="table" and finite(saved.scale) and clampScale(saved.scale) or 1
end
function L:SetElementScale(names,value)
    if InCombatLockdown() or not finite(value) then return end
    self:RefreshBars()
    for _,name in ipairs(names) do
        local saved=self:DB().bars[name]
        -- Leave untouched elements alone when asked for their normal size.
        if type(saved)=="table" or clampScale(value)~=1 then
            if type(saved)~="table" then saved={};self:DB().bars[name]=saved end
            saved.scale=clampScale(value)
            if self.entries[name] then self:ApplyBar(self.entries[name].frame) end
        end
    end
    self:ApplyScaleOnly()
    if self.selectionChanged then self.selectionChanged() end
end
function L:RefreshBars()
    if InCombatLockdown() then self.pending=true;return end
    self.pending=false
    self:ApplyScaleOnly()
    for _,spec in ipairs(specs) do
        local name,label=spec[1],spec[2]
        local frame=_G[name]
        if frame and not self.entries[name] then
            local entry={name=name,label=label,frame=frame,scale=frame:GetScale(),point={frame:GetPoint()},width=spec[3],height=spec[4]}
            self.entries[name]=entry;makeMover(entry)
            hooksecurefunc(frame,"SetPoint",function(self,...)
                if L.applying or L.moving==entry or InCombatLockdown() then return end
                entry.point={...};L:ApplyBar(self)
            end)
            hooksecurefunc(frame,"SetScale",function(self)
                if L.applying or InCombatLockdown() then return end
                local saved=L:DB().bars[entry.name]
                if type(saved)=="table" and finite(saved.scale) then L:ApplyBar(self) end
            end)
            frame:HookScript("OnShow",function() if L.editing and not InCombatLockdown() then entry.handle:Show() end end)
            frame:HookScript("OnHide",function() entry.handle:Hide() end)
        end
        local entry=self.entries[name]
        if entry then
            self:ApplyBar(frame)
            if self.editing and frame:IsShown() then entry.handle:Show() else entry.handle:Hide() end
        end
    end
end
function L:Select(name)
    if self.entries[name] then self.selected=name end
    if self.selectionChanged then self.selectionChanged() end
end
function L:SelectNext()
    self:RefreshBars()
    local start=0
    for i,spec in ipairs(specs) do if spec[1]==self.selected then start=i end end
    for n=1,#specs do
        local spec=specs[(start+n-1)%#specs+1]
        if self.entries[spec[1]] then self:Select(spec[1]);return end
    end
end
function L:SetSelectedScale(value)
    if InCombatLockdown() or not finite(value) then return end
    local entry=self.entries[self.selected]
    if not entry then return end
    local saved=self:DB().bars[entry.name]
    if type(saved)~="table" then saved={} end
    saved.scale=clampScale(value);self:DB().bars[entry.name]=saved
    self:ApplyBar(entry.frame)
end
function L:SetEditing(value)
    if value and InCombatLockdown() then return end
    self.editing=value and true or false;self:RefreshBars()
    if addon.Inventory and addon.Inventory.SetEditing then addon.Inventory:SetEditing(self.editing) end
    if not self.editBar and self.editing then
        local bar=CreateFrame("Frame","DrakeishUILayoutEditBar",UIParent)
        self.editBar=bar
        bar:SetSize(420,46);bar:SetPoint("TOP",UIParent,"TOP",0,-30);bar:SetFrameStrata("DIALOG")
        bar:SetBackdrop({bgFile="Interface\\Tooltips\\UI-Tooltip-Background",edgeFile="Interface\\Tooltips\\UI-Tooltip-Border",edgeSize=14})
        bar:SetBackdropColor(.03,.08,.12,.98)
        local text=bar:CreateFontString(nil,"OVERLAY","GameFontHighlightSmall")
        text:SetPoint("LEFT",14,0);text:SetText("Drag blue handles. Resize in Modern UI layout.")
        local done=CreateFrame("Button",nil,bar,"UIPanelButtonTemplate")
        done:SetSize(90,24);done:SetPoint("RIGHT",-12,0);done:SetText("Lock")
        done:SetScript("OnClick",function() L:SetEditing(false) end)
    end
    if self.editBar then if self.editing then self.editBar:Show() else self.editBar:Hide() end end
end
function L:ResetBars()
    if InCombatLockdown() then return end
    local db=self:DB()
    db.bars={};db.bagX=nil;db.bagY=nil;self.applying=true
    for _,entry in pairs(self.scaleBases) do entry.frame:SetScale(entry.scale) end
    if DrakeishUI_MinimapBorder then DrakeishUI_MinimapBorder:SetScale(1) end
    for _,entry in pairs(self.entries) do
        entry.frame:SetScale(entry.scale);entry.frame:ClearAllPoints()
        if entry.point[1] then entry.frame:SetPoint(unpack(entry.point)) end
    end
    if MinimalChatAnchor then
        MinimalChatAnchor:ClearAllPoints();MinimalChatAnchor:SetPoint("BOTTOMLEFT",UIParent,"BOTTOMLEFT",16,40)
        db.bars.MinimalChatAnchor={x=166/UIParent:GetWidth(),y=90/UIParent:GetHeight(),scale=1}
    end
    self.applying=false
    if MultiActionBar_Update then MultiActionBar_Update() end
    self:RefreshBars()
    if addon.Inventory then addon.Inventory:InvalidateLayout() end
    if self.selectionChanged then self.selectionChanged() end
end
function L:RefreshBags()
    if InCombatLockdown() then self.pending=true;return end
    local db=self:DB()
    CONTAINER_SPACING=db.bagSpacing;VISIBLE_CONTAINER_SPACING=db.bagSpacing;CONTAINER_MAX_SCALE=db.bagScale
    if addon.Inventory then addon.Inventory:InvalidateLayout() end
    if updateContainerFrameAnchors then updateContainerFrameAnchors() end
end
function DrakeishUI_GetBagLayoutOptions()
    local db=L:DB()
    return db.bagScale,db.bagSpacing
end

local panel
local function label(parent,text,x,y,font)
    local fs=parent:CreateFontString(nil,"ARTWORK",font or "GameFontHighlightSmall")
    fs:SetPoint("TOPLEFT",parent,"TOPLEFT",x,y);fs:SetPoint("TOPRIGHT",parent,"TOPRIGHT",-24,y)
    fs:SetJustifyH("LEFT");fs:SetHeight(0);fs:SetText(text)
end
local function button(parent,text,x,y,width,fn)
    local b=CreateFrame("Button",nil,parent,"UIPanelButtonTemplate")
    b:SetSize(width,24);b:SetPoint("TOPLEFT",x,y);b:SetText(text);b:SetScript("OnClick",fn)
end
function L:OptionsPanel()
    if panel then return panel end
    panel=CreateFrame("Frame","DrakeishUILayoutOptions",UIParent);panel:Hide()
    panel.name="Modern UI layout";panel.parent="DrakeishUI"
    label(panel,"Modern UI layout",16,-16,"GameFontNormalLarge")
    label(panel,"Move visible elements with the blue handles, then lock them. Settings save per character.",16,-44)
    button(panel,"Move elements",20,-92,145,function() L:SetEditing(true);HideUIPanel(InterfaceOptionsFrame) end)
    button(panel,"Lock elements",175,-92,145,function() L:SetEditing(false) end)
    button(panel,"Reset elements",330,-92,145,function() L:ResetBars() end)
    local choose=CreateFrame("Frame","DrakeishUILayoutElementDropdown",panel,"UIDropDownMenuTemplate")
    choose:SetPoint("TOPLEFT",panel,"TOPLEFT",4,-136)
    UIDropDownMenu_SetWidth(choose,220)
    UIDropDownMenu_Initialize(choose,function(_,level)
        if level and level~=1 then return end
        L:RefreshBars()
        for _,spec in ipairs(specs) do
            local name,label=spec[1],spec[2]
            local info=UIDropDownMenu_CreateInfo()
            info.text=label;info.value=name;info.checked=(L.selected==name)
            if L.entries[name] then
                info.func=function() L:Select(name) end
            else
                info.disabled=true
                info.tooltipTitle=label
                info.tooltipText="This element is not available yet."
                info.tooltipWhileDisabled=true
                info.tooltipOnButton=true
            end
            UIDropDownMenu_AddButton(info)
        end
    end)
    UIDropDownMenu_SetText(choose,"Select an element")
    local scale=CreateFrame("Slider","DrakeishUILayoutElementScale",panel,"OptionsSliderTemplate")
    scale:SetSize(360,18);scale:SetPoint("TOPLEFT",28,-195);scale:SetMinMaxValues(.5,1.5);scale:SetValueStep(.05)
    _G[scale:GetName().."Low"]:SetText("50%");_G[scale:GetName().."High"]:SetText("150%")
    scale:SetScript("OnValueChanged",function(_,value)
        value=math.floor(value/.05+.5)*.05
        _G[scale:GetName().."Text"]:SetText("Selected element scale ("..math.floor(value*100+.5).."%)")
        if not panel.refreshing then L:SetSelectedScale(value) end
    end)
    function L.selectionChanged()
        local entry=L.entries[L.selected]
        UIDropDownMenu_SetText(choose,entry and ("Element: "..entry.label) or "Select an element")
        local saved=entry and L:DB().bars[entry.name]
        local value=type(saved)=="table" and finite(saved.scale) and saved.scale or 1
        panel.refreshing=true;scale:SetValue(value)
        _G[scale:GetName().."Text"]:SetText("Selected element scale ("..math.floor(value*100+.5).."%)")
        panel.refreshing=false
    end
    local gap=CreateFrame("Slider","DrakeishUILayoutActionSpacing",panel,"OptionsSliderTemplate")
    gap:SetSize(360,18);gap:SetPoint("TOPLEFT",28,-253);gap:SetMinMaxValues(0,16);gap:SetValueStep(1)
    _G[gap:GetName().."Low"]:SetText("0");_G[gap:GetName().."High"]:SetText("16")
    gap:SetScript("OnValueChanged",function(_,value)
        value=math.floor(value+.5);_G[gap:GetName().."Text"]:SetText("Optional bar button spacing ("..value..")")
        if panel.refreshing or InCombatLockdown() then return end
        L:DB().actionSpacing=value
        if addon.RefreshSidebarPositions then addon.RefreshSidebarPositions() end
    end)
    button(panel,"Bag layout and sorting",20,-290,220,function() L:OpenBagOptions() end)
    label(panel,"Move action bars, player, target, focus, pet and party frames.",20,-330)
    label(panel,"You can also move chat, the minimap, buffs, cast bar and combined bags.",20,-351)
    label(panel,"An element must be visible to drag it. Layout is locked during combat.",20,-372)
    panel:SetScript("OnShow",function()
        L:RefreshBars();if not L.entries[L.selected] then L:SelectNext() end
        L.selectionChanged();panel.refreshing=true;gap:SetValue(L:DB().actionSpacing)
        _G[gap:GetName().."Text"]:SetText("Optional bar button spacing ("..L:DB().actionSpacing..")")
        panel.refreshing=false
    end)
    InterfaceOptions_AddCategory(panel)
    self:BagOptionsPanel()
    return panel
end
function L:BagOptionsPanel()
    if self.bagPanel then return self.bagPanel end
    local p=CreateFrame("Frame","DrakeishUIBagOptions",UIParent);p:Hide();self.bagPanel=p
    p.name="Modern UI bags";p.parent="DrakeishUI"
    label(p,"Modern UI bags",16,-16,"GameFontNormalLarge")
    label(p,"Customize carried bags and open the bag tools to search, filter or sort. Bank bags keep separate windows. Settings save per character.",16,-44)
    local controls={}
    local b=CreateFrame("CheckButton",nil,p,"UICheckButtonTemplate");b:SetPoint("TOPLEFT",14,-88)
    local fs=b:CreateFontString(nil,"ARTWORK","GameFontHighlight");fs:SetPoint("LEFT",b,"RIGHT",4,0);fs:SetText("Combine carried bags into one window")
    b:SetScript("OnClick",function(self)
        if InCombatLockdown() then self:SetChecked(L:DB().combined);return end
        L:DB().combined=self:GetChecked() and true or false;L:RefreshBags()
    end)
    controls[#controls+1]=function() b:SetChecked(L:DB().combined) end
    local function slider(text,y,key,lo,hi,step)
        local s=CreateFrame("Slider","DrakeishUIBagOptions"..key,p,"OptionsSliderTemplate")
        s:SetSize(400,18);s:SetPoint("TOPLEFT",28,y);s:SetMinMaxValues(lo,hi);s:SetValueStep(step)
        _G[s:GetName().."Low"]:SetText(tostring(lo));_G[s:GetName().."High"]:SetText(tostring(hi))
        local function title(value) _G[s:GetName().."Text"]:SetText(text.." ("..string.format(key=="bagScale" and "%.2f" or "%d",value)..")") end
        s:SetScript("OnValueChanged",function(_,value)
            value=math.floor(value/step+.5)*step;title(value)
            if p.refreshing or InCombatLockdown() then return end
            L:DB()[key]=value;L:RefreshBags()
        end)
        controls[#controls+1]=function() local v=L:DB()[key];s:SetValue(v);title(v) end
    end
    slider("Space between separate bags",-141,"bagSpacing",4,40,1)
    slider("Bag scale (reduced automatically to fit)",-194,"bagScale",.5,1.5,.05)
    slider("Combined bag columns",-247,"columns",8,28,1)
    slider("Combined bag item spacing",-300,"bagSlotSpacing",0,16,1)
    button(p,"Search, filter and sort bags",20,-337,260,function() if addon.Inventory then addon.Inventory:ShowTools() end end)
    button(p,"Item glow outlines",290,-337,150,function() L:OpenOutlineOptions() end)
    label(p,"Shift-drag the combined bag title to move it. The spacing and scale controls also apply to separate bags.",20,-378)
    label(p,"Bag menus can exclude individual bags from sorting. Search and filters dim nonmatching items.",20,-410)
    p:SetScript("OnShow",function() p.refreshing=true;for _,refresh in ipairs(controls) do refresh() end;p.refreshing=false end)
    InterfaceOptions_AddCategory(p)
    self:OutlineOptionsPanel()
    return p
end
function L:OutlineOptionsPanel()
    if self.outlinePanel then return self.outlinePanel end
    local p=CreateFrame("Frame","DrakeishUIBagOutlineOptions",UIParent);p:Hide();self.outlinePanel=p
    p.name="Bag item outlines";p.parent="DrakeishUI"
    label(p,"Bag item outlines",16,-16,"GameFontNormalLarge")
    label(p,"Color item edges by category in separate bags, combined bags and open bank bags. Settings save per character.",16,-44)
    local function refreshItems() if addon.Inventory then addon.Inventory:FilterItems() end end
    local function check(name,text,x,y,read,write)
        local b=CreateFrame("CheckButton",name,p,"UICheckButtonTemplate");b:SetPoint("TOPLEFT",x,y)
        local t=b:CreateFontString(nil,"ARTWORK","GameFontHighlight");t:SetPoint("LEFT",b,"RIGHT",4,0);t:SetText(text)
        b:SetScript("OnClick",function(self) write(self:GetChecked() and true or false);refreshItems() end)
        return function() b:SetChecked(read()) end
    end
    local refreshers={check("DrakeishUIBagGlowEnabled","Show category glow outlines",14,-86,
        function() return L:DB().bagGlow end,function(v) L:DB().bagGlow=v end)}
    for i,category in ipairs(self.outlineTypes) do
        local key=category[1]
        local x=((i-1)%2)*235+14;local y=-136-math.floor((i-1)/2)*47
        refreshers[#refreshers+1]=check("DrakeishUIBagGlowCategory"..i,key,x,y,
            function() return L:DB().bagGlowTypes[key] end,function(v) L:DB().bagGlowTypes[key]=v end)
        local swatch=CreateFrame("Button","DrakeishUIBagGlowColor"..i,p,"UIPanelButtonTemplate")
        swatch:SetPoint("TOPLEFT",x+169,y-4);swatch:SetSize(48,24);swatch:SetText("Color")
        swatch:SetScript("OnClick",function()
            if not ColorPickerFrame then return end
            ColorPickerFrame:Hide()
            local color=L:DB().bagGlowColors[key];local original={color[1],color[2],color[3]}
            local function apply(values)
                if values then L:DB().bagGlowColors[key]={unpack(values)}
                else L:DB().bagGlowColors[key]={ColorPickerFrame:GetColorRGB()} end
                refreshItems()
            end
            ColorPickerFrame.hasOpacity=false;ColorPickerFrame.opacityFunc=nil
            ColorPickerFrame:SetColorRGB(unpack(original))
            ColorPickerFrame.previousValues=original;ColorPickerFrame.func=function() apply() end
            ColorPickerFrame.cancelFunc=function() apply(original) end
            ColorPickerFrame:Show()
        end)
    end
    local intensity=CreateFrame("Slider","DrakeishUIBagGlowIntensity",p,"OptionsSliderTemplate")
    intensity:SetPoint("TOPLEFT",28,-354);intensity:SetSize(400,18);intensity:SetMinMaxValues(.1,1);intensity:SetValueStep(.05)
    _G[intensity:GetName().."Low"]:SetText("10%");_G[intensity:GetName().."High"]:SetText("100%")
    intensity:SetScript("OnValueChanged",function(_,v)
        v=math.floor(v/.05+.5)*.05
        _G[intensity:GetName().."Text"]:SetText("Outline brightness ("..math.floor(v*100+.5).."%)")
        if not p.refreshing then L:DB().bagGlowAlpha=v;refreshItems() end
    end)
    label(p,"Quest items take priority, followed by junk and then the item's ordinary category. Empty slots have no outline. Item quality and quest markers remain visible.",20,-395)
    p:SetScript("OnShow",function()
        p.refreshing=true;for _,refresh in ipairs(refreshers) do refresh() end
        intensity:SetValue(L:DB().bagGlowAlpha)
        _G[intensity:GetName().."Text"]:SetText("Outline brightness ("..math.floor(L:DB().bagGlowAlpha*100+.5).."%)")
        p.refreshing=false
    end)
    InterfaceOptions_AddCategory(p)
    return p
end
function L:OpenOutlineOptions()
    self:OptionsPanel()
    if InterfaceOptionsFrame_OpenToCategory then
        local p=self:OutlineOptionsPanel();InterfaceOptionsFrame_OpenToCategory(p);InterfaceOptionsFrame_OpenToCategory(p)
    end
end
function L:OpenOptions()
    local p=self:OptionsPanel()
    if InterfaceOptionsFrame_OpenToCategory then
        InterfaceOptionsFrame_OpenToCategory(p);InterfaceOptionsFrame_OpenToCategory(p)
    end
end
function L:OpenBagOptions()
    self:OptionsPanel()
    if InterfaceOptionsFrame_OpenToCategory then
        local p=self:BagOptionsPanel()
        InterfaceOptionsFrame_OpenToCategory(p);InterfaceOptionsFrame_OpenToCategory(p)
    end
end
function L:OpenBagSettings()
    local p=WaystoneInterfaceStylePanel
    if p and p._drakeishBagSettings and InterfaceOptionsFrame_OpenToCategory then
        InterfaceOptionsFrame_OpenToCategory(p);InterfaceOptionsFrame_OpenToCategory(p)
    else
        self:OpenBagOptions()
    end
end
local boot=CreateFrame("Frame")
for _,event in ipairs({"PLAYER_LOGIN","PLAYER_ENTERING_WORLD","PLAYER_REGEN_ENABLED","PLAYER_REGEN_DISABLED","DISPLAY_SIZE_CHANGED"}) do boot:RegisterEvent(event) end
boot:SetScript("OnEvent",function(_,event)
    if event=="PLAYER_REGEN_DISABLED" then
        L.editing=false
        if L.moving then L.moving.frame:StopMovingOrSizing();L.moving=nil end
        for _,entry in pairs(L.entries) do entry.handle:Hide() end
        if L.editBar then L.editBar:Hide() end
        if addon.Inventory and addon.Inventory.SetEditing then addon.Inventory:SetEditing(false) end
        return
    end
    L:RefreshBars();L:RefreshBags()
end)
L:DB()
