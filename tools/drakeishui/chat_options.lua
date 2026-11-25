local _, addon = ...
local S, L = addon.Settings, addon.Layout
local A = MinimalChatAnchor
if not A or not L then return end
local C = {}
addon.ChatOptions=C
local cfg, original, tabs = {}, {}, {}
local refresh
local function apply() refresh() end
local function choices(...)
    local a, t={...},{}
    for i=1,#a,2 do t[#t+1]={value=a[i],name=a[i+1]} end
    return t
end
local fonts=choices("inherit","Inherit","client","Client default","expressway","Expressway","calibri","Calibri")
local outlines=choices("legacy","Existing outline","","None","OUTLINE","Outline","THICKOUTLINE","Thick outline")
local function option(key,name,kind,default,extra,group)
    local o=extra or {}
    o.id,o.name,o.type,o.default="chat."..key,name,kind,default
    o.category,o.group,o.apply="Chat",group or "Chat appearance",apply
    if o.choices then
        local valid={}
        for _, c in ipairs(o.choices) do valid[c.value]=true end
        o.validate=function(value) if valid[value] then return value end end
    end
    S:Register(o)
    if not o.localGet then cfg[key]=default end
end
local function sizeOption(minimum,maximum)
    return {min=0,max=maximum,step=1,validate=function(value)
        if type(value)~="number" or value~=value or math.abs(value)==math.huge then return end
        if value==0 then return 0 end
        return math.floor(math.max(minimum,math.min(maximum,value))+.5)
    end}
end
option("enabled","Enable chat customization","toggle",true)
option("font","Chat font","font","inherit",{choices=fonts})
option("size","Chat font size (0 keeps current)","slider",0,sizeOption(8,28))
option("outline","Chat outline","dropdown","legacy",{choices=outlines})
option("spacing","Line spacing (-1 keeps current)","slider",-1,{min=-1,max=8,step=1})
option("background","Background color","colour",{0,0,0})
option("opacity","Background opacity","slider",0,{min=0,max=1,step=.01})
option("border","Show chat border","toggle",false)
option("fade","Fade delay","slider",-1,{
    min=-1,max=120,step=1,desc="Seconds before chat fades. -1 keeps the existing delay. 0 disables fading.",
})
local retainedLines = sizeOption(50,1000)
retainedLines.desc = "Number of chat lines to keep. 0 keeps the existing limit."
option("lines","Retained lines","slider",0,retainedLines)
option("time","Timestamps","dropdown","off",{choices=choices("off","Off","short","HH:MM","seconds","HH:MM:SS","12h","12-hour")})
option("class","Class-colored chat names","dropdown","legacy",{choices=choices("legacy","Existing settings","on","On","off","Off")})
option("shorten","Shorten linked channel names","toggle",false)
option("tabs","Chat tabs","dropdown","hidden",{choices=choices("hidden","Hidden","always","Always","hover","Mouse-over")},"Tabs")
option("tabFont","Tab font","font","inherit",{choices=fonts},"Tabs")
option("tabSize","Tab font size","slider",12,{min=8,max=22,step=1},"Tabs")
option("tabOutline","Tab outline","dropdown","legacy",{choices=outlines},"Tabs")

local function db()
    local layout=L:DB()
    layout.chatSize=type(layout.chatSize)=="table" and layout.chatSize or {}
    return layout.chatSize
end
local function limits()
    local scale=A:GetEffectiveScale()/UIParent:GetEffectiveScale()
    return math.min(900,math.max(1,UIParent:GetWidth()/scale-32)),
           math.min(500,math.max(1,UIParent:GetHeight()/scale-110))
end
function C:GetInset()
    if cfg.enabled and (cfg.border or cfg.opacity>0) then return 5 end
    return 0
end

local function showSize(w,h)
    local mw,mh=limits()
    A:SetSize(math.min(mw,math.max(240,w)),math.min(mh,math.max(80,h)))
    if A._drakeishLayoutChanged then A._drakeishLayoutChanged() end
end
function C:ApplySize()
    local saved=db()
    local width,height=tonumber(saved.width) or 300,tonumber(saved.height) or 100
    if width~=width or height~=height then width,height=300,100 end
    if not cfg.enabled then width,height=300,100 end
    showSize(width,height)
    if L.entries[A:GetName()] then L:ApplyBar(A) end
end
for _, spec in ipairs({{"width",300,240,900},{"height",100,80,500}}) do
    local key, default, low, high=unpack(spec)
    option(key,"Chat "..key,"slider",default,{min=low,max=high,step=1,
        localGet=function() return db()[key] end,
        localSet=function(value) db()[key]=value; C:ApplySize() end},"Chat size")
end

local function viewFont(fs,group,fontKey,size,flags,fallback)
    local base = fontKey=="expressway" and addon._dir.."expressway.ttf" or
                 fontKey=="calibri" and addon._dir.."Calibri1.ttf" or
                 fontKey=="client" and STANDARD_TEXT_FONT or fallback
    if addon.Appearance and fontKey=="inherit" then addon.Appearance:ApplyFont(fs,group,base,size,flags)
    elseif base then fs:SetFont(base,size,flags) end
end

local legacyClass
local function classColors()
    if not SetChatColorNameByClass or not ChatTypeInfo then return end
    local saved=L:DB()
    legacyClass=saved.chatClassColors
    if cfg.enabled and cfg.class~="legacy" then
        if type(legacyClass)~="table" then legacyClass={}; saved.chatClassColors=legacyClass end
        local types={"SAY","YELL","GUILD","OFFICER","PARTY","PARTY_LEADER","RAID","RAID_LEADER",
                     "RAID_WARNING","BATTLEGROUND","BATTLEGROUND_LEADER","WHISPER","WHISPER_INFORM"}
        for i=1,20 do types[#types+1]="CHANNEL"..i end
        for _, kind in ipairs(types) do
            local info=ChatTypeInfo[kind]
            if info then
                if legacyClass[kind]==nil then legacyClass[kind]=info.colorNameByClass==true or info.colorNameByClass==1 end
                SetChatColorNameByClass(kind,cfg.class=="on")
            end
        end
    elseif type(legacyClass)=="table" then
        for kind, value in pairs(legacyClass) do SetChatColorNameByClass(kind,value) end
        saved.chatClassColors=nil
    end
end
local function styleChat(frame)
    if not frame then return end
    local record=original[frame]
    if not record then
        local font,size,flags=frame:GetFont()
        record={font=font,size=size,flags=flags,spacing=frame:GetSpacing(),fading=not not frame:GetFading(),
            delay=frame:GetTimeVisible(),lines=frame:GetMaxLines()}
        original[frame]=record
        local oldAdd=frame.AddMessage
        frame.AddMessage=function(self,message,...)
            if cfg.enabled and type(message)=="string" then
                if cfg.shorten then
                    message=message:gsub("(|Hchannel:[^|]+|h)%[(%d+)%. [^%]]+%](|h)","%1[%2]%3")
                end
                local pattern=cfg.time=="seconds" and "%H:%M:%S" or cfg.time=="short" and "%H:%M" or cfg.time=="12h" and "%I:%M %p"
                if pattern then message="["..date(pattern).."] "..message end
            end
            return oldAdd(self,message,...)
        end
    end
    local use=cfg.enabled
    local size=use and cfg.size>0 and cfg.size or record.size
    local flags=use and cfg.outline~="legacy" and cfg.outline or record.flags
    if size then viewFont(frame,"chat",use and cfg.font or "legacy",size,flags,record.font) end
    frame:SetSpacing(use and cfg.spacing>=0 and cfg.spacing or record.spacing)
    local fading = record.fading
    if use and cfg.fade>=0 then fading=cfg.fade>0 end
    frame:SetFading(fading)
    frame:SetTimeVisible(use and cfg.fade>=0 and cfg.fade or record.delay)
    local lines=use and cfg.lines>0 and cfg.lines or record.lines
    if lines and lines>0 and frame:GetMaxLines()~=lines then frame:SetMaxLines(lines) end
end

local driver=CreateFrame("Frame",nil,A)
local resize=CreateFrame("Button",nil,A)
resize:SetSize(18,18);resize:SetPoint("BOTTOMRIGHT");resize:SetFrameStrata("DIALOG")
resize:SetNormalTexture("Interface\\ChatFrame\\UI-ChatIM-SizeGrabber-Up")
resize:RegisterForDrag("LeftButton")
resize:Hide()
local drag, elapsed
resize:SetScript("OnDragStart",function()
    if not cfg.enabled or InCombatLockdown() then return end
    local x,y=GetCursorPosition()
    drag={x,y,A:GetWidth(),A:GetHeight()}
end)
resize:SetScript("OnDragStop",function()
    if not drag then return end
    drag=nil
    if not InCombatLockdown() then db().width,db().height=A:GetWidth(),A:GetHeight(); C:ApplySize(); S:Refresh() end
end)
local function makeTabs()
    if tabs[1] then return end
    for i, label in ipairs({"Chat","Combat Log"}) do
        local btn=CreateFrame("Button",nil,A,"UIPanelButtonTemplate")
        btn:SetSize(84,20)
        btn:SetPoint("BOTTOMRIGHT",A,"TOPRIGHT",i==1 and -88 or 0,1)
        btn:SetText(label)
        btn:SetScript("OnClick",function() DrakeishUI_SetChatView(i==1 and "chat" or "combat") end)
        tabs[i]=btn
    end
end
local function visibility()
    local chatVisible=chat_state~=1
    local show=cfg.enabled and chatVisible and cfg.tabs~="hidden" and
        not (ChatFrame1EditBox and ChatFrame1EditBox:IsShown()) and
        not (ChatFrame2EditBox and ChatFrame2EditBox:IsShown())
    if show and cfg.tabs=="hover" then show=A:IsMouseOver() or tabs[1]:IsMouseOver() or tabs[2]:IsMouseOver() end
    for _, tab in ipairs(tabs) do if show then tab:Show() else tab:Hide() end end
    if L.editing and cfg.enabled and not InCombatLockdown() then resize:Show() else resize:Hide() end
end
driver:SetScript("OnUpdate",function(_,dt)
    if drag then
        if InCombatLockdown() then drag=nil; C:ApplySize(); return end
        local x,y=GetCursorPosition(); local scale=A:GetEffectiveScale()
        showSize(drag[3]+(x-drag[1])/scale,drag[4]+(drag[2]-y)/scale)
    end
    elapsed=(elapsed or 0)+dt
    if elapsed>.12 then elapsed=0; visibility() end
end)
refresh=function()
    for key in pairs(cfg) do cfg[key]=S:Get("chat."..key) end
    styleChat(ChatFrame1); styleChat(ChatFrame2)
    classColors()
    local r,g,b=unpack(cfg.background)
    A:SetBackdropColor(r,g,b,cfg.enabled and cfg.opacity or 0)
    A:SetBackdropBorderColor(1,1,1,cfg.enabled and cfg.border and 1 or 0)
    if cfg.enabled and cfg.tabs~="hidden" then
        makeTabs()
        for _, btn in ipairs(tabs) do
            viewFont(btn:GetFontString(),"chat",cfg.tabFont,cfg.tabSize,
                cfg.tabOutline=="legacy" and "" or cfg.tabOutline,STANDARD_TEXT_FONT)
        end
    end
    C:ApplySize()
    visibility()
end
C.Refresh=refresh
local events=CreateFrame("Frame")
for _, event in ipairs({"PLAYER_ENTERING_WORLD","DISPLAY_SIZE_CHANGED","UI_SCALE_CHANGED","PLAYER_REGEN_ENABLED"}) do events:RegisterEvent(event) end
events:SetScript("OnEvent",function() if S:IsReady() then refresh() end end)
