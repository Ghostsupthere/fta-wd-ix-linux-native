local _, addon = ...
local B = { active=false }
addon.KeybindMode = B
local S = addon.Settings
local panel, overlay, badge, detail
local draft, original, bindingSet
local target, action, actionLabel, clickName
local confirmation
local conflictDialog="DRAKEISHUI_KEYBIND_CONFLICT"

local nativeBars={
    ActionButton="ACTIONBUTTON", BonusActionButton="ACTIONBUTTON",
    MultiBarBottomLeftButton="MULTIACTIONBAR1BUTTON", MultiBarBottomRightButton="MULTIACTIONBAR2BUTTON",
    MultiBarRightButton="MULTIACTIONBAR3BUTTON", MultiBarLeftButton="MULTIACTIONBAR4BUTTON",
    PetActionButton="BONUSACTIONBUTTON", ShapeshiftButton="SHAPESHIFTBUTTON",
}
local function actionFor(button)
    local name=button and button.GetName and button:GetName()
    if not name then return end
    local prefix,index=name:match("^(%a+)(%d+)$")
    local command=prefix and nativeBars[prefix]
    index=tonumber(index)
    if command and index<=12 then return command..index, name end
    if command or prefix=="MultiCastActionButton" or prefix=="VehicleMenuBarActionButton" or
        name=="MultiCastSummonSpellButton" or name=="MultiCastRecallSpellButton" then
        return "CLICK "..name..":LeftButton",name,name
    end

    if name:match("^DrakeishUI_SpellBookCard%d+Btn$") then
        local card=button:GetParent()
        local spell=card and card.spellName
        if spell and spell~="" and not card.passive and not card.unlearned and (card.slot or card.spellID) then
            return "SPELL "..spell,spell
        end
    end
    if prefix=="MacroButton" and MacroFrame and MacroFrame.macroBase then
        local macro=GetMacroInfo(MacroFrame.macroBase+button:GetID())
        if macro and macro~="" then return "MACRO "..macro,macro end
    end
end

local function current(key)
    return GetBindingAction(key,false,1) or ""
end

local function effective(key)
    local pending=draft[key]
    if pending then return pending.action end
    return current(key)
end

local function keysFor(command)
    local result={}
    for _, key in ipairs({GetBindingKey(command,1)}) do
        if effective(key)==command then result[#result+1]=key end
    end
    for key, pending in pairs(draft) do
        if pending.action==command then
            local found=false
            for _, listed in ipairs(result) do if key==listed then found=true end end
            if not found then result[#result+1]=key end
        end
    end
    table.sort(result)
    return result
end

local function describe(command)
    return _G["BINDING_NAME_"..command] or command
end

local function showStatus()
    if not target then
        detail:SetText("Hover a button or spell to see its bindings.")
        return
    end
    local keys=keysFor(action)
    local summary=#keys>0 and table.concat(keys,", ") or "Unbound"
    badge:SetText(#keys>0 and keys[1] or "")
    detail:SetText(actionLabel..": "..summary)
end

local function selectTarget(button)
    local command,label,click=actionFor(button)
    if not command or not button:IsVisible() then
        target,action,actionLabel,clickName=nil,nil,nil,nil
        overlay:Hide()
        showStatus()
        return
    end
    if button==target and command==action then return end
    target,action,actionLabel,clickName=button,command,label,click
    overlay:ClearAllPoints()
    overlay:SetAllPoints(target)
    overlay:Show()
    showStatus()
end

local function stage(key,command,click)
    if original[key]==nil then original[key]=current(key) end
    if command==original[key] then draft[key]=nil;original[key]=nil
    else draft[key]={action=command,click=click} end
end

local ignored={LSHIFT=true,RSHIFT=true,LCTRL=true,RCTRL=true,LALT=true,RALT=true,
    SHIFT=true,CTRL=true,ALT=true,UNKNOWN=true}
local mouse={RightButton="BUTTON2",MiddleButton="BUTTON3",Button4="BUTTON4",Button5="BUTTON5"}

local function capture(key)
    if not B.active or not target or confirmation or InCombatLockdown() then return end
    if key=="ESCAPE" then
        for _, bound in ipairs(keysFor(action)) do stage(bound,"") end
        showStatus()
        return
    end
    if not key or key=="LeftButton" or ignored[key] then return end
    key=mouse[key] or key:upper()
    if key=="" or key:find("%c") then return end
    key=(IsAltKeyDown() and "ALT-" or "")..(IsControlKeyDown() and "CTRL-" or "")..
        (IsShiftKeyDown() and "SHIFT-" or "")..key

    local other=effective(key)
    if other~="" and other~=action then
        local data={key=key,action=action,click=clickName}
        confirmation=data
        overlay:Hide()
        if not StaticPopup_Show(conflictDialog,key,describe(other),data) then confirmation=nil end
    else
        stage(key,action,clickName)
        showStatus()
    end
end

StaticPopupDialogs[conflictDialog]={
    text="%s is bound to %s. Replace that binding?",
    button1=YES,button2=NO,
    OnAccept=function(_,data)
        if B.active and not InCombatLockdown() and data==confirmation then
            stage(data.key,data.action,data.click)
            showStatus()
        end
    end,
    OnHide=function() confirmation=nil end,
    timeout=0,whileDead=true,hideOnEscape=true,preferredIndex=3,
}

function B:Discard()
    if not self.active then return end
    self.active=false
    draft,original,bindingSet=nil,nil,nil
    target,action,actionLabel,clickName=nil,nil,nil,nil
    confirmation=nil
    if StaticPopup_Hide then StaticPopup_Hide(conflictDialog) end
    overlay:Hide()
    panel:Hide()
    S:Refresh()
end

function B:Save()
    if not self.active or confirmation or InCombatLockdown() then return end
    if GetCurrentBindingSet()~=bindingSet then
        detail:SetText("The binding set changed. Discard and reopen Keybind Mode.")
        return
    end
    for key, previous in pairs(original) do
        if current(key)~=previous then
            detail:SetText("Bindings changed outside Keybind Mode. Discard and reopen it.")
            return
        end
    end
    local changed={}
    for key, pending in pairs(draft) do
        local ok
        if pending.click then ok=SetBindingClick(key,pending.click,"LeftButton")
        else ok=SetBinding(key,pending.action~="" and pending.action or nil) end
        if not ok then
            for _, applied in ipairs(changed) do
                local previous=original[applied]
                SetBinding(applied,previous~="" and previous or nil)
            end
            detail:SetText("Could not bind "..key..". Use another key or discard the draft.")
            return
        end
        changed[#changed+1]=key
    end
    if #changed>0 then SaveBindings(bindingSet) end
    self:Discard()
    print("DrakeishUI bindings saved.")
end

local function makeButton(text,x,callback)
    local button=CreateFrame("Button",nil,panel,"UIPanelButtonTemplate")
    button:SetSize(122,24)
    button:SetPoint("BOTTOM",x,14)
    button:SetText(text)
    DrakeishUI_SocialNE.buttonskin.Skin(button)
    button:SetScript("OnClick",callback)
end

local function build()
    panel=CreateFrame("Frame","DrakeishUIKeybindMode",UIParent)
    panel:Hide()
    panel:SetFrameStrata("DIALOG")
    panel:SetSize(480,126)
    panel:SetPoint("TOP",UIParent,"TOP",0,-145)
    panel:SetClampedToScreen(true)
    panel:EnableMouse(true)
    DrakeishUI_SocialNE.chrome.Apply(panel,{layout="ButtonFrameTemplateNoPortrait",title="Keybind Mode",noPortrait=true})
    panel.CloseButton:SetScript("OnClick",function() B:Discard() end)
    local help=panel:CreateFontString(nil,"OVERLAY","GameFontHighlightSmall")
    help:SetPoint("TOP",0,-33)
    help:SetText("Hover a button and press a key. Escape clears its bindings.")
    detail=panel:CreateFontString(nil,"OVERLAY","GameFontHighlight")
    detail:SetPoint("TOPLEFT",17,-57)
    detail:SetPoint("RIGHT",-17,0)
    makeButton("Save",-72,function() B:Save() end)
    makeButton("Discard",72,function() B:Discard() end)

    overlay=CreateFrame("Frame",nil,UIParent)
    overlay:Hide()
    overlay:SetFrameStrata("TOOLTIP")
    overlay:EnableMouse(true)
    overlay:EnableKeyboard(true)
    overlay:EnableMouseWheel(true)
    local highlight=overlay:CreateTexture(nil,"BACKGROUND")
    highlight:SetAllPoints()
    highlight:SetTexture(.7,.45,.08,.35)
    badge=overlay:CreateFontString(nil,"OVERLAY","GameFontNormal")
    badge:SetPoint("TOP",0,-2)
    overlay:SetScript("OnKeyDown",function(_,key) capture(key) end)
    overlay:SetScript("OnMouseUp",function(_,key) capture(key) end)
    overlay:SetScript("OnMouseWheel",function(_,delta) capture(delta>0 and "MOUSEWHEELUP" or "MOUSEWHEELDOWN") end)
    panel:SetScript("OnUpdate",function()
        if confirmation then return end
        local focus=GetMouseFocus()
        if focus~=overlay then
            selectTarget(focus)
        elseif not target or not target:IsVisible() then
            selectTarget(nil)
        else
            selectTarget(target)
        end
    end)
    panel:RegisterEvent("PLAYER_REGEN_DISABLED")
    panel:SetScript("OnEvent",function()
        if B.active then B:Discard();print("DrakeishUI keybind draft discarded on entering combat.") end
    end)
end

function B:Start()
    if InCombatLockdown() then print("DrakeishUI Keybind Mode is unavailable in combat.");return end
    if self.active then return end
    bindingSet=GetCurrentBindingSet()
    if bindingSet~=1 and bindingSet~=2 then return end
    if not panel then build() end
    local layout = addon.Layout
    if layout and layout.editing then layout:SetEditing(false) end
    self.active=true
    draft,original={},{}
    S:Close()
    panel:Show()
    showStatus()
end
