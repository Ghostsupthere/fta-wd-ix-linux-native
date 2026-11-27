-- Reposition the existing message frames; their native text, fades, events and
-- visibility remain owned by FrameXML. Positions use a fraction of screen height.
local addon = select(2, ...)
local M = {entries = {}}
addon.Messages = M
local specs = {
    {key="zone", name="ZoneTextFrame", label="Zone and subzone names", value=18, point="CENTER", font="ZoneTextFont"},
    {key="info", name="UIErrorsFrame", label="Quest progress, errors and notices", value=28, point="TOP", font="ErrorFont"},
    {key="raid", name="RaidWarningFrame", label="Raid warnings", value=38, point="TOP", font="GameFontNormalHuge"},
    {key="boss", name="RaidBossEmoteFrame", label="Boss messages", value=46, point="TOP", font="GameFontNormalHuge"},
}
local function percent(value, fallback)
    if type(value)~="number" or value~=value or math.abs(value)==math.huge then return fallback end
    return math.max(5, math.min(70, math.floor(value+.5)))
end
function M:DB()
    local layout = addon.Layout:DB()
    if type(layout.messages)~="table" then layout.messages={} end
    local db = layout.messages
    if type(db.enabled)~="boolean" then db.enabled=true end
    for _,spec in ipairs(specs) do db[spec.key]=percent(db[spec.key], spec.value) end
    return db
end
local function points(frame)
    local result = {}
    for i=1,frame:GetNumPoints() do result[i]={frame:GetPoint(i)} end
    return result
end
local function scale(frame)
    return frame:GetEffectiveScale()
end
function M:Apply()
    if self.applying then return end
    if InCombatLockdown() then self.pending=true;return end
    self.pending=false
    local db = self:DB()
    self.applying=true
    for _,spec in ipairs(specs) do
        local frame = _G[spec.name]
        if frame then
            local entry = self.entries[spec.key]
            if not entry then
                entry={frame=frame,original=points(frame)};self.entries[spec.key]=entry
                hooksecurefunc(frame,"SetPoint",function()
                    if M.applying then return end
                    entry.original=points(frame)
                    M:Apply()
                end)
            end
            if db.enabled then
                local height = UIParent:GetHeight()*scale(UIParent)/scale(frame)
                frame:ClearAllPoints()
                frame:SetPoint(spec.point,UIParent,"TOP",0,-height*db[spec.key]/100)
                entry.owned=true
            elseif entry.owned then
                frame:ClearAllPoints()
                for _,point in ipairs(entry.original) do frame:SetPoint(unpack(point)) end
                entry.owned=false
            end
        end
    end
    self.applying=false
    self:PlacePreview()
end
function M:PlacePreview()
    if not self.preview or not self.preview:IsShown() then return end
    local db=self:DB()
    for _,spec in ipairs(specs) do
        local text=self.preview.labels[spec.key]
        text:ClearAllPoints()
        text:SetPoint(spec.point,UIParent,"TOP",0,-UIParent:GetHeight()*db[spec.key]/100)
    end
end
function M:HidePreview()
    if self.preview then self.preview:Hide();self.preview:SetScript("OnUpdate",nil) end
end
function M:Preview()
    if InCombatLockdown() or not self:DB().enabled then return end
    if not self.preview then
        local frame=CreateFrame("Frame",nil,UIParent);self.preview=frame
        frame:SetAllPoints(UIParent);frame:SetFrameStrata("TOOLTIP");frame:EnableMouse(false)
        frame.labels={}
        for _,spec in ipairs(specs) do
            local text=frame:CreateFontString(nil,"OVERLAY",spec.font)
            text:SetText(spec.label.." (preview)")
            frame.labels[spec.key]=text
        end
    end
    self.preview.remaining=8
    self.preview:SetScript("OnUpdate",function(frame,elapsed)
        frame.remaining=frame.remaining-elapsed
        if frame.remaining<=0 then M:HidePreview() end
    end)
    self.preview:Show();self:PlacePreview()
end
function M:OptionsPanel()
    if self.panel then return self.panel end
    local panel=CreateFrame("Frame","DrakeishUIScreenMessages",UIParent);self.panel=panel;panel:Hide()
    panel.name="Screen messages";panel.parent="DrakeishUI"
    local function label(text,y,font)
        local fs=panel:CreateFontString(nil,"ARTWORK",font or "GameFontHighlightSmall")
        fs:SetPoint("TOPLEFT",16,y);fs:SetPoint("TOPRIGHT",-20,y)
        fs:SetJustifyH("LEFT");fs:SetHeight(0);fs:SetText(text)
    end
    label("Screen messages",-16,"GameFontNormalLarge")
    label("Move the text shown across the screen. Higher percentages place text farther down. Changes apply immediately and save per character.",-48)
    local enabled=CreateFrame("CheckButton","DrakeishUIScreenMessagesEnabled",panel,"InterfaceOptionsCheckButtonTemplate")
    enabled:SetPoint("TOPLEFT",14,-90)
    _G[enabled:GetName().."Text"]:SetText("Use custom message positions")
    local controls={}
    local function refresh()
        panel.refreshing=true
        local db=M:DB();enabled:SetChecked(db.enabled)
        for _,spec in ipairs(specs) do
            local slider=controls[spec.key]
            slider:SetValue(db[spec.key]);slider:EnableMouse(db.enabled)
            slider:SetAlpha(db.enabled and 1 or .5)
            _G[slider:GetName().."Text"]:SetText(spec.label.." ("..db[spec.key].."% from top)")
        end
        panel.refreshing=false
    end
    enabled:SetScript("OnClick",function(self)
        if not InCombatLockdown() then M:DB().enabled=self:GetChecked() and true or false;M:Apply() end
        M:HidePreview();refresh()
    end)
    for i,spec in ipairs(specs) do
        local slider=CreateFrame("Slider","DrakeishUIScreenMessages"..spec.key,panel,"OptionsSliderTemplate")
        controls[spec.key]=slider
        slider:SetSize(400,18);slider:SetPoint("TOPLEFT",28,-148-(i-1)*55)
        slider:SetMinMaxValues(5,70);slider:SetValueStep(1)
        _G[slider:GetName().."Low"]:SetText("Top (5%)")
        _G[slider:GetName().."High"]:SetText("Lower (70%)")
        slider:SetScript("OnValueChanged",function(_,value)
            if panel.refreshing then return end
            if not InCombatLockdown() and M:DB().enabled then
                M:DB()[spec.key]=percent(value,spec.value);M:Apply();M:Preview()
            end
            refresh()
        end)
    end
    local function button(name,text,x,callback)
        local b=CreateFrame("Button",name,panel,"UIPanelButtonTemplate")
        b:SetSize(190,24);b:SetPoint("TOPLEFT",x,-359);b:SetText(text);b:SetScript("OnClick",callback)
    end
    button("DrakeishUIScreenMessagesPreview","Preview positions",20,function() M:Preview() end)
    button("DrakeishUIScreenMessagesReset","Reset message positions",224,function()
        if InCombatLockdown() then return end
        local db=M:DB();db.enabled=true
        for _,spec in ipairs(specs) do db[spec.key]=spec.value end
        M:Apply();refresh();M:Preview()
    end)
    label("Zone names keep their subzone and PvP labels together. Combat text, chat, and addons' own alerts keep their existing settings. Position changes are locked during combat.",-404)
    panel:SetScript("OnShow",refresh)
    panel:SetScript("OnHide",function() M:HidePreview() end)
    InterfaceOptions_AddCategory(panel)
    return panel
end
function M:OpenOptions()
    local panel=self:OptionsPanel()
    InterfaceOptionsFrame_OpenToCategory(panel);InterfaceOptionsFrame_OpenToCategory(panel)
end
local boot=CreateFrame("Frame")
for _,event in ipairs({"PLAYER_LOGIN","PLAYER_ENTERING_WORLD","PLAYER_REGEN_ENABLED","PLAYER_REGEN_DISABLED","DISPLAY_SIZE_CHANGED"}) do boot:RegisterEvent(event) end
boot:SetScript("OnEvent",function(_,event)
    if event=="PLAYER_REGEN_DISABLED" then M:HidePreview();return end
    M:Apply()
end)
UIParent:HookScript("OnSizeChanged",function() M:Apply() end)
