local _, addon = ...
local S,L=addon.Settings,addon.Layout
if not L then return end
local D={}
addon.Dimensions=D
local dimensions
local function db()
    local layout=L:DB()
    if type(layout.sizes)~="table" then layout.sizes={} end
    dimensions=layout.sizes
    return dimensions
end
function D:Value(group,key, fallback)
    if not self.enabled then return fallback end
    local t=dimensions and dimensions[group]
    local value=type(t)=="table" and t[key]
    return type(value)=="number" and value==value and math.abs(value)~=math.huge and value or fallback
end

local refresh
local function apply()
    db()
    D.enabled=S:Get("layout.resize")
    refresh()
end
S:Register({id="layout.resize",name="Enable layout dimensions",category="Layout",group="Dimensions",type="toggle",
    default=true,protected=true,apply=apply})
local definitions={
    { "cast","Cast Bar","Cast bar", {{"width","Width",120,500},{"height","Height",12,50}} },
    { "tracker","Layout","Objective tracker", {{"width","Width",160,480},{"height","Height",100,650}} },
    { "minimap","Minimap","Minimap", {{"size","Diameter",100,280}} },
    { "buffs","Buffs","Buff rows", {{"columns","Icons per row",6,32},{"size","Icon size",16,48},{"gap","Spacing",0,16}} },
    { "debuffs","Buffs","Debuff rows", {{"columns","Icons per row",6,32},{"size","Icon size",16,48},{"gap","Spacing",0,16}} },
    { "raid","Party and Raid","Raid dimensions", {{"width","Cell width",54,180},{"height","Cell height",16,80},{"gap","Group spacing",0,16}} },
    { "main","Action Bars","Main bar dimensions", {{"size","Button size",20,64},{"gap","Spacing",0,16},{"columns","Buttons per row",1,12}} },
    { "bottomLeft","Action Bars","Bottom left bar dimensions", {{"size","Button size",20,64},{"gap","Spacing",0,16},{"columns","Buttons per row",1,12}} },
    { "bottomRight","Action Bars","Bottom right bar dimensions", {{"size","Button size",20,64},{"gap","Spacing",0,16},{"columns","Buttons per row",1,12}} },
    { "right","Action Bars","Right bar dimensions", {{"size","Button size",20,64},{"gap","Spacing",0,16},{"columns","Buttons per row",1,12}} },
    { "left","Action Bars","Left bar dimensions", {{"size","Button size",20,64},{"gap","Spacing",0,16},{"columns","Buttons per row",1,12}} },
}
local editorSpecs={}
for _, definition in ipairs(definitions) do
    local group, category, label, fields=unpack(definition)
    editorSpecs[group]=fields
    for _, field in ipairs(fields) do
        local key,name,min,max=unpack(field)
        -- A negative number restores the existing dimension, leaving zero available as a spacing.
        local id="layout."..group.."."..key
        field.id=id
        S:Register({id=id,name=name.." (-1 existing)",category=category,group=label,type="slider",default=-1,
            min=-1,max=max,step=1,protected=true,keywords="size width height spacing "..label,
            validate=function(value)
                if type(value)~="number" or value~=value or math.abs(value)==math.huge then return end
                if value==-1 then return -1 end
                return math.floor(math.max(min,math.min(max,value))+.5)
            end,
            localGet=function() local groupDB=db()[group];return type(groupDB)=="table" and groupDB[key] or nil end,
            localSet=function(value)
                local data=db()
                if type(data[group])~="table" then data[group]={} end
                data[group][key]=(value~=-1) and value or nil
                refresh(group)
            end})
    end
end
editorSpecs.chat={{"width","Width",240,900,id="chat.width"},{"height","Height",80,500,id="chat.height"}}

local originals=setmetatable({},{__mode="k"})
local function setSize(frame,w,h)
    if not frame then return end
    local old=originals[frame]
    if not old then
        if not w and not h then return end
        old={w=frame:GetWidth(),h=frame:GetHeight()}
        originals[frame]=old
    end
    local width,height=w or old.w,h or old.h
    if frame:GetWidth()~=width or frame:GetHeight()~=height then frame:SetSize(width,height) end
    if not w and not h then originals[frame]=nil end
end
local function cast()
    local frame=ImprovedUICastBar
    if not frame then return end
    local w,h=D:Value("cast","width"),D:Value("cast","height")
    setSize(frame,w,h)
    setSize(frame.Border,w and w+2,h and h+4)
    setSize(frame.Background,w,h)
    for _, tick in ipairs(frame.Ticks or {}) do setSize(tick,nil,h and h-2) end
end
local function minimap()
    local size=D:Value("minimap","size")
    setSize(Minimap,size,size)
    -- The ring art is drawn at a fixed proportion of the map (160 around the stock 140), so it grows and shrinks
    -- with the map. A fixed margin let the map spill past the ring on larger sizes.
    local ring=DrakeishUI_MinimapBorder
    local stock=originals[Minimap]
    if ring and size and stock and stock.w>0 then
        local base=originals[ring]
        if not base then base={w=ring:GetWidth(),h=ring:GetHeight()};originals[ring]=base end
        setSize(ring,base.w*size/stock.w,base.h*size/stock.w)
    else
        setSize(ring)
    end
end
local function tracker()
    setSize(WatchFrame,D:Value("tracker","width"),D:Value("tracker","height"))
end

local barSpecs={
    { "main","pUiMainBar","ActionButton",12 }, { "main","pUiMainBar","BonusActionButton",12 },
    { "bottomLeft","MultiBarBottomLeft","MultiBarBottomLeftButton",12 },
    { "bottomRight","MultiBarBottomRight","MultiBarBottomRightButton",12 },
    { "right","MultiBarRight","MultiBarRightButton",1 }, { "left","MultiBarLeft","MultiBarLeftButton",1 },
}
local actionPoints=setmetatable({},{__mode="k"})
local function actions()
    if InCombatLockdown() then return end
    local stockSize=ActionButton1 and (originals[ActionButton1] and originals[ActionButton1].w or ActionButton1:GetWidth()) or 36
    for _, spec in ipairs(barSpecs) do
        local group,holder,prefix,stockColumns=unpack(spec)
        local frame=_G[holder]
        local buttonSize,gap,cols=D:Value(group,"size"),D:Value(group,"gap"),D:Value(group,"columns")
        local custom=buttonSize or gap or cols
        local size=buttonSize or stockSize
        gap,cols=gap or L:DB().actionSpacing,cols or stockColumns
        local vertical=stockColumns==1
        if frame then
            if group~="main" then
                if custom then
                    setSize(frame,cols*size+(cols-1)*gap,math.ceil(12/cols)*(size+gap)-gap)
                elseif originals[frame] then
                    originals[frame]=nil
                    frame:SetSize(stockColumns*stockSize+(stockColumns-1)*gap,math.ceil(12/stockColumns)*(stockSize+gap)-gap)
                end
            elseif custom then
                -- The main bar's holder carries the metal border art; keep it the size of the buttons it frames
                -- (the stock holder is 526 x 42: twelve 37 px buttons, eleven 7 px gaps and a 5 px margin).
                local rows=math.ceil(12/cols)
                setSize(frame,cols*size+(cols-1)*gap+5,rows*size+(rows-1)*gap+5)
            elseif originals[frame] then
                setSize(frame)
            end
            local first=_G[prefix..1]
            for i=1,12 do
                local button=_G[prefix..i]
                if button and first then
                    local saved=actionPoints[button]
                    if custom and not saved then
                        saved={};actionPoints[button]=saved
                        for j=1,button:GetNumPoints() do saved[j]={button:GetPoint(j)} end
                    end
                    setSize(button,buttonSize,buttonSize)
                    if custom and (group~="main" or i>1) then
                        local x, y=((i-1)%cols)*(size+gap),math.floor((i-1)/cols)*(size+gap)
                        local point=group=="main" and "BOTTOMLEFT" or vertical and "TOPLEFT" or "BOTTOMLEFT"
                        button:ClearAllPoints()
                        button:SetPoint(point,group=="main" and first or frame,point,x,vertical and -y or y)
                    elseif not custom and saved then
                        button:ClearAllPoints()
                        for _, points in ipairs(saved) do button:SetPoint(unpack(points)) end
                        actionPoints[button]=nil
                    end
                end
            end
        end
    end
end

local hadAuraLayout
function D:ApplyAuras(offset)
    if not BuffFrame or InCombatLockdown() then return end
    local buffSize,debuffSize=self:Value("buffs","size"),self:Value("debuffs","size")
    local cols,spacing=self:Value("buffs","columns"),self:Value("buffs","gap")
    local dcols,dspacing=self:Value("debuffs","columns"),self:Value("debuffs","gap")
    local custom=buffSize or debuffSize or cols or spacing or dcols or dspacing
    if not custom and not hadAuraLayout then return end
    hadAuraLayout=not not custom
    cols,spacing,dcols,dspacing=cols or 10,spacing or 5,dcols or 16,dspacing or 5
    local count,first=0,nil
    local function place(icon)
        if not icon then return end
        setSize(icon,buffSize,buffSize)
        if icon.styled and icon.border then setSize(icon.border,buffSize and buffSize*1.1,buffSize and buffSize*1.1) end
        if not custom or not icon:IsShown() then return end
        first=first or icon
        local size=buffSize or icon:GetWidth()
        icon:ClearAllPoints()
        icon:SetPoint("TOPRIGHT",BuffFrame,"TOPRIGHT",offset-(count%cols)*(size+spacing),-math.floor(count/cols)*(size+18))
        count=count+1
    end
    for i=1,3 do place(_G["TempEnchant"..i]) end
    for i=1,BUFF_ACTUAL_DISPLAY or 32 do place(_G["BuffButton"..i]) end
    local y=math.max(1,math.ceil(count/cols))*((buffSize or (first and first:GetHeight()) or 30)+18)+12
    for i=1,16 do
        local icon=_G["DebuffButton"..i]
        if icon then
            setSize(icon,debuffSize,debuffSize)
            if icon.styled and icon.border then setSize(icon.border,debuffSize and debuffSize*1.1,debuffSize and debuffSize*1.1) end
            if custom then
                local size=debuffSize or icon:GetWidth()
                icon:ClearAllPoints()
                icon:SetPoint("TOPRIGHT",BuffFrame,"TOPRIGHT",offset-((i-1)%dcols)*(size+dspacing),
                    -y-math.floor((i-1)/dcols)*(size+18))
            end
        end
    end
end

local names={MinimalChatAnchor="chat",ImprovedUICastBar="cast",CastingBarFrame="cast",WatchFrame="tracker",Minimap="minimap",
    DrakeishUI_RaidFrames="raid",BuffFrame="buffs",pUiMainBar="main",
    MultiBarBottomLeft="bottomLeft",MultiBarBottomRight="bottomRight",MultiBarRight="right",MultiBarLeft="left"}
local function selected()
    return L.entries[L.selected], names[L.selected]
end
local controls, drag
local function editPanel()
    local host=DrakeishUILayoutCoords
    if not host then return end
    if not controls then
        controls={height=host:GetHeight()}
        for i=1,3 do
            local label=host:CreateFontString(nil,"OVERLAY","GameFontNormalSmall")
            label:SetPoint("TOPLEFT",host,"TOPLEFT",14+(i-1)*134,-69)
            local input=CreateFrame("EditBox",nil,host,"InputBoxTemplate")
            input:SetSize(48,21);input:SetPoint("LEFT",label,"RIGHT",7,0)
            input:SetAutoFocus(false);input:SetMaxLetters(5)
            input:SetScript("OnEscapePressed",function(self) self:ClearFocus() end)
            input:SetScript("OnEnterPressed",function(self)
                local value=tonumber(self:GetText())
                self:ClearFocus()
                if value and self.option then S:Set(self.option,value) end
                editPanel()
            end)
            controls[i]={label=label,input=input}
        end
        local grip=CreateFrame("Button",nil,UIParent)
        grip:SetFrameStrata("DIALOG");grip:SetSize(19,19)
        grip:SetNormalTexture("Interface\\ChatFrame\\UI-ChatIM-SizeGrabber-Up")
        grip:RegisterForDrag("LeftButton")
        grip:SetScript("OnDragStart",function()
            local entry,group=selected()
            if not entry or not group or InCombatLockdown() then return end
            local x,y=GetCursorPosition()
            local width,height=entry.frame:GetSize()
            if group=="raid" then
                local cell=DrakeishUI_RaidUnit1
                if cell then width,height=cell:GetSize() end
            elseif group=="cast" and ImprovedUICastBar then width,height=ImprovedUICastBar:GetSize()
            end
            drag={group=group,x=x,y=y,w=width,h=height,scale=entry.frame:GetEffectiveScale()}
        end)
        grip:SetScript("OnDragStop",function()
            if not drag then return end
            local d=drag;drag=nil
            if InCombatLockdown() then return end
            local x,y=GetCursorPosition()
            local fields=editorSpecs[d.group]
            S:Set(fields[1].id,d.w+(x-d.x)/d.scale)
            if fields[2] then S:Set(fields[2].id,d.h+(d.y-y)/d.scale) end
            editPanel()
        end)
        controls.grip=grip
    end
    local entry,group=selected()
    local fields=group and editorSpecs[group]
    local show=D.enabled and fields and L.editing and host:IsShown() and not InCombatLockdown()
    host:SetHeight(controls.height+(show and 27 or 0))
    for i, pair in ipairs(controls) do
        local field=show and fields[i]
        if field and S.byID[field.id] then
            pair.input.option=field.id
            pair.label:SetText(field[2])
            if not pair.input:HasFocus() then pair.input:SetText(tostring(S:Get(field.id))) end
            pair.label:Show();pair.input:Show()
        else pair.input:ClearFocus();pair.input:Hide();pair.label:Hide() end
    end
    local canDrag=group=="cast" or group=="tracker" or group=="minimap" or group=="raid"
    if show and entry and canDrag and entry.frame:IsVisible() then
        controls.grip:ClearAllPoints()
        controls.grip:SetPoint("BOTTOMRIGHT",entry.frame,"BOTTOMRIGHT")
        controls.grip:Show()
    else controls.grip:Hide() end
    if addon.Appearance then addon.Appearance.StyleLayout() end
end
refresh=function(group)
    if not S:IsReady() or InCombatLockdown() then return end
    if not group or group=="cast" then cast() end
    if not group or group=="minimap" then minimap() end
    if not group or group=="tracker" then tracker() end
    if (not group or group=="raid") and DrakeishUI_UpdateRaidFrames then DrakeishUI_UpdateRaidFrames() end
    if (not group or group=="buffs" or group=="debuffs") and BuffFrame_UpdateAllBuffAnchors then BuffFrame_UpdateAllBuffAnchors() end
    if not group or group=="main" or group=="bottomLeft" or group=="bottomRight" or group=="left" or group=="right" then
        if addon.RefreshSidebarPositions then addon.RefreshSidebarPositions() end
        actions()
    end
    L:RefreshBars()
    editPanel()
end
local function install()
    hooksecurefunc(L,"Select",editPanel)
    hooksecurefunc(L,"SetEditing",editPanel)
    hooksecurefunc(L,"RefreshBars",editPanel)
    if MultiActionBar_Update then hooksecurefunc("MultiActionBar_Update",actions) end
    if WatchFrame_Update then hooksecurefunc("WatchFrame_Update",function() if S:IsReady() and not InCombatLockdown() then tracker() end end) end
    if CastingBarFrame then CastingBarFrame:HookScript("OnShow",function() if S:IsReady() and not InCombatLockdown() then cast(); L:RefreshBars() end end) end
end
local events=CreateFrame("Frame")
events:RegisterEvent("ADDON_LOADED")
events:RegisterEvent("PLAYER_ENTERING_WORLD")
events:RegisterEvent("PLAYER_REGEN_ENABLED")
events:RegisterEvent("PLAYER_REGEN_DISABLED")
events:SetScript("OnEvent",function(self,event,name)
    if event=="ADDON_LOADED" then
        if name=="DrakeishUI" then install(); self:UnregisterEvent(event) end
    elseif event=="PLAYER_REGEN_DISABLED" then drag=nil; editPanel()
    else refresh() end
end)
