local _, addon = ...
local S = addon.Settings
local NE = DrakeishUI_SocialNE
local categories = {
    "General", "Action Bars", "Unit Frames", "Bags", "Chat", "Cast Bar",
    "Buffs", "Minimap", "Tooltips", "Character Panel", "Layout", "Profiles",
}
local WIDTH, HEIGHT, CONTENT_WIDTH = 1080, 676, 781
local CONTROL_WIDTH, CARD_GAP = 152, 9
local category = "Character Panel"
local pageName, tabName, highlighted
local tree, treeContent, tabs, bannerProfile, moveButton, bindButton
local frame, search, heading, crumb, scroll, content, reset, status
local navigation, rows, visibleRows, cards, tabButtons = {}, {}, {}, {}, {}
local pages, routes = {}, {}
local widgets = {}

local function label(parent, font)
    local text = parent:CreateFontString(nil, "ARTWORK", font or "GameFontHighlight")
    text:SetJustifyH("LEFT")
    return text
end

local function button(parent, width, text)
    local item = CreateFrame("Button", nil, parent, "UIPanelButtonTemplate")
    item:SetSize(width, 24)
    item:SetText(text or "")
    NE.buttonskin.Skin(item)
    return item
end

-- Flat items for the section list and the page tabs. Buttons that look like every other button made it hard to
-- see where you were, so these show the open section with a gold bar, a tint and gold text.
local PIXEL = "Interface\\Buttons\\WHITE8X8"
local GOLD = { 1, .84, .25 }
local STYLES = {
    category = { text = { .92, .84, .64 }, active = { .20, .16, .09, .95 }, activeText = GOLD, font = "GameFontNormal", inset = 10 },
    page = { text = { .78, .78, .76 }, active = { .30, .22, .06, .92 }, activeText = { 1, .92, .55 }, font = "GameFontHighlightSmall", inset = 24 },
    tab = { text = { .80, .78, .70 }, active = { .16, .13, .08, .95 }, activeText = GOLD, font = "GameFontHighlightSmall", inset = 0, centre = true },
}
local function paint(item)
    local style = STYLES[item.kind]
    local text = style.text
    local back = { 0, 0, 0, 0 }
    if item.active then
        text, back = style.activeText, style.active
    elseif item.hover and item.enabled then
        back = { .14, .12, .09, .9 }
    end
    if not item.enabled then text = { .5, .5, .5 } end
    item.text:SetTextColor(text[1], text[2], text[3])
    item.back:SetTexture(back[1], back[2], back[3], back[4])
    if item.kind == "tab" then
        item.line:SetHeight(item.active and 2 or 1)
        if item.active then item.line:SetTexture(1, .78, .12, 1) else item.line:SetTexture(.30, .25, .19, 1) end
    elseif item.active then item.bar:Show() else item.bar:Hide() end
end
local function flatItem(parent, width, height, kind)
    local item = CreateFrame("Button", nil, parent)
    item:SetSize(width, height)
    item.kind, item.enabled = kind, true
    item.back = item:CreateTexture(nil, "BACKGROUND")
    item.back:SetAllPoints()
    item.back:SetTexture(0, 0, 0, 0)
    item.text = item:CreateFontString(nil, "ARTWORK", STYLES[kind].font)
    item.text:SetPoint("LEFT", STYLES[kind].inset, 0)
    item.text:SetPoint("RIGHT", -6, 0)
    item.text:SetJustifyH(STYLES[kind].centre and "CENTER" or "LEFT")
    if kind == "tab" then
        item.line = item:CreateTexture(nil, "ARTWORK")
        item.line:SetPoint("BOTTOMLEFT")
        item.line:SetPoint("BOTTOMRIGHT")
    else
        item.bar = item:CreateTexture(nil, "ARTWORK")
        item.bar:SetTexture(1, .78, .12, 1)
        item.bar:SetPoint("TOPLEFT")
        item.bar:SetPoint("BOTTOMLEFT")
        item.bar:SetWidth(3)
        item.rule = item:CreateTexture(nil, "ARTWORK")
        item.rule:SetTexture(.30, .25, .19, 1)
        item.rule:SetPoint("TOPLEFT", 4, 0)
        item.rule:SetPoint("TOPRIGHT", -4, 0)
        item.rule:SetHeight(1)
        item.guide = item:CreateTexture(nil, "ARTWORK")
        item.guide:SetTexture(.45, .36, .15, .8)
        item.guide:SetPoint("TOPLEFT", 12, 0)
        item.guide:SetPoint("BOTTOMLEFT", 12, 0)
        item.guide:SetWidth(1)
    end
    item:SetScript("OnEnter", function(self) self.hover = true; paint(self) end)
    item:SetScript("OnLeave", function(self) self.hover = nil; paint(self) end)
    return item
end
local function setFlat(item, text, active, enabled)
    item.text:SetText(text)
    item.active, item.enabled = active and true or nil, enabled ~= false
    if item.enabled then item:Enable() else item:Disable() end
    paint(item)
end

local function editBox(parent, width)
    local edit = CreateFrame("EditBox", nil, parent, "InputBoxTemplate")
    edit:SetSize(width, 24)
    edit:SetAutoFocus(false)
    edit:SetTextInsets(8, 8, 0, 0)
    local pixel = "Interface\\Buttons\\WHITE8X8"
    edit:SetBackdrop({ bgFile = pixel, edgeFile = pixel, edgeSize = 1 })
    edit:SetBackdropColor(.06, .055, .05, .95)
    edit:SetBackdropBorderColor(.37, .32, .25, 1)
    edit:SetScript("OnEscapePressed", function(self) self:ClearFocus() end)
    return edit
end

local function read(option)
    if option.get then return option.get() end
    return S:Desired(option.id)
end

local function write(option, value)
    if option.set then option.set(value) else S:Set(option.id, value) end
end

local function choices(option)
    if type(option.choices) == "function" then return option.choices() end
    return option.choices
end

local function updateRow(row)
    local option = row.option
    row.title:SetText(option.name or "")
    if row.update then row.update(row, option) end
end

widgets.toggle = function(row)
    local check = CreateFrame("CheckButton", nil, row, "UICheckButtonTemplate")
    check:SetSize(28, 28)
    check:SetPoint("RIGHT", -8, 0)
    check:SetScript("OnClick", function(self)
        write(row.option, not not self:GetChecked())
    end)
    row.update = function(_, option) check:SetChecked(read(option)) end
end

-- A dropdown field: dark input-style box, the chosen value on the left, an arrow cap on the right
-- that points down and turns up while the menu is open. No text arrows, no push-button look.
local FIELD_BG, FIELD_EDGE, FIELD_HOVER = { .06, .055, .05, .95 }, { .37, .32, .25, 1 }, { .85, .68, .22, 1 }
local CAP_WIDTH = 22
local ARROW = "Interface\\Buttons\\UI-SortArrow"

local function dropdownField(parent, width)
    local field = CreateFrame("Button", nil, parent)
    field:SetSize(width, 24)
    local pixel = "Interface\\Buttons\\WHITE8X8"
    field:SetBackdrop({ bgFile = pixel, edgeFile = pixel, edgeSize = 1 })
    field:SetBackdropColor(unpack(FIELD_BG))
    field:SetBackdropBorderColor(unpack(FIELD_EDGE))
    local cap = field:CreateTexture(nil, "ARTWORK")
    cap:SetTexture(.14, .115, .085, 1)
    cap:SetPoint("TOPRIGHT", -1, -1)
    cap:SetPoint("BOTTOMRIGHT", -1, 1)
    cap:SetWidth(CAP_WIDTH)
    local divider = field:CreateTexture(nil, "OVERLAY")
    divider:SetTexture(unpack(FIELD_EDGE))
    divider:SetPoint("TOPRIGHT", cap, "TOPLEFT", 0, 0)
    divider:SetPoint("BOTTOMRIGHT", cap, "BOTTOMLEFT", 0, 0)
    divider:SetWidth(1)
    local arrow = field:CreateTexture(nil, "OVERLAY")
    arrow:SetTexture(ARROW)
    arrow:SetSize(12, 12)
    arrow:SetVertexColor(.8, .72, .5)
    arrow:SetPoint("CENTER", cap, "CENTER", 0, 0)
    field.arrow = arrow
    local text = field:CreateFontString(nil, "ARTWORK", "GameFontHighlightSmall")
    text:SetJustifyH("LEFT")
    text:SetPoint("LEFT", 8, 0)
    text:SetPoint("RIGHT", cap, "LEFT", -6, 0)
    text:SetHeight(14)
    field.text = text
    function field:SetOpenArrow(open)
        if open then arrow:SetTexCoord(0, .5625, 1, 0) else arrow:SetTexCoord(0, .5625, 0, 1) end
    end
    field:SetOpenArrow(false)
    field:SetScript("OnEnter", function(self) self:SetBackdropBorderColor(unpack(FIELD_HOVER)); self.arrow:SetVertexColor(1, .85, .35) end)
    field:SetScript("OnLeave", function(self) self:SetBackdropBorderColor(unpack(FIELD_EDGE)); self.arrow:SetVertexColor(.8, .72, .5) end)
    field:SetScript("OnDisable", function(self) self.text:SetAlpha(.5); self.arrow:SetAlpha(.5) end)
    field:SetScript("OnEnable", function(self) self.text:SetAlpha(1); self.arrow:SetAlpha(1) end)
    return field
end

local function watchOpen(field)
    field:SetOpenArrow(true)
    field:SetScript("OnUpdate", function(self)
        local menu = _G.DragonUIMenu
        if not (menu and menu:IsShown() and menu.anchor == self) then
            self:SetOpenArrow(false)
            self:SetScript("OnUpdate", nil)
        end
    end)
end

local function choiceWidget(row, preview)
    local selectButton = dropdownField(row, CONTROL_WIDTH)
    selectButton:SetPoint("RIGHT", -8, 0)
    selectButton:SetScript("OnClick", function(self)
        local option = row.option
        local value = read(option)
        local entries = {}
        for _, choice in ipairs(choices(option)) do
            local selectedValue = choice.value
            entries[#entries + 1] = {
                text = choice.name, checked = selectedValue == value,
                func = function() write(option, selectedValue); updateRow(row) end,
            }
        end
        addon.Menu.Open(self, entries)
        watchOpen(self)
    end)
    row.update = function(_, option)
        local value = read(option)
        selectButton.text:SetFontObject(GameFontHighlightSmall)
        for _, choice in ipairs(choices(option)) do
            if choice.value == value then
                selectButton.text:SetText(choice.name)
                if preview and choice.path then selectButton.text:SetFont(choice.path, 12, "") end
                return
            end
        end
        selectButton.text:SetText("Choose...")
    end
end

widgets.dropdown = function(row) choiceWidget(row, false) end
widgets.font = function(row) choiceWidget(row, true) end

widgets.slider = function(row)
    local edit = editBox(row, 48)
    edit:SetPoint("RIGHT", -8, 0)
    local slider = CreateFrame("Slider", nil, row)
    slider:SetOrientation("HORIZONTAL")
    slider:SetSize(92, 16)
    slider:SetPoint("RIGHT", edit, "LEFT", -12, 0)
    local track = slider:CreateTexture(nil, "BACKGROUND")
    track:SetTexture(0.24, 0.22, 0.19, 1)
    track:SetPoint("LEFT", 1, 0)
    track:SetPoint("RIGHT", -1, 0)
    track:SetHeight(4)
    slider:SetThumbTexture("Interface\\Buttons\\UI-SliderBar-Button-Horizontal")

    edit:SetScript("OnEnterPressed", function(self)
        local value = tonumber(self:GetText())
        if value and value == value and math.abs(value) < math.huge then write(row.option, value) end
        self:ClearFocus()
        updateRow(row)
    end)
    edit:SetScript("OnEditFocusLost", function(self) self:SetText(tostring(read(row.option))) end)
    row.ClearEditFocus = function() edit:ClearFocus() end
    slider:SetScript("OnValueChanged", function(_, value)
        if not row.updatingSlider and row.option then write(row.option, value) end
    end)
    row.update = function(_, option)
        -- Native sliders fire OnValueChanged during range, step and value updates.
        -- A pooled row may still hold the previous setting's thumb position.
        local value = read(option)
        row.updatingSlider = true
        slider:SetMinMaxValues(option.min, option.max)
        slider:SetValueStep(option.step or 1)
        slider:SetValue(value)
        row.updatingSlider = nil
        if not edit:HasFocus() then edit:SetText(tostring(value)) end
    end
end

widgets.colour = function(row)
    -- A flat swatch: the colour fills the whole control and a thin edge keeps dark colours visible. The red
    -- button art showed around an inset preview.
    local pick = CreateFrame("Button", nil, row)
    pick:SetSize(60, 24)
    pick:SetPoint("RIGHT", -8, 0)
    pick:SetBackdrop({ bgFile = PIXEL, edgeFile = PIXEL, edgeSize = 1 })
    local function border(hover)
        if not pick:IsEnabled() then pick:SetBackdropBorderColor(.24, .23, .21, 1)
        elseif hover then pick:SetBackdropBorderColor(.85, .68, .22, 1)
        else pick:SetBackdropBorderColor(.37, .32, .25, 1) end
    end
    pick:SetScript("OnEnter", function() border(true) end)
    pick:SetScript("OnLeave", function() border(false) end)
    pick:SetScript("OnClick", function()
        if not pick:IsEnabled() then return end
        local option = row.option
        local original = read(option)
        local picker = ColorPickerFrame
        picker:Hide()
        picker.func, picker.opacityFunc = nil, nil
        picker.hasOpacity = false
        picker:SetColorRGB(unpack(original))
        picker.previousValues = original
        picker.func = function() write(option, { picker:GetColorRGB() }) end
        picker.cancelFunc = function() write(option, original) end
        picker:Show()
    end)
    row.update = function(_, option)
        local rgb = read(option)
        if option.disabled and option.disabled() then pick:Disable(); pick:SetAlpha(.55)
        else pick:Enable(); pick:SetAlpha(1) end
        pick:SetBackdropColor(rgb[1], rgb[2], rgb[3], 1)
        border(pick:IsMouseOver())
    end
end

widgets.text = function(row)
    local edit = editBox(row, CONTROL_WIDTH)
    edit:SetPoint("RIGHT", -8, 0)
    edit:SetScript("OnEnterPressed", function(self)
        write(row.option, self:GetText())
        self:ClearFocus()
        updateRow(row)
    end)
    edit:SetScript("OnEditFocusLost", function(self)
        if row.option then self:SetText(read(row.option) or "") end
    end)
    edit:SetScript("OnTextChanged", function(self, userInput)
        if userInput and row.option and row.option.live then write(row.option, self:GetText()) end
    end)
    row.ClearEditFocus = function() edit:ClearFocus() end
    row.update = function(_, option)
        edit:SetMaxLetters(option.maxLength or 64)
        if not edit:HasFocus() then edit:SetText(read(option) or "") end
    end
end

widgets.button = function(row)
    local action = button(row, CONTROL_WIDTH)
    action:SetPoint("RIGHT", -8, 0)
    action:SetScript("OnClick", function() row.option.action() end)
    row.update = function(_, option)
        action:SetText(option.buttonText or option.name)
        if option.disabled and option.disabled() then action:Disable() else action:Enable() end
    end
end

widgets.header = function(row) row.title:SetFontObject(GameFontNormal) end
widgets.description = function(row) row.title:SetFontObject(GameFontHighlightSmall) end

local profileMessage, profileName, copyFrom = "", "", nil
local profileText = ""
local function profileChoices()
    local available = {}
    for _, choice in ipairs(S:Profiles()) do
        if choice.value ~= S:Active() then available[#available + 1] = choice end
    end
    return available
end

local profileRows = {
    { type = "dropdown", name = "Active profile", choices = function() return S:Profiles() end,
      get = function() return S:Active() end,
      set = function(value) profileMessage, copyFrom = "", nil; S:Select(value) end },
    { type = "header", name = "Shared profiles" },
    { type = "description", name = "Changes to a shared profile apply to every character using it." },
    { type = "text", name = "New profile name", maxLength = 32, live = true, get = function() return profileName end,
      set = function(value)
          profileName, profileMessage = value, ""
          S:Refresh()
      end },
    { type = "button", buttonText = "Create shared profile", action = function()
          local errorMessage = S:CreateShared(profileName)
          profileMessage = errorMessage or "Shared profile created."
          if not errorMessage then profileName, copyFrom = "", nil end
          S:Refresh()
      end },
    { type = "header", name = "Copy and reset" },
    { type = "dropdown", name = "Copy from", choices = profileChoices, get = function() return copyFrom end,
      set = function(value) copyFrom, profileMessage = value, ""; S:Refresh() end },
    { type = "button", buttonText = "Copy into active profile",
      disabled = function() return copyFrom == nil or copyFrom == S:Active() end,
      action = function()
          S:CopyFrom(copyFrom)
          profileMessage = "Settings copied to the active profile."
          S:Refresh()
      end },
    { type = "button", buttonText = "Reset active profile",
      action = function() StaticPopup_Show("DRAKEISHUI_SETTINGS_RESET_PROFILE", nil, nil, { active = S:Active() }) end },
    { type = "header", name = "Export and import" },
    { type = "text", name = "Profile text", maxLength = 16384, live = true,
      get = function() return profileText end,
      set = function(value) profileText = value end },
    { type = "button", buttonText = "Export active profile",
      action = function()
          local result, message = S:ExportProfile()
          if result then profileText = result end
          profileMessage = message or "Copy the profile text to share these settings."
          S:Refresh()
      end },
    { type = "button", buttonText = "Import as new profile",
      action = function()
          local message = S:ImportProfile(profileName,profileText)
          if not message then profileName, copyFrom, profileText = "", nil, "" end
          profileMessage = message or "Imported and selected the new shared profile."
          S:Refresh()
      end },
}

StaticPopupDialogs.DRAKEISHUI_SETTINGS_RESET_PROFILE = {
    text = "Restore the default settings for the active DrakeishUI profile?",
    button1 = YES, button2 = NO,
    OnAccept = function(_, data)
        if S:Active() ~= data.active then return end
        S:Reset()
        profileMessage = "Active profile reset."
        S:Refresh()
    end,
    timeout = 0, whileDead = true, hideOnEscape = true, preferredIndex = 3,
}

local function rowFor(option, index)
    local kind = option.type
    rows[kind] = rows[kind] or {}
    local row = rows[kind][index]
    if not row then
        row = CreateFrame("Frame", nil, content)
        row:SetHeight(32)
        row:EnableMouse(true)
        row.title = label(row)
        row.title:SetPoint("LEFT", 8, 0)
        row.title:SetPoint("RIGHT", (kind == "header" or kind == "description") and -8 or -CONTROL_WIDTH-16, 0)
        local highlight = row:CreateTexture(nil,"BACKGROUND")
        highlight:SetTexture(.30,.20,.07,.7)
        highlight:SetAllPoints()
        row.highlight = highlight
        row:SetScript("OnEnter", function(self)
            local option = self.option
            if not GameTooltip or not option or not option.id then return end
            GameTooltip:SetOwner(self,"ANCHOR_RIGHT")
            GameTooltip:SetText(option.name)
            if option.desc then GameTooltip:AddLine(option.desc,1,1,1,true) end
            if option.min then
                GameTooltip:AddLine(("Range: %s to %s"):format(option.min,option.max),.8,.8,.8)
            end
            GameTooltip:AddLine(option.id,.65,.65,.65)
            GameTooltip:Show()
        end)
        row:SetScript("OnLeave",function() if GameTooltip then GameTooltip:Hide() end end)
        widgets[kind](row)
        rows[kind][index] = row
    end
    return row
end

local function refresh()
    if not frame or not frame:IsShown() then return end
    for _, row in ipairs(visibleRows) do updateRow(row) end

    local message = category == "Profiles" and profileMessage or ""
    if not S:IsReady() then
        message = "Settings are unavailable. Check the saved profile version and character login."
    elseif next(S.pending) then
        message = "Protected changes will apply when combat ends."
    end
    status:SetText(message)
    local layout = addon.Layout
    if layout and layout.editing then
        moveButton:SetText("Lock UI")
        moveButton:LockHighlight()
    else
        moveButton:SetText("Move UI")
        moveButton:UnlockHighlight()
    end
    local binding=addon.KeybindMode
    if binding and binding.active then bindButton:LockHighlight() else bindButton:UnlockHighlight() end
    bannerProfile:SetText(S:IsReady() and (S:Active() or "This Character") or "")
    local r,g,b
    if addon.Appearance then r,g,b=addon.Appearance:GetAccent() end
    heading:SetTextColor(r or 1,g or .82,b or 0)
end

local function matches(option, query)
    local text=table.concat({option.name,option.id,option.category,option.group or "",option.keywords or ""}," "):lower()
    for word in query:gmatch("%S+") do
        if not text:find(word,1,true) then return false end
    end
    return true
end

local barTitles={main="Main bar",bottomLeft="Bottom left",bottomRight="Bottom right",
    right="Right bar",left="Left bar",pet="Pet",stance="Stance",totem="Totem",vehicle="Vehicle",bags="Bags / Micro"}
local unitTitles={player="Player",target="Target",targettarget="Target of Target",focus="Focus",focustarget="Focus Target",pet="Pet",party="Party",
    raid="Raid",raidThin="Raid",boss="Boss",arena="Arena"}
local unitText={font=true,size=true,outline=true,align=true,healthColor=true,powerColor=true}
local function route(option)
    local id, category, group=option.id,option.category,option.group
    if category=="General" then return category,"Appearance and Fonts",nil,group end
    if category=="Character Panel" then return category,"General",nil,group end
    if category=="Action Bars" then
        local bar=id:match("^cooldown%.([^.]+)%.")
        if bar then return category,"Per-bar Cooldowns",barTitles[bar] or bar,"Text style" end
        if id:match("^cooldown%.") then return category,"Cooldowns",nil,group end
        bar=id:match("^layout%.([^.]+)%.")
        if bar then return category,"Size and Layout",barTitles[bar] or bar,"Button geometry" end
        return category,"General",nil,group
    end
    if category=="Unit Frames" or category=="Party and Raid" then
        local unit,key=id:match("^unit%.([^.]+)%.(.+)$")
        if unitTitles[unit] then
            if group == "Bar Size" then
                local section = unit == "raidThin" and "Thin raid" or unit == "raid" and "Full raid" or group
                return "Unit Frames",unitTitles[unit],"Bar Size",section
            end
            if key == "portrait" then return "Unit Frames",unitTitles[unit],"Portrait","Portrait" end
            local tab=unitText[key] and "Text" or "Values"
            local section=unit=="raidThin" and "Thin raid" or unit=="raid" and "Full raid" or nil
            return "Unit Frames",unitTitles[unit],tab,section or (unitText[key] and "Text style" or "Visibility and format")
        end
        if category=="Party and Raid" then return "Unit Frames","Raid","Layout",group end
        return "Unit Frames","General",nil,group
    end
    if category=="Chat" then
        local slot=id:match("^footer%.(%d)%.")
        if slot then return category,"Footer Slots","Slot "..slot,"Readout" end
        if id:match("^footer%.") then return category,"Footer Slots","General","Footer" end
        if id:match("^chat%.tab") then return category,"Tabs",nil,"Tabs" end
        if id=="chat.width" or id=="chat.height" then return category,"Size",nil,"Dimensions" end
        if id=="chat.time" or id=="chat.class" or id=="chat.shorten" then
            return category,"Timestamps",nil,"Message formatting"
        end
        return category,"Appearance",nil,group
    end
    if category=="Tooltips" and id:match("^tooltip%.") then return category,"Tooltips",nil,group end
    if category=="Bags" then
        local name=id:match("^bagQuality%.level") and "Item Level" or
            (id:match("^layout%.") and "Layout" or id:match("^appearance%.") and "Text" or "Quality")
        return category,name,nil,group
    end
    return category,group or "General",nil,group
end

local function pageFor(category,name)
    pages[category]=pages[category] or {list={},byName={}}
    local collection=pages[category]
    if not collection.byName[name] then
        local page={name=name,tabs={},byTab={}}
        collection.list[#collection.list+1]=page
        collection.byName[name]=page
    end
    return collection.byName[name]
end

local function addToPage(option,category,name,tab,group)
    local page=pageFor(category,name)
    tab=tab or ""
    if not page.byTab[tab] then
        page.byTab[tab]={name=tab,groups={},byGroup={}}
        page.tabs[#page.tabs+1]=page.byTab[tab]
    end
    local tabData=page.byTab[tab]
    group=group or "Options"
    if not tabData.byGroup[group] then
        tabData.byGroup[group]={title=group,options={}}
        tabData.groups[#tabData.groups+1]=tabData.byGroup[group]
    end
    local items=tabData.byGroup[group].options
    items[#items+1]=option
    if option.id then routes[option.id]={category,name,tab} end
end

local function populate()
    for _, option in ipairs(S.options) do addToPage(option,route(option)) end
    local exchange=false
    local group
    for _, option in ipairs(profileRows) do
        if option.type=="header" then
            group=option.name
            if group=="Export and import" then exchange=true end
        else
            addToPage(option,"Profiles",exchange and "Export and Import" or "Manage",nil,group or "Active profile")
            if option.name=="New profile name" then
                addToPage(option,"Profiles","Export and Import",nil,"Export and import")
            end
        end
    end
    addToPage({type="button",name="Key bindings",buttonText="Keybind Mode",action=function()
        if addon.KeybindMode then addon.KeybindMode:Start() end
    end},"Action Bars","General",nil,"Key bindings")
end

local draw
local function windowState()
    return S:IsReady() and addon.Layout and addon.Layout.DB and addon.Layout:DB()
end
local function remember()
    local db=windowState()
    if db then db.settingsPage={category=category,page=pageName,tab=tabName} end
end

local function selectPage(section,page,tab,match)
    category,pageName,tabName,highlighted=section,page,tab,match
    search:ClearFocus()
    if search:GetText()~="" then search:SetText("") else draw() end
end

local function drawNavigation()
    local used=0
    local function add(text,kind,active,action,first)
        used=used+1
        local item=navigation[used]
        if not item or item.kind~=kind then
            if item then item:Hide() end
            item=flatItem(treeContent,184,27,kind)
            navigation[used]=item
        end
        item:ClearAllPoints()
        item:SetPoint("TOPLEFT", treeContent,"TOPLEFT",0,-(used-1)*27)
        setFlat(item,text,active,action~=nil)
        item:SetScript("OnClick",action)
        if item.rule then if kind=="category" and not first then item.rule:Show() else item.rule:Hide() end end
        if item.guide then if kind=="page" then item.guide:Show() else item.guide:Hide() end end
        item:Show()
    end
    local categoryY
    for _, name in ipairs(categories) do
        local collection=pages[name]
        if name==category then categoryY=used*27 end
        add(name,"category",name==category,collection and function()
            if name==category and pageName then
                pageName=nil; tabName=nil; highlighted=nil
                if search:GetText()~="" then search:SetText("") else draw() end
            else selectPage(name,collection.list[1].name) end
        end or nil,name==categories[1])
        if name==category and pageName and collection then
            for _, page in ipairs(collection.list) do
                local key=page.name
                add(key,"page",key==pageName,function() selectPage(name,key) end)
            end
        end
    end
    for i=used+1,#navigation do navigation[i]:Hide() end
    treeContent:SetHeight(math.max(1,used*27))
    if categoryY then
        local current=tree.GetVerticalScroll and tree:GetVerticalScroll() or 0
        if categoryY<current or categoryY+27>current+tree:GetHeight() then
            tree:SetVerticalScroll(math.max(0,categoryY-27))
        end
    end
end

local function drawTabs(page)
    local used=0
    if page and #page.tabs>1 then
        for i,item in ipairs(page.tabs) do
            used=i
            local tab=tabButtons[i]
            if not tab then tab=flatItem(tabs,108,24,"tab");tabButtons[i]=tab end
            local name=item.name
            tab:ClearAllPoints()
            tab:SetPoint("TOPLEFT",tabs,"TOPLEFT",((i-1)%7)*112,-math.floor((i-1)/7)*28)
            setFlat(tab,name,name==tabName)
            tab:SetScript("OnClick",function() selectPage(category,pageName,name) end)
            tab:Show()
        end
    end
    for i=used+1,#tabButtons do tabButtons[i]:Hide() end
    scroll:ClearAllPoints()
    scroll:SetPoint("TOPLEFT",tabs,"TOPLEFT",-5,-8-math.ceil(used/7)*28)
    scroll:SetPoint("BOTTOMRIGHT",tabs:GetParent(),"BOTTOMRIGHT",-24,48)
end

draw=function()
    addon.Menu.Close()
    for _, row in ipairs(visibleRows) do
        if row.ClearEditFocus then row.ClearEditFocus() end
        row:Hide()
    end
    for _, card in ipairs(cards) do card:Hide() end
    wipe(visibleRows)
    local query=search:GetText():lower():match("^%s*(.-)%s*$")
    local collection=pages[category]
    local page=collection and collection.byName[pageName]
    if page and not page.byTab[tabName] then tabName=page.tabs[1].name end
    local used, y, cardCount, jump={},0,0,nil

    local function addRow(option,x,rowY,width)
        local kind=option.type
        used[kind]=(used[kind] or 0)+1
        local row=rowFor(option,used[kind])
        row.option=option
        row:ClearAllPoints()
        row:SetWidth(width)
        row:SetPoint("TOPLEFT",content,"TOPLEFT",x,-rowY)
        if option.id and option.id==highlighted then row.highlight:Show();jump=rowY
        else row.highlight:Hide() end
        row:Show()
        visibleRows[#visibleRows+1]=row
    end
    local function addCard(group)
        cardCount=cardCount+1
        local card=cards[cardCount]
        if not card then
            card=CreateFrame("Frame",nil,content)
            card:SetBackdrop({bgFile="Interface\\Buttons\\WHITE8X8",edgeFile="Interface\\Buttons\\WHITE8X8",edgeSize=1})
            card:SetBackdropColor(.12,.105,.085,.88)
            card:SetBackdropBorderColor(.30,.25,.19,1)
            card:SetFrameLevel(content:GetFrameLevel())
            card.title=label(card,"GameFontNormal")
            card.title:SetPoint("TOPLEFT",10,-9)
            card.rule=card:CreateTexture(nil,"ARTWORK")
            card.rule:SetTexture(.45,.36,.15,.8)
            card.rule:SetPoint("TOPLEFT",8,-25)
            card.rule:SetPoint("TOPRIGHT",-8,-25)
            card.rule:SetHeight(1)
            cards[cardCount]=card
        end
        card.title:SetText(group.title)
        card:ClearAllPoints()
        card:SetPoint("TOPLEFT",content,"TOPLEFT",0,-y)
        card:Show()
        local column=0
        local start=y
        y=y+28
        local width=(CONTENT_WIDTH-2*CARD_GAP)/2
        for _, option in ipairs(group.options) do
            if option.type=="description" or option.name=="Profile text" then
                if column==1 then y=y+34;column=0 end
                addRow(option,CARD_GAP,y,CONTENT_WIDTH-2*CARD_GAP)
                y=y+34
            else
                addRow(option,CARD_GAP+column*width,y,width)
                column=column+1
                if column==2 then y=y+34;column=0 end
            end
        end
        if column==1 then y=y+34 end
        card:SetSize(CONTENT_WIDTH,y-start+5)
        y=y+14
    end

    if S:IsReady() and query~="" then
        local results={}
        for _, option in ipairs(S.options) do
            if matches(option,query) then
                local path=routes[option.id]
                if #results<100 then
                    local open=function() selectPage(path[1],path[2],path[3],option.id) end
                    results[#results+1]={type="button",name=option.name,buttonText=path[2],action=open,id=option.id}
                end
            end
        end
        search.firstResult=results[1] and results[1].action
        if #results==0 then addRow({type="description",name="No matching settings."},0,0,CONTENT_WIDTH);y=34
        else addCard({title="Matching settings",options=results}) end
    elseif S:IsReady() and page then
        search.firstResult=nil
        for _, group in ipairs(page.byTab[tabName].groups) do addCard(group) end
        remember()
    else
        search.firstResult=nil
        if S:IsReady() and collection then
            for _, item in ipairs(collection.list) do
                addRow({type="button",name=item.name,buttonText="Open",action=function() selectPage(category,item.name) end},
                    0,y,CONTENT_WIDTH)
                y=y+34
            end
        end
    end
    heading:SetText(query~="" and "Search results" or (pageName or category))
    crumb:SetText(query=="" and pageName and category or "")
    drawNavigation()
    drawTabs(query=="" and page or nil)
    reset.category=category
    if category=="Unit Frames" and (pageName=="Party" or pageName=="Raid") then reset.category="Party and Raid" end
    if S:IsReady() and query=="" and page and category~="Profiles" and y>0 then reset:Show() else reset:Hide() end
    content:SetHeight(math.max(1,y))
    scroll:SetVerticalScroll(jump and math.min(math.max(0,y-scroll:GetHeight()),math.max(0,jump-36)) or 0)
    refresh()
end

local function inset(parent)
    local panel = CreateFrame("Frame", nil, parent)
    panel:SetFrameLevel(parent:GetFrameLevel() + 3)
    local bg = panel:CreateTexture(nil, "BACKGROUND")
    bg:SetTexture(0.04, 0.04, 0.05, 0.90)
    bg:SetAllPoints()
    NE.nineslice.AttachInset(panel)
    return panel
end

local function fit()
    local width, height = UIParent:GetWidth(), UIParent:GetHeight()
    if width > 0 and height > 0 then
        frame:SetScale(math.min(1, width * .95 / WIDTH, height * .95 / HEIGHT))
    end
end

local function build()
    frame = CreateFrame("Frame", "DrakeishUISettingsWindow", UIParent)
    frame:Hide()
    frame:SetFrameStrata("DIALOG")
    frame:SetSize(WIDTH, HEIGHT)
    frame:SetPoint("CENTER")
    frame:EnableMouse(true)
    frame:SetClampedToScreen(true)
    frame:SetMovable(true)
    tinsert(UISpecialFrames, frame:GetName())
    NE.chrome.Apply(frame, { layout = "ButtonFrameTemplateNoPortrait", title = "DrakeishUI Settings", noPortrait = true })
    frame.CloseButton:SetScript("OnClick", function() frame:Hide() end)
    local drag = CreateFrame("Frame", nil, frame)
    drag:SetFrameLevel(frame:GetFrameLevel() + 12)
    drag:SetPoint("TOPLEFT", 12, -2)
    drag:SetPoint("TOPRIGHT", -32, -2)
    drag:SetHeight(22)
    drag:EnableMouse(true)
    drag:RegisterForDrag("LeftButton")
    drag:SetScript("OnDragStart", function() frame:StartMoving() end)
    drag:SetScript("OnDragStop", function() frame:StopMovingOrSizing() end)

    search = editBox(frame, 225)
    search:SetFrameLevel(frame:GetFrameLevel() + 6)
    search:SetPoint("TOPRIGHT", -27, -72)
    search:SetMaxLetters(80)
    local placeholder = label(search, "GameFontDisable")
    placeholder:SetPoint("LEFT", 8, 0)
    placeholder:SetText("Search settings")
    search:SetScript("OnTextChanged", function(self)
        if self:GetText() == "" then placeholder:Show() else placeholder:Hide() end
        if frame:IsShown() then draw() end
    end)
    search:SetScript("OnEnterPressed", function(self)
        self:ClearFocus()
        if self.firstResult then self.firstResult() end
    end)

    local banner=inset(frame)
    banner:SetPoint("TOPLEFT",frame,"TOPLEFT",19,-29)
    banner:SetPoint("TOPRIGHT",frame,"TOPRIGHT",-18,-29)
    banner:SetHeight(34)
    moveButton=button(banner,110,"Move UI")
    moveButton:SetPoint("LEFT",7,0)
    moveButton:SetScript("OnClick",function()
        if addon.Layout and not InCombatLockdown() then addon.Layout:SetEditing(not addon.Layout.editing) end
        refresh()
    end)
    bindButton=button(banner,132,"Keybind Mode")
    bindButton:SetPoint("LEFT",moveButton,"RIGHT",7,0)
    bindButton:SetScript("OnClick",function() if addon.KeybindMode then addon.KeybindMode:Start() end end)
    local resetPositions=button(banner,135,"Reset Positions")
    resetPositions:SetPoint("LEFT",bindButton,"RIGHT",7,0)
    resetPositions:SetScript("OnClick",function()
        if addon.Layout and not InCombatLockdown() then StaticPopup_Show("DRAKEISHUI_RESET_POSITIONS") end
    end)
    bannerProfile=label(banner)
    bannerProfile:SetPoint("RIGHT",-12,0)

    local sidebar=inset(frame)
    sidebar:SetPoint("TOPLEFT",19,-106)
    sidebar:SetPoint("BOTTOMLEFT",19,18)
    sidebar:SetWidth(220)
    tree=CreateFrame("ScrollFrame",nil,sidebar)
    tree:SetPoint("TOPLEFT",8,-7)
    tree:SetPoint("BOTTOMRIGHT",-22,7)
    treeContent=CreateFrame("Frame",nil,tree)
    treeContent:SetSize(184,1)
    tree:SetScrollChild(treeContent)
    NE.scrollbar.BuildCustomPixel(tree)

    local pane = inset(frame)
    pane:SetPoint("TOPLEFT", sidebar, "TOPRIGHT", 8, 0)
    pane:SetPoint("BOTTOMRIGHT", frame, "BOTTOMRIGHT", -18, 18)
    crumb = label(pane, "GameFontHighlightSmall")
    crumb:SetPoint("TOPLEFT", 17, -10)
    crumb:SetTextColor(.62, .56, .44)
    heading = label(pane, "GameFontNormalLarge")
    heading:SetPoint("TOPLEFT", 17, -23)
    tabs=CreateFrame("Frame",nil,pane)
    tabs:SetPoint("TOPLEFT",15,-52)
    tabs:SetPoint("TOPRIGHT",-24,-52)
    tabs:SetHeight(56)
    scroll = CreateFrame("ScrollFrame", nil, pane)
    content = CreateFrame("Frame", nil, scroll)
    content:SetSize(CONTENT_WIDTH, 1)
    scroll:SetScrollChild(content)
    NE.scrollbar.BuildCustomPixel(scroll)

    reset = button(pane, 148, "Reset this section")
    reset:SetPoint("BOTTOMRIGHT", -13, 12)
    reset:SetScript("OnClick", function() S:Reset(reset.category) end)
    status = label(pane, "GameFontHighlightSmall")
    status:SetPoint("BOTTOMLEFT", 15, 13)
    status:SetWidth(363)
    frame:SetScript("OnShow", function() fit(); draw() end)
    frame:SetScript("OnHide", function()
        frame:StopMovingOrSizing()
        search:ClearFocus()
        for _, row in ipairs(visibleRows) do
            if row.ClearEditFocus then row.ClearEditFocus() end
        end
        addon.Menu.Close()
    end)
    frame:RegisterEvent("DISPLAY_SIZE_CHANGED")
    frame:RegisterEvent("UI_SCALE_CHANGED")
    frame:SetScript("OnEvent", fit)
    populate()
    local db=windowState()
    local saved=db and db.settingsPage
    if type(saved)=="table" and pages[saved.category] and pages[saved.category].byName[saved.page] then
        category,pageName,tabName=saved.category,saved.page,saved.tab
    end
    pageName=pageName or (pages[category] and pages[category].list[1].name)
    if addon.Layout and addon.Layout.SetEditing then hooksecurefunc(addon.Layout,"SetEditing",refresh) end
    S.onRefresh = refresh
end

StaticPopupDialogs.DRAKEISHUI_RESET_POSITIONS={
    text="Reset the positions and scales of movable frames?",
    button1=YES,button2=NO,
    OnAccept=function() if addon.Layout and not InCombatLockdown() then addon.Layout:ResetBars() end end,
    timeout=0,whileDead=true,hideOnEscape=true,preferredIndex=3,
}

function S:Close()
    if frame then frame:Hide() end
end

function S:Open(section)
    self:Ready()
    if not frame then build() end
    if section and pages[section] then
        category,pageName,tabName,highlighted=section,pages[section].list[1].name,nil,nil
        if search:GetText()~="" then
            search:SetText("")
            if frame:IsShown() then return end
        end
    end
    if frame:IsShown() then draw() else frame:Show() end
end
